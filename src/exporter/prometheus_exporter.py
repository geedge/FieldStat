#!/usr/bin/python3

import argparse
import sys
import json
import re
import os
from http.server import HTTPServer, BaseHTTPRequestHandler

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

libfieldstat.histogram_value_sum.argtypes           = [ctypes.c_void_p]
libfieldstat.histogram_value_sum.restype            =  ctypes.c_longlong

libfieldstat.hll_base64_to_count.argtypes           = [ctypes.c_char_p]
libfieldstat.hll_base64_to_count.restype            =  ctypes.c_double

import logging
logging.basicConfig(level=logging.DEBUG, format='%(asctime)s - %(message)s')

class PrometheusExporterVars:
    hist_bins   = []
    hist_format = ""
    json_path   = ""
    uri_path    = ""

class BuildPrometheusMetrics:
    def __init__(self):
        self.hist_bins   = PrometheusExporterVars.hist_bins
        self.hist_format = PrometheusExporterVars.hist_format
        self.json_path   = PrometheusExporterVars.json_path

    def __escape_metric_name(self, metric_name):
        #regex: [a-zA-Z_:][a-zA-Z0-9_:]*
        escaped_name = re.sub(r'[^a-zA-Z0-9_:]', '_', metric_name)
        return escaped_name

    def __escape_metric_tags(self, json_dict):
        #regex: [a-zA-Z_][a-zA-Z0-9_]*
        escaped_tags = []
        tags_dict    = json_dict["tags"]
        tags_dict["app_name"] = json_dict["name"]

        for key,value in tags_dict.items():
            escaped_key = re.sub(r'[^a-zA-Z0-9_:]', '_', key)
            value_str = str(value)
            escaped_tags.append(f'{escaped_key}="{value_str}"')

        return ','.join(escaped_tags)

    def __build_type_counter(self, name, tags, value):
        return self.__escape_metric_name(name) + "{" + tags + "}" + ' ' + str(value) + '\n'


    def __build_histogram_format(self, name, tags, c_hist):
        metrics = ""
        for i in self.hist_bins:
            value = libfieldstat.histogram_count_le_value(c_hist, int(i))
            metric = name + "_bucket" + "{" + tags + ",le=\"{:.2f}\"".format(i) + "}" + ' ' + str(value) + '\n'
            metrics += metric

        return metrics

    def __build_summary_format(self, name, tags, c_hist):
        metrics = ""
        for i in self.hist_bins:
            value = libfieldstat.histogram_value_at_percentile(c_hist, float(i * 100))
            metric = name + "{" + tags +  ",quantile=\"{:.2f}%\"".format(i * 100) + "}" + ' ' + str(value) + '\n'
            metrics += metric

        return metrics

    def __build_type_histogram(self, name, tags, value):
        escaped_name = self.__escape_metric_name(name)
        metrics = ""
        
        c_hist = libfieldstat.histogram_base64_decode(value.encode('utf-8'))
        
        if self.hist_format == "summary":
            metrics += self.__build_summary_format(escaped_name, tags, c_hist)
        if self.hist_format == "histogram":
            metrics += self.__build_histogram_format(escaped_name, tags, c_hist)

        sum_value = libfieldstat.histogram_value_sum(c_hist)
        metrics  += escaped_name + "_sum"   + "{" + tags + "}" + ' ' + str(sum_value) + '\n'

        cnt_value = libfieldstat.histogram_value_total_count(c_hist)
        metrics  += escaped_name + "_count" + "{" + tags + "}" + ' ' + str(cnt_value) + '\n'

        libfieldstat.histogram_free(c_hist)

        return metrics
    
    
    def __build_type_hll(self, name, tags, value):
        hll_value = libfieldstat.hll_base64_to_count(value.encode('utf-8'))
        return  name + "{" + tags + "}" + ' ' + "{:.2f}".format(hll_value) + '\n'

    def __build_metrics(self, json_dict):
        metrics      = ""
        metric_tags  = self.__escape_metric_tags(json_dict)
        for key,value in json_dict["fields"].items():
            if isinstance(value, int):
                metrics += self.__build_type_counter(key, metric_tags, value)
            else:
                is_hll = libfieldstat.is_hll(value.encode('utf-8'))
                if is_hll:
                    metrics += self.__build_type_hll(key, metric_tags, value)
                else:
                    metrics += self.__build_type_histogram(key, metric_tags, value)
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

    @classmethod
    def run_build_metrics(cls):
        builder = cls()
        return builder.build_metrics_payload()


class PrometheusEndpoint(BaseHTTPRequestHandler):
    def __init__(self, request, client_address, server):
        self.desired_path = PrometheusExporterVars.uri_path
        super().__init__(request, client_address, server)

    def do_GET(self):
        if self.path == self.desired_path:
            resp = BuildPrometheusMetrics.run_build_metrics()
            self.send_response(200)
            self.send_header('Content-type', 'text/plain; version=0.0.4')
            self.end_headers()
            self.wfile.write(resp.encode())
        else:
            self.send_error(404)
            self.end_headers()

class PrometheusExporter:
    DEFAULT_LISTEN_PORT = 8080
    DEFAULT_HIST_BINS   = [0.1,0.5,0.8,0.9,0.95,0.99]
    DEFAULT_HIST_FORMAT = "summary"
    DEFAULT_JSON_PATH   = "./fieldstat.json"
    DEFAULT_URI_PATH    = "/metrics"
    
    def __init__(self):
        self.listen_port = self.DEFAULT_LISTEN_PORT
    
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
        
        n_bins = list(set(bins))
        n_bins.sort()
        return bins

    def read_cmd_options(self):
        bins_str = ','.join([str(x) for x in self.DEFAULT_HIST_BINS])

        parser = argparse.ArgumentParser(description='Fieldstat prometheus exporter.')

        parser.add_argument("-p", "--listen-port", type = int, default = self.DEFAULT_LISTEN_PORT,
                            help = "Specify the prometheus endpoint port to listen. i.e., 80,8080")
        parser.add_argument("-u", "--uri-path",    type = str, default = self.DEFAULT_URI_PATH,
                            help = "Specify the prometheus endpoint uri path.")
        parser.add_argument("-b", "--hist-bins",   type = str, default = bins_str,
                            help = "The metrics of histogram type output bins.")
        parser.add_argument("-f", "--hist-format", type = str, default = self.DEFAULT_HIST_FORMAT,
                            help = "The metrics of histogram type output format.")
        parser.add_argument("-j", "--json-path",   type = str, default = self.DEFAULT_JSON_PATH,
                            help = "The input fieldstat metrics json file path.")

        args = parser.parse_args()

        if -1 == self.__verify_cmd_args(args):
            parser.print_help()
            sys.exit(1)

        self.listen_port = args.listen_port
        PrometheusExporterVars.uri_path    = args.uri_path
        PrometheusExporterVars.hist_bins   = self.__parse_bins_str(args.hist_bins)
        PrometheusExporterVars.hist_format = args.hist_format
        PrometheusExporterVars.json_path   = args.json_path

    def enable_prometheus_endpoint(self):
        server_address = ('', self.listen_port)
        httpd = HTTPServer(server_address, PrometheusEndpoint)
        httpd.serve_forever()
        
    @classmethod
    def run_prometheus_endpoints(cls):
        exporter = cls()
        exporter.read_cmd_options()
        exporter.enable_prometheus_endpoint()

        
if __name__ == '__main__':
    PrometheusExporter.run_prometheus_endpoints()