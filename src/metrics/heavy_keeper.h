#pragma once

#include <stddef.h>
#include <uuid/uuid.h>

/* ---------------------------------------------------------------------------------------
TOP K using heavykeeper algorithm. 
Author: Chen Zizhan 
Time: 2023/4/26

# usage example
struct heavy_keeper *hk = heavy_keeper_new(K, NULL);

// or: run heavykeeper with user defined params: 
// heavy_keeper_options params = {5, 1000, 1.5};
// struct heavy_keeper *hk = heavy_keeper_new(K, &params);

const char flow_id[] = "any string representing url, ip, etc";
unsigned count = 1; // if count the number of session/packet/... 
// Or: if sum up the total traffic volume in bytes
// unsigned count = 700;

heavy_keeper_add(hk, flow_id, strlen(flow_id), count);
// repeat update for every flow ... ... ... ... 
// ...........................
struct heavy_keeper_result* stats = heavy_keeper_result_new(K);
heavy_keeper_query(hk, stats);

heavy_keeper_free(hk);

use the stats variable
For example :
  for (int i = 0; i < stats->n_key; i++) {
      printf("top %d flow id is: %s, its count: #llu", i, stats->key[i], stats->count[i]);
  }

heavy_keeper_result_free(stats);

---------------------------------------------------------------------------------------------*/

#ifdef __cplusplus
extern "C"{
#endif


// Query result
struct heavy_keeper_result {
    size_t n_key; // number of result
    char **key; // key[i] is the flow id, i < n_key
    size_t *key_len; // key_len[i] is the length flow id string, i < n_key
    unsigned *count; // count[i] is the count of the corresponding flow id
};

// Parameters used in algorithm
struct heavy_keeper_options{
    int array_num;  // the size of the array. Default value: 4
                    // Set it by how accurate you want. Value of 4 garentees an accuray more than 90% and around 97% in all tests.
                    // Not too big because it have an impact on both time and memory efficiency.
    int max_bucket_num; // M2, the maximum number of buckets every array keeps. Default value: k*log(k) and no less than 100. 
                        // Basically, as long as big enough, it won't affect the accuracy significantly.
    double decay_exp_rate; // b, bigger variance of flow size is(elephant flows take more ratio), smaller it should be.
                           //  Must bigger than 1. Better not bigger than 1.3, otherwise some elephant flow will be missed.
};

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

/**
 * @brief   Create a heavy keeper.
 * @param   max_query_num            you want to get a summery of top-max_query_num flows.
 * @param   params                   parameters used in algorithm. If NULL, default params will be used.
 * @returns a pointer to the heavy keeper.
 */
struct heavy_keeper *heavy_keeper_new(int max_query_num, struct heavy_keeper_options *params);

/**
 * @brief   Create a heavy keeper.
 * @param   max_query_num            you want to get a summery of top-max_query_num flows.
 * @param   params                   parameters used in algorithm. If NULL, default params will be used.
 * @param   uuid                     any uuid_t typed unsigned char[16] you want to use to identify a heavykeeper uniquely.
 * @returns a pointer to the heavy keeper.
 */
struct heavy_keeper *heavy_keeper_new_with_uuid(int max_query_num, struct heavy_keeper_options *params, uuid_t uuid);

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

/**
 * @brief   Add a flow to the heavy keeper.
 * @param   hk            the pointer to the heavy keeper.
 * @param   key           the flow id string.
 * @param   key_len       the length of the flow id string.
 * @param   count         the count of the flow or its traffic volume in bytes.
 */
void heavy_keeper_add(struct heavy_keeper *hk, const char *key, size_t key_len, unsigned int count);

/**
 * @brief   Query the top-K flows.The flow id with bigger count ranks at smaller index.
 * @param   hk            the pointer to the heavy keeper.
 * @return  a pointer to the heavy keeper query result. User should free it after use by calling heavy_keeper_result_free.
 */
struct heavy_keeper_result *heavy_keeper_query(struct heavy_keeper *hk);

/**
 * @brief   free a heavy keeper query result.
 * @param   stats_hd     the pointer to the heavy keeper query result.
 */
void heavy_keeper_result_free(struct heavy_keeper_result *stats_hd);

/**
 * @brief   Merge two heavy keeper.
 * @param   hk            the pointer to the heavy keeper.
 * @param   hk_merged     the pointer to the heavy keeper to be merged. Must have the same params with hk.
 * @returns 0 if success, -1 if failed.
 */
int heavy_keeper_merge(struct heavy_keeper *hk, const struct heavy_keeper *src);

/**
 * @brief   Merge all replicas of a heavy keeper to my replica, and delete all the others, so as to save memory. This function is only safe to use when the hk is not merged to the others. 
 * @param   hk            the pointer to the heavy keeper.
 */
void heavy_keeper_replicas_all_join_to_mine(struct heavy_keeper *hk);

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

// TODO: 
/*
支持多个数据一起塞进来，其中一个作为键值，其他的跟随记录
例子：
list: key: "1.2.3.4"

name              count
"byte":           1000  [**primary**]
"packet":112
"out bype":12

struct list_entry {
    char *name;
    unsigned int count;
}


hk = heavy_keeper_new(K, "byte", "packet", "out byte")
heavy_keeper_add(hk, "1,2,3,4", 1000, 112, 12)

struct heavy_keeper *heavy_keeper_new(int K, ...);
int heavy_keeper_add(struct heavy_keeper *hk, const char *key, ...)


// heavy_keeper_add_list(hk, "1.2.3.4", {{"byte", 1000}, {"packet", 112}, {"out byte", 12}})
heavy_keeper_add_list(hk, "1.2.3.4", {1000, 112, 12});

heavy_keeper_query(hk) -> 
    struct heavy_keeper_result_list {
        char *key[];
        struct list_entry *list[];
        size_t result_num; // 最大K个
    };

result 举例：
result_num = 2
key: "1.2.3.4", "123.123.123.123"
list[1]: {{"byte", 1000}, {"packet", 112}, {"out byte", 12}}
list[2]: {{"byte", 12}, {"packet", 1}, {"out byte", 0}

*/




#ifdef __cplusplus
}
#endif