
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
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    EXPECT_EQ(fieldstat_get_cube_version(instance, cube_id), 0);
    int ret = fieldstat_destroy_cube(instance, cube_id);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(fieldstat_get_cube_version(instance, cube_id), 1);
    ret = fieldstat_destroy_cube(instance, cube_id);
    EXPECT_EQ(ret, FS_ERR_INVALID_CUBE_ID);
    EXPECT_EQ(fieldstat_get_cube_version(instance, cube_id), 1);

    fieldstat_free(instance);
}

TEST(test_register, query_on_wrong_version)
{
    EXPECT_EQ(fieldstat_get_cube_version(NULL, 1), FS_ERR_NULL_HANDLER);
    struct fieldstat *instance = fieldstat_new();
    EXPECT_EQ(fieldstat_get_cube_version(instance, 1), FS_ERR_INVALID_CUBE_ID);
    EXPECT_EQ(fieldstat_get_cube_version(instance, -1), FS_ERR_INVALID_CUBE_ID);
    fieldstat_free(instance);
}

TEST(test_register, delete_cube_and_register_and_origin_position)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_destroy_cube(instance, cube_id);
    cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    EXPECT_EQ(cube_id, 0);
    fieldstat_free(instance);
}

TEST(test_register, delete_comprehensive_cube_with_cells_and_metrics)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_INT, 1, 1);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter", COUNTER_MERGE_BY_SUM);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 1);

    fieldstat_destroy_cube(instance, cube_id);
    fieldstat_free(instance);
}

TEST(test_register, delete_topk_cube_with_cells_and_metrics)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, 10);
    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_INT, 1, 1);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter", COUNTER_MERGE_BY_SUM);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 1);

    fieldstat_destroy_cube(instance, cube_id);
    fieldstat_free(instance);
}

TEST(test_register, reset_and_try_to_query_cell)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_INT, 1, 1);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter", COUNTER_MERGE_BY_SUM);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 1);

    fieldstat_reset(instance);
    EXPECT_EQ(fieldstat_get_max_metric_id(instance, cube_id), 0);
    long long value;
    EXPECT_EQ(fieldstat_counter_get(instance, cube_id, metric_id, cell_id, &value), -1);

    fieldstat_free(instance);
}

TEST(test_register, register_many_cubes)
{
    struct fieldstat *instance = fieldstat_new();
    int registered_cube = 10000; // will trigger realloc many times
    struct fieldstat_tag shared_tag = TEST_SHARED_TAG;
    for (int i = 0; i < registered_cube; i++) {
        shared_tag.value_longlong = i;
        int cube_id = fieldstat_create_cube(instance, &shared_tag, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
        EXPECT_EQ(cube_id, i);
    }
    // try to use the cube
    for (int i = 0; i < registered_cube; i++) {
        int cell_id = fieldstat_cube_add(instance, i, &TEST_TAG_INT, 1, 1);
        int metric_id = fieldstat_register_counter(instance, i, "counter", COUNTER_MERGE_BY_SUM);
        fieldstat_counter_incrby(instance, i, metric_id, cell_id, i);
    }

    for (int i = 0; i < registered_cube; i++) {
        long long result = my_fieldstat_counter_get(instance, i, 0, 0);
        EXPECT_EQ(result, i);
    }

    fieldstat_free(instance);
}

TEST(test_register, add_long_tagged_cells)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    struct fieldstat_tag test_tag_long[100];
    for (int i = 0; i < 100; i++) {
        test_tag_long[i] = TEST_TAG_INT;
    }
    int cell_id = fieldstat_cube_add(instance, cube_id, test_tag_long, 100, 1);
    EXPECT_EQ(cell_id, 0);

    int metric_id = fieldstat_register_counter(instance, cube_id, "counter", COUNTER_MERGE_BY_SUM);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 10086);

    EXPECT_EQ(my_fieldstat_counter_get(instance, cube_id, metric_id, cell_id), 10086);

    fieldstat_free(instance);
}

