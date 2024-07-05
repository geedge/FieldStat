
#include <gtest/gtest.h>
#include <map>
#include <string>
#include "fieldstat.h"
#include "utils.hpp"

using namespace std;

struct fieldstat *test_init_standard_instance()
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    EXPECT_EQ(cube_id, 0);

    int metric_id = fieldstat_register_counter(instance, "czz_test counter metric");
    EXPECT_EQ(metric_id, 0);

    return instance;
}

long long my_fieldstat_counter_get(const struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag_list *tag_list = &TEST_TAG_LIST_INT)
{
    long long ret = 0;
    fieldstat_counter_get(instance, cube_id, tag_list, metric_id,  &ret);
    return ret;
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
    const char *name = fieldstat_get_metric_name(instance, 0);
    EXPECT_STREQ(name, "czz_test counter metric");

    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance, 0, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(tag_list->n_tag, 1);
    EXPECT_STREQ(tag_list->tag[0].key, TEST_TAG_INT.key);

    int *metric_id = NULL;
    size_t n_metric = 0;
    fieldstat_get_metric_in_cell(instance, 0, &tag_list[0], &metric_id, &n_metric);
    EXPECT_EQ(n_metric, 1);
    EXPECT_EQ(metric_id[0], 0);
    free(metric_id);

    fieldstat_tag_list_arr_free(tag_list, n_cell);
}

TEST(metric_test_counter, simple_register_and_query_counter)
{
    struct fieldstat *instance = test_init_standard_instance();

    fieldstat_counter_incrby(instance, 0, 0, &TEST_TAG_INT, 1, 10000);
    fieldstat_counter_incrby(instance, 0, 0, &TEST_TAG_INT, 1, 86);

    test_assert_standard_instance(instance);
    EXPECT_EQ(my_fieldstat_counter_get(instance, 0, 0), 10086);

    fieldstat_free(instance);
}

TEST(metric_test_counter, merge_counter)
{
    struct fieldstat *instance = test_init_standard_instance();
    fieldstat_counter_incrby(instance, 0, 0, &TEST_TAG_INT, 1, 100);
    fieldstat_counter_incrby(instance, 0, 0, &TEST_TAG_INT, 1, -10000);

    struct fieldstat *instance_total = fieldstat_new();
    fieldstat_merge(instance_total, instance);

    // query
    test_assert_standard_instance(instance_total);

    long long measure = my_fieldstat_counter_get(instance_total, 0, 0);
    EXPECT_EQ(measure, -9900);

    fieldstat_free(instance);
    fieldstat_free(instance_total);
}

TEST(metric_test_counter, merge_counter_twice_with_reset)
{
    struct fieldstat *instance = test_init_standard_instance();
    fieldstat_counter_incrby(instance, 0, 0, &TEST_TAG_INT, 1, 10086);
    struct fieldstat *instance_total = fieldstat_new();

    fieldstat_merge(instance_total, instance);

    fieldstat_reset(instance);
    fieldstat_counter_incrby(instance, 0, 0, &TEST_TAG_INT, 1, 4);
    fieldstat_merge(instance_total, instance);

    test_assert_standard_instance(instance_total);
    long long measure = my_fieldstat_counter_get(instance_total, 0, 0);
    EXPECT_EQ(measure, 10086 + 4);

    fieldstat_free(instance);
    fieldstat_free(instance_total);
}

TEST(metric_test_counter, topk_add_and_test_accuracy)
{
    struct fieldstat *instance = fieldstat_new();
    fieldstat_create_cube(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_TOPK, 10);
    fieldstat_register_counter(instance, "test");

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
        fieldstat_counter_incrby(instance, 0, 0, tags[i]->get_tag(), tags[i]->get_tag_count(), 1);
    }

    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance, 0, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 10);
    long long error = 0;
    for (size_t i = 0; i < n_cell; i++) {
        Fieldstat_tag_list_wrapper tmp = Fieldstat_tag_list_wrapper(&tag_list[i]);
        // tmp.print_tag_list();

        string key = tmp.get_tag()[0].key;
        string value = tmp.get_tag()[0].value_str;

        EXPECT_STREQ(key.c_str(), "elephant ");

        error += abs(my_fieldstat_counter_get(instance, 0, 0, &tag_list[i]) - flow_cnt[key + value]);
    }
    printf("topk_add_and_test_accuracy Mean ratio e: %lld\n", error);
    EXPECT_LT(error, tag_list_num * 0.005);

    fieldstat_tag_list_arr_free(tag_list, n_cell);
    fieldstat_free(instance);

    for (int i = 0; i < tag_list_num; i++)
        delete tags[i];
}

