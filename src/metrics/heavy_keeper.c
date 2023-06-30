#include "heavy_keeper.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "minheap/heap.h"
#include "mpack/mpack.h"
#include "uthash.h"
#include "xxhash/xxhash.h"
#include "cjson/cJSON.h"

#include "utils/sorted_set.h"


struct Bucket {
    unsigned int finger_print;
    unsigned count; // the actual count, sum of all uuid_hash_node->count
};

struct metric_header {
    char **metric_names;
    size_t *name_len; // exclude '\0'
    size_t n_metric;
    size_t primary_metric_index;
};

struct heavy_keeper {
    int K;
    struct sorted_set *top_K_heap;
    struct heavy_keeper_options params;
    struct Bucket *sketch;

    struct metric_header *metric_header;
};

#define FP_HASH_KEY 0

struct Bucket *new_sketch(struct heavy_keeper_options *params) {
    size_t array_len = (size_t)params->array_num * (size_t)params->max_bucket_num;
    size_t alloc_size = array_len * sizeof(struct Bucket);
    struct Bucket *ret = (struct Bucket *)malloc(alloc_size);
    memset(ret, 0, alloc_size);

    return ret;
}

void params_set_to_default(struct heavy_keeper_options *p, int K) {
    double log_ret = log((double)K) / 2.3;  // 2.3: log_e(10), log_ret = log_10(K)

    if (log_ret < 3) {
        p->array_num = 3;
    } else {
        p->array_num = (int)(log_ret);
    }
    p->decay_exp_rate = 1.17; // by test, 1.17 is the best
    p->max_bucket_num = (int)(log_ret * K * 2);
    if (p->max_bucket_num < 150) {
        p->max_bucket_num = 150;  // determined through test, too small max_bucket_num will let accuracy decrease severely.
    } else if (p->max_bucket_num > 600) {
        p->max_bucket_num = p->max_bucket_num / 4 + 450;
    }
}

void params_set_to(struct heavy_keeper_options *p, struct heavy_keeper_options *src) {
    if (src->array_num != 0) {
        p->array_num = src->array_num;
    }
    if (src->decay_exp_rate != 0) {
        p->decay_exp_rate = src->decay_exp_rate;
    }
    if (src->max_bucket_num != 0) {
        p->max_bucket_num = src->max_bucket_num;
    }
}

struct heavy_keeper *heavy_keeper_new(int max_query_num, struct heavy_keeper_options *params) {
    struct heavy_keeper *hk = (struct heavy_keeper *)malloc(sizeof(struct heavy_keeper));
    hk->K = max_query_num;

    hk->top_K_heap = sorted_set_new(max_query_num);

    params_set_to_default(&(hk->params), max_query_num);

    if (params != NULL) {
        params_set_to(&(hk->params), params);
    }
    hk->sketch = new_sketch(&(hk->params));
    return hk;
}

static void heavy_keeper_specify_metric_header(struct heavy_keeper *hk, const char *metric_name[], size_t name_str_len[], size_t n_metric, int primary_metric) {
    hk->metric_header = (struct metric_header *)malloc(sizeof(struct metric_header));
    hk->metric_header->n_metric = n_metric;
    hk->metric_header->primary_metric_index = primary_metric;
    hk->metric_header->metric_names = (char **)malloc(n_metric * sizeof(const char *));
    hk->metric_header->name_len = (size_t *)malloc(n_metric * sizeof(size_t));
    for (int i = 0; i < n_metric; i++) {
        hk->metric_header->metric_names[i] = (char *)malloc(name_str_len[i] + 1);
        memcpy(hk->metric_header->metric_names[i], metric_name[i], name_str_len[i]);
        hk->metric_header->metric_names[i][name_str_len[i]] = '\0';
        hk->metric_header->name_len[i] = name_str_len[i];
    }
}

static void heavy_keeper_free_metric_header(struct metric_header *header) {
    for (int i = 0; i < header->n_metric; i++) {
        free(header->metric_names[i]);
    }
    free(header->metric_names);
    free(header->name_len);
    free(header);
}

struct heavy_keeper *heavy_keeper_with_metrics_new(int max_query_num, const char *metric_name[], size_t n_metric, size_t primary_metric) {
    if (primary_metric > n_metric - 1) {
        printf("ERR: primary_metric index is out of range\n");
        return NULL;
    }
    printf("heavy_keeper_with_metrics_new enter\n");
    
