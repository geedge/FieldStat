// #include <assert.h>
// #include <stdlib.h>
// #include <stdio.h>
// #include <string.h>

// #include "field_and_metric.h"
// #include "cjson/cJSON.h"
// #include "metric_type_internal.h"




// /*
//     "counter": {
//         "type": "counter",
//         "value": 0
//     }
// */
// void metric_counter_serialization(const struct metric_counter *counter, char **blob, size_t *blob_sz)
// {
//     cJSON *root = cJSON_CreateObject();
//     if (counter->base.type == FIELD_TYPE_COUNTER) {
//         cJSON_AddStringToObject(root, "type", "counter");
//     } else {
//         cJSON_AddStringToObject(root, "type", "gauge");
//     }
//     cJSON_AddNumberToObject(root, "value", counter->value);

//     char *out = cJSON_PrintUnformatted(root);
//     *blob = out;
//     *blob_sz = strlen(out) + 1;
//     cJSON_Delete(root);
// }

// int metric_counter_dump(struct cell *pthis, struct serializer *serializer)
// {
//     struct metric_counter *counter = (struct metric_counter *)pthis;
    
//     switch (serializer->type) {
//         case SERIALIZER_TYPE_BIN:
//         {
//             struct serializer_bin *bin = (struct serializer_bin *)serializer;

//             metric_counter_serialization(counter, &bin->buf[bin->buf_num], &bin->buf_size[bin->buf_num]);
//             bin->field_name[bin->buf_num] = strdup(counter->base.name);
//             bin->buf_num++;
//             return 0;
//             break;
//         }
//         default:
//             assert(0);
//             break;
//     }
// }

// void metric_counter_refresh(struct cell *pthis)
// {
//     if (pthis->type == METRICS_TYPE_GAUGE) {
//         return;
//     }
//     struct metric_counter *counter = (struct metric_counter *)pthis;
//     counter->value = 0;
// }

// void metric_counter_del(struct cell *pthis)
// {
//     struct metric_counter *counter = (struct metric_counter *)pthis;
//     free(pthis->name);
//     free(counter);
// }

// int metric_counter_merge(struct cell *pthis, struct cell *from)
// {
//     struct metric_counter *dest = (struct metric_counter *)pthis;
//     struct metric_counter *src = (struct metric_counter *)from;
//     dest->value += src->value;
//     return 0;
// }

// // TODO: deserialize

// struct metric *metric_counter_create()
// {
//     struct metric_counter *counter = malloc(sizeof(struct metric_counter));
//     counter->value = 0;

//     struct cell *base = (struct cell *)counter;

//     base->type = FIELD_TYPE_COUNTER;
//     base->del = metric_counter_del;
//     base->dump = metric_counter_dump;
//     base->refresh = metric_counter_refresh;
//     base->merge = metric_counter_merge;

//     return (struct metric *)counter;
// }

// void metric_counter_add(struct metric *counter, long long value)
// {
//     struct metric_counter *c = (struct metric_counter *)counter;
//     c->value += value;
// }
