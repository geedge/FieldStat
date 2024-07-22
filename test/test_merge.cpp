
#include <gtest/gtest.h>
#include <set>
#include <unordered_map>
#include <unordered_set>
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

long long merge_test_fieldstat_counter_get(const struct fieldstat *instance, int cube_id, int metric_id, const struct field_list *tag_list = &TEST_TAG_LIST_STRING)
{
    long long ret = 0;
    fieldstat_counter_get(instance, cube_id, tag_list, metric_id, &ret);
    return ret;
}

double merge_test_fieldstat_hll_get(const struct fieldstat *instance, int cube_id, int metric_id, const struct field_list *tag_list = &TEST_TAG_LIST_STRING)
{
    double ret = 0;
    fieldstat_hll_get(instance, cube_id, tag_list, metric_id, &ret);
    return ret;
}

int test_fieldstat_cube_create(struct fieldstat *instance, const struct field *dimensions, size_t n_dimensions, enum sampling_mode mode, int k, int primary_metric_id=0)
{
    assert(mode == SAMPLING_MODE_COMPREHENSIVE);
    int ret = fieldstat_cube_create(instance, dimensions, n_dimensions);
    fieldstat_cube_set_sampling(instance, ret, mode, k, primary_metric_id);
    return ret;
}

TEST(unit_test_merge, cube_shared_tag_mapping_with_new_cube)
{
    struct fieldstat *instance = fieldstat_new();
    (void)test_fieldstat_cube_create(instance, &TEST_TAG_DOUBLE, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int cube_id2 = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_counter(instance,cube_id2,"metric in cube 2");
    fieldstat_counter_incrby(instance, cube_id2, metric_id, &TEST_TAG_STRING, 1, 1);
    struct fieldstat *instance_dest = fieldstat_new();
    int cube_id_dest = test_fieldstat_cube_create(instance_dest, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);

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
    test_fieldstat_cube_create(instance, &TEST_TAG_INT, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_counter(instance, 0, "metric_name");

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
    int cube_id = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_counter(instance, 0, "metric_name");
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);

    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_STRING, 1, 10086);
    fieldstat_merge(instance_dest, instance);

    int *cube_id_dest;
    int n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id_dest, &n_cube);
    EXPECT_TRUE(n_cube == 1);
    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest, cube_id_dest[0], 0), "metric_name");
    free(cube_id_dest);
    long long measure = merge_test_fieldstat_counter_get(instance, cube_id, metric_id);
    EXPECT_EQ(measure, 10086);

    struct field_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance, cube_id, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(tag_list->n_field, 1);
    EXPECT_STREQ(tag_list->field[0].key, TEST_TAG_STRING.key);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
}

TEST(unit_test_merge, merge_existing_cell_on_existing_cube_and_metric_comprehensive)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_counter(instance, cube_id, "metric_name");
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_STRING, 1, 5);
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);

    fieldstat_merge(instance_dest, instance);

    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest, cube_id, 0), "metric_name");
    long long measure = merge_test_fieldstat_counter_get(instance_dest, cube_id, metric_id);
    EXPECT_EQ(measure, 10);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
}

