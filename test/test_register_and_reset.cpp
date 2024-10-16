
#include <gtest/gtest.h>
#include "fieldstat.h"
#include "utils.hpp"

int test_fieldstat_cube_create(struct fieldstat *instance, const struct field *dimensions, size_t n_dimensions, enum sampling_mode mode, int k)
{
    assert(mode == SAMPLING_MODE_COMPREHENSIVE);
    int ret = fieldstat_cube_create(instance, dimensions, n_dimensions);
    fieldstat_cube_set_sampling(instance, ret, mode, k, 0);
    return ret;
}

TEST(test_register, delete_cube_and_register_and_origin_position)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_cube_destroy(instance, cube_id);
    cube_id = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    EXPECT_EQ(cube_id, 0);
    EXPECT_EQ(fieldstat_find_cube(instance, &TEST_SHARED_TAG, 1) , 0);
    fieldstat_free(instance);
}

TEST(test_register, delete_comprehensive_cube_with_cells_and_metrics)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter");
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_FIELD_INT, 1, 1);

    fieldstat_cube_destroy(instance, cube_id);

    struct field_list *tag_list = fieldstat_cube_get_dimension(instance, cube_id);
    EXPECT_EQ(tag_list, nullptr);
    int cube_id_ret = fieldstat_find_cube(instance, &TEST_SHARED_TAG, 1);
    EXPECT_EQ(cube_id_ret, FS_ERR_INVALID_DIMENSION);

    fieldstat_free(instance);
}

TEST(test_register, delete_topk_cube_with_cells_and_metrics)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter");
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOPK, 10, metric_id);
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_FIELD_INT, 1, 1);

    fieldstat_cube_destroy(instance, cube_id);
    struct field_list *tag_list = fieldstat_cube_get_dimension(instance, cube_id);
    EXPECT_EQ(tag_list, nullptr);
    int cube_id_ret = fieldstat_find_cube(instance, &TEST_SHARED_TAG, 1);
    EXPECT_EQ(cube_id_ret, FS_ERR_INVALID_DIMENSION);

    fieldstat_free(instance);
}

TEST(test_register, delete_spreadsketch_cube_with_cells_and_metrics)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int metric_id1 = fieldstat_register_counter(instance, cube_id, "counter");
    int metric_primary = fieldstat_register_hll(instance, cube_id, "hll_primary", 5);
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOP_CARDINALITY, 10, metric_primary);

    fieldstat_counter_incrby(instance, cube_id, metric_id1, &TEST_FIELD_INT, 1, 1);

    const struct field *fields[] = {&TEST_FIELD_DOUBLE};
    fieldstat_hll_add_fields(instance, cube_id, metric_primary, &TEST_FIELD_INT, 1, fields, 1);

    fieldstat_cube_destroy(instance, cube_id);
    struct field_list *tag_list = fieldstat_cube_get_dimension(instance, cube_id);
    EXPECT_EQ(tag_list, nullptr);
    int cube_id_ret = fieldstat_find_cube(instance, &TEST_SHARED_TAG, 1);
    EXPECT_EQ(cube_id_ret, FS_ERR_INVALID_DIMENSION);

    fieldstat_free(instance);
}

TEST(test_register, reset_and_try_to_query_cell_comprehensive)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter");
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_FIELD_INT, 1, 1);

    fieldstat_reset(instance);
    long long value;
    EXPECT_EQ(fieldstat_counter_get(instance, cube_id, &TEST_FIELD_LIST_INT, metric_id, &value), FS_ERR_INVALID_DIMENSION);

    size_t n_cell;
    struct field_list *tag_list;
    fieldstat_cube_get_cells(instance, cube_id, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 0);

    fieldstat_free(instance);
}

TEST(test_register, reset_and_try_to_query_cell_topk)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter");
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOPK, 10, metric_id);

    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_FIELD_INT, 1, 1);

    fieldstat_reset(instance);
    long long value;
    EXPECT_EQ(fieldstat_counter_get(instance, cube_id, &TEST_FIELD_LIST_INT, metric_id, &value), FS_ERR_INVALID_DIMENSION);

    size_t n_cell;
    struct field_list *tag_list;
    fieldstat_cube_get_cells(instance, cube_id, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 0);

    fieldstat_free(instance);
}

TEST(test_register, reset_and_try_to_query_cell_spreadsketch)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int metric_id = fieldstat_register_hll(instance, cube_id, "hll", 5);
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOP_CARDINALITY, 10, metric_id);
    fieldstat_hll_add(instance, cube_id, metric_id, &TEST_FIELD_INT, 1, "12abc", 5);

    fieldstat_reset(instance);
    double value;
    EXPECT_EQ(fieldstat_hll_get(instance, cube_id, &TEST_FIELD_LIST_INT, metric_id, &value), FS_ERR_INVALID_DIMENSION);

    size_t n_cell;
    struct field_list *tag_list;
    fieldstat_cube_get_cells(instance, cube_id, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 0);

    fieldstat_free(instance);
}

