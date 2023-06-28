
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "fs_cell.h"

#include "fieldstat.h"
#include "heavy_keeper.h"
#include "hdr/hdr_histogram.h"
#include "hdr/hdr_histogram_log.h"
#include "st_hyperloglog.h"
#include "mpack/mpack.h"


struct metrics;
typedef void (*metrics_func_del)(struct metrics *);
typedef void (*metrics_func_refresh)(struct metrics *);
typedef int (*metrics_func_merge)(struct metrics *, const struct metrics *);
typedef int (*metrics_func_copy)(struct metrics *, const struct metrics *);
typedef void (*metrics_func_serialize)(const struct metrics *, char **/*blob*/, size_t */*blob_size*/);
typedef int (*metrics_func_deserialize)(struct metrics *, const char *, size_t );
struct metrics {
    metrics_func_del del;
    metrics_func_refresh refresh;
    metrics_func_merge merge;
    metrics_func_copy copy;
    metrics_func_serialize serialize;
    metrics_func_deserialize deserialize;

    enum field_type type;
};

struct metric_topk {
    struct metrics base;
    struct heavy_keeper *hk;
};

struct metric_counter {
    struct metrics base;
    long long value;
};

struct metric_cardinality {
    struct metrics base;
    struct ST_hyperloglog *hll;
};

struct metric_histogram {
    struct metrics base;
    struct hdr_histogram *hdr;
};

struct cell {
    struct fieldstat_tag *tag;
    size_t tag_num;
    struct metrics *metric;
};

static void fieldtag_clear(struct fieldstat_tag *kv_pair)
{
    if (kv_pair->key) {
        free((char *)kv_pair->key);
        kv_pair->key = NULL;
    }
    if (kv_pair->type == TAG_CSTRING && kv_pair->value_str) {
        free((char *)kv_pair->value_str);
        kv_pair->value_str = NULL;
    }
}

static void fieldtag_copy(const struct fieldstat_tag *kv_pair, struct fieldstat_tag *dest)
{
    dest->key = strdup(kv_pair->key);
    dest->type = kv_pair->type;
    switch(kv_pair->type)
    {
        case TAG_INTEGER:
            dest->value_int = kv_pair->value_int;
            break;
        case TAG_DOUBLE:
            dest->value_double = kv_pair->value_double;
            break;
        case TAG_CSTRING:
            dest->value_str = strdup(kv_pair->value_str);
            break;
        default:
            assert(0);
            break;
    }
}

/*
{
    "type":<enum field_type>,
    "PL":<content_blob>
}
*/
void mpack_blob_wrap_basic_info(enum field_type type, const char *blob, size_t blob_size, char **new_blob, size_t *new_blob_size)
{
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, new_blob, new_blob_size);
    mpack_start_map(&writer, 2);
    mpack_write_cstr(&writer, "type");
    mpack_write_int(&writer, type);
    mpack_write_cstr(&writer, "PL");
    mpack_write_bin(&writer, blob, blob_size);
    mpack_finish_map(&writer);
    mpack_writer_destroy(&writer);
}

// user must free new_blob
void mpack_blob_unpack_basic_info(const char *blob, size_t blob_size, enum field_type *type, char **new_blob, size_t *new_blob_size)
{
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, blob_size);
    mpack_tree_parse(&tree);
    mpack_node_t root = mpack_tree_root(&tree);

    *type = mpack_node_int(mpack_node_map_cstr(root, "type"));
    mpack_node_t payload_node = mpack_node_map_cstr(root, "PL");
    *new_blob_size = mpack_node_bin_size(payload_node);
    *new_blob = (char *)malloc(*new_blob_size);
    memcpy(*new_blob, mpack_node_bin_data(payload_node), *new_blob_size);

    mpack_tree_destroy(&tree);
}