TEST(unit_test_merge, new_too_many_cells_on_one_metric_given_source_cube_reset_and_get_different_cube_comprehensive)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 2); // limit is 2
    int metric_id = fieldstat_register_counter(instance, cube_id, "metric name");
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_STRING, 1, 1);
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);

    fieldstat_reset(instance);

    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_INT, 1, 2); // 2nd cell
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_DOUBLE, 1, 3); // 3rd cell, exceeding the limit 2

    fieldstat_merge(instance_dest, instance);

    struct field_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance_dest, 0, &tag_list, &n_cell);
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
    int cube_id = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 2);
    int metric_id1 = fieldstat_register_counter(instance, cube_id, "metric name1");
    int metric_id2 = fieldstat_register_counter(instance, cube_id, "metric name2");
    fieldstat_counter_incrby(instance, cube_id, metric_id1, &TEST_TAG_STRING, 1, 1); // 1st cell on metric name1
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);
    fieldstat_reset(instance);

    int metric_id3 = fieldstat_register_counter(instance, cube_id, "metric name3");
    fieldstat_counter_incrby(instance, cube_id, metric_id3, &TEST_TAG_INT, 1, 2); // 2nd cell on metric name3, this is a metric dest dont have
    fieldstat_counter_incrby(instance, cube_id, metric_id2, &TEST_TAG_DOUBLE, 1, 3); // 3nd cell on metric name2
    fieldstat_merge(instance_dest, instance);

    int *metric_ids = NULL;
    size_t n_metrics = 0;
    fieldstat_get_metric_in_cell(instance_dest, 0, &TEST_TAG_LIST_STRING, &metric_ids, &n_metrics);
    EXPECT_EQ(n_metrics, 1);
    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest, 0, metric_ids[0]), "metric name1");
    free(metric_ids);

    fieldstat_get_metric_in_cell(instance_dest, 0, &TEST_TAG_LIST_INT, &metric_ids, &n_metrics);
    EXPECT_EQ(n_metrics, 1);
    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest, 0, metric_ids[0]), "metric name3");
    free(metric_ids);
     // 3nd cell failed to merge, because max sampling is 2
    fieldstat_get_metric_in_cell(instance_dest, 0, &TEST_TAG_LIST_DOUBLE, &metric_ids, &n_metrics);
    EXPECT_EQ(n_metrics, 0);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
}

TEST(unit_test_merge, new_cube_and_metric_to_empty_topk)
{
    struct fieldstat *instance = fieldstat_new();
    fieldstat_cube_create(instance, &TEST_TAG_INT, 1);
    fieldstat_register_counter(instance, 0, "metric_name");
    fieldstat_cube_set_sampling(instance, 0, SAMPLING_MODE_TOPK, 10, 0);

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
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int metric_id = fieldstat_register_counter(instance, cube_id, "metric_name");
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOPK, 10, 0);
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);

    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_STRING, 1, 10086);
    fieldstat_merge(instance_dest, instance);

    int *cube_id_dest;
    int n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id_dest, &n_cube);
    EXPECT_TRUE(n_cube == 1);
    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest,cube_id_dest[0], 0), "metric_name");
    free(cube_id_dest);
    long long measure = merge_test_fieldstat_counter_get(instance, cube_id, metric_id);
    EXPECT_EQ(measure, 10086);

    struct field_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance, cube_id, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(tag_list->n_field, 1);
    EXPECT_STREQ(tag_list->field[0].key, TEST_TAG_STRING.key);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
}

TEST(unit_test_merge, merge_existing_cell_on_existing_cube_and_metric_topk)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int metric_id = fieldstat_register_counter(instance, cube_id, "metric_name");
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOPK, 10, 0);
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_STRING, 1, 5);
    struct fieldstat *instance_dest = fieldstat_new();

    fieldstat_merge(instance_dest, instance);
    fieldstat_merge(instance_dest, instance);

    int *cube_id_dest;
    int n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id_dest, &n_cube);
    EXPECT_TRUE(n_cube == 1);
    int ret_cube_id = cube_id_dest[0];
    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest,cube_id_dest[0], 0), "metric_name");
    free(cube_id_dest);

    struct field_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance_dest, ret_cube_id, &tag_list, &n_cell);
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
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int metric_id = fieldstat_register_counter(instance, cube_id, "metric name");
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOPK, 2, 0);
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_STRING, 1, 1);
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);

    fieldstat_reset(instance);

    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_INT, 1, 2); // 2nd cell
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_DOUBLE, 1, 3); // 3rd cell,bigger than the others, so keep it

    fieldstat_merge(instance_dest, instance);

    struct field_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance_dest, 0, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 2);
    EXPECT_EQ(merge_test_fieldstat_counter_get(instance_dest, 0, 0, &tag_list[0]), 3);
    EXPECT_EQ(merge_test_fieldstat_counter_get(instance_dest, 0, 0, &tag_list[1]), 2);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
}

struct fieldstat *topk_test_push_flows(vector<Fieldstat_tag_list_wrapper *> &flows_in_test, int K, long long count = 1)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int metric_id = fieldstat_register_counter(instance, cube_id, "metric name");
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOPK, K, 0);
    for (size_t i = 0; i < flows_in_test.size(); i++) {
        fieldstat_counter_incrby(instance, cube_id, metric_id, flows_in_test[i]->get_tag(), flows_in_test[i]->get_tag_count(), count);
    }
    return instance;
}

