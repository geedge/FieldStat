#pragma once

#include <stddef.h>
#include "fieldstat.h"

struct cell;

void cell_add_tag(struct cell *pthis, const struct fieldstat_tag *tag_list, size_t tag_num);
void cell_read_tag(const struct cell *pthis, struct fieldstat_tag **tag_list, size_t *tag_count);
void cell_free(struct cell *pthis);
void cell_refresh(struct cell *pthis);
struct cell *cell_duplicate(const struct cell *pthis);
void cell_serialize(const struct cell *pthis, char **blob, size_t *blob_size);
struct cell *cell_deserialize(const char *blob, size_t blob_size);
int cell_merge(struct cell *pthis, const struct cell *from);

struct cell *cell_create_for_topk(int K);
void cell_topk_add(struct cell *pthis, const char *key, size_t key_len, unsigned value);
void cell_topk_query(const struct cell *pthis, char ***keys_out, long long **values_out, size_t *num_out);

struct cell *cell_create_for_counter();
void cell_counter_incrby(struct cell *pthis, long long value);
long long cell_counter_get(const struct cell *pthis);

struct cell *cell_create_for_cardinality(const char precision);
int cell_cardinality_add(struct cell *pthis, const char *key, size_t key_len);
double cell_cardinality_get(const struct cell *pthis);

struct cell *cell_create_for_histogram(long long lowest_trackable_value, long long highest_trackable_value, int significant_figures);
int cell_histogram_record(struct cell *pthis, long long value);
long long cell_histogram_value_at_percentile(const struct cell *pthis, double percentile);