TEST(test_register, reset_and_try_to_query_metric_comprehensive)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_counter = fieldstat_register_counter(instance, cube_id, "counter");
    int metric_hll = fieldstat_register_hll(instance, cube_id, "hll", 5);
    int metric_hist = fieldstat_register_histogram(instance, cube_id, "hist", 0, 10000, 5);
    int metric_operated_after_reset = fieldstat_register_counter(instance, cube_id, "counter_after_reset");

    fieldstat_counter_incrby(instance, cube_id, metric_counter, &TEST_FIELD_INT, 1, 1);
    fieldstat_hll_add(instance, cube_id, metric_hll, &TEST_FIELD_INT, 1, "1", 1);
    fieldstat_histogram_record(instance, cube_id, metric_hist, &TEST_FIELD_INT, 1, 1000);

    fieldstat_reset(instance);
    fieldstat_counter_incrby(instance, cube_id, metric_operated_after_reset, &TEST_FIELD_INT, 1, 1);

    long long value;
    double value_hll;
    EXPECT_EQ(fieldstat_counter_get(instance, cube_id, &TEST_FIELD_LIST_INT, metric_counter, &value), FS_ERR_INVALID_METRIC_ID);
    EXPECT_EQ(fieldstat_hll_get(instance, cube_id, &TEST_FIELD_LIST_INT, metric_hll, &value_hll), FS_ERR_INVALID_METRIC_ID);
    EXPECT_EQ(fieldstat_histogram_value_at_percentile(instance, cube_id, &TEST_FIELD_LIST_INT, metric_hist, 0.5), FS_ERR_INVALID_METRIC_ID);
    EXPECT_EQ(fieldstat_counter_get(instance, cube_id, &TEST_FIELD_LIST_INT, metric_operated_after_reset, &value), FS_OK);

    fieldstat_free(instance);
}

TEST(test_register, reset_and_try_to_query_primary_metric_topk)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int metric_counter_primary = fieldstat_register_counter(instance, cube_id, "counter");
    int metric_hll = fieldstat_register_hll(instance, cube_id, "hll", 5);
    int metric_operated_after_reset = fieldstat_register_counter(instance, cube_id, "counter_after_reset");
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOPK, 1, metric_counter_primary);

    fieldstat_counter_incrby(instance, cube_id, metric_counter_primary, &TEST_FIELD_INT, 1, 1);
    fieldstat_hll_add(instance, cube_id, metric_hll, &TEST_FIELD_INT, 1, "1", 1);

    fieldstat_reset(instance);
    fieldstat_counter_incrby(instance, cube_id, metric_operated_after_reset, &TEST_FIELD_INT, 1, 1);

    long long value;
    double value_hll;
    char *value_hll_base64;
    size_t value_hll_base64_len;
    EXPECT_EQ(fieldstat_counter_get(instance, cube_id, &TEST_FIELD_LIST_INT, metric_counter_primary, &value), FS_ERR_INVALID_METRIC_ID);
    EXPECT_EQ(fieldstat_hll_get(instance, cube_id, &TEST_FIELD_LIST_INT, metric_hll, &value_hll), FS_ERR_INVALID_METRIC_ID);
    fieldstat_metric_get_serialization_as_base64(instance, cube_id,metric_hll, &TEST_FIELD_LIST_INT,&value_hll_base64, &value_hll_base64_len);
    EXPECT_EQ(value_hll_base64, nullptr);
    EXPECT_EQ(fieldstat_counter_get(instance, cube_id, &TEST_FIELD_LIST_INT, metric_operated_after_reset, &value), FS_OK);

    fieldstat_free(instance);
}

