#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct json_writer;

// #define QUOTATION_MARK \"


struct json_writer *json_writer_init();
void json_writer_start_map(struct json_writer *writer); // {
void json_writer_end_map(struct json_writer *writer); // }
void json_writer_array_start(struct json_writer *writer); // [
void json_writer_array_end(struct json_writer *writer); // ]

void json_writer_str_field(struct json_writer *writer, const char *key, const char *value, size_t str_len);
void json_writer_int_field(struct json_writer *writer, const char *key, int value);
void json_writer_double_field(struct json_writer *writer, const char *key, double value);
void json_writer_longlong_field(struct json_writer *writer, const char *key, long long value);
void json_writer_object_item(struct json_writer *writer, const char *key, struct json_writer *writer_in);
void json_writer_finish(struct json_writer *writer, char **result, size_t *result_len);

#ifdef __cplusplus
}
#endif