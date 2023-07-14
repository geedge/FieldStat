
#include <gtest/gtest.h>
#include "fieldstat.h"
#include "utils.hpp"

struct fieldstat *test_init_standard_instance_one_cube_one_metric_one_cell_hdr()
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    EXPECT_EQ(cube_id, 0);

    int metric_id = fieldstat_register_histogram(instance, cube_id, "czz_test hdr metric", 1, 600000, 3);
    EXPECT_EQ(metric_id, 0);

    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_STRING, 1, 1);
    EXPECT_EQ(cell_id, 0);

    return instance;
}

void test_assert_standard_instance(struct fieldstat *instance)
{
    int *ret_cube_id_arr = NULL;
    size_t n_cube = 0;
    fieldstat_get_cubes(instance, &ret_cube_id_arr, &n_cube);
    EXPECT_EQ(n_cube, 1);
    int ret_cell_id = ret_cube_id_arr[0];
    free(ret_cube_id_arr);
    EXPECT_EQ(ret_cell_id, 0);
    int ret_metric_id = fieldstat_get_max_metric_id(instance, ret_cell_id);
    EXPECT_EQ(ret_metric_id, 0);
    const char *name = fieldstat_get_metric_name(instance, 0, 0);
    EXPECT_STREQ(name, "czz_test hdr metric");

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

TEST(metric_test_histogram, simple_register_and_query)
{
    struct fieldstat *instance = test_init_standard_instance_one_cube_one_metric_one_cell_hdr();

    fieldstat_histogram_record(instance, 0, 0, 0, 1234);
    fieldstat_histogram_record(instance, 0, 0, 0, 1234);
    fieldstat_histogram_record(instance, 0, 0, 0, 123);

    test_assert_standard_instance(instance);
    EXPECT_EQ(fieldstat_histogram_value_at_percentile(instance, 0, 0, 0, 50.0), 1234);
    EXPECT_EQ(fieldstat_histogram_count_le_value(instance, 0, 0, 0, 1000), 1);

    fieldstat_free(instance);
}

TEST(metric_test_histogram, serialize_and_merge)
{
    struct fieldstat *instance = test_init_standard_instance_one_cube_one_metric_one_cell_hdr();
    fieldstat_histogram_record(instance, 0, 0, 0, 1234);
    fieldstat_histogram_record(instance, 0, 0, 0, 1234);
    fieldstat_histogram_record(instance, 0, 0, 0, 123);

    char *blob = NULL;
    size_t blob_len = 0;
    fieldstat_serialize(instance, &blob, &blob_len);
    struct fieldstat *instance_from_blob = fieldstat_deserialize(blob, blob_len);
    free(blob);
    struct fieldstat *instance_total = fieldstat_new();
    fieldstat_merge(instance_total, instance_from_blob);

    // query
    test_assert_standard_instance(instance_total);

    EXPECT_EQ(fieldstat_histogram_value_at_percentile(instance_total, 0, 0, 0, 50.0), 1234);
    EXPECT_EQ(fieldstat_histogram_count_le_value(instance, 0, 0, 0, 1000), 1);

    fieldstat_free(instance);
    fieldstat_free(instance_from_blob);
    fieldstat_free(instance_total);
}

TEST(metric_test_histogram, serialization_and_merge_twice_with_reset)
{
    struct fieldstat *instance = test_init_standard_instance_one_cube_one_metric_one_cell_hdr();
    fieldstat_histogram_record(instance, 0, 0, 0, 1234);
    fieldstat_histogram_record(instance, 0, 0, 0, 123);

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
    fieldstat_histogram_record(instance, 0, 0, 0, 1234);

    fieldstat_serialize(instance, &blob, &blob_len);
    instance_from_blob = fieldstat_deserialize(blob, blob_len);
    free(blob);
    fieldstat_merge(instance_total, instance_from_blob);

    test_assert_standard_instance(instance_total);
    EXPECT_EQ(fieldstat_histogram_value_at_percentile(instance_total, 0, 0, 0, 50.0), 1234);
    EXPECT_EQ(fieldstat_histogram_count_le_value(instance_total, 0, 0, 0, 1000), 1);

    fieldstat_free(instance);
    fieldstat_free(instance_from_blob);
    fieldstat_free(instance_total);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}