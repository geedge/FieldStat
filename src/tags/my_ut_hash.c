#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "mpack/mpack.h"
#include "my_ut_hash_inner.h"
#include "my_ut_hash.h"
#include "tag_id_map.h"

// #define USING_BASE_HASH HASH_JEN
#define USING_BASE_HASH HASH_SFH

int simple_hash_cnt = 0;
int hash_cnt = 0;

void build_dynamic_cell_key(const struct fieldstat_tag tags[], size_t n_tags, char **out_key, size_t *out_key_size)
{
	int i = 0;
	int used_len = 0;
	struct fieldstat_tag *tag = NULL;

    char one_time_key[1024] = {0};
    size_t tmp_key_size = sizeof(one_time_key);
    size_t remain_key_size = tmp_key_size;
    size_t total_key_size = tmp_key_size;
    char *dynamic_mem = (char *)malloc(remain_key_size);
    size_t key_len;
    size_t val_len;

	for(i = 0; i < (int)n_tags; i++)
	{
		tag = (struct fieldstat_tag *)&tags[i];
		switch(tag->type)
		{
			case 0:
                key_len = strlen(tag->key);
                val_len = sizeof(long long);
                used_len = key_len + val_len;
                memcpy(one_time_key, tag->key, key_len);
                memcpy(one_time_key + key_len, &tag->value_longlong, val_len);
				break;
			case 1:
                key_len = strlen(tag->key);
                val_len = sizeof(double);
                used_len = key_len + val_len;
                memcpy(one_time_key, tag->key, key_len);
                memcpy(one_time_key + key_len, &tag->value_double, val_len);
				break;
			case 2:
                key_len = strlen(tag->key);
                val_len = strlen(tag->value_str);
                used_len = key_len + val_len;
                memcpy(one_time_key, tag->key, key_len);
                memcpy(one_time_key + key_len, tag->value_str, val_len);
				break;
			default:
				assert(0);
				break;
		}
        
        if (used_len >= remain_key_size) {
            total_key_size += tmp_key_size;
            dynamic_mem = (char *)realloc(dynamic_mem, total_key_size);
            remain_key_size += tmp_key_size;
        }

        memcpy(dynamic_mem + total_key_size - remain_key_size, one_time_key, used_len);
        remain_key_size -= used_len;
	}

    *out_key = (char *)malloc(total_key_size - remain_key_size + 1);
    memcpy(*out_key, dynamic_mem, total_key_size - remain_key_size);
    (*out_key)[total_key_size - remain_key_size] = '\0';
    *out_key_size = total_key_size - remain_key_size; // not include '\0'

    free(dynamic_mem);
}

struct tag_hash_key {
    char *compound_key;
    size_t compound_key_len; // excluding '\0'
    
    struct fieldstat_tag *tags; // not used for hash, just to recover the original tags.
    size_t n_my_tag;
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
    simple_hash_cnt++;
    return my_hash_key->hashv;
}

