#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "mpack/mpack.h"
#include "my_ut_hash_inner.h"
#include "my_ut_hash.h"
#include "tag_id_map.h"

#define USING_BASE_HASH HASH_JEN

int simple_hash_cnt = 0;
int hash_cnt = 0;

struct my_hash_tag {
    char *key;
    size_t key_len; // including '\0'
    unsigned char *value;
    size_t value_len; // sizeof(value) if INT or DOUBLE, strlen(value) + 1 if CSTRING

    enum fs_tag_type tag_type; // not used for hash.
};

struct tag_hash_key {
    struct my_hash_tag *my_tag;
    size_t n_my_tag;
    bool is_hashed;
    unsigned int hashv;
};

void my_hash_key_list_add(struct tag_id_map **tbl, struct tag_id_map *node)
{
    HASH_ADD_TAG(*tbl, tag, node);
}

struct tag_id_map *my_hash_key_list_find(const struct tag_id_map *tbl, const struct tag_hash_key *my_hash_key)
{
    struct tag_id_map *node = NULL;
    HASH_FIND_TAG(tbl, my_hash_key, node);
    return node;
}

unsigned int tag_hash_key_cal_hash_value(struct tag_hash_key *my_hash_key)
{
    if (my_hash_key->is_hashed) {
        simple_hash_cnt++;
        return my_hash_key->hashv;
    }
    hash_cnt++;
    unsigned int hashv = 0;
    unsigned int result = 0;
    struct my_hash_tag *tmp_pst_hashed_tag = my_hash_key->my_tag;
    for (int i = 0; i < my_hash_key->n_my_tag; i++)
    {
        USING_BASE_HASH(tmp_pst_hashed_tag->key, tmp_pst_hashed_tag->key_len, hashv);
        result = hashv;
        USING_BASE_HASH(tmp_pst_hashed_tag->value, tmp_pst_hashed_tag->value_len, hashv);
        result += hashv;

        tmp_pst_hashed_tag++;
    }
    my_hash_key->hashv = result;
    my_hash_key->is_hashed = true;
    return result;
}

int tag_hash_key_cmp(const struct tag_hash_key *a, const struct tag_hash_key *b)
{
    if (a->is_hashed && b->is_hashed) {
        if (a->hashv != b->hashv) {
            return 1;
        }
        // if equal, must continue to compare, because hash collision may happen.
    }

    if (a->n_my_tag != b->n_my_tag) {
        return 1;
    }
    struct my_hash_tag *a_tag = a->my_tag;
    struct my_hash_tag *b_tag = b->my_tag;
    for (int i = 0; i < a->n_my_tag; i++) {
        if (a_tag->key_len != b_tag->key_len) {
            return 1;
        }
        if (memcmp(a_tag->key, b_tag->key, a_tag->key_len) != 0) {
            return 1;
        }
        if (a_tag->value_len != b_tag->value_len) {
            return 1;
        }
        if (memcmp(a_tag->value, b_tag->value, a_tag->value_len) != 0) {
            return 1;
        }
        a_tag++;
        b_tag++;
    }
    return 0;
}

void convert_fieldstat_tag_to_my_hash_tag(const struct fieldstat_tag *src, struct my_hash_tag *dst)
{
    dst->tag_type = src->type;
    size_t key_len = strlen(src->key) + 1;
    dst->key = (char *)malloc(key_len);
    memcpy(dst->key, src->key, key_len);
    dst->key_len = key_len;
    unsigned char *value = NULL;
    switch (src->type)
    {
    case TAG_INTEGER:
        value = (unsigned char *)malloc(sizeof(long long));
        memcpy(value, &(src->value_longlong), sizeof(long long));
        dst->value_len = sizeof(long long);
        break;
    case TAG_DOUBLE:
        value = (unsigned char *)malloc(sizeof(double));
        memcpy(value, &(src->value_double), sizeof(double));
        dst->value_len = sizeof(double);
        break;
    case TAG_CSTRING:{
        size_t value_len = strlen(src->value_str) + 1;
        value = (unsigned char *)malloc(value_len);
        memcpy(value, src->value_str, value_len);
        dst->value_len = value_len;
        break;
    }
    default:
        break;
    }
    dst->value = value;
}

