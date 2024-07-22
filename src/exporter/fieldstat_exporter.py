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
from itertools import chain
from prettytable import PrettyTable,NONE,HEADER
from http.server import HTTPServer, BaseHTTPRequestHandler
from jinja2 import Environment, FileSystemLoader

import ctypes

class FieldstatAPI:
    libfieldstat = ctypes.CDLL('libfieldstat4.so')
    libfieldstat.fieldstat_histogram_base64_decode.argtypes       = [ctypes.c_char_p]
    libfieldstat.fieldstat_histogram_base64_decode.restype        =  ctypes.c_void_p

    libfieldstat.fieldstat_histogram_free.argtypes                = [ctypes.c_void_p]

    libfieldstat.fieldstat_histogram_value_at_percentile_api.argtypes = [ctypes.c_void_p, ctypes.c_double]
    libfieldstat.fieldstat_histogram_value_at_percentile_api.restype  =  ctypes.c_longlong

    libfieldstat.fieldstat_histogram_count_le_value_api.argtypes      = [ctypes.c_void_p, ctypes.c_longlong]
    libfieldstat.fieldstat_histogram_count_le_value_api.restype       =  ctypes.c_longlong

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
    local_display_notagged = False

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
        self.json_paths  = copy.deepcopy(FieldstatExporterVars.json_paths)
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
            value   = FieldstatAPI.libfieldstat.fieldstat_histogram_count_le_value_api(c_hist, int(i))
            metric  = name + "_bucket" + "{" + tags + ",le=\"{:.2f}\"".format(i) + "}" + ' ' + str(value) + '\n'
            metrics += metric
            self.n_lines += 1

        return metrics

    def __build_summary_format(self, name, tags, c_hist):
        metrics = ""
        for i in self.hist_bins:
            value   = FieldstatAPI.libfieldstat.fieldstat_histogram_value_at_percentile_api(c_hist, float(i * 100))
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
                value = FieldstatAPI.libfieldstat.fieldstat_histogram_value_at_percentile_api(c_hist, float(i * 100))
                row_values.append(str(value))
            if self.format == "histogram":
                value = FieldstatAPI.libfieldstat.fieldstat_histogram_count_le_value_api(c_hist, int(i))
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