    struct heavy_keeper *hk = heavy_keeper_new(max_query_num, NULL);
    size_t name_len[n_metric];
    for (int i = 0; i < n_metric; i++) {
        name_len[i] = strlen(metric_name[i]);
    }
    heavy_keeper_specify_metric_header(hk, metric_name, name_len, n_metric, primary_metric);

    sorted_set_hook_function_of_free_user_data(hk->top_K_heap, free);
    return hk;
}

void heavy_keeper_clear(struct heavy_keeper *hk) {
    memset(hk->sketch, 0, (size_t)hk->params.array_num * (size_t)hk->params.max_bucket_num * sizeof(struct Bucket));
    sorted_set_free(hk->top_K_heap);
    hk->top_K_heap = sorted_set_new(hk->K);
}

struct heavy_keeper_result *heavy_keeper_result_new(struct heavy_keeper *hk) {
    size_t expected_max_len = hk->K;
    struct heavy_keeper_result *stats_hd = (struct heavy_keeper_result *)malloc(sizeof(struct heavy_keeper_result));
    memset(stats_hd, 0, sizeof(struct heavy_keeper_result));
    stats_hd->count = (unsigned *)malloc(expected_max_len * sizeof(unsigned));
    stats_hd->key = (char **)malloc(expected_max_len * sizeof(char *));
    stats_hd->key_len = (size_t *)malloc(expected_max_len * sizeof(size_t));

    if (hk->metric_header == NULL) {
        return stats_hd;
    }

    stats_hd->metric_value_arr = (unsigned int **)malloc(expected_max_len * sizeof(unsigned int *));
    stats_hd->primary_metric_id = hk->metric_header->primary_metric_index;
    stats_hd->metric_name = (char **)malloc(hk->metric_header->n_metric * sizeof(char *));
    stats_hd->n_metric = hk->metric_header->n_metric;
    for (int i = 0; i < hk->metric_header->n_metric; i++) {
        stats_hd->metric_name[i] = (char *)malloc(hk->metric_header->name_len[i] + 1);
        memcpy(stats_hd->metric_name[i], hk->metric_header->metric_names[i], hk->metric_header->name_len[i]);
        stats_hd->metric_name[i][hk->metric_header->name_len[i]] = '\0';
    }

    return stats_hd;
}

void heavy_keeper_result_free(struct heavy_keeper_result *stats_hd) {
    for (int i = 0; i < stats_hd->n_key; i++) {
        free(stats_hd->key[i]);
    }

    free(stats_hd->count);
    free(stats_hd->key);
    free(stats_hd->key_len);

    if (stats_hd->metric_value_arr == NULL) {
        free(stats_hd);
        return;
    }

    for (int i = 0; i < stats_hd->n_metric; i++) {
        free(stats_hd->metric_name[i]);
    }
    for (int i = 0; i < stats_hd->n_key; i++) {
        free(stats_hd->metric_value_arr[i]);
    }

    free(stats_hd->metric_value_arr);
    free(stats_hd->metric_name);
    free(stats_hd);
}

void heavy_keeper_free(struct heavy_keeper *hk) {
    printf("free heavy keeper enter\n");
    sorted_set_free(hk->top_K_heap);
    free(hk->sketch);
    heavy_keeper_free_metric_header(hk->metric_header);
    free(hk);
}

/*********************************add and query*************************************/
/***********************************************************************************/
bool if_need_to_decay(struct heavy_keeper *hk, struct Bucket *bucket, unsigned count) {
    double exp = (double)bucket->count / count;
    if (exp > 250) {  // decay_exp_rate ^ (-250) is very small, so regard the chance as 0.
        return false;
    }

    double r = (double)rand() / (double)RAND_MAX;

    double b = hk->params.decay_exp_rate;
    double decay_rate = pow(b, -exp);

    if (r < decay_rate) {
        return true;
    }
    return false;
}

static uint64_t __fnv_1a(const char *key, size_t len, int seed) {
    // FNV-1a hash (http://www.isthe.com/chongo/tech/comp/fnv/)
    size_t i;
    uint32_t h = 2166136261UL + (seed * 15);  // FNV_OFFSET 32 bit with magic number seed
    for (i = 0; i < len; ++i) {
        h = h ^ (unsigned char)key[i];
        h = h * 16777619UL;  // FNV_PRIME 32 bit
    }
    return h;
}

