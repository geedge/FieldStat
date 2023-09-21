
#include <gtest/gtest.h>
#include <set>
#include <unordered_map>
#include "fieldstat.h"
#include "utils.hpp"

using namespace std;

vector<Fieldstat_tag_list_wrapper *> test_gen_topk_flows(int num, int rand_flow_type_num)
{
    vector<Fieldstat_tag_list_wrapper *> flows;
    for (int i = 0; i < num; i++)
    {
        if (rand() % 2) {
            flows.push_back(new Fieldstat_tag_list_wrapper("my key", to_string(i).c_str()));
        } else {
            flows.push_back(new Fieldstat_tag_list_wrapper("elephant", to_string(rand() % rand_flow_type_num).c_str()));
        }
    }
    return flows;
}

double test_cal_accuracy_given_expected_key(vector<struct Fieldstat_tag_list_wrapper *> &expected_keys, vector<struct Fieldstat_tag_list_wrapper *> &test_result) {
    unordered_map<string, int> countMap;
    for (size_t i = 0; i < expected_keys.size(); i++) {
        std::string key = expected_keys[i]->to_string();
        countMap[key]++;
    }

    return test_cal_topk_accuracy(test_result, countMap);
}

TEST(unit_test_merge, test_metric_name_mapping_with_new_metric_on_existing_cube)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id1 = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id_1_0 = fieldstat_register_counter(instance, cube_id1, "metric_name cube1 cube2", COUNTER_MERGE_BY_SUM);
    int cell_id_1_0 = fieldstat_cube_add(instance, cube_id1, &TEST_TAG_STRING, 1, 1);
    fieldstat_counter_incrby(instance, cube_id1, metric_id_1_0, cell_id_1_0, 1);
    int metric_id_1_1 = fieldstat_register_counter(instance, cube_id1, "shared name", COUNTER_MERGE_BY_SUM);
    int cell_id_1_1 = fieldstat_cube_add(instance, cube_id1, &TEST_TAG_STRING, 1, 1);
    fieldstat_counter_incrby(instance, cube_id1, metric_id_1_1, cell_id_1_1, 2);
    int cube_id2 = fieldstat_register_cube(instance, &TEST_TAG_INT, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id_2_0 = fieldstat_register_counter(instance, cube_id2, "metric_name cube1 cube2", COUNTER_MERGE_BY_SUM);
    int cell_id_2_0 = fieldstat_cube_add(instance, cube_id2, &TEST_TAG_STRING, 1, 1);
    fieldstat_counter_incrby(instance, cube_id2, metric_id_2_0, cell_id_2_0, 3);

    struct fieldstat *instance_dest = fieldstat_new();
    int cube_id_dest = fieldstat_register_cube(instance_dest, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    (void)fieldstat_register_counter(instance_dest, cube_id_dest, "shared name", COUNTER_MERGE_BY_SUM);

    fieldstat_merge(instance_dest, instance);

    int *cube_id;
    int n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id, &n_cube);
    EXPECT_TRUE(n_cube == 2);
    EXPECT_TRUE(cube_id[0] == cube_id_dest);

    EXPECT_EQ(fieldstat_get_max_metric_id(instance_dest, cube_id[0]) + 1, 2);
    EXPECT_EQ(fieldstat_get_max_metric_id(instance_dest, cube_id[1]) + 1, 1);

    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest, cube_id_dest, 0), "shared name");
    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest, cube_id_dest, 1), "metric_name cube1 cube2");
    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest, cube_id[1], 0), "metric_name cube1 cube2");

    EXPECT_EQ(my_fieldstat_counter_get(instance_dest, cube_id_dest, 0, 0), 2); // shared name
    EXPECT_EQ(my_fieldstat_counter_get(instance_dest, cube_id_dest, 1, 0), 1); // metric_name cube1 cube2 on cube1
    EXPECT_EQ(my_fieldstat_counter_get(instance_dest, cube_id[1], 0, 0), 3); // metric_name cube1 cube2 on cube2

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
    free(cube_id);
}

