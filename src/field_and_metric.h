
/* 
    * metrics
    * - metrics_func_del : free the metrics. 
    * - metrics_func_visit : Let the metric visit a serializer. The effect is to write the metrics data to the serializer.
    * - metrics_func_refresh : refresh the metrics. This function should not depend on the serializer anymore.
    * 
    * you should also write a metrics_new function to create a metrics, where realizations of the above functions are specified.
*/
#include "fieldstat.h"

struct fs_field;

typedef void (*metrics_func_del)(struct metrics *);
typedef void (*metrics_func_refresh)(struct metrics *);
typedef int (*metrics_func_visit)(struct metrics *, struct serializer *);
typedef int (*metrics_func_merge)(struct metrics *, struct metrics *);
struct metrics {
    metrics_func_del del;
    metrics_func_visit visit;
    metrics_func_refresh refresh;
    metrics_func_merge merge;

    size_t tag_num;
	char *tag_key[N_TAG_MAX];
	char *tag_value[N_TAG_MAX];

    struct fs_field *father;
};

struct metric_hash_node {
    char *key; // kv_pair of tags
    size_t key_len; // exclude '\0'
    struct metrics *metric;
    UT_hash_handle hh;
};


struct fs_field {
    enum field_type type;
    int field_id;
    char *name;
    size_t name_len; // exclude '\0'
    struct metric_hash_node *hash_tbl;
};

struct metrics *metric_hdr_create(const char *name, size_t name_len, const char * bins, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures);
int metric_hdr_add(struct metrics *hdr, long long value);

struct metric_counter *metric_counter_create();
void metric_counter_add(struct metric_counter *counter, long long value);

void metric_topk_add(struct metrics *counter, const char *key, size_t key_len, unsigned value);
struct metrics *metric_topk_create(const char *name, size_t name_len, int K);

struct metrics *metric_unique_create(const char *name, size_t name_len, const char precision);
int metric_unique_add(struct metrics *unique, const char *key, size_t keylen);

struct metrics *deserialize_for_metric_type(const char *data, size_t data_len, int type);