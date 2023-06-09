// #pragma once


// struct result_kvpair {
//     char *key;
//     size_t key_len;
//     union {
//         int64_t int_value;
//         double double_value;
//         char *string_value;
//     };
//     enum value_type value_type;
//     size_t value_len;
// };

// struct metric_output;

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

// // struct line
// // {
// //     char *field_name;
// //     int id;
// //     char *tags[];
// //     tag_number;
// //     struct field *f[];

// //     value_type[];
// // }

// // metric 
// // struct line lines = read_metric(struct metric);

// // field_name:COUNER 
// // ID: 
// // TAG: "IP"
// // TAG_VALUE:"100"
// // fields:[count]
// // field_value:[10]

// // field_name:COUNER 
// // TAG: "IP", "PORT"
// // TAG_VALUE:"100","9000"
// // fields:["count"]
// // field_value:[10]

// // field_name:HDR 
// // TAG: "IP", "PORT"
// // TAG_VALUE:"100","9000"
// // fields:["0.5", 0.9]
// // field_value:[10, 1234]

// // field_name:TOPK 
// // TAG: "IP", "PORT"
// // TAG_VALUE:"100","9000"
// // fields:["100.56465", "2"..."65498."]
// // field_value:[10, 1234, ]


// // free_line();




// // /* -------------------------------------------------------------------------- */
// // /*                                    plan2                                   */
// // /* -------------------------------------------------------------------------- */
// // read_counter(struct counter *counter)
// // {
// //     return counter->value;
// // }

// // read_metric {
// //     return struct field *[]
// // }

// // int read_metric_number(instance)
// // {
// //     return instance->metric_number;
// // }
