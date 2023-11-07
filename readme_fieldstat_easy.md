
# Fieldstat exporter
Fieldstat exporter provides exporter functions for fieldstat4, including prometheus exporter and local exporter. 

## prometheus exporter
The prometheus exporter reads the fieldstat4 json file and enables the prometheus endpoint. The default port is 8080.

## local exporter
The local exporter reads the fieldstat4 json file and outputs it to the screen.



### List of rpm files
    ...
    |-- bin
        |-- fieldstat_exporter
    |-- lib
        |-- ibfieldstat4.a
        |-- libfieldstat4.so
### Service configuration
The following is the prometheus exporter systemd service example file.
```systemd
[Unit]
Description=fieldstat4 prometheus service.
After=target.service      #Replace target service with the actual service used.
Requires=target.service   #Replace target service with the actual service used.

[Service]
Type=simple
ExecStart=/opt/tsg/framework/bin/fieldstat_exporter prometheus
RestartSec=10s
Restart=always
PrivateTmp=True

[Install]
WantedBy=multi-user.target
```
## Command Line
Fieldstat exporter includes prometheus including prometheus and local subcommands.
```txt
[root@localhost bin]# ./fieldstat_exporter --help
usage: fieldstat_exporter [-h] {prometheus,local} ...

Fieldstat exporter

positional arguments:
  {prometheus,local}
    prometheus        Set prometheus exporter
    local             Set local exporter

optional arguments:
  -h, --help          show this help message and exit
```
The following is prometheus exporter command line example.
```bash
./fieldstat_exporter prometheus -b 0.1,0.2,0.3,0.4,0.5 -f summary -j /tmp/fieldstat.json -p 8080 -u /metrics
```
The following is fieldstat prometheus exporter help info.
```txt
[root@localhost bin]# ./fieldstat_exporter prometheus --help
usage: fieldstat_exporter prometheus [-h] [-b HIST_BINS] [-f HIST_FORMAT]
                                     [-j JSON_PATH] [-p LISTEN_PORT]
                                     [-u URI_PATH]

optional arguments:
  -h, --help            show this help message and exit
  -b HIST_BINS, --hist-bins HIST_BINS
                        The metrics of histogram type output bins.
  -f HIST_FORMAT, --hist-format HIST_FORMAT
                        The metrics of histogram type output format.
  -j JSON_PATH, --json-path JSON_PATH
                        The input fieldstat metrics json file path.
  -p LISTEN_PORT, --listen-port LISTEN_PORT
                        Specify the prometheus endpoint port to listen. i.e.,
                        80,8080
  -u URI_PATH, --uri-path URI_PATH
                        Specify the prometheus endpoint uri path
```
The prometheus exporter optional arguments default values.
args|default value
--- | ---
-b, --hist-bins|[0.1,0.5,0.8,0.9,0.95,0.99]
-f, --hist-format|summary
-j, --json-path|./fieldstat.json
-p, --listen-port|8080
-u, --uri-path|/metrics
The following is local exporter command line example.
```bash
./fieldstat_exporter local -b 0.1,0.2,0.3,0.4,0.5 -f summary -j /tmp/fieldstat.json -i 1 -l --clear-screen -m policy_id:1,device_name:xxg
```
The following is fieldstat local exporter help info.
```txt
[root@localhost bin]# ./fieldstat_exporter local --help
usage: fieldstat_exporter local [-h] [-b HIST_BINS] [-f HIST_FORMAT]
                                [-j JSON_PATH] [-i INTERVAL] [-l]
                                [--clear-screen] [--display-hll]
                                [--display-hist] [--display-counter]
                                [-m MATCH_TAGS]

optional arguments:
  -h, --help            show this help message and exit
  -b HIST_BINS, --hist-bins HIST_BINS
                        The metrics of histogram type output bins.
  -f HIST_FORMAT, --hist-format HIST_FORMAT
                        The metrics of histogram type output format.
  -j JSON_PATH, --json-path JSON_PATH
                        The input fieldstat metrics json file path.
  -i INTERVAL, --interval INTERVAL
                        interval, seconds to wait between print.
  -l, --loop            print loop, exit when recv a signal.
  --clear-screen        clear screen at start of loop.
  --display-hll         Display hyperloglog type metrics.
  --display-hist        Display histogram type metrics.
  --display-counter     Display counter type metrics.
  --disable-table       disable display table format.
  -m MATCH_TAGS, --match-tags MATCH_TAGS
                        Display the tags match metrics
```
The local exporter optional arguments default values.
args|default value
--- | ---
-b, --hist-bins|[0.1,0.5,0.8,0.9,0.95,0.99]
-f, --hist-format|summary
-j, --json-path|./fieldstat.json
-i, --interval|1(s)
-l, --loop|False
--clear-screen|False
--display-hll|False
--display-hist|False
--display-counter|False
-m, --match-tags|""