TEST(test_register, reset_and_try_to_query_primary_metric_spreadsketch)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int metric_counter = fieldstat_register_counter(instance, cube_id, "counter");
    int metric_hll_primary = fieldstat_register_hll(instance, cube_id, "hll", 5);
    int metric_operated_after_reset = fieldstat_register_counter(instance, cube_id, "counter_after_reset");
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOP_CARDINALITY, 1, metric_hll_primary);

    fieldstat_counter_incrby(instance, cube_id, metric_counter, &TEST_FIELD_INT, 1, 1);
    fieldstat_hll_add(instance, cube_id, metric_hll_primary, &TEST_FIELD_INT, 1, "1", 1);

    fieldstat_reset(instance);
    fieldstat_counter_incrby(instance, cube_id, metric_operated_after_reset, &TEST_FIELD_INT, 1, 1);

    long long value;
    double value_hll;
    char *value_hll_base64;
    size_t value_hll_base64_len;
    EXPECT_EQ(fieldstat_counter_get(instance, cube_id, &TEST_FIELD_LIST_INT, metric_counter, &value), FS_ERR_INVALID_METRIC_ID);
    EXPECT_EQ(fieldstat_hll_get(instance, cube_id, &TEST_FIELD_LIST_INT, metric_hll_primary, &value_hll), FS_ERR_INVALID_METRIC_ID);
    EXPECT_EQ(fieldstat_counter_get(instance, cube_id, &TEST_FIELD_LIST_INT, metric_operated_after_reset, &value), FS_OK);
    fieldstat_metric_get_serialization_as_base64(instance, cube_id,metric_hll_primary, &TEST_FIELD_LIST_INT,&value_hll_base64, &value_hll_base64_len);
    EXPECT_EQ(value_hll_base64, nullptr);

    fieldstat_free(instance);
}

TEST(test_register, reset_and_new_cell_comprehensive)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 2);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter");
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_FIELD_INT, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_FIELD_DOUBLE, 1, 1);
    int ret = fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_FIELD_STRING, 1, 1);
    EXPECT_EQ(ret, FS_ERR_TOO_MANY_CELLS);

    fieldstat_reset(instance);
    ret = fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_FIELD_STRING, 1, 1);
    EXPECT_EQ(ret, FS_OK);

    fieldstat_free(instance);
}

TEST(test_register, reset_and_new_cell_topk)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter");
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOPK, 1, metric_id);
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_FIELD_INT, 1, 100);//100: bigger value
    int ret = fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_FIELD_DOUBLE, 1, 1);
    EXPECT_EQ(ret, FS_ERR_TOO_MANY_CELLS);

    fieldstat_reset(instance);
    ret = fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_FIELD_DOUBLE, 1, 1);
    EXPECT_EQ(ret, FS_OK);

    fieldstat_free(instance);
}

TEST(test_register, reset_and_new_cell_spreadsketch)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int metric_id = fieldstat_register_hll(instance, cube_id, "hll", 5);
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOP_CARDINALITY, 1, metric_id);
    // spread sketch will store more data than expected cell number 1. So loop for many cells first to trigger the error
    struct field test_tag_long = TEST_FIELD_INT;
    for (int i = 0; i < 10000; i++) {
        test_tag_long.value_longlong = i;
        fieldstat_hll_add(instance, cube_id, metric_id, &test_tag_long, 1, "12abc", 5);
    }
    int ret = fieldstat_hll_add(instance, cube_id, metric_id, &TEST_FIELD_DOUBLE, 1, "12abc", 5);
    EXPECT_EQ(ret, FS_ERR_TOO_MANY_CELLS);

    fieldstat_reset(instance);
    ret = fieldstat_hll_add(instance, cube_id, metric_id, &TEST_FIELD_DOUBLE, 1, "12abc", 5);
    EXPECT_EQ(ret, FS_OK);

    fieldstat_free(instance);
}

TEST(test_register, ensure_recovery_more_faster_comprehensive) {
    struct fieldstat *instance = fieldstat_new();
    int cell_num = 1000;
    int cube_id = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, cell_num);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter");
    struct field test_tag_long = TEST_FIELD_INT;

    clock_t start = clock();
    for (int i = 0; i < cell_num; i++) {
        test_tag_long.value_longlong = i;
        fieldstat_counter_incrby(instance, cube_id, metric_id, &test_tag_long, 1, 1);
    }
    clock_t end = clock();
    clock_t duration_initialize = end - start;

    fieldstat_reset(instance);

    start = clock();
    for (int i = 0; i < cell_num; i++) {
        test_tag_long.value_longlong = i;
        fieldstat_counter_incrby(instance, cube_id, metric_id, &test_tag_long, 1, 1);
    }
    end = clock();
    clock_t duration_reset = end - start;

    EXPECT_LT(duration_reset, duration_initialize);

    fieldstat_free(instance);
}

TEST(test_register, ensure_recovery_more_faster_topk) {
    struct fieldstat *instance = fieldstat_new();
    int cell_num = 1000;
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter");
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOPK, cell_num, metric_id);

    struct field test_tag_long = TEST_FIELD_INT;

    clock_t start = clock();
    for (int i = 0; i < cell_num; i++) {
        test_tag_long.value_longlong = i;
        fieldstat_counter_incrby(instance, cube_id, metric_id, &test_tag_long, 1, 1);
    }
    clock_t end = clock();
    clock_t duration_initialize = end - start;

    fieldstat_reset(instance);

    start = clock();
    for (int i = 0; i < cell_num; i++) {
        test_tag_long.value_longlong = i;
        fieldstat_counter_incrby(instance, cube_id, metric_id, &test_tag_long, 1, 1);
    }
    end = clock();
    clock_t duration_reset = end - start;

    EXPECT_LT(duration_reset, duration_initialize);

    fieldstat_free(instance);
}

