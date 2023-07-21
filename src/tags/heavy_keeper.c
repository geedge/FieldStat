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
#include "xxhash/xxhash.h"
#include "cjson/cJSON.h"

#include "tags/sorted_set.h"
#include "tags/my_ut_hash.h"

struct Bucket {
    unsigned int finger_print;
    unsigned count; // the actual count, sum of all uuid_hash_node->count
};

struct heavy_keeper {
    int K;
    struct sorted_set *top_K_heap;
    struct heavy_keeper_options params;
    struct Bucket *sketch;
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

struct heavy_keeper *heavy_keeper_with_data_new(int max_query_num) {
    struct heavy_keeper *hk = heavy_keeper_new(max_query_num, NULL);

    sorted_set_hook_function_of_free_user_data(hk->top_K_heap, free);
    return hk;
}

void heavy_keeper_clear(struct heavy_keeper *hk) {
    memset(hk->sketch, 0, (size_t)hk->params.array_num * (size_t)hk->params.max_bucket_num * sizeof(struct Bucket));
    sorted_set_free(hk->top_K_heap);
    hk->top_K_heap = sorted_set_new(hk->K);
    sorted_set_hook_function_of_free_user_data(hk->top_K_heap, free);
}

struct heavy_keeper_result *heavy_keeper_result_new(struct heavy_keeper *hk) {
    size_t expected_max_len = hk->K;
    struct heavy_keeper_result *stats_hd = (struct heavy_keeper_result *)malloc(sizeof(struct heavy_keeper_result));
    memset(stats_hd, 0, sizeof(struct heavy_keeper_result));
    stats_hd->count = (unsigned *)malloc(expected_max_len * sizeof(unsigned));
    stats_hd->tags = (struct tag_hash_key **)malloc(expected_max_len * sizeof(struct tag_hash_key *));

    stats_hd->cube_id = (int *)malloc(expected_max_len * sizeof(int));

