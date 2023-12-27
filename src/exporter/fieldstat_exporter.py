#!/usr/bin/python3

import argparse
import sys
import json
import re
import os
import logging
import shutil
import datetime
import time
import copy
import fcntl
from prettytable import PrettyTable,NONE,HEADER
from http.server import HTTPServer, BaseHTTPRequestHandler
from jinja2 import Environment, FileSystemLoader

import ctypes

class FieldstatAPI:
    libfieldstat = ctypes.CDLL('libfieldstat4.so')
    libfieldstat.fieldstat_histogram_base64_decode.argtypes       = [ctypes.c_char_p]
    libfieldstat.fieldstat_histogram_base64_decode.restype        =  ctypes.c_void_p

    libfieldstat.fieldstat_histogram_free.argtypes                = [ctypes.c_void_p]

    libfieldstat.fieldstat_histogram_value_at_percentile.argtypes = [ctypes.c_void_p, ctypes.c_double]
    libfieldstat.fieldstat_histogram_value_at_percentile.restype  =  ctypes.c_longlong

    libfieldstat.fieldstat_histogram_count_le_value.argtypes      = [ctypes.c_void_p, ctypes.c_longlong]
    libfieldstat.fieldstat_histogram_count_le_value.restype       =  ctypes.c_longlong

    libfieldstat.fieldstat_histogram_value_total_count.argtypes   = [ctypes.c_void_p]
    libfieldstat.fieldstat_histogram_value_total_count.restype    =  ctypes.c_longlong

    libfieldstat.fieldstat_histogram_value_min.argtypes           = [ctypes.c_void_p]
    libfieldstat.fieldstat_histogram_value_min.restype            =  ctypes.c_longlong

    libfieldstat.fieldstat_histogram_value_max.argtypes           = [ctypes.c_void_p]
    libfieldstat.fieldstat_histogram_value_max.restype            =  ctypes.c_longlong

    libfieldstat.fieldstat_histogram_value_mean.argtypes          = [ctypes.c_void_p]
    libfieldstat.fieldstat_histogram_value_mean.restype           =  ctypes.c_double

    libfieldstat.fieldstat_histogram_value_stddev.argtypes        = [ctypes.c_void_p]
    libfieldstat.fieldstat_histogram_value_stddev.restype         =  ctypes.c_double

    libfieldstat.fieldstat_hll_base64_to_count.argtypes           = [ctypes.c_char_p]
    libfieldstat.fieldstat_hll_base64_to_count.restype            =  ctypes.c_double

    libfieldstat.fieldstat_histogram_value_sum.argtypes           = [ctypes.c_void_p]
    libfieldstat.fieldstat_histogram_value_sum.restype            =  ctypes.c_longlong

    libfieldstat.fieldstat_is_hll.argtypes                        = [ctypes.c_char_p]
    libfieldstat.fieldstat_is_hll.restype                         =  ctypes.c_bool

logging.basicConfig(level=logging.DEBUG, format='%(asctime)s - %(message)s')

################################################################################
#  global variables
################################################################################
class FieldstatExporterVars:
    local_display_counter = False
    local_display_hist    = False
    local_display_hll     = False
    local_match_tags      = {}
    local_template        = ""

    prom_uri_path         = ""

    json_paths            = []
    json_dirs             = []
    hist_format           = ""
    hist_bins             = []

    local_scientific_form = False

################################################################################
#  global functions
################################################################################
def read_json_paths_from_dirs(dirs):
    json_paths = []
    for item in dirs:
        if not os.path.exists(item):
            logging.error("Dir {%s} is not exist.", item)
            continue
        if not os.path.isdir(item):
            logging.error("Path {%s} is not directory.", item)
            continue
        for file in os.listdir(item):
            if file.endswith(".json"):
                file_path = os.path.abspath(os.path.join(item, file))
                json_paths.append(file_path)
    return json_paths

