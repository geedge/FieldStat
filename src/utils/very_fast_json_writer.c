
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct json_writer {
    char *buffer;
    size_t cursor;
    size_t buffer_size;
};

// #define QUOTATION_MARK \"
#define INT_STR_LEN 11
#define DOUBLE_STR_LEN 32
#define LONG_LONG_STR_LEN 21
#define PAGE_SIZE 4096

struct json_writer *json_writer_init() {
    struct json_writer *writer = malloc(sizeof(struct json_writer));
    writer->buffer_size = PAGE_SIZE;
    writer->buffer = calloc(writer->buffer_size, sizeof(char));
    writer->cursor = 0;
    return writer;
}

void json_writer_check_and_realloc(struct json_writer *writer, size_t size) {
    size_t dest_size = writer->cursor + size;

    if (dest_size < writer->buffer_size) {
        return;
    }

    while (writer->buffer_size <= dest_size) {
        writer->buffer_size += PAGE_SIZE;
        writer->buffer = realloc(writer->buffer, writer->buffer_size);
    }
}

void json_writer_start_map(struct json_writer *writer){
    json_writer_check_and_realloc(writer, 1);
    writer->buffer[writer->cursor] = '{';
    writer->cursor++;
}

void json_writer_end_map(struct json_writer *writer) {
    if (writer->buffer[writer->cursor - 1] == '{') { // empty map
        json_writer_check_and_realloc(writer, 1);
        writer->buffer[writer->cursor++] = '}';
        return;
    }
    writer->buffer[writer->cursor - 1] = '}'; // replace last comma with }
    json_writer_check_and_realloc(writer, 1);
    writer->buffer[writer->cursor++] = ',';
}

void json_writer_array_start(struct json_writer *writer) {
    json_writer_check_and_realloc(writer, 1);
    writer->buffer[writer->cursor++] = '[';
}

void json_writer_array_end(struct json_writer *writer) {
    if (writer->buffer[writer->cursor - 1] == '[') { // empty array
        json_writer_check_and_realloc(writer, 1);
        writer->buffer[writer->cursor++] = ']';
        return;
    }
    writer->buffer[writer->cursor - 1] = ']'; // replace last comma with ]
}

void json_writer_str_item(struct json_writer *writer, const char *value, size_t str_len) {
    json_writer_check_and_realloc(writer, str_len + 2); // 2 for quotation mark
    writer->buffer[writer->cursor++] = '"';
    memcpy(writer->buffer + writer->cursor, value, str_len);
    writer->cursor += str_len;
    writer->buffer[writer->cursor++] = '"';
}

void json_writer_str_field(struct json_writer *writer, const char *key, const char *value, size_t str_len)
{
    json_writer_str_item(writer, key, strlen(key));
    json_writer_check_and_realloc(writer, 1);
    writer->buffer[writer->cursor++] = ':';
    json_writer_str_item(writer, value, str_len);
    json_writer_check_and_realloc(writer, 1);
    writer->buffer[writer->cursor++] = ',';
}

void json_writer_int_field(struct json_writer *writer, const char *key, int value){
    json_writer_str_item(writer, key, strlen(key));
    json_writer_check_and_realloc(writer, 1 + INT_STR_LEN + 1); // 1 for colon, 1 for comma
    writer->buffer[writer->cursor++] = ':';
    int len = snprintf(writer->buffer + writer->cursor, INT_STR_LEN, "%d", value);
    writer->cursor += len;
    writer->buffer[writer->cursor++] = ',';
}

void json_writer_double_field(struct json_writer *writer, const char *key, double value)
{
    json_writer_str_item(writer, key, strlen(key));
    json_writer_check_and_realloc(writer, 1 + DOUBLE_STR_LEN + 1); // 1 for colon, 1 for comma
    writer->buffer[writer->cursor++] = ':';
    int len = snprintf(writer->buffer + writer->cursor, DOUBLE_STR_LEN, "%.2f", value);
    writer->cursor += len;
    writer->buffer[writer->cursor++] = ',';
}

void json_writer_longlong_field(struct json_writer *writer, const char *key, long long value)
{
    json_writer_str_item(writer, key, strlen(key));
    json_writer_check_and_realloc(writer, 1 + LONG_LONG_STR_LEN + 1); // 1 for colon, 1 for comma
    writer->buffer[writer->cursor++] = ':';
    int len = snprintf(writer->buffer + writer->cursor, LONG_LONG_STR_LEN, "%lld", value);
    writer->cursor += len;
    writer->buffer[writer->cursor++] = ',';
}

void json_writer_finish(struct json_writer *writer, char **result, size_t *result_len)
{
    if (writer->cursor == 0) {
        free(writer->buffer);
        *result = NULL;
        *result_len = 0;
        free(writer);
        return;
    }

    writer->buffer[writer->cursor - 1] = '\0'; // replace last comma with \0
    *result = writer->buffer;
    *result_len = writer->cursor - 1; // exclude the comma  
    free(writer);
}

const char *json_writer_unwrap(const struct json_writer *writer)
{
    if (writer->cursor == 0) {
        return NULL;
    }

    return writer->buffer;
}

void json_writer_object_item(struct json_writer *writer, const char *key, struct json_writer *writer_in)
{
    json_writer_str_item(writer, key, strlen(key));

    json_writer_check_and_realloc(writer, 2);
    writer->buffer[writer->cursor++] = ':';
    writer->buffer[writer->cursor++] = '{';

    char *str_writer_in;
    size_t str_writer_in_len;
    json_writer_finish(writer_in, &str_writer_in, &str_writer_in_len);
    if (str_writer_in_len != 0) {
        json_writer_check_and_realloc(writer, str_writer_in_len);
        memcpy(writer->buffer + writer->cursor, str_writer_in, str_writer_in_len);
        writer->cursor += str_writer_in_len;
        free(str_writer_in);
    }

    json_writer_check_and_realloc(writer, 2);
    writer->buffer[writer->cursor++] = '}';
    writer->buffer[writer->cursor++] = ',';
}
