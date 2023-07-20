#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "mpack/mpack.h"
#include "my_ut_hash_inner.h"
#include "my_ut_hash.h"


#define USING_BASE_HASH HASH_JEN

struct my_hash_tag {
    char *key;
    size_t key_len; // including '\0'
    unsigned char *value;
    size_t value_len; // sizeof(value) if INT or DOUBLE, strlen(value) + 1 if CSTRING
};

struct my_hash_tag_key {
    struct my_hash_tag *my_tag;
    size_t n_my_tag;
    bool is_hashed;
    unsigned int hashv;
};

unsigned int my_tag_hash_func(struct my_hash_tag_key *my_hash_key)
{
    if (my_hash_key->is_hashed) {
        return my_hash_key->hashv;
    }
    unsigned int hashv = 0;
    unsigned int result = 0;
    struct my_hash_tag *tmp_pst_hashed_tag = my_hash_key->my_tag;
    for (int i = 0; i < my_hash_key->n_my_tag; i++)
    {
        printf("the hash key: %s, length: %zu, pst: %p\n", tmp_pst_hashed_tag->key, tmp_pst_hashed_tag->key_len, tmp_pst_hashed_tag->key);
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

void convert_fieldstat_tag_to_my_hash_tag(const struct fieldstat_tag *src, struct my_hash_tag *dst)
{
    size_t key_len = strlen(src->key) + 1;
    dst->key = (char *)malloc(key_len);
    memcpy(dst->key, src->key, key_len);
    dst->key_len = key_len;
    unsigned char *value = NULL;
    switch (src->type)
    {
    case TAG_INTEGER:
        value = (unsigned char *)malloc(sizeof(long long));
        memcpy(value, &(src->value_int), sizeof(long long));
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

struct my_hash_tag_key *my_hash_key_convert_from_fieldstat_tag(const struct fieldstat_tag *src, size_t n_src)
{
    struct my_hash_tag *my_hash_tag_arr = (struct my_hash_tag *)malloc(sizeof(struct my_hash_tag) * n_src);
    for (int i = 0; i < n_src; i++) {
        convert_fieldstat_tag_to_my_hash_tag(src + i, my_hash_tag_arr + i);
    }

    struct my_hash_tag_key *my_hash_key = (struct my_hash_tag_key *)malloc(sizeof(struct my_hash_tag_key));
    my_hash_key->my_tag = my_hash_tag_arr;
    my_hash_key->n_my_tag = n_src;
    my_hash_key->hashv = 0;
    return my_hash_key;
}

void my_hash_key_convert_to_fieldstat_tag(const struct my_hash_tag_key *tag_key, struct fieldstat_tag **out, size_t *n_out)
{
    size_t ret_n_tag = tag_key->n_my_tag;
    *n_out = ret_n_tag;
    struct fieldstat_tag *ret_fieldstat_tag = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * ret_n_tag);
    *out = ret_fieldstat_tag;

    struct my_hash_tag *tmp_pst_my_hash_tag = tag_key->my_tag;
    for (int i = 0; i < ret_n_tag; i++)
    {
        ret_fieldstat_tag->key = strdup(tmp_pst_my_hash_tag->key);
        ret_fieldstat_tag->type = TAG_CSTRING; // todo: 这里没法复原了，因为my_hash_tag里没有type
        ret_fieldstat_tag->value_str = (char *)tmp_pst_my_hash_tag->value;
        ret_fieldstat_tag++;
        tmp_pst_my_hash_tag++;
    }
}
struct my_hash_tag_key *my_hash_tag_key_copy(const struct my_hash_tag_key *src)
{
    //todo: 当前全部拷贝，但是实际上某些调用这个接口的地方可以改成浅拷贝，看性能要求吧
    // note: 这个函数同时会拷贝hashv，所以尽量用copy，而不是从tag里构造
    struct my_hash_tag_key *dst = (struct my_hash_tag_key *)malloc(sizeof(struct my_hash_tag_key));
    dst->n_my_tag = src->n_my_tag;
    dst->hashv = src->hashv;
    dst->my_tag = (struct my_hash_tag *)malloc(sizeof(struct my_hash_tag) * src->n_my_tag);
    for (int i = 0; i < src->n_my_tag; i++)
    {
        dst->my_tag[i].key_len = src->my_tag[i].key_len;
        dst->my_tag[i].key = (char *)malloc(src->my_tag[i].key_len + 1);
        memcpy(dst->my_tag[i].key, src->my_tag[i].key, src->my_tag[i].key_len + 1);
        dst->my_tag[i].value_len = src->my_tag[i].value_len;
        dst->my_tag[i].value = (unsigned char *)malloc(src->my_tag[i].value_len);
        memcpy(dst->my_tag[i].value, src->my_tag[i].value, src->my_tag[i].value_len);
    }
}

void my_hash_tag_key_free(struct my_hash_tag_key *tag_key)
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
unsigned my_hash_tag_key_fnv_hash(const struct my_hash_tag_key *tag, unsigned seed) {
    uint32_t h = 2166136261UL + (seed * 15);

    struct my_hash_tag *my_hash_tag_arr = tag->my_tag;
    size_t n_tag = tag->n_my_tag;
    size_t key_len, val_len;
    char *key, *value;
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
            h = h ^ (unsigned char)value[j];
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
        },
        {
            "k": "tag2",
            "v": "value2"
        }
    ],
    "hashed":true,
    "hashv": 0,
}
*/

void my_hash_tag_key_serialize(const struct my_hash_tag_key *tag, char **blob, size_t *blob_size)
{
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_size);
    mpack_build_map(&writer);

    mpack_write_cstr(&writer, "tags");
    mpack_start_array(&writer, tag->n_my_tag);
    for (int i = 0; i < tag->n_my_tag; i++)
    {
        struct my_hash_tag *my_hash_tag = tag->my_tag + i;
        mpack_start_map(&writer, 2);
        mpack_write_cstr(&writer, "k");
        mpack_write_bin(&writer, my_hash_tag->key, my_hash_tag->key_len);
        mpack_write_cstr(&writer, "v");
        mpack_write_bin(&writer, my_hash_tag->value, my_hash_tag->value_len);
        mpack_finish_map(&writer);
    }
    mpack_finish_array(&writer);

    mpack_write_cstr(&writer, "hashed");
    mpack_write_bool(&writer, tag->is_hashed);

    mpack_write_cstr(&writer, "hashv");
    mpack_write_u32(&writer, tag->hashv);

    mpack_complete_map(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        printf("ERR: An error occurred in my_hash_tag_key_serialize!\n");
    }
}

struct my_hash_tag_key *my_hash_tag_key_deserialize(const char *blob, size_t blob_size)
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
    }

    bool is_hashed = mpack_node_bool(mpack_node_map_cstr(root, "hashed"));
    unsigned hashv_val = mpack_node_u32(mpack_node_map_cstr(root, "hashv"));

    struct my_hash_tag_key *my_hash_key = (struct my_hash_tag_key *)malloc(sizeof(struct my_hash_tag_key));
    my_hash_key->my_tag = my_hash_tag_arr;
    my_hash_key->n_my_tag = n_tag;
    my_hash_key->is_hashed = is_hashed;
    my_hash_key->hashv = hashv_val;

    mpack_tree_destroy(&tree);
    return my_hash_key;
}