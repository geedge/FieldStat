#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>   
#include <assert.h>

#include "xxhash/xxhash.h"
#include "uthash.h"

#include "spread_sketch.h"
#include "hll_common.h"
#include "exdata.h"

// todo：把primary metric 记到sketch 里，且使用特殊的st Hyperloglog
// todo: topk 也是这样
/*
primary metric：认为primary metric 的precision 设置是无效的，在创建spread sketch 时，就已经设置了hll。
这样做的好处：如果不这样，而是在set primary时，那么要提供接口“替换所有hll”，这使得set primary metric 接口的职责变成了：1. 将ss 中的hll 删除。2. 将cell 中的metric结构替换 3. 读取配置并产生新的方法。太复杂了。
实际上，关键在于，set primary metric 的时候，并没有register metric，所以create cube 和set primary metric 一定是两个接口，而create cube 的时候，明明应该已经创建了hll。

方案2：
修改set primary metric 的逻辑，一个cube上只能调用一次，且必须调用一次。（选用这个）
-----------------------------------------------------------------------------------------------
关于添加hll 的过程：
当是primary metric 的时候，不操作metric。而是走一个一般的spread sketch流程。query时采用新逻辑。


在swarm kv 里，spread sketch 需要支持serialize 操作。也就是最好提供一个serialize方法。但是这个东西不用怎么测，实际上不会出现。
*/

/*
把primary metric 加到 spread sketch 的问题。确认一些实现上不舒服的地方

对于comprehensive 和 topk 的cube，创建cube 时，cube 获得了创建primary metric 的全部参数，但是spread sketch内部的precision 是在fieldstat_cube_set_primary_metric 时确定的。准备提供一个spread_sketch_change_configuration() 接口，在fieldstat_cube_set_primary_metric 时，比较默认的precision与primary metric 的precision的值，如果不同，调用此接口，删除所有的Hyperloglog 并重新创建。

fieldstat输出时，产生的Hyperloglog结果是一个序列化的base64字符串。需要提供一个spread_sketch_query_as_base64接口，用于输出spread sketch 内部的Hyperloglog结构。因为spread sketch 内的sketch每行都创建了一个Hyperloglog，所以primary metric 的base64查询，可能对应最多 r 个不同的查询结果，我会先遍历调用hll_count，查询计数最小的行数，再把对应最小计数的Hyperloglog 序列化。

swarmkv 内需要spread_sketch_serialize接口，因为两遍的spread sketch必须一样，所以多增加一个exdata_serialize 回调。deserialize 时，需要额外传入一组回调函数，用来对spread sketch初始化，也可以不传入（分为spread_skech_deserialze和spread_skech_deserialze_with_exdata 两个函数)，认为不存在exdata，所有新的exdata 是NULL。
*/
/*
1. 修改set primary metric 为 set sampling
2. 增加一个list key 和一堆根据key 查base64之类的接口
3. serialize 不用实现带exdata的情况
*/

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
    long long last_update_ms; // linux timestamp since Jan 1st 1970
    uint32_t *sthll_register;
};

struct spread_sketch {
    int depth;
    int width;
    long long time_window_ms;
    unsigned char precision;

    // shared states of all sthlls
    long long reset_idx;
    struct timeval reset_time;

    struct spread_sketch_scheme scheme;
    
    struct bucket *buckets;
    struct entry_table *table;