struct tag_hash_key *tag_hash_key_construct_with_fieldstat_tag(const struct fieldstat_tag *src, size_t n_src)
{
    struct my_hash_tag *my_hash_tag_arr = (struct my_hash_tag *)malloc(sizeof(struct my_hash_tag) * n_src);
    for (int i = 0; i < n_src; i++) {
        convert_fieldstat_tag_to_my_hash_tag(src + i, my_hash_tag_arr + i);
    }

    struct tag_hash_key *my_hash_key = (struct tag_hash_key *)malloc(sizeof(struct tag_hash_key));
    my_hash_key->my_tag = my_hash_tag_arr;
    my_hash_key->n_my_tag = n_src;
    my_hash_key->hashv = 0;
    my_hash_key->is_hashed = false;
    return my_hash_key;
}

void tag_hash_key_convert_to_fieldstat_tag(const struct tag_hash_key *tag_key, struct fieldstat_tag **out, size_t *n_out)
{
    size_t ret_n_tag = tag_key->n_my_tag;
    *n_out = ret_n_tag;
    struct fieldstat_tag *ret_fieldstat_tag = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * ret_n_tag);
    *out = ret_fieldstat_tag;

    struct my_hash_tag *tmp_pst_my_hash_tag = tag_key->my_tag;
    for (int i = 0; i < ret_n_tag; i++)
    {
        ret_fieldstat_tag->key = (char *)malloc(tmp_pst_my_hash_tag->key_len);
        memcpy((char *)ret_fieldstat_tag->key, tmp_pst_my_hash_tag->key, tmp_pst_my_hash_tag->key_len);

        ret_fieldstat_tag->type = tmp_pst_my_hash_tag->tag_type;

        switch (tmp_pst_my_hash_tag->tag_type)
        {
        case TAG_INTEGER:
            memcpy(&(ret_fieldstat_tag->value_longlong), tmp_pst_my_hash_tag->value, sizeof(long long));
            break;
        case TAG_DOUBLE:
            memcpy(&(ret_fieldstat_tag->value_double), tmp_pst_my_hash_tag->value, sizeof(double));
            break;
        case TAG_CSTRING:
            ret_fieldstat_tag->value_str = (char *)malloc(tmp_pst_my_hash_tag->value_len);
            memcpy((char *)ret_fieldstat_tag->value_str, tmp_pst_my_hash_tag->value, tmp_pst_my_hash_tag->value_len);
            break;
        default:
            break;
        }

        ret_fieldstat_tag++;
        tmp_pst_my_hash_tag++;
    }
}

struct tag_hash_key *tag_hash_key_copy(const struct tag_hash_key *src)
{
    struct tag_hash_key *dst = (struct tag_hash_key *)malloc(sizeof(struct tag_hash_key));
    dst->n_my_tag = src->n_my_tag;
    dst->is_hashed = src->is_hashed;
    dst->hashv = src->hashv;
    dst->my_tag = (struct my_hash_tag *)malloc(sizeof(struct my_hash_tag) * src->n_my_tag);
    for (int i = 0; i < src->n_my_tag; i++)
    {
        struct my_hash_tag *dst_tag = dst->my_tag + i;
        struct my_hash_tag *src_tag = src->my_tag + i;
    
        dst_tag->key_len = src_tag->key_len;
        dst_tag->key = (char *)malloc(src_tag->key_len);
        memcpy(dst_tag->key, src_tag->key, src_tag->key_len);
    
        dst_tag->value_len = src_tag->value_len;
        dst_tag->value = (unsigned char *)malloc(src_tag->value_len);
        memcpy(dst_tag->value, src_tag->value, src_tag->value_len);
    
        dst_tag->tag_type = src_tag->tag_type;
    }

    return dst;
}

void tag_hash_key_free(struct tag_hash_key *tag_key)
{
    struct my_hash_tag *my_hash_tag_arr = tag_key->my_tag;
    for (int i = 0; i < tag_key->n_my_tag; i++)
    {
        free(my_hash_tag_arr[i].key);
        free(my_hash_tag_arr[i].value);
    }
    free(tag_key->my_tag);
    free(tag_key);
}

// FNV-1a hash (http://www.isthe.com/chongo/tech/comp/fnv/)
unsigned tag_hash_key_cal_fnv_hash(const struct tag_hash_key *tag, unsigned seed) {
    uint32_t h = 2166136261UL + (seed * 15);

    struct my_hash_tag *my_hash_tag_arr = tag->my_tag;
    size_t n_tag = tag->n_my_tag;
    size_t key_len, val_len;
    char *key;
    unsigned char *value;
    for (int i = 0; i < n_tag; i++) {
        key_len = my_hash_tag_arr[i].key_len;
        key = my_hash_tag_arr[i].key;

        for (int j = 0; j < key_len; j++) {
            h = h ^ (unsigned char)key[j];
            h = h * 16777619UL;
        }
        val_len = my_hash_tag_arr[i].value_len;
        value = my_hash_tag_arr[i].value;
        for (int j = 0; j < val_len; j++) {
            h = h ^ value[j];
            h = h * 16777619UL;
        }
    }

    return h;
}

