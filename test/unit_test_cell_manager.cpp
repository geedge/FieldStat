
#include <gtest/gtest.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <algorithm>


#include "fieldstat.h"
#include "tags/cell_manager.h"
#include "utils.hpp"

using namespace std;

struct tag_hash_key *test_gen_tag_key(const char *key, int value)
{
    struct fieldstat_tag tag = {
        .key = key,
        .type = TAG_CSTRING,
        {.value_str = strdup(to_string(value).c_str())},
    };

    struct tag_hash_key *tag_key = tag_hash_key_construct_with_fieldstat_tag(&tag, 1);

    free((void *)tag.value_str);

    return tag_key;
}

struct Fieldstat_tag_list_wrapper *test_key_tag_to_wrapper(const struct tag_hash_key *key)
{
    struct fieldstat_tag *tag;
    size_t n_out;
    tag_hash_key_convert_to_fieldstat_tag(key, &tag, &n_out);
    struct fieldstat_tag_list tag_list;
    tag_list.tag = tag;
    tag_list.n_tag = n_out;
    struct Fieldstat_tag_list_wrapper *wrapper = new Fieldstat_tag_list_wrapper(&tag_list);

    for (size_t i = 0; i < n_out; i++)
    {
        if (tag[i].type == TAG_CSTRING)
            free((void *)tag[i].value_str);
        free((void *)tag[i].key);
    }
    free(tag);

    return wrapper;
}

double cal_accuracy_with_tags(const vector<struct tag_hash_key *> &expected_keys, const vector<struct tag_hash_key *> &test_result) {
    unordered_map<string, int> countMap;
    for (size_t i = 0; i < expected_keys.size(); i++) {
        struct Fieldstat_tag_list_wrapper *wrapper = test_key_tag_to_wrapper(expected_keys[i]);
        string key = wrapper->to_string();
        countMap[key]++;
        delete wrapper;
    }
    vector<struct Fieldstat_tag_list_wrapper *> test_result_wrapper;

    for (size_t i = 0; i < test_result.size(); i++) {
        struct Fieldstat_tag_list_wrapper *wrapper = test_key_tag_to_wrapper(test_result[i]);
        test_result_wrapper.push_back(wrapper);
    }

    double ret = test_cal_topk_accuracy(test_result_wrapper, countMap);

    for (size_t i = 0; i < test_result.size(); i++) {
        delete test_result_wrapper[i];
    }
    return ret;
}

vector<tag_hash_key *> test_query_cell_manager_content(const struct cell_manager *cm)
{
    int ret_len;
    const struct tag_hash_key **dump_ret = cell_manager_dump(cm, &ret_len);
    vector<tag_hash_key *> test_result;
    for (int i = 0; i < ret_len; i++) {
        if (dump_ret[i] == NULL) {
            continue;
        }
        test_result.push_back((struct tag_hash_key *)dump_ret[i]);
    }

    return test_result;
}

TEST(unit_test_cell_manager, topk_add_and_query_accuracy)
{
    struct cell_manager *cm = cell_manager_new(SAMPLING_MODE_TOPK, 10);
    const int TEST_ROUND = 10000;

    vector<struct tag_hash_key *> keys;
    for (int i = 0; i < TEST_ROUND; i++)
    {
        if (rand()) {
            struct tag_hash_key *key = test_gen_tag_key("key", rand() % 10);
            keys.push_back(key);
        } else {
            struct tag_hash_key *key = test_gen_tag_key("key", rand() % 1000);
            keys.push_back(key);
        }
    }

    int pop_dummy;
    int exist_dummy;
    for (int i = 0; i < TEST_ROUND; i++) {
        cell_manager_add_cell_topk(cm, keys[i], 1, &pop_dummy, &exist_dummy);
    }

    vector<tag_hash_key *> test_result = test_query_cell_manager_content(cm);
    EXPECT_EQ(test_result.size(), 10);
    double accuracy = cal_accuracy_with_tags(keys, test_result);
    EXPECT_NEAR(accuracy, 1.0, 0.01);
    
    cell_manager_free(cm);
    for (int i = 0; i < TEST_ROUND; i++) {
        tag_hash_key_free(keys[i]);
    }
}