unsigned hash_xxhash_flow_id(const char *key, size_t key_len, unsigned seed) {
    // XXH64_hash_t res_64 = XXH3_64bits_withSeed(key, key_len + 1, seed);
    // return res_64 >> 32;
    // unsigned long int res = XXH32(key, key_len + 1, seed);
    // return res;
    return __fnv_1a(key, key_len, seed);
}

struct Bucket *map_flow_id_hash_to_bucket(struct heavy_keeper *hk, int arrar_index, const char *flow_id, size_t flow_id_len, unsigned int already_hashed_flow_id) {
    int w = hk->params.max_bucket_num;
    int Hsh;
    if (arrar_index == FP_HASH_KEY) {
        Hsh = already_hashed_flow_id % w;
    } else {
        Hsh = hash_xxhash_flow_id(flow_id, flow_id_len, arrar_index) % w;
    }

    return &(hk->sketch[arrar_index * w + Hsh]);
}

unsigned count_add(unsigned a, unsigned b) {
    if (a > UINT32_MAX - b) {
        return UINT32_MAX;
    }
    return a + b;
}

unsigned my_max(unsigned a, unsigned b) {
    if (a > b) {
        return a;
    }
    return b;
}

void heavy_keeper_add(struct heavy_keeper *heavy_keeper, const char *key, size_t key_len, unsigned int count) {
    if (key_len == 0) {
        printf("key_len should not be 0\n");
        return;
    }

    struct sorted_set *summary = heavy_keeper->top_K_heap;
    unsigned int FP = hash_xxhash_flow_id(key, key_len, FP_HASH_KEY);
    unsigned nMin = sorted_set_get_min_score(summary);
    unsigned maxv = 0;

    for (int j = 0; j < heavy_keeper->params.array_num; j++) {
        struct Bucket *bucket = map_flow_id_hash_to_bucket(heavy_keeper, j, key, key_len, FP);

        if (bucket->finger_print == FP) {
            // If a flow is not in the min-heap, then the estimated flow size should be no larger than nmin.
            // The flows whose counts are both larger than nmin and not in min-heap must have the same xxhash value, where its FP stored in bucket represents another flowid,
            // In this case, the sketch won't be updated. This flow is expected to be taken into account in another array,
            // where its FP is different from the one it should collided with so element flows won't be missed.
            if (bucket->count > nMin && sorted_set_get_member_score(summary, key, key_len) == 0) {
                    continue;
            }
            bucket->count = count_add(bucket->count, count);
            maxv = my_max(maxv, bucket->count);
        } else {
            if (!if_need_to_decay(heavy_keeper, bucket, count)) {
                continue;
            }
            if (bucket->count < count) {
                bucket->finger_print = FP;
                bucket->count = count;

                maxv = my_max(maxv, count);
            } else {
                bucket->count -= count;
            }
        }
    }

    unsigned int tmp_cnt = sorted_set_get_member_score(summary, key, key_len);
    if (tmp_cnt == 0) {  // the flow is not in sorted set
        if ((maxv - nMin <= count && maxv != nMin) || sorted_set_cardinality(summary) != heavy_keeper->K) {
            sorted_set_insert(summary, key, key_len, maxv);
        }
    } else {
        if (maxv > tmp_cnt) {
            sorted_set_incrby(summary, key, key_len, maxv - tmp_cnt);
        }
    }
}

int heavy_keeper_with_metrics_add(struct heavy_keeper *hk, const char *key, size_t key_len, unsigned int count[])
{
    if (hk->metric_header == NULL) {
        printf("ERR: metric_header is NULL, please call heavy_keeper_new_list instead of heavy_keeper_new.\n");
        return -1;
    }
    printf("heavy_keeper_with_metrics_add enter\n");
    size_t primary_id = hk->metric_header->primary_metric_index;
    heavy_keeper_add(hk, key, key_len, count[primary_id]);

    unsigned int **pst_other_count_in_rec = (unsigned int **)sorted_set_get_pointer_to_user_data(hk->top_K_heap, key, key_len);
    if (pst_other_count_in_rec == NULL) { // the key is not added into sorted set
        return 0;
    }

    // fill the other metrics to sorted set
    size_t metric_num = hk->metric_header->n_metric;
    unsigned int *count_to_add = (unsigned int *)malloc(metric_num * sizeof(unsigned int));
    memcpy(count_to_add, count, metric_num * sizeof(unsigned int));
    printf("going to get pointer to user data\n");
    if (*pst_other_count_in_rec == NULL) {
        printf("change pointer\n");
        *pst_other_count_in_rec = count_to_add;
    } else {
        printf("rewrite pointer\n");
        for (int i = 0; i < metric_num; i++) {
            (*pst_other_count_in_rec)[i] = count_add((*pst_other_count_in_rec)[i], count_to_add[i]);
        }
        free(count_to_add);
    }

    return 0;
}

