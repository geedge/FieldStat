
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

struct tag_hash_key *gen_key(const char *key, int value)
{
    const char *value_str = to_string(value).c_str();
    struct fieldstat_tag tag = {
        .key = key,
        .type = TAG_CSTRING,
        {.value_str = value_str},
    };

    struct tag_hash_key *tag_key = tag_hash_key_construct_with_fieldstat_tag(&tag, 1);

    return tag_key;
}

double cal_accuracy(vector<struct tag_hash_key *> &expected_keys, vector<struct tag_hash_key *> &test_result) {
    map<string, int> countMap;
    for (size_t i = 0; i < expected_keys.size(); i++) {
        std::string key = tag_hash_key_get_compound_key(expected_keys[i]);
        countMap[key]++;
    }

    std::vector<std::pair<std::string, int>> countVector(countMap.begin(), countMap.end());
    std::sort(countVector.begin(), countVector.end(), [](const std::pair<std::string, int> &a, const std::pair<std::string, int> &b) {
        return a.second > b.second;
    });

    std::set<std::string> myset;
    int min_in_max_count = 0;
    size_t i;
    for (i = 0; i < test_result.size(); ++i) {
        myset.insert(countVector[i].first);
        min_in_max_count = countVector[i].second;
    }
    int last_cnt = min_in_max_count;
    while (last_cnt == min_in_max_count && i < countVector.size()) {
        if (countVector[i].second != last_cnt) {
            break;
        }
        myset.insert(countVector[i].first);
        i++;
    }

    int correct = 0;
    for (size_t i = 0; i < test_result.size(); i++) {
        if (myset.find(tag_hash_key_get_compound_key(test_result[i])) != myset.end()) {
            correct++;
        }
    }

    double accuracy = (double)correct / test_result.size();
    return accuracy;
}

vector<tag_hash_key *> test_query_cell_manager_content(struct cell_manager *cm)
{
    int ret_len;
    const struct tag_hash_key **dump_ret = cell_manager_dump(cm, &ret_len);
    vector<tag_hash_key *> test_result;
    for (int i = 0; i < ret_len; i++) {
        if (dump_ret[i] == NULL) {
            continue;
        }
        test_result.push_back((struct tag_hash_key *)dump_ret[i]);
        printf("query content, id: %d, key: %s\n", i, tag_hash_key_get_compound_key(dump_ret[i]));
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
            struct tag_hash_key *key = gen_key("key", rand() % 10);
            keys.push_back(key);
        } else {
            struct tag_hash_key *key = gen_key("key", rand() % 1000);
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
    double accuracy = cal_accuracy(keys, test_result);
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
    keys.push_back(gen_key("key_share", 1));
    keys.push_back(gen_key("key_1", 1));
    keys.push_back(gen_key("key_1", 2));
    keys.push_back(gen_key("key_share", 1));
    keys.push_back(gen_key("key_2", 1));

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
    auto test_result_1 = test_query_cell_manager_content(cm1);
    for (size_t i = 0; i < test_result_1.size(); i++) {
        printf("cm1 content: %s\n", tag_hash_key_get_compound_key(test_result_1[i]));
    }
    auto test_result_2 = test_query_cell_manager_content(cm2);
    for (size_t i = 0; i < test_result_2.size(); i++) {
        printf("cm2 content: %s\n", tag_hash_key_get_compound_key(test_result_2[i]));
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
    double accuracy = cal_accuracy(keys, test_result);
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
    keys.push_back(gen_key("key_1", 1));
    keys.push_back(gen_key("key_1", 1));
    keys.push_back(gen_key("key_1", 2));

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
    keys1.push_back(gen_key("key_1", 1));
    keys1.push_back(gen_key("key_1", 2));
    keys1.push_back(gen_key("key_shared", 1));

    vector<struct tag_hash_key *> keys2;
    for (int i = 0; i < 9; i++) {
        keys2.push_back(gen_key("key_2", i));
    }
    keys2.push_back(gen_key("key_shared", 1));

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
    double accuracy = cal_accuracy(keys1, test_result);
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


int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}