TEST(unit_test_cell_manager, merge_topk_given_K_large_enough)
{
    struct cell_manager *cm1 = cell_manager_new(SAMPLING_MODE_TOPK, 10);
    struct cell_manager *cm2 = cell_manager_new(SAMPLING_MODE_TOPK, 10);

    vector<struct tag_hash_key *> keys;
    keys.push_back(test_gen_tag_key("key_share", 1));
    keys.push_back(test_gen_tag_key("key_1", 1));
    keys.push_back(test_gen_tag_key("key_1", 2));
    keys.push_back(test_gen_tag_key("key_share", 1));
    keys.push_back(test_gen_tag_key("key_2", 1));

    int pop_dummy;
    int exist_dummy;
    int cell_id_1[3];
    int cell_id_2[2];

    for (size_t i = 0; i < 3; i++) {
        cell_id_1[i] = cell_manager_add_cell_topk(cm1, keys[i], 1, &pop_dummy, &exist_dummy);
    }
    for (size_t i = 3; i < 5; i++) {
        cell_id_2[i - 3] = cell_manager_add_cell_topk(cm2, keys[i], 1, &pop_dummy, &exist_dummy);
    }
    int *cell_id_popped;
    int n_cell_id_popped;
    int *cell_id_old;
    int *cell_id_added;
    int n_cell_id_added;

    cell_manager_merge_topk(cm1, cm2, &cell_id_popped, &n_cell_id_popped, &cell_id_old, &cell_id_added, &n_cell_id_added);
    EXPECT_EQ(n_cell_id_popped, 0);
    EXPECT_EQ(n_cell_id_added, 2);
    EXPECT_EQ(cell_id_added[0], cell_id_1[0]); // key_share in cm1
    EXPECT_EQ(cell_id_old[0], cell_id_2[0]); // key_share in cm2
    EXPECT_EQ(cell_id_added[1], 3); // key_2, new cell
    EXPECT_EQ(cell_id_old[1], cell_id_2[1]); // key_2 in cm2

    auto test_result = test_query_cell_manager_content(cm1);
    double accuracy = cal_accuracy_with_tags(keys, test_result);
    EXPECT_NEAR(accuracy, 1.0, 0.01);

    EXPECT_EQ(cell_manager_get_count_by_tag(cm1, keys[0]), 2); // key_share merged once

    free(cell_id_popped);
    free(cell_id_old);
    free(cell_id_added);
    cell_manager_free(cm1);
    cell_manager_free(cm2);

    for (size_t i = 0; i < keys.size(); i++) {
        tag_hash_key_free(keys[i]);
    }
}

TEST(unit_test_cell_manager, merge_topk_to_empty)
{
    struct cell_manager *cm1 = cell_manager_new(SAMPLING_MODE_TOPK, 10);
    struct cell_manager *cm2 = cell_manager_new(SAMPLING_MODE_TOPK, 10);

    vector<struct tag_hash_key *> keys;
    keys.push_back(test_gen_tag_key("key_1", 1));
    keys.push_back(test_gen_tag_key("key_1", 1));
    keys.push_back(test_gen_tag_key("key_1", 2));

    int pop_dummy;
    int exist_dummy;
    int cell_id_1[3];

    for (size_t i = 0; i < 3; i++) {
        cell_id_1[i] = cell_manager_add_cell_topk(cm1, keys[i], 1, &pop_dummy, &exist_dummy);
        printf("cell id: %d\n", cell_id_1[i]);
    }
    
    auto test_result_1 = test_query_cell_manager_content(cm1);
    for (size_t i = 0; i < test_result_1.size(); i++) {
        printf("cm1 content: %s\n", tag_hash_key_get_compound_key(test_result_1[i]));
    }

    int *cell_id_popped;
    int n_cell_id_popped;
    int *cell_id_old;
    int *cell_id_added;
    int n_cell_id_added;

    cell_manager_merge_topk(cm2, cm1, &cell_id_popped, &n_cell_id_popped, &cell_id_old, &cell_id_added, &n_cell_id_added);
    EXPECT_EQ(n_cell_id_popped, 0);
    EXPECT_EQ(n_cell_id_added, 2);
    EXPECT_EQ(cell_id_added[0], 0); // new added
    EXPECT_EQ(cell_id_old[0], cell_id_1[0]); // key_11 in cm1
    EXPECT_EQ(cell_id_added[1], 1); // new added
    EXPECT_EQ(cell_id_old[1], cell_id_1[2]); // key_12 in cm1
    EXPECT_EQ(cell_manager_get_count_by_tag(cm2, keys[0]), 2); // key_11 added twice
    EXPECT_EQ(cell_manager_get_count_by_tag(cm2, keys[2]), 1); // key_12

    free(cell_id_popped);
    free(cell_id_old);
    free(cell_id_added);
    cell_manager_free(cm1);
    cell_manager_free(cm2);

    for (size_t i = 0; i < keys.size(); i++) {
        tag_hash_key_free(keys[i]);
    }
}

