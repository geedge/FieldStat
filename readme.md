# Fieldstat3
Fieldstat3 is a running state output and statistics library. Fieldtat3 inherits the functions of Fieldstat2.
## Metric ype
The metric type of Fieldstat is similar to promethues. As follows.
- Counter
- Gauge
- Histogram
- Summary

## Exporter Type
Fieldstat3 has three exporter types. As follows.
| Exporter Type  | Decription |
| --- | --- |
| Local | Export metric to a specified local file in a custom format. |
| Prometheus | Export metric by prometheus endpoint in the prometheus Format. |
| Line Protocol | Export metric to Line Protocol Server(i.e., telegraf) in Line Portocol Format. |

### Local
#### Counter Metric Type
The Local exporter exports the changing(speed/s) and current(sum) value of the Counter metrics. As follows.
```txt
        field_00  field_01
sum         0	    184300
speed/s     0	     92150
```
#### Gauge Metric Type
The local exporter exports the current value of the Gauge metrics. as follows.
```txt
status_00:  100  status_01: 10
```
#### Summary Metric Type
The local exporter exports the Summary metrics in a table format. As follows.
```
summary       50%   80%   95%   MAX   MIN    AVG   STDDEV   CNT
ssl_up(ms)      0     0     0     0     0    0.00    0.00     0
peek_sni(ms)    0     0     0     0     0    0.00    0.00     0
ssl_down(ms)    0     0     0     0     0    0.00    0.00     0
ask_kyr(ms)     0     0     0     0     0    0.00    0.00     0
```
#### Histogram Metric Type
The local exporter exports the Histogram metrics in a table format. As follows.
```
histogram       le=50   le=80   le=95   MAX   MIN     AVG   STDDEV   CNT
ssl_up(ms)          0       0       0     0     0    0.00     0.00     0
peek_sni(ms)        0       0       0     0     0    0.00     0.00     0
ssl_down(ms)        0       0       0     0     0    0.00     0.00     0
ask_kyr(ms)         0       0       0     0     0    0.00     0.00     0
```
### Prometheus
#### Counter Metric Type
The prometheus exporter exports the current value of the Counter metrics. As follows.
```txt
counter_00{app_name="counter_app_name"} 1\n
```
#### Gauge Metric Type
The prometheus exporter exports the current value of the Gauge metrics. as follows.
```txt
gauge_00{app_name="gauge_app_name"} 1\n
```
#### Summary Metric Type
The prometheus exporter exports the Summary metrics. As follows.
```
prometheus_00{app_name="prometheus_app_name",quantile="0.50"} 50\n
prometheus_00{app_name="prometheus_app_name",quantile="0.10"} 10\n
prometheus_00{app_name="prometheus_app_name",quantile="0.80"} 80\n
prometheus_00{app_name="prometheus_app_name",quantile="0.90"} 90\n
prometheus_00{app_name="prometheus_app_name",quantile="0.95"} 95\n
prometheus_00{app_name="prometheus_app_name",quantile="0.99"} 99\n
prometheus_00_count{app_name="prometheus_app_name"} 100\n"
prometheus_00_sum{app_name="prometheus_app_name"} 5050\n"
```
#### Histogram Metric Type
The local exporter exports the Histogram metrics in a table format. As follows.
```
histogram_00_bucket{app_name="histogram_00_app_name",le="10.00"} 10\n
histogram_00_bucket{app_name="histogram_00_app_name",le="50.00"} 50\n
histogram_00_bucket{app_name="histogram_00_app_name",le="90.00"} 90\n
histogram_00_bucket{app_name="histogram_00_app_name",le="95.00"} 95\n
histogram_00_bucket{app_name="histogram_00_app_name",le="99.00"} 99\n
histogram_00_bucket{app_name="histogram_00_app_name",le="80.00"} 80\n
histogram_00_count{app_name="histogram_00_app_name"} 100\n
histogram_00_sum{app_name="histogram_00_app_name"} 5050\n
```
### Line Protocol
#### Counter Metric Type
The Line Protocol exporter exports the current value of the Counter metrics. As follows.
```txt
counter_00,app_name=counter_00_app_name counter_00=0\n
```
#### Gauge Metric Type
The Line Protocol exporter exports the current value of the Gauge metrics. as follows.
```txt
gauge_00,app_name=gauge_00_app_name gauge_00=0\n
```
#### Summary Metric Type
The Line Protocol exporter exports the Summary metrics. As follows.
```
summary_00,app_name=summary_00_app_name P50=0,P80=0,P99=0,max=0,min=0,avg=0,stddev=0.00,cnt=0\n
```
#### Histogram Metric Type
The Line Protocol exporter exports the Histogram metrics in a table format. As follows.
```
summary_00,app_name=summary_00_app_name le50=0,le80=0,le99=0,max=0,min=0,avg=0,stddev=0.00,cnt=0\n
```
## Table format
The table format is a separated concept. It is a reencapsulation of metrics whose metric type is counter and gauge.
### Local Exporter
The Local Exporter exports the table in the following manner.
```
__________________________________
             column_0	  column_1
line_0              0	         1	
line_1              1	         2
```
### Prometheus Exporter
The Prometheus Exporter exports the table in the following manner.
```
column_0{app_name="table_0_app_name",table_name="table_0",line_name="line_0"} 0\n
column_1{app_name="table_0_app_name",table_name="table_0",line_name="line_0"} 1\n
column_0{app_name="table_0_app_name",table_name="table_0",line_name="line_1"} 1\n
column_1{app_name="table_0_app_name",table_name="table_0",line_name="line_1"} 2\n

```
### Line Protocol Exporter
The Line Protocol Exporter exports the table in the following manner.
```
line_0,app_name=table_0_app_name,table_name=table_0 column_0=0,column_1=1\n
line_1,app_name=table_0_app_name,table_name=table_0 column_0=1,column_1=2\n
```
## Static and Dynamic Interface
The Fieldstat3 has static and dynamic interfaces. 
### Static Interface
The static interfaces provide prometheus exporter, local exporter, and Line protocol expoeter.
There are used in environments with low performance requirements.
### Dynamic Interface
The static interfaces only provide Line Protocol expoeter. 
There are used in environments with high performance requirements.