/* 
    "tags":[{
        "key":<tag key string>
        "type":0 1 or 2
        "value":<tag value, can be int(0) double(1) or str(2)>
    }]
*/
void cell_serialize_write_fieldtag(mpack_writer_t *writer, const struct cell *pthis)
{
    struct fieldstat_tag *tag_list;
    size_t tag_num;
    cell_read_tag(pthis, &tag_list, &tag_num);

    printf("cell_serialize_write_fieldtag, after read tag, tag num: %zu\n", tag_num);

    mpack_write_cstr(writer, "tags");
    mpack_start_array(writer, tag_num);
    for (int j = 0; j < tag_num; j++) {
        printf("serialize tag: k:%s\n", tag_list[j].key);
        printf("serialize tag: type:%d\n", tag_list[j].type);
        mpack_start_map(writer, 3);
        mpack_write_cstr(writer, "key");
        mpack_write_cstr(writer, tag_list[j].key);
        mpack_write_cstr(writer, "type");
        mpack_write_i8(writer, tag_list[j].type);
        mpack_write_cstr(writer, "value");
        switch (tag_list[j].type)
        {
            case TAG_INTEGER:
                printf("write value int: %lld\n", tag_list[j].value_int);
                mpack_write_i64(writer, tag_list[j].value_int);
                break;
            case TAG_DOUBLE:
                printf("write value double: %lf\n", tag_list[j].value_double);

                mpack_write_double(writer, tag_list[j].value_double);
                break;
            case TAG_CSTRING:
                printf("write value str: %s\n", tag_list[j].value_str);
                mpack_write_cstr(writer, tag_list[j].value_str);
                break;
            default:
                assert(0);
                break;
        }
        mpack_finish_map(writer);
    }
    mpack_finish_array(writer);

    for (int j = 0; j < tag_num; j++) {
        fieldtag_clear(&tag_list[j]);
    }
    free(tag_list);
}

void deserialize_tag(mpack_node_t *root, struct fieldstat_tag **tag_list_out, size_t *tag_num_out)
{
    mpack_node_t tag_arr = mpack_node_map_cstr(*root, "tags");
    size_t tag_num = mpack_node_array_length(tag_arr);
    printf("deserialize_tag enter, tag num: %zu\n", tag_num);

    struct fieldstat_tag *tag_list = malloc(sizeof(struct fieldstat_tag) * tag_num);
    for (int j = 0; j < tag_num; j++) {
        printf("deserialize tag loop %d  1\n", j);
        mpack_node_t tag = mpack_node_array_at(tag_arr, j);
        printf("deserialize tag loop %d  1.000\n", j);

        mpack_node_t key = mpack_node_map_cstr(tag, "key");
        printf("deserialize tag loop %d  1.0\n", j);
        tag_list[j].key = (char *)malloc(mpack_node_strlen(key) + 1);
        printf("deserialize tag loop %d  1.1\n", j);
        mpack_node_copy_cstr(key, (char *)tag_list[j].key, mpack_node_strlen(key) + 1);
        printf("deserialize tag loop %d  1.2\n", j);
        tag_list[j].type = mpack_node_i8(mpack_node_map_cstr(tag, "type"));
        printf("deserialize tag loop %d  1.3\n", j);

        mpack_node_t value = mpack_node_map_cstr(tag, "value");
        printf("deserialize tag loop %d  2\n", j);

        switch (tag_list[j].type)
        {
            case TAG_INTEGER:
                printf("deserialize tag loop %d  3\n", j);

                tag_list[j].value_int = mpack_node_i64(value);
                break;
            case TAG_DOUBLE:
                printf("deserialize tag loop %d  4\n", j);

                tag_list[j].value_double = mpack_node_double(value);
                break;
            case TAG_CSTRING:
                printf("deserialize tag loop %d  5\n", j);

                tag_list[j].value_str = (char *)malloc(mpack_node_strlen(value) + 1);
                mpack_node_copy_cstr(value, (char *)tag_list[j].value_str, mpack_node_strlen(value) + 1);
                break;
            default:
                assert(0);
                break;
        }
    }
    printf("deserialize_tag exit\n");
    *tag_list_out = tag_list;
    *tag_num_out = tag_num;
}


/* -------------------------------------------------------------------------- */
/*                                    metric-topk                             */
/* -------------------------------------------------------------------------- */
static void metric_topk_serialize(const struct metrics *pthis, char **blob, size_t *blob_size)
{
    printf("metric_topk_serialize enter\n");

    struct metric_topk *topk = (struct metric_topk *)pthis;
    heavy_keeper_serialization(topk->hk, blob, blob_size);
}

static void metric_topk_refresh(struct metrics *pthis)
{
    struct metric_topk *topk = (struct metric_topk *)pthis;
    heavy_keeper_clear(topk->hk);
}

