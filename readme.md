#Fieldstat 4.0 

## design 
Field Stat is a library for outputting and statistics of running states. Compared to the previous version, fieldstat3.0, this version provides support for new functionalities. The following are the concepts used in this program:

- Instance: Corresponds to the `struct fieldstat` and represents the instance handle on a thread.
- Cube: An instance is composed of multiple cubes. A cube is a collection of various statistical measures.
- Cell: A cube is composed of multiple cells. A cell is a data set tagged with specific tags.
- Metric: Each metric corresponds to a column of data in the database. Currently, there are three types of metric statistics: counter, Hyper Log Log, and Histogram.

Compared to version 3.0, version 4.0 introduces the concepts of cube and cell. Cell registration is dynamic, allowing for real-time statistics on sessions. The concept of field has been removed (a combination of tag information and metric)。In version 4.0, tags and metrics are managed independently. This change simplifies the interface usage and improves the speed of batch statistics for the same tag.
Version 4.0 no longer supports multithreading. In version 3.0, the metric of type "counter" could be shared and operated on by multiple threads. However, in version 4.0, each instance is written by only one thread. The fieldstat version 4.0 provides support for distributed statistics through methods like serialize and merge. It is the responsibility of the caller to aggregate statistics from different instances.

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
### Statistic
```
#include "fieldstat.h"

struct fieldstat *instance = fieldstat_new();
int cube_id = fieldstat_register_cube(instance, YOUR_SHARED_TAG, YOUR_SHARED_TAG_LENGTH, SAMPLING_MODE_TOPK, MAX_CELL_NUMBER);
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
fieldstat_serialize(instance, &blob, &blob_size);
struct fieldstat *instance_deserialized = fieldstat_deserialize(blob, blob_size);
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

| operation                                        | time(us) | corresponding api                                                                |
| ------------------------------------------------ | -------- | -------------------------------------------------------------------------------- |
| add a cell(topk)                                 | 0.457940 | fieldstat_cube_add                                                               |
| add a cell(comprehensive)                        | 0.816540 | fieldstat_cube_add                                                               |
| histogram record                                 | 0.017060 | fieldstat_histogram_record                                                       |
| HLL add                                          | 0.018250 | fieldstat_hll_add                                                                |
| copy 1 cell of a 1 counter metric                | 0.378    | fieldstat_merge(empty destination instance)                                      |
| copy 1 cell of a HLL metric                      | 0.557    | fieldstat_merge(empty destination instance)                                      |
| copy 1 cell of a histogram metric                | 3.207    | fieldstat_merge(empty destination instance)                                      |
| copy 1 cell of a counter metric on topk cube     | 0.574    | fieldstat_merge(empty destination instance)                                      |
| merge 1000 cell of a counter metric              | 0.110    | fieldstat_merge(destination also has registered cube, metric and cells)          |
| merge 1000 cell of a HLL metric                  | 0.288    | fieldstat_merge(destination also has registered cube, metric and cells)          |
| merge 1000 cell of a histogram metric            | 1.347    | fieldstat_merge(destination instance also has registered cube, metric and cells) |
| merge 1000 cell of a counter metric on topk cube | 1.078    | fieldstat_merge(destination instance also has registered cube, metric and cells) |
| export a cell                                    | 0.00297  | fieldstat_json_exporter_export                                                   |

### Explanation
#### add a cell(topk)
Generate 100000 random tags, call fieldstat_cube_add for each tag. Calculate the mean time spent for one tag. The max cell number of cube(K) is 1000.
#### add a cell(comprehensive)
Generate 100000 random tags, call fieldstat_cube_add for each tag. Calculate the mean time spent for one tag. Every adding operation is success (The max cell number of cube is 100000).

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