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
from prettytable import PrettyTable,NONE,HEADER
from http.server import HTTPServer, BaseHTTPRequestHandler

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
    libfieldstat.fieldstat_is_hll.restype                         =  ctypes.c_longlong

logging.basicConfig(level=logging.DEBUG, format='%(asctime)s - %(message)s')

################################################################################
#  global variables
################################################################################
class FieldstatExporterVars:
    local_display_counter = False
    local_display_hist    = False
    local_display_hll     = False
    local_match_tags      = {}

    prom_uri_path         = ""

    json_path             = ""
    hist_format           = ""
    hist_bins             = []

################################################################################
#  promethues exporter
################################################################################
class PrometheusExporter:
    def __init__(self):
        self.hist_bins   = FieldstatExporterVars.hist_bins
        self.hist_format = FieldstatExporterVars.hist_format
        self.json_path   = FieldstatExporterVars.json_path
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
                if key.endswith("_delta"):
                    continue
                metrics += self.__build_type_counter(escaped_name, escaped_tags, value)
            else:
                is_hll = FieldstatAPI.libfieldstat.fieldstat_is_hll(value.encode('utf-8'))
                if is_hll:
                    metrics += self.__build_type_hll(escaped_name, escaped_tags, value)
                else:
                    metrics += self.__build_type_histogram(escaped_name, escaped_tags, value)
        return metrics

    def build_metrics_payload(self):
        payload = ""
        
        if not os.path.exists(self.json_path):
            logging.error("Path: {%s} does not exist", self.json_path)
            return payload

        with open(self.json_path) as file:
            json_data = json.load(file)
            for item in json_data:
                payload += self.__build_metrics(item)

        return payload

    def read_lines_num(self):
        return self.n_lines

    @classmethod
    def run_prometheus_exporter(cls):
        builder = cls()
        return builder.build_metrics_payload()

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
        self.rows        = []
        self.field_names = []  
        self.rows        = []

    def add_field_names(self, fields):
        if len(self.field_names) == 0:
            sorted_keys = sorted(fields.keys())
            self.field_names.append("")
            self.field_names.extend(sorted_keys)

    def append_field_rows(self,  tags, match_tags, fields):
        row      = []
        new_tags = {}

        #table only one tags
        if len(tags) - len(match_tags) != 1:
            return

        for key in tags:
            if key not in match_tags:
                new_tags[key] = tags[key]
                break

        for key, value in new_tags.items():
            row.append("%s_%s" % (key, str(value)))

        sorted_keys = sorted(fields.keys())
        for key in sorted_keys:
            row.append(fields[key])

        self.rows.append(row)


    def add_table_column(self, tags, head, value, speed_s):
        column = (head, [tags, str(value), "{:.2f}".format(speed_s)])
        self.columns.append(column)
        self.min_width = max(self.min_width, len(tags), len(head), len(str(value)))

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
    
    def __build_columns_tables(self):
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
        
    def __build_rows_tables(self):
        if len(self.field_names) == 0 or len(self.rows) == 0:
            return
        table = PrettyTable()
        table.vrules = NONE
        table.hrules = NONE
        table.field_names = self.field_names
        
        for item in self.field_names:
            table.align[item] = "r"
        table.align[""] = "l"

        for row in self.rows:
            table.add_row(row)

        self.tables.append(table)

    def read_columns_num(self):
        return len(self.columns)

    def read_rows_num(self):
        return len(self.rows)
    
    def read_tables_num(self):
        return len(self.tables)

    def print_tables(self):
        self.__build_columns_tables()
        self.__build_rows_tables()
        for item in self.tables:
            print(item)


