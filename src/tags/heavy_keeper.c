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
#include "xxhash/xxhash.h"
#include "cjson/cJSON.h"

#include "tags/sorted_set.h"
#include "tags/my_ut_hash.h"

#define assert(s) do { if (!(s)) { printf("assert failed: %s\n", #s);   \
        *((int *)0) = 0;   \
    } \
    } while (0)

struct Bucket {
    unsigned int finger_print;
    unsigned long long count; // the actual count, sum of all uuid_hash_node->count
};

struct heavy_keeper {
    int K;
    struct sorted_set *top_K_heap;
    struct heavy_keeper_options params;
    struct Bucket *sketch;

    unsigned int rand_state;
};

#define FP_HASH_KEY 0

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

void params_set_to(struct heavy_keeper_options *p, const struct heavy_keeper_options *src) {
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

struct heavy_keeper *heavy_keeper_new(int max_query_num, const struct heavy_keeper_options *params) {
    struct heavy_keeper *hk = (struct heavy_keeper *)malloc(sizeof(struct heavy_keeper));
    hk->K = max_query_num;
    hk->rand_state = 0;

    hk->top_K_heap = sorted_set_new(max_query_num);

    params_set_to_default(&(hk->params), max_query_num);

    if (params != NULL) {
        params_set_to(&(hk->params), params);
    }
    hk->sketch = new_sketch(&(hk->params));
    return hk;
}

struct heavy_keeper *heavy_keeper_with_data_new(int max_query_num) {
    struct heavy_keeper *hk = heavy_keeper_new(max_query_num, NULL);
    return hk;
}

void heavy_keeper_reset(struct heavy_keeper *hk) {
    memset(hk->sketch, 0, (size_t)hk->params.array_num * (size_t)hk->params.max_bucket_num * sizeof(struct Bucket));
    sorted_set_reset(hk->top_K_heap);
}

void heavy_keeper_result_free(struct heavy_keeper_result *stats_hd) {
    free(stats_hd->count);
    free(stats_hd->tags);
    free(stats_hd->cell_id);
    free(stats_hd);
}

void heavy_keeper_free(struct heavy_keeper *hk) {
    sorted_set_free(hk->top_K_heap);
    free(hk->sketch);
    free(hk);
}

/*********************************add and query*************************************/
/***********************************************************************************/
bool if_need_to_decay(struct heavy_keeper *hk, struct Bucket *bucket, unsigned long long count) {
    if (bucket->count == 0) {
        return true;
    }
    if (count == 0) {
        return false;
    }

    double exp = (double)bucket->count / count;
    if (exp > 250) {  // decay_exp_rate ^ (-250) is very small, so regard the chance as 0.
        return false;
    }

    double r = (double)rand_r(&(hk->rand_state)) / (double)RAND_MAX;

    double b = hk->params.decay_exp_rate;
    double decay_rate = pow(b, -exp);

    if (r < decay_rate) {
        return true;
    }
    return false;
}

struct Bucket *map_flow_id_hash_to_bucket(struct heavy_keeper *hk, int array_index, const struct tag_hash_key *tag, unsigned int already_hashed_flow_id) {
    int w = hk->params.max_bucket_num;
    int Hsh;
    if (array_index == FP_HASH_KEY) {
        Hsh = already_hashed_flow_id % w;
    } else {
        Hsh = tag_hash_key_cal_hash_val(tag, array_index) % w;
    }

    return &(hk->sketch[array_index * w + Hsh]);
}

unsigned long long count_add(unsigned long long a, unsigned long long b) {
    if (a > UINT64_MAX - b) {
        return UINT64_MAX;
    }
    return a + b;
}

unsigned my_max(unsigned a, unsigned b) {
    if (a > b) {
        return a;
    }
    return b;
}

static int heavy_keeper_add_by_recording_popped_data(struct heavy_keeper *heavy_keeper, const struct tag_hash_key *tag, unsigned long long int count, int cell_id, int *popped_id_out)
{
    if (count == 0) {
        int cell_id_old = sorted_set_get_cell_id(heavy_keeper->top_K_heap, tag);
        if (cell_id_old > 0) {
            return 1; // the flow is in the sorted set. Use original cell id.
        }

        assert(cell_id >= 0);
        assert(tag != NULL);
        long long tmp_v = tag->hashv;
        assert(tmp_v >= 0);

        if (sorted_set_cardinality(heavy_keeper->top_K_heap) < heavy_keeper->K) {
            sorted_set_insert_and_record_popped_back(heavy_keeper->top_K_heap, tag, 0, cell_id, popped_id_out);
            return 0; // newly added
        }
        return -1; // not added
    }

    int user_data_cube_id = 0;
    struct sorted_set *summary = heavy_keeper->top_K_heap;

    unsigned nMin = sorted_set_get_min_count(summary);
    unsigned long long maxv = 0;
    
    unsigned int FP = tag_hash_key_cal_hash_val(tag, FP_HASH_KEY);
    for (int j = 0; j < heavy_keeper->params.array_num; j++) {
        struct Bucket *bucket = map_flow_id_hash_to_bucket(heavy_keeper, j, tag, FP);

        if (bucket->finger_print == FP) {
            // If a flow is not in the min-heap, then the estimated flow size should be no larger than nmin.
            // The flows whose counts are both larger than nmin and not in min-heap must have the same xxhash value, and its FP stored in bucket represents another tag,
            // In this case, the sketch won't be updated. This flow is expected to be taken into account in another array,
            //  where its FP is different from the one it should collided with, so that element flows won't be missed.
            if (bucket->count > nMin && sorted_set_get_cell_id(summary, tag) == -1) {
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

    unsigned long long int tmp_cnt = sorted_set_get_count(summary, tag);
    if (tmp_cnt == 0) {  // the flow is not in sorted set
        if ((maxv - nMin <= count && maxv != nMin) || sorted_set_cardinality(summary) != heavy_keeper->K) {
            assert(cell_id >= 0);
            assert(tag != NULL);
            long long tmp_v = tag->hashv;
            assert(tmp_v >= 0);

            int insert_ret = sorted_set_insert_and_record_popped_back(summary, tag, maxv, cell_id, &user_data_cube_id);
            if (insert_ret == 2) { // insert success and popped one
                *popped_id_out = user_data_cube_id;
                return 0;
            } else if (insert_ret == 0) { // insert failed
                return -1;
            } else if (insert_ret == 1) { // insert success without popping 
                return 0;
            } else {
                return 2;
            }
        }
        return -1; // the flow is not added to the sorted set
    } else {
        if (maxv > tmp_cnt) {
            sorted_set_incrby(summary, tag, maxv - tmp_cnt);
        }
        return 1; // no popped, but the tag definitely exists in the sorted set
    }

    // should not reach here
    return -1;
}

int heavy_keeper_add(struct heavy_keeper *hk, const struct tag_hash_key *tag, unsigned long long int count, int cell_id, int *popped_id_out)
{
    *popped_id_out = -1;
    heavy_keeper_add_by_recording_popped_data(hk, tag, count, cell_id, popped_id_out);

    int cell_id_tmp = sorted_set_get_cell_id(hk->top_K_heap, tag);
    if (cell_id_tmp >= 0) {
        if (cell_id_tmp == cell_id) { // insert success
            return 0;
        } else {
            return 1; // insert success, but use the different cell_id
        }
    } else {
        return -1;
    }

    return -1; // should not reach here
}

struct tag_hash_key *heavy_keeper_get_true_pst_tag(const struct heavy_keeper *hk, const struct tag_hash_key *tag)
{
    return sorted_set_get_true_pst_tag(hk->top_K_heap, tag);
}

struct heavy_keeper_result *heavy_keeper_query(const struct heavy_keeper *hk) {
    size_t query_entry_num = sorted_set_cardinality(hk->top_K_heap);
    struct heavy_keeper_result *stats = (struct heavy_keeper_result *)malloc(sizeof(struct heavy_keeper_result));
    stats->count = (unsigned long long *)malloc(query_entry_num * sizeof(unsigned long long));
    stats->tags = (struct tag_hash_key **)malloc(query_entry_num * sizeof(struct tag_hash_key *));
    stats->cell_id = (int *)malloc(query_entry_num * sizeof(int));
    stats->n_key = query_entry_num;

    sorted_set_dump(hk->top_K_heap, stats->count, stats->tags, stats->cell_id);

    return stats;
}

size_t heavy_keeper_get_cardinality(const struct heavy_keeper *hk) {
    return sorted_set_cardinality(hk->top_K_heap);
}

int heavy_keeper_query_one(const struct heavy_keeper *hk, const struct tag_hash_key *tag, unsigned long long int *count_out, int *cell_id_out)
{
    if (count_out != NULL) {
        unsigned long long int count = sorted_set_get_count(hk->top_K_heap, tag);
        if (count == 0) {
            *count_out = 0;
            return -1;
        }
        *count_out = count;
    }

    if (cell_id_out == NULL) {
        return 0;
    }

    int cell_id = sorted_set_get_cell_id(hk->top_K_heap, tag);
    if (cell_id == -1) {
        *cell_id_out = -1;
        return -1;
    }
    *cell_id_out = cell_id;

    return 0;
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
            const struct Bucket *bucket_src = &(src->sketch[array_id * w + bucket_id]);

            if (bucket_dest->finger_print == bucket_src->finger_print) {
                bucket_dest->count += bucket_src->count;
            } else {
                if (bucket_dest->count < bucket_src->count) {  // bucket_src stores the elephant flow.
                    bucket_dest->count = bucket_src->count;
                    bucket_dest->finger_print = bucket_src->finger_print;
                }
            }
        }
    }
}

unsigned long long find_maxv_in_sketch(struct heavy_keeper *hk, const struct tag_hash_key *tag) {
    struct Bucket *bucket;
    unsigned fp = tag_hash_key_cal_hash_val(tag, FP_HASH_KEY);

    unsigned long long maxv = 0;
    for (int array_id = 0; array_id < hk->params.array_num; array_id++) {
        bucket = map_flow_id_hash_to_bucket(hk, array_id, tag, tag_hash_key_cal_hash_val(tag, FP_HASH_KEY));  // hk->sketch is the merge of two. So just check one

        if (bucket->finger_print == fp) {
            maxv = my_max(maxv, bucket->count);
        }
    }

    return maxv;
}

int find_next_unused_cell_id(const int *sorted_cell_id_arr, size_t arr_len, int last_find_result, int *next_idx)
{
    int cur_idx = *next_idx;

    if (arr_len == 0) {
        if (last_find_result < 0) {
            return 0;
        } else {
            return last_find_result + 1;
        }
    }

    if (cur_idx >= arr_len) {
        int max_id = sorted_cell_id_arr[arr_len - 1];
        if (last_find_result <= max_id) { //  this first time reach the end of sorted_pst_cell_id_arr
            return max_id + 1;
        } else {
            return last_find_result + 1;
        }
    }

    int expected_id = last_find_result + 1;
    while (cur_idx < arr_len) {
        int cur_id = sorted_cell_id_arr[cur_idx];

        // check if expected_id id is used
        if (cur_id == expected_id) { // used
            expected_id++;
            cur_idx++;
        } else if (cur_id > expected_id) { // not used
            *next_idx = cur_idx;
            return expected_id;
        } else { // still cannot guarantee expected_id is not used
            cur_idx++;
        }
    }

    // all cell_id in sorted_pst_cell_id_arr is used
    int max_id = sorted_cell_id_arr[arr_len - 1];
    *next_idx = arr_len;
    return max_id + 1;
}

int cmp_int(const void *a, const void *b) {
    return *(int *)a - *(int *)b;
}

void heavy_keeper_merge_sorted_set_recording_id_details(struct heavy_keeper *dest, const struct heavy_keeper *src, 
    int **cell_id_popped_from_dest_when_merge_src_out, int *cell_id_popped_from_dest_when_merge_src_len_out,
    int **cell_id_in_src_before_merge_out, int **cell_id_in_dest_corresponding_out, int *cell_id_in_src_before_merge_len_out) 
{
    struct sorted_set *new_rec = sorted_set_new(dest->K); // merging result

    struct sorted_set *ss_dest = dest->top_K_heap; // original ss in dest
    const struct sorted_set *ss_src = src->top_K_heap; // the ss to be merged
    int size_dest = sorted_set_cardinality(ss_dest);
    int size_src = sorted_set_cardinality(ss_src);
    int max_size = size_dest > size_src ? size_dest : size_src;

    unsigned long long *count_arr = (unsigned long long *)malloc(max_size * sizeof(unsigned long long));
    // struct tag_hash_key **tag_arr = (struct tag_hash_key **)malloc(max_size * sizeof(struct tag_hash_key *));
    struct tag_hash_key **tag_arr = (struct tag_hash_key **)calloc(max_size, sizeof(struct tag_hash_key *));
    int *cell_id_arr_dst = (int *)malloc(size_dest * sizeof(int));

    sorted_set_dump(ss_dest, count_arr, tag_arr, cell_id_arr_dst);
    int cell_id_popped_from_dest_when_merge_src[dest->K];
    int cell_id_popped_from_dest_when_merge_src_len = 0;
    int cell_id_in_src_before_merge[dest->K];
    int cell_id_in_src_before_merge_len = 0;
    int cell_id_in_dest_corresponding[dest->K]; // length == cell_id_in_src_before_merge_len

    /* ------------------------------ merge dest ------------------------------ */
    for (int i = 0; i < size_dest; i++) {
        unsigned long long maxv = find_maxv_in_sketch(dest, tag_arr[i]);
        assert(tag_arr[i] != NULL);
        assert(cell_id_arr_dst[i] >= 0);
        sorted_set_insert(new_rec, tag_arr[i], maxv, cell_id_arr_dst[i]);
    }

    qsort(cell_id_arr_dst, size_dest, sizeof(int), cmp_int); // use in find_next_unused_cell_id to find the minimum unused cell_id

    /* ------------------------------ merge source ------------------------------ */
    // int *cell_id_arr_src = (int *)malloc(size_src * sizeof(int));
    int *cell_id_arr_src = (int*)calloc(size_src, sizeof(int));
    sorted_set_dump(ss_src, count_arr, tag_arr, cell_id_arr_src);
    int last_find_id = -1;
    int next_idx = 0;

    for (int i = 0; i < size_src; i++) {
        const struct tag_hash_key *tag_src = tag_arr[i];
        assert(tag_src != NULL);

        int cell_id_dest = sorted_set_get_cell_id(ss_dest, tag_src);
        int cell_id_src = cell_id_arr_src[i];
    
        if (cell_id_dest != -1) {  // the tag is in both dest and src, so has been processed in the previous loop. The reason why no need to sum up result is that merging sketch already gets its summed up count
            cell_id_in_src_before_merge[cell_id_in_src_before_merge_len] = cell_id_src;
            cell_id_in_dest_corresponding[cell_id_in_src_before_merge_len] = cell_id_dest;
            cell_id_in_src_before_merge_len++;

            continue;
        }

        unsigned long long maxv = find_maxv_in_sketch(dest, tag_src); // the dest heavy keeper has been merged, so the maxv is the maxv in the merged sketch, instead of the dest sketch
        int popped_id = -1;
        int now_idx = next_idx;
        int find_id = find_next_unused_cell_id(cell_id_arr_dst, size_dest, last_find_id, &now_idx);
        assert(find_id >= 0);
        assert(now_idx >= 0);
        int tmp_ret = sorted_set_insert_and_record_popped_back(new_rec, tag_src, maxv, find_id, &popped_id);
        if (tmp_ret == 2) { // insert success and popped one.
            cell_id_popped_from_dest_when_merge_src[cell_id_popped_from_dest_when_merge_src_len++] = popped_id;
        } 
        if (tmp_ret != 0) { // insert success
            last_find_id = find_id;
            next_idx = now_idx;
        
            cell_id_in_src_before_merge[cell_id_in_src_before_merge_len] = cell_id_src;
            cell_id_in_dest_corresponding[cell_id_in_src_before_merge_len] = last_find_id;
            cell_id_in_src_before_merge_len++;
        }
    }

    free(count_arr);
    free(tag_arr);
    free(cell_id_arr_dst);
    free(cell_id_arr_src);
    sorted_set_free(ss_dest);

    dest->top_K_heap = new_rec;

    if (cell_id_popped_from_dest_when_merge_src_len == 0) {
        *cell_id_popped_from_dest_when_merge_src_out = NULL;
        *cell_id_popped_from_dest_when_merge_src_len_out = 0;
    } else {
        *cell_id_popped_from_dest_when_merge_src_out = (int *)malloc(cell_id_popped_from_dest_when_merge_src_len * sizeof(int));
        memcpy(*cell_id_popped_from_dest_when_merge_src_out, cell_id_popped_from_dest_when_merge_src, cell_id_popped_from_dest_when_merge_src_len * sizeof(int));
        *cell_id_popped_from_dest_when_merge_src_len_out = cell_id_popped_from_dest_when_merge_src_len;
    }
    if (cell_id_in_src_before_merge_len == 0) {
        *cell_id_in_src_before_merge_out = NULL;
        *cell_id_in_src_before_merge_len_out = 0;
        *cell_id_in_dest_corresponding_out = NULL;
    } else {
        *cell_id_in_src_before_merge_out = (int *)malloc(cell_id_in_src_before_merge_len * sizeof(int));
        memcpy(*cell_id_in_src_before_merge_out, cell_id_in_src_before_merge, cell_id_in_src_before_merge_len * sizeof(int));
        *cell_id_in_src_before_merge_len_out = cell_id_in_src_before_merge_len;
        *cell_id_in_dest_corresponding_out = (int *)malloc(cell_id_in_src_before_merge_len * sizeof(int));
        memcpy(*cell_id_in_dest_corresponding_out, cell_id_in_dest_corresponding, cell_id_in_src_before_merge_len * sizeof(int));
    }
}

int heavy_keeper_merge_recording_id_details(struct heavy_keeper *dest, const struct heavy_keeper *src,
    int **cell_id_popped_from_dest_when_merge_src_out, int *cell_id_popped_from_dest_when_merge_src_len_out,
    int **cell_id_in_src_before_merge_out, int **cell_id_in_dest_corresponding_out, int *cell_id_in_src_before_merge_len_out)
{
    if (dest->params.array_num != src->params.array_num ||
        dest->params.max_bucket_num != src->params.max_bucket_num ||
        dest->K != src->K) {
        return -1;
    }
    
    heavy_keeper_merge_sketch(dest, src);
    heavy_keeper_merge_sorted_set_recording_id_details(dest, src,
        cell_id_popped_from_dest_when_merge_src_out, cell_id_popped_from_dest_when_merge_src_len_out,
        cell_id_in_src_before_merge_out, cell_id_in_dest_corresponding_out, cell_id_in_src_before_merge_len_out);
    return 0;
}

struct heavy_keeper *heavy_keeper_copy(const struct heavy_keeper *src)
{
    struct heavy_keeper *hk = (struct heavy_keeper *)malloc(sizeof(struct heavy_keeper));
    hk->params = src->params;
    hk->K = src->K;
    hk->sketch = (struct Bucket *)malloc(hk->params.array_num * hk->params.max_bucket_num * sizeof(struct Bucket));
    memcpy(hk->sketch, src->sketch, hk->params.array_num * hk->params.max_bucket_num * sizeof(struct Bucket));

    hk->top_K_heap = sorted_set_copy(src->top_K_heap);
    return hk;
}

int heavy_keeper_get_config_K(const struct heavy_keeper *hk)
{
    return hk->K;
}
