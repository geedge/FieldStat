
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

struct cell_base {
    struct cell scheme;
    struct fieldstat_tag *tag;
    size_t tag_num;
};

#define CONTENT_BLOB_TAG "c_bin"

void fieldtag_clear(struct fieldstat_tag *kv_pair)
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

void cell_base_clear(struct cell_base *base)
{
    for (int i = 0; i < base->tag_num; i++) {
        fieldtag_clear(&base->tag[i]);
    }
    base->tag_num = 0;
    free(base->tag);
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

static void cell_read_tag(const struct cell *pthis, struct fieldstat_tag **tag_list, size_t *tag_count)
{
    struct cell_base *base = (struct cell_base *)pthis;

    *tag_list = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * base->tag_num);
    for (size_t i = 0; i < base->tag_num; i++) {
        printf("cell_read_tag enter, tag key: %s\n", base->tag[i].key);
        fieldtag_copy(&base->tag[i], &(*tag_list)[i]);
    }
    printf("cell_read_tag enter, tag num: %zu\n", base->tag_num);

    *tag_count = base->tag_num;
}

static void cell_add_tag(struct cell *pthis, const struct fieldstat_tag *tag_list, size_t tag_num)
{
    struct cell_base *base = (struct cell_base *)pthis;
    if (base->tag_num != 0) {
        for (size_t i = 0; i < base->tag_num; i++) {
            fieldtag_clear(&base->tag[i]);
        }
        free(base->tag);
    }
    
    base->tag_num = tag_num;
    base->tag = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * tag_num);
    
    for (size_t i = 0; i < tag_num; i++) {
        fieldtag_copy(&(tag_list)[i], &base->tag[i]);
    }
}

/*
{
    cell bin blob: 
    "tags":[{
        "key":<tag key string>
        "type":0 1 or 2
        "value":<tag value, can be int(0) double(1) or str(2)>
    }]
    "tag_v":[<tags value string>]
    "c_bin":<blob>
    }
*/
// TODO：把不同tag 的类型体现出来，以及deserialize的
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

void fieldtag_write_value_str(struct fieldstat_tag *kv_pair, const char *key, const char *value)
{
    kv_pair->key = strdup(key);
    kv_pair->type = TAG_CSTRING;
    kv_pair->value_str = strdup(value);
}

/* 
    "tags":[{
        "key":<tag key string>
        "type":0 1 or 2
        "value":<tag value, can be int(0) double(1) or str(2)>
    }]
*/
void deserialize_tag(mpack_node_t *root, struct fieldstat_tag **tag_list_out, size_t *tag_num_out)
{
    mpack_node_t tag_arr = mpack_node_map_cstr(*root, "tags");
    size_t tag_num = mpack_node_array_length(tag_arr);
    printf("deserialize_tag enter, tag num: %zu\n", tag_num);

    struct fieldstat_tag *tag_list = malloc(sizeof(struct fieldstat_tag) * tag_num);
    for (int j = 0; j < mpack_node_array_length(tag_arr); j++) {
        mpack_node_t tag = mpack_node_array_at(tag_arr, j);

        mpack_node_t key = mpack_node_map_cstr(tag, "key");
        tag_list[j].key = (char *)malloc(mpack_node_strlen(key) + 1);
        mpack_node_copy_cstr(key, (char *)tag_list[j].key, mpack_node_strlen(key) + 1);
        tag_list[j].type = mpack_node_i8(mpack_node_map_cstr(tag, "type"));
        mpack_node_t value = mpack_node_map_cstr(tag, "value");
        switch (tag_list[j].type)
        {
            case TAG_INTEGER:
                tag_list[j].value_int = mpack_node_i64(value);
                break;
            case TAG_DOUBLE:
                tag_list[j].value_double = mpack_node_double(value);
                break;
            case TAG_CSTRING:
                tag_list[j].value_str = (char *)malloc(mpack_node_strlen(value) + 1);
                mpack_node_copy_cstr(value, (char *)tag_list[j].value_str, mpack_node_strlen(value) + 1);
                break;
            default:
                assert(0);
                break;
        }
    }

    *tag_list_out = tag_list;
    *tag_num_out = tag_num;
}


/* -------------------------------------------------------------------------- */
/*                                    topk                                    */
/* -------------------------------------------------------------------------- */
struct cell_topk {
    struct cell_base base;
    struct heavy_keeper *hk;
};

static void cell_topk_refresh(struct cell *pthis)
{
    struct cell_topk *topk = (struct cell_topk *)pthis;
    heavy_keeper_clear(topk->hk);
}

