// #include <stdlib.h>
// #include <stdio.h>
// #include <string.h>
// #include <stddef.h>
// #include <assert.h>

// #include "metrics/metric_cardinality.h"
// #include "cjson/cJSON.h"
// #include "output/serializer.h"
// #include "metric_type_internal.h"



// void metric_cardinality_refresh(struct cell *pthis)
// {
//     struct metric_cardinality *cardinality = (struct metric_cardinality *)pthis;
//     ST_hyperloglog_reset(cardinality->cardinality);
// }

// void metric_cardinality_del(struct cell *pthis)
// {
//     struct metric_cardinality *cardinality = (struct metric_cardinality *)pthis;
//     free(pthis->name);
//     ST_hyperloglog_free(cardinality->cardinality);
//     free(cardinality);
// }


// int metric_cardinality_dump(struct cell *pthis, struct serializer *serializer)
// {
//     struct metric_cardinality *cardinality = (struct metric_cardinality *)pthis;

//     switch (serializer->type) {
//         case SERIALIZER_TYPE_BIN:
//         {
//             struct serializer_bin *bin = (struct serializer_bin *)serializer;

//             char *blob;
//             size_t blob_size;
            
//             metric_cardinality_serialization(cardinality->cardinality, &blob, &blob_size);

//             bin->buf[bin->buf_num] = blob;
//             bin->buf_size[bin->buf_num] = blob_size;
//             bin->field_name[bin->buf_num] = strdup(cardinality->base.name);
//             bin->buf_num++;
//             printf("size: %zu, strlen: %zu\n", blob_size, strlen(blob));
//             return 0;

//             break;
//         }
//         default:
//             assert(0);
//             break;
//     }
// }

// int metric_cardinality_merge(struct cell *pthis, struct cell *from)
// {
//     struct metric_cardinality *cardinality = (struct metric_cardinality *)pthis;
//     struct metric_cardinality *from_cardinality = (struct metric_cardinality *)from;

//     return ST_hyperloglog_merge(cardinality->cardinality, from_cardinality->cardinality);
// }

// struct metric_cardinality *metric_cardinality_create(const char *name, size_t name_len, const char precision)
// {
//     struct metric_cardinality *cardinality = (struct metric_cardinality *)malloc(sizeof(struct metric_cardinality));
//     cardinality->cardinality = ST_hyperloglog_new(precision);
//     if (cardinality->cardinality == NULL) {
//         free(cardinality);
//         return NULL;
//     }

//     cardinality->base.name = strdup(name);
//     cardinality->base.refresh = metric_cardinality_refresh;
//     cardinality->base.del = metric_cardinality_del;
//     cardinality->base.dump = metric_cardinality_dump;
//     cardinality->base.merge = metric_cardinality_merge;
//     cardinality->base.type = FIELD_TYPE_CARDINALITY;
//     return cardinality;
// }

// int metric_cardinality_add(struct metric_cardinality *cardinality, const char *key, size_t keylen)
// {
//     return ST_hyperloglog_add(cardinality->cardinality, key, keylen);
// }
