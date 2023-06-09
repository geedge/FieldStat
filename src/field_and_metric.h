#pragma once


/* 
    * metrics
    * - metrics_func_del : free the metrics. 
    * - metrics_func_visit : Let the metric visit a serializer. The effect is to write the metrics data to the serializer.
    * - metrics_func_refresh : refresh the metrics. This function should not depend on the serializer anymore.
    * 
    * you should also write a metrics_new function to create a metrics, where realizations of the above functions are specified.
*/
#include "fieldstat.h"
#include "uthash.h"

#ifdef __cplusplus
extern "C" {
#endif

struct fs_field;
struct metrics;
typedef void (*metrics_func_del)(struct metrics *);
typedef void (*metrics_func_refresh)(struct metrics *);
typedef int (*metrics_func_visit)(struct metrics *, struct serializer *);
typedef int (*metrics_func_merge)(struct metrics *, struct metrics *);
typedef struct metrics *(*metrics_func_dup)(struct metrics *);
struct metrics {
    metrics_func_del del;
    metrics_func_visit visit;
    metrics_func_refresh refresh;
    metrics_func_merge merge;
    metrics_func_dup dup;

    size_t tag_num;
	char *tag_key[MAX_TAG_NUM];
	char *tag_value[MAX_TAG_NUM];

    struct fs_field *container;
    enum field_type type;
};

struct metric_hash_node {
    char *key; // kv_pair of tags
    size_t key_len; // exclude '\0'
    struct metrics *metric;
    UT_hash_handle hh;
};

struct para_hdr{
    const char *bins;
    long long lowest_trackable_value;
    long long highest_trackable_value;
    int significant_figures;
};

struct para_topk {
    int K;
};

struct para_unique{
    const char precision;
};

struct field_paras {
    union {
        struct para_hdr hdr;
        struct para_topk topk;
        struct para_unique unique;
    } para;
};

struct fs_field {
    enum field_type type;
    int field_id;
    char *name;
    struct metric_hash_node *hash_tbl;
    struct field_paras *field_paras;
};

struct fs_field *field_new(enum field_type type, const char *name, struct field_paras *param);
void field_free(struct fs_field *field);
void insert_metric_to_field(struct fs_field *field, struct metrics *metric, const char *key, size_t key_len);
struct metrics *find_metric_in_field(struct fs_field *field, const char *key, size_t key_len);

int field_topk_add(struct fs_field *field, const char *key, size_t key_len, double score, const struct fieldstat_tag tags[], size_t n_tag);
// TODO: other type

struct metrics *deserialize_for_metric_type(const char *blob, size_t blob_size, int type);

#ifdef __cplusplus
}
#endif