TEST(test_register, ensure_recovery_more_faster_spreadsketch) {
    struct fieldstat *instance = fieldstat_new();
    int cell_num = 1000;
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int metric_id = fieldstat_register_hll(instance, cube_id, "counter", 6);
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOP_CARDINALITY, cell_num, metric_id);
    char long_string[1000];
    memset(long_string, 'a', 1000);
    long_string[999] = '\0';
    struct field test_tag_long = {long_string, FIELD_VALUE_INTEGER, {.value_longlong = 0}};

    clock_t start = clock();
    for (int i = 0; i < cell_num; i++) {
        test_tag_long.value_longlong = i;
        fieldstat_hll_add(instance, cube_id, metric_id, &test_tag_long, 1, "1", 1);
    }
    clock_t end = clock();
    clock_t duration_initialize = end - start;

    fieldstat_reset(instance);

    start = clock();
    for (int i = 0; i < cell_num; i++) {
        test_tag_long.value_longlong = i;
        fieldstat_hll_add(instance, cube_id, metric_id, &test_tag_long, 1, "1", 1);
    }
    end = clock();
    clock_t duration_reset = end - start;

    EXPECT_LT(duration_reset, duration_initialize);

    fieldstat_free(instance);
}

TEST(test_register, register_many_cubes)
{
    struct fieldstat *instance = fieldstat_new();
    int registered_cube = 10000; // will trigger realloc many times
    struct field shared_tag = TEST_SHARED_TAG;
    for (int i = 0; i < registered_cube; i++) {
        shared_tag.value_longlong = i;
        int cube_id = test_fieldstat_cube_create(instance, &shared_tag, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
        EXPECT_EQ(cube_id, i);
        fieldstat_register_counter(instance, cube_id, "counter");
    }
    // try to use the cube
    for (int i = 0; i < registered_cube; i++) {
        fieldstat_counter_incrby(instance, i, 0, &TEST_FIELD_INT, 1, i);
    }

    for (int i = 0; i < registered_cube; i++) {
        long long result;
        fieldstat_counter_get(instance, i, &TEST_FIELD_LIST_INT, 0,  &result);
        EXPECT_EQ(result, i);
    }

    fieldstat_free(instance);
}

TEST(test_register, add_many_tagged_cells)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    size_t n_field = 1000;
    struct field test_tag_long[n_field];
    for (size_t i = 0; i < n_field; i++) {
        test_tag_long[i] = TEST_FIELD_INT; // will trigger realloc 
    }

    int metric_id = fieldstat_register_counter(instance, cube_id, "counter");
    fieldstat_counter_incrby(instance, cube_id, metric_id, test_tag_long, n_field, 10086);

    long long result;
    struct field_list tag_list = {test_tag_long, n_field};
    fieldstat_counter_get(instance, cube_id, &tag_list, metric_id,  &result);
    EXPECT_EQ(result, 10086);

    fieldstat_free(instance);
}

TEST(test_register, add_long_tagged_cells)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    struct field test_tag_long = TEST_FIELD_STRING;
    char *long_string = (char *)malloc(5001);
    memset(long_string, 'a', 5001);
    long_string[5000] = '\0';
    test_tag_long.value_str = long_string;

    int metric_id = fieldstat_register_counter(instance, cube_id, "counter");
    fieldstat_counter_incrby(instance, cube_id, metric_id, &test_tag_long, 1, 10086);

    long long result;
    struct field_list tag_list = {&test_tag_long, 1};
    fieldstat_counter_get(instance, cube_id, &tag_list, metric_id, &result);
    EXPECT_EQ(result, 10086);

    fieldstat_free(instance);
    free(long_string);
}

TEST(test_register, register_many_metrics)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = 0;
    for (int i = 0; i < 200; i++) {
        metric_id = fieldstat_register_counter(instance, cube_id, (std::string("counter ") + std::to_string(i)).c_str());
        EXPECT_EQ(metric_id, i);
    }

    for (int i = 0; i < 200; i++) {
        EXPECT_EQ(fieldstat_counter_incrby(instance, cube_id, i, &TEST_FIELD_INT, 1, 1), 0);
    }

    fieldstat_free(instance);
}