TEST(unit_test_cell_manager, merge_topk_to_full_one)
{
    struct cell_manager *cm1 = cell_manager_new(SAMPLING_MODE_TOPK, 10);
    struct cell_manager *cm2 = cell_manager_new(SAMPLING_MODE_TOPK, 10);

    vector<struct tag_hash_key *> keys1;
    keys1.push_back(test_gen_tag_key("key_1", 1));
    keys1.push_back(test_gen_tag_key("key_1", 2));
    keys1.push_back(test_gen_tag_key("key_shared", 1));

    vector<struct tag_hash_key *> keys2;
    for (int i = 0; i < 9; i++) {
        keys2.push_back(test_gen_tag_key("key_2", i));
    }
    keys2.push_back(test_gen_tag_key("key_shared", 1));

    int pop_dummy;
    int exist_dummy;
    int cell_id_1[3];
    int cell_id_2[10];
    for (size_t i = 0; i < 3; i++) {
        cell_id_1[i] = cell_manager_add_cell_topk(cm1, keys1[i], 10, &pop_dummy, &exist_dummy);
    }
    for (size_t i = 0; i < 10; i++) {
        unsigned int count = i < 2 ? i : 5; // the first 2 keys have count 1 and 2(less), the rest have count 5
        cell_id_2[i] = cell_manager_add_cell_topk(cm2, keys2[i], count, &pop_dummy, &exist_dummy);
    }

    int *cell_id_popped;
    int n_cell_id_popped;
    int *cell_id_old;
    int *cell_id_added;
    int n_cell_id_added;
    cell_manager_merge_topk(cm2, cm1, &cell_id_popped, &n_cell_id_popped, &cell_id_old, &cell_id_added, &n_cell_id_added);

    EXPECT_EQ(n_cell_id_popped, 2); // 2 "key_1" added, should pop 2 cells
    EXPECT_EQ(cell_id_popped[0], cell_id_2[0]); // key2 which has count of 1
    EXPECT_EQ(cell_id_popped[1], cell_id_2[1]); // key2 which has count of 2
    EXPECT_EQ(n_cell_id_added, 3); // 2 "key_1", "key_shared" added, should add 3 cells
    EXPECT_EQ(cell_id_added[0], 10); // newly added
    EXPECT_EQ(cell_id_added[1], 11); // newly added
    EXPECT_EQ(cell_id_added[2], cell_id_2[9]); // shared key
    EXPECT_EQ(cell_id_old[0], cell_id_1[0]); // key_1 in cm1
    EXPECT_EQ(cell_id_old[1], cell_id_1[1]); // key_1 in cm1
    EXPECT_EQ(cell_id_old[2], cell_id_1[2]); // key_shared in cm1

    auto test_result = test_query_cell_manager_content(cm2);
    // join keys2 to keys1
    keys1.insert(keys1.end(), std::make_move_iterator(keys2.begin()), std::make_move_iterator(keys2.end()));
    double accuracy = cal_accuracy_with_tags(keys1, test_result);
    EXPECT_NEAR(accuracy, 1.0, 0.01);

    free(cell_id_popped);
    free(cell_id_old);
    free(cell_id_added);
    cell_manager_free(cm1);
    cell_manager_free(cm2);
    for (size_t i = 0; i < keys1.size(); i++) {
        tag_hash_key_free(keys1[i]);
    }
    // all keys are moved to cm1, so no need to free keys2
}

void add_key_and_assert_find_result(struct cell_manager *cm, const struct tag_hash_key *key)
{
    int pop_dummy;
    int exist_dummy;
    int cell_id = cell_manager_add_cell_topk(cm, key, 1234, &pop_dummy, &exist_dummy);
    EXPECT_EQ(cell_id, 0);
    EXPECT_EQ(cell_manager_get_count_by_tag(cm, key), 1234);
    const struct tag_hash_key *key_get = cell_manager_get_tag_by_cell_id(cm, cell_id);
    EXPECT_STREQ(tag_hash_key_get_compound_key(key_get), tag_hash_key_get_compound_key(key));
}

void add_key_and_assert_find_result_comprehensive(struct cell_manager *cm, const struct tag_hash_key *key)
{
    int cell_id = cell_manager_add_cell(cm, key);
    EXPECT_EQ(cell_id, 0);
    const struct tag_hash_key *key_get = cell_manager_get_tag_by_cell_id(cm, cell_id);
    EXPECT_STREQ(tag_hash_key_get_compound_key(key_get), tag_hash_key_get_compound_key(key));
}

