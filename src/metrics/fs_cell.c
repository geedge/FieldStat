
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "fs_cell.h"

#include "fieldstat.h"
#include "heavy_keeper.h"
#include "mpack/mpack.h"

struct cell_base {
    struct cell scheme;
    struct fieldstat_tag *tag;
    size_t tag_num;
};

struct cell_topk {
    struct cell_base base;
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

void cell_base_clear(struct cell_base *base)
{
    for (int i = 0; i < base->tag_num; i++) {
        fieldtag_clear(&base->tag[i]);
    }
    free(base->tag);
}

static void cell_topk_refresh(struct cell *pthis)
{
    struct cell_topk *topk = (struct cell_topk *)pthis;
    heavy_keeper_clear(topk->hk);
}

static void cell_topk_destroy(struct cell *pthis)
{
    struct cell_topk *topk = (struct cell_topk *)pthis;
    cell_base_clear(&topk->base);
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

static void cell_read_tag(const struct cell *pthis, struct fieldstat_tag **tag_list, size_t *tag_count)
{
    struct cell_base *base = (struct cell_base *)pthis;

    *tag_list = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * base->tag_num);
    for (size_t i = 0; i < base->tag_num; i++) {
        fieldtag_copy(&base->tag[i], &(*tag_list)[i]);
    }

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
    "tag_k":[<tags key string>]
    "tag_v":[<tags value string>]
    "blob":<blob>
    "blob_size":<blob_size>
    }
*/
// TODO：把不同tag 的类型体现出来，以及deserialize的
void fieldtag_parse_and_write(mpack_writer_t *writer, const struct cell *pthis)
{
    struct fieldstat_tag *tag_list;
    size_t tag_num;
    cell_read_tag(pthis, &tag_list, &tag_num);

    mpack_start_map(writer, 5);
    mpack_write_cstr(writer, "tag_k");
    mpack_start_array(writer, tag_num);
    for (int j = 0; j < tag_num; j++) {
        mpack_write_cstr(writer, tag_list[j].key);
    }
    mpack_finish_array(writer);

    // fixme: 这里可能是其他类型的tag
    mpack_write_cstr(writer, "tag_v");
    mpack_start_array(writer, tag_num);
    for (int j = 0; j < tag_num; j++) {
        mpack_write_cstr(writer, tag_list[j].value_str);
    }
    mpack_finish_array(writer);
}

void parse_and_write_blob(mpack_writer_t *writer, const char *blob, size_t blob_size)
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

static void cell_topk_serialize(const struct cell *pthis, char **blob, size_t *blob_size)
{
    char *blob_content = NULL;
    size_t blob_size_content = 0;
    struct cell_topk *topk = (struct cell_topk *)pthis;
    heavy_keeper_serialization(topk->hk, &blob_content, &blob_size_content);

    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_size);

    mpack_build_map(&writer);
    fieldtag_parse_and_write(&writer, pthis);
    parse_and_write_blob(&writer, blob_content, blob_size_content);
    mpack_finish_map(&writer);

    if (mpack_writer_destroy(&writer) != mpack_ok) {
        fprintf(stderr, "An error occurred in cell_topk_serialize!\n");
    }
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
    base->del = cell_topk_destroy;
    base->refresh = cell_topk_refresh;
    base->merge = cell_topk_merge;
    base->dup = cell_topk_duplicate;
    base->serialize = cell_topk_serialize;
    base->read_tag = cell_read_tag;
    base->add_tag = cell_add_tag;
}

struct cell *cell_deserialize_for_topk(const char *blob, size_t blob_size) 
{
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, blob_size);
    mpack_node_t root = mpack_tree_root(&tree);

    mpack_node_t content_blob = mpack_node_map_cstr(root, "blob");

    struct heavy_keeper *hk = heavy_keeper_deserialization(mpack_node_data(content_blob), mpack_node_data_len(content_blob));
    struct cell_topk *topk = (struct cell_topk *)malloc(sizeof(struct cell_topk));
    struct cell *cell = (struct cell *)topk;
    topk->hk = hk;
    cell_topk_scheme_init(cell);

    struct fieldstat_tag *tag_list = NULL;
    size_t tag_num = 0;
    deserialize_tag(&root, &tag_list, &tag_num);
    cell->add_tag(cell, tag_list, tag_num);

    mpack_tree_destroy(&tree);
    free(tag_list);

    return (struct cell *)topk;
}

struct cell *cell_create_for_topk(int K)
{
    struct cell_topk *topk = (struct cell_topk *)malloc(sizeof(struct cell_topk));
    topk->hk = heavy_keeper_new(K, NULL);

    struct cell *base = (struct cell *)topk;
    cell_topk_scheme_init(base);
    return base;
}

void cell_add_for_topk(struct cell *pthis, const char *key, size_t key_len, unsigned value)
{
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
