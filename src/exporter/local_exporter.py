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
from prettytable import PrettyTable,NONE,HEADER

import ctypes

libfieldstat = ctypes.CDLL('libfieldstat4.so')
libfieldstat.histogram_base64_decode.argtypes       = [ctypes.c_char_p]
libfieldstat.histogram_base64_decode.restype        =  ctypes.c_void_p

libfieldstat.histogram_free.argtypes                = [ctypes.c_void_p]

libfieldstat.histogram_value_at_percentile.argtypes = [ctypes.c_void_p, ctypes.c_double]
libfieldstat.histogram_value_at_percentile.restype  =  ctypes.c_longlong

libfieldstat.histogram_count_le_value.argtypes      = [ctypes.c_void_p, ctypes.c_longlong]
libfieldstat.histogram_count_le_value.restype       =  ctypes.c_longlong

libfieldstat.histogram_value_total_count.argtypes   = [ctypes.c_void_p]
libfieldstat.histogram_value_total_count.restype    =  ctypes.c_longlong

libfieldstat.histogram_value_min.argtypes           = [ctypes.c_void_p]
libfieldstat.histogram_value_min.restype            =  ctypes.c_longlong

libfieldstat.histogram_value_max.argtypes           = [ctypes.c_void_p]
libfieldstat.histogram_value_max.restype            =  ctypes.c_longlong

libfieldstat.histogram_value_mean.argtypes          = [ctypes.c_void_p]
libfieldstat.histogram_value_mean.restype           =  ctypes.c_double

libfieldstat.histogram_value_stddev.argtypes        = [ctypes.c_void_p]
libfieldstat.histogram_value_stddev.restype         =  ctypes.c_double

libfieldstat.hll_base64_to_count.argtypes           = [ctypes.c_char_p]
libfieldstat.hll_base64_to_count.restype            =  ctypes.c_double

logging.basicConfig(level=logging.DEBUG, format='%(asctime)s - %(message)s')

'''
[
    {
        "name": "-",
        "tags": {
            "send_log": "sum",
            "policy_id": 1,
            "quanlity": 0.500000
        },
        "fields": {
            "T_success_log": 1
        },
        "timestamp": 1694657637836
    },
    {
        "name": "-",
        "tags": {
            "send_log": "SECURITY-EVENT",
            "policy_id": 1,
            "quanlity": 0.500000
        },
        "fields": {
            "T_success_log": 1
        },
        "timestamp": 1694657637836
    }
]
'''

class LocalExporterVars:
    terminal_size = 1
    json_path     = ""
    hist_format   = ""
    hist_bins     = []

class CounterTable:
    INFO_COLUMN_WIDTH = 11 # len(speed/s) + 2 * (" ") + 2 * ("|")
    COLUMM_PADDING    = 3  # 2 *(" ") + "|"
    
    def __init__(self):
        self.column_size = LocalExporterVars.terminal_size - self.INFO_COLUMN_WIDTH
        self.info_column = ("", ["tags", "sum", "speed/s"])
        self.min_width   = self.INFO_COLUMN_WIDTH
        self.tables      = []
        self.columns     = []

    def build_table_column(self, tags, key, value):
        column = (key, [tags, str(value), ""])
        self.columns.append(column)
        self.min_width = max(self.min_width, len(tags), len(key), len(str(value)))
        
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
    
    def __build_tables(self):
        # One table print in screen size
        # One table per screen size
        table_size = self.column_size // (self.min_width + self.COLUMM_PADDING)
        if 0 == table_size:
            table_size = 1
        n_columns  = len(self.columns)

        for i in range(0, n_columns, table_size):
            table  = None
            l_edge = i
            r_edge = min(i + table_size, n_columns)

            if r_edge >= n_columns:
                table  = self.__build_one_table(self.columns[l_edge:])
            else:
                table  = self.__build_one_table(self.columns[l_edge:r_edge])
            
            self.tables.append(table)

    def print_tables(self):
        self.__build_tables()
        for item in self.tables:
            print(item)


class HistogramTable:
    def __init__(self):
        self.format = LocalExporterVars.hist_format
        self.bins   = LocalExporterVars.hist_bins
        self.tables = []

    def __build_summary_format(self, c_hist, table):
        for i in LocalExporterVars.hist_bins:
            header = "{:.2f}%".format(i * 100)
            row    =  libfieldstat.histogram_value_at_percentile(c_hist, float(i * 100))
            table.add_column(header, [row])

    def __build_histogram_format(self, c_hist, table):
        for i in LocalExporterVars.hist_bins:
            header = "le={:d}".format(i)
            row    =  libfieldstat.histogram_value_at_percentile(c_hist, int(i))
            table.add_column(header, [row])


    def build_table(self, tags, key, value):
        table  = PrettyTable()
        c_hist = libfieldstat.histogram_base64_decode(value.encode('utf-8'))
        if self.format == "summary":
            self.__build_summary_format(c_hist, table)
        if self.format == "histogram":
            self.__build_histogram_format(c_hist, table)

        max_value = libfieldstat.histogram_value_max(c_hist)
        table.add_column("MAX", [max_value])
        
        min_value = libfieldstat.histogram_value_min(c_hist)
        table.add_column("MIN", [min_value])
        
        avg_value = libfieldstat.histogram_value_mean(c_hist)
        table.add_column("AVG", ["{:.2f}".format(avg_value)])
        
        dev_value = libfieldstat.histogram_value_stddev(c_hist)
        table.add_column("STDDEV", ["{:.2f}".format(dev_value)])
        
        cnt_value = libfieldstat.histogram_value_total_count(c_hist)
        table.add_column("CNT", [cnt_value])

        libfieldstat.histogram_free(c_hist)

        table.title = key + " " + tags
        self.tables.append(table)

    def print_tables(self):
        for item in self.tables:
            print(item)

