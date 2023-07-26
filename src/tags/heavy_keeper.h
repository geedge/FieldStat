#pragma once

#include <stddef.h>
#include <uuid/uuid.h>

/* ---------------------------------------------------------------------------------------
TOP K using heavykeeper algorithm. 
Author: Chen Zizhan 
Time: 2023/4/26
---------------------------------------------------------------------------------------------*/

#ifdef __cplusplus
extern "C"{
#endif


// Query result
struct heavy_keeper_result {
    size_t n_key; // number of result
    struct tag_hash_key **tags;
    unsigned *count; // count[i] is the metric count of the corresponding flow id

    int *cell_id;
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

struct heavy_keeper;

struct heavy_keeper *heavy_keeper_with_data_new(int max_query_num);
/**
 * @brief   free a heavy keeper.
 * @param   hk            the pointer to the heavy keeper.
 */
void heavy_keeper_free(struct heavy_keeper *hk);

/*
 * @brief   clear a heavy keeper. It will clear all the data in the heavy keeper.
    * @param   hk            the pointer to the heavy keeper.
*/
void heavy_keeper_clear(struct heavy_keeper *hk);

// tag: the pointer to the tag, user should alloc it. Its memory will be freed by heavy keeper, so don't free it after calling heavy_keeper_add.
// return -1 when the key is not inserted to the heavy keeper(not fail). popped_id_out is -1 if not popped. any other value >= 0 means the id of the popped member.
// the cube id will give the key if the key is new, in which case return 0, if the key is already in the heavy keeper, return 1.
int heavy_keeper_add(struct heavy_keeper *hk, struct tag_hash_key *tag, unsigned int count, int cube_id, int *popped_id_out);

/**
 * @brief   Query the top-K flows.The flow id with bigger count ranks at smaller index.
 * @param   hk            the pointer to the heavy keeper.
 * @return  a pointer to the heavy keeper query result. User should free it after use by calling heavy_keeper_result_free.
 */
struct heavy_keeper_result *heavy_keeper_query(struct heavy_keeper *hk);

int heavy_keeper_query_one(struct heavy_keeper *hk, const struct tag_hash_key *tag, unsigned int *count_out, int *cube_id_out);

/**
 * @brief   free a heavy keeper query result.
 * @param   stats_hd     the pointer to the heavy keeper query result.
 */
void heavy_keeper_result_free(struct heavy_keeper_result *stats_hd);

int heavy_keeper_merge_recording_id_details(struct heavy_keeper *dest, const struct heavy_keeper *src,
    int **cell_id_popped, int *n_cell_id_popped,
    int **cell_id_added_src, int **cell_id_added_dst, int *n_cell_id_added,
    int **cell_id_merging_src, int **cell_id_merging_dst, int *n_cell_id_merged);

/*
    Equal to heavy_keeper_merge, with empty dest, but much faster.
*/
struct heavy_keeper *heavy_keeper_copy(const struct heavy_keeper *src);

/**
 * @brief   Serialize a heavy keeper.
 * @param   hk            the pointer to the heavy keeper.
 * @param   blob          output of the serialized data. Must be freed by caller. *blob can be NULL, the function will allocate memory for it.
 * @param   blob_sz       Output, the size of the serialized data.
*/
void heavy_keeper_serialization(const struct heavy_keeper *hk, char **blob, size_t *blob_sz);

/**
 * @brief   Deserialize a heavy keeper.
 * @param   blob          the serialized data. From heavy_keeper_serialization.
 * @param   size          the size of the serialized data.
 * @returns a pointer to the heavy keeper.
*/
struct heavy_keeper *heavy_keeper_deserialization(const char *blob, size_t size);


/**
 * @brief   Get the "K" parameter of the heavy keeper.
*/
int heavy_keeper_get_config_K(struct heavy_keeper *hk);

void heavy_keeper_get_config_metric_header(struct heavy_keeper *hk, char **metric_names, size_t *n_metric, size_t *primary_metric_index);


#ifdef __cplusplus
}
#endif