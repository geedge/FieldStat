


#include <gtest/gtest.h>
#include "fieldstat.h"
#include "utils.hpp"


TEST(test_performance, merge_performance_when_comprehensive_sampling_multi_instance)
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
    printf("merge_performance_when_comprehensive_sampling_multi_instance elapsed_secs: %f\n", elapsed_secs);
    EXPECT_TRUE(elapsed_secs < 0.1);

    fieldstat_free(instance_dest);
    for (int i = 0; i < INSTANCE_NUM; i++) {
        fieldstat_free(instances[i]);
    }
    for (int i = 0; i < DIMENSION_TOTAL; i++) {
        delete tags[i];
    }
}

clock_t perform_merge_test(std::function<void (struct fieldstat*, int, int, int)> metric_add_func, 
    std::function<int(struct fieldstat*, int)> metric_register_func, enum sampling_mode mode, bool merge_empty_dest)
{
    const int MAX_CELL_NUM = 1000;
    Fieldstat_tag_list_wrapper *tags[MAX_CELL_NUM];
    for (int i = 0; i < MAX_CELL_NUM; i++) {
        tags[i] = new Fieldstat_tag_list_wrapper("my key", i);
    }
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, mode, MAX_CELL_NUM);
    int metric_id = metric_register_func(instance, cube_id);
    for (int j = 0; j < MAX_CELL_NUM; j++) {
        int cell_id = fieldstat_cube_add(instance, cube_id, tags[j]->get_tag(), 1, 1);
        metric_add_func(instance, cube_id, metric_id, cell_id);
    }

    struct fieldstat *instance_dest = fieldstat_new();
    if (!merge_empty_dest) {
        fieldstat_merge(instance_dest, instance);
    }

    clock_t start = clock();
    fieldstat_merge(instance_dest, instance);
    clock_t end = clock();

    fieldstat_free(instance_dest);
    fieldstat_free(instance);
    for (int i = 0; i < MAX_CELL_NUM; i++) {
        delete tags[i];
    }

    return end - start;
}

TEST(test_performance, merge_performance_one_instance_comprehensive_counter_empty_dest)
{   
    // int metric_id = fieldstat_register_counter(tmp_i, cube_id, "metric name", false);
    // fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 1);
    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, int cell_id) {
        fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 1);
    };
    auto metric_register_func = [](struct fieldstat *instance, int cube_id) {
        return fieldstat_register_counter(instance, cube_id, "metric name", false);
    };

    clock_t elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_COMPREHENSIVE, true);
    printf("merge_performance_one_instance_comprehensive_counter_empty_dest elapsed_secs: %ld\n", elapsed);
}

TEST(test_performance, merge_performance_one_instance_comprehensive_hll_empty_dest)
{   
    //     int metric_id = fieldstat_register_hll(instance, cube_id, "czz_test hll metric", 10);
    // int ret = fieldstat_hll_add(instance, cube_id, metric_id, cell_id, "hello", 5);

    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, int cell_id) {
        fieldstat_hll_add(instance, cube_id, metric_id, cell_id, "hello", 5);
    };
    auto metric_register_func = [](struct fieldstat *instance, int cube_id) {
        return fieldstat_register_hll(instance, cube_id, "hll metric", 6);
    };

    clock_t elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_COMPREHENSIVE, true);
    printf("merge_performance_one_instance_comprehensive_counter_empty_dest elapsed_secs: %ld\n", elapsed);
}

TEST(test_performance, merge_performance_one_instance_comprehensive_histogram_empty_dest)
{   
    // int metric_id = fieldstat_register_histogram(instance, cube_id, "czz_test", 1, 100000, 1);
    // int ret = fieldstat_histogram_record(instance, cube_id, metric_id, cell_id, 1234);

    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, int cell_id) {
        fieldstat_histogram_record(instance, cube_id, metric_id, cell_id, 1234);
    };
    auto metric_register_func = [](struct fieldstat *instance, int cube_id) {
        return fieldstat_register_histogram(instance, cube_id, "histogram metric", 1, 100000, 1);
    };

    clock_t elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_COMPREHENSIVE, true);
    printf("merge_performance_one_instance_comprehensive_counter_empty_dest elapsed_secs: %ld\n", elapsed);
}

TEST(test_performance, merge_performance_one_instance_topk_counter_empty_dest)
{   
    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, int cell_id) {
        fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, rand() % 1000);
    };
    auto metric_register_func = [](struct fieldstat *instance, int cube_id) {
        return fieldstat_register_counter(instance, cube_id, "metric name", false);
    };

    clock_t elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_TOPK, true);
    printf("merge_performance_one_instance_comprehensive_counter_empty_dest elapsed_secs: %ld\n", elapsed);
}

TEST(test_performance, merge_performance_one_instance_comprehensive_counter_full_dest)
{   
    // int metric_id = fieldstat_register_counter(tmp_i, cube_id, "metric name", false);
    // fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 1);
    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, int cell_id) {
        fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 1);
    };
    auto metric_register_func = [](struct fieldstat *instance, int cube_id) {
        return fieldstat_register_counter(instance, cube_id, "metric name", false);
    };

    clock_t elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_COMPREHENSIVE, false);
    printf("merge_performance_one_instance_comprehensive_counter_empty_dest elapsed_secs: %ld\n", elapsed);
}