int tag_hash_key_cmp(const struct tag_hash_key *a, const struct tag_hash_key *b)
{
    if (a->n_my_tag != b->n_my_tag) {
        return 1;
    }
    if (a->compound_key_len != b->compound_key_len) {
        return 1;
    }
    if (memcmp(a->compound_key, b->compound_key, a->compound_key_len) != 0) {
        return 1;
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/*                                  fieldtag                                  */
/* -------------------------------------------------------------------------- */

void fieldtag_copy(struct fieldstat_tag *dest, const struct fieldstat_tag *src)
{
    dest->key = strdup(src->key);
    dest->type = src->type;
    switch(src->type)
    {
        case TAG_INTEGER:
            dest->value_longlong = src->value_longlong;
            break;
        case TAG_DOUBLE:
            dest->value_double = src->value_double;
            break;
        case TAG_CSTRING:
            dest->value_str = strdup(src->value_str);
            break;
        default:
            assert(0);
            break;
    }
}

inline void fieldtag_clear(struct fieldstat_tag *tag)
{
    free((char *)tag->key);
    if (tag->type == TAG_CSTRING) {
        free((char *)tag->value_str);
    }
}

void fieldtag_list_free(struct fieldstat_tag *tags, size_t n_tags)
{
    for (int i = 0; i < (int)n_tags; i++) {
        fieldtag_clear(&tags[i]);
    }
    free(tags);
}

int my_cmp_tag(const void *a, const void *b)
{
    struct fieldstat_tag *aa = (struct fieldstat_tag *)a;
    struct fieldstat_tag *bb = (struct fieldstat_tag *)b;
    int key_cmp = strcmp(aa->key, bb->key);
    if (key_cmp != 0) {
        return key_cmp;
    }

    switch (aa->type) {
        case TAG_INTEGER:
            return aa->value_longlong - bb->value_longlong;
        case TAG_DOUBLE:
            return aa->value_double - bb->value_double;
        case TAG_CSTRING:
            return strcmp(aa->value_str, bb->value_str);
        default:
            return 0;
    }
}

/* 
    [{
        "key":<tag key string>
        "type":0 1 or 2
        "value":<tag value, can be int(0) double(1) or str(2)>
    }]
*/
void fieldtag_list_serialize(const struct fieldstat_tag *tag_list, size_t tag_num, char **blob, size_t *blob_size)
{
    struct fieldstat_tag *tag_list_sorted = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * tag_num);
    for (int i = 0; i < tag_num; i++) {
        fieldtag_copy(&tag_list_sorted[i], &tag_list[i]);
    }
    // sort tag so that when merge by blob, we can merge cells with the same tags
    qsort(tag_list_sorted, tag_num, sizeof(struct fieldstat_tag), my_cmp_tag);

    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_size);

    mpack_start_array(&writer, tag_num);
    for (int j = 0; j < tag_num; j++) {
        mpack_start_map(&writer, 3);
        mpack_write_cstr(&writer, "key");
        mpack_write_cstr(&writer, tag_list_sorted[j].key);
        mpack_write_cstr(&writer, "type");
        mpack_write_i8(&writer, tag_list_sorted[j].type);
        mpack_write_cstr(&writer, "value");
        switch (tag_list_sorted[j].type)
        {
            case TAG_INTEGER:
                mpack_write_i64(&writer, tag_list_sorted[j].value_longlong);
                break;
            case TAG_DOUBLE:
                mpack_write_double(&writer, tag_list_sorted[j].value_double);
                break;
            case TAG_CSTRING:
                mpack_write_cstr(&writer, tag_list_sorted[j].value_str);
                break;
            default:
                assert(0);
                break;
        }
        mpack_finish_map(&writer);
    }
    mpack_finish_array(&writer);

    if (mpack_writer_destroy(&writer) != mpack_ok) {
        printf("ERR mpack writer fieldtag_list_serialize destroy failed\n");
    }

    fieldtag_list_free(tag_list_sorted, tag_num);
}

void fieldtag_list_deserialize(const char *blob, size_t blob_size, struct fieldstat_tag **tag_list_out, size_t *tag_num_out)
{
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, blob_size);
    mpack_tree_parse(&tree);
    mpack_node_t tag_arr = mpack_tree_root(&tree);

    size_t tag_num = mpack_node_array_length(tag_arr);

    struct fieldstat_tag *tag_list = malloc(sizeof(struct fieldstat_tag) * tag_num);
    for (int j = 0; j < tag_num; j++) {
        mpack_node_t tag = mpack_node_array_at(tag_arr, j);

        mpack_node_t key = mpack_node_map_cstr(tag, "key");
        tag_list[j].key = (const char *)malloc(mpack_node_strlen(key) + 1);
        mpack_node_copy_cstr(key, (char *)tag_list[j].key, mpack_node_strlen(key) + 1);
        tag_list[j].type = mpack_node_i8(mpack_node_map_cstr(tag, "type"));

        mpack_node_t value = mpack_node_map_cstr(tag, "value");

        switch (tag_list[j].type)
        {
            case TAG_INTEGER:
                tag_list[j].value_longlong = mpack_node_i64(value);
                break;
            case TAG_DOUBLE:
                tag_list[j].value_double = mpack_node_double(value);
                break;
            case TAG_CSTRING:
                tag_list[j].value_str = (const char *)malloc(mpack_node_strlen(value) + 1);
                mpack_node_copy_cstr(value, (char *)tag_list[j].value_str, mpack_node_strlen(value) + 1);
                break;
            default:
                assert(0);
                break;
        }
    }
    *tag_list_out = tag_list;
    *tag_num_out = tag_num;

    mpack_tree_destroy(&tree);
}

struct tag_hash_key *tag_hash_key_construct_with_fieldstat_tag(const struct fieldstat_tag *src, size_t n_src)
{
    struct tag_hash_key *ret_hash_key = (struct tag_hash_key *)malloc(sizeof(struct tag_hash_key));

    char *compound_key = NULL;
    size_t compound_key_len = 0;
    build_dynamic_cell_key(src, n_src, &compound_key, &compound_key_len);
    ret_hash_key->compound_key = compound_key;
    ret_hash_key->compound_key_len = compound_key_len;