def print_dictionary_with_alignment(dict, terminal_width, padding_grid):
    current_line_length = 0
    for key, value in dict.items():
        pair_length = len(key) + len(str(value)) + 1

        # estimate the length used by current pair
        if pair_length > padding_grid:
            padding = (pair_length // padding_grid + 1) * padding_grid
        else:
            padding = padding_grid

        # if current line space is not enough, then start a new line
        if current_line_length + padding > terminal_width and current_line_length != 0:
            print()
            current_line_length = 0

        # print by padding the space until the length of the pair is equal to padding
        print(f"{key}:{value}", end=' ' * (padding - pair_length))
        current_line_length += padding

    print()
    print("-" * terminal_width)

class hashable_tag:
    def __init__(self, tags):
        self.tags = tags

    def __eq__(self, other):
        return self.tags == other.tags

    def __hash__(self):
        return hash(frozenset(self.tags.items()))

class JsonParser:
    def __init__(self, json_objects):
        # json_objects is a list of json object {tags, fields, fields_delta, timestamp_ms, timestamp_ms_delta}
        self.tag_info_map = {} # tag:{fieldname, delta fields, timestamp, timestamp_delta}

        for item in json_objects:
            tags = item["tags"]
            delta_fields = item["fields_delta"] if "fields_delta" in item else {}
            timestamp_delta = item["timestamp_ms_delta"] if "timestamp_ms_delta" in item else -1
            self.tag_info_map[hashable_tag(tags)] = (item["fields"], delta_fields, item["timestamp_ms"], timestamp_delta)


    def get_v(self, tags, field_name):
        if hashable_tag(tags) not in self.tag_info_map:
            return "invalid tag"

        i_tuple = self.tag_info_map[hashable_tag(tags)]
        fields = i_tuple[0]

        if field_name[0] == "%" and field_name[-1] == "%":
            field_name = field_name[1:-1]
            scientific_form = True
        else:
            scientific_form = False
        def sci_note(v):
            if isinstance(v, str):
                return v

            if scientific_form:
                return "{:.2e}".format(v)
            if isinstance(v, float):
                return "{:.2f}".format(v)
            return v

        pattern = r'#Name<(.*?)>'
        match = re.search(pattern, field_name)
        if match:
            field_name = re.sub(pattern, '', field_name)
            
        if field_name in fields:
            tmp_v = fields[field_name]
            if isinstance(tmp_v, str):
                return "field_value_is_string"

            return sci_note(tmp_v)
        
        # try special field name
        if field_name.startswith("#") == False:
            return "-"
        pattern = re.compile(r'#Ratio<(\w+),(\w+)(,.+?)?>')
        match = pattern.match(field_name)
        if match:
            field_n = match.group(1)
            field_d = match.group(2)
            scaling = match.group(3)
            if scaling:
                scaling = float(scaling[1:])
            if field_n in fields and field_d in fields:
                if isinstance(fields[field_n], str) or isinstance(fields[field_d], str):
                    return "field_value_is_string"

                if fields[field_d] != 0:
                    tmp_v = fields[field_n] / fields[field_d]
                else:
                    if fields[field_n] == 0:
                        tmp_v = 0
                    else:
                        return "-"
                
                if scaling:
                    if scaling > 0:
                        tmp_v *= scaling
                    else:
                        tmp_v /= -scaling

                return sci_note(tmp_v)
            else:
                return "-"
    
        pattern = re.compile(r'#Speed<(\w+)>')
        match = pattern.match(field_name)
        if match:
            fields_delta = i_tuple[1]
            timestamp_ms_delta = i_tuple[3]
            f_name = match.group(1)
            speed_fields = f_name + "/s"
            if f_name in fields_delta:
                tmp_v = fields_delta[f_name]
                if isinstance(tmp_v, str):
                    return "field_value_is_string"

                tmp_v = tmp_v * 1000 / timestamp_ms_delta if timestamp_ms_delta > 0 else "-"
                return sci_note(tmp_v)
            else:
                return "-"

        return "-"

def convert_to_header_name(name):
    if name[0] == "%" and name[-1] == "%":
        name = name[1:-1]

    pattern = r'#Name<(.*?)>'
    match = re.search(pattern, name)
    if match:
        return match.group(1)

    if name.startswith("#"):
        pattern = re.compile(r'#Ratio<(\w+),(\w+)>')
        match = pattern.match(name)
        if match:
            a = match.group(1)
            b = match.group(2)
            return f"{a}/{b}"
        
        pattern = re.compile(r'#Speed<(\w+)>')
        match = pattern.match(name)
        if match:
            return f"{match.group(1)}/s"

    return name

class LocalExporter:
    def __init__(self):
        self.terminal_size, _  = shutil.get_terminal_size((128, 64))
        self.json_paths        = copy.deepcopy(FieldstatExporterVars.json_paths)
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
        self.display_notagged = FieldstatExporterVars.local_display_notagged
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
            if len(tags) == 0:
                continue
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
            if "timestamp_ms_delta" not in item:
                timestamp_ms_delta = 0
            else:
                timestamp_ms_delta = item["timestamp_ms_delta"]
                
            if "fields_delta" not in item:
                fields_delta = {}
            else:
                fields_delta = item["fields_delta"]

            tags_new = item["tags"]
            tags_new = json.dumps(tags_new)

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
            self.objects_matched = []
            objects = self.read_json_objects_from_file(item)
            self.objects_matched = self.read_match_tags_json_objects(objects)
            self.fields_untagged = [item for item in objects if len(item["tags"]) == 0] if len(self.match_tags) == 0 else []

            exporter_name = objects[0]["name"] if len(objects) > 0 else None

            if len(self.template) > 0:
                self.export_templates()
            else:
                self.build_not_table_format_exporter(self.objects_matched + self.fields_untagged)
            self.print_local_exporter(exporter_name)

    def print_table_format(self, groupby, columns, verbose=False):
        parser = JsonParser(self.objects_matched)
        ret = PrettyTable()
        ret.vrules = NONE

        header = [""]
        header += [convert_to_header_name(x) for x in columns]
        ret.field_names = header

        ret.align[""] = "l"
        for i in header[1:]:
            ret.align[i] = "r"

        for item in self.objects_matched:
            tags = item["tags"]
            if groupby not in tags:
                continue

            if verbose:
                row_str = []
                for key, value in tags.items():
                    row_str.append("%s_%s" % (key, str(value)))
                row = ['&'.join(row_str)]
            else: 
                row = [groupby + "_" + str(tags[groupby])]
            row += [parser.get_v(tags, x) for x in columns]
            if FieldstatExporterVars.local_scientific_form:
                row = ["{:.2e}".format(float(x)) if isinstance(x, int) else x for x in row]
            ret.add_row(row)
        
        return str(ret) + '\n'

    def print_counter_type(self, field_keys):
        counter_fields = []
        for item in chain(self.objects_matched, self.fields_untagged):
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


    def print_local_exporter(self, exporter_name):
        #The top edge
        timestamp      = datetime.datetime.now().timestamp()
        head_line = datetime.datetime.fromtimestamp(timestamp).strftime('%a %b %d %H:%M:%S %Y')
        if exporter_name is not None:
            head_line += "  " + exporter_name
        
        num_of_equals  = (self.terminal_size - len(head_line)) // 2
        print('=' * num_of_equals + head_line + '=' * num_of_equals)

        if self.display_notagged:
            if len(self.fields_untagged) > 0:
                print_dictionary_with_alignment(self.fields_untagged[0]["fields"], self.terminal_size, 30)

        if len(self.template) > 0:
            print(self.template_ja2.render())
        else:
            if self.display_counter:
                self.ctable.print_tables()

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

def get_jinja_help(args):
    # 根据参数值打印不同的帮助信息
    if args.th == 'print_tables':
        print("fieldstat_exporter.py local -t '{{ print_tables(<tag key used to filter the metrics>, <field name list>, [verbose mode] }}'")
        print("- Print a table. Its row is organized by the tag key used to filter the metrics, and its column is organized by the field name list.")
        print("- Special field name format: #Ratio<field1,field2[,scale]> means the ratio of field1 and field2.\n  The third optional parameter scale is to multiply the ratio by a scale factor. If scale is negative, it will divide the ratio by the absolute value of scale. The default scale is 1.\n  e.g. #Ratio<In bytes,cputick,-1000> outputs In bytes/(cputick*1000).")
        print("- Special field name format: #Speed<field> means printout the speed of the field value changing per second.")
        print("- Apart from the two format above, add #Name<the metric name you defined>, to change the column name. It's optional, it the column name is not defined, it will use the field name.\n  As for ratio, the default name is field1/field2, and for speed, the default name is field/s.")
        print("- Verbose mode is optional, if true, it will print all the tags used by the shown metrics. The default value is false, and it will only print the tags with keys equal to groupby.")
        print("- To print out scientific notation for a specific field, use %<value>% in the field name list.\n  The percent marks should be used at the outermost. e.g. %#Name<in speed>#Speed<IN_Bytes>%")
        print("- Use Example:\n  fieldstat_exporter.py local -t '{{ print_tables(\"statistic_items\", [\"T_success_log\",\"T_total_log\",\"%IN_Bytes%\"\"#Name<geedge>#Ratio<T_success_log,T_total_log>\",\"#Speed<IN_Bytes>\"], True) }}'")
    elif args.th == 'print_counters':
        print("fieldstat_exporter.py local -t '{{ print_counters(<field name list>)}}'")
        print("Print the counter type metrics. Only the metrics with name in the field name list will be printed.")
    elif args.th == 'print_histograms':
        print("fieldstat_exporter.py local -t '{{ print_histograms(<field name list>)}}'")
        print("Print the histogram type metrics. Only the metrics with name in the field name list will be printed.")
        print("the bins and formats of histogram are set by -b and -f separately.")
    else:
        print("The -t option only supports 'print_tables', 'print_counters', 'print_histograms'.")
        print("try -th print_tables, -th print_counters, -th print_histograms for specific usage.")

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
                            help = "Specify the print template with jinja2. For specific usage, try -th")
        parser.add_argument("-e", "--no-tagged",      action = 'store_true', default = False,
                    help = "Print all counter metrics without tags in one list.")

        parser.add_argument("--scientific-form",  action = 'store_true', default = False,
                            help = "Output the counter type value in scientific notation.")

        parser.add_argument('-th', choices=['print_tables', 'print_counters', 'print_histograms'], help='Print out the help for -t option usage.')

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
            FieldstatExporterVars.local_display_notagged = args.no_tagged
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

        if args.command == 'local' and args.th:
            get_jinja_help(args)
            sys.exit(0)

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


# -tl 
# 展示配置
# -tc 创建一个表格列模板
# -tt 创建一个表格模板
# -tc field_name_list
# -tt print_tables("tag", ["field1", "field2", "field3"]或者一串数字，表示列模板。)
# -t "print_tables('tag', ['field1', 'field2', 'field3'])" 直接手动输入，或者-t 指定表格模板名，或者-t print_tables('tag', 列模板名)
# 以下三个都是合法的：
# -t "{{ print_tables('tag', ['field1', 'field2', 'field3']) }}{{ print_tables('tag2', ['field1', 'field4']) }}" 

# -tt "print_tables('tag', ['field1', 'field2', 'field3'])"
# -tt “print_tables('tag2', ['field1', 'field4'])”
# -t "1 2" 

# -tc field1 field2 field3
# -t "{{print_tables('tag', 3)}} 2"

if __name__ == '__main__':
    FieldstatExporter.run_fieldstat_exporter()
