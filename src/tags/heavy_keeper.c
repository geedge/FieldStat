#include "heavy_keeper.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "minheap/heap.h"
#include "mpack/mpack.h"
#include "uthash.h"
#include "xxhash/xxhash.h"

#include "exdata.h"

#define FP_HASH_KEY 0
#define INVALID_COUNT UINT64_MAX
#define MAX(a, b)  	(((a) > (b)) ? (a) : (b))

struct entry_data { // the value constitute of a sorted set entry
    char *key;
    size_t key_len;

    void *exdata;
};

struct sorted_set {
    heap *heap;
    struct hash_node *hash_tbl;

    int n_living_entry;

    exdata_new_cb new_fn;
    exdata_free_cb free_fn;
    exdata_merge_cb merge_fn;
    exdata_reset_cb reset_fn;
    exdata_copy_cb copy_fn;
};

struct hash_node {
    const char *key; // value is equal to entry_data::key(point to the same memory)
    size_t key_len;
    heap_entry *val;
    UT_hash_handle hh;
};


struct Bucket {
    unsigned int finger_print;
    unsigned long long count; // the actual count, sum of all uuid_hash_node->count
};

// Parameters used in algorithm
struct heavy_keeper_options{
    int array_num;  // the size of the array. Default value: 4
                    // Set it by how accurate you want. Value of 4 guarantees an accuracy more than 90% and around 97% in all tests.
                    // Not too big because it have an impact on both time and memory efficiency.
    int max_bucket_num; // M2, the maximum number of buckets every array keeps. Default value: k*log(k) and no less than 100. 
                        // Basically, as long as big enough, it won't affect the accuracy significantly.
    double decay_exp_rate; // b, bigger variance of flow size is(elephant flows take more ratio), smaller it should be.
                           //  Must bigger than 1. Better not bigger than 1.3, otherwise some elephant flow will be missed.
};

struct heavy_keeper {
    int K;
    struct sorted_set *top_K_heap;
    struct heavy_keeper_options params;
    struct Bucket *sketch;

    unsigned int rand_state;

    exdata_new_cb new_fn;
    exdata_free_cb free_fn;
    exdata_merge_cb merge_fn;
    exdata_reset_cb reset_fn;
    exdata_copy_cb copy_fn;
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

static char *my_key_dup(const char *key, size_t key_len) {
    char *ret = (char *)malloc(key_len);
    memcpy(ret, key, key_len);
    return ret;
}

struct entry_data *entry_data_construct(const char *key, size_t key_len, void *exdata)
{
    struct entry_data *entry_data = (struct entry_data *)malloc(sizeof(struct entry_data));

    entry_data->key = my_key_dup(key, key_len);
    entry_data->key_len = key_len;
    entry_data->exdata = exdata;
    return entry_data;
}

void entry_data_destroy(struct entry_data *entry_data, exdata_free_cb free_fn)
{
    if (entry_data == NULL) {
        return;
    }
    free(entry_data->key);
    free_fn(entry_data->exdata);
    free(entry_data);
}

unsigned long long sorted_set_entry_get_count(const heap_entry *entry)
{
    unsigned long long count = *(unsigned long long *)entry->key;
    if (count == 0) {
        return 0;
    }
    return count - 1; // sorted set will let the count start from 1, 0 for dying entry.
}

struct entry_data *sorted_set_entry_get_data(const heap_entry *entry)
{
    return entry->value;
}

/*
    dying: To reduce the time of HASH_ADD, and to support merely recording a key without any value, give a dying status.
*/
static inline bool sorted_set_entry_dying(const heap_entry *entry)
{
    unsigned long long count = *(unsigned long long *)entry->key;
    return count == 0;    
}

// cppcheck-suppress [constParameterCallback, unmatchedSuppression]
static bool cmp_entry_by_count(void *aa, void *bb)
{
    heap_entry *a = (heap_entry *)aa;
    heap_entry *b = (heap_entry *)bb;
    unsigned long long count_a = *(unsigned long long *)a->key;
    unsigned long long count_b = *(unsigned long long *)b->key;

    if (count_a < count_b) {
        return true;
    }
    return false;
}

static inline unsigned long long safe_add(unsigned long long a, unsigned long long b)
{
    if (UINT64_MAX - a < b) {
        return UINT64_MAX;
    }
    return a + b;
}

static int my_entry_get_index(void *a)
{
    return ((heap_entry *)a)->index;
}

void my_entry_set_index(int index, void *a)
{
    ((heap_entry *)a)->index = index;
}

struct sorted_set *sorted_set_new(int set_size)
{
    struct sorted_set *ret = (struct sorted_set *)malloc(sizeof(struct sorted_set));
    ret->hash_tbl = NULL;
    ret->n_living_entry = 0;
    heap *h = (heap *)malloc(sizeof(heap));
    init_heap(h, set_size, cmp_entry_by_count, my_entry_get_index, my_entry_set_index);