static int metric_topk_merge(struct metrics *pthis, const struct metrics *from)
{
    struct metric_topk *topk = (struct metric_topk *)pthis;
    struct metric_topk *from_topk = (struct metric_topk *)from;
    return heavy_keeper_merge(topk->hk, from_topk->hk);
}

static int metric_topk_copy(struct metrics *pthis, const struct metrics *from)
{
    struct metric_topk *topk = (struct metric_topk *)pthis;
    struct metric_topk *from_topk = (struct metric_topk *)from;
    if (topk->hk) {
        heavy_keeper_free(topk->hk);
    }
    topk->hk = heavy_keeper_new(topk->hk->K, NULL);

    return heavy_keeper_merge(topk->hk, from_topk->hk);
}

void metric_topk_free(struct metrics *pthis)
{
    struct metric_topk *topk = (struct metric_topk *)pthis;
    heavy_keeper_free(topk->hk);
    free(topk);
}

// blob: c_bin
int metric_topk_deserialize(struct metrics *pthis, const char *blob, size_t blob_size) 
{
    struct metric_topk *topk = (struct metric_topk *)pthis;

    struct heavy_keeper *hk = heavy_keeper_deserialization(blob, blob_size);
    if (hk == NULL) {
        return -1;
    }
    topk->hk = hk;

    return 0;
}

static struct metric_topk *metric_topk_new_scheme()
{
    struct metric_topk *topk = malloc(sizeof(struct metric_topk));
    struct metrics *base = &topk->base;
    base->del = metric_topk_free;
    base->refresh = metric_topk_refresh;
    base->merge = metric_topk_merge;
    base->copy = metric_topk_copy;
    base->serialize = metric_topk_serialize;
    base->deserialize = metric_topk_deserialize;
    base->type = FIELD_TYPE_TOPK;

    topk->hk = NULL;
    return topk;
}

/* -------------------------------------------------------------------------- */
/*                                    cell-topk                               */
/* -------------------------------------------------------------------------- */

struct cell *cell_create_for_topk(int K)
{
    struct cell *cell = (struct cell *)malloc(sizeof(struct cell));
    memset(cell, 0, sizeof(struct cell));
    struct metric_topk *topk = metric_topk_new_scheme();
    cell->metric = (struct metrics *)topk;

    topk->hk = heavy_keeper_new(K, NULL);

    return cell;
}

void cell_topk_add(struct cell *pthis, const char *key, size_t key_len, unsigned value)
{
    printf("cell_topk_add enter, add key: %s\n", key);
    struct metric_topk *topk = (struct metric_topk *)pthis->metric;
    heavy_keeper_add(topk->hk, key, key_len, value);
}

// (*values_out)[i] is the value of cstring (*keys_out)[i]
typedef char * cstring;
void cell_topk_query(const struct cell *pthis, cstring **keys_out, long long **values_out, size_t *num_out)
{
    struct metric_topk *topk = (struct metric_topk *)pthis->metric;

    struct heavy_keeper_result *result = heavy_keeper_query(topk->hk);
    *num_out = result->n_key;
    *keys_out = (cstring *)malloc(sizeof(cstring) * result->n_key);
    *values_out = (long long *)malloc(sizeof(long long) * result->n_key);
    for (int i = 0; i < result->n_key; i++) {
        (*keys_out)[i] = (cstring)strdup(result->key[i]);
        (*values_out)[i] = result->count[i];
    }
    heavy_keeper_result_free(result);
}

/* -------------------------------------------------------------------------- */
/*                                   metric-counter                           */
/* -------------------------------------------------------------------------- */

static void metric_counter_refresh(struct metrics *pthis)
{
    struct metric_counter *counter = (struct metric_counter *)pthis;
    counter->value = 0;
}

static void metric_counter_free(struct metrics *pthis)
{
    struct metric_counter *counter = (struct metric_counter *)pthis;
    free(counter);
}

/*
    "counter": {
        "value": 0
    }
*/
static void metric_counter_serialize(const struct metrics *pthis, char **blob, size_t *blob_size)
{
    struct metric_counter *counter = (struct metric_counter *)pthis;
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_size);
    mpack_build_map(&writer);

    mpack_write_cstr(&writer, "value");
    printf("metric_counter_serialize counter->value: %lld\n", counter->value);
    mpack_write_i64(&writer, counter->value);

    mpack_complete_map(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        fprintf(stderr, "An error occurred in counter_metric_serialize!\n");
    }
}