################################################################################
#  promethues exporter
################################################################################
class PrometheusExporter:
    def __init__(self):
        self.hist_bins   = FieldstatExporterVars.hist_bins
        self.hist_format = FieldstatExporterVars.hist_format
        self.json_paths  = FieldstatExporterVars.json_paths
        self.json_paths.extend(read_json_paths_from_dirs(FieldstatExporterVars.json_dirs))
        self.n_lines     = 0

    def __escape_metric_name(self, metric_name):
        #regex: [a-zA-Z_:][a-zA-Z0-9_:]*
        name = re.sub(r'[^a-zA-Z0-9_:]', '_', metric_name)
        return name

    def __escape_metric_tags(self, json_dict):
        #regex: [a-zA-Z_][a-zA-Z0-9_]*
        dst_tags = []
        src_tags = json_dict["tags"]
        src_tags["app_name"] = json_dict["name"]

        for key,value in src_tags.items():
            dst_key = re.sub(r'[^a-zA-Z0-9_:]', '_', key)
            dst_val = str(value)
            dst_tags.append(f'{dst_key}="{dst_val}"')

        return ','.join(dst_tags)

    def __build_type_counter(self, name, tags, value):
        metric = name + "{" + tags + "}" + ' ' + str(value) + '\n'
        self.n_lines += 1
        return metric


    def __build_histogram_format(self, name, tags, c_hist):
        metrics = ""

        for i in self.hist_bins:
            value   = FieldstatAPI.libfieldstat.fieldstat_histogram_count_le_value(c_hist, int(i))
            metric  = name + "_bucket" + "{" + tags + ",le=\"{:.2f}\"".format(i) + "}" + ' ' + str(value) + '\n'
            metrics += metric
            self.n_lines += 1

        return metrics

    def __build_summary_format(self, name, tags, c_hist):
        metrics = ""
        for i in self.hist_bins:
            value   = FieldstatAPI.libfieldstat.fieldstat_histogram_value_at_percentile(c_hist, float(i * 100))
            metric  = name + "{" + tags +  ",quantile=\"{:.2f}%\"".format(i * 100) + "}" + ' ' + str(value) + '\n'
            metrics += metric
            self.n_lines += 1

        return metrics

    def __build_type_histogram(self, name, tags, value):
        metrics = "" 
        c_hist  = FieldstatAPI.libfieldstat.fieldstat_histogram_base64_decode(value.encode('utf-8'))
        
        if self.hist_format == "summary":
            metrics += self.__build_summary_format(name, tags, c_hist)
        if self.hist_format == "histogram":
            metrics += self.__build_histogram_format(name, tags, c_hist)

        sum_value = FieldstatAPI.libfieldstat.fieldstat_histogram_value_sum(c_hist)
        metrics  += name + "_sum"   + "{" + tags + "}" + ' ' + str(sum_value) + '\n'
        self.n_lines += 1

        cnt_value = FieldstatAPI.libfieldstat.fieldstat_histogram_value_total_count(c_hist)
        metrics  += name + "_count" + "{" + tags + "}" + ' ' + str(cnt_value) + '\n'
        self.n_lines += 1

        FieldstatAPI.libfieldstat.fieldstat_histogram_free(c_hist)

        return metrics
    
    
    def __build_type_hll(self, name, tags, value):
        hll_value = FieldstatAPI.libfieldstat.fieldstat_hll_base64_to_count(value.encode('utf-8'))
        self.n_lines += 1
        return  name + "{" + tags + "}" + ' ' + "{:.2f}".format(hll_value) + '\n'

    def __build_metrics(self, json_dict):
        metrics      = ""
        escaped_tags = self.__escape_metric_tags(json_dict)
        for key,value in json_dict["fields"].items():
            escaped_name = self.__escape_metric_name(key)
            if isinstance(value, int):
                metrics += self.__build_type_counter(escaped_name, escaped_tags, value)
            else:
                is_hll = FieldstatAPI.libfieldstat.fieldstat_is_hll(value.encode('utf-8'))
                if is_hll:
                    metrics += self.__build_type_hll(escaped_name, escaped_tags, value)
                else:
                    metrics += self.__build_type_histogram(escaped_name, escaped_tags, value)
        return metrics

    def build_metrics_payload(self, json_path):
        payload = ""
        
        if not os.path.exists(json_path):
            logging.error("Path: {%s} does not exist", json_path)
            return payload

        with open(json_path) as file:
            fcntl.flock(file, fcntl.LOCK_EX)
            json_data = []
            try:
                json_data = json.load(file)
            except json.JSONDecodeError as e:
                print("Invalid JSON:", e)
            fcntl.flock(file, fcntl.LOCK_UN)
            for item in json_data:
                payload += self.__build_metrics(item)

        return payload

    def merge_multi_files_payload(self):
        payloads = ""
        for item in self.json_paths:
            payloads += self.build_metrics_payload(item)
        return payloads

    def read_lines_num(self):
        return self.n_lines

    @classmethod
    def run_prometheus_exporter(cls):
        builder = cls()
        return builder.merge_multi_files_payload()

