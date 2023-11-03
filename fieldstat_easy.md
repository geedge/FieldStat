
# fieldstat easy

## Description
**Fieldstat easy** wraps the fieldstat to provide a simpler interface and multi-threaded support. For details, refer to [fieldstat](readme.md).

Fieldstat easy support multi-threaded writing and output. The output of fieldstat easy is in json format, which can be easily parsed by other python.

All the metrics in fieldstat easy are dynamic, which means that the metrics has no volume limit, and users can add any tags to the metrics as needed and at any time.

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
int hdr_id = fieldstat_easy_register_histogram(fse, "delay", 1, 10000, 1);
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
            "delay_delta": "HISTEwAAAOgAAAAAAAAAAQAAAAAAAAABAAAAAAAAJxA/8AAAAAAAAAQEAQYKCgIEAggECgYECgQGBAYIDgoMAgQBCgQECAICEAIUDBIUEg4ECAwKDBQGFCAWFBYYFBIcIhogGhYiEhg6JDQ4NDw+GjouOC4oMC4ufGZidGhaYGBMXGJYYk5eXugBrAHYAdQB7AHeAcoB3AHAAfIBwAHYAdgBwgG+Ae4BzgOqA6QDsAOUA64DrAOMA4gD5gL4ArAD4AKYA44DtAOCBvgFkgaiBrIG1AasBswG0ga2BqAGpAaKBrYGpAamBvAM3AzWDI4NgA2mDKIMkg2uDIANiA3gDMQMlA2ODfIM2hikGf4XjA0AAAAAAAAA"
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
 - `fields` The fields of the metrics. The fields of the counter (`bytes` in example) are of integer type, and the fields of the histogram (`delay` in example) are of string type(in base64 format). Thoses with key ending with `_delta` are metrics recording the values of the current output interval. While the value of the fields is the value of all time.

The output histogram is a base64 encoded string. You can decode it with the following command.

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

c_hist  = FieldstatAPI.libfieldstat.fieldstat_histogram_base64_decode(value.encode('utf-8'))
sum_value = FieldstatAPI.libfieldstat.fieldstat_histogram_value_sum(c_hist)
FieldstatAPI.libfieldstat.fieldstat_histogram_free(c_hist) # need to free memory, because it's a C binding
```

### prometheus 


### output to file


