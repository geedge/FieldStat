#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "query_apis.h"
#include "serializer_query.h"
#include "fieldstat_tag.h"


struct instance_query_result {
    struct metric_info_query_inheritance **metric_info;
    size_t metric_num;
    
};


// TODO: 调用serializer_query，并输出一个具体的结构体。为了性能稍微快一点，直接把query的结构体指针带进来。

// TODO：read tag

// TODO：read field




// struct metric_output *metric_output_create(size_t field_num, size_t tag_num);
// void metric_output_free(struct metric_output *mo);
// int metric_output_read_tags(struct metric_output *mo, struct result_kvpair *tags_out, size_t buf_len); // buffer should be lone enough, -1 if buffer not lone enough
// int metric_output_read_fields(struct metric_output *mo, struct result_kvpair *fields_out, size_t buf_len); // buffer should be lone enough, -1 if buffer not lone enough
// int metric_output_read_field_name(struct metric_output *mo, char *field_name_out, size_t buf_len);
// enum field_type metric_output_get_type(struct metric_output *mo);


// // // read_tag(struct tag *)
// // // {
// // //     return n_tag;
// // // }

// // // read_value(const struct tag*[])

// //     fields_key;
// //     key_type;
// //     field_value;
// //     valuetype