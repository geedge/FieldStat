#include <gtest/gtest.h>

#include "cjson/cJSON.h"

#include "fieldstat.h"
#include "fieldstat_exporter.h"

TEST(test_empty_tag, serialize)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, NULL, 0, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_cube_add(instance, cube_id, NULL, 0, 1);

    char *blob;
    size_t blob_size;
    fieldstat_serialize(instance, &blob, &blob_size);
    struct fieldstat *instance2 = fieldstat_deserialize(blob, blob_size);
    free(blob);

    int *ret_cube_id_arr = NULL;
    int n_cube = 0;
    fieldstat_get_cubes(instance2, &ret_cube_id_arr, &n_cube);
    int ret_cell_id = ret_cube_id_arr[0];
    free(ret_cube_id_arr);
    struct fieldstat_tag_list *shared_tag = fieldstat_get_shared_tags(instance2, ret_cell_id);
    EXPECT_EQ(shared_tag->n_tag, 0);
    fieldstat_tag_list_arr_free(shared_tag, 1);

    int *ret_cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_read_cell(instance2, ret_cell_id, &ret_cell_ids, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(ret_cell_ids[0], 0);
    EXPECT_EQ(tag_list[0].n_tag, 0);
    EXPECT_TRUE(tag_list[0].tag == NULL);
    free(ret_cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);


    fieldstat_free(instance);
    fieldstat_free(instance2);
}

TEST(test_empty_tag, add_many_times) // test hash find ok
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, NULL, 0, SAMPLING_MODE_COMPREHENSIVE, 1);
    int cell_id = fieldstat_cube_add(instance, cube_id, NULL, 0, 1);
    EXPECT_EQ(cell_id, 0);
    cell_id = fieldstat_cube_add(instance, cube_id, NULL, 0, 1);
    EXPECT_EQ(cell_id, 0);

    int metric_id = fieldstat_register_counter(instance, cube_id, "metric", COUNTER_MERGE_BY_MAX);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 1);

    EXPECT_EQ(fieldstat_counter_get(instance, cube_id, metric_id, cell_id), 2);

    fieldstat_free(instance);
}

struct fieldstat *test_empty_my_init(enum sampling_mode mode = SAMPLING_MODE_COMPREHENSIVE)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, NULL, 0, mode, 1);
    int cell_id = fieldstat_cube_add(instance, cube_id, NULL, 0, 1);
    int metric_id = fieldstat_register_counter(instance, cube_id, "metric", COUNTER_MERGE_BY_SUM);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 1);

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
    struct fieldstat_tag_list *shared_tag = fieldstat_get_shared_tags(instance_dst, ret_cell_id);
    EXPECT_EQ(shared_tag->n_tag, 0);
    EXPECT_TRUE(shared_tag->tag == NULL);
    fieldstat_tag_list_arr_free(shared_tag, 1);
    free(ret_cube_id_arr);

    int *ret_cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_read_cell(instance_dst, ret_cell_id, &ret_cell_ids, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(ret_cell_ids[0], 0);
    EXPECT_EQ(tag_list[0].n_tag, 0);
    EXPECT_TRUE(tag_list[0].tag == NULL);
    free(ret_cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);

    EXPECT_EQ(fieldstat_counter_get(instance_dst, 0, 0, 0), 2);

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
    struct fieldstat_tag_list *shared_tag = fieldstat_get_shared_tags(instance_dst, ret_cell_id);
    EXPECT_EQ(shared_tag->n_tag, 0);
    EXPECT_TRUE(shared_tag->tag == NULL);
    fieldstat_tag_list_arr_free(shared_tag, 1);
    free(ret_cube_id_arr);

    int *ret_cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_read_cell(instance_dst, ret_cell_id, &ret_cell_ids, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(ret_cell_ids[0], 0);
    EXPECT_EQ(tag_list[0].n_tag, 0);
    EXPECT_TRUE(tag_list[0].tag == NULL);
    free(ret_cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);

    EXPECT_EQ(fieldstat_counter_get(instance_dst, 0, 0, 0), 2);

    fieldstat_free(instance_dst);
    fieldstat_free(instance_src);
}

TEST(test_empty_tag, export)
{
    struct fieldstat *instance = test_empty_my_init();
    struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new(instance);
    char *json_string = fieldstat_json_exporter_export(fieldstat_json_exporter);
    // std::cout << "json_string: \n" << json_string << std::endl;

    cJSON *root_arr = cJSON_Parse(json_string);
    free(json_string);
    fieldstat_json_exporter_free(fieldstat_json_exporter);
    fieldstat_free(instance);

    cJSON *root = cJSON_GetArrayItem(root_arr, 0);
    // check tag
    cJSON *tag = cJSON_GetObjectItem(root, "tags");
    EXPECT_EQ(cJSON_GetArraySize(tag), 0);

    cJSON *metrics = cJSON_GetObjectItem(root, "fields");
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