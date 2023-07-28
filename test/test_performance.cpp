


#include <gtest/gtest.h>
#include "fieldstat.h"
#include "utils.hpp"


TEST(test_performance, merge_performance_when_comprehensive_sampling)
{   
    const int INSTANCE_NUM = 100;
    const int MAX_CELL_NUM = 65535;
    const int DIMENSION_TOTAL = 100000;
    // const int INSTANCE_NUM = 2;
    // const int MAX_CELL_NUM = 1000;
    // const int DIMENSION_TOTAL = 1024;
    Fieldstat_tag_list_wrapper *tags[DIMENSION_TOTAL];
    for (int i = 0; i < DIMENSION_TOTAL; i++)
    {
        tags[i] = new Fieldstat_tag_list_wrapper("my key", i);
    }

    struct fieldstat *instances[INSTANCE_NUM];
    for (int i = 0; i < INSTANCE_NUM; i++) {
        struct fieldstat *tmp_i = fieldstat_new();
        int cube_id = fieldstat_register_cube(tmp_i, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, MAX_CELL_NUM);
        int metric_id = fieldstat_register_counter(tmp_i, cube_id, "metric name", false);
        for (int j = 0; j < MAX_CELL_NUM; j++) {
            int cell_id = fieldstat_cube_add(tmp_i, cube_id, tags[rand() % DIMENSION_TOTAL]->get_tag(), 1, 1);

            fieldstat_counter_incrby(tmp_i, cube_id, metric_id, cell_id, 1);
        }
        instances[i] = tmp_i;
    }
    
    struct fieldstat *instance_dest = fieldstat_new();
    printf("prepare done\n");

    clock_t start = clock();
    // getchar();
    for (int i = 0; i < INSTANCE_NUM; i++) {
        fieldstat_merge(instance_dest, instances[i]);
    }
    // exit(0);
    clock_t end = clock();

    double elapsed_secs = double(end - start) / CLOCKS_PER_SEC;
    printf("performance_test_when_comprehensive_sampling elapsed_secs: %f\n", elapsed_secs);
    EXPECT_TRUE(elapsed_secs < 0.1);

    fieldstat_free(instance_dest);
    for (int i = 0; i < INSTANCE_NUM; i++) {
        fieldstat_free(instances[i]);
    }
    for (int i = 0; i < DIMENSION_TOTAL; i++) {
        delete tags[i];
    }
}

TEST(test_performance, merge_performance_when_topk_sampling){
    const int INSTANCE_NUM = 100;
    const int MAX_CELL_NUM = 1024;
    const int DIMENSION_TOTAL = 100000;
    const int CUBE_NUM = 100;

    Fieldstat_tag_list_wrapper *tags[DIMENSION_TOTAL];
    for (int i = 0; i < DIMENSION_TOTAL; i++)
    {
        if (i%2)
            tags[i] = new Fieldstat_tag_list_wrapper("my key", i);
        else
            tags[i] = new Fieldstat_tag_list_wrapper("elephant", i % 1024);
    }

    Fieldstat_tag_list_wrapper *shared_tags[CUBE_NUM];
    for (int i = 0; i < CUBE_NUM; i++)
    {
        shared_tags[i] = new Fieldstat_tag_list_wrapper("shared key", i);
    }
    
    struct fieldstat *instances[INSTANCE_NUM];
    for (int i = 0; i < INSTANCE_NUM; i++) {
        for (int j = 0; j < CUBE_NUM; j++) {
            struct fieldstat *tmp_i = fieldstat_new();
            int cube_id = fieldstat_register_cube(tmp_i, shared_tags[j]->get_tag(), 1, SAMPLING_MODE_TOPK, MAX_CELL_NUM);
            int metric_id = fieldstat_register_counter(tmp_i, cube_id, "metric name", false);
            for (int j = 0; j < MAX_CELL_NUM; j++) {
                int cell_id = fieldstat_cube_add(tmp_i, cube_id, tags[rand() % DIMENSION_TOTAL]->get_tag(), 1, 1);
                if (cell_id == -1) {
                    continue;
                }
                fieldstat_counter_incrby(tmp_i, cube_id, metric_id, cell_id, 1);
            }
            instances[i] = tmp_i;
        }
    }

    struct fieldstat *instance_dest = fieldstat_new();
    printf("prepare done\n");
    clock_t start = clock();
    // getchar();
    for (int i = 0; i < INSTANCE_NUM; i++) {
        fieldstat_merge(instance_dest, instances[i]);
    }
    // exit(0);
    clock_t end = clock();

    double elapsed_secs = double(end - start) / CLOCKS_PER_SEC;
    printf("performance_test_when_comprehensive_sampling elapsed_secs: %f\n", elapsed_secs);
    EXPECT_TRUE(elapsed_secs < 0.2);

    fieldstat_free(instance_dest);
    for (int i = 0; i < INSTANCE_NUM; i++) {
        fieldstat_free(instances[i]);
    }
    for (int i = 0; i < DIMENSION_TOTAL; i++) {
        delete tags[i];
    }
}

