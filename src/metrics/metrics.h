#pragma once

#include <stddef.h>
#include "fieldstat.h"

struct metrics;


const char *metric_get_name(const struct metrics *metrics); // don't free the return value
void metric_free(struct metrics *pthis);

void metric_refresh(struct metrics *pthis); //todo: 没用了，删掉
struct metrics *metric_duplicate(const struct metrics *pthis);
void metric_serialize(const struct metrics *pthis, char **blob, size_t *blob_size);
struct metrics *metric_deserialize(const char *blob, size_t blob_size);
int metric_merge(struct metrics *pthis, const struct metrics *from);
void metric_get_plain_blob(const struct metrics *pthis, char **blob, size_t *blob_size);

struct metrics *metric_create_for_counter(bool is_gauge);
void metric_counter_incrby(struct metrics *pthis, long long value);
void metric_counter_set(struct metrics *pthis, long long value);
long long metric_counter_get(const struct metrics *pthis);

struct metrics *metric_create_for_hll(const char precision);
int metric_hll_add(struct metrics *pthis, const char *key, size_t key_len);
double metric_hll_get(const struct metrics *pthis);

struct metrics *metric_create_for_histogram(long long lowest_trackable_value, long long highest_trackable_value, int significant_figures);
int metric_histogram_record(struct metrics *pthis, long long value);
// for the meaning of these two query method, refer to https://prometheus.io/docs/concepts/metric_types/, and search for "histogram" and "summary"
long long metric_histogram_value_at_percentile(const struct metrics *pthis, double percentile);
long long metric_histogram_count_le_value(const struct metrics *pthis, long long value);