TEST(test_register, register_too_many_metrics)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
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
    
    struct fieldstat *dup = fieldstat_fork(instance);

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
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter", COUNTER_MERGE_BY_SUM);
    int metric_id2 = fieldstat_register_counter(instance, cube_id, "counter2", COUNTER_MERGE_BY_SUM);
    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_INT, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 1);
    int cube_id_del = fieldstat_create_cube(instance, &TEST_TAG_DOUBLE_collided, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int cube_id2 = fieldstat_create_cube(instance, &TEST_TAG_DOUBLE, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_destroy_cube(instance, cube_id_del);

    struct fieldstat *dup = fieldstat_fork(instance);

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

    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int cube_id2 = fieldstat_create_cube(instance, &TEST_TAG_DOUBLE, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter", COUNTER_MERGE_BY_SUM);
    int metric_id2 = fieldstat_register_counter(instance, cube_id2, "counter2", COUNTER_MERGE_BY_SUM);
    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_INT, 1, 1);
    int cell_id2 = fieldstat_cube_add(instance, cube_id2, &TEST_TAG_INT, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 1);
    fieldstat_counter_incrby(instance, cube_id2, metric_id2, cell_id2, 1);

    fieldstat_merge(instance_dst, instance);

    fieldstat_destroy_cube(instance_dst, 0);

    int *cube_ids;
    int cube_num;
    fieldstat_get_cubes(instance_dst, &cube_ids, &cube_num);
    EXPECT_EQ(cube_num, 1);
    EXPECT_EQ(cube_ids[0], cube_id2);

    fieldstat_merge(instance_dst, instance);

    long long val_deleted_once = my_fieldstat_counter_get(instance_dst, cube_id, metric_id, cell_id);
    EXPECT_EQ(val_deleted_once, 1);
    long long val_merged_twice = my_fieldstat_counter_get(instance_dst, cube_id2, metric_id2, cell_id2);
    EXPECT_EQ(val_merged_twice, 2);

    fieldstat_free(instance);
    fieldstat_free(instance_dst);
    free(cube_ids);
}

TEST(test_register, register_many_cells_on_unlimited_sized_cube)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 0);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter", COUNTER_MERGE_BY_SUM);
    int cell_id = 0;
    struct fieldstat_tag test_tag = {"abc", TAG_INTEGER, {.value_longlong = 0}};
    for (int i = 0; i < 10000; i++) {
        test_tag.value_longlong = i;
        cell_id = fieldstat_cube_add(instance, cube_id, &test_tag, 1, 1);
        EXPECT_EQ(cell_id, i);
        fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 1);
    }

    for (int i = 0; i < 10000; i++) {
        EXPECT_EQ(my_fieldstat_counter_get(instance, cube_id, metric_id, i), 1);
    }

    fieldstat_free(instance);
}

TEST(test_register, unregister_cell)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter", COUNTER_MERGE_BY_SUM);
    int cell_id1 = fieldstat_cube_add(instance, cube_id, &TEST_TAG_INT, 1, 1);
    int cell_id2 = fieldstat_cube_add(instance, cube_id, &TEST_TAG_DOUBLE, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id2, 1);

    int cell_id_removed = fieldstat_cube_remove(instance, cube_id, &TEST_TAG_INT, 1);
    EXPECT_EQ(cell_id_removed, cell_id1);
    long long value;
    EXPECT_EQ(fieldstat_counter_get(instance, cube_id, metric_id, cell_id1, &value), -1);
    
    int *cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells_from_cube(instance, cube_id, &cell_ids, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(cell_ids[0], cell_id2);
    EXPECT_EQ(tag_list[0].tag->type, TEST_TAG_DOUBLE.type);
    EXPECT_STREQ(tag_list[0].tag->key, TEST_TAG_DOUBLE.key);
    free(cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);

    // test merge and serialize to make sure no changes on basic functions
    struct fieldstat *instance_dst = fieldstat_new();
    fieldstat_merge(instance_dst, instance);
    fieldstat_get_cells_from_cube(instance, cube_id, &cell_ids, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_STREQ(tag_list[0].tag->key, TEST_TAG_DOUBLE.key);
    fieldstat_free(instance_dst);
    free(cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);

    char *blob;
    size_t blob_size;
    fieldstat_serialize(instance, &blob, &blob_size);
    struct fieldstat *instance_ser = fieldstat_deserialize(blob, blob_size);
    free(blob);
    fieldstat_get_cells_from_cube(instance_ser, cube_id, &cell_ids, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_STREQ(tag_list[0].tag->key, TEST_TAG_DOUBLE.key);
    fieldstat_free(instance_ser);
    free(cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);

    // completely new tag even with original tag. New posision(not original 0)
    EXPECT_EQ(fieldstat_cube_add(instance, cube_id, &TEST_TAG_INT, 1, 1), 2);

    fieldstat_free(instance);
}

TEST(test_register, register_cube_twice) {
    struct fieldstat *instance = fieldstat_new();
    fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, 10);
    int cube_id2 = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, 10);
    EXPECT_EQ(cube_id2, FS_ERR_INVALID_KEY);
    fieldstat_free(instance);
}

TEST(test_register, find_cube) {
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, 10);

    int find_cube_id = fieldstat_find_cube(instance, &TEST_SHARED_TAG, 1);
    EXPECT_EQ(cube_id, find_cube_id);
    int find_cube_id2 = fieldstat_find_cube(instance, &TEST_TAG_DOUBLE, 1);
    EXPECT_EQ(find_cube_id2, FS_ERR_INVALID_KEY);
    
    fieldstat_free(instance);
}