## Usage
Here are some examples of calling function interfaces.
### Using Statics Interface
#### Create Fieldstat3 Instance
```c
struct fieldstat_instance *instance = fieldstat_instance_new("fs3_example");
```
#### Enable Line Protocol Exporter
```c
int ret = fieldstat_set_line_protocol_server(instance, "127.0.0.1", 8001);
```
#### Enable Prometheus Exporter
```c
int ret = fieldstat_global_enable_prometheus_endpoint(9020, "/metrcis");
int ret = fieldstat_enable_prometheus_output(instance);
```
#### Enable Local Exporter
```c
int ret = fieldstat_set_local_output(instance, "/tmp/fieldstat3.txt", "default");
```
#### Register and Operate Table
```c
int ret = -1;
const char *columns[]          = {"column0", "column1", "column2", "column3"};
enum field_type columns_type[] = {FIELD_METRIC_TYPE_COUNTER,FIELD_METRIC_TYPE_COUNTER, FIELD_METRIC_TYPE_COUNTER, FIELD_METRIC_TYPE_COUNTER};
int table_id = fieldstat_register_table(instance, "table_name", columns, columns_type, sizeof(columns)/sizeof(column[0]));

int output_metrics_id[4];
int ret = fieldstat_register_table_row(instance, table_id, "row0", NULL, 0, output_metrics_id);
//Incrby
ret = fieldstat_value_incrby(instance, output_metric_ids[0], 1);
//Decrby
ret = fieldstat_value_decrby(instance, output_metric_ids[0], 1);
//Set
ret = fieldstat_value_set(instance, output_metric_ids[0], 1);
```
#### Register and Operate Counter
```c
int ret = 0;
int mid = fieldstat_register(instance, FIELD_METRIC_TYPE_COUNTER, "counter00", NULL, 0);
//Incrby
ret = fieldstat_value_incrby(instance, mid, 10);
//Decrby
ret = fieldstat_value_decrby(instance, mid, 10);
//Set
ret = fieldstat_value_set(instance, mid, 10);
``` 
#### Register and Operate Gague
```c
int ret = 0;
int mid = fieldstat_register(instance, FIELD_METRIC_TYPE_GAUGE,   "gauge00",   NULL, 0);
//Incrby
ret = fieldstat_value_incrby(instance, mid, 10);
//Decrby
ret = fieldstat_value_decrby(instance, mid, 10);
//Set
ret = fieldstat_value_set(instance, mid, 10);
```
#### Register and Operate Histogram
```c
const char * bin = "10,20,30,40,50,60,70,80,90";
int mid = fieldstat_register_histogram(instance, "htr", NULL, 0, bins, 1, 1000, 2, 0);
//Append value into histogram set.
ret = fieldstat_value_set(instance, mid, 10);
```
#### Register and Operate Summary
```c
const char * bins = "0.1,0.2,0.3,0.4,0.5,0.8,0.9,0.95,0.99";
int mid = fieldstat_register_summary(instance,   "htr", NULL, 0, bins, 1, 1000, 2, 0);
//Append value into summary set.
ret = fieldstat_value_set(instance, mid, 10);
```
#### Start Fieldstat Instance
```c
fieldstat_instance_start(instance);
```

