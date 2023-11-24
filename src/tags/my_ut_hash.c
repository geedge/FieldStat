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


unsigned int tag_hash_key_cal_hash_value(const struct tag_hash_key *my_hash_key)
{
    return my_hash_key->hashv;
}

// used by HASH_FIND
int tag_hash_key_cmp(const struct tag_hash_key *a, const struct tag_hash_key *b)
{
    if (a->n_my_tag == 0 && b->n_my_tag == 0) {
        return 0;
    }

    if (a->n_my_tag != b->n_my_tag) {
        return 1;
    }
    if (a->hashv128.high64 != b->hashv128.high64 || a->hashv128.low64 != b->hashv128.low64) {
        return 1;
    }

    size_t tmp_tag_n = a->n_my_tag;
    const struct fieldstat_tag *tag_a = a->tags;
    const struct fieldstat_tag *tag_b = b->tags;
    for (size_t i = 0; i < tmp_tag_n; i++) {
        if (tag_a->type == TAG_CSTRING) {
            if (strcmp(tag_a->value_str, tag_b->value_str) != 0) {
                return 1;
            }
        } else {
            if (tag_a->value_longlong != tag_b->value_longlong) { // sizeof(long long) == sizeof(double)
                return 1;
            }
        }

        // its hardly possible that the keys are different while the value is the same. What's more, the tags are in the same 128 hash. So it's not necessary to compare the key.
        // if (strcmp(tag_a->key, tag_b->key) != 0) {
        //     return 1;
        // }
        
        tag_a++;
        tag_b++;
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

unsigned int cal_tag_hash_xxhash(const struct fieldstat_tag *tag, size_t n_tag, unsigned int seed)
{
    XXH32_state_t state;
    XXH32_reset(&state, seed);

    for (int i = 0; i < n_tag; i++)
    {
        XXH32_update(&state, (const xxh_u8 *)tag[i].key, strlen(tag[i].key));

        if (tag[i].type != TAG_CSTRING) { // sizeof(long long) == sizeof(double)
            XXH32_update(&state, (const xxh_u8 *)&tag[i].value_longlong, sizeof(long long));
        } else {
            XXH32_update(&state, (const xxh_u8 *)tag[i].value_str, strlen(tag[i].value_str));
        }
    }

    return XXH32_digest(&state);
}

XXH128_hash_t cal_tag_hash_xxhash128(const struct fieldstat_tag *tag, size_t n_tag, unsigned int seed)
{
    XXH3_state_t *state = XXH3_createState();
    XXH3_128bits_reset_withSeed(state, seed);
    
    for (int i = 0; i < n_tag; i++)
    {
        // XXH32_update(&state, (const xxh_u8 *)tag[i].key, strlen(tag[i].key));
        XXH3_128bits_update(state, tag[i].key, strlen(tag[i].key));

        if (tag[i].type != TAG_CSTRING) { // sizeof(long long) == sizeof(double)
            XXH3_128bits_update(state, &tag[i].value_longlong, sizeof(long long));
        } else {
            XXH3_128bits_update(state, tag[i].value_str, strlen(tag[i].value_str));
        }
    }

    XXH128_hash_t ret = XXH3_128bits_digest(state);
    XXH3_freeState(state);

    return ret;
}

void tag_hash_key_init_with_fieldstat_tag(struct tag_hash_key *tag_key, const struct fieldstat_tag *src, size_t n_src, bool deep_copy)
{
    tag_key->n_my_tag = n_src;
    tag_key->is_deep_copy = deep_copy;
    if (n_src == 0) {
        tag_key->tags = NULL;
        tag_key->hashv = 0;
        return;
    }

    tag_key->hashv128 = cal_tag_hash_xxhash128(src, n_src, 0);
    tag_key->hashv = tag_key->hashv128.high64 ^ tag_key->hashv128.low64;

    if (deep_copy) {
        tag_key->tags = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * n_src);
        for (int i = 0; i < (int)n_src; i++) {
            fieldtag_copy(&tag_key->tags[i], &src[i]);
        }
    } else {
        tag_key->tags = (struct fieldstat_tag *)src;
    }
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
    dst->hashv128 = src->hashv128;

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

// todo: 可能可以在每次操作的时候，都和之前比一次cell id对应的tag，如果tag对上了，就沿用上次的cell id。
// 也许吧。

// FNV-1a hash (http://www.isthe.com/chongo/tech/comp/fnv/)
unsigned tag_hash_key_cal_hash_val(const struct tag_hash_key *tag, unsigned seed) {
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