TEST(unit_test_merge, merge_accuracy_test_with_K_large_enough_topk)
{
    int K = 100;
    vector<Fieldstat_tag_list_wrapper *> flows_in_src = test_gen_topk_flows(K, K);
    struct fieldstat *instance_src = topk_test_push_flows(flows_in_src, K);
    vector<Fieldstat_tag_list_wrapper *> flows_in_dest = test_gen_topk_flows(K, K);
    struct fieldstat *instance_dest = topk_test_push_flows(flows_in_dest, K);
    fieldstat_merge(instance_dest, instance_src);

    struct field_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance_dest, 0, &tag_list, &n_cell);

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

TEST(unit_test_merge, merge_accuracy_test_gen_dest_full_all_inserted_given_src_flows_larger_topk)
{
    int K = 1000;
    vector<Fieldstat_tag_list_wrapper *> flows_in_src = test_gen_topk_flows(10000, K);
    struct fieldstat *instance_src = topk_test_push_flows(flows_in_src, K, 1000); // 1000 times larger than dest 1
    vector<Fieldstat_tag_list_wrapper *> flows_in_dest;
    for (int i = 0; i < K; i++) {
        Fieldstat_tag_list_wrapper *tmp = new Fieldstat_tag_list_wrapper("flows in dest", to_string(i).c_str());
        flows_in_dest.push_back(tmp);
    }
    struct fieldstat *instance_dest = topk_test_push_flows(flows_in_dest, K, 1);

    fieldstat_merge(instance_dest, instance_src);

    struct field_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance_dest, 0, &tag_list, &n_cell);
    vector<Fieldstat_tag_list_wrapper *> flows_in_merged;
    for (size_t i = 0; i < n_cell; i++) {
        flows_in_merged.push_back(new Fieldstat_tag_list_wrapper(&tag_list[i]));
    }

    flows_in_dest.insert(flows_in_dest.end(), std::make_move_iterator(flows_in_src.begin()), std::make_move_iterator(flows_in_src.end()));
    double accuracy = test_cal_accuracy_given_expected_key(flows_in_dest, flows_in_merged);

    EXPECT_GT(accuracy, 0.999); // should be 1.0

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

TEST(unit_test_merge, merge_accuracy_test_gen_dest_full_some_inserted_and_some_merged_and_some_fail_to_add_topk)
{
    int K = 100;
    vector<Fieldstat_tag_list_wrapper *> flows_in_src = test_gen_topk_flows(30000, K + 50); // let elephant flows in src and dest different
    struct fieldstat *instance_src = topk_test_push_flows(flows_in_src, K);
    vector<Fieldstat_tag_list_wrapper *> flows_in_dest = test_gen_topk_flows(30000, K + 50);
    struct fieldstat *instance_dest = topk_test_push_flows(flows_in_dest, K);
    fieldstat_merge(instance_dest, instance_src);

    struct field_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance_dest, 0, &tag_list, &n_cell);
    vector<Fieldstat_tag_list_wrapper *> flows_in_merged;
    for (size_t i = 0; i < n_cell; i++) {
        flows_in_merged.push_back(new Fieldstat_tag_list_wrapper(&tag_list[i]));
    }
    
    flows_in_dest.insert(flows_in_dest.end(), std::make_move_iterator(flows_in_src.begin()), std::make_move_iterator(flows_in_src.end()));
    double accuracy = test_cal_accuracy_given_expected_key(flows_in_dest, flows_in_merged);
    EXPECT_GE(accuracy, 0.87);
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

TEST(unit_test_merge, primary_metric_has_no_value)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int metric_primary = fieldstat_register_counter(instance, cube_id, "primary");
    int metric_operated = fieldstat_register_counter(instance, cube_id, "operated");
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOPK, 2, metric_primary);
    fieldstat_counter_incrby(instance, cube_id, metric_operated, &TEST_TAG_STRING, 1, 1);
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);
    fieldstat_merge(instance_dest, instance);

    struct field_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance_dest, 0, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_STREQ(tag_list[0].field[0].key, TEST_TAG_STRING.key);

    EXPECT_EQ(merge_test_fieldstat_counter_get(instance_dest, 0, metric_operated, &tag_list[0]), 2);
    EXPECT_EQ(merge_test_fieldstat_counter_get(instance_dest, 0, metric_primary, &tag_list[0]), 0);
    fieldstat_tag_list_arr_free(tag_list, n_cell);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
}