static int metric_counter_merge(struct metrics *pthis, const struct metrics *from)
{
    struct metric_counter *counter = (struct metric_counter *)pthis;
    struct metric_counter *from_counter = (struct metric_counter *)from;
    counter->value += from_counter->value;
    return 0;
}

static int metric_counter_copy(struct metrics *pthis, const struct metrics *from)
{
    struct metric_counter *counter = (struct metric_counter *)pthis;
    struct metric_counter *from_counter = (struct metric_counter *)from;
    counter->value = from_counter->value;
    return 0;
}

int metric_counter_deserialize(struct metrics *pthis, const char *blob, size_t blob_size) 
{
    mpack_tree_t content_tree;
    mpack_tree_init_data(&content_tree, blob, blob_size);
    mpack_tree_parse(&content_tree);
    mpack_node_t content_root = mpack_tree_root(&content_tree);

    struct metric_counter *counter = (struct metric_counter *)pthis;
    counter->value = mpack_node_i64(mpack_node_map_cstr(content_root, "value"));
    printf("counter->value: %lld\n", counter->value);

    mpack_tree_destroy(&content_tree);
    return 0;
}

static struct metric_counter *metric_counter_new_scheme()
{
    struct metric_counter *counter = malloc(sizeof(struct metric_counter));
    struct metrics *base = &counter->base;
    base->del = metric_counter_free;
    base->refresh = metric_counter_refresh;
    base->merge = metric_counter_merge;
    base->copy = metric_counter_copy;
    base->serialize = metric_counter_serialize;
    base->deserialize = metric_counter_deserialize;
    base->type = FIELD_TYPE_COUNTER;

    counter->value = 0;
    return counter;
}

/* -------------------------------------------------------------------------- */
/*                                cell-counter                                */
/* -------------------------------------------------------------------------- */

void cell_counter_incrby(struct cell *pthis, long long value)
{
    struct metric_counter *counter = (struct metric_counter *)pthis->metric;
    counter->value += value;
}

long long cell_counter_get(const struct cell *pthis)
{
    struct metric_counter *counter = (struct metric_counter *)pthis->metric;
    return counter->value;
}

struct cell *cell_create_for_counter()
{
    struct metric_counter *counter = metric_counter_new_scheme();
    struct cell *cell = malloc(sizeof(struct cell));
    memset(cell, 0, sizeof(struct cell));
    cell->metric = (struct metrics *)counter;
    return cell;
}

/* -------------------------------------------------------------------------- */
/*                                 metric-cardinality                         */
/* -------------------------------------------------------------------------- */

static int metric_cardinality_copy(struct metrics *pthis, const struct metrics *from)
{
    struct metric_cardinality *cardinality = (struct metric_cardinality *)pthis;
    struct metric_cardinality *cardinality_from = (struct metric_cardinality *)from;
    cardinality->hll = ST_hyperloglog_new(cardinality_from->hll->cfg.precision);
    return ST_hyperloglog_merge(cardinality->hll, cardinality_from->hll);
}

int metric_cardinality_deserialize(struct metrics *pthis, const char *blob, size_t blob_size) 
{
    struct metric_cardinality *cardinality = (struct metric_cardinality *)pthis;
    cardinality->hll = ST_hyperloglog_deserialize(blob, blob_size);

    return 0;
}

static void metric_cardinality_refresh(struct metrics *pthis)
{
    struct metric_cardinality *cardinality = (struct metric_cardinality *)pthis;
    ST_hyperloglog_reset(cardinality->hll);
}

static void metric_cardinality_free(struct metrics *pthis)
{
    struct metric_cardinality *cardinality = (struct metric_cardinality *)pthis;
    ST_hyperloglog_free(cardinality->hll);
    free(cardinality);
}

static void metric_cardinality_serialize(const struct metrics *pthis, char **blob, size_t *blob_size)
{
    struct metric_cardinality *cardinality = (struct metric_cardinality *)pthis;
    ST_hyperloglog_serialize(cardinality->hll, blob, blob_size);
}

