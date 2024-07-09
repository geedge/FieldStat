
#include <gtest/gtest.h>
#include "fieldstat.h"
#include "utils.hpp"


TEST(test_register, delete_cube_and_register_and_origin_position)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_destroy_cube(instance, cube_id);
    cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    EXPECT_EQ(cube_id, 0);
    EXPECT_EQ(fieldstat_find_cube(instance, &TEST_SHARED_TAG, 1) , 0);
    fieldstat_free(instance);
}

TEST(test_register, delete_comprehensive_cube_with_cells_and_metrics)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_counter(instance, "counter");
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_INT, 1, 1);

    fieldstat_destroy_cube(instance, cube_id);
    fieldstat_free(instance);
}

TEST(test_register, delete_topk_cube_with_cells_and_metrics)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_TOPK, 10);
    int metric_id = fieldstat_register_counter(instance, "counter");
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_INT, 1, 1);

    fieldstat_destroy_cube(instance, cube_id);
    fieldstat_free(instance);
}

int test_get_max_metric_id(const struct fieldstat *instance)
{
    int *metric_id_out;
    size_t n_metric;
    (void)fieldstat_get_metrics(instance, &metric_id_out, &n_metric);
    free(metric_id_out);
    return n_metric - 1;
}

TEST(test_register, reset_and_try_to_query_cell)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_counter(instance, "counter");
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_INT, 1, 1);

    fieldstat_reset(instance);
    EXPECT_EQ(test_get_max_metric_id(instance), 0);
    long long value;
    EXPECT_EQ(fieldstat_counter_get(instance, cube_id, &TEST_TAG_LIST_INT, metric_id, &value), FS_ERR_INVALID_TAG);

    fieldstat_free(instance);
}

TEST(test_register, reset_and_new_cell)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 2);
    int metric_id = fieldstat_register_counter(instance, "counter");
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_INT, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_DOUBLE, 1, 1);
    int ret = fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_STRING, 1, 1);
    EXPECT_EQ(ret, FS_ERR_TOO_MANY_CELLS);

    fieldstat_reset(instance);
    ret = fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_STRING, 1, 1);
    EXPECT_EQ(ret, FS_OK);

    fieldstat_free(instance);
}

