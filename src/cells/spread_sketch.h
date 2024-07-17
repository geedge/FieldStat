#pragma once



#ifdef __cplusplus
extern "C"{
#endif

#include "exdata.h"
#include <stdint.h> //uint64_t
#include <sys/time.h> // struct timeval
#include <stddef.h>

#define DUMMY_ITEM_HASH (1ULL<<63) // level(left most zeros) = 0

struct spread_sketch;

struct spread_sketch *spread_sketch_new(int depth, int width, unsigned char precision, int time_window_ms, struct timeval now);

void spread_sketch_free(struct spread_sketch *ss);

void spread_sketch_reset(struct spread_sketch *ss);

int spread_sketch_add_hash(struct spread_sketch *ss, const char *key, size_t key_length, uint64_t item_hash, void *arg, struct timeval now);
int spread_sketch_add(struct spread_sketch *ss, const char *key, size_t key_length, const char* item, size_t item_len, void *arg, struct timeval now);

void spread_sketch_set_exdata_schema(struct spread_sketch *ss, exdata_new_cb new_fn, exdata_free_cb free_fn, exdata_merge_cb merge_fn, exdata_reset_cb reset_fn, exdata_copy_cb copy_fn);

// get the number of cells in the heavy keeper
int spread_sketch_get_count(const struct spread_sketch *ss);

void spread_sketch_list_keys(const struct spread_sketch *ss, char ***keys, size_t **key_lens, size_t *n_keys);
double spread_sketch_get_cardinality(const struct spread_sketch *ss, const char *key, size_t key_len);
char *spread_sketch_get_hll_base64_serialization(const struct spread_sketch *ss, const char *key, size_t key_len);
void *spread_sketch_get0_exdata(const struct spread_sketch *ss, const char *key, size_t key_len);

void spread_sketch_merge(struct spread_sketch *dest, const struct spread_sketch *src);

struct spread_sketch *spread_sketch_copy(const struct spread_sketch *src);

void spread_sketch_get_parameter(const struct spread_sketch *ss, int *depth_out, int *width_out, unsigned char *precision_out, int *time_window_ms_out);
/*
// spread sketch alway store values more than expected_query_num,expected_query_num is a hint to set spread sketch parameters properly
*/
void spread_sketch_get_parameter_recommendation(int expected_super_spreader_number, int *depth_out, int *width_out, unsigned char *precision_out);


// for test
// void spread_sketch_one_point_query(const struct spread_sketch *ss, const char *key, size_t key_len, int *level_out, void **exdata_out);

#ifdef __cplusplus
}
#endif