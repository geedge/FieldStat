
#include <gtest/gtest.h>
#include "fieldstat.h"
#include "utils.hpp"

TEST(unit_test_merge, test_metric_name_mapping_with_new_metric_on_existing_cube)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id1 = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    (void)fieldstat_register_counter(instance, cube_id1, "metric_name cube1 cube2", false);
    (void)fieldstat_register_counter(instance, cube_id1, "shared name", false);
    int cube_id2 = fieldstat_register_cube(instance, &TEST_TAG_INT, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    (void)fieldstat_register_counter(instance, cube_id2, "metric_name cube1 cube2", false);

    struct fieldstat *instance_dest = fieldstat_new();
    int cube_id_dest = fieldstat_register_cube(instance_dest, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    (void)fieldstat_register_counter(instance_dest, cube_id_dest, "shared name", false);

    fieldstat_merge(instance_dest, instance);

    int *cube_id;
    size_t n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id, &n_cube);
    EXPECT_TRUE(n_cube == 2);
    EXPECT_TRUE(cube_id[0] == cube_id_dest);

    EXPECT_EQ(fieldstat_get_max_metric_id(instance_dest, cube_id[0]) + 1, 2);
    EXPECT_EQ(fieldstat_get_max_metric_id(instance_dest, cube_id[1]) + 1, 1);

    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest, cube_id_dest, 0), "shared name");
    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest, cube_id_dest, 1), "metric_name cube1 cube2");
    EXPECT_STREQ(fieldstat_get_metric_name(instance_dest, cube_id[1], 0), "metric_name cube1 cube2");

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
    free(cube_id);
}

TEST(unit_test_merge, cube_shared_tag_mapping)
{
    struct fieldstat *instance = fieldstat_new();
    (void)fieldstat_register_cube(instance, &TEST_TAG_DOUBLE, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int cube_id2 = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_counter(instance, cube_id2, "metric in cube 2", false);
    struct fieldstat *instance_dest = fieldstat_new();
    int cube_id_dest = fieldstat_register_cube(instance_dest, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);

    fieldstat_merge(instance_dest, instance);

    int *cube_id;
    size_t n_cube;
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
    size_t n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id, &n_cube);
    EXPECT_TRUE(n_cube == 0);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
}

TEST(unit_test_merge, new_cube_and_metric_to_empty_comprehensive)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_TAG_INT, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_counter(instance, cube_id, "metric_name", false);

    struct fieldstat *instance_dest = fieldstat_new();

    fieldstat_merge(instance_dest, instance);

    int *cube_id_dest;
    size_t n_cube;
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
    int metric_id = fieldstat_register_counter(instance, cube_id, "metric_name", false);
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);

    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_STRING, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 10086);

    fieldstat_merge(instance_dest, instance);

    int *cube_id_dest;
    size_t n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id_dest, &n_cube);
    EXPECT_TRUE(n_cube == 1);
    int ret_cube_id = cube_id_dest[0];
    free(cube_id_dest);
    int ret_metric_id = fieldstat_get_max_metric_id(instance, ret_cube_id);
    EXPECT_EQ(ret_metric_id, 0);
    long long measure = fieldstat_counter_get(instance, cube_id, metric_id, cell_id);
    printf("fieldstat_counter_get done, measure: %lld\n", measure);
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
    int metric_id = fieldstat_register_counter(instance, cube_id, "metric_name", false);
    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_STRING, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 5);
    struct fieldstat *instance_dest = fieldstat_new();

    fieldstat_merge(instance_dest, instance);
    printf("first merge fin in test, the measure: %lld", fieldstat_counter_get(instance_dest, 0, 0, 0));

    fieldstat_merge(instance_dest, instance);

    int *cube_id_dest;
    size_t n_cube;
    fieldstat_get_cubes(instance_dest, &cube_id_dest, &n_cube);
    EXPECT_TRUE(n_cube == 1);
    int ret_cube_id = cube_id_dest[0];
    free(cube_id_dest);
    int ret_metric_id = fieldstat_get_max_metric_id(instance_dest, ret_cube_id);
    EXPECT_EQ(ret_metric_id, 0);
    long long measure = fieldstat_counter_get(instance_dest, cube_id, metric_id, 0);
    EXPECT_EQ(measure, 10);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
}

TEST(unit_test_merge, new_too_many_cells_on_one_metric_comprehensive)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 2);
    int metric_id = fieldstat_register_counter(instance, cube_id, "metric name", false);
    int cell_id1 = fieldstat_cube_add(instance, cube_id, &TEST_TAG_STRING, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id1, 1);
    struct fieldstat *instance_dest = fieldstat_new();
    fieldstat_merge(instance_dest, instance);
    fieldstat_reset(instance);

    int cell_id2 = fieldstat_cube_add(instance, cube_id, &TEST_TAG_INT, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id2, 2);
    int cell_id3 = fieldstat_cube_add(instance, cube_id, &TEST_TAG_DOUBLE, 1, 1);
    printf("add oper before 3nd incrby\n");
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id3, 3);

    printf("add oper done\n");
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

//todo: TEST(unit_test_merge, new_too_many_cells_on_multiple_metric_comprehensive) 

TEST(unit_test_merge, merge_on_full_metric_comprehensive)
{

}

TEST(unit_test_merge, empty_metric_topk)
{

}

TEST(unit_test_merge, new_cube_and_metric_topk)
{

}

TEST(unit_test_merge, new_metric_on_existing_cube_topk)
{

}

TEST(unit_test_merge, new_cell_on_existing_cube_and_metric_topk)
{

}

TEST(unit_test_merge, merge_existing_cell_on_existing_cube_and_metric_topk)
{

}

TEST(unit_test_merge, new_too_many_cells_topk)
{

}

TEST(unit_test_merge, merge_on_full_metric_topk)
{

}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}