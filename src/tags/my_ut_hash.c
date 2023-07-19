#include "my_ut_hash.h"

#define USING_BASE_HASH HASH_JEN

unsigned int my_tag_hash_func(struct my_hash_tag_key *my_hash_key)
{
    if (my_hash_key->hashv != 0)
    {
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

void my_hash_tag_key_free(struct my_hash_tag_key *my_hash_key)
{
    for (int i = 0; i < my_hash_key->n_my_tag; i++)
    {
        free(my_hash_key->my_tag[i].key);
        free(my_hash_key->my_tag[i].value);
    }
    free(my_hash_key->my_tag);
    free(my_hash_key);
}