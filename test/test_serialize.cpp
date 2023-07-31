
#include <gtest/gtest.h>
#include "fieldstat.h"
#include "utils.hpp"


TEST(unit_test_serialize, serialize_and_deserialize_fieldstat_instance_comprehensive)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_counter(instance, cube_id, "czz_test counter metric", true);
    int cell_id1 = fieldstat_cube_add(instance, cube_id, &TEST_TAG_STRING, 1, 1);
    int cell_id2 = fieldstat_cube_add(instance, cube_id, &TEST_TAG_STRING_collided, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id1, 10086);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id2, 123);

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

    EXPECT_EQ(ret_cell_id, 0);
    int ret_metric_id = fieldstat_get_max_metric_id(instance2, ret_cell_id);
    EXPECT_EQ(ret_metric_id, 0);
    long long measure = fieldstat_counter_get(instance2, cube_id, metric_id, cell_id1);
    EXPECT_EQ(measure, 10086);
    const char *name = fieldstat_get_metric_name(instance2, cube_id, metric_id);
    EXPECT_STREQ(name, "czz_test counter metric");

    printf("going to get cells\n");

    int *ret_cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells(instance2, cube_id, metric_id, &ret_cell_ids, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 2);
    EXPECT_EQ(ret_cell_ids[0], cell_id1);
    EXPECT_EQ(ret_cell_ids[1], cell_id2);
    EXPECT_EQ(tag_list[0].n_tag, 1);
    EXPECT_EQ(tag_list[1].n_tag, 1);
    EXPECT_STREQ(tag_list[0].tag[0].key, TEST_TAG_STRING.key);
    EXPECT_STREQ(tag_list[1].tag[0].key, TEST_TAG_STRING_collided.key);
    
    fieldstat_free(instance);
    fieldstat_free(instance2);
    free(ret_cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
}

TEST(unit_test_serialize, serialize_and_deserialize_fieldstat_instance_topk)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, 10);
    int metric_id = fieldstat_register_counter(instance, cube_id, "czz_test counter metric", true);
    int cell_id1 = fieldstat_cube_add(instance, cube_id, &TEST_TAG_STRING, 1, 1);
    int cell_id2 = fieldstat_cube_add(instance, cube_id, &TEST_TAG_STRING_collided, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id1, 10086);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id2, 123);

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

    EXPECT_EQ(ret_cell_id, 0);
    int ret_metric_id = fieldstat_get_max_metric_id(instance2, ret_cell_id);
    EXPECT_EQ(ret_metric_id, 0);
    long long measure = fieldstat_counter_get(instance2, cube_id, metric_id, cell_id1);
    EXPECT_EQ(measure, 10086);
    const char *name = fieldstat_get_metric_name(instance2, cube_id, metric_id);
    EXPECT_STREQ(name, "czz_test counter metric");

    printf("going to get cells\n");

    int *ret_cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells(instance2, cube_id, metric_id, &ret_cell_ids, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 2);
    EXPECT_EQ(ret_cell_ids[0], cell_id1);
    EXPECT_EQ(ret_cell_ids[1], cell_id2);
    EXPECT_EQ(tag_list[0].n_tag, 1);
    EXPECT_EQ(tag_list[1].n_tag, 1);
    EXPECT_STREQ(tag_list[0].tag[0].key, TEST_TAG_STRING.key);
    EXPECT_STREQ(tag_list[1].tag[0].key, TEST_TAG_STRING_collided.key);
    
    fieldstat_free(instance);
    fieldstat_free(instance2);
    free(ret_cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
}

TEST(unit_test_serialize, rearrange_tags_when_serialize)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, 10);
    int metric_id = fieldstat_register_counter(instance, cube_id, "czz_test counter metric", true);
    struct fieldstat_tag tag1 = {"tag1", TAG_INTEGER, 1};
    struct fieldstat_tag tag2 = {"tag1", TAG_INTEGER, 2};
    struct fieldstat_tag tag3 = {"tag1", TAG_DOUBLE, {.value_double = 3.0}};
    struct fieldstat_tag tag4 = {"tag1", TAG_CSTRING, {.value_str = "4"}};
    struct fieldstat_tag tag5 = {"tag1", TAG_CSTRING, {.value_str = "5"}};
    struct fieldstat_tag tag6 = {"tag2", TAG_DOUBLE, {.value_double = 6.0}};
    const struct fieldstat_tag tags1[] = {tag6, tag2, tag1, tag3, tag5, tag4};
    const struct fieldstat_tag tags2[] = {tag1, tag2, tag3, tag4, tag5, tag6};
    int cell_id1 = fieldstat_cube_add(instance, cube_id, tags1, 6, 1);
    int cell_id2 = fieldstat_cube_add(instance, cube_id, tags2, 6, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id2, 2);

    char *blob;
    size_t blob_size;
    fieldstat_serialize(instance, &blob, &blob_size);
    struct fieldstat *instance2 = fieldstat_deserialize(blob, blob_size);
    free(blob);

    int *ret_cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells(instance2, cube_id, metric_id, &ret_cell_ids, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 2);
    for (size_t i = 0; i < n_cell; i++) {
        EXPECT_EQ(tag_list[i].n_tag, 6);
        for (size_t j = 0; j < tag_list[i].n_tag; j++) {
            EXPECT_STREQ(tag_list[i].tag[j].key, tags2[j].key);
            EXPECT_EQ(tag_list[i].tag[j].type, tags2[j].type);
            if (tag_list[i].tag[j].type == TAG_INTEGER) {
                EXPECT_EQ(tag_list[i].tag[j].value_longlong, tags2[j].value_longlong);
            } else if (tag_list[i].tag[j].type == TAG_DOUBLE) {
                EXPECT_EQ(tag_list[i].tag[j].value_double, tags2[j].value_double);
            } else if (tag_list[i].tag[j].type == TAG_CSTRING) {
                EXPECT_STREQ(tag_list[i].tag[j].value_str, tags2[j].value_str);
            }
        }
    }

    fieldstat_free(instance);
    fieldstat_free(instance2);
    free(ret_cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}