TEST(test_performance, merge_performance_one_instance_comprehensive_hll_full_dest)
{   
    //     int metric_id = fieldstat_register_hll(instance, cube_id, "czz_test hll metric", 10);
    // int ret = fieldstat_hll_add(instance, cube_id, metric_id, cell_id, "hello", 5);

    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, int cell_id) {
        fieldstat_hll_add(instance, cube_id, metric_id, cell_id, "hello", 5);
    };
    auto metric_register_func = [](struct fieldstat *instance, int cube_id) {
        return fieldstat_register_hll(instance, cube_id, "hll metric", 6);
    };

    clock_t elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_COMPREHENSIVE, false);
    printf("merge_performance_one_instance_comprehensive_counter_empty_dest elapsed_secs: %ld\n", elapsed);
}

TEST(test_performance, merge_performance_one_instance_comprehensive_histogram_full_dest)
{   
    // int metric_id = fieldstat_register_histogram(instance, cube_id, "czz_test", 1, 600000, 3);
    // int ret = fieldstat_histogram_record(instance, cube_id, metric_id, cell_id, 1234);

    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, int cell_id) {
        fieldstat_histogram_record(instance, cube_id, metric_id, cell_id, 1234);
    };
    auto metric_register_func = [](struct fieldstat *instance, int cube_id) {
        return fieldstat_register_histogram(instance, cube_id, "histogram metric", 1, 100000, 1);
    };

    clock_t elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_COMPREHENSIVE, false);
    printf("merge_performance_one_instance_comprehensive_counter_empty_dest elapsed_secs: %ld\n", elapsed);
}

TEST(test_performance, merge_performance_one_instance_topk_counter_full_dest)
{   
    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, int cell_id) {
        fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, rand() % 1000);
    };
    auto metric_register_func = [](struct fieldstat *instance, int cube_id) {
        return fieldstat_register_counter(instance, cube_id, "metric name", false);
    };

    clock_t elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_TOPK, false);
    printf("merge_performance_one_instance_comprehensive_counter_empty_dest elapsed_secs: %ld\n", elapsed);
}


TEST(test_performance, performance_test_add_cells_comprehensive)
{
    size_t cell_count = 100000;
    struct fieldstat_tag tags[cell_count];
    for (size_t i = 0; i < cell_count; i++) {
        tags[i] = TEST_TAG_INT;
        tags[i].value_longlong = i;
    }
    // getchar();
    struct fieldstat *instance = fieldstat_new();
    fieldstat_register_cube(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_COMPREHENSIVE, cell_count);
    fieldstat_register_counter(instance, 0, "test", 0);

    clock_t start = clock();
    for (size_t i = 0; i < cell_count; i++) {
        fieldstat_cube_add(instance, 0, &tags[i % cell_count], 1, 1);
    }
    clock_t end = clock();
    double seconds = (double)(end - start) / cell_count;
    printf("performance_test_add_cells_comprehensive time cost: %f\n", seconds);
    EXPECT_TRUE(seconds < 1);
    fieldstat_free(instance);
}

TEST(test_performance, performance_test_add_cells_topk)
{
    size_t cell_count = 100000;
    struct fieldstat_tag tags[cell_count];
    for (size_t i = 0; i < cell_count; i++) {
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
    for (size_t i = 0; i < cell_count; i++) {
        fieldstat_cube_add(instance, 0, &tags[i % cell_count], 1, 1);
    }
    clock_t end = clock();
    double seconds = (double)(end - start) / cell_count;
    // exit(0);

    EXPECT_TRUE(seconds < 1);
    printf("performance_test_on_1000_cells_topk_1000000_times time cost: %f\n", seconds);

    fieldstat_free(instance);
}

TEST(test_performance, performance_test_add_cells_histogram_record)
{
    struct fieldstat *instance = fieldstat_new();
    fieldstat_register_cube(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_histogram(instance, 0, "test", 1, 1000000, 3);
    int cell_id = fieldstat_cube_add(instance, 0, &TEST_TAG_DOUBLE, 1, 1);
    size_t test_num = 100000;
    long long vals[test_num];
    for (size_t i = 0; i < test_num; i++) {
        vals[i] = rand() % 1000000 + 1;
    }

    clock_t start = clock();
    for (size_t i = 0; i < test_num; i++) {
        fieldstat_histogram_record(instance, 0, 0, cell_id, vals[i]);
    }
    clock_t end = clock();
    double seconds = (double)(end - start) / test_num;
    printf("performance_test_add_cells_histogram_record time cost: %f\n", seconds);
    EXPECT_TRUE(seconds < 1);
    fieldstat_free(instance);
}

TEST(test_performance, performance_test_add_cells_hll_add)
{
    struct fieldstat *instance = fieldstat_new();
    fieldstat_register_cube(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_hll(instance, 0, "test", 6);
    int cell_id = fieldstat_cube_add(instance, 0, &TEST_TAG_DOUBLE, 1, 1);
    size_t test_num = 100000;
    std::string vals[test_num];
    for (size_t i = 0; i < test_num; i++) {
        vals[i] = std::to_string(rand() % 1000000 + 1);
    }

    clock_t start = clock();
    for (size_t i = 0; i < test_num; i++) {
        fieldstat_hll_add(instance, 0, 0, cell_id, vals[i].c_str(), vals[i].length());
    }
    clock_t end = clock();
    double seconds = (double)(end - start) / test_num;
    printf("performance_test_add_cells_hll_add time cost: %f\n", seconds);
    EXPECT_TRUE(seconds < 1);
    fieldstat_free(instance);
}


int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}