/*
{
    "tags": [
        {
            "k": "tag1",
            "v": "value1"
            "t":0/1/2
        },
        {
            "k": "tag2",
            "v": "value2"
            "t":0/1/2
        }
    ],
    "hashed":true,
    "hashv": 0,
}
*/

void tag_hash_key_serialize(const struct tag_hash_key *tag, char **blob, size_t *blob_size)
{
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_size);
    mpack_build_map(&writer);

    mpack_write_cstr(&writer, "tags");
    mpack_start_array(&writer, tag->n_my_tag);
    for (int i = 0; i < tag->n_my_tag; i++)
    {
        struct my_hash_tag *my_hash_tag = tag->my_tag + i;
        mpack_start_map(&writer, 3);
        mpack_write_cstr(&writer, "k");
        mpack_write_bin(&writer, my_hash_tag->key, my_hash_tag->key_len);
        mpack_write_cstr(&writer, "v");
        mpack_write_bin(&writer, (const char *)my_hash_tag->value, my_hash_tag->value_len);
        mpack_write_cstr(&writer, "t");
        mpack_write_u8(&writer, my_hash_tag->tag_type);
        mpack_finish_map(&writer);
    }
    mpack_finish_array(&writer);

    mpack_write_cstr(&writer, "hashed");
    mpack_write_bool(&writer, tag->is_hashed);

    mpack_write_cstr(&writer, "hashv");
    mpack_write_u32(&writer, tag->hashv);

    mpack_complete_map(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        printf("ERR: An error occurred in tag_hash_key_serialize!\n");
    }
}

struct tag_hash_key *tag_hash_key_deserialize(const char *blob, size_t blob_size)
{
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, blob_size);
    mpack_tree_parse(&tree);
    mpack_node_t root = mpack_tree_root(&tree);

    mpack_node_t tags = mpack_node_map_cstr(root, "tags");
    size_t n_tag = mpack_node_array_length(tags);
    struct my_hash_tag *my_hash_tag_arr = (struct my_hash_tag *)malloc(sizeof(struct my_hash_tag) * n_tag);
    for (int i = 0; i < n_tag; i++)
    {
        struct my_hash_tag *my_hash_tag = my_hash_tag_arr + i;
        mpack_node_t tag_node = mpack_node_array_at(tags, i);
        mpack_node_t value = mpack_node_map_cstr(tag_node, "v");
        mpack_node_t key = mpack_node_map_cstr(tag_node, "k");

        my_hash_tag->key_len = mpack_node_bin_size(key);
        my_hash_tag->key = (char *)malloc(my_hash_tag->key_len);
        memcpy(my_hash_tag->key, mpack_node_bin_data(key), my_hash_tag->key_len);

        my_hash_tag->value_len = mpack_node_bin_size(value);
        my_hash_tag->value = (unsigned char *)malloc(my_hash_tag->value_len);
        memcpy(my_hash_tag->value, mpack_node_bin_data(value), my_hash_tag->value_len);

        my_hash_tag->tag_type = mpack_node_u8(mpack_node_map_cstr(tag_node, "t"));
    }

    bool is_hashed = mpack_node_bool(mpack_node_map_cstr(root, "hashed"));
    unsigned hashv_val = mpack_node_u32(mpack_node_map_cstr(root, "hashv"));

    struct tag_hash_key *my_hash_key = (struct tag_hash_key *)malloc(sizeof(struct tag_hash_key));
    my_hash_key->my_tag = my_hash_tag_arr;
    my_hash_key->n_my_tag = n_tag;
    my_hash_key->is_hashed = is_hashed;
    my_hash_key->hashv = hashv_val;

    mpack_tree_destroy(&tree);
    return my_hash_key;
}


int tmp_get_hash_count()
{
    return hash_cnt;
}

int tmp_get_simple_hash_count()
{
    return simple_hash_cnt;
}

void reset_hash_count()
{
    hash_cnt = 0;
    simple_hash_cnt = 0;
}