static int metric_cardinality_merge(struct metrics *pthis, const struct metrics *from)
{
    struct metric_cardinality *cardinality = (struct metric_cardinality *)pthis;
    struct metric_cardinality *from_cardinality = (struct metric_cardinality *)from;
    return ST_hyperloglog_merge(cardinality->hll, from_cardinality->hll);
}

static struct metric_cardinality *metric_cardinality_new_scheme()
{
    struct metric_cardinality *cardinality = malloc(sizeof(struct metric_cardinality));
    struct metrics *base = &cardinality->base;
    base->del = metric_cardinality_free;
    base->refresh = metric_cardinality_refresh;
    base->merge = metric_cardinality_merge;
    base->copy = metric_cardinality_copy;
    base->serialize = metric_cardinality_serialize;
    base->deserialize = metric_cardinality_deserialize;
    base->type = FIELD_TYPE_CARDINALITY;

    return cardinality;
}

/* -------------------------------------------------------------------------- */
/*                              cell-cardinality                              */
/* -------------------------------------------------------------------------- */

struct cell *cell_create_for_cardinality(const char precision)
{
    struct cell *cell = (struct cell *)malloc(sizeof(struct cell));
    memset(cell, 0, sizeof(struct cell));

    struct metric_cardinality *cardinality = metric_cardinality_new_scheme();
    cardinality->hll = ST_hyperloglog_new(precision);

    cell->metric = (struct metrics *)cardinality;

    return cell;
}

int cell_cardinality_add(struct cell *pthis, const char *key, size_t key_len)
{
    struct metric_cardinality *cardinality = (struct metric_cardinality *)pthis->metric;
    return ST_hyperloglog_add(cardinality->hll, key, key_len);
}

double cell_cardinality_get(const struct cell *pthis)
{
    struct metric_cardinality *cardinality = (struct metric_cardinality *)pthis->metric;
    return ST_hyperloglog_count(cardinality->hll);
}

/* -------------------------------------------------------------------------- */
/*                              metric-histogram                              */
/* -------------------------------------------------------------------------- */
static void metric_histogram_refresh(struct metrics *pthis)
{
    struct metric_histogram *histogram = (struct metric_histogram *)pthis;
    hdr_reset(histogram->hdr);
}

static void metric_histogram_free(struct metrics *pthis)
{
    struct metric_histogram *histogram = (struct metric_histogram *)pthis;
    hdr_close(histogram->hdr);
    free(histogram);
}

static void metric_histogram_serialize(const struct metrics *pthis, char **blob, size_t *blob_size)
{
    struct metric_histogram *histogram = (struct metric_histogram *)pthis;
    hdr_log_encode(histogram->hdr, blob);
    *blob_size = strlen(*blob);
}

static int metric_histogram_merge(struct metrics *pthis, const struct metrics *from)
{
    struct metric_histogram *histogram = (struct metric_histogram *)pthis;
    struct metric_histogram *from_histogram = (struct metric_histogram *)from;
    return hdr_add(histogram->hdr, from_histogram->hdr);
}

static int metric_histogram_copy(struct metrics *pthis, const struct metrics *from)
{
    struct metric_histogram *histogram = (struct metric_histogram *)pthis;
    struct metric_histogram *from_histogram = (struct metric_histogram *)from;
    histogram->hdr = NULL;
    int ret1 = hdr_init(from_histogram->hdr->lowest_discernible_value, from_histogram->hdr->highest_trackable_value, from_histogram->hdr->significant_figures, &histogram->hdr);
    if (ret1 != 0) {
        return -1;
    }
    long ret = hdr_add(histogram->hdr, from_histogram->hdr);
    if (ret != 0) {
        return -1;
    }
    return 0;
}

static int metric_histogram_deserialize(struct metrics *pthis, const char *blob, size_t blob_size)
{
    struct metric_histogram *histogram = (struct metric_histogram *)pthis;
    histogram->hdr = NULL;
    int ret = hdr_log_decode(&histogram->hdr, (char *)blob, blob_size); // still const, just to avoid warning
    if (ret != 0 || histogram->hdr == NULL) {
        printf("metric_histogram_deserialize failed\n");
        return -1;
    }
    return 0;
}