TEST(unit_test_merge, cube_shared_tag_mapping_with_new_cube)
{
    struct fieldstat *instance = fieldstat_new();
    (void)fieldstat_register_cube(instance, &TEST_TAG_DOUBLE, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int cube_id2 = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_counter(instance, cube_id2, "metric in cube 2", COUNTER_MERGE_BY_SUM);
    struct fieldstat *instance_dest = fieldstat_new();
    int cube_id_dest = fieldstat_register_cube(instance_dest, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);

    fieldstat_merge(instance_dest, instance);

    int *cube_id;
    int n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id, &n_cube);
    EXPECT_TRUE(n_cube == 2);
    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest, cube_id_dest, 0), "metric in cube 2");

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
    free(cube_id);
}

TEST(unit_test_merge, empty_instance)
{
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);

    int *cube_id;
    int n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id, &n_cube);
    EXPECT_TRUE(n_cube == 0);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
}

TEST(unit_test_merge, new_cube_and_metric_to_empty_comprehensive)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_TAG_INT, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_counter(instance, cube_id, "metric_name", COUNTER_MERGE_BY_SUM);

    struct fieldstat *instance_dest = fieldstat_new();

    fieldstat_merge(instance_dest, instance);

    int *cube_id_dest;
    int n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id_dest, &n_cube);
    EXPECT_TRUE(n_cube == 1);
    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest, cube_id_dest[0], 0), "metric_name");

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
    free(cube_id_dest);
}

TEST(unit_test_merge, new_cell_on_existing_cube_and_metric_comprehensive)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_counter(instance, cube_id, "metric_name", COUNTER_MERGE_BY_SUM);
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);

    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_STRING, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 10086);

    fieldstat_merge(instance_dest, instance);

    int *cube_id_dest;
    int n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id_dest, &n_cube);
    EXPECT_TRUE(n_cube == 1);
    int ret_cube_id = cube_id_dest[0];
    free(cube_id_dest);
    int ret_metric_id = fieldstat_get_max_metric_id(instance, ret_cube_id);
    EXPECT_EQ(ret_metric_id, 0);
    long long measure = my_fieldstat_counter_get(instance, cube_id, metric_id, cell_id);
    EXPECT_EQ(measure, 10086);

    int *ret_cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells(instance, cube_id, metric_id, &ret_cell_ids, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(tag_list->n_tag, 1);
    EXPECT_STREQ(tag_list->tag[0].key, TEST_TAG_STRING.key);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
    free(ret_cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
}

TEST(unit_test_merge, merge_existing_cell_on_existing_cube_and_metric_comprehensive)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_counter(instance, cube_id, "metric_name", COUNTER_MERGE_BY_SUM);
    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_STRING, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 5);
    struct fieldstat *instance_dest = fieldstat_new();

    fieldstat_merge(instance_dest, instance);
    fieldstat_merge(instance_dest, instance);

    int *cube_id_dest;
    int n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id_dest, &n_cube);
    EXPECT_TRUE(n_cube == 1);
    int ret_cube_id = cube_id_dest[0];
    free(cube_id_dest);
    int ret_metric_id = fieldstat_get_max_metric_id(instance_dest, ret_cube_id);
    EXPECT_EQ(ret_metric_id, 0);
    long long measure = my_fieldstat_counter_get(instance_dest, cube_id, metric_id, 0);
    EXPECT_EQ(measure, 10);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
}

TEST(unit_test_merge, new_too_many_cells_on_one_metric_given_source_cube_reset_and_get_different_cube_comprehensive)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 2);
    int metric_id = fieldstat_register_counter(instance, cube_id, "metric name", COUNTER_MERGE_BY_SUM);
    int cell_id1 = fieldstat_cube_add(instance, cube_id, &TEST_TAG_STRING, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id1, 1);
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);
    fieldstat_reset(instance);

    int cell_id2 = fieldstat_cube_add(instance, cube_id, &TEST_TAG_INT, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id2, 2);
    int cell_id3 = fieldstat_cube_add(instance, cube_id, &TEST_TAG_DOUBLE, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id3, 3);

    fieldstat_merge(instance_dest, instance);

    int *cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells(instance_dest, 0, 0, &cell_ids, &tag_list, &n_cell);
    Fieldstat_tag_list_wrapper tag_list_wrapper(tag_list);
    tag_list_wrapper.print_tag_list();
    EXPECT_EQ(n_cell, 2);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
    free(cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
}