TEST(test_register, fork_registered_info_from_empty_one)
{
    struct fieldstat *instance = fieldstat_new();
    
    struct fieldstat *dup = fieldstat_fork(instance);

    int *cube_ids;
    int cube_num;
    fieldstat_get_cubes(dup, &cube_ids, &cube_num);
    EXPECT_EQ(cube_num, 0);
    free(cube_ids);
    fieldstat_free(dup);
    fieldstat_free(instance);
}

TEST(test_register, fork_registered_info_with_cube_and_metric)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter");
    int metric_id2 = fieldstat_register_counter(instance, cube_id, "counter2");
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_FIELD_INT, 1, 1);
    int cube_id_del = test_fieldstat_cube_create(instance, &TEST_FIELD_STRING, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    test_fieldstat_cube_create(instance, &TEST_FIELD_DOUBLE, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_cube_destroy(instance, cube_id_del);

    struct fieldstat *dup = fieldstat_fork(instance);

    int *cube_ids;
    int cube_num;
    fieldstat_get_cubes(dup, &cube_ids, &cube_num);
    EXPECT_EQ(cube_num, 2);
    struct field_list *tag_list = NULL;
    
    tag_list = fieldstat_cube_get_dimension(dup, cube_ids[0]);
    EXPECT_STREQ(tag_list->field[0].key, TEST_SHARED_TAG.key);
    fieldstat_field_list_arr_free(tag_list, 1);
    size_t n_cell = 0;
    fieldstat_cube_get_cells(dup, cube_ids[0], &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 0);

    tag_list = fieldstat_cube_get_dimension(dup, cube_ids[1]);
    EXPECT_STREQ(tag_list->field[0].key, TEST_FIELD_DOUBLE.key);
    
    fieldstat_field_list_arr_free(tag_list, 1);

    EXPECT_STREQ(fieldstat_metric_get_name(dup, cube_ids[0], metric_id), "counter");
    EXPECT_STREQ(fieldstat_metric_get_name(dup, cube_ids[0], metric_id2), "counter2");
    EXPECT_EQ(fieldstat_metric_get_name(dup, cube_ids[1], metric_id), nullptr);

    free(cube_ids);
    fieldstat_free(dup);
    fieldstat_free(instance);
}

TEST(test_register, unregister_cube_on_wrong_instance)
{
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat *instance_dst = fieldstat_new();

    int cube_id = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int cube_id2 = test_fieldstat_cube_create(instance, &TEST_FIELD_DOUBLE, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter");
    int metric_id2 = fieldstat_register_counter(instance, cube_id2, "counter2");
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_FIELD_INT, 1, 1);
    fieldstat_counter_incrby(instance, cube_id2, metric_id2, &TEST_FIELD_INT, 1, 1);

    fieldstat_merge(instance_dst, instance);

    fieldstat_cube_destroy(instance_dst, 0);

    int *cube_ids;
    int cube_num;
    fieldstat_get_cubes(instance_dst, &cube_ids, &cube_num);
    EXPECT_EQ(cube_num, 1);
    EXPECT_EQ(cube_ids[0], cube_id2);

    fieldstat_merge(instance_dst, instance);

    long long val_deleted_once;
    fieldstat_counter_get(instance_dst, cube_id, &TEST_FIELD_LIST_INT,metric_id,  &val_deleted_once);
    EXPECT_EQ(val_deleted_once, 1);
    long long val_merged_twice = -1;
    fieldstat_counter_get(instance_dst, cube_id2, &TEST_FIELD_LIST_INT, metric_id2,  &val_merged_twice);
    EXPECT_EQ(val_merged_twice, 2);

    fieldstat_free(instance);
    fieldstat_free(instance_dst);
    free(cube_ids);
}

TEST(test_register, register_many_cells_on_unlimited_sized_cube)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 0);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter");
    struct field test_tag = {"abc", FIELD_VALUE_INTEGER, {.value_longlong = 0}};
    for (int i = 0; i < 10000; i++) {
        test_tag.value_longlong = i;
        fieldstat_counter_incrby(instance, cube_id, metric_id, &test_tag, 1, 1);
    }

    for (int i = 0; i < 10000; i++) {
        test_tag.value_longlong = i;
        struct field_list tag_list = {&test_tag, 1};
        long long value;
        fieldstat_counter_get(instance, cube_id, &tag_list, metric_id, &value);
        EXPECT_EQ(value, 1);
    }

    fieldstat_free(instance);
}

TEST(test_register, register_cube_twice) {
    struct fieldstat *instance = fieldstat_new();
    test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int cube_id2 = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    EXPECT_EQ(cube_id2, FS_ERR_DIMENSION_ALREADY_EXISTS);
    fieldstat_free(instance);
}