TEST(test_register, register_many_cubes)
{
    struct fieldstat *instance = fieldstat_new();
    int registered_cube = 10000; // will trigger realloc many times
    struct field shared_tag = TEST_SHARED_TAG;
    for (int i = 0; i < registered_cube; i++) {
        shared_tag.value_longlong = i;
        int cube_id = fieldstat_create_cube(instance, &shared_tag, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
        EXPECT_EQ(cube_id, i);
    }
    // try to use the cube
    int metric_id = fieldstat_register_counter(instance, "counter");
    for (int i = 0; i < registered_cube; i++) {
        fieldstat_counter_incrby(instance, i, metric_id, &TEST_TAG_INT, 1, i);
    }

    for (int i = 0; i < registered_cube; i++) {
        long long result;
        fieldstat_counter_get(instance, i, &TEST_TAG_LIST_INT, 0,  &result);
        EXPECT_EQ(result, i);
    }

    fieldstat_free(instance);
}

TEST(test_register, add_many_tagged_cells)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    size_t n_field = 1000;
    struct field test_tag_long[n_field];
    for (int i = 0; i < n_field; i++) {
        test_tag_long[i] = TEST_TAG_INT; // will trigger realloc 
    }

    int metric_id = fieldstat_register_counter(instance, "counter");
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
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    struct field test_tag_long = TEST_TAG_STRING;
    char *long_string = (char *)malloc(5001);
    memset(long_string, 'a', 5001);
    long_string[5000] = '\0';
    test_tag_long.value_str = long_string;

    int metric_id = fieldstat_register_counter(instance, "counter");
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
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = 0;
    for (int i = 0; i < 200; i++) {
        metric_id = fieldstat_register_counter(instance, (std::string("counter ") + std::to_string(i)).c_str());
        EXPECT_EQ(metric_id, i);
    }

    for (int i = 0; i < 200; i++) {
        EXPECT_EQ(fieldstat_counter_incrby(instance, cube_id, i, &TEST_TAG_INT, 1, 1), 0);
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
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_counter(instance, "counter");
    int metric_id2 = fieldstat_register_counter(instance, "counter2");
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_INT, 1, 1);
    int cube_id_del = fieldstat_create_cube(instance, &TEST_TAG_DOUBLE_collided, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_create_cube(instance, &TEST_TAG_DOUBLE, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_destroy_cube(instance, cube_id_del);

    struct fieldstat *dup = fieldstat_fork(instance);

    int *cube_ids;
    int cube_num;
    fieldstat_get_cubes(dup, &cube_ids, &cube_num);
    EXPECT_EQ(cube_num, 2);
    struct field_list *tag_list = NULL;
    
    tag_list = fieldstat_cube_get_tags(dup, cube_ids[0]);
    EXPECT_STREQ(tag_list->field[0].key, TEST_SHARED_TAG.key);
    fieldstat_tag_list_arr_free(tag_list, 1);
    size_t n_cell = 0;
    fieldstat_cube_get_cells(dup, cube_ids[0], &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 0);

    tag_list = fieldstat_cube_get_tags(dup, cube_ids[1]);
    EXPECT_STREQ(tag_list->field[0].key, TEST_TAG_DOUBLE.key);
    free(cube_ids);
    fieldstat_tag_list_arr_free(tag_list, 1);

    EXPECT_STREQ(fieldstat_get_metric_name(dup, metric_id), "counter");
    EXPECT_STREQ(fieldstat_get_metric_name(dup, metric_id2), "counter2");

    fieldstat_free(dup);
    fieldstat_free(instance);
}

TEST(test_register, unregister_cube_on_wrong_instance)
{
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat *instance_dst = fieldstat_new();

    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int cube_id2 = fieldstat_create_cube(instance, &TEST_TAG_DOUBLE, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_counter(instance, "counter");
    int metric_id2 = fieldstat_register_counter(instance, "counter2");
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_INT, 1, 1);
    fieldstat_counter_incrby(instance, cube_id2, metric_id2, &TEST_TAG_INT, 1, 1);

    fieldstat_merge(instance_dst, instance);

    fieldstat_destroy_cube(instance_dst, 0);

    int *cube_ids;
    int cube_num;
    fieldstat_get_cubes(instance_dst, &cube_ids, &cube_num);
    EXPECT_EQ(cube_num, 1);
    EXPECT_EQ(cube_ids[0], cube_id2);

    fieldstat_merge(instance_dst, instance);

    long long val_deleted_once;
    fieldstat_counter_get(instance_dst, cube_id, &TEST_TAG_LIST_INT,metric_id,  &val_deleted_once);
    EXPECT_EQ(val_deleted_once, 1);
    long long val_merged_twice;
    fieldstat_counter_get(instance_dst, cube_id2, &TEST_TAG_LIST_INT, metric_id2,  &val_merged_twice);
    EXPECT_EQ(val_merged_twice, 2);

    fieldstat_free(instance);
    fieldstat_free(instance_dst);
    free(cube_ids);
}

TEST(test_register, register_many_cells_on_unlimited_sized_cube)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 0);
    int metric_id = fieldstat_register_counter(instance, "counter");
    struct field test_tag = {"abc", TAG_INTEGER, {.value_longlong = 0}};
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
    fieldstat_register_counter(instance, "counter");
    int metric_id2 = fieldstat_register_counter(instance, "counter");
    EXPECT_EQ(metric_id2, FS_ERR_INVALID_KEY);
    fieldstat_free(instance);
}