TEST(unit_test_merge, primary_metric_id_different)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int metric_primary = fieldstat_register_counter(instance, cube_id, "primary");
    int metric_2 = fieldstat_register_counter(instance, cube_id, "2");
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOPK, 2, metric_primary);

    fieldstat_counter_incrby(instance, cube_id, metric_primary, &TEST_TAG_STRING, 1, 100);
    fieldstat_counter_incrby(instance, cube_id, metric_2, &TEST_TAG_STRING, 1, 1);

    struct fieldstat *instance_dst = fieldstat_new();
    int cube_id_dst = fieldstat_cube_create(instance_dst, &TEST_SHARED_TAG, 1);
    fieldstat_register_counter(instance_dst, cube_id_dst, "2");
    int metric_primary_dst = fieldstat_register_counter(instance_dst, cube_id_dst, "primary");
    fieldstat_cube_set_sampling(instance_dst, cube_id_dst, SAMPLING_MODE_TOPK, 2, metric_primary_dst);

    EXPECT_EQ(fieldstat_merge(instance_dst, instance), FS_ERR_DIFFERENT_CONFIGURATION_FOR_SAME_CUBE);

    fieldstat_free(instance);
    fieldstat_free(instance_dst);
}

TEST(unit_test_merge, new_cube_and_metric_to_empty_spreadsketch) {
    struct fieldstat *instance = fieldstat_new();
    fieldstat_cube_create(instance, &TEST_TAG_INT, 1);
    fieldstat_register_hll(instance, 0, "metric", 6);
    fieldstat_cube_set_sampling(instance, 0, SAMPLING_MODE_TOP_CARDINALITY, 10, 0);

    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);

    int *cube_id_dest;
    int n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id_dest, &n_cube);
    EXPECT_TRUE(n_cube == 1);
    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest, cube_id_dest[0], 0), "metric");

    free(cube_id_dest);
    fieldstat_free(instance);
    fieldstat_free(instance_dest);
}

TEST(unit_test_merge, new_cell_on_existing_cube_and_metric_spreadsketch) {
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int metric_id = fieldstat_register_hll(instance, cube_id, "metric", 6);
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOP_CARDINALITY, 10, 0);
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);

    fieldstat_hll_add(instance, cube_id, metric_id, &TEST_TAG_STRING, 1, "1", 1);
    fieldstat_hll_add(instance, cube_id, metric_id, &TEST_TAG_STRING, 1, "2", 1);
    fieldstat_merge(instance_dest, instance);

    int *cube_id_dest;
    int n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id_dest, &n_cube);
    EXPECT_TRUE(n_cube == 1);
    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest, cube_id_dest[0], 0), "metric");
    long long measure = merge_test_fieldstat_hll_get(instance, cube_id, metric_id);
    EXPECT_NEAR(measure, 2, 0.3);

    struct field_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance, cube_id, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(tag_list->n_field, 1);
    EXPECT_STREQ(tag_list->field[0].key, TEST_TAG_STRING.key);

    free(cube_id_dest);
    fieldstat_free(instance);
    fieldstat_free(instance_dest);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
}

TEST(unit_test_merge, merge_existing_cell_on_existing_cube_and_metric_spreadsketch) {
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int metric_id = fieldstat_register_hll(instance, cube_id, "metric", 6);
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOP_CARDINALITY, 10, 0);
    fieldstat_hll_add(instance, cube_id, metric_id, &TEST_TAG_STRING, 1, "1", 1);
    struct fieldstat *instance_dest = fieldstat_new();

    fieldstat_merge(instance_dest, instance);
    fieldstat_merge(instance_dest, instance);
    fieldstat_hll_add(instance, cube_id, metric_id, &TEST_TAG_STRING, 1, "2", 1);
    fieldstat_merge(instance_dest, instance);

    struct field_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance_dest, cube_id, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    double value = merge_test_fieldstat_hll_get(instance_dest, cube_id, metric_id, &tag_list[0]);
    EXPECT_NEAR(value, 2, 0.3);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
}