TEST(test_register, find_cube) {
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);

    int find_cube_id = fieldstat_find_cube(instance, &TEST_SHARED_TAG, 1);
    EXPECT_EQ(cube_id, find_cube_id);
    int find_cube_id2 = fieldstat_find_cube(instance, &TEST_FIELD_DOUBLE, 1);
    EXPECT_EQ(find_cube_id2, FS_ERR_INVALID_DIMENSION);
    
    fieldstat_free(instance);
}

TEST(test_register, set_sampling_error) {
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int ret = fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOPK, 10, 0);
    EXPECT_EQ(ret, FS_ERR_INVALID_METRIC_ID);
    int metric_id = fieldstat_register_hll(instance, cube_id, "hll", 5);
    ret = fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOPK, 10, metric_id);
    EXPECT_EQ(ret, FS_ERR_INVALID_METRIC_TYPE);
    
    fieldstat_free(instance);
}

TEST(test_register, register_metric_twice) {
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);

    fieldstat_register_counter(instance, cube_id, "counter");
    int metric_id2 = fieldstat_register_counter(instance, cube_id, "counter");
    EXPECT_EQ(metric_id2, FS_ERR_METRIC_NAME_ALREADY_EXISTS);
    fieldstat_free(instance);
}

TEST(test_register, add_on_uninitialized_cube) {
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter");
    // fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_FIELD_INT, 1, 1);
    EXPECT_EQ(fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_FIELD_INT, 1, 1), FS_ERR_CUBE_SAMPLING_NOT_INITIALIZED);
    
    fieldstat_free(instance);
}

