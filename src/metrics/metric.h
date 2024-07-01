#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "fieldstat.h" // for enum metric_type

struct metric;

const char *metric_get_name(const struct metric *metrics); // don't free the return value
enum metric_type metric_get_type(const struct metric *metrics);
void metric_free(struct metric *pthis);
void metric_reset(struct metric *pthis);
struct metric *metric_copy(const struct metric *src);
struct metric *metric_fork(const struct metric *src);

int metric_merge(struct metric *dest, const struct metric *src);
void metric_get_plain_blob(const struct metric *pthis, char **blob, size_t *blob_size);

struct metric *metric_counter_new(const char *name);
void metric_counter_incrby(struct metric *pthis, long long value);
void metric_counter_set(struct metric *pthis, long long value);
long long metric_counter_get(const struct metric *pthis);

struct metric *metric_hll_new(const char *name, unsigned char precision);
void metric_hll_add(struct metric *pthis, const char *key, size_t key_len);
double metric_hll_get(const struct metric *pthis);

struct metric *metric_histogram_new(const char *name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures);
int metric_histogram_record(struct metric *pthis, long long value);
// for the meaning of these two query method, refer to https://prometheus.io/docs/concepts/metric_types/, and search for "histogram" and "summary"
long long metric_histogram_value_at_percentile(const struct metric *pthis, double percentile);
long long metric_histogram_count_le_value(const struct metric *pthis, long long value);