class PrometheusEndpoint(BaseHTTPRequestHandler):
    def __init__(self, request, client_address, server):
        self.desired_path = FieldstatExporterVars.prom_uri_path
        super().__init__(request, client_address, server)

    def do_GET(self):
        if self.path == self.desired_path:
            resp = PrometheusExporter.run_prometheus_exporter()
            self.send_response(200)
            self.send_header('Content-type', 'text/plain')
            self.end_headers()
            self.wfile.write(resp.encode())
        else:
            resp = "uri path:" + self.desired_path
            self.send_response(404)
            self.end_headers()
            self.wfile.write(resp.encode())



################################################################################
#  local exporter
################################################################################
class CounterTable:
    INFO_COLUMN_WIDTH = 11 # len(speed/s) + 2 * (" ") + 2 * ("|")
    COLUMM_PADDING    = 3  # 2 *(" ") + "|"
    
    def __init__(self):
        self.terminal_size, _ = shutil.get_terminal_size((128, 64))
        self.column_size = self.terminal_size - self.INFO_COLUMN_WIDTH
        self.info_column = ("", ["tags", "sum", "speed/s"])
        self.min_width   = self.INFO_COLUMN_WIDTH
        self.tables      = []
        self.columns     = []
        self.field_names = []  

    def __convert_value_to_str(self, value, speed_s):
        value_str = ""
        speed_str = ""
        if FieldstatExporterVars.local_scientific_form == True:
            value_str = "{:.2e}".format(value)
            speed_str = "{:.2e}".format(speed_s)
        else:
            value_str = str(value)
            speed_str = "{:.2f}".format(speed_s)
        return value_str, speed_str

    def add_table_column(self, tags, head, value, speed_s):
        value_str, speed_str = self.__convert_value_to_str(value, speed_s)
        column = (head, [tags, value_str, speed_str])
        self.columns.append(column)
        self.min_width = max(self.min_width, len(tags), len(head), len(value_str))

    def __build_one_table(self, columns_slice):
        table = PrettyTable()
        table.vrules = NONE
        table.hrules = NONE
        # append info column into table
        table.add_column(self.info_column[0], self.info_column[1], align = "l" )
        for item in columns_slice:
            # append data column into table
            table.add_column(item[0], item[1], align="r")
            # parameters max length
            table.min_width[item[0]] = self.min_width

        return table
    
    def build_columns_tables(self):
        # One table print in screen size
        # One table per screen size
        n_columns  = len(self.columns)
        if n_columns == 0:
            return

        table_size = self.column_size // (self.min_width + self.COLUMM_PADDING)
        if 0 == table_size:
            table_size = 1

        for i in range(0, n_columns, table_size):
            table  = None
            l_edge = i
            r_edge = min(i + table_size, n_columns)

            if r_edge >= n_columns:
                table  = self.__build_one_table(self.columns[l_edge:])
            else:
                table  = self.__build_one_table(self.columns[l_edge:r_edge])
            
            self.tables.append(table)

    def read_columns_num(self):
        return len(self.columns)

    def read_rows_num(self):
        return len(self.rows)
    
    def read_tables_num(self):
        return len(self.tables)

    def print_tables(self):
        self.build_columns_tables()
        for item in self.tables:
            print(item)
        if len(self.tables) > 0:
            print("=" * self.terminal_size)
            


class TableFormatTable:
    def __init__(self):
        self.terminal_size, _ = shutil.get_terminal_size((128, 64))
        self.tables = []
        self.rows   = []

    def create_table(self, columns, enable_speed):
        field_names = []
        field_names.append("")

        if enable_speed:
            columns_with_speed = []
            for item in columns:
                columns_with_speed.append(item)
                speed_fields = str(item) + "/s"
                columns_with_speed.append(speed_fields)
            field_names.extend(columns_with_speed)
        else:
            field_names.extend(columns)

        table = PrettyTable()
        table.vrules = NONE
        #table.hrules = NONE
        table.field_names = field_names

        for item in field_names:
            table.align[item] = "r"
        table.align[""] = "l"

        self.tables.append(table)

        return table


    def add_table_row(self, table, tags, fields, fields_delta, interval_ms, enable_speed, columns):
        row = []

        if table is None:
            return

        #exporter table row name.
        row_str = []
        for key, value in tags.items():
            #row.append("%s_%s" % (key, str(value)))
            row_str.append("%s_%s" % (key, str(value)))
        row.append('&'.join(row_str))

        #exporter table row value.

        for item in columns:
            if FieldstatExporterVars.local_scientific_form == True:
                row.append("{:.2e}".format(fields[item]))
            else:
                row.append(fields[item])

            if enable_speed:
                if (interval_ms > 0) and (item in fields_delta):
                    speed_s = fields_delta[item] * 1000 / interval_ms
                    if FieldstatExporterVars.local_scientific_form == True:
                        row.append("{:.2e}".format(speed_s))
                    else:
                        row.append("{:.2f}".format(speed_s))
                else:
                    row.append("-")

        table.add_row(row)


    def print_tables(self):
        for item in self.tables:
            print(item)
        if len(self.tables) > 0:
            print("=" * self.terminal_size)