TEST(metric_test_counter, add_with_wrong_cube_id_expecting_fail)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_TOPK, 10);

    int ret = fieldstat_counter_incrby(instance, cube_id + 1, 0, &TEST_TAG_INT, 1, 1);
    EXPECT_EQ(ret, FS_ERR_INVALID_CUBE_ID);
    ret = fieldstat_counter_incrby(instance, -1, 0, &TEST_TAG_INT, 1, 1);
    EXPECT_EQ(ret, FS_ERR_INVALID_CUBE_ID);

    fieldstat_free(instance);
}

TEST(metric_test_counter, add_with_wrong_metric_id_expecting_fail)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_TOPK, 10);
    int metric_id = fieldstat_register_counter(instance, "test");

    int ret = fieldstat_counter_incrby(instance, cube_id, metric_id + 1, &TEST_TAG_INT, 1, 1);
    EXPECT_EQ(ret, FS_ERR_INVALID_METRIC_ID);
    ret = fieldstat_counter_incrby(instance, cube_id, -1, &TEST_TAG_INT, 1, 1);
    EXPECT_EQ(ret, FS_ERR_INVALID_METRIC_ID);

    fieldstat_free(instance);
}

TEST(metric_test_counter, add_and_query_on_dummy_cell_of_topk)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, 10);
    fieldstat_register_counter(instance, "primary"); // also the dummy one
    int metric_id = fieldstat_register_counter(instance, "using");

    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_INT, 1, 1);

    // add success
    long long measure = my_fieldstat_counter_get(instance, cube_id, metric_id);
    EXPECT_EQ(measure, 1);

    int *metric_ids_in_cell = NULL;
    size_t n_metric_in_cell = 0;
    fieldstat_get_metric_in_cell(instance, cube_id, &TEST_TAG_LIST_INT, &metric_ids_in_cell, &n_metric_in_cell);
    EXPECT_EQ(n_metric_in_cell, 1);
    EXPECT_EQ(metric_ids_in_cell[0], metric_id);
    free(metric_ids_in_cell);

    fieldstat_free(instance);
}

TEST(metric_test_counter, primary_counter_add_after_first)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, 10);
    int metric_id_primary = fieldstat_register_counter(instance, "primary");
    int metric_id2 = fieldstat_register_counter(instance, "using");
    fieldstat_cube_set_primary_metric(instance, cube_id, metric_id_primary);


    int ret = fieldstat_counter_incrby(instance, cube_id, metric_id2, &TEST_TAG_INT, 1, 10);
    EXPECT_EQ(ret, FS_OK);
    ret = fieldstat_counter_incrby(instance, cube_id, metric_id_primary, &TEST_TAG_INT, 1, 20);
    EXPECT_EQ(ret, FS_OK);

    EXPECT_EQ(my_fieldstat_counter_get(instance, cube_id, metric_id_primary), 20);
    EXPECT_EQ(my_fieldstat_counter_get(instance, cube_id, metric_id2), 10);

    // TOPK functions well
    struct fieldstat_tag tag = TEST_TAG_INT;
    for (int i = 0; i < 9; i++) {
        tag.value_longlong = i + 123;
        ret = fieldstat_counter_incrby(instance, cube_id, metric_id_primary, &tag, 1, 1 + i);
        EXPECT_EQ(ret, FS_OK);
    }
    // now the TOPK is full, add a new one
    tag.value_longlong = 321;
    ret = fieldstat_counter_incrby(instance, cube_id, metric_id_primary, &tag, 1, 1);
    EXPECT_EQ(ret, FS_ERR_TOO_MANY_CELLS);

    fieldstat_free(instance);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}