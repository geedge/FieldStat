#pragma once

#include <stddef.h>
#include "fieldstat.h"

struct metrics;
typedef void (*metrics_func_del)(struct metrics *);
typedef void (*metrics_func_refresh)(struct metrics *);
typedef int (*metrics_func_merge)(struct metrics *, const struct metrics *);
typedef struct metrics *(*metrics_func_dup)(const struct metrics *);
typedef void (*metrics_func_serialize)(const struct metrics *, char **/*blob*/, size_t */*blob_size*/);
typedef void (*metrics_func_read_tag)(const struct metrics *, struct fieldstat_tag **/*tag_list_out*/, size_t */*tag_num_out*/);
typedef void (*metrics_func_add_tag)(struct metrics *, const struct fieldstat_tag */*tag_list*/, size_t /*tag_num*/);
struct metrics {
    metrics_func_del del;
    metrics_func_refresh refresh;
    metrics_func_merge merge;
    metrics_func_dup dup;
    metrics_func_serialize serialize;
    metrics_func_read_tag read_tag;
    metrics_func_add_tag add_tag;
};


struct metrics *metric_topk_create(int K);
struct metrics *metrics_topk_deserialize(const char *blob, size_t blob_size);
void metric_topk_add(struct metrics *pthis, const char *key, size_t key_len, unsigned value);
void metric_topk_query(const struct metrics *pthis, char ***keys_out, long long **values_out, size_t *num_out);

// struct metric_counter *metric_counter_create();
// void metric_counter_add(struct metric_counter *counter, long long value);
// //TODO: 各自的deserialize


// struct metrics *metric_hdr_create(const char *name, size_t name_len, const char * bins, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures);
// int metric_hdr_add(struct metrics *hdr, long long value);


// struct metrics *metric_unique_create(const char *name, size_t name_len, const char precision);
// int metric_unique_add(struct metrics *unique, const char *key, size_t keylen);



    // TODO: metric 改名 cell