static void cell_topk_free(struct cell *pthis)
{
    struct cell_topk *topk = (struct cell_topk *)pthis;
    cell_base_clear(&topk->base);
    heavy_keeper_free(topk->hk);
    free(topk);
}

static void cell_topk_serialize(const struct cell *pthis, char **blob, size_t *blob_size)
{
    printf("cell_topk_serialize enter\n");


    struct cell_topk *topk = (struct cell_topk *)pthis;
    // TODO: 这一段代码重复了，把4种type的提取出来

    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_size);

    mpack_build_map(&writer);
    cell_serialize_write_fieldtag(&writer, pthis);
    printf("write fieldtag done\n");

    char *blob_content = NULL;
    size_t blob_size_content = 0;
    heavy_keeper_serialization(topk->hk, &blob_content, &blob_size_content);
    printf("heavy_keeper_serialization done, blob_size_content len: %zu, bin: %s\n", blob_size_content, blob_content);
    mpack_write_cstr(&writer, CONTENT_BLOB_TAG);
    mpack_write_bin(&writer, blob_content, blob_size_content);
    printf("write blob done\n");
    mpack_complete_map(&writer);

    if (mpack_writer_destroy(&writer) != mpack_ok) {
        fprintf(stderr, "An error occurred in cell_topk_serialize!\n");
    }
    printf("cell_topk_serialize done, blob size: %zu\n", *blob_size);

    free(blob_content);
}

static int cell_topk_merge(struct cell *pthis, const struct cell *from)
{
    struct cell_topk *topk = (struct cell_topk *)pthis;
    struct cell_topk *from_topk = (struct cell_topk *)from;
    return heavy_keeper_merge(topk->hk, from_topk->hk);
}

static void cell_topk_scheme_init(struct cell *base);

static struct cell * cell_topk_duplicate(const struct cell *pthis)
{
    struct cell_topk *topk = (struct cell_topk *)pthis;
    struct cell_topk *new_topk = (struct cell_topk *)malloc(sizeof(struct cell_topk));
    memset(new_topk, 0, sizeof(struct cell_topk));

    new_topk->hk = heavy_keeper_new(topk->hk->K, NULL);
    heavy_keeper_merge(new_topk->hk, topk->hk);
    cell_topk_scheme_init((struct cell *)new_topk);

    new_topk->base.tag_num = topk->base.tag_num;
    for (int i = 0; i < topk->base.tag_num; i++) {
        fieldtag_copy(&topk->base.tag[i], &new_topk->base.tag[i]);
    }
    return (struct cell *)new_topk;
}

static void cell_topk_scheme_init(struct cell *base)
{
    base->del = cell_topk_free;
    base->refresh = cell_topk_refresh;
    base->merge = cell_topk_merge;
    base->dup = cell_topk_duplicate;
    base->serialize = cell_topk_serialize;
    base->read_tag = cell_read_tag;
    base->add_tag = cell_add_tag;
}

struct cell *cell_deserialize_for_topk(const char *blob, size_t blob_size) 
{
    printf("cell_deserialize_for_topk enter\n");
    struct cell_topk *topk = (struct cell_topk *)malloc(sizeof(struct cell_topk));
    memset(topk, 0, sizeof(struct cell_topk));
    struct cell *cell = (struct cell *)topk;
    cell_topk_scheme_init(cell);

    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, blob_size);
    mpack_tree_parse(&tree);
    mpack_node_t root = mpack_tree_root(&tree);

    struct fieldstat_tag *tag_list = NULL;
    size_t tag_num = 0;
    deserialize_tag(&root, &tag_list, &tag_num);
    printf("cell_deserialize_for_topk, after deserialize_tag, tag num: %zu\n", tag_num);
    cell->add_tag(cell, tag_list, tag_num);

    mpack_node_t content_blob = mpack_node_map_cstr(root, CONTENT_BLOB_TAG);
    printf("content blob: %s, size: %zu\n", mpack_node_bin_data(content_blob), mpack_node_bin_size(content_blob));
    struct heavy_keeper *hk = heavy_keeper_deserialization(mpack_node_bin_data(content_blob), mpack_node_bin_size(content_blob));
    topk->hk = hk;

    mpack_tree_destroy(&tree);
    for (int i = 0; i < tag_num; i++) {
        fieldtag_clear(&tag_list[i]);
    }
    free(tag_list);

    return (struct cell *)topk;
}