TEST(unit_test_merge, new_too_many_cells_on_multiple_metric_given_source_cube_reset_and_get_different_cube_comprehensive)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 2);
    int metric_id1 = fieldstat_register_counter(instance, cube_id, "metric name1", COUNTER_MERGE_BY_SUM);
    int metric_id2 = fieldstat_register_counter(instance, cube_id, "metric name2", COUNTER_MERGE_BY_SUM);
    int cell_id1 = fieldstat_cube_add(instance, cube_id, &TEST_TAG_STRING, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id1, cell_id1, 1);
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);
    fieldstat_reset(instance);

    int metric_id3 = fieldstat_register_counter(instance, cube_id, "metric name3", COUNTER_MERGE_BY_SUM);
    int cell_id2 = fieldstat_cube_add(instance, cube_id, &TEST_TAG_INT, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id3, cell_id2, 2);
    int cell_id3 = fieldstat_cube_add(instance, cube_id, &TEST_TAG_DOUBLE, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id2, cell_id3, 3);

    fieldstat_merge(instance_dest, instance);

    int *cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells(instance_dest, 0, metric_id1, &cell_ids, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_STREQ(tag_list->tag[0].key, TEST_TAG_STRING.key);
    free(cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);

    fieldstat_get_cells(instance_dest, 0, metric_id2, &cell_ids, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 0);

    fieldstat_get_cells(instance_dest, 0, metric_id3, &cell_ids, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_STREQ(tag_list->tag[0].key, TEST_TAG_INT.key);
    free(cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
}

TEST(unit_test_merge, new_cube_and_metric_to_empty_topk)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_TAG_INT, 1, SAMPLING_MODE_TOPK, 10);
    fieldstat_register_counter(instance, cube_id, "metric_name", COUNTER_MERGE_BY_SUM);

    struct fieldstat *instance_dest = fieldstat_new();

    fieldstat_merge(instance_dest, instance);

    int *cube_id_dest;
    int n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id_dest, &n_cube);
    EXPECT_TRUE(n_cube == 1);
    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest, cube_id_dest[0], 0), "metric_name");

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
    free(cube_id_dest);
}

TEST(unit_test_merge, new_cell_on_existing_cube_and_metric_topk)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, 10);
    int metric_id = fieldstat_register_counter(instance, cube_id, "metric_name", COUNTER_MERGE_BY_SUM);
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);

    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_STRING, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 10086);

    fieldstat_merge(instance_dest, instance);

    int *cube_id_dest;
    int n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id_dest, &n_cube);
    EXPECT_TRUE(n_cube == 1);
    int ret_cube_id = cube_id_dest[0];
    free(cube_id_dest);
    int ret_metric_id = fieldstat_get_max_metric_id(instance, ret_cube_id);
    EXPECT_EQ(ret_metric_id, 0);
    long long measure = my_fieldstat_counter_get(instance, cube_id, metric_id, cell_id);
    EXPECT_EQ(measure, 10086);

    int *ret_cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells(instance, cube_id, metric_id, &ret_cell_ids, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(tag_list->n_tag, 1);
    EXPECT_STREQ(tag_list->tag[0].key, TEST_TAG_STRING.key);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
    free(ret_cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
}

TEST(unit_test_merge, merge_existing_cell_on_existing_cube_and_metric_topk)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, 10);
    int metric_id = fieldstat_register_counter(instance, cube_id, "metric_name", COUNTER_MERGE_BY_SUM);
    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_STRING, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 5);
    struct fieldstat *instance_dest = fieldstat_new();

    fieldstat_merge(instance_dest, instance);
    fieldstat_merge(instance_dest, instance);

    int *cube_id_dest;
    int n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id_dest, &n_cube);
    EXPECT_TRUE(n_cube == 1);
    int ret_cube_id = cube_id_dest[0];
    free(cube_id_dest);
    int ret_metric_id = fieldstat_get_max_metric_id(instance_dest, ret_cube_id);
    EXPECT_EQ(ret_metric_id, 0);

    int *ret_cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells(instance_dest, ret_cube_id, ret_metric_id, &ret_cell_ids, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    long long measure = my_fieldstat_counter_get(instance_dest, cube_id, metric_id, ret_cell_ids[0]);
    EXPECT_EQ(measure, 10);

    free(ret_cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
    fieldstat_free(instance);
    fieldstat_free(instance_dest);
}

TEST(unit_test_merge, new_too_many_cells_on_one_metric_given_source_cube_reset_and_get_different_cube_topk)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, 2);
    int metric_id = fieldstat_register_counter(instance, cube_id, "metric name", COUNTER_MERGE_BY_SUM);
    int cell_id1 = fieldstat_cube_add(instance, cube_id, &TEST_TAG_STRING, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id1, 1);
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);
    fieldstat_reset(instance);

    int cell_id2 = fieldstat_cube_add(instance, cube_id, &TEST_TAG_INT, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id2, 2);
    int cell_id3 = fieldstat_cube_add(instance, cube_id, &TEST_TAG_DOUBLE, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id3, 3);

    fieldstat_merge(instance_dest, instance);

    int *cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells(instance_dest, 0, 0, &cell_ids, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 2);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
    free(cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
}