static struct metric_histogram *metric_histogram_new_scheme()
{
    struct metric_histogram *histogram = malloc(sizeof(struct metric_histogram));
    struct metrics *base = &histogram->base;
    base->del = metric_histogram_free;
    base->refresh = metric_histogram_refresh;
    base->merge = metric_histogram_merge;
    base->copy = metric_histogram_copy;
    base->serialize = metric_histogram_serialize;
    base->deserialize = metric_histogram_deserialize;
    base->type = FIELD_TYPE_HISTOGRAM;

    return histogram;
}

/* -------------------------------------------------------------------------- */
/*                               cell-histogram                               */
/* -------------------------------------------------------------------------- */

struct cell *cell_create_for_histogram(long long lowest_trackable_value, long long highest_trackable_value, int significant_figures) 
{
    struct hdr_histogram* tmp_p_hdr = NULL;
    int ret = hdr_init((int64_t)lowest_trackable_value, (int64_t)highest_trackable_value, significant_figures, &tmp_p_hdr);
	if (ret != 0) {
        fprintf(stderr, "hdr_init failed!\n");
        return NULL;
    }

    struct metric_histogram *histogram = metric_histogram_new_scheme();
    histogram->hdr = tmp_p_hdr;

    struct cell *cell = (struct cell *)malloc(sizeof(struct cell));
    memset(cell, 0, sizeof(struct cell));
    cell->metric = (struct metrics *)histogram;

    return cell;
}

int cell_histogram_record(struct cell *pthis, long long value)
{
    struct metric_histogram *histogram = (struct metric_histogram *)pthis->metric;
    bool ret = hdr_record_value(histogram->hdr, value);
    if (ret == false) {
        return -1;
    }
    return 0;
}

long long cell_histogram_value_at_percentile(const struct cell *pthis, double percentile)
{
    struct metric_histogram *histogram = (struct metric_histogram *)pthis->metric;

    if (histogram->hdr == NULL) {
        printf("cell_histogram_value_at_percentile hdr is null\n");
    }
    return hdr_value_at_percentile(histogram->hdr, percentile);
}

/* -------------------------------------------------------------------------- */
/*                                   cell                                     */
/* -------------------------------------------------------------------------- */

static struct metrics *metric_scheme_new(enum field_type type)
{
    switch (type)
    {
        case FIELD_TYPE_TOPK:
            return (struct metrics *)metric_topk_new_scheme();
        case FIELD_TYPE_HISTOGRAM:
            return (struct metrics *)metric_histogram_new_scheme();
        case FIELD_TYPE_COUNTER:
            return (struct metrics *)metric_counter_new_scheme();
        case FIELD_TYPE_CARDINALITY:
            return (struct metrics *)metric_cardinality_new_scheme();
        default:
            assert(0);
            return NULL;
    }
}

void cell_read_tag(const struct cell *pthis, struct fieldstat_tag **tag_list, size_t *tag_count)
{
    *tag_list = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * pthis->tag_num);
    for (size_t i = 0; i < pthis->tag_num; i++) {
        fieldtag_copy(&pthis->tag[i], &(*tag_list)[i]);
    }

    *tag_count = pthis->tag_num;
}

void cell_add_tag(struct cell *pthis, const struct fieldstat_tag *tag_list, size_t tag_num)
{
    if (pthis->tag_num != 0) {
        for (size_t i = 0; i < pthis->tag_num; i++) {
            fieldtag_clear(&pthis->tag[i]);
        }
        free(pthis->tag);
    }
    
    pthis->tag_num = tag_num;
    pthis->tag = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * tag_num);
    
    for (size_t i = 0; i < tag_num; i++) {
        fieldtag_copy(&(tag_list)[i], &pthis->tag[i]);
    }
}


// TODO: 和 olap 的对齐，我估计最后就只提供一个content_blob了，这里要有什么东西？
/*
{
    cell bin blob: 
    "tags":[{
        "key":<tag key string>
        "type":0 1 or 2
        "value":<tag value, can be int(0) double(1) or str(2)>
    }]
    "c_bin":<wrapped content_blob(have type)>
    }
*/

