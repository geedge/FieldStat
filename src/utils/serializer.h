#pragma once

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

struct fs_reader;

struct fs_reader *fs_reader_new();
void fs_reader_free(struct fs_reader *reader);

void fs_reader_read_uint(struct fs_reader *reader, uint32_t value);
void fs_reader_read_longlong(struct fs_reader *reader, uint64_t value);
void fs_reader_read_double(struct fs_reader *reader, double value);
void fs_reader_read_bin(struct fs_reader *reader, const char *value, size_t size);
void fs_reader_read_nil(struct fs_reader *reader);
void fs_reader_read_str(struct fs_reader *reader, const char *value);
void fs_reader_start_bin_array(struct fs_reader *reader, size_t size);
void fs_reader_read_int_array(struct fs_reader *reader, const int *value, size_t size); 

const char *fs_reader_unwrap(const struct fs_reader *reader, size_t *size);
void fs_reader_finalize(struct fs_reader *reader, char **blob, size_t *size);
/* ------------------------------------------------------------------------ */
struct fs_writer;
struct fs_writer *fs_writer_new(const char *blob, size_t size);
void fs_writer_free(struct fs_writer *writer);

int fs_writer_write_uint(struct fs_writer *writer, uint32_t *value);
int fs_writer_write_longlong(struct fs_writer *writer, long long *value);
int fs_writer_write_double(struct fs_writer *writer, double *value);
int fs_writer_write_bin(struct fs_writer *writer, char **value, size_t *size);
int fs_writer_expect_nil(struct fs_writer *writer); // return 0 if not nil. return 1 if nil, and move cursor.
int fs_writer_write_str(struct fs_writer *writer, char **value);
int fs_writer_write_bin_array(struct fs_writer *writer, char ***array, size_t *n_arr, size_t **arr_len);
int fs_writer_write_int_array(struct fs_writer *writer, int **value, size_t *size);


#ifdef __cplusplus
}
#endif