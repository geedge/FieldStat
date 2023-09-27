
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

long long merge_test_fieldstat_counter_get(const struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag_list *tag_list = &TEST_TAG_LIST_STRING)
{
    long long ret = 0;
    fieldstat_counter_get(instance, cube_id, metric_id, tag_list, &ret);
    return ret;
}

TEST(unit_test_merge, test_metric_name_mapping_with_new_metric_on_existing_cube)
{
    struct fieldstat *instance = fieldstat_new();

    int cube_id1 = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id_1_0 = fieldstat_register_counter(instance, "metric_name cube1 cube2");
    int metric_id_1_1 = fieldstat_register_counter(instance, "shared name");
    fieldstat_counter_incrby(instance, cube_id1, metric_id_1_0, &TEST_TAG_STRING, 1, 1);
    fieldstat_counter_incrby(instance, cube_id1, metric_id_1_1, &TEST_TAG_STRING, 1, 2);
    int cube_id2 = fieldstat_create_cube(instance, &TEST_TAG_INT, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_counter_incrby(instance, cube_id2, metric_id_1_0, &TEST_TAG_STRING, 1, 3);

    struct fieldstat *instance_dest = fieldstat_new();
    int cube_id_dest = fieldstat_create_cube(instance_dest, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    (void)fieldstat_register_counter(instance_dest, "shared name");
    // shared name metric is not operated on cube_id_dest

    EXPECT_EQ(fieldstat_merge(instance_dest, instance), FS_OK);

    int *cube_id;
    int n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id, &n_cube);
    EXPECT_TRUE(n_cube == 2);
    EXPECT_TRUE(cube_id[0] == cube_id_dest);

    int *metric_ids;
    size_t n_metrics;
    fieldstat_get_metrics_used_by_cube(instance_dest, cube_id_dest, &metric_ids, &n_metrics);
    EXPECT_EQ(n_metrics, 2);
    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest, metric_ids[0]), "shared name");
    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest, metric_ids[1]), "metric_name cube1 cube2");

    EXPECT_EQ(merge_test_fieldstat_counter_get(instance_dest, cube_id_dest, 0), 2); // shared name
    EXPECT_EQ(merge_test_fieldstat_counter_get(instance_dest, cube_id_dest, 1), 1); // metric_name cube1 cube2 on cube1
    EXPECT_EQ(merge_test_fieldstat_counter_get(instance_dest, cube_id[1], 1), 3); // metric_name cube1 cube2 on cube2

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
    free(cube_id);
    free(metric_ids);
}

TEST(unit_test_merge, cube_shared_tag_mapping_with_new_cube)
{
    struct fieldstat *instance = fieldstat_new();
    (void)fieldstat_create_cube(instance, &TEST_TAG_DOUBLE, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int cube_id2 = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_counter(instance, "metric in cube 2");
    fieldstat_counter_incrby(instance, cube_id2, metric_id, &TEST_TAG_STRING, 1, 1);
    struct fieldstat *instance_dest = fieldstat_new();
    int cube_id_dest = fieldstat_create_cube(instance_dest, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);

    fieldstat_merge(instance_dest, instance);

    int *cube_id;
    int n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id, &n_cube);
    EXPECT_TRUE(n_cube == 2);
    EXPECT_EQ(merge_test_fieldstat_counter_get(instance_dest, cube_id_dest, 0), 1);

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
    fieldstat_create_cube(instance, &TEST_TAG_INT, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_counter(instance, "metric_name");

    struct fieldstat *instance_dest = fieldstat_new();

    fieldstat_merge(instance_dest, instance);

    int *cube_id_dest;
    int n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id_dest, &n_cube);
    EXPECT_TRUE(n_cube == 1);
    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest, 0), "metric_name");

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
    free(cube_id_dest);
}

TEST(unit_test_merge, new_cell_on_existing_cube_and_metric_comprehensive)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_counter(instance, "metric_name");
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);

    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_STRING, 1, 10086);
    fieldstat_merge(instance_dest, instance);

    int *cube_id_dest;
    int n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id_dest, &n_cube);
    EXPECT_TRUE(n_cube == 1);
    free(cube_id_dest);
    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest, 0), "metric_name");
    long long measure = merge_test_fieldstat_counter_get(instance, cube_id, metric_id);
    EXPECT_EQ(measure, 10086);

    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells_used_by_metric(instance, cube_id, metric_id, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(tag_list->n_tag, 1);
    EXPECT_STREQ(tag_list->tag[0].key, TEST_TAG_STRING.key);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
}