struct cell *cell_create_for_topk(int K)
{
    struct cell_topk *topk = (struct cell_topk *)malloc(sizeof(struct cell_topk));
    memset(topk, 0, sizeof(struct cell_topk));
    topk->hk = heavy_keeper_new(K, NULL);

    struct cell *base = (struct cell *)topk;
    cell_topk_scheme_init(base);
    return base;
}

void cell_oper_for_topk(struct cell *pthis, const char *key, size_t key_len, unsigned value)
{
    printf("cell_oper_for_topk enter, add key: %s\n", key);
    struct cell_topk *topk = (struct cell_topk *)pthis;
    heavy_keeper_add(topk->hk, key, key_len, value);
}

// (*values_out)[i] is the value of cstring (*keys_out)[i]
typedef char * cstring;
void cell_query_for_topk(const struct cell *pthis, cstring **keys_out, long long **values_out, size_t *num_out)
{
    struct cell_topk *topk = (struct cell_topk *)pthis;

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
/*                                   counter                                  */
/* -------------------------------------------------------------------------- */
struct cell_counter {
    struct cell_base base;
    long long value;
    // enum metric_counter_type type; // TODO: gauge
};

static void cell_counter_refresh(struct cell *pthis)
{
    struct cell_counter *counter = (struct cell_counter *)pthis;
    counter->value = 0;
}

static void cell_counter_free(struct cell *pthis)
{
    struct cell_counter *counter = (struct cell_counter *)pthis;
    cell_base_clear(&counter->base);
    free(counter);
}


// /*
//     "counter": {
//         "type":  0, // 0 for counter, 1 for gauge
//         "value": 0
//     }
// */
static void counter_metric_serialize(struct cell_counter *counter, char **blob, size_t *blob_size)
{
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_size);
    mpack_build_map(&writer);

    // TODO: gauge
    // mpack_write_cstr(&writer, "type");
    // mpack_write_i32(&writer, 0); 
    mpack_write_cstr(&writer, "value");
    mpack_write_i64(&writer, counter->value);

    mpack_complete_map(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        fprintf(stderr, "An error occurred in counter_metric_serialize!\n");
    }
}

static void cell_counter_serialize(const struct cell *pthis, char **blob, size_t *blob_size)
{
    printf("cell_counter_serialize enter\n");
    char *blob_content = NULL;
    size_t blob_size_content = 0;
    struct cell_counter *counter = (struct cell_counter *)pthis;
    counter_metric_serialize(counter, &blob_content, &blob_size_content);
    
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_size);
    mpack_build_map(&writer);
    cell_serialize_write_fieldtag(&writer, pthis);
    mpack_write_cstr(&writer, CONTENT_BLOB_TAG);
    mpack_write_bin(&writer, blob_content, blob_size_content);
    mpack_complete_map(&writer);
    free(blob_content);

    if (mpack_writer_destroy(&writer) != mpack_ok) {
        fprintf(stderr, "An error occurred in cell_counter_serialize!\n");
    }
}

static int cell_counter_merge(struct cell *pthis, const struct cell *from)
{
    struct cell_counter *counter = (struct cell_counter *)pthis;
    struct cell_counter *from_counter = (struct cell_counter *)from;
    counter->value += from_counter->value;
    return 0;
}

static void cell_counter_scheme_init(struct cell *base);

static struct cell * cell_counter_duplicate(const struct cell *pthis)
{
    struct cell_counter *counter = (struct cell_counter *)pthis;
    struct cell_counter *new_counter = (struct cell_counter *)malloc(sizeof(struct cell_counter));
    memset(new_counter, 0, sizeof(struct cell_counter));
    new_counter->value = counter->value;
    cell_counter_scheme_init((struct cell *)new_counter);

    new_counter->base.tag_num = counter->base.tag_num;
    for (int i = 0; i < counter->base.tag_num; i++) {
        fieldtag_copy(&counter->base.tag[i], &new_counter->base.tag[i]);
    }
    return (struct cell *)new_counter;
}

static void cell_counter_scheme_init(struct cell *base)
{
    base->del = cell_counter_free;
    base->refresh = cell_counter_refresh;
    base->merge = cell_counter_merge;
    base->dup = cell_counter_duplicate;
    base->serialize = cell_counter_serialize;
    base->read_tag = cell_read_tag;
    base->add_tag = cell_add_tag;
}

