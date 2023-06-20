#pragma once

#include <stddef.h>
#include "fieldstat.h"

struct cell;

void cell_add_tag(struct cell *pthis, const struct fieldstat_tag *tag_list, size_t tag_num);
void cell_read_tag(const struct cell *pthis, struct fieldstat_tag **tag_list, size_t *tag_count);
void cell_free(struct cell *pthis);
void cell_refresh(struct cell *pthis);
struct cell * cell_duplicate(const struct cell *pthis);
void cell_serialize(const struct cell *pthis, char **blob, size_t *blob_size);
struct cell *cell_deserialize(const char *blob, size_t blob_size);
int cell_merge(struct cell *pthis, const struct cell *from);

struct cell *cell_create_for_topk(int K);
// TODO: blob 里带类型，带版本号（协议版本号），当前得知道是什么metric
void cell_oper_for_topk(struct cell *pthis, const char *key, size_t key_len, unsigned value);
// TODO：rename 
void cell_query_for_topk(const struct cell *pthis, char ***keys_out, long long **values_out, size_t *num_out);

struct cell *cell_create_for_counter();
void cell_oper_for_counter(struct cell *pthis, long long value);
// TODO: set
long long cell_query_for_counter(const struct cell *pthis);
// TODO: 命名 get / read

struct cell *cell_create_for_cardinality(const char precision);
int cell_oper_for_cardinality(struct cell *pthis, const char *key, size_t key_len);
double cell_query_for_cardinality(const struct cell *pthis);

struct cell *cell_create_for_histogram(long long lowest_trackable_value, long long highest_trackable_value, int significant_figures);
int cell_oper_for_histogram(struct cell *pthis, long long value);
long long cell_query_for_histogram(const struct cell *pthis, double percentile);