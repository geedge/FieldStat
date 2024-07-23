#!/usr/bin/python3

import unittest
import sys
import urllib
from urllib.request import urlopen
from threading import Thread
from http.server import HTTPServer, BaseHTTPRequestHandler
import argparse
import random
import math
from io import StringIO
from unittest.mock import patch
from contextlib import redirect_stdout
from prettytable import PrettyTable,NONE,HEADER

import os
current_path  = os.path.dirname(os.path.abspath(__file__))
sys.path.append(current_path + '/../../src/exporter')

from fieldstat_exporter import FieldstatAPI

from fieldstat_exporter import FieldstatExporterVars

from fieldstat_exporter import FieldstatExporter
from fieldstat_exporter import PrometheusExporter
from fieldstat_exporter import PrometheusEndpoint

from fieldstat_exporter import CounterTable
from fieldstat_exporter import HistogramTable
from fieldstat_exporter import LocalExporter



FIELDSTAT_INPUT_JSON_PATH = "/tmp/fieldstat.json"


class TestPrometheusExporter(unittest.TestCase):
    def setUp(self):
        self.hist_val = "HISTEwAAAGQAAAAAAAAAAwAAAAAAAAABAAAAAAAJJ8A/8AAAAAAAA"\
                        "AEEAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAg"\
                        "ICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgI"\
                        "CAgICAgICAgICAgICAgICAgICAgIAAAAAAAAA"
        self.prom     = PrometheusExporter()


    def test__escape_metric_name(self):
        name = "tsg_master_log:(){}/\\%*$-,;"
        ret  = self.prom._PrometheusExporter__escape_metric_name(name)
        self.assertEqual(ret, "tsg_master_log:____________")

    def test__escape_metric_tags(self):
        json_obj = {'name': '-',
                    'tags': {
                        'send(){}/\\%*$-,;': 'sum', 
                        'policy_id': 1, 
                        'quanlity': 0.5
                    },
                    'fields': {'T_success_log': 1},
                    'timestamp': 1694657637836}
        tags = self.prom._PrometheusExporter__escape_metric_tags(json_obj)

        self.assertEqual(tags, "send____________=\"sum\",policy_id=\"1\",quanlity=\"0.5\",app_name=\"-\"")

    def test__build_type_counter(self):
        name  = "tsg_master_log"
        tags  = "send_log=\"sum\",policy_id=\"1\",app_name=\"-\""
        value = 100 

        metric = self.prom._PrometheusExporter__build_type_counter(name, tags, value)
        self.assertEqual(metric, "tsg_master_log{send_log=\"sum\",policy_id=\"1\",app_name=\"-\"} 100\n")


    def test__build_histogram_format(self):
        c_hist = FieldstatAPI.libfieldstat.fieldstat_histogram_base64_decode(self.hist_val.encode('utf-8'))
        name   = "tsg_master_log"
        tags   = "policy_id=\"1\",app_name=\"-\""
        self.prom.hist_bins = [10,20,50,80,90,95,99]

        metrics = self.prom._PrometheusExporter__build_histogram_format(name, tags, c_hist)

        desired = "tsg_master_log_bucket{policy_id=\"1\",app_name=\"-\",le=\"10.00\"} 10\n"\
                  "tsg_master_log_bucket{policy_id=\"1\",app_name=\"-\",le=\"20.00\"} 20\n"\
                  "tsg_master_log_bucket{policy_id=\"1\",app_name=\"-\",le=\"50.00\"} 50\n"\
                  "tsg_master_log_bucket{policy_id=\"1\",app_name=\"-\",le=\"80.00\"} 80\n"\
                  "tsg_master_log_bucket{policy_id=\"1\",app_name=\"-\",le=\"90.00\"} 90\n"\
                  "tsg_master_log_bucket{policy_id=\"1\",app_name=\"-\",le=\"95.00\"} 95\n"\
                  "tsg_master_log_bucket{policy_id=\"1\",app_name=\"-\",le=\"99.00\"} 99\n"

        self.assertEqual(metrics, desired)


    def test__build_summary_format(self):
        c_hist = FieldstatAPI.libfieldstat.fieldstat_histogram_base64_decode(self.hist_val.encode('utf-8'))
        name   = "tsg_master_log"
        tags   = "policy_id=\"1\",app_name=\"-\""
        self.prom.hist_bins = [0.1,0.2,0.5,0.8,0.9,0.95,0.99]

        metrics = self.prom._PrometheusExporter__build_summary_format(name, tags, c_hist)

        desired = "tsg_master_log{policy_id=\"1\",app_name=\"-\",quantile=\"10.00%\"} 9\n" \
                  "tsg_master_log{policy_id=\"1\",app_name=\"-\",quantile=\"20.00%\"} 19\n"\
                  "tsg_master_log{policy_id=\"1\",app_name=\"-\",quantile=\"50.00%\"} 49\n"\
                  "tsg_master_log{policy_id=\"1\",app_name=\"-\",quantile=\"80.00%\"} 79\n"\
                  "tsg_master_log{policy_id=\"1\",app_name=\"-\",quantile=\"90.00%\"} 89\n"\
                  "tsg_master_log{policy_id=\"1\",app_name=\"-\",quantile=\"95.00%\"} 94\n"\
                  "tsg_master_log{policy_id=\"1\",app_name=\"-\",quantile=\"99.00%\"} 98\n"
        self.assertEqual(metrics, desired)


    def test__build_type_histogram(self):
        name  = "tsg_master_log"
        tags  = "policy_id=\"1\",app_name=\"-\""
        value = self.hist_val

        self.prom.hist_bins   = [0.1,0.2,0.5,0.8,0.9,0.95,0.99]
        self.prom.hist_format = "summary"
        
        metrics = self.prom._PrometheusExporter__build_type_histogram(name, tags, value)
        
        desired = "tsg_master_log{policy_id=\"1\",app_name=\"-\",quantile=\"10.00%\"} 9\n" \
                  "tsg_master_log{policy_id=\"1\",app_name=\"-\",quantile=\"20.00%\"} 19\n"\
                  "tsg_master_log{policy_id=\"1\",app_name=\"-\",quantile=\"50.00%\"} 49\n"\
                  "tsg_master_log{policy_id=\"1\",app_name=\"-\",quantile=\"80.00%\"} 79\n"\
                  "tsg_master_log{policy_id=\"1\",app_name=\"-\",quantile=\"90.00%\"} 89\n"\
                  "tsg_master_log{policy_id=\"1\",app_name=\"-\",quantile=\"95.00%\"} 94\n"\
                  "tsg_master_log{policy_id=\"1\",app_name=\"-\",quantile=\"99.00%\"} 98\n"\
                  "tsg_master_log_sum{policy_id=\"1\",app_name=\"-\"} 4950\n"\
                  "tsg_master_log_count{policy_id=\"1\",app_name=\"-\"} 100\n"
        self.assertEqual(metrics, desired)
        
        self.prom.hist_bins   = [10,20,50,80,90,95,99]
        self.prom.hist_format = "histogram"
        
        metrics = self.prom._PrometheusExporter__build_type_histogram(name, tags, value)

        desired = "tsg_master_log_bucket{policy_id=\"1\",app_name=\"-\",le=\"10.00\"} 10\n"\
                  "tsg_master_log_bucket{policy_id=\"1\",app_name=\"-\",le=\"20.00\"} 20\n"\
                  "tsg_master_log_bucket{policy_id=\"1\",app_name=\"-\",le=\"50.00\"} 50\n"\
                  "tsg_master_log_bucket{policy_id=\"1\",app_name=\"-\",le=\"80.00\"} 80\n"\
                  "tsg_master_log_bucket{policy_id=\"1\",app_name=\"-\",le=\"90.00\"} 90\n"\
                  "tsg_master_log_bucket{policy_id=\"1\",app_name=\"-\",le=\"95.00\"} 95\n"\
                  "tsg_master_log_bucket{policy_id=\"1\",app_name=\"-\",le=\"99.00\"} 99\n"\
                  "tsg_master_log_sum{policy_id=\"1\",app_name=\"-\"} 4950\n"\
                  "tsg_master_log_count{policy_id=\"1\",app_name=\"-\"} 100\n"
        self.assertEqual(metrics, desired)


    def test__build_type_hll(self):
        name  = "tsg_master_log"
        tags  = "policy_id=\"1\",app_name=\"-\""
        value = "AQUBDECDAQxAQQUIIEEJCDCFARgQRAUIMIMAAAECAAAAAAAAAA=="
        
        metric = self.prom._PrometheusExporter__build_type_hll(name, tags, value)

        self.assertEqual(metric, "tsg_master_log{policy_id=\"1\",app_name=\"-\"} 62.61\n")


    def test__build_metrics(self):
        counter_dict = {"name": "-",
                        "tags": {
                            "send_log": "PROXY-EVENT",
                            "policy_id": 1,
                            "quanlity": 0.50
                        },
                        "fields": {
                            "T_success_log": 1
                        },
                        "fields_delta": {
                            "T_success_log": 1
                        },
                        "timestamp_ms": 100010,
                        "timestamp_ms_delta": 1000
                        }
        metrics = self.prom._PrometheusExporter__build_metrics(counter_dict)
        self.assertEqual(metrics, "T_success_log{send_log=\"PROXY-EVENT\",policy_id=\"1\",quanlity=\"0.5\",app_name=\"-\"} 1\n")

        hll_dict = {"name": "-",
                    "tags": {
                        "rule_id": 1
                    },
                    "fields": {
                        "external_ip": "AQUBDECDAQxAQQUIIEEJCDCFARgQRAUIMIMAAAECAAAAAAAAAA==",
                    },
                    "timestamp_ms": 100010,
                    "timestamp_ms_delta": 100010
                }
        metrics = self.prom._PrometheusExporter__build_metrics(hll_dict)
        self.assertEqual(metrics, "external_ip{rule_id=\"1\",app_name=\"-\"} 62.61\n")

        hist_dict = {"name": "-",
                     "tags": {
                         "thread_id": 1,
                         "hit_rate": 1.10,
                         "rule_id": 1,
                         "action": "deny"
                     },
                     "fields": {
                         "list_num": "HISTEwAAAGQAAAAAAAAAAwAAAAAAAAABAAAAAAAJJ8A/8AAAAAAAAAEEAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgIAAAAAAAAA"
                     },
                     "timestamp_ms": 100010,
                     "timestamp_ms_delta": 0
                    }
        self.prom.hist_bins   = [0.1,0.2,0.5,0.8,0.9,0.95,0.99]
        self.prom.hist_format = "summary"
        metrics = self.prom._PrometheusExporter__build_metrics(hist_dict)
        
        desired = "list_num{thread_id=\"1\",hit_rate=\"1.1\",rule_id=\"1\",action=\"deny\",app_name=\"-\",quantile=\"10.00%\"} 9\n"\
                  "list_num{thread_id=\"1\",hit_rate=\"1.1\",rule_id=\"1\",action=\"deny\",app_name=\"-\",quantile=\"20.00%\"} 19\n"\
                  "list_num{thread_id=\"1\",hit_rate=\"1.1\",rule_id=\"1\",action=\"deny\",app_name=\"-\",quantile=\"50.00%\"} 49\n"\
                  "list_num{thread_id=\"1\",hit_rate=\"1.1\",rule_id=\"1\",action=\"deny\",app_name=\"-\",quantile=\"80.00%\"} 79\n"\
                  "list_num{thread_id=\"1\",hit_rate=\"1.1\",rule_id=\"1\",action=\"deny\",app_name=\"-\",quantile=\"90.00%\"} 89\n"\
                  "list_num{thread_id=\"1\",hit_rate=\"1.1\",rule_id=\"1\",action=\"deny\",app_name=\"-\",quantile=\"95.00%\"} 94\n"\
                  "list_num{thread_id=\"1\",hit_rate=\"1.1\",rule_id=\"1\",action=\"deny\",app_name=\"-\",quantile=\"99.00%\"} 98\n"\
                  "list_num_sum{thread_id=\"1\",hit_rate=\"1.1\",rule_id=\"1\",action=\"deny\",app_name=\"-\"} 4950\n"\
                  "list_num_count{thread_id=\"1\",hit_rate=\"1.1\",rule_id=\"1\",action=\"deny\",app_name=\"-\"} 100\n"
        self.assertEqual(metrics, desired)


    def test_build_metrics_payload(self):
        self.prom.hist_bins   = [0.1,0.2,0.5,0.8,0.9,0.95,0.99]
        self.prom.hist_format = "summary"
        self.prom.n_lines     = 0

        payload = self.prom.build_metrics_payload("/tmp/invalid_path.json")
        self.assertEqual(payload, "")
        payload = self.prom.build_metrics_payload(FIELDSTAT_INPUT_JSON_PATH)
        lines   = payload.split('\n')
        self.assertEqual(len(lines), 93 + 1)
        self.assertEqual(self.prom.read_lines_num(), 93)

