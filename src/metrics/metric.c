
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "metric.h"

#include "fieldstat.h"
#include "heavy_keeper.h"
#include "mpack/mpack.h"

struct metric_base {
    struct metrics scheme;
    struct fieldstat_tag *tag;
    size_t tag_num;
};

struct metric_topk {
    struct metric_base base;
    struct heavy_keeper *hk;
};

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

void metric_base_clear(struct metric_base *base)
{
    for (int i = 0; i < base->tag_num; i++) {
        fieldtag_clear(&base->tag[i]);
    }
    free(base->tag);
}

static void metric_topk_refresh(struct metrics *pthis)
{
    struct metric_topk *topk = (struct metric_topk *)pthis;
    heavy_keeper_clear(topk->hk);
}

static void metric_topk_destroy(struct metrics *pthis)
{
    struct metric_topk *topk = (struct metric_topk *)pthis;
    metric_base_clear(&topk->base);
    heavy_keeper_free(topk->hk);
    free(topk);
}

void fieldtag_copy(const struct fieldstat_tag *kv_pair, struct fieldstat_tag *dest)
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

static void metric_read_tag(const struct metrics *pthis, struct fieldstat_tag **tag_list, size_t *tag_count)
{
    struct metric_base *base = (struct metric_base *)pthis;

    *tag_list = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * base->tag_num);
    for (size_t i = 0; i < base->tag_num; i++) {
        fieldtag_copy(&base->tag[i], &(*tag_list)[i]);
    }

    *tag_count = base->tag_num;
}

static void metric_add_tag(struct metrics *pthis, const struct fieldstat_tag *tag_list, size_t tag_num)
{
    struct metric_base *base = (struct metric_base *)pthis;
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
    metric bin blob: 
    "tag_k":[<tags key string>]
    "tag_v":[<tags value string>]
    "blob":<blob>
    "blob_size":<blob_size>
    }
*/
void serialize_and_write_tag(mpack_writer_t *writer, const struct metrics *pthis)
{
    struct fieldstat_tag *tag_list;
    size_t tag_num;
    metric_read_tag(pthis, &tag_list, &tag_num);

    mpack_start_map(writer, 5);
    mpack_write_cstr(writer, "tag_k");
    mpack_start_array(writer, tag_num);
    for (int j = 0; j < tag_num; j++) {
        mpack_write_cstr(writer, tag_list[j].key);
    }
    mpack_finish_array(writer);

    mpack_write_cstr(writer, "tag_v");
    mpack_start_array(writer, tag_num);
    for (int j = 0; j < tag_num; j++) {
        mpack_write_cstr(writer, tag_list[j].value_str);
    }
    mpack_finish_array(writer);
}

void write_blob(mpack_writer_t *writer, const char *blob, size_t blob_size)
{
    mpack_write_cstr(writer, "blob");
    mpack_write_bin(writer, blob, blob_size);

    mpack_write_cstr(writer, "blob_size");
    mpack_write_i32(writer, blob_size);
}

void fieldtag_write_value_str(struct fieldstat_tag *kv_pair, const char *key, const char *value)
{
    kv_pair->key = strdup(key);
    kv_pair->type = TAG_CSTRING;
    kv_pair->value_str = strdup(value);
}

void deserialize_tag(mpack_node_t *root, struct fieldstat_tag **tag_list_out, size_t *tag_num_out)
{
    mpack_node_t tag_k_arr = mpack_node_map_cstr(*root, "tag_k");
    mpack_node_t tag_v_arr = mpack_node_map_cstr(*root, "tag_v");
    size_t tag_num = mpack_node_array_length(tag_k_arr);

    struct fieldstat_tag *tag_list = malloc(sizeof(struct fieldstat_tag) * tag_num);
    for (int j = 0; j < mpack_node_array_length(tag_k_arr); j++) {
        fieldtag_write_value_str(&tag_list[j], mpack_node_str(mpack_node_array_at(tag_k_arr, j)), mpack_node_str(mpack_node_array_at(tag_v_arr, j)));
        // TODO: 这里有两个问题，1. 同样的值，但是类型不同的tag应该算成一样的还是不一样的？tag的key起码应该不同吧，这个我直接写到接口里规定key应该互不相同
        // TODO: 第二个问题，要确认一下tag是字符串和数字是不是在功能上（比如查找）是等价的，或者把这两种tag塞到dynamic里能不能有同样的效果。
    }
    
    *tag_list_out = tag_list;
    *tag_num_out = tag_num;
}