struct cell *cell_deserialize_for_counter(const char *blob, size_t blob_size) 
{
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, blob_size);
    mpack_tree_parse(&tree);
    mpack_node_t root = mpack_tree_root(&tree);

    struct cell_counter *counter = (struct cell_counter *)malloc(sizeof(struct cell_counter));
    memset(counter, 0, sizeof(struct cell_counter));
    struct cell *cell = (struct cell *)counter;
    cell_counter_scheme_init(cell);

    mpack_node_t content_blob = mpack_node_map_cstr(root, CONTENT_BLOB_TAG);
    mpack_tree_t content_tree;
    mpack_tree_init_data(&content_tree, mpack_node_bin_data(content_blob), mpack_node_bin_size(content_blob));
    mpack_tree_parse(&content_tree);
    mpack_node_t content_root = mpack_tree_root(&content_tree);
    counter->value = mpack_node_i64(mpack_node_map_cstr(content_root, "value"));
    printf("counter->value: %lld\n", counter->value);
    // TODO: gauge

    struct fieldstat_tag *tag_list = NULL;
    size_t tag_num = 0;
    deserialize_tag(&root, &tag_list, &tag_num);
    cell->add_tag(cell, tag_list, tag_num);

    mpack_tree_destroy(&tree);
    mpack_tree_destroy(&content_tree);
    for (int i = 0; i < tag_num; i++) {
        fieldtag_clear(&tag_list[i]);
    }
    free(tag_list);
    return (struct cell *)counter;
}

struct cell *cell_create_for_counter()
{
    struct cell_counter *counter = (struct cell_counter *)malloc(sizeof(struct cell_counter));
    memset(counter, 0, sizeof(struct cell_counter));

    struct cell *base = (struct cell *)counter;
    cell_counter_scheme_init(base);
    return base;
}

// TODO: name 应该是metric吧。说实话好想typedef一下
void cell_oper_for_counter(struct cell *pthis, long long value)
{
    struct cell_counter *counter = (struct cell_counter *)pthis;
    counter->value += value;
}

long long cell_query_for_counter(const struct cell *pthis)
{
    struct cell_counter *counter = (struct cell_counter *)pthis;
    return counter->value;
}

/* -------------------------------------------------------------------------- */
/*                                 hyperloglog                                */
/* -------------------------------------------------------------------------- */
struct metric_cardinality {
    struct cell_base base;
    struct ST_hyperloglog *hll;
};

static void metric_cardinality_refresh(struct cell *pthis)
{
    struct metric_cardinality *cardinality = (struct metric_cardinality *)pthis;
    ST_hyperloglog_reset(cardinality->hll);
}

static void metric_cardinality_free(struct cell *pthis)
{
    struct metric_cardinality *cardinality = (struct metric_cardinality *)pthis;
    cell_base_clear(&cardinality->base);
    ST_hyperloglog_free(cardinality->hll);
    free(cardinality);
}

static void metric_cardinality_serialize(const struct cell *pthis, char **blob, size_t *blob_size)
{
    char *blob_content = NULL;
    size_t blob_size_content = 0;
    struct metric_cardinality *cardinality = (struct metric_cardinality *)pthis;
    ST_hyperloglog_serialize(cardinality->hll, &blob_content, &blob_size_content); // TODO: 这个序列化感觉不行
    
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_size);
    mpack_build_map(&writer);
    cell_serialize_write_fieldtag(&writer, pthis);
    mpack_write_cstr(&writer, CONTENT_BLOB_TAG);
    mpack_write_bin(&writer, blob_content, blob_size_content);
    mpack_complete_map(&writer);
    free(blob_content);

    if (mpack_writer_destroy(&writer) != mpack_ok) {
        fprintf(stderr, "An error occurred in metric_cardinality_serialize!\n");
    }
}

static int metric_cardinality_merge(struct cell *pthis, const struct cell *from)
{
    struct metric_cardinality *cardinality = (struct metric_cardinality *)pthis;
    struct metric_cardinality *from_cardinality = (struct metric_cardinality *)from;
    return ST_hyperloglog_merge(cardinality->hll, from_cardinality->hll);
}

static void metric_cardinality_scheme_init(struct cell *base);

static struct cell * metric_cardinality_duplicate(const struct cell *pthis)
{
    struct metric_cardinality *cardinality = (struct metric_cardinality *)pthis;
    struct metric_cardinality *new_cardinality = (struct metric_cardinality *)malloc(sizeof(struct metric_cardinality));
    memset(new_cardinality, 0, sizeof(struct metric_cardinality));
    new_cardinality->hll = ST_hyperloglog_new(cardinality->hll->cfg.precision);
    ST_hyperloglog_merge(new_cardinality->hll, cardinality->hll);

