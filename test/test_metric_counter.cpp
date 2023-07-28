
#include <gtest/gtest.h>
#include <map>
#include <string>
#include "fieldstat.h"
#include "utils.hpp"

using namespace std;

struct fieldstat *test_init_standard_instance_one_cube_one_metric_one_cell_counter(bool is_gauge = false)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    EXPECT_EQ(cube_id, 0);

    int metric_id = fieldstat_register_counter(instance, cube_id, "czz_test counter metric", is_gauge);
    EXPECT_EQ(metric_id, 0);

    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_STRING, 1, 1);
    EXPECT_EQ(cell_id, 0);

    return instance;
}

void test_assert_standard_instance(const struct fieldstat *instance)
{
    int *ret_cube_id_arr = NULL;
    int n_cube = 0;
    fieldstat_get_cubes(instance, &ret_cube_id_arr, &n_cube);
    EXPECT_EQ(n_cube, 1);
    int ret_cell_id = ret_cube_id_arr[0];
    free(ret_cube_id_arr);
    EXPECT_EQ(ret_cell_id, 0);
    int ret_metric_id = fieldstat_get_max_metric_id(instance, ret_cell_id);
    EXPECT_EQ(ret_metric_id, 0);
    const char *name = fieldstat_get_metric_name(instance, 0, 0);
    EXPECT_STREQ(name, "czz_test counter metric");

    int *ret_cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells(instance, 0, 0, &ret_cell_ids, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(ret_cell_ids[0], 0);
    EXPECT_EQ(tag_list->n_tag, 1);
    EXPECT_STREQ(tag_list->tag[0].key, TEST_TAG_STRING.key);

    free(ret_cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
}

TEST(metric_test_counter, simple_register_and_query_counter)
{
    struct fieldstat *instance = test_init_standard_instance_one_cube_one_metric_one_cell_counter();

    fieldstat_counter_incrby(instance, 0, 0, 0, 10000);
    fieldstat_counter_incrby(instance, 0, 0, 0, 86);

    test_assert_standard_instance(instance);
    EXPECT_EQ(fieldstat_counter_get(instance, 0, 0, 0), 10086);

    fieldstat_free(instance);
}

TEST(metric_test_counter, serialize_and_merge_counter)
{
    struct fieldstat *instance = test_init_standard_instance_one_cube_one_metric_one_cell_counter();
    fieldstat_counter_incrby(instance, 0, 0, 0, 100);
    fieldstat_counter_incrby(instance, 0, 0, 0, -10000);

    char *blob = NULL;
    size_t blob_len = 0;
    fieldstat_serialize(instance, &blob, &blob_len);
    struct fieldstat *instance_from_blob = fieldstat_deserialize(blob, blob_len);
    free(blob);
    struct fieldstat *instance_total = fieldstat_new();
    fieldstat_merge(instance_total, instance_from_blob);

    // query
    test_assert_standard_instance(instance_total);

    long long measure = fieldstat_counter_get(instance_total, 0, 0, 0);
    EXPECT_EQ(measure, -9900);

    fieldstat_free(instance);
    fieldstat_free(instance_from_blob);
    fieldstat_free(instance_total);
}

TEST(metric_test_counter, serialization_and_merge_counter_twice_with_reset)
{
    struct fieldstat *instance = test_init_standard_instance_one_cube_one_metric_one_cell_counter();
    fieldstat_counter_incrby(instance, 0, 0, 0, 10086);

    char *blob = NULL;
    size_t blob_len = 0;
    fieldstat_serialize(instance, &blob, &blob_len);
    struct fieldstat *instance_from_blob = fieldstat_deserialize(blob, blob_len);
    free(blob);
    struct fieldstat *instance_total = fieldstat_new();
    fieldstat_merge(instance_total, instance_from_blob);

    fieldstat_free(instance_from_blob);
    fieldstat_reset(instance);
    int cell_id = fieldstat_cube_add(instance, 0, &TEST_TAG_STRING, 1, 1);
    EXPECT_EQ(cell_id, 0);
    fieldstat_counter_incrby(instance, 0, 0, 0, 4);

    fieldstat_serialize(instance, &blob, &blob_len);
    instance_from_blob = fieldstat_deserialize(blob, blob_len);
    free(blob);
    fieldstat_merge(instance_total, instance_from_blob);

    test_assert_standard_instance(instance_total);
    long long measure = fieldstat_counter_get(instance_total, 0, 0, 0);
    EXPECT_EQ(measure, 10086 + 4);

    fieldstat_free(instance);
    fieldstat_free(instance_from_blob);
    fieldstat_free(instance_total);
}

TEST(metric_test_counter, simple_register_and_query_gauge)
{
    struct fieldstat *instance = test_init_standard_instance_one_cube_one_metric_one_cell_counter(true);

    fieldstat_counter_set(instance, 0, 0, 0, 10000);
    fieldstat_counter_set(instance, 0, 0, 0, 86);

    test_assert_standard_instance(instance);
    EXPECT_EQ(fieldstat_counter_get(instance, 0, 0, 0), 86);

    fieldstat_free(instance);
}

TEST(metric_test_counter, serialize_and_merge_gauge)
{
    struct fieldstat *instance = test_init_standard_instance_one_cube_one_metric_one_cell_counter(true);
    fieldstat_counter_incrby(instance, 0, 0, 0, 10086);
    fieldstat_counter_set(instance, 0, 0, 0, 10);

    char *blob = NULL;
    size_t blob_len = 0;
    fieldstat_serialize(instance, &blob, &blob_len);
    struct fieldstat *instance_from_blob = fieldstat_deserialize(blob, blob_len);
    free(blob);
    struct fieldstat *instance_total = fieldstat_new();
    fieldstat_merge(instance_total, instance_from_blob);

    test_assert_standard_instance(instance_total);

    long long measure = fieldstat_counter_get(instance_total, 0, 0, 0);
    EXPECT_EQ(measure, 10);

    fieldstat_free(instance);
    fieldstat_free(instance_from_blob);
    fieldstat_free(instance_total);
}

TEST(metric_test_counter, serialization_and_merge_gauge_twice_with_reset)
{
    struct fieldstat *instance = test_init_standard_instance_one_cube_one_metric_one_cell_counter(true);
    fieldstat_counter_incrby(instance, 0, 0, 0, 1234);

    char *blob = NULL;
    size_t blob_len = 0;
    fieldstat_serialize(instance, &blob, &blob_len);
    struct fieldstat *instance_from_blob = fieldstat_deserialize(blob, blob_len);
    free(blob);
    struct fieldstat *instance_total = fieldstat_new();
    fieldstat_merge(instance_total, instance_from_blob);

    fieldstat_free(instance_from_blob);
    fieldstat_reset(instance);
    int cell_id = fieldstat_cube_add(instance, 0, &TEST_TAG_STRING, 1, 1);
    EXPECT_EQ(cell_id, 0);
    fieldstat_counter_incrby(instance, 0, 0, 0, 1000);

    fieldstat_serialize(instance, &blob, &blob_len);
    instance_from_blob = fieldstat_deserialize(blob, blob_len);
    free(blob);
    fieldstat_merge(instance_total, instance_from_blob);

    test_assert_standard_instance(instance_total);
    long long measure = fieldstat_counter_get(instance_total, 0, 0, 0);
    EXPECT_EQ(measure, 1234);

    fieldstat_free(instance);
    fieldstat_free(instance_from_blob);
    fieldstat_free(instance_total);
}

TEST(metric_test_counter, topk_add_and_test_accuracy)
{
    struct fieldstat *instance = fieldstat_new();
    fieldstat_register_cube(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_TOPK, 10);
    fieldstat_register_counter(instance, 0, "test", 0);

    int tag_list_num = 10000;
    Fieldstat_tag_list_wrapper *tags[tag_list_num];
    map<string, int> flow_cnt;
    for (int i = 0; i < tag_list_num; i++)
    {   
        const char *flow_k;
        string flow_v;
        if (rand() % 2) {
            flow_k = "elephant ";
            flow_v = std::to_string(rand() % 10);
        } else {
            flow_k = "mouse";
            flow_v = std::to_string(rand() % 1000);
        }
        tags[i] = new Fieldstat_tag_list_wrapper(flow_k, flow_v.c_str());

        flow_cnt[flow_k + flow_v]++;
    }

    for (int i = 0; i < tag_list_num; i++) {
        int cell_id = fieldstat_cube_add(instance, 0, tags[i]->get_tag(), tags[i]->get_tag_count(), 1);
        if (cell_id != -1)
            fieldstat_counter_incrby(instance, 0, 0, cell_id, 1);
    }

    int *ret_cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells(instance, 0, 0, &ret_cell_ids, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 10);
    long long error = 0;
    for (size_t i = 0; i < n_cell; i++) {
        Fieldstat_tag_list_wrapper tmp = Fieldstat_tag_list_wrapper(&tag_list[i]);
        // tmp.print_tag_list();

        string key = tmp.get_tag()[0].key;
        string value = tmp.get_tag()[0].value_str;

        EXPECT_STREQ(key.c_str(), "elephant ");

        error += abs(fieldstat_counter_get(instance, 0, 0, ret_cell_ids[i]) - flow_cnt[key + value]);
    }
    printf("error: %lld\n", error);
    EXPECT_LT(error, tag_list_num * 0.005);

    free(ret_cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
    fieldstat_free(instance);

    for (int i = 0; i < tag_list_num; i++)
        delete tags[i];
}

// todo: add on wrong cube/metric/cell

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}