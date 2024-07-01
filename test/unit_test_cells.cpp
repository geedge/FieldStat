
#include <gtest/gtest.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <algorithm>


#include "fieldstat.h"
#include "heavy_keeper.h"
#include "tag_map.h"
#include "utils.hpp"
#include "my_ut_hash.h"

using namespace std;

struct tag_hash_key *test_gen_tag_key(const char *key, int value)
{
    struct fieldstat_tag tag = {
        .key = key,
        .type = TAG_CSTRING,
        {.value_str = strdup(to_string(value).c_str())},
    };

    struct tag_hash_key *tag_key = (struct tag_hash_key *)malloc(sizeof(struct tag_hash_key));
    tag_hash_key_init_with_fieldstat_tag(tag_key, &tag, 1, true);

    free((void *)tag.value_str);

    return tag_key;
}

struct Fieldstat_tag_list_wrapper *test_key_tag_to_wrapper(const struct tag_hash_key *key)
{
    assert(key != NULL);
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

struct test_heavy_keeper_args {
    struct tag_hash_key *key;
};

void *test_heavy_keeper_new_cb(void *arg)
{
    struct test_heavy_keeper_args *args = (struct test_heavy_keeper_args *)arg;
    return args->key;
}
void test_heavy_keeper_free_cb(void *exdata)
{
}
void test_heavy_keeper_reset_cb(void *exdata)
{
}
void test_heavy_keeper_merge_cb(void *dest, void *src)
{
}
void *test_heavy_keeper_copy_cb(void *src)
{
    return src;
}


vector<tag_hash_key *> test_query_heavy_keeper_content(const struct heavy_keeper *hk)
{
    size_t ret_len;
    struct tag_hash_key **dump_ret_exdata = NULL;
    heavy_keeper_list(hk, (void ***)&dump_ret_exdata, &ret_len);
    vector<tag_hash_key *> test_result;
    for (size_t i = 0; i < ret_len; i++) {
        // char *tmp = tag_hash_key_get_compound_key(dump_ret_exdata[i]);
        // printf("test_query_heavy_keeper_content key: %s\n", tmp);
        // free(tmp);
        test_result.push_back((struct tag_hash_key *)dump_ret_exdata[i]);
    }

    free(dump_ret_exdata);

    return test_result;
}

TEST(unit_test_heavy_keeper, topk_simple_add)
{
    struct heavy_keeper *hk = heavy_keeper_new(10);
    heavy_keeper_set_exdata_schema(hk, test_heavy_keeper_new_cb, test_heavy_keeper_free_cb, test_heavy_keeper_merge_cb, test_heavy_keeper_reset_cb, test_heavy_keeper_copy_cb);
    const int TEST_ROUND = 10;

    vector<struct tag_hash_key *> keys;
    for (int i = 0; i < TEST_ROUND; i++)
    {
        struct tag_hash_key *key = test_gen_tag_key("key", i);
        keys.push_back(key);
    }
    
    for (int i = 0; i < TEST_ROUND; i++) {
        struct test_heavy_keeper_args args = {keys[i]};
        heavy_keeper_add(hk, keys[i], 1, &args);
    }

    vector<tag_hash_key *> test_result = test_query_heavy_keeper_content(hk);
    EXPECT_EQ(test_result.size(), 10);
    double accuracy = cal_accuracy_with_tags(keys, test_result);
    EXPECT_NEAR(accuracy, 1.0, 0.01);
    
    heavy_keeper_free(hk);
    for (int i = 0; i < TEST_ROUND; i++) {
        tag_hash_key_free(keys[i]);
    }
}

TEST(unit_test_heavy_keeper, topk_add_pop)
{
    struct heavy_keeper *hk = heavy_keeper_new(10);
    heavy_keeper_set_exdata_schema(hk, test_heavy_keeper_new_cb, test_heavy_keeper_free_cb, test_heavy_keeper_merge_cb, test_heavy_keeper_reset_cb, test_heavy_keeper_copy_cb);
    const int TEST_ROUND = 11;

    vector<struct tag_hash_key *> keys;
    for (int i = 0; i < TEST_ROUND; i++)
    {
        struct tag_hash_key *key = test_gen_tag_key("key", i);
        keys.push_back(key);
    }

    for (int i = 0; i < TEST_ROUND - 1; i++) {
        struct test_heavy_keeper_args args = {keys[i]};
        heavy_keeper_add(hk, keys[i], 1, &args);
    }
    struct test_heavy_keeper_args args = {keys[TEST_ROUND - 1]};
    heavy_keeper_add(hk, keys[TEST_ROUND - 1], 100, &args);

    vector<tag_hash_key *> test_result = test_query_heavy_keeper_content(hk);
    EXPECT_EQ(test_result.size(), 10);
    double accuracy = cal_accuracy_with_tags(keys, test_result);
    EXPECT_NEAR(accuracy, 1.0, 0.01);

    int count;
    void *exdata;
    heavy_keeper_one_point_query(hk, keys[TEST_ROUND - 1], &count, &exdata);
    EXPECT_EQ(count, 100);

    heavy_keeper_free(hk);    
    for (int i = 0; i < TEST_ROUND; i++) {
        tag_hash_key_free(keys[i]);
    }
}

TEST(unit_test_heavy_keeper, topk_add_last_one_twice)
{
    struct heavy_keeper *hk = heavy_keeper_new(10);
    heavy_keeper_set_exdata_schema(hk, test_heavy_keeper_new_cb, test_heavy_keeper_free_cb, test_heavy_keeper_merge_cb, test_heavy_keeper_reset_cb, test_heavy_keeper_copy_cb);
    const int TEST_ROUND = 10;

    vector<struct tag_hash_key *> keys;
    for (int i = 0; i < TEST_ROUND; i++)
    {
        struct tag_hash_key *key = test_gen_tag_key("key", i);
        keys.push_back(key);
    }

    for (int i = 0; i < TEST_ROUND; i++) {
        struct test_heavy_keeper_args args = {keys[i]};
        heavy_keeper_add(hk, keys[i], 1, &args);
    }
    struct test_heavy_keeper_args args = {keys[TEST_ROUND - 1]};
    heavy_keeper_add(hk, keys[TEST_ROUND - 1], 100, &args);

    vector<tag_hash_key *> test_result = test_query_heavy_keeper_content(hk);
    EXPECT_EQ(test_result.size(), 10);
    double accuracy = cal_accuracy_with_tags(keys, test_result);
    EXPECT_NEAR(accuracy, 1.0, 0.01);

    int count;
    void *exdata;
    heavy_keeper_one_point_query(hk, keys[TEST_ROUND - 1], &count, &exdata);
    EXPECT_EQ(count, 100 + 1);
    
    heavy_keeper_free(hk);
    for (int i = 0; i < TEST_ROUND; i++) {
        tag_hash_key_free(keys[i]);
    }
}

TEST(unit_test_heavy_keeper, topk_add_and_query_accuracy)
{
    struct heavy_keeper *hk = heavy_keeper_new(10);
    heavy_keeper_set_exdata_schema(hk, test_heavy_keeper_new_cb, test_heavy_keeper_free_cb, test_heavy_keeper_merge_cb, test_heavy_keeper_reset_cb, test_heavy_keeper_copy_cb);
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

    for (int i = 0; i < TEST_ROUND; i++) {
        struct test_heavy_keeper_args args = {keys[i]};
        heavy_keeper_add(hk, keys[i], 1, &args);
    }

    vector<tag_hash_key *> test_result = test_query_heavy_keeper_content(hk);
    EXPECT_EQ(test_result.size(), 10);
    double accuracy = cal_accuracy_with_tags(keys, test_result);
    EXPECT_NEAR(accuracy, 1.0, 0.01);
    
    heavy_keeper_free(hk);
    for (int i = 0; i < TEST_ROUND; i++) {
        tag_hash_key_free(keys[i]);
    }
}

TEST(unit_test_heavy_keeper, merge_topk_given_K_large_enough)
{
    struct heavy_keeper *hk1 = heavy_keeper_new(10);
    struct heavy_keeper *hk2 = heavy_keeper_new(10);
    heavy_keeper_set_exdata_schema(hk1, test_heavy_keeper_new_cb, test_heavy_keeper_free_cb, test_heavy_keeper_merge_cb, test_heavy_keeper_reset_cb, test_heavy_keeper_copy_cb);
    heavy_keeper_set_exdata_schema(hk2, test_heavy_keeper_new_cb, test_heavy_keeper_free_cb, test_heavy_keeper_merge_cb, test_heavy_keeper_reset_cb, test_heavy_keeper_copy_cb);

    vector<struct tag_hash_key *> keys;
    keys.push_back(test_gen_tag_key("key_share", 1));
    keys.push_back(test_gen_tag_key("key_1", 1));
    keys.push_back(test_gen_tag_key("key_1", 2));
    keys.push_back(test_gen_tag_key("key_share", 1));
    keys.push_back(test_gen_tag_key("key_2", 1));

    for (size_t i = 0; i < 3; i++) {
        struct test_heavy_keeper_args args = {keys[i]};
        heavy_keeper_add(hk1, keys[i], 1, &args);
    }
    for (size_t i = 3; i < 5; i++) {
        struct test_heavy_keeper_args args = {keys[i]};
        heavy_keeper_add(hk2, keys[i], 1, &args);
    }

    heavy_keeper_merge(hk1, hk2);

    auto test_result = test_query_heavy_keeper_content(hk1);
    double accuracy = cal_accuracy_with_tags(keys, test_result);
    EXPECT_NEAR(accuracy, 1.0, 0.01);

    int count;
    void *exdata;
    heavy_keeper_one_point_query(hk1, keys[0], &count, &exdata);
    EXPECT_EQ(count, 2); // key_share merged once

    heavy_keeper_free(hk1);
    heavy_keeper_free(hk2);

    for (size_t i = 0; i < keys.size(); i++) {
        tag_hash_key_free(keys[i]);
    }
}

TEST(unit_test_heavy_keeper, merge_topk_to_full_one)
{
    struct heavy_keeper *hk1 = heavy_keeper_new(10);
    struct heavy_keeper *hk2 = heavy_keeper_new(10);
    heavy_keeper_set_exdata_schema(hk1, test_heavy_keeper_new_cb, test_heavy_keeper_free_cb, test_heavy_keeper_merge_cb, test_heavy_keeper_reset_cb, test_heavy_keeper_copy_cb);
    heavy_keeper_set_exdata_schema(hk2, test_heavy_keeper_new_cb, test_heavy_keeper_free_cb, test_heavy_keeper_merge_cb, test_heavy_keeper_reset_cb, test_heavy_keeper_copy_cb);

    vector<struct tag_hash_key *> keys1;
    keys1.push_back(test_gen_tag_key("key_1", 1));
    keys1.push_back(test_gen_tag_key("key_1", 2));
    keys1.push_back(test_gen_tag_key("key_shared", 1));

    vector<struct tag_hash_key *> keys2;
    for (int i = 0; i < 9; i++) {
        keys2.push_back(test_gen_tag_key("key_2", i));
    }
    keys2.push_back(test_gen_tag_key("key_shared", 1));

    for (size_t i = 0; i < 3; i++) {
        struct test_heavy_keeper_args args = {keys1[i]};
        heavy_keeper_add(hk1, keys1[i], 10, &args);
    }
    for (size_t i = 0; i < 10; i++) {
        unsigned int count = i < 2 ? i : 5; // the first 2 keys have count 1 and 2(less), the rest have count 5
        struct test_heavy_keeper_args args = {keys2[i]};
        heavy_keeper_add(hk2, keys2[i], count, &args);
    }

    heavy_keeper_merge(hk2, hk1);

    auto test_result = test_query_heavy_keeper_content(hk2);
    // join keys2 to keys1
    keys1.insert(keys1.end(), std::make_move_iterator(keys2.begin()), std::make_move_iterator(keys2.end()));
    double accuracy = cal_accuracy_with_tags(keys1, test_result);
    EXPECT_NEAR(accuracy, 1.0, 0.01);
    // print all count
    for (size_t i = 0; i < test_result.size(); i++) {
        int count;
        void *exdata;
        heavy_keeper_one_point_query(hk2, test_result[i], &count, &exdata);
        if (strcmp(test_result[i]->tags->key, "key_shared") == 0) {
            EXPECT_EQ(count, 15);
        } else if (strcmp(test_result[i]->tags->key, "key_1") == 0) {
            EXPECT_EQ(count, 10);
        } else if (strcmp(test_result[i]->tags->key, "key_2") == 0) {
            EXPECT_EQ(count, 5);
        }
    }
    
    heavy_keeper_free(hk2);
    heavy_keeper_free(hk1);
    for (size_t i = 0; i < keys1.size(); i++) {
        tag_hash_key_free(keys1[i]);
    }
    // all keys are moved to hk1, so no need to free keys2
}

// // reset once will not delete the cells, just let them be discared, so in such case, cube_add will just add to the same cell.
TEST(unit_test_tag_map, add_after_reset_and_ensure_performance_improvement) {
    clock_t start, end;
    const int TEST_ROUND = 100000;
    // struct cell_manager *hk = cell_manager_new(SAMPLING_MODE_COMPREHENSIVE, TEST_ROUND);
    struct tag_map *hk = tag_map_new(TEST_ROUND);
    vector<struct tag_hash_key *> keys;
    for (int i = 0; i < TEST_ROUND; i++)
    {
        struct tag_hash_key *key = test_gen_tag_key("key", i);
        keys.push_back(key);
    }

    for (int i = 0; i < TEST_ROUND; i++) {
        tag_map_add(hk, keys[i], NULL);
    }

    tag_map_reset(hk);
    start = clock();
    for (int i = 0; i < TEST_ROUND; i++)
    {
        tag_map_add(hk, keys[i], NULL);
    }
    end = clock();
    clock_t time_reset_once = end - start;


    tag_map_reset(hk);
    tag_map_reset(hk);
    start = clock();
    for (int i = 0; i < TEST_ROUND; i++)
    {
        tag_map_add(hk, keys[i], NULL);
    }
    end = clock();
    clock_t time_reset_twice = end - start;

    EXPECT_GT(time_reset_twice, time_reset_once);

    tag_map_free(hk);
    for (int i = 0; i < TEST_ROUND; i++) {
        tag_hash_key_free(keys[i]);
    }
}

TEST(unit_test_heavy_keeper, add_after_reset_and_ensure_performance_improvement) {
    clock_t start, end;
    const int K = 1000;
    const int TEST_ROUND = K;

    struct heavy_keeper *hk = heavy_keeper_new(K);
    vector<struct tag_hash_key *> keys;
    for (int i = 0; i < TEST_ROUND; i++)
    {
        struct tag_hash_key *key = test_gen_tag_key("key", i % K);
        keys.push_back(key);
    }

    start = clock();
    for (int i = 0; i < TEST_ROUND; i++) {
        heavy_keeper_add(hk, keys[i], 1, NULL);
    }
    end = clock();
    clock_t time_empty = end - start;

    heavy_keeper_reset(hk);
    start = clock();
    for (int i = 0; i < TEST_ROUND; i++)
    {
        heavy_keeper_add(hk, keys[i], 1, NULL);
    }
    end = clock();
    clock_t time_reset_once = end - start;
   
    EXPECT_GT(time_empty, time_reset_once);

    heavy_keeper_free(hk);
    for (int i = 0; i < TEST_ROUND; i++) {
        tag_hash_key_free(keys[i]);
    }
}


int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}