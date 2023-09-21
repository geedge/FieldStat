#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "fieldstat.h"

struct metric;

const char *metric_get_name(const struct metric *metrics); // don't free the return value
enum metric_type metric_get_type(const struct metric *metrics);
void metric_free(struct metric *pthis);
void metric_reset(struct metric *pthis);
struct metric *metric_copy(const struct metric *src);
struct metric *metric_copy_structure(const struct metric *src);
int metric_serialize(const struct metric *pthis, char **blob, size_t *blob_size);
struct metric *metric_deserialize(const char *blob, size_t blob_size);

//return -1 when merge error. 0 when success. 1 when src has no cell.
int metric_merge_or_copy_cell(struct metric *dest, const struct metric *src, int dest_cell_id, int src_cell_id);
void metric_get_plain_blob(const struct metric *pthis, int cell_id, char **blob, size_t *blob_size);
void metric_delete_cell(struct metric *pthis, int cell_id);

struct metric *metric_counter_new(const char *name, enum counter_mode mode);
void metric_counter_incrby(struct metric *pthis, int cell_id, long long value);
int metric_counter_set(struct metric *pthis, int cell_id, long long value);
int metric_counter_get(const struct metric *pthis, int cell_id, long long *value_out);

struct metric *metric_hll_new(const char *name, unsigned char precision);
void metric_hll_add(struct metric *pthis, int cell_id, const char *key, size_t key_len);
double metric_hll_get(const struct metric *pthis, int cell_id);

struct metric *metric_histogram_new(const char *name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures);
int metric_histogram_record(struct metric *pthis, int cell_id, long long value);
// for the meaning of these two query method, refer to https://prometheus.io/docs/concepts/metric_types/, and search for "histogram" and "summary"
long long metric_histogram_value_at_percentile(const struct metric *pthis, int cell_id, double percentile);
long long metric_histogram_count_le_value(const struct metric *pthis, int cell_id, long long value);


void metric_get_cell_ids(const struct metric *pthis, int **cell_ids, size_t *cell_id_count);
