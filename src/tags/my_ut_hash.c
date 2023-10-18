#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "my_ut_hash_inner.h"
#include "my_ut_hash.h"
#include "serializer.h"

// #define USING_BASE_HASH HASH_JEN
#define USING_BASE_HASH HASH_SFH

void build_dynamic_cell_key(const struct fieldstat_tag tags[], size_t n_tags, char **out_key, size_t *out_key_size)
{
    if (n_tags == 0) {
        // use a default dummy key
        *out_key = strdup("no tags");
        *out_key_size = strlen(*out_key);
        return;
    }
	int i = 0;
	int used_len = 0;
	struct fieldstat_tag *tag = NULL;

    const size_t init_key_size = 1024;
    size_t one_time_key_size = init_key_size;
    char *one_time_key = (char *)malloc(init_key_size);
    size_t remain_key_size = init_key_size;
    size_t total_key_size = init_key_size;
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
                while (used_len >= one_time_key_size) {
                    one_time_key_size += init_key_size;
                    one_time_key = (char *)realloc(one_time_key, one_time_key_size);
                }
                memcpy(one_time_key, tag->key, key_len);
                memcpy(one_time_key + key_len, &tag->value_longlong, val_len);
				break;
			case 1:
                key_len = strlen(tag->key);
                val_len = sizeof(double);
                used_len = key_len + val_len;
                while (used_len >= one_time_key_size) {
                    one_time_key_size += init_key_size;
                    one_time_key = (char *)realloc(one_time_key, one_time_key_size);
                }
                memcpy(one_time_key, tag->key, key_len);
                memcpy(one_time_key + key_len, &tag->value_double, val_len);
				break;
			case 2:
                key_len = strlen(tag->key);
                val_len = strlen(tag->value_str);
                used_len = key_len + val_len;
                while (used_len >= one_time_key_size) {
                    one_time_key_size += init_key_size;
                    one_time_key = (char *)realloc(one_time_key, one_time_key_size);
                }
                memcpy(one_time_key, tag->key, key_len);
                memcpy(one_time_key + key_len, tag->value_str, val_len);
				break;
			default:
				assert(0);
				break;
		}
        
        while (used_len >= remain_key_size) {
            total_key_size += init_key_size;
            dynamic_mem = (char *)realloc(dynamic_mem, total_key_size);
            remain_key_size += init_key_size;
        }

        memcpy(dynamic_mem + total_key_size - remain_key_size, one_time_key, used_len);
        remain_key_size -= used_len;
	}

    *out_key = (char *)malloc(total_key_size - remain_key_size + 1);
    memcpy(*out_key, dynamic_mem, total_key_size - remain_key_size);
    (*out_key)[total_key_size - remain_key_size] = '\0';
    *out_key_size = total_key_size - remain_key_size; // not include '\0'

    free(dynamic_mem);
    free(one_time_key);
}

struct tag_hash_key {
    char *compound_key;
    size_t compound_key_len; // excluding '\0'
    
    struct fieldstat_tag *tags; // not used for hash, just to recover the original tags.
    size_t n_my_tag;
    unsigned int hashv;
};

unsigned int tag_hash_key_cal_hash_value(const struct tag_hash_key *my_hash_key)
{
    return my_hash_key->hashv;
}