class HistogramTable:
    def __init__(self):
        self.format = FieldstatExporterVars.hist_format
        self.bins   = FieldstatExporterVars.hist_bins
        self.tables = []

    def __build_summary_format(self, c_hist, table):
        for i in self.bins:
            head = "{:.2f}%".format(i * 100)
            row  =  FieldstatAPI.libfieldstat.fieldstat_histogram_value_at_percentile(c_hist, float(i * 100))
            table.add_column(head, [row])

    def __build_histogram_format(self, c_hist, table):
        for i in self.bins:
            head = "le={:d}".format(int(i))
            row  =  FieldstatAPI.libfieldstat.fieldstat_histogram_value_at_percentile(c_hist, int(i))
            table.add_column(head, [row])


    def build_table(self, tags, key, value):
        table  = PrettyTable()
        c_hist = FieldstatAPI.libfieldstat.fieldstat_histogram_base64_decode(value.encode('utf-8'))
        if self.format == "summary":
            self.__build_summary_format(c_hist, table)
        if self.format == "histogram":
            self.__build_histogram_format(c_hist, table)

        max_value = FieldstatAPI.libfieldstat.fieldstat_histogram_value_max(c_hist)
        table.add_column("MAX", [max_value])
        
        min_value = FieldstatAPI.libfieldstat.fieldstat_histogram_value_min(c_hist)
        table.add_column("MIN", [min_value])
        
        avg_value = FieldstatAPI.libfieldstat.fieldstat_histogram_value_mean(c_hist)
        table.add_column("AVG", ["{:.2f}".format(avg_value)])
        
        dev_value = FieldstatAPI.libfieldstat.fieldstat_histogram_value_stddev(c_hist)
        table.add_column("STDDEV", ["{:.2f}".format(dev_value)])
        
        cnt_value = FieldstatAPI.libfieldstat.fieldstat_histogram_value_total_count(c_hist)
        table.add_column("CNT", [cnt_value])

        FieldstatAPI.libfieldstat.fieldstat_histogram_free(c_hist)

        table.title = key + " " + tags
        self.tables.append(table)

    def read_tables_num(self):
        return len(self.tables)

    def print_tables(self):
        for item in self.tables:
            print(item)


