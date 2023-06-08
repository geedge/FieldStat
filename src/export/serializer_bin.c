// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <time.h>

// #include "cjson/cJSON.h"

// #include "output/serializer.h"

// int serializer_bin_access(struct serializer *pthis, struct metrics *metrics)
// {
//     struct serializer_bin *bin = (struct serializer_bin *)pthis;

//     if (bin->buf_num >= bin->max_buf_num) {
//         return -1;
//     }
//     int ret = metrics->visit(metrics, pthis);

//     return ret;
// } 


// int serializer_bin_gen(struct serializer *pthis, const char *instance_name, const char **tag_key, const char **tag_value, size_t tag_nums,
//     char **blob, size_t *blob_size)
// {
//     struct serializer_bin *bin = (struct serializer_bin *)pthis;
//     cJSON *root = cJSON_CreateObject();
//     cJSON_AddStringToObject(root, "name", instance_name);

//     cJSON *tags = cJSON_CreateObject();
//     for (size_t j = 0; j < tag_nums; j++) {
//         cJSON_AddStringToObject(tags,  tag_key[j], tag_value[j]);
//         cJSON_AddItemToObject(root, "tags", tags);
//     }

//     struct timespec this_output_time;
// 	clock_gettime(CLOCK_MONOTONIC, &this_output_time);
// 	cJSON_AddNumberToObject(root, "ts", this_output_time.tv_sec);

//     cJSON *metrics = cJSON_CreateObject();
//     for (size_t j = 0; j < bin->buf_num; j++) {
//         cJSON_AddStringToObject(metrics, bin->field_name[j], bin->buf[j]);
//     }
//     cJSON_AddItemToObject(root, "metrics", metrics);

//     // output to blob
//     char *out = cJSON_PrintUnformatted(root);

//     *blob = out;
//     *blob_size = strlen(out);
//     printf("size of cjson data: %zu\n", *blob_size);
//     cJSON_Delete(root);
//     return 0;
// }

// void serializer_bin_del(struct serializer *pthis)
// {
//     struct serializer_bin *bin = (struct serializer_bin *)pthis;
//     for (size_t i = 0; i < bin->buf_num; i++) {
//         free(bin->buf[i]);
//         free(bin->field_name[i]);
//     }
//     free(bin->buf);
//     free(bin->buf_size);
//     free(bin->field_name);
//     free(bin);
// }

// struct serializer_bin *serializer_bin_create(size_t metric_num)
// {
//     struct serializer_bin *bin = malloc(sizeof(struct serializer_bin));
//     struct serializer *base = (struct serializer *)bin;
//     base->type = SERIALIZER_TYPE_BIN;
//     base->access = serializer_bin_access;
//     base->gen = serializer_bin_gen;
//     base->del = serializer_bin_del;
//     bin->buf_num = 0;
//     bin->max_buf_num = metric_num;
//     bin->buf = malloc(sizeof(char *) * metric_num);
//     bin->buf_size = malloc(sizeof(size_t) * metric_num);
//     bin->field_name = malloc(sizeof(char *) * metric_num);

//     return bin;
// }