    metric_cardinality_scheme_init((struct cell *)new_cardinality);

    new_cardinality->base.tag_num = cardinality->base.tag_num;
    for (int i = 0; i < cardinality->base.tag_num; i++) {
        fieldtag_copy(&cardinality->base.tag[i], &new_cardinality->base.tag[i]);
    }
    return (struct cell *)new_cardinality;
}

static void metric_cardinality_scheme_init(struct cell *base)
{
    base->del = metric_cardinality_free;
    base->refresh = metric_cardinality_refresh;
    base->merge = metric_cardinality_merge;
    base->dup = metric_cardinality_duplicate;
    base->serialize = metric_cardinality_serialize;
    base->read_tag = cell_read_tag;
    base->add_tag = cell_add_tag;
}

struct cell *cell_deserialize_for_cardinality(const char *blob, size_t blob_size) 
{
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, blob_size);
    mpack_tree_parse(&tree);
    mpack_node_t root = mpack_tree_root(&tree);

    mpack_node_t content_blob = mpack_node_map_cstr(root, CONTENT_BLOB_TAG);

    struct metric_cardinality *cardinality = (struct metric_cardinality *)malloc(sizeof(struct metric_cardinality));
    memset(cardinality, 0, sizeof(struct metric_cardinality));
    struct cell *cell = (struct cell *)cardinality;
    metric_cardinality_scheme_init(cell);

    cardinality->hll = ST_hyperloglog_deserialize(mpack_node_bin_data(content_blob), mpack_node_bin_size(content_blob));
    
    struct fieldstat_tag *tag_list = NULL;
    size_t tag_num = 0;
    deserialize_tag(&root, &tag_list, &tag_num);
    cell->add_tag(cell, tag_list, tag_num);

    mpack_tree_destroy(&tree);
    for (int i = 0; i < tag_num; i++) {
        fieldtag_clear(&tag_list[i]);
    }
    free(tag_list);

    return cell;
}

struct cell *cell_create_for_cardinality(const char precision)
{
    struct metric_cardinality *cardinality = (struct metric_cardinality *)malloc(sizeof(struct metric_cardinality));
    memset(cardinality, 0, sizeof(struct metric_cardinality));
    cardinality->hll = ST_hyperloglog_new(precision);

    struct cell *base = (struct cell *)cardinality;
    metric_cardinality_scheme_init(base);
    return base;
}

int cell_oper_for_cardinality(struct cell *pthis, const char *key, size_t key_len)
{
    struct metric_cardinality *cardinality = (struct metric_cardinality *)pthis;
    return ST_hyperloglog_add(cardinality->hll, key, key_len);
}

double cell_query_for_cardinality(const struct cell *pthis)
{
    struct metric_cardinality *cardinality = (struct metric_cardinality *)pthis;
    return ST_hyperloglog_count(cardinality->hll);
}


/* -------------------------------------------------------------------------- */
/*                                  histogram                                 */
/* -------------------------------------------------------------------------- */
struct metric_histogram {
    struct cell_base base;
    struct hdr_histogram *hdr;
    double *bins; // TODO：这个如果在序列化的时候带上还挺麻烦，一个方法是不带了，另一个是当成tag
};

static void metric_histogram_refresh(struct cell *pthis)
{
    struct metric_histogram *histogram = (struct metric_histogram *)pthis;
    hdr_reset(histogram->hdr);
}

static void metric_histogram_free(struct cell *pthis)
{
    struct metric_histogram *histogram = (struct metric_histogram *)pthis;
    cell_base_clear(&histogram->base);
    hdr_close(histogram->hdr);
    free(histogram);
}

static void metric_histogram_serialize(const struct cell *pthis, char **blob, size_t *blob_size)
{
    char *blob_content = NULL;
    struct metric_histogram *histogram = (struct metric_histogram *)pthis;
    hdr_log_encode(histogram->hdr, &blob_content);
    size_t blob_size_content = strlen(blob_content);

    printf("metric_histogram_serialize the blob is : %s, size is %zu\n", blob_content, blob_size_content);

    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_size);
    mpack_build_map(&writer);
    cell_serialize_write_fieldtag(&writer, pthis);
    mpack_write_cstr(&writer, CONTENT_BLOB_TAG);
    mpack_write_bin(&writer, blob_content, blob_size_content);
    free(blob_content);
    mpack_complete_map(&writer);

    if (mpack_writer_destroy(&writer) != mpack_ok) {
        fprintf(stderr, "An error occurred in metric_histogram_serialize!\n");
    }
}