class JsonDataParser:
    def __init__(self):
        self.json_path = LocalExporterVars.json_path
        self.ctable    = CounterTable()
        self.htable    = HistogramTable()
        self.hlltable  = CounterTable() 

    def __dealwith_counter(self, tags, key, value):
        self.ctable.build_table_column(tags, key, value)

    def __dealwith_histogram(self, tags, key, value):
        self.htable.build_table(tags, key, value)

    def __dealwith_hll(self, tags, key, value):
        hll_value = libfieldstat.hll_base64_to_count(value.encode('utf-8'))
        self.hlltable.build_table_column(tags, key, "{:.2f}".format(hll_value))

    def __parse_json_tags(self, json_object):
        tags_dict = json_object["tags"]
        tags_dict.update({"app_name": json_object["name"]})
        return json.dumps(tags_dict)

    def __parse_json_object(self, json_object):
        tags   = self.__parse_json_tags(json_object)
        fields = json_object["fields"]

        for key,value in fields.items():
            if isinstance(value, int):
                self.__dealwith_counter(tags, key, value)
            else:
                is_hll = libfieldstat.is_hll(value.encode('utf-8'))
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
            for json_object in data:
                self.__parse_json_object(json_object)

    def __print_top_edge(self):
        timestamp = datetime.datetime.now().timestamp()
        formatted_time = datetime.datetime.fromtimestamp(timestamp).strftime('%a %b %d %H:%M:%S %Y')
        
        num_of_equals = (LocalExporterVars.terminal_size - len(formatted_time)) // 2
        
        result = '=' * num_of_equals + formatted_time + '=' * num_of_equals
        print(result)

    def __print_bottom_edge(self):
        print('-' * LocalExporterVars.terminal_size)

    def print_data(self):
        self.__print_top_edge()
        self.ctable.print_tables()
        print("\n")
        self.htable.print_tables()
        print("\n")
        self.hlltable.print_tables()
        self.__print_bottom_edge()

    @classmethod
    def run_json_data_parser(cls):
        praser = cls()
        praser.parse_data()
        praser.print_data()

class LocalExporter:
    DEFAULT_HIST_BINS   = [0.1,0.5,0.8,0.9,0.95,0.99]
    DEFAULT_HIST_FORMAT = "summary"
    DEFAULT_JSON_PATH   = "./fieldstat.json"
    DEFAULT_INTERVAL_S  = 1

    def __init__(self):
        self.is_loop    = False
        self.interval_s = self.DEFAULT_INTERVAL_S

    def __verify_cmd_args(self, args):
        if args.hist_format not in ["summary", "histogram"]:
            logging.error("When historgram format must be 'summary' or 'histogram'")
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
                if not re.match(r'^\d+(\.\d+)?$', bin):
                    logging.error("When historgram format is %s, bins value is integer or decimal, "
                                  "bins format example: 0.1,1,10,100,1000", args.hist_format)
                    return -1
        return 0

    def __parse_bins_str(self, bins_str):
        bins = [] 
        for item in bins_str.split(','):
            bins.append(float(item))
        return list(set(bins))

    def read_cmd_options(self):
        bins_str = ','.join([str(x) for x in self.DEFAULT_HIST_BINS])

        parser = argparse.ArgumentParser(description='Fieldstat local exporter')

        parser.add_argument("-b", "--hist-bins",   type = str, default = bins_str,
                            help = "The metrics of histogram type output bins.")
        parser.add_argument("-f", "--hist-format", type = str, default = self.DEFAULT_HIST_FORMAT,
                            help = "The metrics of histogram type output format.")
        parser.add_argument("-j", "--json-path",   type = str, default = self.DEFAULT_JSON_PATH,
                            help = "The input fieldstat metrics json file path.")
        parser.add_argument("-i", "--interval",    type = int, default = self.DEFAULT_INTERVAL_S,
                            help = "interval, seconds to wait between print.")

        parser.add_argument("-l", "--loops",     action='store_true',  default = False,
                            help = "print loop, exit when recv a signal.")

        args = parser.parse_args()

        if -1 == self.__verify_cmd_args(args):
            parser.print_help()
            sys.exit(1)

        LocalExporterVars.hist_format = args.hist_format
        LocalExporterVars.json_path   = args.json_path
        LocalExporterVars.hist_bins   = self.__parse_bins_str(args.hist_bins)
        
        self.interval_s = args.interval
        self.is_loop    = args.loops

    def local_export(self):
        try:
            while True:
                LocalExporterVars.terminal_size, _ = shutil.get_terminal_size((128, 64))
                JsonDataParser.run_json_data_parser()
                if not self.is_loop:
                    break;
                time.sleep(self.interval_s)
        except KeyboardInterrupt:
            pass

    @classmethod
    def run_local_exporter(cls):
        exporter = cls()
        exporter.read_cmd_options()
        exporter.local_export()


        
if __name__ == '__main__':
    LocalExporter.run_local_exporter()
