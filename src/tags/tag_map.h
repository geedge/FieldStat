#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C"{
#endif

#include "exdata.h"
#include "my_ut_hash.h"

struct tag_map;

struct tag_map *tag_map_new(int max_query_num);
void tag_map_set_exdata_schema(struct tag_map *pthis, exdata_new_cb new_fn, exdata_free_cb free_fn, exdata_merge_cb merge_fn, exdata_reset_cb reset_fn, exdata_copy_cb copy_fn);
void tag_map_free(struct tag_map *pthis);
void tag_map_reset(struct tag_map *pthis);
int tag_map_merge(struct tag_map *dest, struct tag_map *src);
struct tag_map *tag_map_copy(const struct tag_map *src);

// int tag_map_add(struct tag_map *pthis, const char *key, size_t key_len, int count, void *arg);
int tag_map_add(struct tag_map *pthis, const char *key, size_t key_len, void *arg);

// void *tag_map_get0_exdata(struct tag_map *pthis, const char *key, size_t key_len);
void *tag_map_get0_exdata(struct tag_map *pthis, const char *key, size_t key_len);
int tag_map_get_count(const struct tag_map *pthis);
size_t tag_map_list(const struct tag_map *pthis, void **exdatas, size_t n_exdatas);

#ifdef __cplusplus
}
#endif