TEST(unit_test_merge, new_too_many_cells_on_multiple_metric_given_source_cube_reset_and_get_different_cube_topk)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, 2);
    int metric_id1 = fieldstat_register_counter(instance, cube_id, "metric name1", COUNTER_MERGE_BY_SUM);
    int metric_id2 = fieldstat_register_counter(instance, cube_id, "metric name2", COUNTER_MERGE_BY_SUM);
    int cell_id1 = fieldstat_cube_add(instance, cube_id, &TEST_TAG_STRING, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id1, cell_id1, 1);
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);
    fieldstat_reset(instance);

    int metric_id3 = fieldstat_register_counter(instance, cube_id, "metric name3", COUNTER_MERGE_BY_SUM);
    int cell_id2 = fieldstat_cube_add(instance, cube_id, &TEST_TAG_INT, 1, 2);
    fieldstat_counter_incrby(instance, cube_id, metric_id3, cell_id2, 2);
    int cell_id3 = fieldstat_cube_add(instance, cube_id, &TEST_TAG_DOUBLE, 1, 3);
    fieldstat_counter_incrby(instance, cube_id, metric_id2, cell_id3, 3);

    fieldstat_merge(instance_dest, instance);

    int *cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells(instance_dest, 0, metric_id1, &cell_ids, &tag_list, &n_cell);
    int pop_cell_cnt = 0;
    bool int_tag_found = false;
    bool double_tag_found = false;
    if (n_cell == 1) {
        if (strcmp(tag_list->tag[0].key, TEST_TAG_INT.key) == 0) {
            int_tag_found = true;
        } else if (strcmp(tag_list->tag[0].key, TEST_TAG_DOUBLE.key) == 0) {
            double_tag_found = true;
        } else {
            printf("the tag is %s\n", tag_list->tag[0].key);
        }
        free(cell_ids);
        fieldstat_tag_list_arr_free(tag_list, n_cell);
    } else {
        EXPECT_EQ(n_cell, 0);
        pop_cell_cnt++;
    }

    fieldstat_get_cells(instance_dest, 0, metric_id2, &cell_ids, &tag_list, &n_cell);
    if (n_cell == 1) {
        if (strcmp(tag_list->tag[0].key, TEST_TAG_INT.key) == 0) {
            int_tag_found = true;
        } else if (strcmp(tag_list->tag[0].key, TEST_TAG_DOUBLE.key) == 0) {
            double_tag_found = true;
        } else {
            printf("the tag is %s\n", tag_list->tag[0].key);
        }
        free(cell_ids);
        fieldstat_tag_list_arr_free(tag_list, n_cell);
    } else {
        EXPECT_EQ(n_cell, 0);
        pop_cell_cnt++;
    }

    fieldstat_get_cells(instance_dest, 0, metric_id3, &cell_ids, &tag_list, &n_cell);
    if (n_cell == 1) {
        if (strcmp(tag_list->tag[0].key, TEST_TAG_INT.key) == 0) {
            int_tag_found = true;
        } else if (strcmp(tag_list->tag[0].key, TEST_TAG_DOUBLE.key) == 0) {
            double_tag_found = true;
        } else {
            printf("the tag is %s\n", tag_list->tag[0].key);
        }
        free(cell_ids);
        fieldstat_tag_list_arr_free(tag_list, n_cell);
    } else {
        EXPECT_EQ(n_cell, 0);
        pop_cell_cnt++;
    }
    EXPECT_EQ(pop_cell_cnt, 1);
    EXPECT_TRUE(int_tag_found);
    EXPECT_TRUE(double_tag_found);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
}

