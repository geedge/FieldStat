#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "fieldstat.h" // for enum metric_type
#include "metric_manifest.h"

struct metric;

void metric_free(struct metric *pthis);
void metric_reset(struct metric *pthis);
struct metric *metric_copy(const struct metric *src);
struct metric *metric_new(const struct metric_manifest *manifest);
int metric_merge(struct metric *dest, const struct metric *src);
void metric_serialize(const struct metric *pthis, char **blob, size_t *blob_size);

void metric_counter_incrby(struct metric *pthis, long long value);
void metric_counter_set(struct metric *pthis, long long value);
long long metric_counter_get(const struct metric *pthis);

void metric_hll_add(struct metric *pthis, const char *key, size_t key_len);
double metric_hll_get(const struct metric *pthis);

int metric_histogram_record(struct metric *pthis, long long value);
// for the meaning of these two query method, refer to https://prometheus.io/docs/concepts/metric_types/, and search for "histogram" and "summary"
long long metric_histogram_value_at_percentile(const struct metric *pthis, double percentile);
long long metric_histogram_count_le_value(const struct metric *pthis, long long value);