    ret->heap = h;

    ret->new_fn = default_new_fn;
    ret->free_fn = default_free_fn;
    ret->merge_fn = default_merge_fn;
    ret->reset_fn = default_reset_fn;
    ret->copy_fn = default_copy_fn;

    return ret;
}

void sorted_set_schema_set(struct sorted_set *ss, exdata_new_cb new_fn, exdata_free_cb free_fn, exdata_merge_cb merge_fn, exdata_reset_cb reset_fn, exdata_copy_cb copy_fn)
{
    ss->new_fn = new_fn;
    ss->free_fn = free_fn;
    ss->merge_fn = merge_fn;
    ss->reset_fn = reset_fn;
    ss->copy_fn = copy_fn;
}

void sorted_set_free(struct sorted_set *ss)
{
    struct hash_node *tbl = ss->hash_tbl;
    struct hash_node *h_node, *tmp;
    HASH_ITER(hh, tbl, h_node, tmp) {
        HASH_DEL(tbl, h_node);  
        free(h_node);
    }

    heap *heap = ss->heap;
    heap_entry *node;
    for (int i = 0; i < heap->cur_size; i++) {
        node = (heap_entry *)heap->nodes[i];
        entry_data_destroy(node->value, ss->free_fn);
        free(node->key);
        free(node);
    }
    free(heap->nodes);
    free(heap);

    free(ss);
}

heap_entry *sorted_set_find_entry(const struct sorted_set *ss, const char *key, size_t key_len)
{
    struct hash_node *hash_tbl = ss->hash_tbl;
    struct hash_node *s = NULL;
    HASH_FIND(hh, hash_tbl, key, key_len, s);

    if (s == NULL) {
        return NULL;
    }
    return (heap_entry *)s->val;
}

void sorted_set_entry_set_key(struct sorted_set *ss, heap_entry *entry, unsigned long long count)
{
    *(unsigned long long *)(entry->key) = count;

    adjust_heap_node(ss->heap, entry);
}

int sorted_set_pop(struct sorted_set *ss)
{
    struct hash_node *hash_tbl = ss->hash_tbl;
    heap_entry *entry = (heap_entry *)pop_heap(ss->heap);
    if (entry == NULL) {
        return -1;
    }
    struct entry_data *tmp_data = sorted_set_entry_get_data(entry);

    struct hash_node *s = NULL;
    HASH_FIND(hh, hash_tbl, tmp_data->key, tmp_data->key_len, s);
    assert(s!=NULL);

    HASH_DEL(hash_tbl, s);
    free(s);
    ss->hash_tbl = hash_tbl;

    int ret;
    if (sorted_set_entry_dying(entry)) {
        ret = 0;
    } else {
        ss->n_living_entry--;
        ret = 1;
    }

    entry_data_destroy(tmp_data, ss->free_fn);
    free(entry->key);
    free(entry);

    return ret;
}

static inline bool sorted_set_check_is_full(const struct sorted_set *ss)
{
    return ss->heap->cur_size >= ss->heap->max_size;
}

unsigned long long sorted_set_get_min_count(const struct sorted_set *ss)
{   
    heap *heap = ss->heap;
    if (heap->cur_size == 0) {
        return INVALID_COUNT;
    }
    const heap_entry *ret = (heap_entry *)(heap->nodes[0]);

    unsigned long long count = *(unsigned long long *)ret->key;
    if (count == 0) {
        return INVALID_COUNT;
    }
    return count - 1; // sorted set will let the count start from 1, 0 for dying entry.
}

void sorted_set_insert_to_available_heap(struct sorted_set *ss, const char *key, size_t key_len, unsigned long long cnt, void *exdata)
{
    cnt = safe_add(cnt, 1); // sorted set will let the count start from 1, 0 for dying entry.
    unsigned long long *tmp_cnt = (unsigned long long*)malloc(sizeof(unsigned long long));
    *tmp_cnt = cnt;
    struct entry_data *tmp_data = entry_data_construct(key, key_len, exdata);
    heap_entry *node = (heap_entry *)malloc(sizeof(heap_entry));
    node->key = tmp_cnt;
    node->value = tmp_data;

    push_heap(ss->heap, (void *)node);

    struct hash_node *hash_tbl = ss->hash_tbl;
    struct hash_node *s = (struct hash_node *)malloc(sizeof(struct hash_node));
    s->key = sorted_set_entry_get_data(node)->key;
    s->key_len = key_len;
    s->val = node;
    HASH_ADD_KEYPTR(hh, hash_tbl, s->key, key_len, s);
    ss->hash_tbl = hash_tbl;

    ss->n_living_entry++;
}

int sorted_set_insert(struct sorted_set *ss, const char *key, size_t key_len, unsigned long long cnt, void *args)
{
    // if there is a dying record, reborn it to use.
    heap_entry *entry = sorted_set_find_entry(ss, key, key_len);
    if (entry != NULL) {
        if (!sorted_set_entry_dying(entry)) {
            assert(0);
            return -1;
        }
        sorted_set_entry_set_key(ss, entry, safe_add(cnt, 1)); // sorted set will let the count start from 1, 0 for dying entry.

        ss->n_living_entry++;
        return 1;
    }

    if (sorted_set_check_is_full(ss)) {
        unsigned long long tmp_mincnt = sorted_set_get_min_count(ss);
        if (tmp_mincnt != INVALID_COUNT && cnt <= tmp_mincnt) { // even if all cells in sorted set are dying, the sorted set can still be a full one, in which case, the min count is invalid.
            return 0;
        }
        sorted_set_pop(ss);
    }
    sorted_set_insert_to_available_heap(ss, key, key_len, cnt, ss->new_fn(args));
    return 1;
}

int sorted_set_cardinality(const struct sorted_set *ss)
{
    return ss->n_living_entry;
}

unsigned long long sorted_set_get_count(const struct sorted_set *ss, const char *key, size_t key_len)
{
    if (sorted_set_cardinality(ss) == 0) {
        return INVALID_COUNT;
    }
    const heap_entry *entry = sorted_set_find_entry(ss, key, key_len);
    if (entry == NULL) {
        return INVALID_COUNT;
    }
    if (sorted_set_entry_dying(entry)) {
        return INVALID_COUNT;
    }

    return sorted_set_entry_get_count(entry);
}

int sorted_set_incrby(struct sorted_set *ss, const char *key, size_t key_len, unsigned long long count)
{
    heap_entry *entry = sorted_set_find_entry(ss, key, key_len);
    if (entry == NULL) {
        return -1;
    }
    unsigned long long cnt_old;
    if (sorted_set_entry_dying(entry) == false) {
        cnt_old = sorted_set_entry_get_count(entry);
        cnt_old += 1; // sorted set will let the count start from 1, 0 for dying entry.
    } else {
        cnt_old = 0;
    }
    sorted_set_entry_set_key(ss, entry, safe_add(count, cnt_old));
    return 0;
}

typedef struct {
    unsigned long long key;
    struct entry_data *val;
} tmp_heap_node;

int cmp_tmp_heap_node(const void *a, const void *b)
{
    const tmp_heap_node *aa = (tmp_heap_node *)a;
    const tmp_heap_node *bb = (tmp_heap_node *)b;

    if (aa->key < bb->key) {
        return 1;
    } else if (aa->key > bb->key) {
        return -1;
    } else {
        return 0;
    }
}

void sorted_set_dump(const struct sorted_set *ss, char **key_out, size_t *key_len_out, void **exdata_out)
{
    struct minheap *h = ss->heap;
    tmp_heap_node *tmp_nodes = (tmp_heap_node *)malloc(sizeof(tmp_heap_node) * h->cur_size);
    size_t n_living_entry = 0;
    for (int i = 0; i < h->cur_size; i++) {
        const heap_entry *entry = (heap_entry *)h->nodes[i];
        if (sorted_set_entry_dying(entry)) {
            continue;
        }
        tmp_nodes[n_living_entry].key = sorted_set_entry_get_count(entry);
        tmp_nodes[n_living_entry].val = sorted_set_entry_get_data(entry);
        n_living_entry++;
    }

    assert(n_living_entry == ss->n_living_entry);
    qsort(tmp_nodes, n_living_entry, sizeof(tmp_heap_node), cmp_tmp_heap_node);
    for (int i = 0; i < n_living_entry; i++) {
        key_out[i] = tmp_nodes[i].val->key;
        key_len_out[i] = tmp_nodes[i].val->key_len;
        exdata_out[i] = tmp_nodes[i].val->exdata;
    }
    free(tmp_nodes);
}

struct sorted_set *sorted_set_copy(const struct sorted_set *ss)
{
    struct sorted_set *ret = sorted_set_new(ss->heap->max_size);
    sorted_set_schema_set(ret, ss->new_fn, ss->free_fn, ss->merge_fn, ss->reset_fn, ss->copy_fn);
    
