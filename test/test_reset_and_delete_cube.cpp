
#include <gtest/gtest.h>
#include "fieldstat.h"
#include "utils.hpp"


TEST(test_reset_and_delete_cube, reset_and_version)
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

TEST(test_reset_and_delete_cube, delete_cube_and_version_increase)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    EXPECT_EQ(fieldstat_get_cube_version(instance, cube_id), 0);
    fieldstat_unregister_cube(instance, cube_id);
    EXPECT_EQ(fieldstat_get_cube_version(instance, cube_id), 1);
    fieldstat_unregister_cube(instance, cube_id);
    EXPECT_EQ(fieldstat_get_cube_version(instance, cube_id), 2);
    EXPECT_EQ(fieldstat_get_cube_version(instance, 123), 0);

    fieldstat_free(instance);
}

TEST(test_reset_and_delete_cube, delete_cube_and_register_and_origin_position)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_unregister_cube(instance, cube_id);
    cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    EXPECT_EQ(cube_id, 0);
    fieldstat_free(instance);
}

TEST(test_reset_and_delete_cube, delete_comprehensive_cube_with_cells_and_metrics)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_INT, 1, 1);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter", false);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 1);

    fieldstat_unregister_cube(instance, cube_id);
    fieldstat_free(instance);
}

TEST(test_reset_and_delete_cube, delete_topk_cube_with_cells_and_metrics)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, 10);
    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_INT, 1, 1);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter", false);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 1);

    fieldstat_unregister_cube(instance, cube_id);
    fieldstat_free(instance);
}

TEST(test_reset_and_delete_cube, reset_and_try_to_query_cell)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_INT, 1, 1);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter", false);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 1);

    fieldstat_reset(instance);
    EXPECT_EQ(fieldstat_get_max_metric_id(instance, cube_id), 0);
    EXPECT_EQ(fieldstat_counter_get(instance, cube_id, metric_id, cell_id), -1);

    fieldstat_free(instance);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}