TEST(unit_test_merge, new_too_many_cells_on_one_metric_given_source_cube_reset_and_get_different_cube_spreadsketch) {
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int metric_id = fieldstat_register_hll(instance, cube_id, "metric", 6);
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOP_CARDINALITY, 2, 0);
    fieldstat_hll_add(instance, cube_id, metric_id, &TEST_TAG_STRING, 1, "1", 1);
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);

    fieldstat_reset(instance);
    fieldstat_hll_add(instance, cube_id, metric_id, &TEST_TAG_INT, 1, "21", 2);
    fieldstat_hll_add(instance, cube_id, metric_id, &TEST_TAG_INT, 1, "22", 2);
    fieldstat_hll_add(instance, cube_id, metric_id, &TEST_TAG_DOUBLE, 1, "31", 2);
    fieldstat_hll_add(instance, cube_id, metric_id, &TEST_TAG_DOUBLE, 1, "32", 2);
    fieldstat_hll_add(instance, cube_id, metric_id, &TEST_TAG_DOUBLE, 1, "33", 2);
    fieldstat_merge(instance_dest, instance);

    struct field_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance_dest, 0, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 2);
    EXPECT_NEAR(merge_test_fieldstat_hll_get(instance_dest, 0, 0, &tag_list[0]), 3, 0.3);
    EXPECT_NEAR(merge_test_fieldstat_hll_get(instance_dest, 0, 0, &tag_list[1]), 2, 0.3);
    EXPECT_STREQ(tag_list[0].field[0].key, TEST_TAG_DOUBLE.key);
    EXPECT_STREQ(tag_list[1].field[0].key, TEST_TAG_INT.key);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
}


TEST(unit_test_merge, gen_dest_full_all_src_inserted_given_src_flows_larger_spreadsketch) {
    int K = 100;
    SpreadSketchZipfGenerator flow_generator(1.0, K); // exactly the number of cells, so there will be almost all(in case of hash collision happen) cells added successfully
    struct fieldstat *instance_src = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance_src, &TEST_SHARED_TAG, 1);
    int metric_id = fieldstat_register_hll(instance_src, cube_id, "metric", 6);
    fieldstat_cube_set_sampling(instance_src, cube_id, SAMPLING_MODE_TOP_CARDINALITY, K, 0);
    struct fieldstat *instance_dest = fieldstat_fork(instance_src);
    const char dest_key[] = "key of dest";
    const char src_key[] = "key of src";

    std::unordered_map<std::string, std::unordered_set<std::string>> flow_cnt;
    for (int i = 0; i < 500000; i++) { // add more, so the fanout of any flow to src instance is more than dest
        Flow flow = flow_generator.next();
        Fieldstat_tag_list_wrapper dimension = Fieldstat_tag_list_wrapper(src_key, flow.src_ip.c_str());
        Fieldstat_tag_list_wrapper item = Fieldstat_tag_list_wrapper("dummy", flow.dst_ip.c_str());
        fieldstat_hll_add_field(instance_src, cube_id, metric_id, dimension.get_tag(), dimension.get_tag_count(), item.get_tag(), item.get_tag_count());

        flow_cnt[dimension.to_string()].insert(item.to_string());
    }
    
    for (int i = 0; i < 1000; i++) {
        Flow flow = flow_generator.next();
        Fieldstat_tag_list_wrapper dimension = Fieldstat_tag_list_wrapper(dest_key, flow.src_ip.c_str());
        Fieldstat_tag_list_wrapper item = Fieldstat_tag_list_wrapper("dummy", flow.dst_ip.c_str());
        fieldstat_hll_add_field(instance_dest, cube_id, metric_id, dimension.get_tag(), dimension.get_tag_count(), item.get_tag(), item.get_tag_count());

        flow_cnt[dimension.to_string()].insert(item.to_string());
    }

    fieldstat_merge(instance_dest, instance_src);

    struct field_list *tag_list = NULL;
    struct field_list *tag_list_src = NULL;
    size_t n_cell = 0;
    size_t n_cell_src = 0;
    std::vector<struct Fieldstat_tag_list_wrapper *> test_result;
    fieldstat_cube_get_cells(instance_dest, 0, &tag_list, &n_cell);
    fieldstat_cube_get_cells(instance_src, 0, &tag_list_src, &n_cell_src);
    for (size_t i = 0; i < n_cell; i++) {
        test_result.push_back(new Fieldstat_tag_list_wrapper(&tag_list[i]));
    }
    std::unordered_map<std::string, int> expected_unique_cnt;
    for (auto &kv : flow_cnt) {
        expected_unique_cnt[kv.first] = kv.second.size();
    }

    double recall = test_cal_topk_accuracy(test_result, expected_unique_cnt);
    EXPECT_NEAR(recall, n_cell_src * 1.0 / n_cell, 0.0001); // the false positive is only generated because some cells in src are left because of hash collision

    fieldstat_free(instance_src);
    fieldstat_free(instance_dest);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
    fieldstat_tag_list_arr_free(tag_list_src, n_cell_src);
    for (size_t i = 0; i < test_result.size(); i++) {
        delete test_result[i];
    }
}

