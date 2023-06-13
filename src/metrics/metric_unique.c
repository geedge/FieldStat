// #include <stdlib.h>
// #include <stdio.h>
// #include <string.h>
// #include <stddef.h>
// #include <assert.h>

// #include "metrics/metric_unique.h"
// #include "cjson/cJSON.h"
// #include "output/serializer.h"
// #include "metric_type_internal.h"

// struct metric_unique {
//     struct cell base;
//     struct ST_hyperloglog *unique;
// };


// void metric_unique_refresh(struct cell *pthis)
// {
//     struct metric_unique *unique = (struct metric_unique *)pthis;
//     ST_hyperloglog_reset(unique->unique);
// }

// void metric_unique_del(struct cell *pthis)
// {
//     struct metric_unique *unique = (struct metric_unique *)pthis;
//     free(pthis->name);
//     ST_hyperloglog_free(unique->unique);
//     free(unique);
// }

// void metric_unique_serialization(const struct ST_hyperloglog *h, char **blob, size_t *blob_sz)
// {
//     size_t num_reg = NUM_REG(h->cfg.precision);
//     size_t words = INT_CEIL(num_reg, REG_PER_WORD);

//     cJSON *root = cJSON_CreateObject();
//     cJSON_AddStringToObject(root, "type", "unique");
//     cJSON_AddNumberToObject(root, "precision", h->cfg.precision);

//     cJSON *reg = cJSON_CreateArray();
//     for (int i = 0; i < words; i++) {
//         cJSON *y_json = cJSON_CreateNumber(h->registers[i]);
//         cJSON_AddItemToArray(reg, y_json);
//     }
//     cJSON_AddItemToObject(root, "reg", reg);

//     char *out = cJSON_PrintUnformatted(root);
//     *blob = out;
//     *blob_sz = strlen(out) + 1;
//     cJSON_Delete(root);

//     return;
// }

// int metric_unique_dump(struct cell *pthis, struct serializer *serializer)
// {
//     struct metric_unique *unique = (struct metric_unique *)pthis;

//     switch (serializer->type) {
//         case SERIALIZER_TYPE_BIN:
//         {
//             struct serializer_bin *bin = (struct serializer_bin *)serializer;

//             char *blob;
//             size_t blob_size;
            
//             metric_unique_serialization(unique->unique, &blob, &blob_size);

//             bin->buf[bin->buf_num] = blob;
//             bin->buf_size[bin->buf_num] = blob_size;
//             bin->field_name[bin->buf_num] = strdup(unique->base.name);
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

// int metric_unique_merge(struct cell *pthis, struct cell *from)
// {
//     struct metric_unique *unique = (struct metric_unique *)pthis;
//     struct metric_unique *from_unique = (struct metric_unique *)from;

//     return ST_hyperloglog_merge(unique->unique, from_unique->unique);
// }

// struct metric_unique *metric_unique_create(const char *name, size_t name_len, const char precision)
// {
//     struct metric_unique *unique = (struct metric_unique *)malloc(sizeof(struct metric_unique));
//     unique->unique = ST_hyperloglog_new(precision);
//     if (unique->unique == NULL) {
//         free(unique);
//         return NULL;
//     }

//     unique->base.name = strdup(name);
//     unique->base.refresh = metric_unique_refresh;
//     unique->base.del = metric_unique_del;
//     unique->base.dump = metric_unique_dump;
//     unique->base.merge = metric_unique_merge;
//     unique->base.type = FIELD_TYPE_HLL;
//     return unique;
// }

// int metric_unique_add(struct metric_unique *unique, const char *key, size_t keylen)
// {
//     return ST_hyperloglog_add(unique->unique, key, keylen);
// }