struct fieldstat *test_push_flows(vector<Fieldstat_tag_list_wrapper *> &flows_in_test, int K, long long count = 1)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, K);
    int metric_id = fieldstat_register_counter(instance, cube_id, "metric name", COUNTER_MERGE_BY_SUM);
    for (size_t i = 0; i < flows_in_test.size(); i++) {
        int cell_id = fieldstat_cube_add(instance, cube_id, flows_in_test[i]->get_tag(), flows_in_test[i]->get_tag_count(), count);
        if (cell_id < 0) {
            continue;
        }
        fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, count);
    }
    return instance;
}

TEST(unit_test_merge, merge_accuracy_test_with_K_large_enough_topk)
{
    int K = 100;
    vector<Fieldstat_tag_list_wrapper *> flows_in_src = test_gen_topk_flows(K, K);
    struct fieldstat *instance_src = test_push_flows(flows_in_src, K);
    vector<Fieldstat_tag_list_wrapper *> flows_in_dest = test_gen_topk_flows(K, K);
    struct fieldstat *instance_dest = test_push_flows(flows_in_dest, K);
    fieldstat_merge(instance_dest, instance_src);

    int *cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells(instance_dest, 0, 0, &cell_ids, &tag_list, &n_cell);

    vector<Fieldstat_tag_list_wrapper *> flows_in_merged;
    for (size_t i = 0; i < n_cell; i++) {
        flows_in_merged.push_back(new Fieldstat_tag_list_wrapper(&tag_list[i]));
    }

    flows_in_dest.insert(flows_in_dest.end(), std::make_move_iterator(flows_in_src.begin()), std::make_move_iterator(flows_in_src.end()));
    double accuracy = test_cal_accuracy_given_expected_key(flows_in_dest, flows_in_merged);
    EXPECT_TRUE(accuracy > 0.99); // should be 1.0

    fieldstat_free(instance_src);
    fieldstat_free(instance_dest);
    free(cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
    for (size_t i = 0; i < flows_in_merged.size(); i++) {
        delete flows_in_merged[i];
    }
    for (size_t i = 0; i < flows_in_dest.size(); i++) {
        delete flows_in_dest[i];
    }
}

TEST(unit_test_merge, merge_accuracy_test_gen_dest_full_all_inserted_given_src_flows_larger)
{
    int K = 100;
    vector<Fieldstat_tag_list_wrapper *> flows_in_src = test_gen_topk_flows(10000, K);
    struct fieldstat *instance_src = test_push_flows(flows_in_src, K, 1000);
    vector<Fieldstat_tag_list_wrapper *> flows_in_dest;
    for (int i = 0; i < K; i++) {
        Fieldstat_tag_list_wrapper *tmp = new Fieldstat_tag_list_wrapper("flows in dest", to_string(i).c_str());
        flows_in_dest.push_back(tmp);
    }
    struct fieldstat *instance_dest = test_push_flows(flows_in_dest, K, 1);

    fieldstat_merge(instance_dest, instance_src);

    int *cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells(instance_dest, 0, 0, &cell_ids, &tag_list, &n_cell);
    vector<Fieldstat_tag_list_wrapper *> flows_in_merged;
    for (size_t i = 0; i < n_cell; i++) {
        flows_in_merged.push_back(new Fieldstat_tag_list_wrapper(&tag_list[i]));
    }

    flows_in_dest.insert(flows_in_dest.end(), std::make_move_iterator(flows_in_src.begin()), std::make_move_iterator(flows_in_src.end()));
    double accuracy = test_cal_accuracy_given_expected_key(flows_in_dest, flows_in_merged);

    EXPECT_TRUE(accuracy > 0.99); // should be 1.0

    fieldstat_free(instance_src);
    fieldstat_free(instance_dest);
    free(cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
    for (size_t i = 0; i < flows_in_merged.size(); i++) {
        delete flows_in_merged[i];
    }
    for (size_t i = 0; i < flows_in_dest.size(); i++) {
        delete flows_in_dest[i];
    }
}

TEST(unit_test_merge, merge_accuracy_test_gen_dest_full_some_inserted_and_some_merged_and_some_fail_to_add)
{
    int K = 100;
    vector<Fieldstat_tag_list_wrapper *> flows_in_src = test_gen_topk_flows(10000, K + 50); // let elephant flows in src and dest different
    struct fieldstat *instance_src = test_push_flows(flows_in_src, K);
    vector<Fieldstat_tag_list_wrapper *> flows_in_dest = test_gen_topk_flows(10000, K + 50);
    struct fieldstat *instance_dest = test_push_flows(flows_in_dest, K);
    fieldstat_merge(instance_dest, instance_src);

    int *cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells(instance_dest, 0, 0, &cell_ids, &tag_list, &n_cell);
    vector<Fieldstat_tag_list_wrapper *> flows_in_merged;
    for (size_t i = 0; i < n_cell; i++) {
        flows_in_merged.push_back(new Fieldstat_tag_list_wrapper(&tag_list[i]));
    }
    
    flows_in_dest.insert(flows_in_dest.end(), std::make_move_iterator(flows_in_src.begin()), std::make_move_iterator(flows_in_src.end()));
    double accuracy = test_cal_accuracy_given_expected_key(flows_in_dest, flows_in_merged);
    EXPECT_TRUE(accuracy >= 0.93); // by heavy keeper benchmark, with K = 100, merging result should be about 0.96, for adding the flows will also cause some inaccuracy, so here we set 0.93
    printf("merge_accuracy_test_gen_dest_full_some_inserted_and_some_merged_and_some_fail_to_add accuracy is %lf\n", accuracy);

    fieldstat_free(instance_src);
    fieldstat_free(instance_dest);
    free(cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
    for (size_t i = 0; i < flows_in_merged.size(); i++) {
        delete flows_in_merged[i];
    }
    for (size_t i = 0; i < flows_in_dest.size(); i++) {
        delete flows_in_dest[i];
    }
}

TEST(unit_test_merge, exception_test_given_cell_added_but_no_metric_operation_given_empty_dst)
{
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat *instance2 = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, 2);
    int metric_id = fieldstat_register_counter(instance, cube_id, "metric name1", COUNTER_MERGE_BY_SUM);
    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_STRING, 1, 1);

    fieldstat_merge(instance2, instance);

    int *cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells(instance2, cube_id, metric_id, &cell_ids, &tag_list, &n_cell);
    EXPECT_TRUE(n_cell == 0);
    long long value;
    EXPECT_EQ(fieldstat_counter_get(instance2, cube_id, metric_id, cell_id, &value), -1);

    fieldstat_free(instance);
    fieldstat_free(instance2);
}

TEST(unit_test_merge, exception_test_given_cell_added_but_no_metric_operation_given_dst_with_cell_topk)
{
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat *instance2 = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, 2);
    int metric_id = fieldstat_register_counter(instance, cube_id, "metric name1", COUNTER_MERGE_BY_SUM);
    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_STRING, 1, 1);

    int cube_id_dst = fieldstat_register_cube(instance2, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, 2);
    int metric_id_dst = fieldstat_register_counter(instance2, cube_id_dst, "metric name1", COUNTER_MERGE_BY_SUM);
    int cell_id_dst = fieldstat_cube_add(instance2, cube_id_dst, &TEST_TAG_STRING, 1, 1);
    fieldstat_counter_incrby(instance2, cube_id_dst, metric_id_dst, cell_id_dst, 123);

    fieldstat_merge(instance2, instance);

    int *cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells(instance2, cube_id, metric_id, &cell_ids, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(my_fieldstat_counter_get(instance2, cube_id, metric_id, cell_id), 123);

    fieldstat_free(instance);
    fieldstat_free(instance2);

    free(cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}