class HistogramTable:
    def __init__(self):
        self.format = FieldstatExporterVars.hist_format
        self.bins   = FieldstatExporterVars.hist_bins
        self.tables = []
        self.terminal_size, _  = shutil.get_terminal_size((128, 64))

    def __get_row_shared_values(self, c_hist):
        shared_values = []

        max_value = FieldstatAPI.libfieldstat.fieldstat_histogram_value_max(c_hist)
        shared_values.append(str(max_value))

        min_value = FieldstatAPI.libfieldstat.fieldstat_histogram_value_min(c_hist)
        shared_values.append(str(min_value))

        avg_value = FieldstatAPI.libfieldstat.fieldstat_histogram_value_mean(c_hist)
        shared_values.append("{:.2f}".format(avg_value))

        dev_value = FieldstatAPI.libfieldstat.fieldstat_histogram_value_stddev(c_hist)
        shared_values.append("{:.2f}".format(dev_value))
        
        cnt_value = FieldstatAPI.libfieldstat.fieldstat_histogram_value_total_count(c_hist)
        shared_values.append(str(cnt_value))
        
        return shared_values

    def  __get_row_values(self, c_hist):
        row_values = []
        for i in self.bins:
            if self.format == "summary":
                value = FieldstatAPI.libfieldstat.fieldstat_histogram_value_at_percentile(c_hist, float(i * 100))
                row_values.append(str(value))
            if self.format == "histogram":
                value = FieldstatAPI.libfieldstat.fieldstat_histogram_count_le_value(c_hist, int(i))
                row_values.append(str(value))
        shared_values = self.__get_row_shared_values(c_hist)
        row_values   += shared_values
        return row_values

    def  __add_table_field_names(self, table):
        field_names = []
        field_names.append("")
        for i in self.bins:
            if self.format == "summary":
                field_names.append("{:.2f}%".format(i * 100))
            if self.format == "histogram":
                field_names.append("le={:d}".format(int(i)))
        field_names += ["MAX", "MIN", "AVG", "STDDEV", "CNT"]
        table.field_names = field_names
        for item in field_names:
            table.min_width[item] = 10
            if item == "":
                table.align[item] = "l"
            else:
                table.align[item] = "r"

    def __add_table_row(self, table, hist_value, row_name):
        if hist_value is None:
            return
        c_hist = FieldstatAPI.libfieldstat.fieldstat_histogram_base64_decode(hist_value.encode('utf-8'))
        row    = [row_name] + self.__get_row_values(c_hist)
        FieldstatAPI.libfieldstat.fieldstat_histogram_free(c_hist)
        table.add_row(row)

    def build_table(self, tags, key, hist_value, hist_value_delta, interval_ms):
        table = PrettyTable()
        table.vrules = NONE
        # table.hrules = NONE
        table.title = key + " " + tags + " "
        if interval_ms > 0:
            table.title += "interval: {:d}ms".format(int(interval_ms))
        self.__add_table_field_names(table)
        self.__add_table_row(table, hist_value, "acc")
        self.__add_table_row(table, hist_value_delta, "delta")

        self.tables.append(table)

    def read_tables_num(self):
        return len(self.tables)

    def print_tables(self):
        for item in self.tables:
            print(item)
        if len(self.tables) > 0:
            print("=" * self.terminal_size)

