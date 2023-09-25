#pragma once

#include <stddef.h>
#include "my_ut_hash.h"

#ifdef __cplusplus
extern "C"{
#endif

struct sorted_set;

/**
 * @brief Create a new sorted set.
 * @param set_size the max size of the sorted set.
 * @return the pointer to the sorted set.
*/
struct sorted_set *sorted_set_new(int set_size);

/**
 * @brief Destroy the sorted set.
 * @param ss the pointer to the sorted set
*/
void sorted_set_free(struct sorted_set *ss);


/** 
 * @brief insert a new entry into the sorted set. The minimum entry will be popped if the list is full.
 * @param tag the pointer to the tag, user should alloc it, when the entry is popped out, the tag will be freed. If the tag is not inserted to the sorted set, user should free it.
 * @return 0 if the entry is not inserted because the sorted set is full and the score of the entry is less than the minimum score in the sorted set. Otherwise, return 1.
*/
int sorted_set_insert(struct sorted_set *ss, const struct tag_hash_key *tag, unsigned cnt, int cell_id);

/*
  @return -1 if the entry already exist. 0 if the entry is not inserted because the sorted set is full. return 2 if some data is popped out. Otherwise, return 1 if the entry is inserted successfully and sorted set not full.
  @param popped_user_data user should alloc it, popped_user_data_size is equal to the size of the user data memory.
*/
int sorted_set_insert_and_record_popped_back(struct sorted_set *ss, const struct tag_hash_key *tag, unsigned cnt, int cell_id, int *popped_user_data);

/**
 * @brief pop out the minumum-score entry from the sorted set.
 * @param ss the sorted set
 * @return the score of the minimum-score member. return 0 if the sorted set is empty.
*/
unsigned sorted_set_pop(struct sorted_set *ss);

/**
 * @brief Increase the score of the given member.
 * @param score By the value of which increase member.
*/
int sorted_set_incrby(struct sorted_set *ss, const struct tag_hash_key *tag, unsigned count);

/**
 * @brief get the minimum score from the sorted set.
 * @param ss the sorted set
 * @return the score of the minimum-score member
*/
unsigned sorted_set_get_min_count(const struct sorted_set *ss);

/** 
 * @brief find the how many elements the sorted set contains.
*/
int sorted_set_cardinality(const struct sorted_set *ss);

/**
 * @brief Get the score of the given key string.
 * @return the score of the member. return 0 if the member is not in the sorted set.
*/
unsigned sorted_set_get_count(const struct sorted_set *ss, const struct tag_hash_key *tag);
int sorted_set_get_cell_id(const struct sorted_set *ss, const struct tag_hash_key *tag);
/**
 * @brief Get all the contents in sorted order from small to big.
 * @param scores_out the scores of the entries. User should alloc it and make sure the buffer is long enough.
 * @param tags_out the tags of the entries. User should alloc it and make sure the buffer is long enough. Don't free the content.
 * @param user_data_out the user data of the entries. User should alloc it and make sure the buffer is long enough. Its content is the pointer to the user data in the sorted set. Don't free the content.
*/
void sorted_set_dump(const struct sorted_set *ss, unsigned *scores_out, struct tag_hash_key **tags_out, int *user_data_out);

struct sorted_set *sorted_set_copy(const struct sorted_set *ss);

void sorted_set_reset(struct sorted_set *ss);

// the cell_manager id_tag_map will not stort the copied tag, instead, it will store the pointer to the tag in the sorted set. Use this function to give it one.
struct tag_hash_key *sorted_set_get_true_pst_tag(const struct sorted_set *ss, const struct tag_hash_key *tag);

#ifdef __cplusplus 
}//end extern "C"
#endif