    for (int i = 0; i < ss->heap->cur_size; i++) {
        const heap_entry *entry = (heap_entry *)ss->heap->nodes[i];
        if (sorted_set_entry_dying(entry)) {
            continue;
        }
        const struct entry_data *data = sorted_set_entry_get_data(entry);
        sorted_set_insert_to_available_heap(ret, data->key, data->key_len, sorted_set_entry_get_count(entry), ss->copy_fn(data->exdata));
    }

    return ret;
}

// set all the entry to dying status. They will be the first to be popped.
void sorted_set_reset(struct sorted_set *ss)
{
    heap *heap = ss->heap;
    heap_entry *entry;
    for (int i = 0; i < heap->cur_size; i++) {
        entry = (heap_entry *)heap->nodes[i];
        *(unsigned long long *)entry->key = 0;

        ss->reset_fn(sorted_set_entry_get_data(entry)->exdata);
    }

    ss->n_living_entry = 0;
}

/* -------------------------------------------------------------------------- */
/*                                heavy keeper                                */
/* -------------------------------------------------------------------------- */

struct Bucket *new_sketch(struct heavy_keeper_options *params) {
    size_t array_len = (size_t)params->array_num * (size_t)params->max_bucket_num;

    struct Bucket *ret = (struct Bucket *)calloc(array_len, sizeof(struct Bucket));

