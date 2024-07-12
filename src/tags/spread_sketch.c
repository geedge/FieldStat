#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>   
#include <assert.h>

#include "xxhash/xxhash.h"
#include "uthash.h"

#include "spread_sketch.h"
#include "exdata.h"

// todo：把primary metric 记到sketch 里，且使用特殊的st Hyperloglog

struct entry {
    int ref_count;
    void *exdata;
    bool dying;
    char *key;
    size_t key_len;
    UT_hash_handle hh;
};

struct spread_sketch_scheme {
    exdata_new_cb new_fn;
    exdata_free_cb free_fn;
    exdata_merge_cb merge_fn;
    exdata_reset_cb reset_fn;
    exdata_copy_cb copy_fn;
};

struct entry_table {
    struct entry *entry;

    struct spread_sketch_scheme scheme;
};

struct bucket {
    struct entry *content;
    uint32_t level;
};

struct spread_sketch {
    int depth;
    int width;

    struct spread_sketch_scheme scheme;
    
    struct bucket *buckets;
    struct entry_table *table;

    uint32_t *min_level_per_row; // TODO: 先看看性能吧, 之后再写。用来记录每行最小的level，从而跳过行数。对于64位的level，维持一个计数，额外使用64 r的空间，当一个最小位数的level 计数到0时，更新最小level。
    // TODO: 对比heavy keeper,不仅仅是跳过的问题，heavykeeper 无论什么情况，在输入0的时候都不会走sketch 更新。
};

static void *default_new_fn(void *arg) {
    return NULL;
}
static void default_free_fn(void *exdata) {
    return;
}
static void default_merge_fn(void *dest, void *src) {
    return;
}
static void default_reset_fn(void *exdata) {
    return;
} 
static void *default_copy_fn(void *exdata) {
    return exdata;
}
static inline bool key_equal(const char *key1, size_t key1_len, const char *key2, size_t key2_len) {
    if (key1_len != key2_len) {
        return false;
    }
    return memcmp(key1, key2, key1_len) == 0;
}
static inline char *key_dup(const char *key, size_t key_len) {
    char *ret = malloc(key_len+1);
    memcpy(ret, key, key_len);
    ret[key_len] = '\0';
    return ret;
}

struct entry *smart_ptr_table_get(struct entry_table *table, const char *key, size_t key_len, void *arg) {
    struct entry *ret;
    HASH_FIND(hh, table->entry, key, key_len, ret);

    if (ret != NULL) {
        ret->dying = false;
        ret->ref_count++;
    } else {
        ret = malloc(sizeof(struct entry));
        ret->dying = false;
        ret->ref_count = 1;
        ret->key = key_dup(key, key_len);
        ret->key_len = key_len;
        if (arg == NULL) {
            ret->exdata = NULL;
        } else {
            ret->exdata = table->scheme.new_fn(arg);
        }
        HASH_ADD_KEYPTR(hh, table->entry, ret->key, ret->key_len, ret);
    }

    return ret;
}

int smart_ptr_table_release(struct entry_table *table, const char *key, size_t key_len) {
    struct entry *ret;
    HASH_FIND(hh, table->entry, key, key_len, ret);
    if (ret == NULL) {
        return -1;
    }

    ret->ref_count--;
    if (ret->ref_count == 0) {
        // printf("release %s\n", key);
        HASH_DEL(table->entry, ret);
        table->scheme.free_fn(ret->exdata);
        free(ret->key);
        free(ret);
    }

    return 0;
}

void smart_ptr_table_free(struct entry_table *table) {
    struct entry *current, *tmp;
    HASH_ITER(hh, table->entry, current, tmp) {
        HASH_DEL(table->entry, current);
        table->scheme.free_fn(current->exdata);
        free(current->key);
        free(current);
    }
    free(table);
}

struct entry_table *smart_ptr_table_new() {
    struct entry_table *table = malloc(sizeof(struct entry_table));
    table->entry = NULL;

    table->scheme.new_fn = default_new_fn;
    table->scheme.free_fn = default_free_fn;
    table->scheme.merge_fn = default_merge_fn;
    table->scheme.reset_fn = default_reset_fn;
    table->scheme.copy_fn = default_copy_fn;

    return table;
}