class LocalExporter:
    def __init__(self):
        self.terminal_size, _  = shutil.get_terminal_size((128, 64))
        self.json_paths        = FieldstatExporterVars.json_paths
        self.json_paths.extend(read_json_paths_from_dirs(FieldstatExporterVars.json_dirs))
        self.ctable            = None
        self.htable            = None
        self.hlltable          = None
        self.tftable           = None
        self.display_counter   = FieldstatExporterVars.local_display_counter
        self.display_hist      = FieldstatExporterVars.local_display_hist
        self.display_hll       = FieldstatExporterVars.local_display_hll
        self.match_tags        = FieldstatExporterVars.local_match_tags
        self.template          = FieldstatExporterVars.local_template
        self.__set_default_display()
        self.objects_matched   = []
        self.template_ja2      = None


    def __set_default_display(self):
        if not (self.display_counter or self.display_hist or self.display_hll):
            self.display_counter = True
            self.display_hist    = True
            self.display_hll     = True


    def __build_counter_type_exporter(self, tags, key, value, value_delta, timestamp_ms_delta):
        speed_s = 0.0
        if (value_delta is not None) and (timestamp_ms_delta > 0):
            speed_s = value_delta * 1000 / timestamp_ms_delta
        self.ctable.add_table_column(tags, key, value, speed_s)

    def __build_histogram_type_exporter(self, tags, key, value, value_delta, timestamp_ms_delta):
        self.htable.build_table(tags, key, value, value_delta, timestamp_ms_delta)

    def __build_hll_type_exporter(self, tags, key, value):
        hll_value = FieldstatAPI.libfieldstat.fieldstat_hll_base64_to_count(value.encode('utf-8'))
        self.hlltable.add_table_column(tags, key, "{:.2f}".format(hll_value), 0)

    def __append_app_name_to_tags(self, json_object):
        tags_dict = copy.deepcopy(json_object["tags"])
        tags_dict.update({"app_name": json_object["name"]})
        return json.dumps(tags_dict)


    def __is_tags_matched(self, tags):
        if len(self.match_tags) == 0:
            return True

        if len(tags) == 0:
            return False

        if len(self.match_tags) > len(tags):
            return False

        for key, value in self.match_tags.items():
            if key not in tags:
                return False
            if value != tags[key]:
                return False

        return True


    def read_json_objects_from_file(self, json_path):
        #check source json file is exist.
        objects = []
        if not os.path.exists(json_path):
            logging.error("Path: {%s} does not exist", json_path)
            return objects
        with open(json_path) as fd:
            fcntl.flock(fd, fcntl.LOCK_EX)
            try:
                objects = json.load(fd)
            except json.JSONDecodeError as e:
                print("Invalid JSON:", e)
            fcntl.flock(fd, fcntl.LOCK_UN)
        return objects

    def read_match_tags_json_objects(self, json_objects):
        matched_objects = []

        for item in json_objects:
            tags = item["tags"]
            #not match tags object. not read.
            if not self.__is_tags_matched(tags):
                continue
            #match tags object. delete matching tags.
            for key,value in self.match_tags.items():
                if key in tags and value == tags[key]:
                    tags.pop(key, None)

            matched_objects.append(item)

        self.objects_matched = matched_objects

        return matched_objects

    def build_not_table_format_exporter(self, json_objects):
        for item in json_objects:
            timestamp_ms_delta = item["timestamp_ms_delta"]
            fields_delta       = item["fields_delta"]
            tags_new           = self.__append_app_name_to_tags(item)

            for key,value in item["fields"].items():
                value_delta = None
                if key in fields_delta:
                    value_delta = fields_delta[key]
                if not isinstance(value, str):
                    self.__build_counter_type_exporter(tags_new, key, value, value_delta, timestamp_ms_delta)
                else:
                    # histogram and hll type
                    is_hll = FieldstatAPI.libfieldstat.fieldstat_is_hll(value.encode('utf-8'))
                    if is_hll:
                        self.__build_hll_type_exporter(tags_new, key, value)
                    else:
                        self.__build_histogram_type_exporter(tags_new, key, value, value_delta, timestamp_ms_delta)


    def build_table_format_exporter(self, json_objects, enable_speed, columns):
        table = None
        for item in json_objects:
            tags   = item["tags"]
            fields = item["fields"]
            fields_delta = item["fields_delta"]
            interval_ms = item["timestamp_ms_delta"]
            if table == None:
                table = self.tftable.create_table(columns, enable_speed)
            
            self.tftable.add_table_row(table, tags, fields, fields_delta, interval_ms, enable_speed, columns)
        return table


    def export_templates(self):
        env = Environment(loader=FileSystemLoader('templates'))
        env.globals.update(print_tables    =self.print_table_format)
        env.globals.update(print_counters  =self.print_counter_type)
        env.globals.update(print_histograms=self.print_histogram_type)
        env.globals.update(print_hlls      =self.print_hll_type)
        template = env.from_string(self.template)
        self.template_ja2 = template

    def build_local_exporter(self):
        for item in self.json_paths:
            print("Json file path: " + item)
            self.ctable = CounterTable()
            self.htable = HistogramTable()
            self.hlltable = CounterTable()
            self.tftable  = TableFormatTable()
            self.objects_matched = []
            objects = self.read_json_objects_from_file(item)
            self.objects_matched = self.read_match_tags_json_objects(objects)

            if len(self.template) > 0:
                self.export_templates()
            else:
                self.build_not_table_format_exporter(self.objects_matched)
            self.print_local_exporter()


    def print_table_format(self, groupby, columns, enable_speed=True, verbose=False):
        table_fields=[]
        for item in self.objects_matched:
            tags   = item["tags"]
            fields = item["fields"]

            #select (groupby in tags key) && ( cloumn0 || column1 ||... in field keys) && (field_value type is int).
            is_match_table_format = False

            if groupby not in tags:
                continue
             
            for column in columns:
                if column not in fields:
                    continue
                if isinstance(fields[column], str):
                    is_match_table_format = False
                    break
                else:
                    is_match_table_format = True
            
            if is_match_table_format == False:
                continue

            # build new field json object.
            new_fields = copy.deepcopy(item)
            
            for key in fields:
                if key not in columns:
                    new_fields["fields"].pop(key)

            for column in columns:
                if column not in fields:
                    new_fields["fields"][column] = "-"

            if not verbose:
                for key in tags:
                    if groupby != key:
                        new_fields["tags"].pop(key)

            # if enable_speed == False:
            #     new_fields["fields_delta"] = {}

            table_fields.append(new_fields)
        return self.build_table_format_exporter(table_fields, enable_speed, columns)
    
