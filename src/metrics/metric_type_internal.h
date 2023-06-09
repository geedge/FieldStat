#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct metrics *metric_hdr_create(const char *name, size_t name_len, const char * bins, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures);
int metric_hdr_add(struct metrics *hdr, long long value);
struct metrics *metrics_topk_deserialize(const char *blob, size_t blob_size);


// struct metric_counter *metric_counter_create();
// void metric_counter_add(struct metric_counter *counter, long long value);
// //TODO: 各自的deserialize

// void metric_topk_add(struct metrics *counter, const char *key, size_t key_len, unsigned value);
// struct metrics *metric_topk_create(int K);

// struct metrics *metric_unique_create(const char *name, size_t name_len, const char precision);
// int metric_unique_add(struct metrics *unique, const char *key, size_t keylen);

#ifdef __cplusplus
}
#endif