TEST(unit_test_cell_manager, add_with_key_length_is_1_int_type_topk)
{
    struct cell_manager *cm = cell_manager_new(SAMPLING_MODE_TOPK, 10);
    struct tag_hash_key *key = tag_hash_key_construct_with_fieldstat_tag(&TEST_TAG_INT, 1);
    
    add_key_and_assert_find_result(cm, key);

    cell_manager_free(cm);
    tag_hash_key_free(key);
}

TEST(unit_test_cell_manager, add_with_key_length_is_1_double_type_topk)
{
    struct tag_hash_key *key = tag_hash_key_construct_with_fieldstat_tag(&TEST_TAG_DOUBLE, 1);
    struct cell_manager *cm = cell_manager_new(SAMPLING_MODE_TOPK, 10);

    add_key_and_assert_find_result(cm, key);

    cell_manager_free(cm);
    tag_hash_key_free(key);
}

TEST(unit_test_cell_manager, add_with_key_length_is_1_string_type_topk)
{
    struct tag_hash_key *key = tag_hash_key_construct_with_fieldstat_tag(&TEST_TAG_STRING, 1);
    struct cell_manager *cm = cell_manager_new(SAMPLING_MODE_TOPK, 10);

    add_key_and_assert_find_result(cm, key);

    cell_manager_free(cm);
    tag_hash_key_free(key);
}

TEST(unit_test_cell_manager, add_with_key_length_is_3_of_diff_types_topk)
{
    const struct fieldstat_tag tags[3] = {TEST_TAG_INT, TEST_TAG_STRING, TEST_TAG_DOUBLE};
    struct tag_hash_key *key = tag_hash_key_construct_with_fieldstat_tag(tags, 3);
    struct cell_manager *cm = cell_manager_new(SAMPLING_MODE_TOPK, 10);

    add_key_and_assert_find_result(cm, key);

    cell_manager_free(cm);
    tag_hash_key_free(key);
}

TEST(unit_test_cell_manager, add_with_key_length_is_1_int_type_comprehensive)
{
    struct cell_manager *cm = cell_manager_new(SAMPLING_MODE_COMPREHENSIVE, 10);
    struct tag_hash_key *key = tag_hash_key_construct_with_fieldstat_tag(&TEST_TAG_INT, 1);
    
    add_key_and_assert_find_result_comprehensive(cm, key);

    cell_manager_free(cm);
    tag_hash_key_free(key);
}

TEST(unit_test_cell_manager, add_with_key_length_is_1_double_type_comprehensive)
{
    struct tag_hash_key *key = tag_hash_key_construct_with_fieldstat_tag(&TEST_TAG_DOUBLE, 1);
    struct cell_manager *cm = cell_manager_new(SAMPLING_MODE_COMPREHENSIVE, 10);

    add_key_and_assert_find_result_comprehensive(cm, key);

    cell_manager_free(cm);
    tag_hash_key_free(key);
}

TEST(unit_test_cell_manager, add_with_key_length_is_1_string_type_comprehensive)
{
    struct tag_hash_key *key = tag_hash_key_construct_with_fieldstat_tag(&TEST_TAG_STRING, 1);
    struct cell_manager *cm = cell_manager_new(SAMPLING_MODE_COMPREHENSIVE, 10);

    add_key_and_assert_find_result_comprehensive(cm, key);

    cell_manager_free(cm);
    tag_hash_key_free(key);
}

TEST(unit_test_cell_manager, add_with_key_length_is_3_of_diff_types_comprehensive)
{
    const struct fieldstat_tag tags[3] = {TEST_TAG_INT, TEST_TAG_STRING, TEST_TAG_DOUBLE};
    struct tag_hash_key *key = tag_hash_key_construct_with_fieldstat_tag(tags, 3);
    struct cell_manager *cm = cell_manager_new(SAMPLING_MODE_COMPREHENSIVE, 10);

    add_key_and_assert_find_result_comprehensive(cm, key);

    cell_manager_free(cm);
    tag_hash_key_free(key);
}

