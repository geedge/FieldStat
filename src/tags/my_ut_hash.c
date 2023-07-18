#include "my_ut_hash.h"

#define USING_BASE_HASH HASH_JEN

unsigned int my_tag_hash_func(struct my_hash_tag *my_tag, size_t n_my_tag)
{
    unsigned int hashv = 0;
    unsigned int result = 0;
    struct my_hash_tag *tmp_pst_hashed_tag = my_tag;
    for (int i = 0; i < n_my_tag; i++)
    {
        printf("the hash key: %s, length: %zu, pst: %p\n", tmp_pst_hashed_tag->key, tmp_pst_hashed_tag->key_len, tmp_pst_hashed_tag->key);
        USING_BASE_HASH(tmp_pst_hashed_tag->key, tmp_pst_hashed_tag->key_len, hashv);
        result = hashv;
        USING_BASE_HASH(tmp_pst_hashed_tag->value, tmp_pst_hashed_tag->value_len, hashv);
        result += hashv;

        tmp_pst_hashed_tag++;
    }
    return result;
}

void convert_fieldstat_tag_to_my_hash_tag(const struct fieldstat_tag *src, struct my_hash_tag *dst)
{
    size_t key_len = strlen(src->key);
    dst->key = (char *)malloc(key_len + 1);
    memcpy(dst->key, src->key, key_len + 1);
    dst->key_len = key_len;
    unsigned char *value = NULL;
    switch (src->type)
    {
    case TAG_INTEGER:
        value = (unsigned char *)malloc(sizeof(int));
        memcpy(value, &(src->value_int), sizeof(int));
        dst->value_len = sizeof(int);
        break;
    case TAG_DOUBLE:
        value = (unsigned char *)malloc(sizeof(double));
        memcpy(value, &(src->value_double), sizeof(double));
        dst->value_len = sizeof(double);
        break;
    case TAG_CSTRING:{
        size_t value_len = strlen(src->value_str);
        value = (unsigned char *)malloc(value_len + 1);
        memcpy(value, src->value_str, value_len + 1);
        dst->value_len = value_len;
        break;
    }
    default:
        break;
    }
    dst->value = value;
}

struct my_hash_tag *convert_fieldstat_tag_arr_to_my_hash_tag(const struct fieldstat_tag *src, size_t n_src)
{
    struct my_hash_tag *dst = (struct my_hash_tag *)malloc(sizeof(struct my_hash_tag) * n_src);
    for (int i = 0; i < n_src; i++)
    {
        convert_fieldstat_tag_to_my_hash_tag(src + i, dst + i);
    }
    return dst;
}