TEST(metric_test_counter, performance_test_on_1_cells_1000000_times)
{
    // getchar();
    struct fieldstat *instance = fieldstat_new();
    fieldstat_register_cube(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_counter(instance, 0, "test", 0);
    int cell_id = fieldstat_cube_add(instance, 0, &TEST_TAG_DOUBLE, 1, 1);
    clock_t start = clock();
    for (size_t i = 0; i < 1000000; i++) {
        fieldstat_counter_incrby(instance, 0, 0, cell_id, 1);
    }
    clock_t end = clock();
    double seconds = (double)(end - start) / CLOCKS_PER_SEC;
    printf("performance_test_on_1_cells_1000000_times time cost: %f\n", seconds);
    EXPECT_TRUE(seconds < 1); // 1 = 0.000001 * 1000000
    fieldstat_free(instance);
}

TEST(metric_test_counter, performance_test_on_60000_cells_comprehensive_1000000_times)
{
    int cell_count = 60000;
    struct fieldstat_tag tags[cell_count];
    for (int i = 0; i < cell_count; i++) {
        tags[i] = TEST_TAG_INT;
        tags[i].value_longlong = i;
    }
    // getchar();
    struct fieldstat *instance = fieldstat_new();
    fieldstat_register_cube(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_COMPREHENSIVE, cell_count);
    fieldstat_register_counter(instance, 0, "test", 0);

    clock_t start = clock();
    for (size_t i = 0; i < 1000000; i++) {
        int cell_id = fieldstat_cube_add(instance, 0, &tags[i % cell_count], 1, 1);
        if (cell_id < 0) {
            continue;
        }
        fieldstat_counter_incrby(instance, 0, 0, cell_id, 1);
    }
    clock_t end = clock();
    double seconds = (double)(end - start) / CLOCKS_PER_SEC;
    printf("performance_test_on_60000_cells_comprehensive_1000000_times time cost: %f\n", seconds);
    EXPECT_TRUE(seconds < 1); // 1 = 0.000001 * 1000000
    fieldstat_free(instance);
}

TEST(metric_test_counter, performance_test_on_1000_cells_topk_1000000_times)
{
    int cell_count = 60000;
    struct fieldstat_tag tags[cell_count];
    for (int i = 0; i < cell_count; i++) {
        tags[i] = TEST_TAG_INT;
        // tags[i].value_longlong = rand() % 10000;
        if (rand()%2)
            tags[i].value_longlong = i;
        else
            tags[i].value_longlong = rand() % 1000;
    }
    struct fieldstat *instance = fieldstat_new();
    fieldstat_register_cube(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_TOPK, 1000);
    fieldstat_register_counter(instance, 0, "test", 0);

    // getchar();
    clock_t start = clock();
    for (size_t i = 0; i < 1000000; i++) {
        int cell_id = fieldstat_cube_add(instance, 0, &tags[i % cell_count], 1, 1);
        if (cell_id != -1)
            fieldstat_counter_incrby(instance, 0, 0, cell_id, 1);
    }
    clock_t end = clock();
    double seconds = (double)(end - start) / CLOCKS_PER_SEC;
    // exit(0);

    EXPECT_TRUE(seconds < 1); // 1 = 0.000001 * 1000000
    printf("performance_test_on_1000_cells_topk_1000000_times time cost: %f\n", seconds);

    fieldstat_free(instance);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}