    int level_cnt[65]; // 64: the level range is [1,65] // count every level number。
    uint32_t level_min; // the minimum level in the sketch
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


uint32_t cal_true_level(const struct spread_sketch *ss, int bucket_idx, long long t) {
    /*
    return f(t), the actual level of bucket, which satisfy:
    1. d 2^f(t)/dt is constants
    2.  f(t0 + T) = 1
    3. f((t0) = L )
    */
   struct bucket *bucket = &ss->buckets[bucket_idx];
    unsigned L = bucket->level;
    long long T = ss->time_window_ms;
    long long t0 = bucket->last_update_ms;

    assert(t >= t0);
    if (t - t0 >= T) {
        return 1;
    }
    if (L <= 1) {
        return L;
    }

    long long tmp_exp = 1 << L;
    double a = ((double)(2 - tmp_exp)) / ((double)T);
    double b = (double)(tmp_exp);

    return (uint32_t)(log2(a * ((double)(t-t0)) + b) + 0.5);
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

struct spread_sketch *spread_sketch_new(int depth, int width, unsigned char precision, int time_window_ms, struct timeval now) {
    struct spread_sketch *pthis = malloc(sizeof(struct spread_sketch));

    pthis->depth = depth;
    pthis->width = width;
    pthis->time_window_ms = time_window_ms;
    pthis->precision = precision;

    pthis->reset_idx = 0;
    pthis->reset_time = now;

    pthis->buckets = calloc(depth * width, sizeof(struct bucket));
    pthis->scheme.new_fn = default_new_fn;
    pthis->scheme.free_fn = default_free_fn;
    pthis->scheme.merge_fn = default_merge_fn;
    pthis->scheme.reset_fn = default_reset_fn;
    pthis->scheme.copy_fn = default_copy_fn;
    pthis->table = smart_ptr_table_new();
    pthis->table->scheme = pthis->scheme;

    for (int i = 0; i < depth * width; i++) {
        pthis->buckets[i].sthll_register = hll_create_register(precision);
    }

    pthis->level_min = 0;
    memset(pthis->level_cnt, 0, sizeof(pthis->level_cnt));
    pthis->level_cnt[0] = pthis->depth * pthis->width;

    return pthis;
}

void move_registers_forward(struct spread_sketch *ss, struct timeval *now) {
    long long reset_reg_count = hll_get_reset_register_count(ss->precision, ss->time_window_ms, *now, &ss->reset_time);
    if (reset_reg_count > 0) {
        for (int i = 0; i < ss->depth * ss->width; i++) {
            hll_reset_registers(ss->buckets[i].sthll_register, ss->precision, ss->reset_idx, reset_reg_count);
        }
        hll_advance_reset_index(&ss->reset_idx, reset_reg_count, ss->precision);
    }
}

void min_level_state_update(struct spread_sketch *ss, uint32_t level_old, uint32_t level_new) {
    ss->level_cnt[level_old]--;
    ss->level_cnt[level_new]++;
    while (ss->level_cnt[ss->level_min] == 0) {
        ss->level_min++;
    }
}

// return 0 if not added, return 1 if added
int spread_sketch_add(struct spread_sketch *ss, const char *key, size_t key_length, uint64_t item_hash, void *arg, , struct timeval now) {
    uint32_t level = (uint32_t)__builtin_clzll(item_hash) + 1;
    long long now_ms = now.tv_sec * 1000 + now.tv_usec / 1000;
    
    if (level <= ss->level_min) {
        if (item_hash == DUMMY_ITEM_HASH) {
            return 0; // DUMMY_ITEM_HASH is the case of "recording a key" instead of update hll. Just return 0 to inform the key is not added to the sketch
        }

        struct entry *content;
        HASH_FIND(hh, ss->table->entry, key, key_length, content);
        if (content != NULL && !content->dying) {
            return 1;
        } else {
            return 0;
        }
    }     
    // https://www.eecs.harvard.edu/~michaelm/postscripts/tr-02-05.pdf
    // A technique from the hashing literature is to use two hash functions h1(x) and h2(x) to simulate additional hash functions of the form gi(x) = h1(x) + ih2(x)
    // Assuming that the 128-bit xxhash function is perfect, we can view it as a combination of two 64-bit hash functions.
    uint64_t hash_x_tmp = XXH3_64bits_withSeed(key, key_length, 171);
    uint32_t hash_x1 = (uint32_t) (hash_x_tmp >> 32);
    uint32_t hash_x2 = (uint32_t) hash_x_tmp;

    bool in_sketch = false;
    move_registers_forward(ss, &now);
    for (int i = 0; i < ss->depth; i++) {
        uint32_t hash_x = hash_x1 + i * hash_x2; 
        int bucket_idx = (hash_x % ss->width) + i * ss->width;
        struct bucket *bucket = &ss->buckets[bucket_idx];

        if (bucket->content != NULL && key_equal(bucket->content->key, bucket->content->key_len, key, key_length)) {
            bucket->content->dying = false;
            bucket->last_update_ms = now_ms;

            if (bucket->level < level) {
                min_level_state_update(ss, bucket->level, level);
                bucket->level = level;
            }
            in_sketch = true;
        } else {
            uint32_t true_level = bucket->content == NULL ? 0: cal_true_level(ss, bucket_idx, now_ms);

            if (level > true_level) {
                min_level_state_update(ss, old_level, level);
                // printf("update key %s to %s, and level %u to %u, in bucket (r,w)=(%d,%u)\n", bucket->content == NULL ? "NULL": bucket->content->key, key, true_level, level, i, hash_x % ss->width);
                const struct entry *content_old = bucket->content;
                if (content_old != NULL) {
                    smart_ptr_table_release(ss->table, content_old->key, content_old->key_len);
                }
                struct entry *content_new = smart_ptr_table_get(ss->table, key, key_length, arg);
                bucket->content = content_new;

                bucket->last_update_ms = now_ms;
                bucket->level = level;

                in_sketch = true;
            }
        }
        hll_add_hash(bucket->sthll_register, ss->precision, item_hash);
    }

    return in_sketch ? 1 : 0;
}

double spread_sketch_point_query(const struct spread_sketch *ss, const char *key, size_t key_length) {
    uint64_t hash_x_tmp = XXH3_64bits_withSeed(key, key_length, 171);
    uint32_t hash_x1 = (uint32_t) (hash_x_tmp >> 32);
    uint32_t hash_x2 = (uint32_t) hash_x_tmp;

    double count_min = (double)INT32_MAX;
    for (int i = 0; i < ss->depth; i++) {
        uint32_t hash_x = hash_x1 + i * hash_x2; 
        int bucket_idx = (hash_x % ss->width) + i * ss->width;

        double est = hll_count(ss->buckets[bucket_idx].sthll_register, ss->precision, ss->reset_idx, ss->time_window_ms);
        if (est < count_min) {
            count_min = est;
        } 
    }
    return count_min;
}

struct spread_sketch *duplicate_and_step(const struct spread_sketch *ss, struct timeval *now) {
    struct spread_sketch *duplicate = malloc(sizeof(struct spread_sketch));
    duplicate->depth = ss->depth;
    duplicate->width = ss->width;
    duplicate->precision = ss->precision;
    duplicate->reset_idx = ss->reset_idx;
    duplicate->reset_time = ss->reset_time;
    duplicate->time_window_ms = ss->time_window_ms;
    duplicate->buckets = calloc(ss->depth * ss->width, sizeof(struct bucket));
    for (int i = 0; i < ss->depth * ss->width; i++) {
        duplicate->buckets[i].sthll_register = hll_duplicate(ss->buckets[i].sthll_register, ss->precision);
    }

    move_registers_forward(duplicate, now);
    return duplicate;
}

double spread_sketch_query(const struct spread_sketch *ss, const char *key, size_t key_length, struct timeval now) {    
    if (hll_should_slide(ss->precision, ss->time_window_ms, now, ss->reset_time)) {
        struct spread_sketch *duplicate = duplicate_and_step(ss, &now);
        double ret = spread_sketch_point_query(duplicate, key, key_length);
        spread_sketch_free(duplicate);
        return ret;
    } else {
        return spread_sketch_point_query(ss, key, key_length);
    }
}

void spread_sketch_free(struct spread_sketch *ss) {
    smart_ptr_table_free(ss->table);
    for (int i = 0; i < ss->depth * ss->width; i++) {
        hll_free_register(ss->buckets[i].sthll_register);
    }
    free(ss->buckets);
    free(ss);
}

void spread_sketch_merge(struct spread_sketch *dst, const struct spread_sketch *src)
{
    assert(dst->depth == src->depth && dst->width == src->width);
    assert(dst->time_window_ms == src->time_window_ms);
    assert(dst->precision == src->precision);

    for (int i = 0; i < dst->depth * dst->width; i++) {
        const struct bucket *bucket_src = &src->buckets[i];
        struct bucket *bucket_dst = &dst->buckets[i];

        if (bucket_src->content == NULL || bucket_src->content->dying) {
            continue;
        }

        if (bucket_dst->content == NULL) {
            bucket_dst->content = smart_ptr_table_get(dst->table, bucket_src->content->key, bucket_src->content->key_len, NULL);
            bucket_dst->level = bucket_src->level;
            bucket_dst->last_update_ms = bucket_src->last_update_ms;

            bucket_dst->sthll_register = hll_duplicate(bucket_src->sthll_register, dst->precision);
            min_level_state_update(dst, 0, bucket_src->level);
            continue;
        }
        bucket_dst->content->dying = false;

        if (key_equal(bucket_src->content->key, bucket_src->content->key_len, bucket_dst->content->key, bucket_dst->content->key_len)) {
            if (bucket_src->level > bucket_dst->level) {
                min_level_state_update(dst, bucket_dst->level, bucket_src->level);
                bucket_dst->level = bucket_src->level;
            }
            if (bucket_src->last_update_ms > bucket_dst->last_update_ms) {
                bucket_dst->last_update_ms = bucket_src->last_update_ms;
            }
        } else {
            uint32_t true_level_src = cal_true_level(src, i, bucket_src->last_update_ms);
            uint32_t true_level_dst = cal_true_level(dst, i, bucket_dst->last_update_ms);

            if (true_level_src > true_level_dst) {
                min_level_state_update(dst, bucket_dst->level, bucket_src->level);
                smart_ptr_table_release(dst->table, bucket_dst->content->key, bucket_dst->content->key_len);
                bucket_dst->content = smart_ptr_table_get(dst->table, bucket_src->content->key, bucket_src->content->key_len, NULL);
                bucket_dst->last_update_ms = bucket_src->last_update_ms;
            }
        }

        hll_merge(bucket_dst->sthll_register, src->buckets[i].sthll_register, dst->precision);
    }

    // for exdata
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
        hll_reset_registers(bucket->sthll_register, ss->precision, ss->reset_idx, 100000); // count is big enough
    }

    struct entry *content, *tmp;
    HASH_ITER(hh, ss->table->entry, content, tmp) {
        ss->scheme.reset_fn(content->exdata);
        content->dying = true;
    }

    memset(ss->level_cnt, 0, sizeof(ss->level_cnt));
    ss->level_cnt[0] = ss->depth * ss->width;
    ss->level_min = 0;
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

double spread_sketch_point_query


struct spread_sketch *spread_sketch_copy(const struct spread_sketch *src) {
    struct spread_sketch *dst = malloc(sizeof(struct spread_sketch));
    memcpy(dst, src, sizeof(struct spread_sketch)); // copy the depth, width, level_cnt, level_min

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
        dst->buckets[i].sthll_register = hll_duplicate(src->buckets[i].sthll_register, dst->precision);
    }
    return dst;
}

void spread_sketch_get_parameter_recommendation(int expected_super_spreader_number, int *depth_out, int *width_out, unsigned char *precision_out)
{
    int logk = expected_super_spreader_number >= 3200 ? 4 : 3; // lg3200 = 3.51,round up to 4
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

    *precision_out = 6;
}

void spread_sketch_get_parameter(const struct spread_sketch *ss, int *depth_out, int *width_out, unsigned char *precision_out, int *time_window_ms_out)
{
    *depth_out = ss->depth;
    *width_out = ss->width;
    *precision_out = ss->precision;
    *time_window_ms_out = ss->time_window_ms;
}

void spread_sketch_change_precision(struct spread_sketch *ss, unsigned char precision) {
    for (int i = 0; i < ss->depth * ss->width; i++) {
        hll_free_register(ss->buckets[i].sthll_register);
        ss->buckets[i].sthll_register = hll_create_register(precision);
    }
    ss->precision = precision;
}

void hyperloglog_serialize_for_networking(const struct hyperloglog *h, char **blob, size_t *blob_sz) {
    hyperloglog_serialize_for_networking(data->hll, blob, blob_size);
}

// todo: serialize(swarm kv)
// todo: swarm kv 上实现reset 和copy。