class TestPrometheusEndpoint(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        FieldstatExporterVars.prom_uri_path = "/metrics"
        server_address = ('', 40001)
        cls.httpd = HTTPServer(server_address, PrometheusEndpoint)
        cls.server_thread = Thread(target=cls.httpd.serve_forever)
        cls.server_thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()
        cls.httpd.server_close()
        cls.server_thread.join()

    def test_valid_request(self):
        response = urlopen('http://localhost:40001/metrics')
        self.assertEqual(response.getcode(), 200)

    def test_invalid_request(self):
        try:
            urlopen('http://localhost:40001/invalid')
        except urllib.error.HTTPError as e:
            self.assertEqual(e.code, 404)


class TestCounterTable(unittest.TestCase):
    def setUp(self):
        self.c_table = CounterTable()

    # def test_create_row_table(self):
    #     fields_names_0 = {"column0": 0, "column1": 1}
    #     table = self.c_table.create_row_table(fields_names_0)
    #     self.assertEqual(table.field_names, ["", "column0", "column1"])

    #     fields_names_1 = {"column2": 2, "column3": 3}
    #     table = self.c_table.create_row_table(fields_names_1)
    #     self.assertEqual(table.field_names, ["", "column2", "column3"])

    # def test_add_row_table_row(self):
    #     self.c_table.field_names = []

    #     table = self.c_table.create_row_table({"column0": 0, "column1": 1})
    #     tags  = {"row": "0"}
    #     field = {"column0": 0, "column1":1}
    #     self.c_table.add_row_table_row(None,  tags, field)

    #     table_str = table.get_string()
    #     row_count = len(table_str.split("\n")) - 1
    #     self.assertEqual(row_count, 0)
        
    #     self.c_table.add_row_table_row(table, tags, field)
    #     row_count = len(table_str.split("\n")) - 1
    #     self.assertEqual(row_count, 0)

    def test_add_table_column(self):
        head    = "policy_hit"
        tags    = "{\"thread_id\": 1,\"action\": \"deny\"}"
        value   = 100
        speed_s = 1.1
        
        self.c_table.columns = []
        self.c_table.add_table_column(tags, head, value, speed_s)

        self.assertEqual(len(self.c_table.columns), 1)
        self.assertEqual(self.c_table.read_columns_num(), 1)
        self.assertEqual(self.c_table.columns[-1], ("policy_hit", ["{\"thread_id\": 1,\"action\": \"deny\"}", "100", "1.10"]))


    def test__build_one_table(self):
        columns = [("policy_hit", ["{\"thread_id\": 1,\"action\": \"deny\"}", "100", "1.10"])]
        table   = self.c_table._CounterTable__build_one_table(columns)

        self.assertEqual(len(table.field_names), 2)


    def test_build_columns_tables(self):
        self.c_table.columns = []
        for i in range(100):
            head = "h" + str(i)
            self.c_table.columns.append((head, ["{\"thread_id\": 1,\"action\": \"deny\"}", "100", "1.10"]))

        self.c_table.min_width = 3
        for _ in range(5):
            self.c_table.column_size = random.randint(1, 100)
            self.c_table.tables      = []
            self.c_table.build_columns_tables()

            table_size = self.c_table.column_size //(self.c_table.min_width + self.c_table.COLUMM_PADDING)
            if 0 == table_size:
                table_size = 1

            self.assertEqual(len(self.c_table.tables),       math.ceil(100/table_size))
            self.assertEqual(self.c_table.read_tables_num(), math.ceil(100/table_size))


    def test_print_tables(self):
        self.c_table.columns = []
        for i in range(100):
            head = "h" + str(i)
            self.c_table.columns.append((head, ["{\"thread_id\": 1,\"action\": \"deny\"}", "100", "1.10"]))

        self.c_table.min_width = 3
        for _ in range(5):
            self.c_table.column_size = random.randint(1, 100)
            self.c_table.tables      = []

            table_size = self.c_table.column_size //(self.c_table.min_width + self.c_table.COLUMM_PADDING)
            if 0 == table_size:
                table_size = 1

            output     = StringIO()
            sys.stdout = output
            self.c_table.print_tables()
            output_str = output.getvalue()
            sys.stdout = sys.__stdout__

            self.assertEqual(len(self.c_table.tables),       math.ceil(100/table_size))
            self.assertEqual(self.c_table.read_tables_num(), math.ceil(100/table_size))
            self.assertEqual(len(output_str.split('\n')),    math.ceil(100/table_size) * 4 + 2)


class TestHistogramTable(unittest.TestCase):
    def setUp(self):
        self.h_table  = HistogramTable()
        self.hist_val = "HISTEwAAAGQAAAAAAAAAAwAAAAAAAAABAAAAAAAJJ8A/8AAAAAAAA"\
                        "AEEAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAg"\
                        "ICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgI"\
                        "CAgICAgICAgICAgICAgICAgICAgIAAAAAAAAA"
        self.c_hist   = FieldstatAPI.libfieldstat.fieldstat_histogram_base64_decode(self.hist_val.encode('utf-8'))


    def test__get_row_shared_values(self):
        shared_values = self.h_table._HistogramTable__get_row_shared_values(self.c_hist)
        self.assertEqual(shared_values, ['99', '1', '49.51', '28.85', '100'])


    def test__get_row_values(self):
        self.h_table.bins   = [0.1, 0.5, 0.8, 0.9, 0.95, 0.99]
        self.h_table.format = "summary"
        row_values = self.h_table._HistogramTable__get_row_values(self.c_hist)
        self.assertEqual(row_values, ['9', '49', '79', '89', '94', '98', '99', '1', '49.51', '28.85', '100'])
        self.h_table.bins   = [10, 50, 80, 90, 95, 99]
        self.h_table.format = "histogram"
        row_values = self.h_table._HistogramTable__get_row_values(self.c_hist)
        self.assertEqual(row_values, ['10', '50', '80', '90', '95', '99', '99', '1', '49.51', '28.85', '100'])


    def test__add_table_field_names(self):
        table_summ = PrettyTable()
        self.h_table.bins   = [0.1, 0.5, 0.8, 0.9, 0.95, 0.99]
        self.h_table.format = "summary"
        self.h_table._HistogramTable__add_table_field_names(table_summ)
        self.assertEqual(table_summ.field_names, ['', '10.00%', '50.00%', '80.00%', '90.00%', '95.00%', '99.00%', 'MAX', 'MIN', 'AVG', 'STDDEV', 'CNT'])

        table_hist = PrettyTable()
        self.h_table.bins   = [10, 50, 80, 90, 95, 99]
        self.h_table.format = "histogram"
        self.h_table._HistogramTable__add_table_field_names(table_hist)
        self.assertEqual(table_hist.field_names, ['', 'le=10', 'le=50', 'le=80', 'le=90', 'le=95', 'le=99', 'MAX', 'MIN', 'AVG', 'STDDEV', 'CNT'])


    def test__add_table_row(self):
        table = PrettyTable()
        self.h_table.bins   = [0.1, 0.5, 0.8, 0.9, 0.95, 0.99]
        self.h_table.format = "summary"
        self.h_table._HistogramTable__add_table_field_names(table)
        self.h_table._HistogramTable__add_table_row(table, self.hist_val, "acc")
        table_str = table.get_string()
        row_count = len(table_str.split("\n"))
        self.assertEqual(row_count, 5)
        self.h_table._HistogramTable__add_table_row(table, self.hist_val, "delta")
        table_str = table.get_string()
        row_count = len(table_str.split("\n"))
        self.assertEqual(row_count, 6)


    def test_build_table(self):
        tags = "{\"thread_id\": 1}"
        key  = "hit_policy"
        self.h_table.build_table(tags, key, self.hist_val, self.hist_val, 1000)
        table_str = self.h_table.tables[-1].get_string()
        row_count = len(table_str.split("\n"))
        self.assertEqual(row_count, 7)


    def test_build_table(self):
        tags  = "{\"thread_id\": 1,\"action\": \"deny\"}"
        key   = "policy_hit"
        self.h_table.format = "summary"

        for _ in range(5):
            n_bins = random.randint(1, 100)
            self.h_table.bins = []
            for i in range(1, n_bins + 1):
                self.h_table.bins.append(i * 0.01)
            self.h_table.build_table(tags, key, self.hist_val, self.hist_val, 1000)
            table = self.h_table.tables[-1]
            self.assertEqual(len(table.field_names), n_bins + 6)
            table_str = self.h_table.tables[-1].get_string()
            row_count = len(table_str.split("\n"))
            self.assertEqual(row_count, 7)


    def test_print_tables(self):
        tags  = "{\"thread_id\": 1,\"action\": \"deny\"}"
        key   = "policy_hit"
        value = self.hist_val

        for _ in range(5):
            n_operate = random.randint(1, 100)
            self.h_table.tables = []
            for _ in range (n_operate):
                self.h_table.build_table(tags, key, self.hist_val, None, 1000)
            
            output     = StringIO()
            sys.stdout = output
            self.h_table.print_tables()
            output_str = output.getvalue()
            sys.stdout = sys.__stdout__
            
            self.assertEqual(len(self.h_table.tables),    n_operate)
            self.assertEqual(len(output_str.split('\n')), n_operate * 6 + 2)


    def tearDown(self):
        FieldstatAPI.libfieldstat.fieldstat_histogram_free(self.c_hist)



class TestLocalExporter(unittest.TestCase):
    def setUp(self):
        self.local    = LocalExporter()
        self.tags     = "{\"thread_id\": 1,\"action\": \"deny\"}"
        self.key      = "policy_hit"


        self.counter_json_object = { "name": "-",
                                     "tags": {
                                         "send_log": "sum"
                                     },
                                     "fields": {
                                         "T_fail_log": 2
                                     },
                                     "fields_delta": {
                                         "T_fail_log": 2
                                     },
                                     "timestamp_ms": 1000,
                                     "timestamp_ms_delta": 1000
                                   }
        
        self.hll_json_object     = { "name": "-",
                                     "tags": {
                                         "rule_id": 1
                                     },
                                     "fields": {
                                         "acc_ip": "AQUFEGDCAhAwhAMMIQQBBBCDBRBggQMEMIcAAADCAAAAAAAAAA=="
                                     },
                                     "timestamp_ms": 100010,
                                     "timestamp_ms_delta": 0
                                    }


        self.hist_json_object    = { "name": "-",
                                     "tags": {
                                         "action": "deny"
                                     },
                                     "fields": {
                                         "list_num": "HISTEwAAAGQAAAAAAAAAAwAAAAAAAAABAAAAAAAJJ8A/8AAAAAAA"\
                                                     "AAEEAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgIC"\
                                                     "AgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgIC"\
                                                     "AgICAgICAgICAgICAgICAgICAgICAgIAAAAAAAAA"
                                     },
                                     "timestamp_ms": 100010,
                                     "timestamp_ms_delta": 0
                                    }


    def test__set_default_display(self):
        for i in [True, False]:
            for j in [True, False]:
                for k in [True, False]:
                    self.local.display_counter = i
                    self.local.display_hist    = j
                    self.local.display_hll     = k
                    self.local._LocalExporter__set_default_display()
                    if not (i or j or k):
                        self.assertEqual(self.local.display_counter, True)
                        self.assertEqual(self.local.display_hist,    True)
                        self.assertEqual(self.local.display_hll,     True)
                    else:
                        self.assertEqual(self.local.display_counter, i)
                        self.assertEqual(self.local.display_hist,    j)
                        self.assertEqual(self.local.display_hll,     k)


    def test__build_counter_type_exporter(self):
        for _ in range(5):
            val        = random.randint(1, 100)
            val_delta  = random.randint(10, 20)
            tsms_delta = random.randint(1, 10) * 1000
            self.local.ctable = CounterTable()
            self.local._LocalExporter__build_counter_type_exporter(self.tags, self.key, val, val_delta, tsms_delta)
            self.assertEqual(self.local.ctable.columns[-1][1][1], str(val))
            self.assertEqual(self.local.ctable.columns[-1][1][2],  "{:.2f}".format(val_delta*1000/tsms_delta))


    def test__build_histogram_type_exporter(self):
        hist_val = "HISTEwAAAGQAAAAAAAAAAwAAAAAAAAABAAAAAAAJJ8A/8AAAAAAAA"\
                   "AEEAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAg"\
                   "ICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgI"\
                   "CAgICAgICAgICAgICAgICAgICAgIAAAAAAAAA"
        self.local.htable = HistogramTable()
        peradd   = len(self.local.htable.tables)
        self.local._LocalExporter__build_histogram_type_exporter(self.tags, self.key, hist_val, None, 0)
        postadd  = len(self.local.htable.tables)
        self.assertEqual(postadd - peradd, 1)


    def test__build_hll_type_exporter(self):
        value   = "AQUBDECDAQxAQQUIIEEJCDCFARgQRAUIMIMAAAECAAAAAAAAAA=="
        self.local.hlltable = CounterTable()
        peradd  = len(self.local.hlltable.columns)
        self.local._LocalExporter__build_hll_type_exporter(self.tags, self.key, value)
        postadd = len(self.local.hlltable.columns)
        
        self.assertEqual(postadd - peradd, 1)

    def test__is_tags_matched(self):
        self.local.match_tags = {}
        tags = {"action": "deny", "policy_id": 0, "hit_rate": 1.1}
        ret = self.local._LocalExporter__is_tags_matched(tags)
        self.assertEqual(ret, True)

        self.local.match_tags = {"action": "deny"}
        tags = {}
        ret = self.local._LocalExporter__is_tags_matched(tags)
        self.assertEqual(ret, False)

        self.local.match_tags = {"action": "deny", "policy_id": 0, "hit_rate": 1.1}
        tags = {"action": "deny"}
        ret = self.local._LocalExporter__is_tags_matched(tags)
        self.assertEqual(ret, False)

        self.local.match_tags = {"action": "deny", "policy_id": 0, "hit_rate": 1.10}
        tags = {"action": "deny", "policy_id": 0, "hit_rate": 1.1}
        ret = self.local._LocalExporter__is_tags_matched(tags)
        self.assertEqual(ret, True)


    def test_read_match_tags_json_objects(self):
        json_objects = [{ "name": "-",
                        "tags": {
                            "send_log": "sum"
                        },
                        "fields": {
                            "T_fail_log": 2
                        },
                        "fields_delta": {
                            "T_fail_log": 2
                        },
                        "timestamp_ms": 1000,
                        "timestamp_ms_delta": 0
                        },
                        { "name": "-",
                        "tags": {
                            "send_log": "firewall"
                        },
                        "fields": {
                            "T_fail_log": 2
                        },
                        "fields_delta": {
                            "T_fail_log": 2
                        },
                        "timestamp_ms": 1000,
                        "timestamp_ms_delta": 0
                        }]

        self.local.match_tags = {}
        match_object = self.local.read_match_tags_json_objects(json_objects)
        self.assertEqual(len(match_object), 2)
    
        self.local.match_tags = {"test": 1}
        match_object = self.local.read_match_tags_json_objects(json_objects)
        self.assertEqual(len(match_object), 0)

        self.local.match_tags = {"send_log": "firewall"}
        match_object = self.local.read_match_tags_json_objects(json_objects)
        self.assertEqual(len(match_object), 1)


    def test_read_json_objects_from_file(self):
        self.local.hlltable = CounterTable()
        self.local.ctable   = CounterTable()
        self.local.htable   = HistogramTable()

        objects0 = self.local.read_json_objects_from_file("/tmp/noexist.json")
        self.assertEqual(len(objects0), 0)

        objects1 = self.local.read_json_objects_from_file(FIELDSTAT_INPUT_JSON_PATH)
        self.assertGreater(len(objects1), 0)


class TestFieldstatExporter(unittest.TestCase):
    def setUp(self):
        self.exporter = FieldstatExporter()


    def test_build_shared_args_parser(self):
        parser = self.exporter._FieldstatExporter__build_shared_args_parser()

        self.assertEqual(parser.parse_args([]).hist_bins,   "0.1,0.5,0.8,0.9,0.95,0.99")
        self.assertEqual(parser.parse_args([]).hist_format, "summary")
        self.assertEqual(parser.parse_args([]).json_paths,  [])

        self.assertEqual(parser.parse_args(["-b", "0.1,0.5,0.8,0.99"]).hist_bins, "0.1,0.5,0.8,0.99")
        self.assertEqual(parser.parse_args(["-f", "histogram"]).hist_format, "histogram")
        self.assertEqual(parser.parse_args(["-j", "/tmp/sapp_fs.json"]).json_paths, ["/tmp/sapp_fs.json"])


    def test_build_prom_parser(self):
        parser = argparse.ArgumentParser(description='Fieldstat exporter')
        shared_args_parser = self.exporter._FieldstatExporter__build_shared_args_parser()
        subparsers = parser.add_subparsers(dest='command')
        subparsers.required = True
        self.exporter._FieldstatExporter__build_prom_parser(subparsers, shared_args_parser)

        args = parser.parse_args(["prometheus"])
        self.assertEqual(args.listen_port, 8080)
        self.assertEqual(args.uri_path,    "/metrics")

        args = parser.parse_args(["prometheus", "-p", "40000", "-u", "/sapp"])
        self.assertEqual(args.listen_port, 40000)
        self.assertEqual(args.uri_path,    "/sapp")


    def test_build_local_parser(self):
        parser = argparse.ArgumentParser(description='Fieldstat exporter')
        shared_args_parser = self.exporter._FieldstatExporter__build_shared_args_parser()
        subparsers = parser.add_subparsers(dest='command')
        subparsers.required = True
        self.exporter._FieldstatExporter__build_local_parser(subparsers, shared_args_parser)

        args = parser.parse_args(["local"])
        self.assertEqual(args.interval,     1)
        self.assertEqual(args.loop,         False)
        self.assertEqual(args.clear_screen, False)

        args = parser.parse_args(["local", "--loop", "--clear-screen", "-i", "1000", "--display-hist", "--display-hll", "--display-counter", "--match-tags", "policy:1,rule:1"])
        self.assertEqual(args.interval,        1000)
        self.assertEqual(args.loop,            True)
        self.assertEqual(args.clear_screen,    True)
        self.assertEqual(args.display_counter, True)
        self.assertEqual(args.display_hist,    True)
        self.assertEqual(args.display_hll,     True)
        self.assertEqual(args.match_tags,      "policy:1,rule:1")

  
    def test_parse_bins_str(self):
        bins_str = "0.1,1,10,20,50,80,90,99"
        bins     = self.exporter._FieldstatExporter__parse_bins_str(bins_str)
        self.assertEqual(bins, [0.1, 1.0, 10.0, 20.0, 50.0, 80.0, 90.0, 99.0])
        
    def test_parse_tags_str(self):
        tags_str = "policy:1,rule:intercept"
        tags     = self.exporter._FieldstatExporter__parse_tags_str(tags_str)
        self.assertEqual(tags, {'policy': 1, 'rule': 'intercept'})

    def test_read_shared_args_value(self):
        parser = self.exporter._FieldstatExporter__build_shared_args_parser()

        args = parser.parse_args()
        self.exporter._FieldstatExporter__read_shared_args_value(args)

        self.assertEqual(FieldstatExporterVars.hist_format, "summary")
        self.assertEqual(FieldstatExporterVars.hist_bins,   [0.1, 0.5, 0.8, 0.9, 0.95, 0.99])
        self.assertEqual(FieldstatExporterVars.json_paths,  [])

        args = parser.parse_args(["-f", "histogram", "-b", "1,2,3,4,5", "-j", FIELDSTAT_INPUT_JSON_PATH])
        self.exporter._FieldstatExporter__read_shared_args_value(args)

        self.assertEqual(FieldstatExporterVars.hist_format, "histogram")
        self.assertEqual(FieldstatExporterVars.hist_bins,   [1.0, 2.0, 3.0, 4.0, 5.0])
        self.assertEqual(FieldstatExporterVars.json_paths,  [FIELDSTAT_INPUT_JSON_PATH])


    def test_read_private_args_value(self):
        parser = argparse.ArgumentParser(description='Fieldstat exporter')
        shared_args_parser = self.exporter._FieldstatExporter__build_shared_args_parser()
        subparsers = parser.add_subparsers(dest='command')
        subparsers.required = True
        self.exporter._FieldstatExporter__build_prom_parser(subparsers, shared_args_parser)
        self.exporter._FieldstatExporter__build_local_parser(subparsers, shared_args_parser)

        args = parser.parse_args(["prometheus"])
        self.exporter._FieldstatExporter__read_private_args_value(args)

        self.assertEqual(FieldstatExporterVars.prom_uri_path, "/metrics")
        self.assertEqual(self.exporter.exporter_mode,    "prometheus")
        self.assertEqual(self.exporter.prom_listen_port, 8080)

        args = parser.parse_args(["prometheus", "-p", "40000", "-u", "/sapp"])
        self.exporter._FieldstatExporter__read_private_args_value(args)

        self.assertEqual(FieldstatExporterVars.prom_uri_path, "/sapp")
        self.assertEqual(self.exporter.exporter_mode,    "prometheus")
        self.assertEqual(self.exporter.prom_listen_port, 40000)

        args = parser.parse_args(["local"])
        self.exporter._FieldstatExporter__read_private_args_value(args)

        self.assertEqual(self.exporter.exporter_mode,      "local")
        self.assertEqual(self.exporter.local_interval_s,   1)
        self.assertEqual(self.exporter.local_enable_loop,  False)
        self.assertEqual(self.exporter.local_clear_screen, False)


        args = parser.parse_args(["local", "-i", "100", "-l", "--clear-screen"])
        self.exporter._FieldstatExporter__read_private_args_value(args)

        self.assertEqual(self.exporter.exporter_mode,      "local")
        self.assertEqual(self.exporter.local_interval_s,   100)
        self.assertEqual(self.exporter.local_enable_loop,  True)
        self.assertEqual(self.exporter.local_clear_screen, True)


    def test_verify_cmd_args(self):
        parser = argparse.ArgumentParser(description='Fieldstat exporter')
        shared_args_parser = self.exporter._FieldstatExporter__build_shared_args_parser()
        subparsers = parser.add_subparsers(dest='command')
        subparsers.required = True
        self.exporter._FieldstatExporter__build_prom_parser(subparsers, shared_args_parser)
        self.exporter._FieldstatExporter__build_local_parser(subparsers, shared_args_parser)

        args = parser.parse_args(["prometheus", "-f", "test"])
        ret  = self.exporter._FieldstatExporter__verify_cmd_args(args)
        self.assertEqual(ret, -1)

        args = parser.parse_args(["prometheus", "-b", "001,002,003,004", "-f", "summary"])
        ret  = self.exporter._FieldstatExporter__verify_cmd_args(args)
        self.assertEqual(ret, -1)

        args = parser.parse_args(["prometheus", "-b", "0.1,0.2,0.3,0.4", "-f", "histogram"])
        ret  = self.exporter._FieldstatExporter__verify_cmd_args(args)
        self.assertEqual(ret, -1)

        args = parser.parse_args(["prometheus", "-p", "800"])
        ret  = self.exporter._FieldstatExporter__verify_cmd_args(args)
        self.assertEqual(ret, -1)

        args = parser.parse_args(["prometheus", "-p", "80000"])
        ret  = self.exporter._FieldstatExporter__verify_cmd_args(args)
        self.assertEqual(ret, -1)

        args = parser.parse_args(["local", "-m", "test"])
        ret  = self.exporter._FieldstatExporter__verify_cmd_args(args)
        self.assertEqual(ret, -1)


    @patch('sys.argv', ['fieldstat.py', 'prometheus'])
    def test_read_cmd_options_promtheus(self):
        self.exporter.read_cmd_options()

        self.assertEqual(FieldstatExporterVars.hist_format,   "summary")
        self.assertEqual(FieldstatExporterVars.hist_bins,     [0.1, 0.5, 0.8, 0.9, 0.95, 0.99])
        self.assertEqual(FieldstatExporterVars.json_paths,    [])

        self.assertEqual(FieldstatExporterVars.prom_uri_path, "/metrics")
        self.assertEqual(self.exporter.prom_listen_port,      8080)
        self.assertEqual(self.exporter.exporter_mode,         "prometheus")


    @patch('sys.argv', ['fieldstat.py', 'local'])
    def test_read_cmd_options_local(self):
        self.exporter.read_cmd_options()
        self.assertEqual(FieldstatExporterVars.hist_format,           "summary")
        self.assertEqual(FieldstatExporterVars.hist_bins,             [0.1, 0.5, 0.8, 0.9, 0.95, 0.99])
        self.assertEqual(FieldstatExporterVars.json_paths,            [])
        self.assertEqual(FieldstatExporterVars.local_display_hist,    False)
        self.assertEqual(FieldstatExporterVars.local_display_hll,     False)
        self.assertEqual(FieldstatExporterVars.local_display_counter, False)
        self.assertEqual(FieldstatExporterVars.local_match_tags,      {})

        self.assertEqual(self.exporter.exporter_mode,       "local")
        self.assertEqual(self.exporter.local_interval_s,    1)
        self.assertEqual(self.exporter.local_enable_loop,   False)
        self.assertEqual(self.exporter.local_clear_screen,  False)


if __name__ == '__main__':
    unittest.main()