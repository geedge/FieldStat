# Fieldstat 4.0 

## design 
Field Stat is a library for outputting and statistics of running states. Compared to the previous version, fieldstat3.0, this version provides support for new functionalities. The following are the concepts used in this program:

- Instance: Corresponds to the `struct fieldstat` and represents the instance handle on a thread.
- Cube: An instance is composed of multiple cubes. A cube is a collection of various statistical measures.
- Cell: A cube is composed of multiple cells. A cell is a data set tagged with specific tags.
- Metric: Each metric corresponds to a column of data in the database. Currently, there are three types of metric statistics: counter, Hyper Log Log, and Histogram.

Compared to version 3.0, version 4.0 introduces the concepts of cube. The concept of field has been removed (a combination of tag information and metric)。In version 4.0, tags and metrics are managed independently and dynamically. This change simplifies the interface usage and improves the speed of batch statistics for the same tag. Cube is a collection of metrics for the same purpose, and is also the unit of metric management. 

Version 4.0 no longer supports multithreading. In version 3.0, the metric of type "counter" could be shared and operated on by multiple threads. However, in version 4.0, each instance is written by only one thread. The fieldstat version 4.0 provides support for distributed statistics through methods like serialize and merge. It is the responsibility of the caller to aggregate statistics from different instances. For simplified multi-threaded support, refer to [fieldstat easy](#Fieldstat-easy).

### sampling mode

Although the addition of cells is dynamic, there is a maximum limit on the number of cells per cube. Currently, there are two modes of limitation known as sampling modes:
SAMPLING_MODE_COMPREHENSIVE
SAMPLING_MODE_TOPK
In the Comprehensive mode, once the maximum number of cells is reached in a cube, no new cells can be added under the current cube. Any subsequent new cells will be discarded.
In the top-K mode, the Heavy Keeper algorithm is used to retain the complete sorting information of cells based on a primary metric. Even after reaching the maximum number of cells, new cells will still be accepted. A new cell with a higher ranking will be added while removing a cell with a lower ranking.

### metrics
#### Counter
A simple counting unit that supports Increment and Set operations. When registering a counter metric, you need to specify whether the counter type is Gauge or a general Counter. Gauge is used for statistical analysis of state variables, typically associated with Set operations. Counter is used for accumulating scalar values, typically associated with Increment operations. The explicit distinction between these two types is because they are handled differently during the merge process.

#### Histogram
Outputs the distribution of numerical values for events, such as latency or file length. Depending on the nature of the measured quantity, it can output the probability distribution of values in terms of quantity or the proportion of values in the entire dataset.

#### Hyper Log Log
Uses statistical techniques to estimate the number of elements in a set that contains a large number of values. For example, it can be used to estimate the number of unique client IP addresses in a set of network flows.

## Usage
### installation 
Download fieldstat4 rpm from https://repo.geedge.net/pulp/content/ and install rpm package.

### Statistic
```
#include "fieldstat.h"

struct fieldstat *instance = fieldstat_new();
int cube_id = fieldstat_create_cube(instance, YOUR_SHARED_TAG, YOUR_SHARED_TAG_LENGTH, SAMPLING_MODE_TOPK, MAX_CELL_NUMBER);
int metric_counter_id = fieldstat_register_counter(instance, cube_id, "any metric name", 0/1);
int metric_histogram_id = fieldstat_register_histogram(instance, cube_id, "any metric name", THE_MINIMUM_NUMBER_TO_RECORD, THE_MAXIMUM_NUMBER_TO_RECORD, PRECISION);
int metric_hll_id = fieldstat_register_hll(instance, cube_id, "any metric name", PRECISION);
int cell_id = fieldstat_cube_add(instance, cube_id, YOUR_TAG, YOUR_TAG_LENGTH, THE_PRIMARY_METRIC);
if (cell_id != -1) {
    fieldstat_counter_incrby(instance, cube_id, metric_counter_id, cell_id, VALUE);
    fieldstat_histogram_record(instance, cube_id, metric_counter_id, cell_id, VALUE);
    fieldstat_hll_add(instance, cube_id, metric_counter_id, cell_id, VALUE, VALUE_STR_LENGTH);
}

fieldstat_free(instance);
```

### Merge
```
fieldstat_merge(instance_dest, instance_deserialized);
free(blob);
fieldstat_free(instance_deserialized);
```

### Export
```

struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new(instance);
// optional
fieldstat_json_exporter_set_global_tag(fieldstat_json_exporter, YOUR_GLOBAL_TAG, YOUR_GLOBAL_TAG_LENGTH);
// optional
fieldstat_json_exporter_set_name(fieldstat_json_exporter, "any name for this exporter");

char *json_string = fieldstat_json_exporter_export(fieldstat_json_exporter);
printf("test, fieldstat_json_exporter_export json_string: %s\n", json_string);
free(json_string);
fieldstat_json_exporter_free(fieldstat_json_exporter);
```

## benchmark
### memory 
The total memory consumption, approximately composes two parts: the memory used for storing data, and the memory used when processing merge as well as export.

The memory is calculated on 64-bit system, with unit of Byte if not specified.

#### Total usage on storing data
The memory are consumed by:
- the array and hash table to store tags of every cells
- Metrics
- Heavy Keeper instance if top-K sampling mode

Note that every cell will have its only metric, so the memory cost by metrics should be multiplied by cell number.

#### metrics
- **Counter** 
  Counter is a very simple data structure, every counter uses 24 Bytes.
- **Hyper Log Log**
  The size of HLL is dependent on its precision. The size is: 
  $$ 4 \cdot  2 ^ {precision}/ 6$$
- **Histogram** 
  The size of histogram size can be approximated by
  $$ log_2(\frac{MAX}{MIN}) * 2^{precision} $$

#### cells
Every time successfully add a cell will increase memory usage by 
$$ 2 sizeof(tag) + 112$$


#### Heavy Keeper
As shown in table. For K above 1000, the size of sketch will hardly increase, the marginal memory cost will be storing cells. Table entry Memory is the memory cost of a initialed cube, while Memory_max is that after cube is full.

| K    | w    | d   | Memory(kB) | Memory_max(kB) |
| ---- | ---- | --- | ---------- | -------------- |
| 10   | 150  | 3   | 3.768      | 5.028          |
| 100  | 400  | 3   | 9.768      | 22.368         |
| 500  | 1125 | 3   | 27.168     | 90.168         |
| 1000 | 1951 | 3   | 46.992     | 172.992        |

#### Memory used when merge
The instance used for merge will allocate a hash table to store all cube tags and a hash table to store all metric names, which costs extra memory. 

$$ (numM + numC) * (88 + \bar{namelen})$$

#### Memory used when export
It is hard to calculate memory usage of CJSON. By experiment, RSS to different cell numbers are shown below.

| cell number | RSS(kB) |
| ----------- | ------- |
| 10000       | 2648    |
| 20000       | 5136    |
| 40000       | 9540    |

## performance
### environment
- **OS**：x86_64 Linux 4.18.0-425.3.1.el8
- **CPU**: Intel(R) Xeon(R) Platinum 8269CY CPU @ 2.50GHz
- **Total Memory**：29GB
- **GCC Version**：8.5.0 20210514 (Red Hat 8.5.0-15) (GCC)
- **Compiling Option**：-O3

### result

| operation                                     | time(us)  | corresponding api                                                                |
| --------------------------------------------- | --------- | -------------------------------------------------------------------------------- |
| add a cell(topk)                              | 0.836     | fieldstat_counter_incyby(new sampling)                                           |
| add a cell(comprehensive)                     | 1.191     | fieldstat_counter_incyby(new sampling)                                           |
| histogram record                              | 0.2946    | fieldstat_histogram_record                                                       |
| HLL add                                       | 0.3306    | fieldstat_hll_add                                                                |
| copy 1 cell of a 1 counter metric             | 0.7814    | fieldstat_merge(empty destination instance)                                      |
| copy 1 cell of a HLL metric                   | 1.96      | fieldstat_merge(empty destination instance)                                      |
| copy 1 cell of a histogram metric             | 0.844     | fieldstat_merge(empty destination instance)                                      |
| copy 1 cell of a counter metric on topk cube  | 1.77      | fieldstat_merge(empty destination instance)                                      |
| merge 1 cell of a counter metric              | 0.448     | fieldstat_merge(destination also has registered cube, metric and cells)          |
| merge 1 cell of a HLL metric                  | 1.70      | fieldstat_merge(destination also has registered cube, metric and cells)          |
| merge 1 cell of a histogram metric            | 0.659     | fieldstat_merge(destination instance also has registered cube, metric and cells) |
| merge 1 cell of a counter metric on topk cube | 0.659     | fieldstat_merge(destination instance also has registered cube, metric and cells) |
| export a cell (with 10 counter)               | 0.118     | fieldstat_json_exporter_export                                                   |

### Explanation
#### add a cell(topk)
Generate 100000 random tags, call fieldstat_counter_incyby for each tag. Calculate the mean time spent for one tag. The max cell number of cube(K) is 1000.
#### add a cell(comprehensive)
Generate 100000 random tags, call fieldstat_counter_incyby for each tag. Calculate the mean time spent for one tag. Every adding operation is success (The max cell number of cube is 100000).

The time is larger than topk mode, because every tags are added to hash table, while topk only select some.
#### histogram record 
For the same cube, the same metric, and the same cell, call fieldstat_histogram_record 100000 times. Calculate the mean time spent for record once.
#### HLL add
For the same cube, the same metric, and the same cell, call fieldstat_hll_add 100000 times. Calculate the mean time spent for record once.
#### copy 1 cell of a counter metric
There are 1000 cells, 1 cube, 1 metric on a instance to be merged. The metric type is counter. Record the time of merging once. Calculate the mean time.

####  copy 1 cell of a HLL metric
There are 1000 cells, 1 cube, 1 metric on a instance to be merged. The metric type is HLL, with precision set to 6. Record the time of merging once. Calculate the mean time. 

####  copy 1 cell of a histogram metric
There are 1000 cells, 1 cube, 1 metric on a instance to be merged. The metric type is Histogram, with min set to 1, max set to 100000, precision set to 1. Record the time of merging once. Calculate the mean time. It should be noticed that merging histogram is time-consuming.

#### copy 1 cell of a counter metric on topk cube
Register a top-K sampling mode cube, following by the same operation of "copy 1 cell of a counter metric".

#### merge 1 cell of a 1 counter metric
First do the same registering operation the same as copy 1 cell of a counter metric. After merging without timer start once to create a duplication of source instance, merge twice with the timer start to measure performance. The second merge won't add new cells, so it will be faster.

####  merge 1 cell of a HLL metric
The same as "copy 1 cell of a HLL metric" but merge twice.

####  merge 1 cell of a histogram metric
The same as "copy 1 cell of a histogram metric" but merge twice.

#### merge 1 cell of a counter metric on topk cube
The same as "copy 1 cell of a counter metric on topk cube" but merge twice. 
Merging Heavy Keeper is slower the coping it, it is why the execution time is unusually higher than copying it.

#### export a cell
I add a instance with 100 cubes, on each of which I register 10 metrics. The max cell number of each cube is 1000, and call the metric operation on every added cell. So there are 100 * 10 * 1000 cells in total. After exporting the instance, calculate the average time spent on each cells.


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
**Fieldstat easy** wraps the fieldstat to provide a simpler interface and multi-threaded support. For details refer to [fieldstat](#Fieldstat-4.0). For the usage of prometheus exporter and file exporter, refer to [fieldstat exporter](#Fieldstat-exporter).

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
            "bytes_delta": 31689,
            "delay_delta": "HISTEwAAAOgAAAAAAAAAAQAAAAAAAAABAAAAAAAAJxA/8AAAAAAAAAQEAQYKCgIEAggECgYECgQGBAYIDgoMAgQBCgQECAICEAIUDBIUEg4ECAwKDBQGFCAWFBYYFBIcIhogGhYiEhg6JDQ4NDw+GjouOC4oMC4ufGZidGhaYGBMXGJYYk5eXugBrAHYAdQB7AHeAcoB3AHAAfIBwAHYAdgBwgG+Ae4BzgOqA6QDsAOUA64DrAOMA4gD5gL4ArAD4AKYA44DtAOCBvgFkgaiBrIG1AasBswG0ga2BqAGpAaKBrYGpAamBvAM3AzWDI4NgA2mDKIMkg2uDIANiA3gDMQMlA2ODfIM2hikGf4XjA0AAAAAAAAA"
        },
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
            "bytes_delta": 31,
        },
        "timestamp_ms": 5507527913,
        "timestamp_ms_delta": 1002
    }****
]
```

Another way to output is to use the `fieldstat_easy_output` function, the output of which is a c-string in json format. The output format is *different* from the above. `fieldstat_easy_output` is stateless, and will only output accumulated data.

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