TEST(unit_test_merge, merge_existing_cell_on_existing_cube_and_metric_comprehensive)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_counter(instance, "metric_name");
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_STRING, 1, 5);
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);

    fieldstat_merge(instance_dest, instance);

    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest, 0), "metric_name");
    long long measure = merge_test_fieldstat_counter_get(instance_dest, cube_id, metric_id);
    EXPECT_EQ(measure, 10);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
}

TEST(unit_test_merge, new_too_many_cells_on_one_metric_given_source_cube_reset_and_get_different_cube_comprehensive)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 2); // limit is 2
    int metric_id = fieldstat_register_counter(instance, "metric name");
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_STRING, 1, 1);
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);

    fieldstat_reset(instance);

    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_INT, 1, 2); // 2nd cell
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_DOUBLE, 1, 3); // 3rd cell, exceeding the limit 2

    fieldstat_merge(instance_dest, instance);

    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells_used_by_metric(instance_dest, 0, 0, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 2);
    EXPECT_EQ(merge_test_fieldstat_counter_get(instance_dest, 0, 0, &tag_list[0]), 1);
    EXPECT_EQ(merge_test_fieldstat_counter_get(instance_dest, 0, 0, &tag_list[1]), 2);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
}

TEST(unit_test_merge, new_too_many_cells_on_multiple_metric_given_source_cube_reset_and_get_different_cube_comprehensive)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 2);
    int metric_id1 = fieldstat_register_counter(instance, "metric name1");
    int metric_id2 = fieldstat_register_counter(instance, "metric name2");
    fieldstat_counter_incrby(instance, cube_id, metric_id1, &TEST_TAG_STRING, 1, 1); // 1st cell on metric name1
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);
    fieldstat_reset(instance);

    int metric_id3 = fieldstat_register_counter(instance, "metric name3");
    fieldstat_counter_incrby(instance, cube_id, metric_id3, &TEST_TAG_INT, 1, 2); // 2nd cell on metric name3, this is a metric dest dont have
    fieldstat_counter_incrby(instance, cube_id, metric_id2, &TEST_TAG_DOUBLE, 1, 3); // 3nd cell on metric name2
    fieldstat_merge(instance_dest, instance);

    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells_used_by_metric(instance_dest, 0, metric_id1, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_STREQ(tag_list->tag[0].key, TEST_TAG_STRING.key);
    fieldstat_tag_list_arr_free(tag_list, n_cell);

    fieldstat_get_cells_used_by_metric(instance_dest, 0, metric_id2, &tag_list, &n_cell); // 3nd cell failed to merge
    EXPECT_EQ(n_cell, 0);

    fieldstat_get_cells_used_by_metric(instance_dest, 0, metric_id3, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_STREQ(tag_list->tag[0].key, TEST_TAG_INT.key);
    fieldstat_tag_list_arr_free(tag_list, n_cell);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
}

TEST(unit_test_merge, new_cube_and_metric_to_empty_topk)
{
    struct fieldstat *instance = fieldstat_new();
    fieldstat_create_cube(instance, &TEST_TAG_INT, 1, SAMPLING_MODE_TOPK, 10);
    fieldstat_register_counter(instance, "metric_name");

    struct fieldstat *instance_dest = fieldstat_new();

    fieldstat_merge(instance_dest, instance);

    int *cube_id_dest;
    int n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id_dest, &n_cube);
    EXPECT_TRUE(n_cube == 1);
    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest, 0), "metric_name");

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
    free(cube_id_dest);
}

TEST(unit_test_merge, new_cell_on_existing_cube_and_metric_topk)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, 10);
    int metric_id = fieldstat_register_counter(instance, "metric_name");
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);

    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_STRING, 1, 10086);
    fieldstat_merge(instance_dest, instance);

    int *cube_id_dest;
    int n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id_dest, &n_cube);
    EXPECT_TRUE(n_cube == 1);
    free(cube_id_dest);
    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest, 0), "metric_name");
    long long measure = merge_test_fieldstat_counter_get(instance, cube_id, metric_id);
    EXPECT_EQ(measure, 10086);

    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells_used_by_metric(instance, cube_id, metric_id, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(tag_list->n_tag, 1);
    EXPECT_STREQ(tag_list->tag[0].key, TEST_TAG_STRING.key);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
}

