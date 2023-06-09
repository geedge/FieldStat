
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>

// #include "serializer.h"


// /* Interface realization: 
//     this serializer is used to output the current metrics of instance server, which will: firstly merge all instance from different process or thread, then output the merged result.
//     Strictly speaking, this is not a serializer, just to generate a query result. 

//     serializer_func_access: fill result with metrics data.
//     serializer_func_gen : NULL

//     */


// struct metric_output {
//     struct serializer base;
//     struct result_kvpair **fields;
//     size_t field_num;
//     struct result_kvpair **tags;
//     size_t tag_num;
//     char *field_name; // not a key, actually is the field name
//     size_t field_name_len; // include '\0'
//     int parent_id;            // its container field id
//     enum field_type type;   // its container field type
// };

// struct serializer_result {
//     struct serializer base;
//     struct metric_output **metric_output;
//     size_t metric_output_num;
// };

// int serializer_result_access(struct serializer *pthis, struct metrics *metrics)
// {
//     struct serializer_result *result = (struct metric_output *)pthis;

//     int ret = metrics->dump(metrics, pthis);

//     //TODO: 写pair的借用merge + zip

//     return ret;
// }

// int serializer_result_gen(struct serializer *pthis, const char *instance_name, const char **tag_key, const char **tag_value, size_t tag_nums,
//     char **blob, size_t *blob_size)
// {
//     printf("function serializer_result_gen is not implemented\n");
//     return -1;
// }

// // kvpair 反正metrics 里有一份，所以总之还是要释放的，用户那份自己处理
// // TODO：开放这个的接口？
// void free_kvpair(struct result_kvpair *kvpair)
// {
//     free(kvpair->key);
//     if (kvpair->value_type == VT_STRING) {
//         free(kvpair->string_value);
//     }
//     free(kvpair);
// }

// void metric_output_free(struct metric_output *mo)
// {
//     for (int i = 0; i < mo->field_num; i++) {
//         free_kvpair(mo->fields[i]);
//     }
//     free(mo->fields);
//     for (int i = 0; i < mo->tag_num; i++) {
//         free_kvpair(mo->tags[i]);
//     }
//     free(mo->tags);
//     free(mo->field_name);
//     free(mo);
// }

// void serializer_result_del(struct serializer *pthis)
// {
//     struct serializer_result *result = (struct serializer_result *)pthis;
//     for (int i = 0; i < result->metric_output_num; i++) {
//         metric_output_free(result->metric_output[i]);
//     }
//     free(result->metric_output);
//     free(result);
// }

// struct serializer_result *serializer_result_create
// {

// }

// void metric_output_free(struct metric_output *mo);
// int metric_output_read_tags(struct metric_output *mo, struct result_kvpair *tags_out, size_t buf_len); // buffer should be lone enough, -1 if buffer not lone enough
// int metric_output_read_fields(struct metric_output *mo, struct result_kvpair *fields_out, size_t buf_len); // buffer should be lone enough, -1 if buffer not lone enough
// int metric_output_read_field_name(struct metric_output *mo, char *field_name_out, size_t buf_len);
// enum field_type metric_output_get_type(struct metric_output *mo);