#   def print_counter(self, [tags], [fields])
    def print_counter_type(self, field_keys):
        counter_fields = []
        for item in self.objects_matched:
            fields = item["fields"]

            new_fields = copy.deepcopy(item)
                    
            for key in fields:
                if (key not in field_keys) or (isinstance(fields[key], str)):
                    new_fields["fields"].pop(key)
            counter_fields.append(new_fields)
        self.build_not_table_format_exporter(counter_fields)
        self.ctable.build_columns_tables()
        str_tables = []
        for item in self.ctable.tables:
            str_tables.append(item.get_string())        
        return "".join(str_tables)

    def print_histogram_type(self, field_keys):
        hist_fields = []
        for item in self.objects_matched:
            fields = item["fields"]
            new_fields = copy.deepcopy(item) 
            for key in fields:
                if (key not in field_keys) \
                    or (not isinstance(fields[key], str)) \
                    or (True == FieldstatAPI.libfieldstat.fieldstat_is_hll(fields[key].encode('utf-8'))):
                    new_fields["fields"].pop(key)
            hist_fields.append(new_fields)
        self.build_not_table_format_exporter(hist_fields)
        str_tables = []
        for item in self.htable.tables:
            str_tables.append(item.get_string())   
        return "".join(str_tables)

    def print_hll_type(self, field_keys):
        hll_fields = []
        for item in self.objects_matched:
            fields = item["fields"]
            new_fields = copy.deepcopy(item) 
            for key in fields:
                if (key not in field_keys) \
                    or (not isinstance(fields[key], str)) \
                    or (False == FieldstatAPI.libfieldstat.fieldstat_is_hll(fields[key].encode('utf-8'))):
                    new_fields["fields"].pop(key)
                    
            hll_fields.append(new_fields)
        self.build_not_table_format_exporter(hll_fields)
        self.hlltable.build_columns_tables()
        str_tables = []
        for item in self.hlltable.tables:
            str_tables.append(item.get_string())        
        return "".join(str_tables)


    def print_local_exporter(self):
        #The top edge
        timestamp      = datetime.datetime.now().timestamp()
        formatted_time = datetime.datetime.fromtimestamp(timestamp).strftime('%a %b %d %H:%M:%S %Y')
        num_of_equals  = (self.terminal_size - len(formatted_time)) // 2
        print('=' * num_of_equals + formatted_time + '=' * num_of_equals)

        if len(self.template) > 0:
            print(self.template_ja2.render())
        else:
            if self.display_counter:
                self.ctable.print_tables()
                self.tftable.print_tables()

            if self.display_hist:
                self.htable.print_tables()
                #print("=" * self.terminal_size)

            if self.display_hll:
                self.hlltable.print_tables()
                #print("=" * self.terminal_size)

        #The bottom edge
        print('-' * self.terminal_size)


    @classmethod
    def run_local_exporter(cls):
        exporter = cls()
        exporter.build_local_exporter()