    return ret;
}

void params_set_to_default(struct heavy_keeper_options *p, int K) {
    if (K > 1000) { // don't let the sketch too large when K gets insanely large
        K = 1000;
    }
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


struct heavy_keeper *heavy_keeper_new(int max_query_num) {
    struct heavy_keeper *hk = (struct heavy_keeper *)malloc(sizeof(struct heavy_keeper));
    hk->K = max_query_num;
    hk->rand_state = 0;

    hk->top_K_heap = sorted_set_new(max_query_num);

    params_set_to_default(&(hk->params), max_query_num);

    hk->sketch = new_sketch(&(hk->params));

    hk->new_fn = default_new_fn;
    hk->free_fn = default_free_fn;
    hk->merge_fn = default_merge_fn;
    hk->reset_fn = default_reset_fn;

    return hk;
}

void heavy_keeper_free(struct heavy_keeper *hk) {
    sorted_set_free(hk->top_K_heap);
    free(hk->sketch);

    free(hk);
}

void heavy_keeper_reset(struct heavy_keeper *hk) {
    sorted_set_reset(hk->top_K_heap);
    memset(hk->sketch, 0, sizeof(struct Bucket) * (size_t)hk->params.array_num * (size_t)hk->params.max_bucket_num);
}

const int DECAY_POW_TABLE[128] = { // 1.17 ^ exp * RAND_MAX, exp is in [0,127]
2147483647,1835456109,1568765905,1340825560,1146004752,979491241,837172001,715531625,611565491,522705548,446756879,381843486,326361954,278941841,238411830,203770795,174163072,148857327,127228484,108742294,92942132,
79437720,67895487,58030331,49598573,42391943,36232430,30967889,26468281,22622462,19335438,16526015,14124799,12072478,10318357,8819109,7537700,6442479,5506392,4706318,4022494,3438029,2938486,2511527,2146604,
1834704,1568123,1340276,1145535,979090,836829,715239,611315,522491,446574,381687,326228,278828,238314,203687,174092,148796,127176,108698,92904,79405,67868,58007,49578,42375,36218,30955,26457,22613,19328,16519,
14119,12068,10314,8815,7535,6440,5504,4704,4021,3437,2937,2510,2146,1834,1567,1340,1145,979,836,715,611,522,446,382,326,279,238,204,174,149,127,109,93,79,68,58,50,42,36,31,26,23,19,17,14,12,10,9,8,6,6,5,
};
bool if_need_to_decay(struct heavy_keeper *hk, const struct Bucket *bucket, unsigned long long count) {
    if (count == 0) {
        return false;
    }
    if (bucket->count < count) { // the exp is 0, so the decay rate is 1
        return true;
    }

    unsigned long long exp = bucket->count / count;
    if (exp > 127) {  // too small, almost no chance to decay 
        return false;
    }

    // double decay_rate = pow(hk->params.decay_exp_rate, -exp);
    // p->decay_exp_rate = 1.17 is fixed, search table to get result directly.
    int decay_rate = DECAY_POW_TABLE[exp];

    if (rand_r(&(hk->rand_state)) < decay_rate) {
        return true;
    }
    return false;
}

unsigned int cal_hash_val_with_seed(const char *key, size_t key_len, unsigned int seed) {
    return XXH3_64bits_withSeed(key, key_len, seed);
}

struct Bucket *find_bucket_by_key(struct heavy_keeper *hk, int array_index, const char *key, size_t key_len) {
    int w = hk->params.max_bucket_num;
    unsigned int Hsh = cal_hash_val_with_seed(key, key_len, array_index + 1) % w; // +1: the let any row do not use FP as hashing key directly.

    // Otherwise, when different key has the same FP(in 2^-64 probability), they will also in the same bucket in the row #0 and hash collision will happen more severely.

    return &(hk->sketch[array_index * w + Hsh]);
}


unsigned my_max(unsigned a, unsigned b) {
    if (a > b) {
        return a;
    }
    return b;
}

/*
1 for newly add something. 0 for not add. -1 for unexpected cases.
*/
int heavy_keeper_add(struct heavy_keeper *heavy_keeper, const char *key, size_t key_len, int count, void *arg) {
    if (count == 0) {
        if (sorted_set_cardinality(heavy_keeper->top_K_heap) < heavy_keeper->K) {
            const struct heap_entry *entry = sorted_set_find_entry(heavy_keeper->top_K_heap, key, key_len);

            if (entry != NULL && !sorted_set_entry_dying(entry)) {
                return 0;
            }

            sorted_set_insert(heavy_keeper->top_K_heap, key, key_len, count, arg);
            return 1;
        }
        return 0;
    }

    struct sorted_set *summary = heavy_keeper->top_K_heap;
    
    unsigned long long int old_cnt = sorted_set_get_count(summary, key, key_len);
    bool not_in_sorted_set = (old_cnt == INVALID_COUNT);
    unsigned long long maxv = 0;
    unsigned int FP = cal_hash_val_with_seed(key, key_len, FP_HASH_KEY);

    for (int j = 0; j < heavy_keeper->params.array_num; j++) {
        struct Bucket *bucket = find_bucket_by_key(heavy_keeper, j, key, key_len);

        if (bucket->finger_print == FP) {
            // If a flow is not in the min-heap, then the estimated flow size should be no larger than nmin.
            // or if the min-heap is not full(nmin == 0), every flow should be taken into account, so of course it should be added.
            // in neither case, bucket->count > nMin && not_in_sorted_set happen.
            // The flows whose counts are both larger than nmin and not in min-heap must have the same xxhash value, and its FP stored in bucket represents another tag,
            // In this case, the sketch won't be updated. This flow is expected to be taken into account in another array,
            //  where its FP is different from the one it should collided with, so that element flows won't be missed.
            if (not_in_sorted_set) {
                unsigned tmp_min = sorted_set_get_min_count(summary);
                if (bucket->count > tmp_min) {
                    continue;
                }
            }
            bucket->count = safe_add(bucket->count, count);
            maxv = MAX(maxv, bucket->count);
        } else {
            if (!if_need_to_decay(heavy_keeper, bucket, count)) {
                continue;
            }

            if (bucket->count < count) {
                bucket->finger_print = FP;
                bucket->count = count;

                maxv = MAX(maxv, count);
            } else {
                bucket->count -= count;
            }
        }
    }

    if (not_in_sorted_set) {
        if (sorted_set_cardinality(summary) != heavy_keeper->K) {
            sorted_set_insert(summary, key, key_len, maxv, arg);
            return 1;
        }
        unsigned nMin = sorted_set_get_min_count(summary);
        if (nMin == INVALID_COUNT || maxv > nMin) {
            sorted_set_insert(summary, key, key_len, maxv, arg);
            return 1;
        }
        return 0;
    } else {
        if (maxv > old_cnt) {
            sorted_set_incrby(summary, key, key_len, maxv - old_cnt);
        }
        return 1; // no popped, but the tag definitely exists in the sorted set
    }
}

int heavy_keeper_set_exdata_schema(struct heavy_keeper *hk, exdata_new_cb new_fn, exdata_free_cb free_fn, exdata_merge_cb merge_fn, exdata_reset_cb reset_fn, exdata_copy_cb copy_fn) {
    sorted_set_schema_set(hk->top_K_heap, new_fn, free_fn, merge_fn, reset_fn, copy_fn);
    hk->new_fn = new_fn;
    hk->free_fn = free_fn;
    hk->merge_fn = merge_fn;
    hk->reset_fn = reset_fn;
    hk->copy_fn = copy_fn;

    return 0;
}

void *heavy_keeper_get0_exdata(const struct heavy_keeper *hk, const char *key, size_t key_len) {
    const heap_entry *entry = sorted_set_find_entry(hk->top_K_heap, key, key_len);
    if (entry == NULL || sorted_set_entry_dying(entry)) {
        return NULL;
    }
    return sorted_set_entry_get_data(entry)->exdata;
}

int heavy_keeper_get_count(const struct heavy_keeper *hk) {
    return sorted_set_cardinality(hk->top_K_heap);
}

size_t heavy_keeper_list(const struct heavy_keeper *hk, void **exdatas, size_t n_exdatas) {
    size_t actual_len = sorted_set_cardinality(hk->top_K_heap);
    if (actual_len == 0) {
        return 0;
    }

    char **keys_dummy = (char **)malloc(sizeof(char *) * actual_len);
    size_t *key_lens_dummy = (size_t *)malloc(sizeof(size_t) * actual_len);
    if (n_exdatas < actual_len) {
        void **exdatas_ret = (void **)malloc(sizeof(void *) * actual_len);
        sorted_set_dump(hk->top_K_heap, keys_dummy, key_lens_dummy, exdatas_ret);
        memcpy(exdatas, exdatas_ret, sizeof(void *) * n_exdatas);
        free(exdatas_ret);
    } else {
        sorted_set_dump(hk->top_K_heap, keys_dummy, key_lens_dummy, exdatas);
    }

    free(keys_dummy);
    free(key_lens_dummy);

    return actual_len < n_exdatas ? actual_len : n_exdatas;
}

static void heavy_keeper_merge_sketch(struct heavy_keeper *dest, const struct heavy_keeper *src) {
    int w = dest->params.max_bucket_num;
    int d = dest->params.array_num;
    //idx
    for (int array_id = 0; array_id < d; array_id++) {
        for (int bucket_id = 0; bucket_id < w; bucket_id++) {
            struct Bucket *bucket_dest = &(dest->sketch[array_id * w + bucket_id]);
            const struct Bucket *bucket_src = &(src->sketch[array_id * w + bucket_id]);

            if (bucket_dest->finger_print == bucket_src->finger_print) {
                bucket_dest->count = safe_add(bucket_dest->count, bucket_src->count);
            } else {
                if (bucket_dest->count < bucket_src->count) {  // bucket_src stores the elephant flow.
                    bucket_dest->count = bucket_src->count;
                    bucket_dest->finger_print = bucket_src->finger_print;
                }
            }
        }
    }
}

unsigned long long find_maxv_in_sketch(struct heavy_keeper *hk, const char *key, size_t key_len) {
    struct Bucket *bucket;
    unsigned fp = cal_hash_val_with_seed(key, key_len, FP_HASH_KEY);

    unsigned long long maxv = 0;
    for (int array_id = 0; array_id < hk->params.array_num; array_id++) {
        bucket = find_bucket_by_key(hk, array_id, key, key_len);  // hk->sketch is the merge of two. So just check one

        if (bucket->finger_print == fp) {
            maxv = MAX(maxv, bucket->count);
        }
    }

    return maxv;
}

int cmp_int(const void *a, const void *b) {
    return *(int *)a - *(int *)b;
}

void heavy_keeper_merge(struct heavy_keeper *dest, const struct heavy_keeper *src) {
    assert(dest->K == src->K);

    heavy_keeper_merge_sketch(dest, src);

    struct sorted_set *new_rec = sorted_set_new(dest->K); // merging result
    sorted_set_schema_set(new_rec, dest->new_fn, dest->free_fn, dest->merge_fn, dest->reset_fn, dest->copy_fn);
    const struct sorted_set *ss_dest = dest->top_K_heap; // original ss in dest
    const struct sorted_set *ss_src = src->top_K_heap; // the ss to be merged
    int size_dest = sorted_set_cardinality(ss_dest);
    int size_src = sorted_set_cardinality(ss_src);
    int max_size = size_dest > size_src ? size_dest : size_src;

    void **exdatas_dst = (void **)calloc(size_dest, sizeof(void *));
    char **key_arr = (char **)malloc(sizeof(char *) * max_size);
    size_t *key_lens = (size_t *)malloc(sizeof(size_t) * max_size);
    sorted_set_dump(ss_dest, key_arr, key_lens, exdatas_dst);


    /* ------------------------------ merge dest ------------------------------ */
    for (int i = 0; i < size_dest; i++) {
        unsigned long long maxv = find_maxv_in_sketch(dest, key_arr[i], key_lens[i]);
        sorted_set_insert_to_available_heap(new_rec, key_arr[i], key_lens[i], maxv, dest->copy_fn(exdatas_dst[i]));
    }

    /* ------------------------------ merge src ------------------------------ */
    void **exdatas_src = (void **)calloc(size_src, sizeof(void *));
    sorted_set_dump(ss_src, key_arr, key_lens, exdatas_src);
    
    for (int i = 0; i < size_src; i++) {
        const heap_entry *entry = sorted_set_find_entry(new_rec, key_arr[i], key_lens[i]);
        if (entry != NULL) { // the tag is in both dest and src, so has been processed in the previous loop. The reason why no need to sum up result is that merged sketch already keeps its summed up count
            // merge exdata
            void *exdata_new = sorted_set_entry_get_data(entry)->exdata;
            void *exdata_src = exdatas_src[i];
            dest->merge_fn(exdata_new, exdata_src);

            continue;
        }

        unsigned long long maxv = find_maxv_in_sketch(dest, key_arr[i], key_lens[i]);// the dest heavy keeper has been merged, so the maxv is the maxv in the merged sketch, instead of the one in the dest

        if (sorted_set_check_is_full(new_rec)) {
            unsigned long long tmp_mincnt = sorted_set_get_min_count(new_rec);
            if (maxv > tmp_mincnt) {
                sorted_set_pop(new_rec); // TODO:  如果dest 和 new 共用指针，这里pop 出来以后，dest 的内存好像会变得有点问题。先别纠结这么复杂的事，new 和 dest 使用copy后的data 先试试。
                sorted_set_insert_to_available_heap(new_rec, key_arr[i], key_lens[i], maxv, dest->copy_fn(exdatas_src[i]));
            }
        } else {
            sorted_set_insert_to_available_heap(new_rec, key_arr[i], key_lens[i], maxv, dest->copy_fn(exdatas_src[i]));
        }
    }

    free(key_arr);
    free(key_lens);
    free(exdatas_dst);
    free(exdatas_src);
    // dest->top_K_heap->free_fn = default_free_fn; // do not free exdata in the original top_K_heap, because they are either moved to new_rec or freed when popped out when add source to dest.
    sorted_set_free(dest->top_K_heap);
    dest->top_K_heap = new_rec;
}

struct heavy_keeper *heavy_keeper_copy(const struct heavy_keeper *src) {
    struct heavy_keeper *ret = (struct heavy_keeper *)malloc(sizeof(struct heavy_keeper));
    ret->K = src->K;
    ret->rand_state = 0;

    ret->top_K_heap = sorted_set_copy(src->top_K_heap);

    ret->params = src->params;
    ret->sketch = (struct Bucket *)malloc(sizeof(struct Bucket) * (size_t)ret->params.array_num * (size_t)ret->params.max_bucket_num);
    memcpy(ret->sketch, src->sketch, sizeof(struct Bucket) * (size_t)ret->params.array_num * (size_t)ret->params.max_bucket_num);

    ret->new_fn = src->new_fn;
    ret->free_fn = src->free_fn;
    ret->merge_fn = src->merge_fn;
    ret->reset_fn = src->reset_fn;
    ret->copy_fn = src->copy_fn;

    return ret;
}

void heavy_keeper_one_point_query(const struct heavy_keeper *hk, const char *key, size_t key_len, int *count_out, void **exdata_out) {
    *count_out = 0;
    *exdata_out = NULL;

    const heap_entry *entry = sorted_set_find_entry(hk->top_K_heap, key, key_len);
    if (entry == NULL) {
        return;
    }
    *count_out = sorted_set_entry_get_count(entry);
    *exdata_out = sorted_set_entry_get_data(entry)->exdata;
}