### Using Dynamic Interface
#### Create Fieldstat3 Instance
```c
int n_threads = 0;
struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("fs3_example", n_threads);
```
#### Enable Line Protocol Exporter
```c
int ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8001);
```
#### Register and Operate Table
```c
int ret = -1;
int thread_id = 0;
int out_column_ids[4];

const char *columns[]          = {"column0", "column1", "column2", "column3"};
enum field_type columns_type[] = {FIELD_METRIC_TYPE_COUNTER,FIELD_METRIC_TYPE_COUNTER, FIELD_METRIC_TYPE_COUNTER, FIELD_METRIC_TYPE_COUNTER};
int table_id = fieldstat_register_dynamic_table(instance, "table_name", columns, columns_type, sizeof(columns)/sizeof(columns[0]), out_column_ids);
//Incrby
ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[0], "table_name", 1, NULL, 0, thread_id);
//Decrby
ret = fieldstat_dynamic_table_metric_value_decrby(instance, table_id, out_column_ids[0], "table_name", 1, NULL, 0, thread_id);
//Set
ret = fieldstat_dynamic_table_metric_value_set(instance, table_id, out_column_ids[0], "table_name", 1, NULL, 0, thread_id);
```
#### Register and Operate Counter
```c
int ret = 0;
int thread_id = 0;
//Incrby
ret = fieldstat_dynamic_metric_value_incrby(instance, FIELD_METRIC_TYPE_COUNTER, "counter00", 1, NULL, 0, thread_id);
//Decrby
ret = fieldstat_dynamic_metric_value_decrby(instance, FIELD_METRIC_TYPE_COUNTER, "counter00", 1, NULL, 0, thread_id);
//Set
ret = fieldstat_dynamic_metric_value_set(instance,    FIELD_METRIC_TYPE_COUNTER, "counter00", 1, NULL, 0, thread_id);
```
#### Register and Operate Gauge
```c
int ret = 0;
int thread_id = 0;
//Incrby
ret = fieldstat_dynamic_metric_value_incrby(instance, FIELD_METRIC_TYPE_GAUGE, "counter00", 1, NULL, 0, thread_id);
//Decrby
ret = fieldstat_dynamic_metric_value_decrby(instance, FIELD_METRIC_TYPE_GAUGE, "counter00", 1, NULL, 0, thread_id);
//Set
ret = fieldstat_dynamic_metric_value_set(instance,    FIELD_METRIC_TYPE_GAUGE, "counter00", 1, NULL, 0, thread_id);
``` 

#### Start Fieldstat Instance
```c
fieldstat_dynamic_instance_start(instance);
```