################################################################################
#  fieldstat exporter
################################################################################
class FieldstatExporter:
    DEFAULT_LISTEN_PORT = 8080
    DEFAULT_HIST_BINS   = [0.1,0.5,0.8,0.9,0.95,0.99]
    DEFAULT_HIST_FORMAT = "summary"
    DEFAULT_URI_PATH    = "/metrics"
    DEFAULT_INTERVAL_S  = 1

    def __init__(self):
        self.local_clear_screen = False
        self.local_enable_loop  = False
        self.local_interval_s   = 1
        self.prom_listen_port   = self.DEFAULT_LISTEN_PORT
        self.exporter_mode      = "local"

    def __build_shared_args_parser(self):
        bins_str = ','.join([str(x) for x in self.DEFAULT_HIST_BINS])

        parser = argparse.ArgumentParser(add_help=False)
        parser.add_argument("-b", "--hist-bins",   type = str, default = bins_str,
                            help = "The metrics of histogram type output bins.")
        parser.add_argument("-f", "--hist-format", type = str, default = self.DEFAULT_HIST_FORMAT,
                            help = "The metrics of histogram type output format.")
        parser.add_argument("-j", "--json-paths", nargs='+', type = str, default = [],
                            help = "The input fieldstat metrics json file paths. Support multi path.")
        parser.add_argument("-d", "--dirs",  nargs='+', type = str, default = [],
                            help ="Specify input fieldstat metrics json file directories.")
        return parser

    def __build_prom_parser(self, subparsers, shared_arg_parser):
        parser = subparsers.add_parser('prometheus', help='Set prometheus exporter', parents=[shared_arg_parser])
        parser.add_argument("-p", "--listen-port", type = int, default = self.DEFAULT_LISTEN_PORT,
                            help = "Specify the prometheus endpoint port to listen. i.e., 80,8080. The default is {}.".format(self.DEFAULT_LISTEN_PORT))
        parser.add_argument("-u", "--uri-path",    type = str, default = self.DEFAULT_URI_PATH,
                            help = "Specify the prometheus endpoint uri path.")

    def __build_local_parser(self, subparsers, shared_arg_parser):
        parser = subparsers.add_parser('local', help='Set local exporter', parents=[shared_arg_parser])
        parser.add_argument("-i", "--interval", type = int,        default = self.DEFAULT_INTERVAL_S,
                            help = "interval, seconds to wait between print.")
        parser.add_argument("-l", "--loop",      action = 'store_true', default = False,
                            help = "print loop, exit when recv a signal.")
        parser.add_argument('--clear-screen',    action = 'store_true', default = False,
                            help = 'clear screen at start of loop')
        parser.add_argument('--display-hll' ,    action = 'store_true', default = False,
                            help = 'Display hyperloglog type metrics')
        parser.add_argument('--display-hist',    action = 'store_true', default = False,
                            help = 'Display histogram type metrics')
        parser.add_argument('--display-counter', action = 'store_true', default = False,
                            help = 'Display counter type metrics')
        parser.add_argument("-m", "--match-tags", type = str,           default = "",
                            help = "Display the tags match metrics")
        parser.add_argument("-t", "--template",   type = str,           default = "",
                            help = "Specify the print template with jinja2.")
        parser.add_argument("--scientific-form",  action = 'store_true', default = False,
                            help = "Output the counter type value in scientific notation.")

    def __parse_bins_str(self, bins_str):
        bins = [] 
        for item in bins_str.split(','):
            bins.append(float(item))
        bins_sort = list(set(bins))
        bins_sort.sort()
        return bins_sort
    
    def __parse_tags_str(self, tags_str):
        tags_dict = {}
        if not tags_str:
            return tags_dict
        pairs = tags_str.split(',')
        for pair in pairs:
            key, value = pair.split(':')
            temp_value = value.strip()
            if temp_value.isdigit():
                tags_dict[key] = int(temp_value)
            elif re.match(r'^[-+]?[0-9]*\.?[0-9]+([eE][-+]?[0-9]+)?$', temp_value):
                tags_dict[key] = float(temp_value)
            else:
                tags_dict[key] = temp_value
            
        return tags_dict

    def __read_json_paths_from_dirs(self, dirs):
        json_paths = []
        for item in dirs:
            if not os.path.exists(item):
                logging.error("Dir {%s} is not exist.", item)
                continue
            if not os.path.isdir(item):
                logging.error("Path {%s} is not directory.", item)
                continue
            for file in os.listdir(item):
                if file.endswith(".json"):
                    file_path = os.path.abspath(os.path.join(item, file))
                    json_paths.append(file_path)
        return json_paths

    def __read_shared_args_value(self, args):
        FieldstatExporterVars.hist_format = args.hist_format
        FieldstatExporterVars.json_paths  = args.json_paths
        FieldstatExporterVars.json_dirs   = args.dirs
        FieldstatExporterVars.hist_bins   = self.__parse_bins_str(args.hist_bins)

    def __read_private_args_value(self, args):
        if args.command == 'prometheus':
            FieldstatExporterVars.prom_uri_path = args.uri_path
            self.exporter_mode       = 'prometheus'
            self.prom_listen_port    = args.listen_port

        if args.command == 'local':
            FieldstatExporterVars.local_display_counter = args.display_counter
            FieldstatExporterVars.local_display_hll     = args.display_hll
            FieldstatExporterVars.local_display_hist    = args.display_hist
            FieldstatExporterVars.local_match_tags      = self.__parse_tags_str(args.match_tags)
            FieldstatExporterVars.local_template        = args.template
            FieldstatExporterVars.local_scientific_form = args.scientific_form
            self.exporter_mode       = 'local'
            self.local_interval_s    = args.interval
            self.local_enable_loop   = args.loop
            self.local_clear_screen  = args.clear_screen

    def __verify_cmd_args(self, args):
        if args.hist_format not in ["summary", "histogram"]:
            logging.error("Historgram format must be 'summary' or 'histogram'")
            return -1

        bins = args.hist_bins.split(',')
        if args.hist_format == "summary":
            for bin in bins:
                if not re.match(r'^0\.([1-9]|[0][1-9]|[1-9]\d)$', bin):
                    logging.error("When historgram format is %s, bins value in [0.01-0.99], "
                                  "bins format example: 0.1,0,2,0.3", args.hist_format)
                    return -1
        if args.hist_format == "histogram":
            for bin in bins:
                if not re.match(r'^\d+$', bin):
                    logging.error("When historgram format is %s, bins value is integer, "
                                  "bins format example: 1,10,100,1000", args.hist_format)
                    return -1

        if args.command == 'prometheus':
            if  args.listen_port < 1024 or args.listen_port > 65535:
                logging.error("invalid listen port,  listen port must be in [1024, 65535]")        
                return -1

        if args.command == 'local':
            if args.match_tags != "":
                pattern = r'^([A-Za-z0-9_-]+:[A-Za-z0-9_\-\.]+,)*([A-Za-z0-9_-]+:[A-Za-z0-9_\-\.]+)$'
                match   = re.match(pattern, args.match_tags)
                if not match:
                    logging.error("invalid match tags,  Example: key1:value1,key2:value2,key3:value3") 
                    return -1

        return 0

    def read_cmd_options(self):
        parser = argparse.ArgumentParser(description='Fieldstat exporter')
        shared_args_parser = self.__build_shared_args_parser()
        subparsers = parser.add_subparsers(dest='command')
        subparsers.required = True
        self.__build_prom_parser(subparsers,  shared_args_parser)
        self.__build_local_parser(subparsers, shared_args_parser)

        args = parser.parse_args()

        if -1 == self.__verify_cmd_args(args):
            parser.print_help()
            sys.exit(1)

        self.__read_shared_args_value(args)
        self.__read_private_args_value(args)


    def __enable_prometheus_endpoint(self):
        server_address = ('', self.prom_listen_port)
        httpd = HTTPServer(server_address, PrometheusEndpoint)
        httpd.serve_forever()

    def __enable_local_exporter(self):
        while True:
            if self.local_clear_screen:
                os.system('clear')
            LocalExporter.run_local_exporter()

            if not self.local_enable_loop:
                break;
            time.sleep(self.local_interval_s)

    def fieldstat_export(self):
        if (len(FieldstatExporterVars.json_paths) == 0) and \
           (len(FieldstatExporterVars.json_dirs)  == 0):
            logging.error("There is no JSON file path and dir specified. The process will exit.")
            exit(1)
        try:
            if self.exporter_mode == 'prometheus':
                self.__enable_prometheus_endpoint()

            if self.exporter_mode == 'local':
                self.__enable_local_exporter()

        except KeyboardInterrupt:
            pass

    @classmethod
    def run_fieldstat_exporter(cls):
        exporter = cls()
        exporter.read_cmd_options()
        exporter.fieldstat_export()


if __name__ == '__main__':
    FieldstatExporter.run_fieldstat_exporter()
