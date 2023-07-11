
#include <gtest/gtest.h>
#include "fieldstat.h"
#include "utils.hpp"

TEST(metric_test_counter, simple_register_and_query)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    EXPECT_EQ(cube_id, 0);

    int metric_id = fieldstat_register_counter(instance, cube_id, "czz_test counter metric", false);
    EXPECT_EQ(metric_id, 0);

    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_STRING, 1, 1);
    EXPECT_NE(cell_id, -1);

    fieldstat_counter_incrby(instance, metric_id, cube_id, cell_id, 10086);

    // query
    int ret_cell_id = fieldstat_get_max_cube_id(instance);
    EXPECT_EQ(ret_cell_id, 0);
    int ret_metric_id = fieldstat_get_max_metric_id(instance, ret_cell_id);
    EXPECT_EQ(ret_metric_id, 0);
    long long measure = fieldstat_counter_get(instance, cube_id, metric_id, cell_id);
    printf("fieldstat_counter_get done, measure: %lld\n", measure);
    EXPECT_EQ(measure, 10086);
    const char *name = fieldstat_get_metric_name(instance, cube_id, metric_id);
    EXPECT_STREQ(name, "czz_test counter metric");

    printf("going to get cells\n");

    int *ret_cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells(instance, cube_id, metric_id, &ret_cell_ids, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(ret_cell_ids[0], cell_id);
    EXPECT_EQ(tag_list->n_tag, 1);
    EXPECT_STREQ(tag_list->tag[0].key, TEST_TAG_STRING.key);

    fieldstat_free(instance);
    free(ret_cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
}

// TODO: cell 超过

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}