static void metric_topk_serialize(const struct metrics *pthis, char **blob, size_t *blob_size)
{
    char *blob_content = NULL;
    size_t blob_size_content = 0;
    struct metric_topk *topk = (struct metric_topk *)pthis;
    heavy_keeper_serialization(topk->hk, &blob_content, &blob_size_content);

    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_size);

    mpack_build_map(&writer);
    serialize_and_write_tag(&writer, pthis);
    write_blob(&writer, blob_content, blob_size_content);
    mpack_finish_map(&writer);

    if (mpack_writer_destroy(&writer) != mpack_ok) {
        fprintf(stderr, "An error occurred in metric_topk_serialize!\n");
    }
}

static int metric_topk_merge(struct metrics *pthis, const struct metrics *from)
{
    struct metric_topk *topk = (struct metric_topk *)pthis;
    struct metric_topk *from_topk = (struct metric_topk *)from;
    return heavy_keeper_merge(topk->hk, from_topk->hk);
}

static void metric_topk_scheme_init(struct metrics *base);

static struct metrics * metric_topk_duplicate(const struct metrics *pthis)
{
    struct metric_topk *topk = (struct metric_topk *)pthis;
    struct metric_topk *new_topk = (struct metric_topk *)malloc(sizeof(struct metric_topk));
    new_topk->hk = heavy_keeper_new(topk->hk->K, NULL);
    heavy_keeper_merge(new_topk->hk, topk->hk);
    metric_topk_scheme_init((struct metrics *)new_topk);

    new_topk->base.tag_num = topk->base.tag_num;
    for (int i = 0; i < topk->base.tag_num; i++) {
        fieldtag_copy(&topk->base.tag[i], &new_topk->base.tag[i]);
    }
    return (struct metrics *)new_topk;
}

static void metric_topk_scheme_init(struct metrics *base)
{
    base->del = metric_topk_destroy;
    base->refresh = metric_topk_refresh;
    base->merge = metric_topk_merge;
    base->dup = metric_topk_duplicate;
    base->serialize = metric_topk_serialize;
    base->read_tag = metric_read_tag;
    base->add_tag = metric_add_tag;
}

struct metrics *metrics_topk_deserialize(const char *blob, size_t blob_size) 
{
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, blob_size);
    mpack_node_t root = mpack_tree_root(&tree);

    mpack_node_t content_blob = mpack_node_map_cstr(root, "blob");

    struct heavy_keeper *hk = heavy_keeper_deserialization(mpack_node_data(content_blob), mpack_node_data_len(content_blob));
    struct metric_topk *topk = (struct metric_topk *)malloc(sizeof(struct metric_topk));
    struct metrics *metric = (struct metrics *)topk;
    topk->hk = hk;
    metric_topk_scheme_init(metric);

    struct fieldstat_tag *tag_list = NULL;
    size_t tag_num = 0;
    deserialize_tag(&root, &tag_list, &tag_num);
    metric->add_tag(metric, tag_list, tag_num);

    mpack_tree_destroy(&tree);
    free(tag_list);

    return (struct metrics *)topk;
}

struct metrics *metric_topk_create(int K)
{
    struct metric_topk *topk = (struct metric_topk *)malloc(sizeof(struct metric_topk));
    topk->hk = heavy_keeper_new(K, NULL);

    struct metrics *base = (struct metrics *)topk;
    metric_topk_scheme_init(base);
    return base;
}

void metric_topk_add(struct metrics *pthis, const char *key, size_t key_len, unsigned value)
{
    struct metric_topk *topk = (struct metric_topk *)pthis;
    heavy_keeper_add(topk->hk, key, key_len, value);
}

// (*values_out)[i] is the value of cstring (*keys_out)[i]
typedef char * cstring;
void metric_topk_query(const struct metrics *pthis, cstring **keys_out, long long **values_out, size_t *num_out)
{
    struct metric_topk *topk = (struct metric_topk *)pthis;

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