    return stats_hd;
}

void heavy_keeper_result_free(struct heavy_keeper_result *stats_hd) {
    for (int i = 0; i < stats_hd->n_key; i++) {
        tag_hash_key_free(stats_hd->tags[i]);
    }

    free(stats_hd->count);
    free(stats_hd->tags);
    free(stats_hd->cube_id);
    free(stats_hd);
}

void heavy_keeper_free(struct heavy_keeper *hk) {
    sorted_set_free(hk->top_K_heap);
    free(hk->sketch);
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

struct Bucket *map_flow_id_hash_to_bucket(struct heavy_keeper *hk, int array_index, const struct tag_hash_key *tag, unsigned int already_hashed_flow_id) {
    int w = hk->params.max_bucket_num;
    int Hsh;
    if (array_index == FP_HASH_KEY) {
        Hsh = already_hashed_flow_id % w;
    } else {
        Hsh = tag_hash_key_cal_fnv_hash(tag, array_index) % w;
    }

    return &(hk->sketch[array_index * w + Hsh]);
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

static int heavy_keeper_add_by_recording_popped_data(struct heavy_keeper *heavy_keeper, const struct tag_hash_key *tag, unsigned int count)
{
    int ret = -1;
    unsigned int user_data_cube_id = 0;
    struct sorted_set *summary = heavy_keeper->top_K_heap;
    unsigned int FP = tag_hash_key_cal_fnv_hash(tag, FP_HASH_KEY);
    unsigned nMin = sorted_set_get_min_count(summary);
    unsigned maxv = 0;

    for (int j = 0; j < heavy_keeper->params.array_num; j++) {
        struct Bucket *bucket = map_flow_id_hash_to_bucket(heavy_keeper, j, tag, FP);

        if (bucket->finger_print == FP) {
            // If a flow is not in the min-heap, then the estimated flow size should be no larger than nmin.
            // The flows whose counts are both larger than nmin and not in min-heap must have the same xxhash value, and its FP stored in bucket represents another tag,
            // In this case, the sketch won't be updated. This flow is expected to be taken into account in another array,
            //  where its FP is different from the one it should collided with, so that element flows won't be missed.
            if (bucket->count > nMin && sorted_set_get_count(summary, tag) == 0) {
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

    unsigned int tmp_cnt = sorted_set_get_count(summary, tag);
    if (tmp_cnt == 0) {  // the flow is not in sorted set
        if ((maxv - nMin <= count && maxv != nMin) || sorted_set_cardinality(summary) != heavy_keeper->K) {
            int insert_ret = sorted_set_insert_and_record_popped_back(summary, tag, maxv, &user_data_cube_id, sizeof(unsigned int));
            if (insert_ret == 2) { // insert success and popped one
                ret = user_data_cube_id;
            }
        }
    } else {
        if (maxv > tmp_cnt) {
            sorted_set_incrby(summary, tag, maxv - tmp_cnt);
        }
    }
    return ret;
}

int heavy_keeper_add(struct heavy_keeper *hk, const struct tag_hash_key *tag, unsigned int count, int cube_id, int *popped_id_out)
{
    *popped_id_out = heavy_keeper_add_by_recording_popped_data(hk, tag, count);

    int **pst_data_in_rec = (int **)sorted_set_get_pointer_to_user_data(hk->top_K_heap, tag);
    if (pst_data_in_rec == NULL) { // the key is not added into sorted set
        return -1;
    }

    if (*pst_data_in_rec == NULL) { // the key is added in this round, so give it a new id
        *pst_data_in_rec = (int *)malloc(sizeof(int));
        **pst_data_in_rec = cube_id;
        return 0;
    }
    // use the original id
    return 1;
}

struct heavy_keeper_result *heavy_keeper_query(struct heavy_keeper *hk) {
    struct heavy_keeper_result *stats  = heavy_keeper_result_new(hk);

    int heap_entry_num = sorted_set_cardinality(hk->top_K_heap);
    unsigned *cnt_arr = (unsigned *)malloc(heap_entry_num * sizeof(unsigned));
    struct tag_hash_key **tag_arr = (struct tag_hash_key **)malloc(heap_entry_num * sizeof(struct tag_hash_key *));
    int **user_data = (int **)malloc(heap_entry_num * sizeof(int *));

    sorted_set_dump(hk->top_K_heap, cnt_arr, tag_arr, (void **)user_data);

    int query_entry_num = heap_entry_num > hk->K ? hk->K : heap_entry_num;
    stats->n_key = query_entry_num;
    int index_q = 0;
    for (int i = query_entry_num - 1; i >= 0; i--) {
        stats->tags[index_q] = tag_hash_key_copy(tag_arr[i]);
        stats->count[index_q] = cnt_arr[i];
        stats->cube_id[index_q] = *(user_data[i]);
        
        index_q++;
    }

    free(cnt_arr);
    free(tag_arr);
    free(user_data);
    return stats;
}

int heavy_keeper_query_one(struct heavy_keeper *hk, const struct tag_hash_key *tag, unsigned int *count_out, int *cube_id_out)
{
    if (count_out != NULL) {
        unsigned int count = sorted_set_get_count(hk->top_K_heap, tag);
        if (count == 0) {
            *count_out = 0;
            return -1;
        }
        *count_out = count;
    }

    if (cube_id_out == NULL) {
        return 0;
    }

    int **pst_data_in_rec = (int **)sorted_set_get_pointer_to_user_data(hk->top_K_heap, tag);
    if (pst_data_in_rec == NULL) {
        *cube_id_out = -1;
        return -1;
    }
    *cube_id_out = **pst_data_in_rec;

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
            struct Bucket *bucket_src = &(src->sketch[array_id * w + bucket_id]);

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

unsigned find_maxv_in_sketch(struct heavy_keeper *hk, const struct tag_hash_key *tag) {
    struct Bucket *bucket;
    unsigned fp = tag_hash_key_cal_fnv_hash(tag, FP_HASH_KEY);

    unsigned maxv = 0;
    for (int array_id = 0; array_id < hk->params.array_num; array_id++) {
        bucket = map_flow_id_hash_to_bucket(hk, array_id, tag, tag_hash_key_cal_fnv_hash(tag, FP_HASH_KEY));  // hk->sketch is the merge of two. So just check one

        if (bucket->finger_print == fp) {
            maxv = my_max(maxv, bucket->count);
        }
    }

    return maxv;
}

int insert_to_sorted_set(struct sorted_set *ss, const struct tag_hash_key *tag, unsigned count, unsigned int user_data) {
    int if_inserted = sorted_set_insert(ss, tag, count);

    if (if_inserted) {
        int **pst_other_count_in_rec = (int **)sorted_set_get_pointer_to_user_data(ss, tag);

        *pst_other_count_in_rec = (int *)malloc(sizeof(unsigned int));
        **pst_other_count_in_rec = user_data;
    }
    return if_inserted;
}

void heavy_keeper_merge_sorted_set(struct heavy_keeper *dest, const struct heavy_keeper *src) {
    struct sorted_set *new_rec = sorted_set_new(dest->K);
    sorted_set_hook_function_of_free_user_data(new_rec, free);

    int size_dest = sorted_set_cardinality(dest->top_K_heap);
    int size_src = sorted_set_cardinality(src->top_K_heap);
    int max_size = size_dest > size_src ? size_dest : size_src;
    unsigned *count_arr = (unsigned *)malloc(max_size * sizeof(unsigned));
    struct tag_hash_key **tag_arr = (struct tag_hash_key **)malloc(max_size * sizeof(struct tag_hash_key *));
    unsigned int **user_datas_dummy = (unsigned int **)malloc(max_size * sizeof(unsigned int *));

    sorted_set_dump(dest->top_K_heap, count_arr, tag_arr, (void **)user_datas_dummy);
    int new_allocated_tag_id = 0; // the merged fieldstat will be assigned with tag_ids independent from the original ones
                                        // it's ok because the corresponding vals are the name of the tag, from which we recover the tags

    for (int i = 0; i < size_dest; i++) {
        unsigned maxv = find_maxv_in_sketch(dest, tag_arr[i]);
        if (insert_to_sorted_set(new_rec, tag_arr[i], maxv, new_allocated_tag_id)) {
            new_allocated_tag_id++;
        }
    }

    sorted_set_dump(src->top_K_heap, count_arr, tag_arr, (void **)user_datas_dummy);
    for (int i = 0; i < size_src; i++) {
        if (sorted_set_get_count(new_rec, tag_arr[i]) != 0) {
            continue;
        }
        // the dest heavy keeper has been merged, so the maxv is the maxv in the merged sketch, instead of the dest sketch
        unsigned maxv = find_maxv_in_sketch(dest, tag_arr[i]);
        if (insert_to_sorted_set(new_rec, tag_arr[i], maxv, new_allocated_tag_id)) {
            new_allocated_tag_id++;
        }
    }

    free(count_arr);
    free(tag_arr);
    free(user_datas_dummy);

    sorted_set_free(dest->top_K_heap);
    dest->top_K_heap = new_rec;
}

int heavy_keeper_merge(struct heavy_keeper *hk, const struct heavy_keeper *src) {
    if (hk->params.array_num != src->params.array_num ||
        hk->params.max_bucket_num != src->params.max_bucket_num ||
        hk->K != src->K) {
        printf("ERR: parameters of two merged heavy keeper must be the same\n");
        return -1;
    }

    heavy_keeper_merge_sketch(hk, src);
    heavy_keeper_merge_sorted_set(hk, src);
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


/******************************* serialization *******************************/
/*Serialize the heavy keeper to a mpack string which is equivalent to a JSON
    The json: 
    {
    "type": "topk"
    "K": <value_of_K>,
    “sketch": [{"FP":<finger_print>, "C":counter}, ...],
    "ss": [
        {
            "tag": <serialized_tag_bin>,
            "score": <primary_score>,
            "id":<cell_id>
        },
        {(more elements in ss, the sorted set)}
        ...
        }
    ]
    }
}
***********************************************************************************/
int heavy_keeper_sorted_set_member_serialization(struct tag_hash_key *tag, unsigned int score, unsigned int cell_id, char **blob, size_t *blob_sz) {
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_sz);
    mpack_build_map(&writer);

    mpack_write_cstr(&writer, "tag");
    char *tag_blob;
    size_t tag_blob_sz;
    tag_hash_key_serialize(tag, &tag_blob, &tag_blob_sz);
    mpack_write_bin(&writer, tag_blob, tag_blob_sz);
    free(tag_blob);

    mpack_write_cstr(&writer, "score");
    mpack_write_u32(&writer, score);

    mpack_write_cstr(&writer, "id");
    mpack_write_u32(&writer, cell_id);

    mpack_complete_map(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        printf("ERR: An error occurred in heavy_keeper_sorted_set_member_serialization!\n");
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
        printf("ERR: An error occurred in heavy_keeper_bucket_serialization!\n");
        return -1;
    }
    return 0;
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
    struct tag_hash_key **tag_arr = (struct tag_hash_key **)malloc(heap_entry_num * sizeof(struct tag_hash_key *));
    unsigned int **user_data = (unsigned int **)malloc(heap_entry_num * sizeof(unsigned int *));
    sorted_set_dump(hk->top_K_heap, key_score, tag_arr, (void **)user_data);
    mpack_build_array(writer);
    for (size_t i = 0; i < heap_entry_num; i++) {
        heavy_keeper_sorted_set_member_serialization(tag_arr[i], key_score[i], *user_data[i], &element_mpack_buff, &element_mpack_size);
        mpack_write_object_bytes(writer, element_mpack_buff, element_mpack_size);
        free(element_mpack_buff);
        element_mpack_size = 0;
    }
    mpack_complete_array(writer);

    free(key_score);
    free(tag_arr);
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

    heavy_keeper_sorted_set_serialization(hk, &writer);

    mpack_complete_map(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        printf("ERR: An error occurred in heavy_keeper_serialization\n");
    }
}

void heavy_keeper_sorted_set_deserialization(mpack_node_t *root, struct sorted_set *out)
{
    mpack_node_t item;
    mpack_node_t array_sorted_set = mpack_node_map_cstr(*root, "ss");
    size_t sorted_set_size = mpack_node_array_length(array_sorted_set);
    for (size_t i = 0; i < sorted_set_size; i++) {
        item = mpack_node_array_at(array_sorted_set, i);

        mpack_node_t tag_bin_node = mpack_node_map_cstr(item, "tag");
        const char *tag_blob = mpack_node_bin_data(tag_bin_node);
        size_t tag_blob_len = mpack_node_bin_size(tag_bin_node);
        struct tag_hash_key *tag = tag_hash_key_deserialize(tag_blob, tag_blob_len);

        unsigned int score = mpack_node_u32(mpack_node_map_cstr(item, "score"));

        unsigned int *id = (unsigned int *)malloc(sizeof(unsigned int)); // will be freed with sorted set
        *id = mpack_node_u32(mpack_node_map_cstr(item, "id"));

        sorted_set_insert(out, tag, score);
        sorted_set_insert_user_data(out, tag, (void *)id);
        tag_hash_key_free(tag);
    }
}

struct heavy_keeper *heavy_keeper_deserialization(const char *blob, size_t size) {
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, size);
    mpack_tree_parse(&tree);
    mpack_node_t root = mpack_tree_root(&tree);

    int K = mpack_node_int(mpack_node_map_cstr(root, "K"));

    struct heavy_keeper *hk = heavy_keeper_new(K, NULL);
    sorted_set_hook_function_of_free_user_data(hk->top_K_heap, free);

    mpack_node_t item_sketch = mpack_node_map_cstr(root, "sketch");
    size_t bin_size = mpack_node_bin_size(item_sketch);
    memcpy(hk->sketch, mpack_node_bin_data(item_sketch), bin_size);

    heavy_keeper_sorted_set_deserialization(&root, hk->top_K_heap);

    if (mpack_tree_destroy(&tree) != mpack_ok) {
        printf("ERR: An error occurred in heavy_keeper_deserialization\n");
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