TEST(calibrate, target_one_more_metric)
{
    struct fieldstat *master = fieldstat_new();
    int cube_id = fieldstat_create_cube(master, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_counter(master, "counter");
    struct fieldstat *target = fieldstat_fork(master);
    EXPECT_EQ(fieldstat_register_counter(target, "counter2"), 1);

    fieldstat_calibrate(master, target);

    EXPECT_EQ(test_get_max_metric_id(target), 0);
    EXPECT_STREQ(fieldstat_get_metric_name(target, 0), "counter");
    struct field_list *tag_list = fieldstat_cube_get_tags(target, cube_id);
    EXPECT_STREQ(tag_list->field[0].key, TEST_SHARED_TAG.key);

    fieldstat_free(master);
    fieldstat_free(target);
    fieldstat_tag_list_arr_free(tag_list, 1);
}

TEST(calibrate, master_one_more_metric)
{
    struct fieldstat *master = fieldstat_new();
    int cube_id = fieldstat_create_cube(master, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_counter(master, "counter");
    struct fieldstat *target = fieldstat_fork(master);
    EXPECT_EQ(fieldstat_register_counter(master, "counter2"), 1);

    fieldstat_calibrate(master, target);

    EXPECT_EQ(test_get_max_metric_id(target), 1);
    EXPECT_STREQ(fieldstat_get_metric_name(target, 0), "counter");
    EXPECT_STREQ(fieldstat_get_metric_name(target, 1), "counter2");
    struct field_list *tag_list = fieldstat_cube_get_tags(target, cube_id);
    EXPECT_STREQ(tag_list->field[0].key, TEST_SHARED_TAG.key);

    fieldstat_free(master);
    fieldstat_free(target);
    fieldstat_tag_list_arr_free(tag_list, 1);
}

// different metric on same cube
TEST(calibrate, different_metric)
{
    struct fieldstat *master = fieldstat_new();
    int cube_id = fieldstat_create_cube(master, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_counter(master, "counter");
    struct fieldstat *target = fieldstat_fork(master);
    EXPECT_EQ(fieldstat_register_counter(target, "counter2"), 1);
    EXPECT_EQ(fieldstat_register_counter(master, "hi i am master new"), 1);

    fieldstat_calibrate(master, target);

    EXPECT_EQ(test_get_max_metric_id(target), 1);
    EXPECT_STREQ(fieldstat_get_metric_name(target, 0), "counter");
    EXPECT_STREQ(fieldstat_get_metric_name(target, 1), "hi i am master new");
    struct field_list *tag_list = fieldstat_cube_get_tags(target, cube_id);
    EXPECT_STREQ(tag_list->field[0].key, TEST_SHARED_TAG.key);

    fieldstat_free(master);
    fieldstat_free(target);
    fieldstat_tag_list_arr_free(tag_list, 1);
}

TEST(calibrate, target_more_cube)
{
    struct fieldstat *master = fieldstat_new();
    int cube_id = fieldstat_create_cube(master, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_counter(master, "counter");
    struct fieldstat *target = fieldstat_fork(master);
    int cube_id2 = fieldstat_create_cube(target, &TEST_TAG_STRING, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    EXPECT_EQ(cube_id2, 1);
    fieldstat_register_counter(target, "counter2");

    fieldstat_calibrate(master, target);

    int n_cube;
    int *cube_ids;
    fieldstat_get_cubes(target, &cube_ids, &n_cube);
    EXPECT_EQ(n_cube, 1);
    EXPECT_EQ(cube_ids[0], cube_id);
    free(cube_ids);

    EXPECT_EQ(test_get_max_metric_id(target), 0);
    EXPECT_STREQ(fieldstat_get_metric_name(target, 0), "counter");
    struct field_list *tag_list = fieldstat_cube_get_tags(target, cube_id);
    EXPECT_STREQ(tag_list->field[0].key, TEST_SHARED_TAG.key);

    EXPECT_EQ(fieldstat_find_cube(target, &TEST_SHARED_TAG, 1), 0);
    EXPECT_EQ(fieldstat_find_cube(target, &TEST_TAG_STRING, 1), FS_ERR_INVALID_KEY);

    fieldstat_free(master);
    fieldstat_free(target);
    fieldstat_tag_list_arr_free(tag_list, 1);
}

TEST(calibrate, master_more_cube)
{
    struct fieldstat *master = fieldstat_new();
    int cube_id = fieldstat_create_cube(master, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    struct fieldstat *target = fieldstat_fork(master);
    int cube_id2 = fieldstat_create_cube(master, &TEST_TAG_STRING, 1, SAMPLING_MODE_COMPREHENSIVE, 10);

    fieldstat_calibrate(master, target);

    int n_cube;
    int *cube_ids;
    fieldstat_get_cubes(target, &cube_ids, &n_cube);
    EXPECT_EQ(n_cube, 2);
    EXPECT_EQ(cube_ids[0], cube_id);
    EXPECT_EQ(cube_ids[1], cube_id2);
    free(cube_ids);

    struct field_list *tag_list = fieldstat_cube_get_tags(target, cube_id);
    EXPECT_STREQ(tag_list->field[0].key, TEST_SHARED_TAG.key);
    struct field_list *tag_list2 = fieldstat_cube_get_tags(target, cube_id2);
    EXPECT_STREQ(tag_list2->field[0].key, TEST_TAG_STRING.key);

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
    fieldstat_register_counter(master, "counter");
    struct fieldstat *target = fieldstat_fork(master);
    fieldstat_destroy_cube(master, cube_id);
    fieldstat_create_cube(master, &TEST_TAG_STRING, 1, SAMPLING_MODE_COMPREHENSIVE, 10);

    fieldstat_calibrate(master, target);

    struct field_list *tag_list = fieldstat_cube_get_tags(target, cube_id);
    EXPECT_STREQ(tag_list->field[0].key, TEST_TAG_STRING.key);

    EXPECT_EQ(fieldstat_find_cube(target, &TEST_TAG_STRING, 1), 0);

    fieldstat_free(master);
    fieldstat_free(target);
    fieldstat_tag_list_arr_free(tag_list, 1);
}

TEST(calibrate, master_many_cube)
{
    struct fieldstat *master = fieldstat_new();
    int registered_cube = 3000; // will trigger realloc 1 times
    struct field shared_tag = TEST_SHARED_TAG;
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
    const struct field *tag_B = &TEST_TAG_INT;
    int cube_idA = fieldstat_create_cube(master, tag_A, 1, SAMPLING_MODE_COMPREHENSIVE, 1);
    int cube_idB = fieldstat_create_cube(master, tag_B, 1, SAMPLING_MODE_COMPREHENSIVE, 1);
    struct fieldstat *target = fieldstat_fork(master);

    EXPECT_EQ(fieldstat_destroy_cube(master, cube_idA), FS_OK);
    EXPECT_EQ(fieldstat_destroy_cube(master, cube_idB), FS_OK);
    int cube_idBa = fieldstat_create_cube(master, tag_B, 1, SAMPLING_MODE_COMPREHENSIVE, 1);
    EXPECT_EQ(cube_idBa, 0);

    fieldstat_calibrate(master, target);

    int *cubes_id_ret = NULL;
    int n_cubes = 0;
    fieldstat_get_cubes(target, &cubes_id_ret, &n_cubes);
    EXPECT_EQ(n_cubes, 1);
    EXPECT_EQ(fieldstat_find_cube(target, tag_A, 1), FS_ERR_INVALID_KEY);
    EXPECT_EQ(fieldstat_find_cube(target, tag_B, 1), cubes_id_ret[0]);

    struct field_list *tag_list = fieldstat_cube_get_tags(target, cubes_id_ret[0]);
    EXPECT_STREQ(tag_list->field[0].key, tag_B->key);
    fieldstat_tag_list_arr_free(tag_list, 1);

    fieldstat_free(master);
    fieldstat_free(target);
    free(cubes_id_ret);
}

TEST(calibrate, delete_first_cube)
{
    struct fieldstat *master = fieldstat_new();
    const struct field *tag_A = &TEST_SHARED_TAG;
    const struct field *tag_B = &TEST_TAG_INT;
    int cube_idA = fieldstat_create_cube(master, tag_A, 1, SAMPLING_MODE_COMPREHENSIVE, 1);
    int cube_idB = fieldstat_create_cube(master, tag_B, 1, SAMPLING_MODE_COMPREHENSIVE, 1);
    struct fieldstat *target = fieldstat_fork(master);

    fieldstat_destroy_cube(master, cube_idA);

    fieldstat_calibrate(master, target);

    int *cubes_id_ret = NULL;
    int n_cubes = 0;
    fieldstat_get_cubes(target, &cubes_id_ret, &n_cubes);
    EXPECT_EQ(n_cubes, 1);
    EXPECT_EQ(cubes_id_ret[0], cube_idB);
    free(cubes_id_ret);

    EXPECT_EQ(fieldstat_find_cube(target, tag_A, 1), FS_ERR_INVALID_KEY);
    EXPECT_EQ(fieldstat_find_cube(target, tag_B, 1), 1);

    struct field_list *tag_list = fieldstat_cube_get_tags(target, 1);
    EXPECT_STREQ(tag_list->field[0].key, tag_B->key);
    fieldstat_tag_list_arr_free(tag_list, 1);

    fieldstat_free(master);
    fieldstat_free(target);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}