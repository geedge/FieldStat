#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C"{
#endif

#include "exdata.h"

#define DUMMY_ITEM_HASH (1ULL<<63) // level(left most zeros) = 0

struct spread_sketch;

// spread sketch alway store values more than expected_query_num,expected_query_num is a hint to set spread sketch parameters properly
struct spread_sketch *spread_sketch_new(int expected_query_num);

void spread_sketch_free(struct spread_sketch *ss);

void spread_sketch_reset(struct spread_sketch *ss);

int spread_sketch_add(struct spread_sketch *ss, const char *key, size_t key_length, uint64_t item_hash, void *arg);
void spread_sketch_set_exdata_schema(struct spread_sketch *ss, exdata_new_cb new_fn, exdata_free_cb free_fn, exdata_merge_cb merge_fn, exdata_reset_cb reset_fn, exdata_copy_cb copy_fn);

void *spread_sketch_get0_exdata(const struct spread_sketch *ss, const char *key, size_t key_len);

// get the number of cells in the heavy keeper
int spread_sketch_get_count(const struct spread_sketch *ss);

size_t spread_sketch_list(const struct spread_sketch *ss, void **exdatas, size_t n_exdatas);

void spread_sketch_merge(struct spread_sketch *dest, const struct spread_sketch *src);

struct spread_sketch *spread_sketch_copy(const struct spread_sketch *src);

// for test
// void spread_sketch_one_point_query(const struct spread_sketch *ss, const char *key, size_t key_len, int *level_out, void **exdata_out);

#ifdef __cplusplus
}
#endif