TEST(unit_test_merge, merge_accuracy_test_gen_dest_full_some_inserted_and_some_merged_and_some_fail_to_add_spreadsketch) {
    int K = 10;
    SpreadSketchZipfGenerator flow_generator(1.0, K * 10);
    struct fieldstat *instance_src = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance_src, &TEST_SHARED_TAG, 1);
    int metric_id = fieldstat_register_hll(instance_src, cube_id, "metric", 6);
    fieldstat_cube_set_sampling(instance_src, cube_id, SAMPLING_MODE_TOP_CARDINALITY, K, 0);
    struct fieldstat *instance_dest = fieldstat_fork(instance_src);

    std::unordered_map<std::string, std::unordered_set<std::string>> flow_cnt;
    for (int i = 0; i < 100000; i++) {
        Flow flow = flow_generator.next();
        const char *use_key = rand()%2? "src":"common";
        Fieldstat_tag_list_wrapper dimension = Fieldstat_tag_list_wrapper(use_key, flow.src_ip.c_str());
        Fieldstat_tag_list_wrapper item = Fieldstat_tag_list_wrapper("dummy", flow.dst_ip.c_str());
        fieldstat_hll_add_field(instance_src, cube_id, metric_id, dimension.get_tag(), dimension.get_tag_count(), item.get_tag(), item.get_tag_count());

        flow_cnt[dimension.to_string()].insert(item.to_string());
    }
    for (int i = 0; i < 100000; i++) {
        Flow flow = flow_generator.next();
        const char *use_key = rand()%2? "dest":"common";
        Fieldstat_tag_list_wrapper dimension = Fieldstat_tag_list_wrapper(use_key, flow.src_ip.c_str());
        Fieldstat_tag_list_wrapper item = Fieldstat_tag_list_wrapper("dummy", flow.dst_ip.c_str());
        fieldstat_hll_add_field(instance_src, cube_id, metric_id, dimension.get_tag(), dimension.get_tag_count(), item.get_tag(), item.get_tag_count());

        flow_cnt[dimension.to_string()].insert(item.to_string());
    }

    fieldstat_merge(instance_dest, instance_src);

    struct field_list *tag_list = NULL;
    size_t n_cell = 0;
    std::vector<struct Fieldstat_tag_list_wrapper *> test_result;
    fieldstat_cube_get_cells(instance_dest, 0, &tag_list, &n_cell);
    for (size_t i = 0; i < n_cell; i++) {
        test_result.push_back(new Fieldstat_tag_list_wrapper(&tag_list[i]));
    }

    std::unordered_map<std::string, int> expected_unique_cnt;
    for (auto &kv : flow_cnt) {
        expected_unique_cnt[kv.first] = kv.second.size();
    }
    double recall = test_cal_topk_accuracy(test_result, expected_unique_cnt);
    EXPECT_GE(recall, 0.7);
    printf("gen_dest_full_all_src_inserted_given_src_flows_larger_spreadsketch recall is %lf\n", recall);

    fieldstat_free(instance_src);
    fieldstat_free(instance_dest);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
    for (size_t i = 0; i < test_result.size(); i++) {
        delete test_result[i];
    }
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
    // testing::GTEST_FLAG(filter) = "*gen_dest_full_all_src_inserted_given_src_flows_larger_spreadsketch";
    
	return RUN_ALL_TESTS();
}