extern "C" 
{
    int cmp_pst_int(const void *a, const void *b);
    int find_next_unused_cell_id(const int *const *sorted_pst_cell_id_arr, size_t arr_len, int last_find_result, int *next_idx);
}
TEST(unit_test_cell_manager, find_next_id_given_all_cell_id_continuous)
{
    int **arr = (int **)malloc(sizeof(int *) * 10);
    for (int i = 0; i < 10; i++)
    {
        arr[i] = (int *)malloc(sizeof(int));
        *arr[i] = i;
    }

    int last_find_result = -1;
    int next_idx = 0;
    last_find_result = find_next_unused_cell_id((const int *const *)arr, 10, last_find_result, &next_idx);
    EXPECT_EQ(last_find_result, 10);
    last_find_result = find_next_unused_cell_id((const int *const *)arr, 10, last_find_result, &next_idx);
    EXPECT_EQ(last_find_result, 11);

    for (int i = 0; i < 10; i++)
    {
        free(arr[i]);
    }
    free(arr);
}

TEST(unit_test_cell_manager, find_next_id_given_continuous_hole)
{
    int **arr = (int **)malloc(sizeof(int *) * 10);
    for (int i = 0; i < 3; i++)
    {
        arr[i] = (int *)malloc(sizeof(int));
        *arr[i] = i;
    }
    arr[3] = (int *)malloc(sizeof(int));
    *arr[3] = 5; // 3, 4 is hole
    for (int i = 4; i < 10; i++)
    {
        arr[i] = (int *)malloc(sizeof(int));
        *arr[i] = i + 2;
    }
    
    int last_find_result = -1;
    int next_idx = 0;
    last_find_result = find_next_unused_cell_id((const int *const *)arr, 10, last_find_result, &next_idx);
    EXPECT_EQ(last_find_result, 3);
    last_find_result = find_next_unused_cell_id((const int *const *)arr, 10, last_find_result, &next_idx);
    EXPECT_EQ(last_find_result, 4);
    last_find_result = find_next_unused_cell_id((const int *const *)arr, 10, last_find_result, &next_idx);
    EXPECT_EQ(last_find_result, 12);
    last_find_result = find_next_unused_cell_id((const int *const *)arr, 10, last_find_result, &next_idx);
    EXPECT_EQ(last_find_result, 13);

    for (int i = 0; i < 10; i++)
    {
        free(arr[i]);
    }
    free(arr);
}

TEST(unit_test_cell_manager, find_next_id_given_holes)
{
    int **arr = (int **)malloc(sizeof(int *) * 10);
    for (int i = 0; i < 10; i++)
    {
        arr[i] = (int *)malloc(sizeof(int));
        *arr[i] = i * 2; // 0, 2, 4, ...
    }
    
    int last_find_result = -1;
    int next_idx = 0;
    last_find_result = find_next_unused_cell_id((const int *const *)arr, 10, last_find_result, &next_idx);
    EXPECT_EQ(last_find_result, 1);
    last_find_result = find_next_unused_cell_id((const int *const *)arr, 10, last_find_result, &next_idx);
    EXPECT_EQ(last_find_result, 3);
    last_find_result = find_next_unused_cell_id((const int *const *)arr, 10, last_find_result, &next_idx);
    EXPECT_EQ(last_find_result, 5);

    for (int i = 0; i < 10; i++)
    {
        free(arr[i]);
    }
    free(arr);
}

TEST(unit_test_cell_manager, find_next_id_given_holes_at_only_start)
{
    int **arr = (int **)malloc(sizeof(int *) * 10);
    for (int i = 0; i < 10; i++)
    {
        arr[i] = (int *)malloc(sizeof(int));
        *arr[i] = i + 2;
    }

    int last_find_result = -1;
    int next_idx = 0;
    last_find_result = find_next_unused_cell_id((const int *const *)arr, 10, last_find_result, &next_idx);
    EXPECT_EQ(last_find_result, 0);
    last_find_result = find_next_unused_cell_id((const int *const *)arr, 10, last_find_result, &next_idx);
    EXPECT_EQ(last_find_result, 1);
    last_find_result = find_next_unused_cell_id((const int *const *)arr, 10, last_find_result, &next_idx);
    EXPECT_EQ(last_find_result, 12);

    for (int i = 0; i < 10; i++)
    {
        free(arr[i]);
    }
    free(arr);
}

TEST(unit_test_cell_manager, find_next_id_given_empty_arr)
{
    int **arr = (int **)malloc(sizeof(int *) * 0);

    int last_find_result = -1;
    int next_idx = 0;
    last_find_result = find_next_unused_cell_id((const int *const *)arr, 0, last_find_result, &next_idx);
    EXPECT_EQ(last_find_result, 0);
    last_find_result = find_next_unused_cell_id((const int *const *)arr, 0, last_find_result, &next_idx);
    EXPECT_EQ(last_find_result, 1);

    free(arr);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}