struct heavy_keeper_result *heavy_keeper_query(struct heavy_keeper *hk) {
    struct heavy_keeper_result *stats  = heavy_keeper_result_new(hk);

    int heap_entry_num = sorted_set_cardinality(hk->top_K_heap);
    unsigned *keys = (unsigned *)malloc(heap_entry_num * sizeof(unsigned));
    char **vals = (char **)malloc(heap_entry_num * sizeof(char *));
    size_t *val_lens = (size_t *)malloc(heap_entry_num * sizeof(size_t));
    unsigned char **user_datas = (unsigned char **)malloc(heap_entry_num * sizeof(unsigned char *));
    sorted_set_dump(hk->top_K_heap, keys, vals, val_lens, (void **)user_datas);

    int query_entry_num = heap_entry_num > hk->K ? hk->K : heap_entry_num;
    stats->n_key = query_entry_num;
    int index_q = 0;
    for (int i = query_entry_num - 1; i >= 0; i--) {
        stats->key[index_q] = (char *)malloc(val_lens[i]);
        strcpy(stats->key[index_q], vals[i]);
        stats->count[index_q] = keys[i];
        stats->key_len[index_q] = val_lens[i] - 1;

        if (hk->metric_header != NULL) {
            stats->metric_value_arr[index_q] = (unsigned int *)malloc(hk->metric_header->n_metric * sizeof(unsigned int));
            memcpy(stats->metric_value_arr[index_q], user_datas[i], hk->metric_header->n_metric * sizeof(unsigned int));
        }
        index_q++;
    }

    free(keys);
    free(vals);
    free(val_lens);
    free(user_datas);
    return stats;
}

/*****************************************merge *****************************************/
/****************************************************************************************/
static void heavy_keeper_merge_sketch(struct heavy_keeper *dest, const struct heavy_keeper *src) {
    int w = dest->params.max_bucket_num;
    int d = dest->params.array_num;
    //idx
    for (int array_id = 0; array_id < d; array_id++) {
        for (int bucket_id = 0; bucket_id < w; bucket_id++) {
            struct Bucket *bucket_dest = &(dest->sketch[array_id * w + bucket_id]);
            struct Bucket *bucket_src = &(src->sketch[array_id * w + bucket_id]);

            if (bucket_dest->finger_print == bucket_src->finger_print) {
                // printf("bucket at %d,%d fp %u equal, bucket point: %p, bucket_dest C(%llu) + bucket_src C(%llu)\n",array_id, bucket_id, bucket_dest->finger_print, bucket_dest, bucket_dest->count,bucket_src->count  );
                bucket_dest->count += bucket_src->count;
            } else {
                if (bucket_dest->count < bucket_src->count) {  // bucket_src stores the elephant flow.
                    // printf("write bucket_src: FP = %u, C = %llu to bucket 1. position: %d,%d\n", bucket_src->finger_print, bucket_src->count,array_id, bucket_id);
                    bucket_dest->count = bucket_src->count;
                    bucket_dest->finger_print = bucket_src->finger_print;
                }
            }
        }
    }
}

unsigned find_maxv_in_sketch(struct heavy_keeper *hk, const char *flow_id, int flow_id_len) {
    struct Bucket *bucket;
    unsigned fp = hash_xxhash_flow_id(flow_id, flow_id_len, FP_HASH_KEY);

    unsigned maxv = 0;
    for (int array_id = 0; array_id < hk->params.array_num; array_id++) {
        bucket = map_flow_id_hash_to_bucket(hk, array_id, flow_id, flow_id_len, hash_xxhash_flow_id(flow_id, flow_id_len, FP_HASH_KEY));  // hk->sketch is the merge of two. So just check one

        if (bucket->finger_print == fp) {
            maxv = my_max(maxv, bucket->count);
        }
    }

    return maxv;
}

