import unittest
import sys
import urllib
from urllib.request import urlopen
from threading import Thread
from http.server import HTTPServer, BaseHTTPRequestHandler

sys.path.append('../src/exporter')
from prometheus_exporter import PrometheusExporterVars
from prometheus_exporter import BuildPrometheusMetrics
from prometheus_exporter import PrometheusEndpoint
from prometheus_exporter import PrometheusExporter

class TestBuildPrometheusMetrics(unittest.TestCase):
    def setUp(self):
        self.builder = BuildPrometheusMetrics()

    def test_escape_metric_name(self):
        metric_name = "tsg_master_log:(){}/\\%*$-,;"
        escaped = self.builder._BuildPrometheusMetrics__escape_metric_name(metric_name)
        self.assertEqual(escaped, "tsg_master_log:____________")


    def test_escape_metric_tags(self):
        #regex: [a-zA-Z_][a-zA-Z0-9_]*
        json_dict = {'name': '-', 'tags': {'send_log': 'sum', 'policy_id': 1, 'quanlity': 0.5},
                     'fields': {'T_success_log': 1}, 'timestamp': 1694657637836}
        tags = self.builder._BuildPrometheusMetrics__escape_metric_tags(json_dict)
        self.assertEqual(tags, "{send_log=\"sum\",policy_id=\"1\",quanlity=\"0.5\",app_name=\"-\"}")
        

    def test_build_one_metric(self):
        name  = "send_log"
        tags  = "{app_name=\"firewall\"}"
        value = 100
        
        one_metric = self.builder._BuildPrometheusMetrics__build_one_metric(name, tags, value)
        self.assertEqual(one_metric, "send_log{app_name=\"firewall\"} 100\n")


    def test_build_metrics_payload(self):
        payload = self.builder.build_metrics_payload()
        self.assertEqual(payload, "")

        self.builder.json_path = "/tmp/t.json"
        payload = self.builder.build_metrics_payload()
        self.assertNotEqual(payload, "")
        

class TestPrometheusEndpoint(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        PrometheusExporterVars.uri_path = "/metrics"
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
        print(response.getcode())
        self.assertEqual(response.getcode(), 200)

    def test_invalid_request(self):
        try:
            urlopen('http://localhost:40001/invalid')
        except urllib.error.HTTPError as e:
            self.assertEqual(e.code, 404)


class TestPrometheusExporter(unittest.TestCase):
    def setUp(self):
        self.exporter = PrometheusExporter()

    def test_verify_cmd_args_valid(self):
        result = self.exporter._PrometheusExporter__verify_cmd_args()
        self.assertEqual(result, 0)

    def test_verify_cmd_args_invalid(self):
        self.exporter.listen_port = 1000
        result = self.exporter._PrometheusExporter__verify_cmd_args()
        self.assertEqual(result, -1)

    def test_read_cmd_options(self):
        self.exporter.read_cmd_options()
        self.assertEqual(self.exporter.listen_port, 8080)
        self.assertEqual(PrometheusExporterVars.hist_format, "summary")
        self.assertEqual(PrometheusExporterVars.hist_bins, "0.1,0.5,0.8,0.9,0.95,0.99")
        self.assertEqual(PrometheusExporterVars.json_path, "./fieldstat.json")
        self.assertEqual(PrometheusExporterVars.uri_path,  "/metrics")


if __name__ == '__main__':
    unittest.main()