TEST(calibrate, target_one_more_metric)
{
    struct fieldstat *master = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(master, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_counter(master, cube_id, "counter");
    struct fieldstat *target = fieldstat_fork(master);
    EXPECT_EQ(fieldstat_register_counter(target, cube_id, "counter2"), 1);

    fieldstat_calibrate(master, target);

    EXPECT_STREQ(fieldstat_metric_get_name(target, cube_id, 0), "counter");
    EXPECT_EQ(fieldstat_cube_get_metric_id_by_name(target, cube_id, "counter"), 0);
    EXPECT_EQ(fieldstat_cube_get_metric_id_by_name(target, cube_id, "counter2"), FS_ERR_INVALID_METRIC_NAME);
    struct field_list *tag_list = fieldstat_cube_get_dimension(target, cube_id);
    EXPECT_STREQ(tag_list->field[0].key, TEST_SHARED_TAG.key);

    fieldstat_free(master);
    fieldstat_free(target);
    fieldstat_field_list_arr_free(tag_list, 1);
}

TEST(calibrate, master_one_more_metric)
{
    struct fieldstat *master = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(master, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_counter(master, cube_id, "counter");
    struct fieldstat *target = fieldstat_fork(master);
    EXPECT_EQ(fieldstat_register_counter(master, cube_id, "counter2"), 1);

    fieldstat_calibrate(master, target);

    EXPECT_STREQ(fieldstat_metric_get_name(target, cube_id,0), "counter");
    EXPECT_STREQ(fieldstat_metric_get_name(target, cube_id,1), "counter2");
    EXPECT_EQ(fieldstat_cube_get_metric_id_by_name(target, cube_id, "counter"), 0);
    EXPECT_EQ(fieldstat_cube_get_metric_id_by_name(target, cube_id, "counter2"), 1);
    struct field_list *tag_list = fieldstat_cube_get_dimension(target, cube_id);
    EXPECT_STREQ(tag_list->field[0].key, TEST_SHARED_TAG.key);

    fieldstat_free(master);
    fieldstat_free(target);
    fieldstat_field_list_arr_free(tag_list, 1);
}

// different metric on same cube
TEST(calibrate, different_metric)
{
    struct fieldstat *master = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(master, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_counter(master, cube_id, "counter");
    struct fieldstat *target = fieldstat_fork(master);
    EXPECT_EQ(fieldstat_register_counter(target, cube_id, "counter2"), 1);
    EXPECT_EQ(fieldstat_register_counter(master, cube_id, "hi i am master new"), 1);

    fieldstat_calibrate(master, target);

    EXPECT_STREQ(fieldstat_metric_get_name(target, cube_id, 0), "counter");
    EXPECT_STREQ(fieldstat_metric_get_name(target, cube_id, 1), "hi i am master new");
    EXPECT_EQ(fieldstat_cube_get_metric_id_by_name(target, cube_id, "counter2"), FS_ERR_INVALID_METRIC_NAME);
    EXPECT_EQ(fieldstat_cube_get_metric_id_by_name(target, cube_id, "counter"), 0);
    EXPECT_EQ(fieldstat_cube_get_metric_id_by_name(target, cube_id, "hi i am master new"), 1);

    struct field_list *tag_list = fieldstat_cube_get_dimension(target, cube_id);
    EXPECT_STREQ(tag_list->field[0].key, TEST_SHARED_TAG.key);

    fieldstat_free(master);
    fieldstat_free(target);
    fieldstat_field_list_arr_free(tag_list, 1);
}

TEST(calibrate, target_more_cube)
{
    struct fieldstat *master = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(master, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_counter(master, cube_id, "counter");
    struct fieldstat *target = fieldstat_fork(master);
    int cube_id2 = test_fieldstat_cube_create(target, &TEST_FIELD_STRING, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    EXPECT_EQ(cube_id2, 1);
    fieldstat_register_counter(target, cube_id, "counter2");

    fieldstat_calibrate(master, target);

    int n_cube;
    int *cube_ids;
    fieldstat_get_cubes(target, &cube_ids, &n_cube);
    EXPECT_EQ(n_cube, 1);
    EXPECT_EQ(cube_ids[0], cube_id);
    free(cube_ids);

    EXPECT_STREQ(fieldstat_metric_get_name(target, cube_id, 0), "counter");
    struct field_list *tag_list = fieldstat_cube_get_dimension(target, cube_id);
    EXPECT_STREQ(tag_list->field[0].key, TEST_SHARED_TAG.key);

    EXPECT_EQ(fieldstat_find_cube(target, &TEST_SHARED_TAG, 1), 0);
    EXPECT_EQ(fieldstat_find_cube(target, &TEST_FIELD_STRING, 1), FS_ERR_INVALID_DIMENSION);

    fieldstat_free(master);
    fieldstat_free(target);
    fieldstat_field_list_arr_free(tag_list, 1);
}

TEST(calibrate, master_more_cube)
{
    struct fieldstat *master = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(master, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    struct fieldstat *target = fieldstat_fork(master);
    int cube_id2 = test_fieldstat_cube_create(master, &TEST_FIELD_STRING, 1, SAMPLING_MODE_COMPREHENSIVE, 10);

    fieldstat_calibrate(master, target);

    int n_cube;
    int *cube_ids;
    fieldstat_get_cubes(target, &cube_ids, &n_cube);
    EXPECT_EQ(n_cube, 2);
    EXPECT_EQ(cube_ids[0], cube_id);
    EXPECT_EQ(cube_ids[1], cube_id2);
    free(cube_ids);

    struct field_list *tag_list = fieldstat_cube_get_dimension(target, cube_id);
    EXPECT_STREQ(tag_list->field[0].key, TEST_SHARED_TAG.key);
    struct field_list *tag_list2 = fieldstat_cube_get_dimension(target, cube_id2);
    EXPECT_STREQ(tag_list2->field[0].key, TEST_FIELD_STRING.key);

    EXPECT_EQ(fieldstat_find_cube(target, &TEST_FIELD_STRING, 1), 1);
    EXPECT_EQ(fieldstat_find_cube(target, &TEST_SHARED_TAG, 1), 0);

    fieldstat_free(master);
    fieldstat_free(target);
    fieldstat_field_list_arr_free(tag_list, 1);
    fieldstat_field_list_arr_free(tag_list2, 1);
}

TEST(calibrate, master_change_cube)
{
    struct fieldstat *master = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(master, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    struct fieldstat *target = fieldstat_fork(master);
    fieldstat_cube_destroy(master, cube_id);
    test_fieldstat_cube_create(master, &TEST_FIELD_STRING, 1, SAMPLING_MODE_COMPREHENSIVE, 10);

    fieldstat_calibrate(master, target);

    struct field_list *tag_list = fieldstat_cube_get_dimension(target, cube_id);
    EXPECT_STREQ(tag_list->field[0].key, TEST_FIELD_STRING.key);

    EXPECT_EQ(fieldstat_find_cube(target, &TEST_FIELD_STRING, 1), 0);

    fieldstat_free(master);
    fieldstat_free(target);
    fieldstat_field_list_arr_free(tag_list, 1);
}

TEST(calibrate, master_many_cube)
{
    struct fieldstat *master = fieldstat_new();
    int registered_cube = 3000; // will trigger realloc 1 times
    struct field shared_tag = TEST_SHARED_TAG;
    for (int i = 0; i < registered_cube; i++) {
        shared_tag.value_longlong = i;
        int cube_id = test_fieldstat_cube_create(master, &shared_tag, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
        EXPECT_EQ(cube_id, i);
    }
    struct fieldstat *target = fieldstat_fork(master);
    int new_registered = 5000;
    for (int i = 0; i < new_registered; i++) {
        shared_tag.value_longlong = i + registered_cube;
        int cube_id = test_fieldstat_cube_create(master, &shared_tag, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
        EXPECT_EQ(cube_id, i + registered_cube);
    }

    fieldstat_calibrate(master, target);

    int n_cube;
    int *cube_ids;
    fieldstat_get_cubes(target, &cube_ids, &n_cube);
    EXPECT_EQ(n_cube, registered_cube + new_registered);
    free(cube_ids);

    for (int i = 0; i < registered_cube + new_registered; i++) {
        shared_tag.value_longlong = i;
        EXPECT_EQ(fieldstat_find_cube(target, &shared_tag, 1), i);
    }

    fieldstat_free(master);
    fieldstat_free(target);
}

// issue： https://jira.geedge.net/browse/TSG-20140
// 流程：
// calibrate调用前：
    // master: B           （B)
    // replica:A B          (A B)

// calibrate调用时，依次比较每个cube id，id:0时，将master[0] 同步,同步时，会先删除A,然后添加B'
    // replica:B' B        (B' B)

// calibrate 继续，同步master[1]，replica[1]不存在，创建新的cube。此时，不仅从id->cube 的数组中删除掉B，且从cube field->id 的字典中删除掉B。可能会错误的删除掉B'，导致两个map的对应不一致。
    // replica:B' /        (B )

// 例如：find_cube(B)-> 返回1。
// incrby(cubeid=1)-> 返回FS_ERR_INVALID_CUBE_ID
TEST(calibrate, issue_calibrate_wrong)
{
    struct fieldstat *master = fieldstat_new();
    const struct field *tag_A = &TEST_SHARED_TAG;
    const struct field *tag_B = &TEST_FIELD_INT;
    int cube_idA = test_fieldstat_cube_create(master, tag_A, 1, SAMPLING_MODE_COMPREHENSIVE, 1);
    int cube_idB = test_fieldstat_cube_create(master, tag_B, 1, SAMPLING_MODE_COMPREHENSIVE, 1);
    struct fieldstat *target = fieldstat_fork(master);

    EXPECT_EQ(fieldstat_cube_destroy(master, cube_idA), FS_OK);
    EXPECT_EQ(fieldstat_cube_destroy(master, cube_idB), FS_OK);
    int cube_idBa = test_fieldstat_cube_create(master, tag_B, 1, SAMPLING_MODE_COMPREHENSIVE, 1);
    EXPECT_EQ(cube_idBa, 0);

    fieldstat_calibrate(master, target);

    int *cubes_id_ret = NULL;
    int n_cubes = 0;
    fieldstat_get_cubes(target, &cubes_id_ret, &n_cubes);
    EXPECT_EQ(n_cubes, 1);
    EXPECT_EQ(fieldstat_find_cube(target, tag_A, 1), FS_ERR_INVALID_DIMENSION);
    EXPECT_EQ(fieldstat_find_cube(target, tag_B, 1), cubes_id_ret[0]);

    struct field_list *tag_list = fieldstat_cube_get_dimension(target, cubes_id_ret[0]);
    EXPECT_STREQ(tag_list->field[0].key, tag_B->key);
    fieldstat_field_list_arr_free(tag_list, 1);

    fieldstat_free(master);
    fieldstat_free(target);
    free(cubes_id_ret);
}

TEST(calibrate, delete_first_cube)
{
    struct fieldstat *master = fieldstat_new();
    const struct field *tag_A = &TEST_SHARED_TAG;
    const struct field *tag_B = &TEST_FIELD_INT;
    int cube_idA = test_fieldstat_cube_create(master, tag_A, 1, SAMPLING_MODE_COMPREHENSIVE, 1);
    int cube_idB = test_fieldstat_cube_create(master, tag_B, 1, SAMPLING_MODE_COMPREHENSIVE, 1);
    struct fieldstat *target = fieldstat_fork(master);

    fieldstat_cube_destroy(master, cube_idA);

    fieldstat_calibrate(master, target);

    int *cubes_id_ret = NULL;
    int n_cubes = 0;
    fieldstat_get_cubes(target, &cubes_id_ret, &n_cubes);
    EXPECT_EQ(n_cubes, 1);
    EXPECT_EQ(cubes_id_ret[0], cube_idB);
    free(cubes_id_ret);

    EXPECT_EQ(fieldstat_find_cube(target, tag_A, 1), FS_ERR_INVALID_DIMENSION);
    EXPECT_EQ(fieldstat_find_cube(target, tag_B, 1), 1);

    struct field_list *tag_list = fieldstat_cube_get_dimension(target, 1);
    EXPECT_STREQ(tag_list->field[0].key, tag_B->key);
    fieldstat_field_list_arr_free(tag_list, 1);

    fieldstat_free(master);
    fieldstat_free(target);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
    // testing::GTEST_FLAG(filter) = "test_register.reset_and_try_to_query_primary*";
	return RUN_ALL_TESTS();
}