    ret_hash_key->tags = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * n_src);
    for (int i = 0; i < n_src; i++) {
        fieldtag_copy(&ret_hash_key->tags[i], &src[i]);
    }

    ret_hash_key->n_my_tag = n_src;
    USING_BASE_HASH(compound_key, compound_key_len, ret_hash_key->hashv);

    hash_cnt++;
    return ret_hash_key;
}

void tag_hash_key_convert_to_fieldstat_tag(const struct tag_hash_key *tag_key, struct fieldstat_tag **out, size_t *n_out)
{
    size_t ret_n_tag = tag_key->n_my_tag;
    *n_out = ret_n_tag;
    struct fieldstat_tag *ret_fieldstat_tag = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * ret_n_tag);
    *out = ret_fieldstat_tag;

    for (int i = 0; i < ret_n_tag; i++) {
        fieldtag_copy(&ret_fieldstat_tag[i], &tag_key->tags[i]);
    }
}

struct tag_hash_key *tag_hash_key_copy(const struct tag_hash_key *src)
{
    struct tag_hash_key *dst = (struct tag_hash_key *)malloc(sizeof(struct tag_hash_key));
    dst->n_my_tag = src->n_my_tag;
    dst->hashv = src->hashv;
    dst->compound_key_len = src->compound_key_len;
    dst->compound_key = (char *)malloc(dst->compound_key_len + 1);
    memcpy(dst->compound_key, src->compound_key, dst->compound_key_len);
    dst->compound_key[dst->compound_key_len] = '\0';

    dst->tags = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * dst->n_my_tag);
    for (int i = 0; i < dst->n_my_tag; i++) {
        fieldtag_copy(&dst->tags[i], &src->tags[i]);
    }

    return dst;
}

void tag_hash_key_free(struct tag_hash_key *tag_key)
{
    fieldtag_list_free(tag_key->tags, tag_key->n_my_tag);
    free(tag_key->compound_key);

    free(tag_key);
}

// FNV-1a hash (http://www.isthe.com/chongo/tech/comp/fnv/)
unsigned tag_hash_key_cal_fnv_hash(const struct tag_hash_key *tag, unsigned seed) {
    const char *key = tag->compound_key;
    size_t len = tag->compound_key_len;
    size_t i;
    uint32_t h = 2166136261UL + (seed * 15);  // FNV_OFFSET 32 bit with magic number seed
    for (i = 0; i < len; ++i) {
        h = h ^ (unsigned char)key[i];
        h = h * 16777619UL;  // FNV_PRIME 32 bit
    }
    return h;
}

/*
{
    "tags": [
        <fieldstat tag serialized result>
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
    char *blob_tag;
    size_t blob_size_tag;
    fieldtag_list_serialize(tag->tags, tag->n_my_tag, &blob_tag, &blob_size_tag);
    mpack_write_bin(&writer, blob_tag, blob_size_tag);
    free(blob_tag);

    mpack_write_cstr(&writer, "hashv");
    // the tags may be changed, so we need to recalculate the hash value
    char *compound_key;
    size_t compound_key_len;
    build_dynamic_cell_key(tag->tags, tag->n_my_tag, &compound_key, &compound_key_len);
    unsigned hashv = 0;
    USING_BASE_HASH(compound_key, compound_key_len, hashv);
    mpack_write_u32(&writer, hashv);
    free(compound_key);

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
    struct fieldstat_tag *fieldstat_tag_arr;
    size_t n_tag;
    fieldtag_list_deserialize(mpack_node_bin_data(tags), mpack_node_bin_size(tags), &fieldstat_tag_arr, &n_tag);

    unsigned hashv_val = mpack_node_u32(mpack_node_map_cstr(root, "hashv"));
    
    char *compound_key;
    size_t compound_key_len;
    build_dynamic_cell_key(fieldstat_tag_arr, n_tag, &compound_key, &compound_key_len);

    struct tag_hash_key *ret_hash_key = (struct tag_hash_key *)malloc(sizeof(struct tag_hash_key));
    ret_hash_key->compound_key = compound_key;
    ret_hash_key->compound_key_len = compound_key_len;
    ret_hash_key->tags = fieldstat_tag_arr;
    ret_hash_key->n_my_tag = n_tag;
    ret_hash_key->hashv = hashv_val;

    mpack_tree_destroy(&tree);
    return ret_hash_key;
}

// todo: delete them

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

const char *tag_hash_key_get_compound_key(const struct tag_hash_key *tag)
{
    return tag->compound_key;
}