TEST(test_register, register_metric_twice) {
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, 10);
    fieldstat_register_counter(instance, cube_id, "counter", COUNTER_MERGE_BY_SUM);
    int metric_id2 = fieldstat_register_counter(instance, cube_id, "counter", COUNTER_MERGE_BY_SUM);
    EXPECT_EQ(metric_id2, FS_ERR_INVALID_KEY);
    fieldstat_free(instance);
}

TEST(calibrate, target_one_more_metric)
{
    struct fieldstat *master = fieldstat_new();
    int cube_id = fieldstat_create_cube(master, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_counter(master, cube_id, "counter", COUNTER_MERGE_BY_SUM);
    struct fieldstat *target = fieldstat_fork(master);
    EXPECT_EQ(fieldstat_register_counter(target, cube_id, "counter2", COUNTER_MERGE_BY_SUM), 1);

    fieldstat_calibrate(master, target);

    EXPECT_EQ(fieldstat_get_max_metric_id(target, cube_id), 0);
    EXPECT_STREQ(fieldstat_get_metric_name(target, cube_id, 0), "counter");
    struct fieldstat_tag_list *tag_list = fieldstat_get_shared_tags(target, cube_id);
    EXPECT_STREQ(tag_list->tag[0].key, TEST_SHARED_TAG.key);

    fieldstat_free(master);
    fieldstat_free(target);
    fieldstat_tag_list_arr_free(tag_list, 1);
}

TEST(calibrate, master_one_more_metric)
{
    struct fieldstat *master = fieldstat_new();
    int cube_id = fieldstat_create_cube(master, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_counter(master, cube_id, "counter", COUNTER_MERGE_BY_SUM);
    struct fieldstat *target = fieldstat_fork(master);
    EXPECT_EQ(fieldstat_register_counter(master, cube_id, "counter2", COUNTER_MERGE_BY_SUM), 1);

    fieldstat_calibrate(master, target);

    EXPECT_EQ(fieldstat_get_max_metric_id(target, cube_id), 1);
    EXPECT_STREQ(fieldstat_get_metric_name(target, cube_id, 0), "counter");
    EXPECT_STREQ(fieldstat_get_metric_name(target, cube_id, 1), "counter2");
    struct fieldstat_tag_list *tag_list = fieldstat_get_shared_tags(target, cube_id);
    EXPECT_STREQ(tag_list->tag[0].key, TEST_SHARED_TAG.key);

    fieldstat_free(master);
    fieldstat_free(target);
    fieldstat_tag_list_arr_free(tag_list, 1);
}

// different metric on same cube
TEST(calibrate, different_metric)
{
    struct fieldstat *master = fieldstat_new();
    int cube_id = fieldstat_create_cube(master, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_counter(master, cube_id, "counter", COUNTER_MERGE_BY_SUM);
    struct fieldstat *target = fieldstat_fork(master);
    EXPECT_EQ(fieldstat_register_counter(target, cube_id, "counter2", COUNTER_MERGE_BY_SUM), 1);
    EXPECT_EQ(fieldstat_register_counter(master, cube_id, "hi i am master new", COUNTER_MERGE_BY_SUM), 1);

    fieldstat_calibrate(master, target);

    EXPECT_EQ(fieldstat_get_max_metric_id(target, cube_id), 1);
    EXPECT_STREQ(fieldstat_get_metric_name(target, cube_id, 0), "counter");
    EXPECT_STREQ(fieldstat_get_metric_name(target, cube_id, 1), "hi i am master new");
    struct fieldstat_tag_list *tag_list = fieldstat_get_shared_tags(target, cube_id);
    EXPECT_STREQ(tag_list->tag[0].key, TEST_SHARED_TAG.key);

    fieldstat_free(master);
    fieldstat_free(target);
    fieldstat_tag_list_arr_free(tag_list, 1);
}

TEST(calibrate, target_more_cube)
{
    struct fieldstat *master = fieldstat_new();
    int cube_id = fieldstat_create_cube(master, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_counter(master, cube_id, "counter", COUNTER_MERGE_BY_SUM);
    struct fieldstat *target = fieldstat_fork(master);
    int cube_id2 = fieldstat_create_cube(target, &TEST_TAG_STRING, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    EXPECT_EQ(cube_id2, 1);
    fieldstat_register_counter(target, cube_id2, "counter2", COUNTER_MERGE_BY_SUM);

    fieldstat_calibrate(master, target);

    int n_cube;
    int *cube_ids;
    fieldstat_get_cubes(target, &cube_ids, &n_cube);
    EXPECT_EQ(n_cube, 1);
    EXPECT_EQ(cube_ids[0], cube_id);
    free(cube_ids);

    EXPECT_EQ(fieldstat_get_max_metric_id(target, cube_id), 0);
    EXPECT_STREQ(fieldstat_get_metric_name(target, cube_id, 0), "counter");
    struct fieldstat_tag_list *tag_list = fieldstat_get_shared_tags(target, cube_id);
    EXPECT_STREQ(tag_list->tag[0].key, TEST_SHARED_TAG.key);

    // todo: query by shared tag,保证cube manager 更新

    fieldstat_free(master);
    fieldstat_free(target);
    fieldstat_tag_list_arr_free(tag_list, 1);
}

TEST(calibrate, master_more_cube)
{
    struct fieldstat *master = fieldstat_new();
    int cube_id = fieldstat_create_cube(master, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_counter(master, cube_id, "counter", COUNTER_MERGE_BY_SUM);
    struct fieldstat *target = fieldstat_fork(master);
    int cube_id2 = fieldstat_create_cube(master, &TEST_TAG_STRING, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_counter(master, cube_id2, "counter2", COUNTER_MERGE_BY_SUM);

    fieldstat_calibrate(master, target);

    int n_cube;
    int *cube_ids;
    fieldstat_get_cubes(target, &cube_ids, &n_cube);
    EXPECT_EQ(n_cube, 2);
    EXPECT_EQ(cube_ids[0], cube_id);
    EXPECT_EQ(cube_ids[1], cube_id2);
    free(cube_ids);

    EXPECT_EQ(fieldstat_get_max_metric_id(target, cube_id), 0);
    EXPECT_STREQ(fieldstat_get_metric_name(target, cube_id, 0), "counter");
    struct fieldstat_tag_list *tag_list = fieldstat_get_shared_tags(target, cube_id);
    EXPECT_STREQ(tag_list->tag[0].key, TEST_SHARED_TAG.key);

    EXPECT_EQ(fieldstat_get_max_metric_id(target, cube_id2), 0);
    EXPECT_STREQ(fieldstat_get_metric_name(target, cube_id2, 0), "counter2");
    struct fieldstat_tag_list *tag_list2 = fieldstat_get_shared_tags(target, cube_id2);
    EXPECT_STREQ(tag_list2->tag[0].key, TEST_TAG_STRING.key);

    EXPECT_EQ(fieldstat_find_cube(target, &TEST_TAG_STRING, 1), 1);
    EXPECT_EQ(fieldstat_find_cube(target, &TEST_SHARED_TAG, 1), 0);

    fieldstat_free(master);
    fieldstat_free(target);
    fieldstat_tag_list_arr_free(tag_list, 1);
    fieldstat_tag_list_arr_free(tag_list2, 1);
}

TEST(calibrate, master_change_cube)
{
    struct fieldstat *master = fieldstat_new();
    int cube_id = fieldstat_create_cube(master, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_counter(master, cube_id, "counter", COUNTER_MERGE_BY_SUM);
    struct fieldstat *target = fieldstat_fork(master);
    fieldstat_destroy_cube(master, cube_id);
    fieldstat_create_cube(master, &TEST_TAG_STRING, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_counter(master, cube_id, "counter2", COUNTER_MERGE_BY_SUM);

    fieldstat_calibrate(master, target);

    EXPECT_EQ(fieldstat_get_max_metric_id(target, cube_id), 0);
    EXPECT_STREQ(fieldstat_get_metric_name(target, cube_id, 0), "counter2");
    struct fieldstat_tag_list *tag_list = fieldstat_get_shared_tags(target, cube_id);
    EXPECT_STREQ(tag_list->tag[0].key, TEST_TAG_STRING.key);

    EXPECT_EQ(fieldstat_find_cube(target, &TEST_TAG_STRING, 1), 0);

    fieldstat_free(master);
    fieldstat_free(target);
    fieldstat_tag_list_arr_free(tag_list, 1);
}

TEST(calibrate, master_many_cube)
{
    struct fieldstat *master = fieldstat_new();
    int registered_cube = 3000; // will trigger realloc 1 times
    struct fieldstat_tag shared_tag = TEST_SHARED_TAG;
    for (int i = 0; i < registered_cube; i++) {
        shared_tag.value_longlong = i;
        int cube_id = fieldstat_create_cube(master, &shared_tag, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
        EXPECT_EQ(cube_id, i);
    }
    struct fieldstat *target = fieldstat_fork(master);
    int new_registered = 5000;
    for (int i = 0; i < new_registered; i++) {
        shared_tag.value_longlong = i + registered_cube;
        int cube_id = fieldstat_create_cube(master, &shared_tag, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
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


int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}