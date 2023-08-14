
#include <gtest/gtest.h>
#include "fieldstat.h"
#include "utils.hpp"


TEST(test_register, reset_and_version)
{
    struct fieldstat *instance = fieldstat_new();
    EXPECT_EQ(fieldstat_get_cell_version(instance), 0);
    fieldstat_reset(instance);
    EXPECT_EQ(fieldstat_get_cell_version(instance), 1);
    fieldstat_reset(instance);
    fieldstat_reset(instance);
    EXPECT_EQ(fieldstat_get_cell_version(instance), 3);

    fieldstat_free(instance);
}

TEST(test_register, delete_cube_and_version_increase)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    EXPECT_EQ(fieldstat_get_cube_version(instance, cube_id), 0);
    int ret = fieldstat_unregister_cube(instance, cube_id);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(fieldstat_get_cube_version(instance, cube_id), 1);
    ret = fieldstat_unregister_cube(instance, cube_id);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(fieldstat_get_cube_version(instance, cube_id), 1);
    EXPECT_EQ(fieldstat_get_cube_version(instance, 123), 0);

    fieldstat_free(instance);
}

TEST(test_register, delete_cube_and_register_and_origin_position)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_unregister_cube(instance, cube_id);
    cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    EXPECT_EQ(cube_id, 0);
    fieldstat_free(instance);
}

TEST(test_register, delete_comprehensive_cube_with_cells_and_metrics)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_INT, 1, 1);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter", COUNTER_MERGE_BY_SUM);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 1);

    fieldstat_unregister_cube(instance, cube_id);
    fieldstat_free(instance);
}

TEST(test_register, delete_topk_cube_with_cells_and_metrics)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, 10);
    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_INT, 1, 1);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter", COUNTER_MERGE_BY_SUM);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 1);

    fieldstat_unregister_cube(instance, cube_id);
    fieldstat_free(instance);
}

TEST(test_register, reset_and_try_to_query_cell)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_INT, 1, 1);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter", COUNTER_MERGE_BY_SUM);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 1);

    fieldstat_reset(instance);
    EXPECT_EQ(fieldstat_get_max_metric_id(instance, cube_id), 0);
    EXPECT_EQ(fieldstat_counter_get(instance, cube_id, metric_id, cell_id), -1);

    fieldstat_free(instance);
}

TEST(test_register, register_many_cubes)
{
    struct fieldstat *instance = fieldstat_new();
    int registered_cube = 10000; // will trigger realloc many times
    for (int i = 0; i < registered_cube; i++) {
        int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
        EXPECT_EQ(cube_id, i);
    }
    // try to use the cube
    for (int i = 0; i < registered_cube; i++) {
        int cell_id = fieldstat_cube_add(instance, i, &TEST_TAG_INT, 1, 1);
        int metric_id = fieldstat_register_counter(instance, i, "counter", COUNTER_MERGE_BY_SUM);
        fieldstat_counter_incrby(instance, i, metric_id, cell_id, i);
    }

    for (int i = 0; i < registered_cube; i++) {
        long long result = fieldstat_counter_get(instance, i, 0, 0);
        EXPECT_EQ(result, i);
    }

    fieldstat_free(instance);
}

TEST(test_register, add_long_tagged_cells)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    struct fieldstat_tag test_tag_long[100];
    for (int i = 0; i < 100; i++) {
        test_tag_long[i] = TEST_TAG_INT;
    }
    int cell_id = fieldstat_cube_add(instance, cube_id, test_tag_long, 100, 1);
    EXPECT_EQ(cell_id, 0);

    int metric_id = fieldstat_register_counter(instance, cube_id, "counter", COUNTER_MERGE_BY_SUM);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 10086);

    EXPECT_EQ(fieldstat_counter_get(instance, cube_id, metric_id, cell_id), 10086);

    fieldstat_free(instance);
}

TEST(test_register, register_too_many_metrics)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = 0;
    for (int i = 0; i < 50; i++) {
        metric_id = fieldstat_register_counter(instance, cube_id, (std::string("counter ") + std::to_string(i)).c_str(), COUNTER_MERGE_BY_SUM);
        EXPECT_EQ(metric_id, i);
    }

    for (int i = 0; i < 50; i++) {
        int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_INT, 1, 1);
        EXPECT_EQ(fieldstat_counter_incrby(instance, cube_id, i, cell_id, 1), 0);
    }

    fieldstat_free(instance);
}

TEST(test_register, dup_registered_info_from_empty_one)
{
    struct fieldstat *instance = fieldstat_new();
    
    struct fieldstat *dup = fieldstat_dup(instance);

    int *cube_ids;
    int cube_num;
    fieldstat_get_cubes(dup, &cube_ids, &cube_num);
    EXPECT_EQ(cube_num, 0);
    free(cube_ids);
    fieldstat_free(dup);
    fieldstat_free(instance);
}

TEST(test_register, dup_registered_info_with_cube_and_metric_without_cell)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter", COUNTER_MERGE_BY_SUM);
    int metric_id2 = fieldstat_register_counter(instance, cube_id, "counter2", COUNTER_MERGE_BY_SUM);
    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_INT, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 1);
    int cube_id_del = fieldstat_register_cube(instance, &TEST_TAG_DOUBLE_collided, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int cube_id2 = fieldstat_register_cube(instance, &TEST_TAG_DOUBLE, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_unregister_cube(instance, cube_id_del);

    struct fieldstat *dup = fieldstat_dup(instance);

    int *cube_ids;
    int cube_num;
    fieldstat_get_cubes(dup, &cube_ids, &cube_num);
    EXPECT_EQ(cube_num, 2);
    EXPECT_EQ(cube_ids[0], cube_id);
    EXPECT_EQ(cube_ids[1], cube_id2);
    free(cube_ids);

    EXPECT_STREQ(fieldstat_get_metric_name(dup, cube_id, metric_id), "counter");
    EXPECT_STREQ(fieldstat_get_metric_name(dup, cube_id, metric_id2), "counter2");

    int *cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells(dup, cube_id, metric_id, &cell_ids, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 0);

    fieldstat_free(dup);
    fieldstat_free(instance);
}

TEST(test_register, unregister_cube_on_wrong_instance)
{
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat *instance_dst = fieldstat_new();

    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int cube_id2 = fieldstat_register_cube(instance, &TEST_TAG_DOUBLE, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter", COUNTER_MERGE_BY_SUM);
    int metric_id2 = fieldstat_register_counter(instance, cube_id2, "counter2", COUNTER_MERGE_BY_SUM);
    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_INT, 1, 1);
    int cell_id2 = fieldstat_cube_add(instance, cube_id2, &TEST_TAG_INT, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 1);
    fieldstat_counter_incrby(instance, cube_id2, metric_id2, cell_id2, 1);

    fieldstat_merge(instance_dst, instance);

    fieldstat_unregister_cube(instance_dst, 0);

    int *cube_ids;
    int cube_num;
    fieldstat_get_cubes(instance_dst, &cube_ids, &cube_num);
    EXPECT_EQ(cube_num, 1);
    EXPECT_EQ(cube_ids[0], cube_id2);

    fieldstat_merge(instance_dst, instance);

    long long val_deleted_once = fieldstat_counter_get(instance_dst, cube_id, metric_id, cell_id);
    EXPECT_EQ(val_deleted_once, 1);
    long long val_merged_twice = fieldstat_counter_get(instance_dst, cube_id2, metric_id2, cell_id2);
    EXPECT_EQ(val_merged_twice, 2);

    fieldstat_free(instance);
    fieldstat_free(instance_dst);
    free(cube_ids);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}