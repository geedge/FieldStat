#include <gtest/gtest.h>

#include "utils.hpp"
#include "cjson/cJSON.h"

#include "fieldstat.h"
#include "fieldstat_exporter.h"

void assert_cell_null(const struct fieldstat *instance, int cube_id, long long expected_count)
{
    struct field_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance, cube_id, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(tag_list[0].n_field, 0);
    EXPECT_TRUE(tag_list[0].field == NULL);
    long long value;
    fieldstat_counter_get(instance, cube_id, &tag_list[0], 0, &value);
    EXPECT_EQ(value, expected_count);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
}


TEST(test_empty_tag, add_many_times)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, NULL, 0, SAMPLING_MODE_COMPREHENSIVE, 1);

    int metric_id = fieldstat_register_counter(instance, "metric");
    fieldstat_counter_incrby(instance, cube_id, metric_id, NULL, 0, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, NULL, 0, 1);

    assert_cell_null(instance, cube_id, 2);

    fieldstat_free(instance);
}

struct fieldstat *test_empty_my_init(enum sampling_mode mode = SAMPLING_MODE_COMPREHENSIVE)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, NULL, 0, mode, 1);
    int metric_id = fieldstat_register_counter(instance, "metric");
    fieldstat_counter_incrby(instance, cube_id, metric_id, NULL, 0, 1);

    return instance;
}

TEST(test_empty_tag, merge)
{
    struct fieldstat *instance_src = test_empty_my_init();
    struct fieldstat *instance_dst = fieldstat_new();

    fieldstat_merge(instance_dst, instance_src);
    fieldstat_merge(instance_dst, instance_src);

    int *ret_cube_id_arr = NULL;
    int n_cube = 0;
    fieldstat_get_cubes(instance_dst, &ret_cube_id_arr, &n_cube);
    int ret_cell_id = ret_cube_id_arr[0];
    struct field_list *shared_tag = fieldstat_cube_get_tags(instance_dst, ret_cell_id);
    EXPECT_EQ(shared_tag->n_field, 0);
    EXPECT_TRUE(shared_tag->field == NULL);
    fieldstat_tag_list_arr_free(shared_tag, 1);
    free(ret_cube_id_arr);

    assert_cell_null(instance_dst, ret_cell_id, 2);

    fieldstat_free(instance_dst);
    fieldstat_free(instance_src);
}

TEST(test_empty_tag, merge_topk)
{
    struct fieldstat *instance_src = test_empty_my_init(SAMPLING_MODE_TOPK);
    struct fieldstat *instance_dst = fieldstat_new();

    fieldstat_merge(instance_dst, instance_src);
    fieldstat_merge(instance_dst, instance_src);

    int *ret_cube_id_arr = NULL;
    int n_cube = 0;
    fieldstat_get_cubes(instance_dst, &ret_cube_id_arr, &n_cube);
    int ret_cell_id = ret_cube_id_arr[0];
    struct field_list *shared_tag = fieldstat_cube_get_tags(instance_dst, ret_cell_id);
    EXPECT_EQ(shared_tag->n_field, 0);
    EXPECT_TRUE(shared_tag->field == NULL);
    fieldstat_tag_list_arr_free(shared_tag, 1);
    free(ret_cube_id_arr);

    assert_cell_null(instance_dst, ret_cell_id, 2);


    fieldstat_free(instance_dst);
    fieldstat_free(instance_src);
}

TEST(test_empty_tag, export)
{
    struct fieldstat *instance = test_empty_my_init();
    struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new();
    char *json_string = fieldstat_json_exporter_export(fieldstat_json_exporter, instance, &TEST_TIMEVAL);
    // std::cout << "json_string: \n" << json_string << std::endl;

    cJSON *root_arr = cJSON_Parse(json_string);
    free(json_string);
    fieldstat_json_exporter_free(fieldstat_json_exporter);
    fieldstat_free(instance);

    cJSON *root = cJSON_GetArrayItem(root_arr, 0);
    // check field
    cJSON *field = cJSON_GetObjectItem(root, "tags");
    EXPECT_EQ(cJSON_GetArraySize(field), 0);

    cJSON *metrics = cJSON_GetObjectItem(root, "tags");
    EXPECT_NE(metrics, nullptr);
    cJSON *counter = cJSON_GetObjectItem(metrics, "metric");
    EXPECT_EQ(counter->valueint, 1);

    cJSON_Delete(root_arr);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}