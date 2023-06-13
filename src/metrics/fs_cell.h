#pragma once

#include <stddef.h>
#include "fieldstat.h"

struct cell;
typedef void (*metrics_func_del)(struct cell *);
typedef void (*metrics_func_refresh)(struct cell *);
typedef int (*metrics_func_merge)(struct cell *, const struct cell *);
typedef struct cell *(*metrics_func_dup)(const struct cell *);
typedef void (*metrics_func_serialize)(const struct cell *, char **/*blob*/, size_t */*blob_size*/);
typedef void (*metrics_func_read_tag)(const struct cell *, struct fieldstat_tag **/*tag_list_out*/, size_t */*tag_num_out*/);
typedef void (*metrics_func_add_tag)(struct cell *, const struct fieldstat_tag */*tag_list*/, size_t /*tag_num*/);
struct cell {
    metrics_func_del del;
    metrics_func_refresh refresh;
    metrics_func_merge merge;
    metrics_func_dup dup;
    metrics_func_serialize serialize;
    metrics_func_read_tag read_tag;
    metrics_func_add_tag add_tag;
};


struct cell *cell_create_for_topk(int K);
struct cell *cell_deserialize_for_topk(const char *blob, size_t blob_size);
void cell_add_for_topk(struct cell *pthis, const char *key, size_t key_len, unsigned value);
void cell_query_for_topk(const struct cell *pthis, char ***keys_out, long long **values_out, size_t *num_out);

// struct metric_counter *metric_counter_create();
// void metric_counter_add(struct metric_counter *counter, long long value);
// //TODO: 各自的deserialize


// struct cell *metric_hdr_create(const char *name, size_t name_len, const char * bins, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures);
// int metric_hdr_add(struct cell *hdr, long long value);


// struct cell *metric_unique_create(const char *name, size_t name_len, const char precision);
// int metric_unique_add(struct cell *unique, const char *key, size_t keylen);



    // TODO: metric 改名 cell