void insert_to_sorted_set(struct sorted_set *ss, char *flow_id_str, int flow_id_len, unsigned count, unsigned int *other_counts, size_t metric_num) {
    int if_inserted = sorted_set_insert(ss, flow_id_str, flow_id_len, count);

    if (other_counts != NULL || if_inserted) {
        unsigned int **pst_other_count_in_rec = (unsigned int **)sorted_set_get_pointer_to_user_data(ss, flow_id_str, flow_id_len);
        if (*pst_other_count_in_rec != NULL) {
            printf("ERR: I didn't expect this to happen. Other count in rec is not NULL\n");
        }
        *pst_other_count_in_rec = (unsigned int *)malloc(sizeof(unsigned int) * metric_num);
        memcpy(*pst_other_count_in_rec, other_counts, sizeof(unsigned int) * metric_num);
    }
}

void heavy_keeper_merge_sorted_set(struct heavy_keeper *dest, const struct heavy_keeper *src) {
    struct sorted_set *new_rec = sorted_set_new(dest->K);
    sorted_set_hook_function_of_free_user_data(new_rec, free);

    int size_dest = sorted_set_cardinality(dest->top_K_heap);
    int size_src = sorted_set_cardinality(src->top_K_heap);
    int max_size = size_dest > size_src ? size_dest : size_src;
    unsigned *keys = (unsigned *)malloc(max_size * sizeof(unsigned));
    char **vals = (char **)malloc(max_size * sizeof(char *));
    size_t *val_lens = (size_t *)malloc(max_size * sizeof(size_t));
    unsigned int **user_datas = (unsigned int **)malloc(max_size * sizeof(unsigned int *));

    sorted_set_dump(dest->top_K_heap, keys, vals, val_lens, (void **)user_datas);

    char *flow_id_str=NULL;
    size_t flow_id_len=0;
    size_t metric_num = dest->metric_header->n_metric;

    for (int i = 0; i < size_dest; i++) {
        flow_id_str = vals[i];
        flow_id_len = val_lens[i] - 1;
        // printf("in dest1, index: %d, flow id: %s, len: %zu\n", i, flow_id_str, flow_id_len);

        unsigned maxv = find_maxv_in_sketch(dest, flow_id_str, flow_id_len);
        insert_to_sorted_set(new_rec, flow_id_str, flow_id_len, maxv, user_datas[i], metric_num);
    }

    sorted_set_dump(src->top_K_heap, keys, vals, val_lens, (void **)user_datas);

    for (int i = 0; i < size_src; i++) {
        flow_id_str = vals[i];
        flow_id_len = val_lens[i] - 1;

        if (sorted_set_get_member_score(dest->top_K_heap, flow_id_str, flow_id_len) != 0) {  // flows that are in dest sketch has been processed in the previous loop
            continue;
        }

        // the dest heavekeeper has been merged, so the maxv is the maxv in the merged sketch, instead of the dest sketch
        unsigned maxv = find_maxv_in_sketch(dest, flow_id_str, flow_id_len);
        insert_to_sorted_set(new_rec, flow_id_str, flow_id_len, maxv, user_datas[i], metric_num);
    }

    free(keys);
    free(vals);
    free(val_lens);
    free(user_datas);

    sorted_set_free(dest->top_K_heap);
    dest->top_K_heap = new_rec;
}

int heavy_keeper_merge(struct heavy_keeper *hk, const struct heavy_keeper *src) {
    if (hk->params.array_num != src->params.array_num ||
        hk->params.max_bucket_num != src->params.max_bucket_num ||
        hk->K != src->K) {
        printf("ERROR: parameters of two merged heavy keeper must be the same\n");
        return -1;
    }

    heavy_keeper_merge_sketch(hk, src);
    heavy_keeper_merge_sorted_set(hk, src);
    return 0;
}