# fieldstat easy

## Description
**Fieldstat easy** wraps the fieldstat to provide a simpler interface and multi-threaded support. For details refer to [fieldstat](readme_fieldstat.md). For the usage of prometheus exporter and file exporter, refer to [fieldstat exporter](#Fieldstat-exporter).

Fieldstat easy support multi-threaded writing and output. The output of fieldstat easy is in json format, which can be easily parsed by other python.

All the metrics in fieldstat easy are dynamic, which means that the metrics has no volume limit, and users can add any tags to the metrics as needed and at any time. Tags, which denote the attributes of values, can be viewed as the keys to the item in table.

## Example 
### coding
```
#include "fieldstat_easy.h"

const int N_THREADS = 3;
struct fieldstat_tag global_tags[1];
struct fieldstat_tag tmptag;
tmptag.key = "app id";
tmptag.type = TAG_INTEGER;
tmptag.value_longlong = 1;
global_tags[0] = tmptag;

struct fieldstat_tag tag1 = {"direction", TAG_STRING, "incoming"};
struct fieldstat_tag tag2 = {"direction", TAG_STRING, "outgoing"};

struct fieldstat_easy *fse = fieldstat_easy_new(N_THREADS, global_tags, 1);
int counter_id = fieldstat_easy_register_counter(fse, "incoming bytes");
int hdr_id = fieldstat_easy_register_histogram(fse, 
                                               "delay", 
                                               1,      // min trackable value, no less than 1. The value less than this value will still be recorded, with an unpromising precision.
                                               10000, // max trackable value, the value larger than this value will be recorded as the max value.
                                               1); // precision, value less than 10 ^ precision will be recorded keeping exact precision. The larger recorded value is, the less precise of its count is.
int output_interval_seconds = 1;
fieldstat_easy_enable_auto_output(fse, FILENAME, output_interval_seconds); // FILENAME is the same as the one you set in python.

std::thread threads[N_THREADS];
for (int thread_id = 0; thread_id < N_THREADS; thread_id++) {
    threads[thread_id] = std::thread([fse, counter_id, hdr_id, thread_id]() {
        while(1) {
            fieldstat_easy_counter_incrby(fse, thread_id, counter_id, &tag1, 1, 100);
            fieldstat_easy_counter_incrby(fse, thread_id, counter_id, &tag2, 1, 2);
            fieldstat_easy_histogram_record(fse, thread_id, hdr_id, &tag1, 1, rand() % 10000);
        }
    });
}
sleep(1000);
fieldstat_easy_free(fse);

```

### raw output
```
[
    {
        "name": "-",
        "tags": {
            "direction": "incoming",
            "app id": 1
        },
        "fields": {
            "bytes": 585167,
            "delay": "HISTEwAAAUYAAAAAAAAAAQAAAAAAAAABAAAAAAAAJxA/8AAAAAAAAJABbHh8ZnZccHp2gAFUaIABdmaCAXx0YoQBdGyMAXhsggF6eowBggGCAfAB6AGWAvQB9AHyAdYB4AHoAeoB3gHEAcwBrgKKAtgB5gPQA7QD5APIA8wD4gO+A+YDxAO2A8ADwgPAA8QD5gOEB5wHhAfQB9gHvAfwB6QHuAe6B/QGxAecB7QHqAfeB/INsA+ODtIO2g2QDo4OrA7WDtYN/A7qDcQN9A6WDpYP3h7OHqwdnhyeHYge7B2EHaQesB7QHaId+B2MHaYejh3gOvI6jDuyOqI7qjqMOpo7mDqeOvI6iDuiOrQ7lDqKOpZztHDuc+x2onbkdoR0hnSAdZR03naWdK5z6nX8dZh1vugBvOcBqusB2OsBkOgB+OkB3OoBxOoB9ukB9uwBqOwBnuoB8OkBxusBsugB7OoBptMD5tEDpNUDivcBAAAAAAAAAA==",
        },
        "fields_delta": {
            "bytes": 31689,
            "delay": "HISTEwAAAOgAAAAAAAAAAQAAAAAAAAABAAAAAAAAJxA/8AAAAAAAAAQEAQYKCgIEAggECgYECgQGBAYIDgoMAgQBCgQECAICEAIUDBIUEg4ECAwKDBQGFCAWFBYYFBIcIhogGhYiEhg6JDQ4NDw+GjouOC4oMC4ufGZidGhaYGBMXGJYYk5eXugBrAHYAdQB7AHeAcoB3AHAAfIBwAHYAdgBwgG+Ae4BzgOqA6QDsAOUA64DrAOMA4gD5gL4ArAD4AKYA44DtAOCBvgFkgaiBrIG1AasBswG0ga2BqAGpAaKBrYGpAamBvAM3AzWDI4NgA2mDKIMkg2uDIANiA3gDMQMlA2ODfIM2hikGf4XjA0AAAAAAAAA"
        }
        "timestamp_ms": 5507527913,
        "timestamp_ms_delta": 1002
    },
    {
        "name": "-",
        "tags": {
            "direction": "outgoing",
            "app id": 1
        },
        "fields": {
            "bytes": 5857,
            "delay": "HISTEwAAAUYAAAAAAAAAAQAAAAAAAAABAAAAAAAAJxA/8AAAAAAAAJABbHh8ZnZccHp2gAFUaIABdmaCAXx0YoQBdGyMAXhsggF6eowBggGCAfAB6AGWAvQB9AHyAdYB4AHoAeoB3gHEAcwBrgKKAtgB5gPQA7QD5APIA8wD4gO+A+YDxAO2A8ADwgPAA8QD5gOEB5wHhAfQB9gHvAfwB6QHuAe6B/QGxAecB7QHqAfeB/INsA+ODtIO2g2QDo4OrA7WDtYN/A7qDcQN9A6WDpYP3h7OHqwdnhyeHYge7B2EHaQesB7QHaId+B2MHaYejh3gOvI6jDuyOqI7qjqMOpo7mDqeOvI6iDuiOrQ7lDqKOpZztHDuc+x2onbkdoR0hnSAdZR03naWdK5z6nX8dZh1vugBvOcBqusB2OsBkOgB+OkB3OoBxOoB9ukB9uwBqOwBnuoB8OkBxusBsugB7OoBptMD5tEDpNUDivcBAAAAAAAAAA==",
        },
        "fields_delta: {
        }
        "timestamp_ms": 5507527913,
        "timestamp_ms_delta": 1002
    }****
]
```

Another way to output is to use the `fieldstat_easy_output` function, the output of which is a c-string in json format. The output format is *different* from the above. `fieldstat_easy_output` is stateless and constant, and will only output accumulated data.

Explaination of the output format:
 - `name` Always `-`. Just omit it.
 - `tags` The tags of the fields. It contains the global tags and the tags of the metrics. The tags of the metrics are the same as the arguments of `fieldstat_easy_counter_incrby` and `fieldstat_easy_histogram_record`.
 - `fields` The fields of the metrics. The fields of the counter (`bytes` in example) are of integer type, and the fields of the histogram (`delay` in example) are of string type(in base64 format). Those with key ending with `_delta` are metrics recording the values of the current output interval, while the value of the fields is the value of all time. For example, when the metric is to record packet sum, time interval set to 1, the field "sum_delta" records speed.

The output histogram is a base64 encoded string. For the formatted output, use python command lines.

Or you can decode it with the following code snippet:

```
// C language
#include "hdr_histogram.h"
#include "metrics/histogram_encoder.h"

char *buf = "HISTEwAAAUYAAAAAAAAAAQAAAAAAAAABAAAAAAAAJxA/8AAAAAAAAJABbHh8ZnZccHp2gAFUaIABdmaCAXx0YoQBdGyMAXhsggF6eowBggGCAfAB6AGWAvQB9AHyAdYB4AHoAeoB3gHEAcwBrgKKAtgB5gPQA7QD5APIA8wD4gO+A+YDxAO2A8ADwgPAA8QD5gOEB5wHhAfQB9gHvAfwB6QHuAe6B/QGxAecB7QHqAfeB/INsA+ODtIO2g2QDo4OrA7WDtYN/A7qDcQN9A6WDpYP3h7OHqwdnhyeHYge7B2EHaQesB7QHaId+B2MHaYejh3gOvI6jDuyOqI7qjqMOpo7mDqeOvI6iDuiOrQ7lDqKOpZztHDuc+x2onbkdoR0hnSAdZR03naWdK5z6nX8dZh1vugBvOcBqusB2OsBkOgB+OkB3OoBxOoB9ukB9uwBqOwBnuoB8OkBxusBsugB7OoBptMD5tEDpNUDivcBAAAAAAAAAA=="

struct hdr_histogram *hdr = histogram_decode_from_b64(buf, strlen(buf));
(void)hdr_percentiles_print(hdr, stdout, 5, 1.0, CLASSIC);
hdr_close(hdr);
```

```
# python language
from fieldstat_exporter import FieldstatAPI

c_hist  = FieldstatAPI.libfieldstat.fieldstat_histogram_base64_decode(buf.encode('utf-8'))
sum_value = FieldstatAPI.libfieldstat.fieldstat_histogram_value_sum(c_hist)
FieldstatAPI.libfieldstat.fieldstat_histogram_free(c_hist) # need to free memory, because it's a C binding
```
