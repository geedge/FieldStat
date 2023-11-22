#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#define XXH_INLINE_ALL
#include "xxhash/xxhash.h"
#include "my_ut_hash_inner.h"
#include "my_ut_hash.h"
#include "serializer.h"


struct tag_hash_key {
   
    struct fieldstat_tag *tags; // not used for hash, just to recover the original tags.
    size_t n_my_tag;
    unsigned int hashv;
    bool is_deep_copy;
};

unsigned int tag_hash_key_cal_hash_value(const struct tag_hash_key *my_hash_key)
{
    return my_hash_key->hashv;
}

int tag_hash_key_cmp(const struct tag_hash_key *a, const struct tag_hash_key *b)
{
    if (a->n_my_tag == 0 && b->n_my_tag == 0) {
        return 0;
    }

    if (a->n_my_tag != b->n_my_tag) {
        return 1;
    }
    for (size_t i = 0; i < a->n_my_tag; i++) {
        const struct fieldstat_tag *tag_a = &a->tags[i];
        const struct fieldstat_tag *tag_b = &b->tags[i];
        if (strcmp(tag_a->key, tag_b->key) != 0) {
            return 1;
        }
        if (tag_a->type != tag_b->type) {
            return 1;
        }
        switch (tag_a->type) {
            case TAG_INTEGER:
                if (tag_a->value_longlong != tag_b->value_longlong) {
                    return 1;
                }
                break;
            case TAG_DOUBLE:
                if (tag_a->value_double != tag_b->value_double) {
                    return 1;
                }
                break;
            case TAG_CSTRING:
                if (strcmp(tag_a->value_str, tag_b->value_str) != 0) {
                    return 1;
                }
                break;
            default:
                assert(0);
                break;
        }
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

#define FNV_HASH_FACTOR 16777619U
#define FNV_HASH_OFFSET 2166136261U

struct fnv_hash_calculator {
    unsigned int hashv;
};

void fnv_hash_calculator_init(struct fnv_hash_calculator *calculator, unsigned int seed)
{
    calculator->hashv = FNV_HASH_OFFSET + (seed * 15);
}

void fnv_hash_calculator_update(struct fnv_hash_calculator *calculator, const char *key, size_t key_len)
{
    unsigned int hashv = calculator->hashv;
    for (size_t i = 0; i < key_len; i++) {
        hashv = hashv ^ key[i];
        hashv = hashv * FNV_HASH_FACTOR;
    }
    calculator->hashv = hashv;
}

unsigned int fnv_hash_calculator_get_hashv(const struct fnv_hash_calculator *calculator)
{
    return calculator->hashv;
}

unsigned int cal_tag_hash(struct fieldstat_tag *tags, size_t n_tag, unsigned int seed)
{
    struct fnv_hash_calculator calculator;
    fnv_hash_calculator_init(&calculator, seed);

    for (int i = 0; i < n_tag; i++)
    {
        fnv_hash_calculator_update(&calculator, tags[i].key, strlen(tags[i].key));
        switch (tags[i].type)
        {
        case TAG_INTEGER:
            fnv_hash_calculator_update(&calculator, (char *)&tags[i].value_longlong, sizeof(long long));
            break;
        case TAG_DOUBLE:
            fnv_hash_calculator_update(&calculator, (char *)&tags[i].value_double, sizeof(double));
            break;
        case TAG_CSTRING:
            fnv_hash_calculator_update(&calculator, tags[i].value_str, strlen(tags[i].value_str));
            break;
        default:
            assert(0);
            break;
        }
    }

    return fnv_hash_calculator_get_hashv(&calculator);
}

unsigned int cal_tag_hash_xxhash(struct fieldstat_tag *tag, size_t n_tag, unsigned int seed)
{
    XXH32_state_t state;
    XXH32_reset(&state, seed);
    
    for (int i = 0; i < n_tag; i++)
    {
        XXH32_update(&state, (const xxh_u8 *)tag[i].key, strlen(tag[i].key));
        switch (tag[i].type)
        {
        case TAG_INTEGER:
            XXH32_update(&state, (const xxh_u8 *)&tag[i].value_longlong, sizeof(long long));
            break;
        case TAG_DOUBLE:
            XXH32_update(&state, (const xxh_u8 *)&tag[i].value_double, sizeof(double));
            break;
        case TAG_CSTRING:
            XXH32_update(&state, (const xxh_u8 *)tag[i].value_str, strlen(tag[i].value_str));
            break;
        default:
            assert(0);
            break;
        }
    }

    return XXH32_digest(&state);
}

unsigned int my_tag_hash_func(struct fieldstat_tag *tags, size_t n_tag)
{
    return cal_tag_hash_xxhash(tags, n_tag, 0);
}

struct tag_hash_key *tag_hash_key_construct_with_fieldstat_tag(const struct fieldstat_tag *src, size_t n_src)
{
    struct tag_hash_key *ret_hash_key = (struct tag_hash_key *)malloc(sizeof(struct tag_hash_key));

    ret_hash_key->tags = n_src == 0 ? NULL : (struct fieldstat_tag *)src;
    ret_hash_key->n_my_tag = n_src;
    ret_hash_key->is_deep_copy = false;

    ret_hash_key->n_my_tag = n_src;

    // calculate hash value
    ret_hash_key->hashv = my_tag_hash_func(ret_hash_key->tags, ret_hash_key->n_my_tag);

    return ret_hash_key;
}

void tag_hash_key_store(struct tag_hash_key *tag_key)
{
    if (tag_key->is_deep_copy) {
        return;
    }
    tag_key->is_deep_copy = true;
    if (tag_key->n_my_tag == 0) {
        return;
    }
    struct fieldstat_tag *new_tags = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * tag_key->n_my_tag);
    for (int i = 0; i < tag_key->n_my_tag; i++) {
        fieldtag_copy(&new_tags[i], &tag_key->tags[i]);
    }
    tag_key->tags = new_tags;
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
    dst->is_deep_copy = true;

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
    if (tag_key->is_deep_copy) {
        fieldtag_list_free(tag_key->tags, tag_key->n_my_tag);
    }
    free(tag_key);
}

// FNV-1a hash (http://www.isthe.com/chongo/tech/comp/fnv/)
unsigned tag_hash_key_cal_fnv_hash(const struct tag_hash_key *tag, unsigned seed) {
    // return cal_tag_hash(tag->tags, tag->n_my_tag, seed);
    return cal_tag_hash_xxhash(tag->tags, tag->n_my_tag, seed);
}


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

char *tag_hash_key_get_compound_key(const struct tag_hash_key *tag)
{
    char *key;
    size_t key_size;
    build_dynamic_cell_key(tag->tags, tag->n_my_tag, &key, &key_size);
    return key;
}