void get_parameter_recommendation(int max_super_spreader_number, int *depth_out, int *width_out)
{
    int logk = max_super_spreader_number >= 3200 ? 4 : 3; // lg3200 = 3.51,round up to 4
    *depth_out = logk;

    int w;
    if (max_super_spreader_number <= 100) {
        w = max_super_spreader_number * 3 /2; // * 1.5, when the number is small, we need more width
    } else {
        w = max_super_spreader_number + 50; // + 50: 100*1.5-100 = 50, make w=f(k) continuous
    }
    if (w < 40) {
        w = 40;
    }
    *width_out = w;
}

struct spread_sketch *spread_sketch_new(int expected_query_num) {
    struct spread_sketch *pthis = malloc(sizeof(struct spread_sketch));
    get_parameter_recommendation(expected_query_num, &pthis->depth, &pthis->width);

    pthis->buckets = calloc(pthis->depth * pthis->width, sizeof(struct bucket));
    pthis->scheme.new_fn = default_new_fn;
    pthis->scheme.free_fn = default_free_fn;
    pthis->scheme.merge_fn = default_merge_fn;
    pthis->scheme.reset_fn = default_reset_fn;
    pthis->scheme.copy_fn = default_copy_fn;
    pthis->table = smart_ptr_table_new();
    pthis->table->scheme = pthis->scheme;

    return pthis;
}

// return 0 if not added, return 1 if added
int spread_sketch_add(struct spread_sketch *ss, const char *key, size_t key_length, uint64_t item_hash, void *arg) {
    // uint64_t hash_identifier = XXH3_64bits_withSeed(identifier, identifier_length, 171);
    uint32_t level = (uint32_t)__builtin_clzll(item_hash) + 1;
    // printf("spread_sketch_add key %s, level %u\n", key, level);
    
    // https://www.eecs.harvard.edu/~michaelm/postscripts/tr-02-05.pdf
    // A technique from the hashing literature is to use two hash functions h1(x) and h2(x) to simulate additional hash functions of the form gi(x) = h1(x) + ih2(x)
    // Assuming that the 128-bit xxhash function is perfect, we can view it as a combination of two 64-bit hash functions.
    uint64_t hash_x_tmp = XXH3_64bits_withSeed(key, key_length, 171);
    uint32_t hash_x1 = (uint32_t) (hash_x_tmp >> 32);
    uint32_t hash_x2 = (uint32_t) hash_x_tmp;

    bool in_sketch = false;
    for (int i = 0; i < ss->depth; i++) {
        uint32_t hash_x = hash_x1 + i * hash_x2; 
        int bucket_idx = (hash_x % ss->width) + i * ss->width;
        struct bucket *bucket = &ss->buckets[bucket_idx];

        if (bucket->content != NULL && key_equal(bucket->content->key, bucket->content->key_len, key, key_length)) {
            bucket->content->dying = false;

            if (bucket->level < level) {
                bucket->level = level;
            }
            in_sketch = true;
        } else {
            uint32_t old_level = bucket->content == NULL ? 0: bucket->level;

            if (level > old_level) {
                // printf("update key %s to %s, and level %u to %u, in bucket (r,w)=(%d,%u)\n", bucket->content == NULL ? "NULL": bucket->content->key, key, old_level, level, i, hash_x % ss->width);
                const struct entry *content_old = bucket->content;
                if (content_old != NULL) {
                    smart_ptr_table_release(ss->table, content_old->key, content_old->key_len);
                }
                struct entry *content_new = smart_ptr_table_get(ss->table, key, key_length, arg);
                bucket->content = content_new;
                bucket->level = level;

                in_sketch = true;
            }
        }
    }

    return in_sketch ? 1 : 0;
}

void spread_sketch_free(struct spread_sketch *ss) {
    smart_ptr_table_free(ss->table);
    free(ss->buckets);
    free(ss);
}