/******************************* serialization *******************************/
/*Serialize the heavy keeper to a mpack string which is equivalent to a JSON
    The json: 
    {
    "type": "topk"
    "K": <value_of_K>,
    “sketch": [{"FP":<finger_print>, "C":counter}, ...]
    "primary_id":<primary_metric int>
    "metric_name": [<metric_name1>, <metric_name2>, ...]
    "ss": [
        {
            "member": <field_string>,
            "score": <primary_score>
            "m_score": [<score1>, <score2>, ...]
        },
        {(more elements in ss, the sorted set)}
        ...
        }
    ]
    }
}
***********************************************************************************/
int heavy_keeper_sorted_set_member_serialization(const char *val, size_t val_len, unsigned int score, unsigned int *count_arr, size_t count_arr_size, char **blob, size_t *blob_sz) {
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_sz);

    mpack_build_map(&writer);
    mpack_write_cstr(&writer, "member");
    mpack_write_str(&writer, val, val_len - 1);
    mpack_write_cstr(&writer, "score");
    mpack_write_u32(&writer, score);

    mpack_write_cstr(&writer, "m_score");
    mpack_build_array(&writer);
    for (size_t i = 0; i < count_arr_size; i++) {
        mpack_write_u32(&writer, count_arr[i]);
    }
    mpack_complete_array(&writer);

    mpack_complete_map(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        fprintf(stderr, "An error occurred in heavy_keeper_sorted_set_member_serialization!\n");
        return -1;
    }
    return 0;
}

int heavy_keeper_bucket_serialization(const struct Bucket *b, char **blob, size_t *blob_sz) {
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_sz);

    mpack_build_map(&writer);
    mpack_write_cstr(&writer, "C");
    mpack_write_u64(&writer, b->count);
    mpack_write_cstr(&writer, "fp");
    mpack_write_u32(&writer, b->finger_print);
    mpack_complete_map(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        fprintf(stderr, "An error occurred in heavy_keeper_bucket_serialization!\n");
        return -1;
    }
    return 0;
}

void heavy_keeper_sketch_serialization(const struct heavy_keeper *hk, mpack_writer_t *writer)
{
    size_t element_mpack_size = 0;
    char *element_mpack_buff = NULL;
    mpack_write_cstr(writer, "sketch");

    mpack_build_array(writer);
    size_t bucket_len = (size_t)hk->params.array_num * (size_t)hk->params.max_bucket_num;
    for (size_t i = 0; i < bucket_len; i++) {
        heavy_keeper_bucket_serialization(&hk->sketch[i], &element_mpack_buff, &element_mpack_size);
        mpack_write_object_bytes(writer, element_mpack_buff, element_mpack_size);
        free(element_mpack_buff);
        element_mpack_size = 0;
    }
    mpack_complete_array(writer);
}

void heavy_keeper_sorted_set_serialization(const struct heavy_keeper *hk, mpack_writer_t *writer)
{
    size_t element_mpack_size = 0;
    char *element_mpack_buff = NULL;
    mpack_write_cstr(writer, "ss");
    int heap_entry_num = sorted_set_cardinality(hk->top_K_heap);
    if (heap_entry_num == 0) { // write an empty array
        mpack_build_array(writer);
        mpack_complete_array(writer);
        return;
    }

    unsigned *key_score = (unsigned *)malloc(heap_entry_num * sizeof(unsigned));
    char **vals = (char **)malloc(heap_entry_num * sizeof(char *));
    size_t *val_lens = (size_t *)malloc(heap_entry_num * sizeof(size_t));
    unsigned int **user_data = (unsigned int **)malloc(heap_entry_num * sizeof(unsigned int *));
    sorted_set_dump(hk->top_K_heap, key_score, vals, val_lens, (void **)user_data);
    mpack_build_array(writer);
    for (size_t i = 0; i < heap_entry_num; i++) {
        heavy_keeper_sorted_set_member_serialization(vals[i], val_lens[i], key_score[i], user_data[i], hk->metric_header->n_metric, &element_mpack_buff, &element_mpack_size);
        mpack_write_object_bytes(writer, element_mpack_buff, element_mpack_size);
        free(element_mpack_buff);
        element_mpack_size = 0;
    }
    mpack_complete_array(writer);

    free(key_score);
    free(vals);
    free(val_lens);
    free(user_data);
}