static int metric_histogram_merge(struct cell *pthis, const struct cell *from)
{
    struct metric_histogram *histogram = (struct metric_histogram *)pthis;
    struct metric_histogram *from_histogram = (struct metric_histogram *)from;
    return hdr_add(histogram->hdr, from_histogram->hdr);
}

static void metric_histogram_scheme_init(struct cell *base);

static struct cell * metric_histogram_duplicate(const struct cell *pthis)
{
    struct metric_histogram *histogram = (struct metric_histogram *)pthis;
    struct metric_histogram *new_histogram = (struct metric_histogram *)malloc(sizeof(struct metric_histogram));
    memset(new_histogram, 0, sizeof(struct metric_histogram));
    hdr_alloc(histogram->hdr->highest_trackable_value, histogram->hdr->significant_figures, &new_histogram->hdr);
    hdr_add(new_histogram->hdr, histogram->hdr);
    // TODO: copy bins

    metric_histogram_scheme_init((struct cell *)new_histogram);

    new_histogram->base.tag_num = histogram->base.tag_num;
    for (int i = 0; i < histogram->base.tag_num; i++) {
        fieldtag_copy(&histogram->base.tag[i], &new_histogram->base.tag[i]);
    }
    return (struct cell *)new_histogram;
}

static void metric_histogram_scheme_init(struct cell *base)
{
    base->del = metric_histogram_free;
    base->refresh = metric_histogram_refresh;
    base->merge = metric_histogram_merge;
    base->dup = metric_histogram_duplicate;
    base->serialize = metric_histogram_serialize;
    base->read_tag = cell_read_tag;
    base->add_tag = cell_add_tag;
}

struct cell *cell_deserialize_for_histogram(const char *blob, size_t blob_size) 
{
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, blob_size);
    mpack_tree_parse(&tree);
    mpack_node_t root = mpack_tree_root(&tree);

    mpack_node_t content_blob = mpack_node_map_cstr(root, CONTENT_BLOB_TAG);

    struct metric_histogram *histogram = (struct metric_histogram *)malloc(sizeof(struct metric_histogram));
    memset(histogram, 0, sizeof(struct metric_histogram));
    struct cell *cell = (struct cell *)histogram;
    metric_histogram_scheme_init(cell);

    // TODO: 这个地方可能有问题
    int ret = hdr_log_decode(&histogram->hdr, (char *)mpack_node_bin_data(content_blob), mpack_node_bin_size(content_blob));
    if (ret != 0) {
        fprintf(stderr, "hdr_log_decode failed!\n");
        return NULL;
    }
    printf("cell_deserialize_for_histogram hdr->total_count: %ld\n", histogram->hdr->total_count);
    
    struct fieldstat_tag *tag_list = NULL;
    size_t tag_num = 0;
    deserialize_tag(&root, &tag_list, &tag_num);
    cell->add_tag(cell, tag_list, tag_num);

    mpack_tree_destroy(&tree);
    for (int i = 0; i < tag_num; i++) {
        fieldtag_clear(&tag_list[i]);
    }
    free(tag_list);

    return cell;
}

struct cell *cell_create_for_histogram(long long lowest_trackable_value, long long highest_trackable_value, int significant_figures) 
// TODO: bin
{
    struct hdr_histogram* tmp_p_hdr = NULL;
    int ret = hdr_init((int64_t)lowest_trackable_value, (int64_t)highest_trackable_value, significant_figures, &tmp_p_hdr);
	if (ret != 0) {
        fprintf(stderr, "hdr_init failed!\n");
        return NULL;
    }

    struct metric_histogram *histogram = (struct metric_histogram *)malloc(sizeof(struct metric_histogram));
    memset(histogram, 0, sizeof(struct metric_histogram));
    histogram->hdr = tmp_p_hdr;

    struct cell *base = (struct cell *)histogram;
    metric_histogram_scheme_init(base);
    return base;
}

int cell_oper_for_histogram(struct cell *pthis, long long value)
{
    struct metric_histogram *histogram = (struct metric_histogram *)pthis;
    bool ret = hdr_record_value(histogram->hdr, value);
    if (ret == false) {
        return -1;
    }
    return 0;
}

long long cell_query_for_histogram(const struct cell *pthis, double percentile)
{
    struct metric_histogram *histogram = (struct metric_histogram *)pthis;
    return hdr_value_at_percentile(histogram->hdr, percentile);
}