class LocalExporter:
    def __init__(self):
        self.terminal_size, _  = shutil.get_terminal_size((128, 64))
        self.json_path         = FieldstatExporterVars.json_path
        self.ctable            = CounterTable()
        self.htable            = HistogramTable()
        self.hlltable          = CounterTable()
        self.display_counter   = FieldstatExporterVars.local_display_counter
        self.display_hist      = FieldstatExporterVars.local_display_hist
        self.display_hll       = FieldstatExporterVars.local_display_hll
        self.match_tags        = FieldstatExporterVars.local_match_tags
        self.is_counter_table  = False
        self.__set_default_display()

    def __set_default_display(self):
        #default print all type metrics
        if not (self.display_counter or self.display_hist or self.display_hll):
            self.display_counter = True
            self.display_hist    = True
            self.display_hll     = True

    def __dealwith_counter(self, tags, key, value, speed_s):
        self.ctable.add_table_column(tags, key, value, speed_s)

    def __dealwith_histogram(self, tags, key, value):
        self.htable.build_table(tags, key, value)

    def __dealwith_hll(self, tags, key, value):
        hll_value = FieldstatAPI.libfieldstat.fieldstat_hll_base64_to_count(value.encode('utf-8'))
        self.hlltable.add_table_column(tags, key, "{:.2f}".format(hll_value), 0)

    def __parse_json_tags(self, json_object):
        tags_dict = copy.deepcopy(json_object["tags"])
        tags_dict.update({"app_name": json_object["name"]})
        return json.dumps(tags_dict)

    def __get_counter_speed_value(self, key, fields, json_object):
        delta_key = key + "_delta"
        if delta_key in fields:
            delta_val = fields[delta_key]
            delta_ms  = json_object["timestamp_ms"] - json_object["timestamp_ms_delta"]
            speed_s   = delta_val * 1000 / delta_ms
            return speed_s
        return 0


    def __match_tags(self, tags):
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

    #table: one same tags + same fields keys 
    def __is_counter_table(self, json_list):
        is_first_elem = True
        prev_tags     = {}
        prev_fields   = {}

        #only one counter type json object, counter type print normal mode
        if len(json_list) <= 1:
            return False

        for json_object in json_list:
            tags = copy.deepcopy(json_object["tags"])

            if not self.__match_tags(tags):
                continue

            for _, value in json_object["fields"].items():
                if isinstance(value, str):
                    return False

            for key in self.match_tags:
                tags.pop(key, None)

            #fields no one tags. print normal mode.
            if len(tags) != 1:
                return False

            if is_first_elem:
                prev_tags     = tags
                prev_fields   = json_object["fields"]
                is_first_elem = False
                continue
            else:
                if prev_tags.keys()   == tags.keys() and \
                   prev_fields.keys() == json_object["fields"].keys():
                   continue
                else:
                    return False
        
        return True


    def __parse_json_object(self, json_object):
        tags   = self.__parse_json_tags(json_object)
        fields = json_object["fields"]

        if not self.__match_tags(json_object["tags"]):
            return

        for key,value in fields.items():
            if not isinstance(value, str):
                if key.endswith("_delta"):
                    continue
                speed_s = self.__get_counter_speed_value(key, fields, json_object)
                if self.is_counter_table:
                    self.ctable.add_field_names(fields)
                    self.ctable.append_field_rows(json_object["tags"], self.match_tags, fields)
                    continue
                else:
                    self.__dealwith_counter(tags, key, value, speed_s)
            else:
                is_hll = FieldstatAPI.libfieldstat.fieldstat_is_hll(value.encode('utf-8'))
                if is_hll:
                    self.__dealwith_hll(tags, key, value)
                else:
                    self.__dealwith_histogram(tags, key, value)

    def parse_data(self):
        if not os.path.exists(self.json_path):
            logging.error("Path: {%s} does not exist", self.json_path)
            return
        with open(self.json_path) as file:
            data = json.load(file)
            self.is_counter_table = self.__is_counter_table(data)
            for json_object in data:
                self.__parse_json_object(json_object)

    def __print_top_edge(self):
        timestamp = datetime.datetime.now().timestamp()
        formatted_time = datetime.datetime.fromtimestamp(timestamp).strftime('%a %b %d %H:%M:%S %Y')
        
        num_of_equals = (self.terminal_size - len(formatted_time)) // 2
        
        result = '=' * num_of_equals + formatted_time + '=' * num_of_equals
        print(result)

    def __print_bottom_edge(self):
        print('-' * self.terminal_size)

    def print_data(self):
        self.__print_top_edge()

        if self.display_counter:
            self.ctable.print_tables()

        if self.display_hist:
            self.htable.print_tables()

        if self.display_hll:
            self.hlltable.print_tables()

        self.__print_bottom_edge()


    @classmethod
    def run_local_exporter(cls):
        praser = cls()
        praser.parse_data()
        praser.print_data()



################################################################################
#  fieldstat exporter
################################################################################
class FieldstatExporter:
    DEFAULT_LISTEN_PORT = 8080
    DEFAULT_HIST_BINS   = [0.1,0.5,0.8,0.9,0.95,0.99]
    DEFAULT_HIST_FORMAT = "summary"
    DEFAULT_JSON_PATH   = "./fieldstat.json"
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
        parser.add_argument("-j", "--json-path",   type = str, default = self.DEFAULT_JSON_PATH,
                            help = "The input fieldstat metrics json file path.")
        return parser

    def __build_prom_parser(self, subparsers, shared_arg_parser):
        parser = subparsers.add_parser('prometheus', help='Set prometheus exporter', parents=[shared_arg_parser])
        parser.add_argument("-p", "--listen-port", type = int, default = self.DEFAULT_LISTEN_PORT,
                            help = "Specify the prometheus endpoint port to listen. i.e., 80,8080")
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
        FieldstatExporterVars.json_path   = args.json_path
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