void heavy_keeper_serialization(const struct heavy_keeper *hk, char **blob, size_t *blob_sz) {
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_sz);
    mpack_build_map(&writer);

    mpack_write_cstr(&writer, "type");
    mpack_write_cstr(&writer, "topk");
    mpack_write_cstr(&writer, "K");
    mpack_write_int(&writer, hk->K);

    mpack_write_cstr(&writer, "sketch");
    size_t sketch_len = hk->params.max_bucket_num * hk->params.array_num;
    mpack_write_bin(&writer, (const char *)hk->sketch, sizeof(struct Bucket) * sketch_len);

    mpack_write_cstr(&writer, "primary_id");
    mpack_write_u32(&writer, hk->metric_header->primary_metric_index);

    mpack_write_cstr(&writer, "metric_name");
    mpack_build_array(&writer);
    for (size_t i = 0; i < hk->metric_header->n_metric; i++) {
        mpack_write_cstr(&writer, hk->metric_header->metric_names[i]);
    }
    mpack_complete_array(&writer);

    // heavy_keeper_sketch_serialization(hk, &writer);
    heavy_keeper_sorted_set_serialization(hk, &writer);

    mpack_complete_map(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        fprintf(stderr, "An error occurred encoding the data!\n");
    }
}

void heavy_keeper_sorted_set_deserialization(mpack_node_t *root, struct sorted_set *out)
{
    mpack_node_t item;
    mpack_node_t array_sorted_set = mpack_node_map_cstr(*root, "ss");
    size_t sorted_set_size = mpack_node_array_length(array_sorted_set);
    for (size_t i = 0; i < sorted_set_size; i++) {
        item = mpack_node_array_at(array_sorted_set, i);
        mpack_node_t flow_id_str = mpack_node_map_cstr(item, "member");
        const char *val = mpack_node_str(flow_id_str);
        size_t val_len = mpack_node_strlen(flow_id_str);
        unsigned int score = mpack_node_u32(mpack_node_map_cstr(item, "score"));
        mpack_node_t score_array = mpack_node_map_cstr(item, "m_score");
        size_t score_array_len = mpack_node_array_length(score_array);
        unsigned int *m_score = (unsigned int *)malloc(score_array_len * sizeof(unsigned int)); // will be freed with sorted set
        for (size_t j = 0; j < score_array_len; j++) {
            m_score[j] = mpack_node_u32(mpack_node_array_at(score_array, j));
        }

        sorted_set_insert(out, val, val_len, score);
        sorted_set_insert_user_data(out, val, val_len, (void *)m_score);
    }
}

struct heavy_keeper *heavy_keeper_deserialization(const char *blob, size_t size) {
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, size);
    mpack_tree_parse(&tree);
    mpack_node_t root = mpack_tree_root(&tree);

    int K = mpack_node_int(mpack_node_map_cstr(root, "K"));
    printf("heavy_keeper_deserialization K: %d\n", K);

    struct heavy_keeper *hk = heavy_keeper_new(K, NULL);
    sorted_set_hook_function_of_free_user_data(hk->top_K_heap, free);

    mpack_node_t item_sketch = mpack_node_map_cstr(root, "sketch");
    size_t bin_size = mpack_node_bin_size(item_sketch);
    memcpy(hk->sketch, mpack_node_bin_data(item_sketch), bin_size);

    mpack_node_t array_metric_name = mpack_node_map_cstr(root, "metric_name");
    size_t n_metric = mpack_node_array_length(array_metric_name);
    const char *metric_name_to_write[n_metric];
    size_t metric_name_len[n_metric];
    for (size_t i = 0; i < n_metric; i++) {
        metric_name_to_write[i] = mpack_node_str(mpack_node_array_at(array_metric_name, i));
        metric_name_len[i] = mpack_node_strlen(mpack_node_array_at(array_metric_name, i));
    }
    heavy_keeper_specify_metric_header(hk, metric_name_to_write, metric_name_len, n_metric, mpack_node_u32(mpack_node_map_cstr(root, "primary_id")));

    heavy_keeper_sorted_set_deserialization(&root, hk->top_K_heap);

    if (mpack_tree_destroy(&tree) != mpack_ok) {
        fprintf(stderr, "An error occurred in heavy_keeper_deserialization!\n");
        return NULL;
    }
    return hk;
}

void heavy_keeper_merge_blob(struct heavy_keeper *hk, const char *blob, size_t blob_sz)
{
	struct heavy_keeper *to_merge=heavy_keeper_deserialization(blob, blob_sz);
	heavy_keeper_merge(hk, to_merge);
	heavy_keeper_free(to_merge);
	return;
}

int heavy_keeper_get_config_K(struct heavy_keeper *hk)
{
    return hk->K;
}
