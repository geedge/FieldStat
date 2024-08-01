
#include <gtest/gtest.h>
#include <map>
#include <string>
#include "fieldstat.h"
#include "utils.hpp"

using namespace std;


struct fieldstat *test_init_standard_instance()
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    EXPECT_EQ(cube_id, 0);

    int metric_id = fieldstat_register_counter(instance, cube_id, "czz_test counter metric");
    EXPECT_EQ(metric_id, 0);

    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_COMPREHENSIVE, 10, 0);

    return instance;
}

long long my_fieldstat_counter_get(const struct fieldstat *instance, int cube_id, int metric_id, const struct field_list *tag_list = &TEST_FIELD_LIST_INT)
{
    long long ret = 0;
    fieldstat_counter_get(instance, cube_id, tag_list, metric_id,  &ret);
    return ret;
}

int test_fieldstat_cube_create(struct fieldstat *instance, const struct field *dimensions, size_t n_dimensions, enum sampling_mode mode, int k, int primary_metric_id=0)
{
    assert(mode == SAMPLING_MODE_COMPREHENSIVE);
    int ret = fieldstat_cube_create(instance, dimensions, n_dimensions);
    fieldstat_cube_set_sampling(instance, ret, mode, k, primary_metric_id);
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
    const char *name = fieldstat_metric_get_name(instance, ret_cell_id, 0);
    EXPECT_STREQ(name, "czz_test counter metric");

    struct field_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance, 0, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(tag_list->n_field, 1);
    EXPECT_STREQ(tag_list->field[0].key, TEST_FIELD_INT.key);

    int *metric_id = NULL;
    size_t n_metric = 0;
    fieldstat_cell_get_metrics(instance, 0, &tag_list[0], &metric_id, &n_metric);
    EXPECT_EQ(n_metric, 1);
    EXPECT_EQ(metric_id[0], 0);
    free(metric_id);

    fieldstat_field_list_arr_free(tag_list, n_cell);
}

TEST(metric_test_counter, simple_register_and_query_counter)
{
    struct fieldstat *instance = test_init_standard_instance();

    fieldstat_counter_incrby(instance, 0, 0, &TEST_FIELD_INT, 1, 10000);
    fieldstat_counter_incrby(instance, 0, 0, &TEST_FIELD_INT, 1, 86);

    test_assert_standard_instance(instance);
    EXPECT_EQ(my_fieldstat_counter_get(instance, 0, 0), 10086);

    fieldstat_free(instance);
}

TEST(metric_test_counter, merge_counter)
{
    struct fieldstat *instance = test_init_standard_instance();
    fieldstat_counter_incrby(instance, 0, 0, &TEST_FIELD_INT, 1, 100);
    fieldstat_counter_incrby(instance, 0, 0, &TEST_FIELD_INT, 1, -10000);

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
    fieldstat_counter_incrby(instance, 0, 0, &TEST_FIELD_INT, 1, 10086);
    struct fieldstat *instance_total = fieldstat_new();

    fieldstat_merge(instance_total, instance);

    fieldstat_reset(instance);
    fieldstat_counter_incrby(instance, 0, 0, &TEST_FIELD_INT, 1, 4);
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
    fieldstat_cube_create(instance, &TEST_FIELD_INT_2, 1);
    fieldstat_register_counter(instance, 0, "test");
    fieldstat_cube_set_sampling(instance, 0, SAMPLING_MODE_TOPK, 10, 0);

    int tag_list_num = 10000;
    Fieldstat_tag_list_wrapper *fields[tag_list_num];
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
        fields[i] = new Fieldstat_tag_list_wrapper(flow_k, flow_v.c_str());

        flow_cnt[flow_k + flow_v]++;
    }

    for (int i = 0; i < tag_list_num; i++) {
        fieldstat_counter_incrby(instance, 0, 0, fields[i]->get_tag(), fields[i]->get_tag_count(), 1);
    }

    struct field_list *tag_list = NULL;
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

    // test sorting
    for (size_t i = 0; i < n_cell - 1; i++) {
        EXPECT_GE(my_fieldstat_counter_get(instance, 0, 0, &tag_list[i]), my_fieldstat_counter_get(instance, 0, 0, &tag_list[i + 1]));
    }

    fieldstat_field_list_arr_free(tag_list, n_cell);
    fieldstat_free(instance);

    for (int i = 0; i < tag_list_num; i++)
        delete fields[i];
}

TEST(metric_test_counter, add_with_wrong_cube_id_expecting_fail)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, &TEST_FIELD_INT_2, 1, SAMPLING_MODE_COMPREHENSIVE, 10);

    int ret = fieldstat_counter_incrby(instance, cube_id + 1, 0, &TEST_FIELD_INT, 1, 1);
    EXPECT_EQ(ret, FS_ERR_INVALID_CUBE_ID);
    ret = fieldstat_counter_incrby(instance, -1, 0, &TEST_FIELD_INT, 1, 1);
    EXPECT_EQ(ret, FS_ERR_INVALID_CUBE_ID);

    fieldstat_free(instance);
}

TEST(metric_test_counter, add_with_wrong_metric_id_expecting_fail)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, &TEST_FIELD_INT_2, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_counter(instance, 0, "test");

    int ret = fieldstat_counter_incrby(instance, cube_id, metric_id + 1, &TEST_FIELD_INT, 1, 1);
    EXPECT_EQ(ret, FS_ERR_INVALID_METRIC_ID);
    ret = fieldstat_counter_incrby(instance, cube_id, -1, &TEST_FIELD_INT, 1, 1);
    EXPECT_EQ(ret, FS_ERR_INVALID_METRIC_ID);

    fieldstat_free(instance);
}

TEST(metric_test_counter, add_and_query_on_dummy_cell_of_topk)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    fieldstat_register_counter(instance, cube_id, "primary"); // also the dummy one
    int metric_id = fieldstat_register_counter(instance, cube_id, "using");
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOPK, 10, 0);

    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_FIELD_INT, 1, 1);

    // add success
    long long measure = my_fieldstat_counter_get(instance, cube_id, metric_id);
    EXPECT_EQ(measure, 1);

    fieldstat_free(instance);
}

TEST(metric_test_counter, primary_counter_add_after_first)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int metric_id_primary = fieldstat_register_counter(instance, cube_id, "primary");
    int metric_id2 = fieldstat_register_counter(instance, cube_id, "using");
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOPK, 10, metric_id_primary);

    int ret = fieldstat_counter_incrby(instance, cube_id, metric_id2, &TEST_FIELD_INT, 1, 10);
    EXPECT_EQ(ret, FS_OK);
    ret = fieldstat_counter_incrby(instance, cube_id, metric_id_primary, &TEST_FIELD_INT, 1, 20);
    EXPECT_EQ(ret, FS_OK);

    EXPECT_EQ(my_fieldstat_counter_get(instance, cube_id, metric_id_primary), 20);
    EXPECT_EQ(my_fieldstat_counter_get(instance, cube_id, metric_id2), 10);

    // TOPK functions well
    struct field field = TEST_FIELD_INT;
    for (int i = 0; i < 9; i++) {
        field.value_longlong = i + 123;
        ret = fieldstat_counter_incrby(instance, cube_id, metric_id_primary, &field, 1, 1 + i);
        EXPECT_EQ(ret, FS_OK);
    }
    // now the TOPK is full, add a new one
    field.value_longlong = 321;
    ret = fieldstat_counter_incrby(instance, cube_id, metric_id_primary, &field, 1, 1);
    EXPECT_EQ(ret, FS_ERR_TOO_MANY_CELLS);

    fieldstat_free(instance);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}