void cell_serialize(const struct cell *pthis, char **blob, size_t *blob_size)
{
    printf("cell_serialize enter\n");

    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_size);
    mpack_build_map(&writer);

    // write tags
    cell_serialize_write_fieldtag(&writer, pthis);
    printf("write fieldtag done\n");

    // write content blob, wrap it with type info 
    char *blob_content = NULL;
    size_t blob_size_content = 0;
    pthis->metric->serialize(pthis->metric, &blob_content, &blob_size_content);
    printf("write blob, blob_size_content blob size: %zu\n", blob_size_content);
    char *wrapped_content = NULL;
    size_t wrapped_size = 0;
    mpack_blob_wrap_basic_info(pthis->metric->type, blob_content, blob_size_content, &wrapped_content, &wrapped_size);
    mpack_write_cstr(&writer, "c_bin");
    mpack_write_bin(&writer, wrapped_content, wrapped_size);

    mpack_complete_map(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        fprintf(stderr, "An error occurred in metric_topk_serialize!\n");
    }
    printf("cell_serialize done, blob size: %zu\n", *blob_size);
    free(blob_content);
    free(wrapped_content);
}

struct cell *cell_deserialize(const char *blob, size_t blob_size) 
{
    struct cell *cell = (struct cell *)malloc(sizeof(struct cell));
    memset(cell, 0, sizeof(struct cell));

    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, blob_size);
    mpack_tree_parse(&tree);
    mpack_node_t root = mpack_tree_root(&tree);
    
    struct fieldstat_tag *tag_list = NULL;
    size_t tag_num = 0;
    deserialize_tag(&root, &tag_list, &tag_num);
    cell_add_tag(cell, tag_list, tag_num);

    mpack_node_t wrapped_content_blob_node = mpack_node_map_cstr(root, "c_bin");

    enum field_type type;
    char *content_blob;
    size_t content_blob_size;
    mpack_blob_unpack_basic_info(mpack_node_bin_data(wrapped_content_blob_node), mpack_node_bin_size(wrapped_content_blob_node), &type, &content_blob, &content_blob_size);
    printf("cell_deserialize, content_blob_size: %zu\n", content_blob_size);
    struct metrics *metric = metric_scheme_new(type);
    cell->metric = metric;
    metric->deserialize(metric, content_blob, content_blob_size);

    mpack_tree_destroy(&tree);
    for (int i = 0; i < tag_num; i++) {
        fieldtag_clear(&tag_list[i]);
    }
    free(tag_list);
    free(content_blob);

    return cell;
}

void cell_free(struct cell *pthis)
{
    for (int i = 0; i < pthis->tag_num; i++) {
        fieldtag_clear(&pthis->tag[i]);
    }
    pthis->tag_num = 0;
    free(pthis->tag);

    pthis->metric->del(pthis->metric);

    free(pthis);
}

void cell_get_plain_blob(const struct cell *pthis, char **blob, size_t *blob_size) 
{
    if (pthis->metric->type == FIELD_TYPE_CARDINALITY) {
        struct metric_cardinality *card = (struct metric_cardinality *)pthis->metric;
        ST_hyperloglog_serialize_for_networking(card->hll, blob, blob_size);
        return;
    }

    pthis->metric->serialize(pthis->metric, blob, blob_size);
}

void cell_refresh(struct cell *pthis)
{
    pthis->metric->refresh(pthis->metric);
}

int cell_merge(struct cell *pthis, const struct cell *from)
{
    return pthis->metric->merge(pthis->metric, from->metric);
}

int cell_copy(struct cell *pthis, const struct cell *from)
{
    return pthis->metric->copy(pthis->metric, from->metric);
}

struct cell *cell_duplicate(const struct cell *pthis)
{
    struct cell *new_cell = (struct cell *)malloc(sizeof(struct cell));
    memset(new_cell, 0, sizeof(struct cell));
    new_cell->tag_num = pthis->tag_num;
    new_cell->tag = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * new_cell->tag_num);
    for (int i = 0; i < new_cell->tag_num; i++) {
        fieldtag_copy(&pthis->tag[i], &new_cell->tag[i]);
    }

    new_cell->metric = metric_scheme_new(pthis->metric->type);
    if (new_cell->metric == NULL) {
        fprintf(stderr, "cell_duplicate: metric_scheme_new failed\n");
        return NULL;
    }
    if (new_cell->metric->copy(new_cell->metric, pthis->metric) != 0) {
        fprintf(stderr, "cell_duplicate: metric copy failed\n");
        return NULL;
    }

    return new_cell;
}
