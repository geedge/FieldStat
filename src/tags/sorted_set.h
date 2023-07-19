#pragma once

#include <stddef.h>

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

/*
    * @brief Define the method to free the user data. If the user data is not NULL, the free_method will be called when the entry is popped out.
*/
void sorted_set_hook_function_of_free_user_data(struct sorted_set *ss, void (*free_method)(void *));

/** 
 * @brief insert a new entry into the sorted set. The minimum entry will be popped if the list is full.
 * @return 0 if the entry is not inserted because the sorted set is full and the score of the entry is less than the minimum score in the sorted set. Otherwise, return 1.
*/
int sorted_set_insert(struct sorted_set *ss, const struct my_hash_tag_key *tag, unsigned cnt);

/*
  @return 0 if the entry is not inserted because the sorted set is full. return 2 if some data is popped out. Otherwise, return 1.
  @param popped_user_data user should alloc it, popped_user_data_size is equal to the size of the user data memory.
*/
int sorted_set_insert_and_record_popped_back(struct sorted_set *ss, const struct my_hash_tag_key *tag, unsigned cnt, void *popped_user_data, size_t popped_user_data_size);

/**
 * @brief pop out the minumum-score entry from the sorted set.
 * @param ss the sorted set
 * @return the score of the minimum-score member. return 0 if the sorted set is empty.
*/
unsigned sorted_set_pop(struct sorted_set *ss);

/**
    * @brief record the user data to the sorted set. The user data will be freed when the entry is popped out, if the free_method is defined by sorted_set_hook_function_of_free_user_data().
    * @param user_data the pointer to the user data. user must malloc it first.
    * @return 0 if success, -1 if the member is not in the sorted set.
*/
int sorted_set_insert_user_data(struct sorted_set *ss, const struct my_hash_tag_key *tag, void *user_data);

/*
    * @brief get the pointer to the user data of the given member. So that the user can modify the user data.
    * @return the pointer to the user data. return NULL if the member is not in the sorted set.
*/
void **sorted_set_get_pointer_to_user_data(struct sorted_set *ss, const struct my_hash_tag_key *tag);
/**
 * @brief Increase the score of the given member.
 * @param score By the value of which increase member.
*/
void sorted_set_incrby(struct sorted_set *ss, const struct my_hash_tag_key *tag, unsigned count);

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
unsigned sorted_set_get_count(const struct sorted_set *ss, const struct my_hash_tag_key *tag);

/**
 * @brief Get all the contents in sorted order from small to big.
 * @param scores_out the scores of the entries. User should alloc it and make sure the buffer is long enough.
 * @param tags_out the tags of the entries. User should alloc it and make sure the buffer is long enough. Don't free the content.
 * @param user_data_out the user data of the entries. User should alloc it and make sure the buffer is long enough. Its content is the pointer to the user data in the sorted set. Don't free the content.
*/
void sorted_set_dump(const struct sorted_set *ss, unsigned *scores_out, struct my_hash_tag_key **tags_out, void **user_data_out);

#ifdef __cplusplus 
}//end extern "C"
#endif
