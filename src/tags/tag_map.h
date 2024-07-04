#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C"{
#endif

#include "exdata.h"
#include "my_ut_hash.h"

struct hash_table;

struct hash_table *hash_table_new(int max_query_num);
void hash_table_set_exdata_schema(struct hash_table *pthis, exdata_new_cb new_fn, exdata_free_cb free_fn, exdata_merge_cb merge_fn, exdata_reset_cb reset_fn, exdata_copy_cb copy_fn);
void hash_table_free(struct hash_table *pthis);
void hash_table_reset(struct hash_table *pthis);
int hash_table_merge(struct hash_table *dest, struct hash_table *src);
struct hash_table *hash_table_copy(const struct hash_table *src);

// int hash_table_add(struct hash_table *pthis, const char *key, size_t key_len, int count, void *arg);
int hash_table_add(struct hash_table *pthis, const char *key, size_t key_len, void *arg);

// void *hash_table_get0_exdata(struct hash_table *pthis, const char *key, size_t key_len);
void *hash_table_get0_exdata(struct hash_table *pthis, const char *key, size_t key_len);
int hash_table_get_count(const struct hash_table *pthis);
size_t hash_table_list(const struct hash_table *pthis, void **exdatas, size_t n_exdatas);

#ifdef __cplusplus
}
#endif