TEST(unit_test_merge, merge_existing_cell_on_existing_cube_and_metric_topk)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, 10);
    int metric_id = fieldstat_register_counter(instance, "metric_name");
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_STRING, 1, 5);
    struct fieldstat *instance_dest = fieldstat_new();

    fieldstat_merge(instance_dest, instance);
    fieldstat_merge(instance_dest, instance);

    int *cube_id_dest;
    int n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id_dest, &n_cube);
    EXPECT_TRUE(n_cube == 1);
    int ret_cube_id = cube_id_dest[0];
    free(cube_id_dest);
    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest, 0), "metric_name");

    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells_used_by_metric(instance_dest, ret_cube_id, 0, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    long long measure = merge_test_fieldstat_counter_get(instance_dest, cube_id, metric_id, &tag_list[0]);
    EXPECT_EQ(measure, 10);

    fieldstat_tag_list_arr_free(tag_list, n_cell);
    fieldstat_free(instance);
    fieldstat_free(instance_dest);
}

TEST(unit_test_merge, new_too_many_cells_on_one_metric_given_source_cube_reset_and_get_different_cube_topk)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, 2);
    int metric_id = fieldstat_register_counter(instance, "metric name");
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_STRING, 1, 1);
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);

    fieldstat_reset(instance);

    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_INT, 1, 2); // 2nd cell
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_DOUBLE, 1, 3); // 3rd cell,bigger than the others, so keep it

    fieldstat_merge(instance_dest, instance);

    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells_used_by_metric(instance_dest, 0, 0, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 2);
    EXPECT_EQ(merge_test_fieldstat_counter_get(instance_dest, 0, 0, &tag_list[0]), 3);
    EXPECT_EQ(merge_test_fieldstat_counter_get(instance_dest, 0, 0, &tag_list[1]), 2);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
}

struct fieldstat *test_push_flows(vector<Fieldstat_tag_list_wrapper *> &flows_in_test, int K, long long count = 1)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, K);
    int metric_id = fieldstat_register_counter(instance, "metric name");
    for (size_t i = 0; i < flows_in_test.size(); i++) {
        fieldstat_counter_incrby(instance, cube_id, metric_id, flows_in_test[i]->get_tag(), flows_in_test[i]->get_tag_count(), count);
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

    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells_used_by_metric(instance_dest, 0, 0, &tag_list, &n_cell);

    vector<Fieldstat_tag_list_wrapper *> flows_in_merged;
    for (size_t i = 0; i < n_cell; i++) {
        flows_in_merged.push_back(new Fieldstat_tag_list_wrapper(&tag_list[i]));
    }

    flows_in_dest.insert(flows_in_dest.end(), std::make_move_iterator(flows_in_src.begin()), std::make_move_iterator(flows_in_src.end()));
    double accuracy = test_cal_accuracy_given_expected_key(flows_in_dest, flows_in_merged);
    EXPECT_TRUE(accuracy > 0.99); // should be 1.0

    fieldstat_free(instance_src);
    fieldstat_free(instance_dest);
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
    struct fieldstat *instance_src = test_push_flows(flows_in_src, K, 1000); // 1000 times larger than dest 1
    vector<Fieldstat_tag_list_wrapper *> flows_in_dest;
    for (int i = 0; i < K; i++) {
        Fieldstat_tag_list_wrapper *tmp = new Fieldstat_tag_list_wrapper("flows in dest", to_string(i).c_str());
        flows_in_dest.push_back(tmp);
    }
    struct fieldstat *instance_dest = test_push_flows(flows_in_dest, K, 1);

    fieldstat_merge(instance_dest, instance_src);

    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells_used_by_metric(instance_dest, 0, 0, &tag_list, &n_cell);
    vector<Fieldstat_tag_list_wrapper *> flows_in_merged;
    for (size_t i = 0; i < n_cell; i++) {
        flows_in_merged.push_back(new Fieldstat_tag_list_wrapper(&tag_list[i]));
    }

    flows_in_dest.insert(flows_in_dest.end(), std::make_move_iterator(flows_in_src.begin()), std::make_move_iterator(flows_in_src.end()));
    double accuracy = test_cal_accuracy_given_expected_key(flows_in_dest, flows_in_merged);

    EXPECT_TRUE(accuracy > 0.99); // should be 1.0

    fieldstat_free(instance_src);
    fieldstat_free(instance_dest);
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

    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells_used_by_metric(instance_dest, 0, 0, &tag_list, &n_cell);
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
    fieldstat_tag_list_arr_free(tag_list, n_cell);
    for (size_t i = 0; i < flows_in_merged.size(); i++) {
        delete flows_in_merged[i];
    }
    for (size_t i = 0; i < flows_in_dest.size(); i++) {
        delete flows_in_dest[i];
    }
}

// todo: primary key 不同的情况
// todo: primary key 上一个操作都没有

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}