void spread_sketch_merge(struct spread_sketch *dst, const struct spread_sketch *src)
{
    assert(dst->depth == src->depth && dst->width == src->width);

    for (int i = 0; i < dst->depth * dst->width; i++) {
        const struct bucket *bucket_src = &src->buckets[i];
        struct bucket *bucket_dst = &dst->buckets[i];

        if (bucket_src->content == NULL || bucket_src->content->dying) {
            continue;
        }
        if (bucket_dst->content == NULL) {
            bucket_dst->content = smart_ptr_table_get(dst->table, bucket_src->content->key, bucket_src->content->key_len, NULL);
            bucket_dst->level = bucket_src->level;
            continue;
        }
        bucket_dst->content->dying = false;

        if (key_equal(bucket_src->content->key, bucket_src->content->key_len, bucket_dst->content->key, bucket_dst->content->key_len)) {
            if (bucket_src->level > bucket_dst->level) {
                bucket_dst->level = bucket_src->level;
            }
        } else {
            if (bucket_src->level > bucket_dst->level) {
                smart_ptr_table_release(dst->table, bucket_dst->content->key, bucket_dst->content->key_len);
                bucket_dst->content = smart_ptr_table_get(dst->table, bucket_src->content->key, bucket_src->content->key_len, NULL);
                bucket_dst->level = bucket_src->level;
            }
        }
    }

    const struct spread_sketch_scheme *scheme = &dst->table->scheme;

    struct entry *content_dest, *content_src, *tmp;
    HASH_ITER(hh, dst->table->entry, content_dest, tmp) {
        HASH_FIND(hh, src->table->entry, content_dest->key, content_dest->key_len, content_src);
        if (content_src == NULL || content_src->dying) {
            continue;
        }

        if (content_dest->exdata == NULL) {
            content_dest->exdata = scheme->copy_fn(content_src->exdata);
        } else {
            scheme->merge_fn(content_dest->exdata, content_src->exdata);
        }
    }
}

void *spread_sketch_get0_exdata(const struct spread_sketch *ss, const char *key, size_t key_len) {
    struct entry *content;
    HASH_FIND(hh, ss->table->entry, key, key_len, content);
    if (content == NULL || content->dying) {
        return NULL;
    }
    return content->exdata;
}

void spread_sketch_reset(struct spread_sketch *ss) {
    for (int i = 0; i < ss->depth * ss->width; i++) {
        struct bucket *bucket = &ss->buckets[i];
        bucket->level = 0;
    }

    struct entry *content, *tmp;
    HASH_ITER(hh, ss->table->entry, content, tmp) {
        ss->scheme.reset_fn(content->exdata);
        content->dying = true;
    }
}

void spread_sketch_set_exdata_schema(struct spread_sketch *ss, exdata_new_cb new_fn, exdata_free_cb free_fn, exdata_merge_cb merge_fn, exdata_reset_cb reset_fn, exdata_copy_cb copy_fn) {
    ss->scheme.new_fn = new_fn;
    ss->scheme.free_fn = free_fn;
    ss->scheme.merge_fn = merge_fn;
    ss->scheme.reset_fn = reset_fn;
    ss->scheme.copy_fn = copy_fn;

    ss->table->scheme = ss->scheme;
}

int spread_sketch_get_count(const struct spread_sketch *ss) {
    int cnt = 0;
    struct entry *content, *tmp;
    HASH_ITER(hh, ss->table->entry, content, tmp) {
        if (!content->dying) {
            cnt++;
        }
    }

    return cnt;
}

size_t spread_sketch_list(const struct spread_sketch *ss, void **exdatas, size_t n_exdatas) {
    size_t count = 0;
    struct entry *content, *tmp;
    HASH_ITER(hh, ss->table->entry, content, tmp) {
        if (content->dying) {
            continue;
        }
        if (count >= n_exdatas) {
            break;
        }
        exdatas[count] = content->exdata;
        count++;
    }
    return count;
}

struct spread_sketch *spread_sketch_copy(const struct spread_sketch *src) {
    struct spread_sketch *dst = malloc(sizeof(struct spread_sketch));
    memcpy(dst, src, sizeof(struct spread_sketch));

    dst->buckets = calloc(dst->depth * dst->width, sizeof(struct bucket));
    dst->table = smart_ptr_table_new();
    spread_sketch_set_exdata_schema(dst, src->scheme.new_fn, src->scheme.free_fn, src->scheme.merge_fn, src->scheme.reset_fn, src->scheme.copy_fn);

    for (int i = 0; i < dst->depth * dst->width; i++) {
        if (src->buckets[i].content == NULL || src->buckets[i].content->dying) {
            continue;
        }
        dst->buckets[i].content = smart_ptr_table_get(dst->table, src->buckets[i].content->key, src->buckets[i].content->key_len, NULL);
        dst->buckets[i].level = src->buckets[i].level;
        if (dst->buckets[i].content->exdata == NULL) {
            dst->buckets[i].content->exdata = src->scheme.copy_fn(src->buckets[i].content->exdata);
        }
    }
    return dst;
}