int tag_hash_key_cmp(const struct tag_hash_key *a, const struct tag_hash_key *b)
{
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

void fieldtag_list_free(struct fieldstat_tag *tags, size_t n_tags)
{
    if (tags == NULL) {
        return;
    }
    for (int i = 0; i < (int)n_tags; i++) {
        struct fieldstat_tag *tag = &tags[i];
        free((char *)tag->key);
        if (tag->type == TAG_CSTRING) {
            free((char *)tag->value_str);
        }
    }
    free(tags);
}

int my_cmp_tag(const void *a, const void *b)
{
    const struct fieldstat_tag *aa = (struct fieldstat_tag *)a;
    const struct fieldstat_tag *bb = (struct fieldstat_tag *)b;
    int key_cmp = strcmp(aa->key, bb->key);
    if (key_cmp != 0) {
        return key_cmp;
    }
    if (aa->type != bb->type) {
        return aa->type - bb->type;
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
void fieldtag_serialize(const struct fieldstat_tag *tag_list, char **blob, size_t *blob_size)
{
    struct fs_reader *reader = fs_reader_new();
    fs_reader_read_str(reader, tag_list->key);
    fs_reader_read_uint(reader, tag_list->type);
    switch (tag_list->type)
    {
        case TAG_INTEGER:
            fs_reader_read_longlong(reader, tag_list->value_longlong);
            break;
        case TAG_DOUBLE:
            fs_reader_read_double(reader, tag_list->value_double);
            break;
        case TAG_CSTRING:
            fs_reader_read_str(reader, tag_list->value_str);
            break;
        default:
            assert(0);
            break;
    }
    fs_reader_finalize(reader, blob, blob_size);
}

void fieldtag_deserialize(const char *blob, size_t blob_size, struct fieldstat_tag *tag)
{
    struct fs_writer *writer = fs_writer_new(blob, blob_size);

    fs_writer_write_str(writer, (char **)&tag->key);
    fs_writer_write_uint(writer, &tag->type);
    switch (tag->type)
    {
        case TAG_INTEGER:
            fs_writer_write_longlong(writer, &tag->value_longlong);
            break;
        case TAG_DOUBLE:
            fs_writer_write_double(writer, &tag->value_double);
            break;
        case TAG_CSTRING:
            fs_writer_write_str(writer, (char **)&tag->value_str);
            break;
        default:
            assert(0);
            break;
    }

    fs_writer_free(writer);
}

void fieldtag_list_serialize(const struct fieldstat_tag *tag_list, size_t tag_num, char **blob, size_t *blob_size)
{
    struct fs_reader *reader = fs_reader_new();
    if (tag_num == 0) {
        fs_reader_read_nil(reader);
    } else {
        fs_reader_start_bin_array(reader, tag_num);

        for (int j = 0; j < tag_num; j++) {
            char *blob_inner = NULL;
            size_t blob_inner_size = 0;
            fieldtag_serialize(&tag_list[j], &blob_inner, &blob_inner_size);
            fs_reader_read_bin(reader, blob_inner, blob_inner_size);
            free(blob_inner);
        }
    }

    fs_reader_finalize(reader, blob, blob_size);
}

void fieldtag_list_deserialize(const char *blob, size_t blob_size, struct fieldstat_tag **tag_list_out, size_t *tag_num_out)
{
    struct fs_writer *writer = fs_writer_new(blob, blob_size);
    int ret = fs_writer_expect_nil(writer);
    if (ret < 0) {
        printf("fs_writer_expect_nil error: %d\n", ret);
        assert(0);
    }

    if (ret == 1) {
        *tag_list_out = NULL;
        *tag_num_out = 0;
        fs_writer_free(writer);
        return;
    }

    char **tag_blob = NULL;
    size_t tag_num = 0;
    size_t *tag_blob_len = NULL;
    ret = fs_writer_write_bin_array(writer, &tag_blob, &tag_num, &tag_blob_len);
    if (ret < 0) {
        printf("fs_writer_write_bin_array error: %d\n", ret);
        assert(0);
    }

    struct fieldstat_tag *tag_list = malloc(sizeof(struct fieldstat_tag) * tag_num);
    for (int j = 0; j < tag_num; j++) {
        fieldtag_deserialize(tag_blob[j], tag_blob_len[j], &tag_list[j]);
    }
    *tag_list_out = tag_list;
    *tag_num_out = tag_num;

    for (int j = 0; j < tag_num; j++) {
        free(tag_blob[j]);
    }
    free(tag_blob);
    free(tag_blob_len);
    fs_writer_free(writer);
}

struct tag_hash_key *tag_hash_key_construct_with_fieldstat_tag(const struct fieldstat_tag *src, size_t n_src)
{
    struct tag_hash_key *ret_hash_key = (struct tag_hash_key *)malloc(sizeof(struct tag_hash_key));

    char *compound_key = NULL;
    size_t compound_key_len = 0;
    build_dynamic_cell_key(src, n_src, &compound_key, &compound_key_len);
    ret_hash_key->compound_key = compound_key;
    ret_hash_key->compound_key_len = compound_key_len;

    if (n_src == 0) {
        ret_hash_key->tags = NULL;
    } else {
        ret_hash_key->tags = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * n_src);
        for (int i = 0; i < n_src; i++) {
            fieldtag_copy(&ret_hash_key->tags[i], &src[i]);
        }
    }

    ret_hash_key->n_my_tag = n_src;
    USING_BASE_HASH(compound_key, compound_key_len, ret_hash_key->hashv);

    return ret_hash_key;
}

void tag_hash_key_convert_to_fieldstat_tag(const struct tag_hash_key *tag_key, struct fieldstat_tag **out, size_t *n_out)
{
    size_t ret_n_tag = tag_key->n_my_tag;
    *n_out = ret_n_tag;
    if (ret_n_tag == 0) {
        *out = NULL;
        return;
    }

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

    if (dst->n_my_tag == 0) {
        dst->tags = NULL;
    } else {
        dst->tags = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * dst->n_my_tag);
        for (int i = 0; i < dst->n_my_tag; i++) {
            fieldtag_copy(&dst->tags[i], &src->tags[i]);
        }
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
    "tags": <fieldstat tag serialized result>,
    "hashv": 0,
}
*/
void tag_hash_key_serialize(const struct tag_hash_key *tag, char **blob, size_t *blob_size)
{
    struct fs_reader *reader = fs_reader_new();

    char *blob_tag;
    size_t blob_size_tag;
    fieldtag_list_serialize(tag->tags, tag->n_my_tag, &blob_tag, &blob_size_tag);
    fs_reader_read_bin(reader, blob_tag, blob_size_tag);
    free(blob_tag);

    fs_reader_read_uint(reader, tag->hashv);

    fs_reader_finalize(reader, blob, blob_size);
}

struct tag_hash_key *tag_hash_key_deserialize(const char *blob, size_t blob_size)
{
    struct fs_writer *writer = fs_writer_new(blob, blob_size);

    char *tag_blob;
    size_t tag_blob_size;
    fs_writer_write_bin(writer, &tag_blob, &tag_blob_size);
    struct fieldstat_tag *fieldstat_tag_arr;
    size_t n_tag;
    fieldtag_list_deserialize(tag_blob, tag_blob_size, &fieldstat_tag_arr, &n_tag);
    free(tag_blob);

    unsigned hashv_val;
    fs_writer_write_uint(writer, &hashv_val);
    
    char *compound_key;
    size_t compound_key_len;
    build_dynamic_cell_key(fieldstat_tag_arr, n_tag, &compound_key, &compound_key_len);

    struct tag_hash_key *ret_hash_key = (struct tag_hash_key *)malloc(sizeof(struct tag_hash_key));
    ret_hash_key->compound_key = compound_key;
    ret_hash_key->compound_key_len = compound_key_len;
    ret_hash_key->tags = fieldstat_tag_arr;
    ret_hash_key->n_my_tag = n_tag;
    ret_hash_key->hashv = hashv_val;

    fs_writer_free(writer);
    return ret_hash_key;
}

const char *tag_hash_key_get_compound_key(const struct tag_hash_key *tag)
{
    return tag->compound_key;
}