


#include <gtest/gtest.h>
#include "fieldstat.h"
#include "fieldstat_exporter.h"
#include "utils.hpp"


int test_fieldstat_cube_create(struct fieldstat *instance, const struct field *tag, size_t tag_count, enum sampling_mode mode, int k, int primary_metric_id=0)
{
    int ret = fieldstat_cube_create(instance, tag, tag_count);
    fieldstat_cube_set_sampling(instance, ret, mode, k, primary_metric_id);
    return ret;
}


// /* -------------------------------------------------------------------------- */
// /*                                    merge                                   */
// /* -------------------------------------------------------------------------- */
double perform_merge_test(std::function<void (struct fieldstat*, int, int, const struct field *, int)> metric_add_func, 
    std::function<int(struct fieldstat*)> metric_register_func, enum sampling_mode mode, bool merge_empty_dest)
{
    const int MAX_CELL_NUM = 1000;
    Fieldstat_tag_list_wrapper *fields[MAX_CELL_NUM];
    for (int i = 0; i < MAX_CELL_NUM; i++) {
        fields[i] = new Fieldstat_tag_list_wrapper("my key", i);
    }
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int metric_id = metric_register_func(instance);
    fieldstat_cube_set_sampling(instance, cube_id, mode, MAX_CELL_NUM, metric_id);
    for (int j = 0; j < MAX_CELL_NUM; j++) {
        metric_add_func(instance, cube_id, metric_id, fields[j]->get_tag(), 1);
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
        delete fields[i];
    }

    return (end - start) / 1000.0;
}

TEST(test_performance, merge_performance_one_instance_comprehensive_counter_empty_dest)
{   
    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, const struct field *fields, int n_field) {
        fieldstat_counter_incrby(instance, cube_id, metric_id, fields, n_field, 1);
    };
    auto metric_register_func = [](struct fieldstat *instance) {
        return fieldstat_register_counter(instance, 0, "metric name");
    };

    double elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_COMPREHENSIVE, true);
    printf("merge_performance_one_instance_comprehensive_counter_empty_dest elapsed_secs: %f\n", elapsed);
    EXPECT_TRUE(elapsed < 1000);
}

TEST(test_performance, merge_performance_one_instance_comprehensive_hll_empty_dest)
{   
    //     int metric_id = fieldstat_register_hll(instance, cube_id, "czz_test hll metric", 10);
    // int ret = fieldstat_hll_add(instance, cube_id, metric_id, cell_id, "hello", 5);

    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, const struct field *fields, int n_field) {
        fieldstat_hll_add(instance, cube_id, metric_id, fields, n_field, "hello", 5);
    };
    auto metric_register_func = [](struct fieldstat *instance) {
        return fieldstat_register_hll(instance, 0, "hll metric", 6);
    };

    double elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_COMPREHENSIVE, true);
    printf("merge_performance_one_instance_comprehensive_hll_empty_dest elapsed_secs: %f\n", elapsed);
    EXPECT_TRUE(elapsed < 1.7);
}

TEST(test_performance, merge_performance_one_instance_comprehensive_histogram_empty_dest)
{   
    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, const struct field *fields, int n_field) {
        fieldstat_histogram_record(instance, cube_id, metric_id, fields, n_field, 1234);
    };
    auto metric_register_func = [](struct fieldstat *instance) {
        return fieldstat_register_histogram(instance, 0, "histogram metric", 1, 100000, 1);
    };

    double elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_COMPREHENSIVE, true);
    printf("merge_performance_one_instance_comprehensive_histogram_empty_dest elapsed_secs: %f\n", elapsed);
    EXPECT_TRUE(elapsed < 9);
}

TEST(test_performance, merge_performance_one_instance_topk_counter_empty_dest)
{   
    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, const struct field *fields, int n_field) {
        fieldstat_counter_incrby(instance, cube_id, metric_id, fields, n_field, rand() % 1000);
    };
    auto metric_register_func = [](struct fieldstat *instance) {
        return fieldstat_register_counter(instance, 0, "metric name");
    };

    double elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_TOPK, true);
    printf("merge_performance_one_instance_topk_counter_empty_dest elapsed_secs: %f\n", elapsed);
    EXPECT_TRUE(elapsed < 1);
}

TEST(test_performance, merge_performance_one_instance_comprehensive_counter_full_dest)
{   
    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, const struct field *fields, int n_field) {
        fieldstat_counter_incrby(instance, cube_id, metric_id, fields, n_field, 1);
    };
    auto metric_register_func = [](struct fieldstat *instance) {
        return fieldstat_register_counter(instance, 0, "metric name");
    };

    double elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_COMPREHENSIVE, false);
    printf("merge_performance_one_instance_comprehensive_counter_full_dest elapsed_secs: %f\n", elapsed);
    EXPECT_TRUE(elapsed < 1);
}

TEST(test_performance, merge_performance_one_instance_comprehensive_hll_full_dest)
{   
    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, const struct field *fields, int n_field) {
        fieldstat_hll_add(instance, cube_id, metric_id, fields, n_field, "hello", 5);
    };
    auto metric_register_func = [](struct fieldstat *instance) {
        return fieldstat_register_hll(instance, 0, "hll metric", 6);
    };

    double elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_COMPREHENSIVE, false);
    printf("merge_performance_one_instance_comprehensive_hll_full_dest elapsed_secs: %f\n", elapsed);
    EXPECT_TRUE(elapsed < 3);
}

TEST(test_performance, merge_performance_one_instance_comprehensive_histogram_full_dest)
{   
    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, const struct field *fields, int n_field) {
        fieldstat_histogram_record(instance, cube_id, metric_id, fields, n_field, 1234);
    };
    auto metric_register_func = [](struct fieldstat *instance) {
        return fieldstat_register_histogram(instance, 0, "histogram metric", 1, 100000, 1);
    };

    double elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_COMPREHENSIVE, false);
    printf("merge_performance_one_instance_comprehensive_histogram_full_dest elapsed_secs: %f\n", elapsed);
    EXPECT_TRUE(elapsed < 9);
}

TEST(test_performance, merge_performance_one_instance_topk_counter_full_dest)
{   
    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, const struct field *fields, int n_field) {
        fieldstat_counter_incrby(instance, cube_id, metric_id, fields, n_field, rand() % 1000);
    };
    auto metric_register_func = [](struct fieldstat *instance) {
        return fieldstat_register_counter(instance,0, "metric name");
    };

    double elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_TOPK, false);
    printf("merge_performance_one_instance_topk_counter_full_dest elapsed_secs: %f\n", elapsed);
    EXPECT_TRUE(elapsed < 2);
}

struct fieldstat *construct_fs_with_many_empty_cubes(int cube_num, int metric_num, enum sampling_mode mode)
{
    struct fieldstat *instance = fieldstat_new();
    struct field tmp_tag = TEST_TAG_INT;
    for (int i = 0; i < cube_num; i++) {
        tmp_tag.value_longlong = i;
        int cube_id = fieldstat_cube_create(instance, &tmp_tag, 1);

        for (int j = 0; j < metric_num; j++) {
            fieldstat_register_counter(instance, cube_id, std::to_string(j).c_str());
        }

        fieldstat_cube_set_sampling(instance, cube_id, mode, 1000, 0);
    }
    return instance;
}

TEST(test_performance, merge_empty_cubes_comprehensive)
{
    struct fieldstat *instance = construct_fs_with_many_empty_cubes(1000, 10, SAMPLING_MODE_COMPREHENSIVE); // many empty cubes, 10 metrics is a common case
    
    struct fieldstat *instance_dest = fieldstat_fork(instance);
    clock_t start = clock();
    fieldstat_merge(instance_dest, instance);
    clock_t end = clock();

    double seconds = (double)(end - start) / 1000;
    printf("merge_empty_cubes_comprehensive time cost: %f s\n", seconds);
    EXPECT_TRUE(seconds < 1);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
}

TEST(test_performance, merge_empty_cubes_topk)
{
    struct fieldstat *instance = construct_fs_with_many_empty_cubes(1000, 10, SAMPLING_MODE_TOPK); // many empty cubes, 10 metrics is a common case
    
    struct fieldstat *instance_dest = fieldstat_fork(instance);
    clock_t start = clock();
    fieldstat_merge(instance_dest, instance);
    clock_t end = clock();

    double seconds = (double)(end - start) / 1000;
    printf("merge_empty_cubes_topk time cost: %f s\n", seconds);
    EXPECT_TRUE(seconds < 1);

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
}

/* -------------------------------------------------------------------------- */
/*                                     add                                    */
/* -------------------------------------------------------------------------- */
TEST(test_performance, performance_test_add_cells_comprehensive)
{
    size_t cell_count = 100000;
    struct field fields[cell_count];
    for (size_t i = 0; i < cell_count; i++) {
        fields[i] = TEST_TAG_INT;
        fields[i].value_longlong = i;
    }
    // getchar();
    struct fieldstat *instance = fieldstat_new();
    test_fieldstat_cube_create(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_COMPREHENSIVE, cell_count);
    fieldstat_register_counter(instance, 0, "test");

    clock_t start = clock();
    for (size_t i = 0; i < cell_count; i++) {
        fieldstat_counter_incrby(instance, 0, 0, &fields[i % cell_count], 1, 1);
    }
    clock_t end = clock();
    double seconds = (double)(end - start) / cell_count;
    printf("performance_test_add_cells_comprehensive time cost: %f\n", seconds);
    EXPECT_TRUE(seconds < 1.3);
    fieldstat_free(instance);
}

TEST(test_performance, performance_test_add_cells_topk)
{
    size_t cell_count = 100000;
    struct field fields[cell_count];
    for (size_t i = 0; i < cell_count; i++) {
        fields[i] = TEST_TAG_INT;
        // fields[i].value_longlong = rand() % 10000;
        if (rand()%2)
            fields[i].value_longlong = i;
        else
            fields[i].value_longlong = rand() % 1000;
    }
    struct fieldstat *instance = fieldstat_new();
    fieldstat_cube_create(instance, &TEST_TAG_INT_collided, 1);
    fieldstat_register_counter(instance, 0, "test");
    fieldstat_cube_set_sampling(instance, 0, SAMPLING_MODE_TOPK, 1000, 0);

    // getchar();
    clock_t start = clock();
    for (size_t i = 0; i < cell_count; i++) {
        fieldstat_counter_incrby(instance, 0, 0, &fields[i % cell_count], 1, 1);
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
    test_fieldstat_cube_create(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_histogram(instance, 0, "test", 1, 100000, 3);
    size_t test_num = 100000;
    long long vals[test_num];
    for (size_t i = 0; i < test_num; i++) {
        vals[i] = rand() % 100000 + 1;
    }
    clock_t start = clock();
    for (size_t i = 0; i < test_num; i++) {
        fieldstat_histogram_record(instance, 0, 0, &TEST_TAG_INT, 1, vals[i]);
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
    test_fieldstat_cube_create(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_hll(instance, 0, "test", 6);
    size_t test_num = 100000;
    std::string vals[test_num];
    for (size_t i = 0; i < test_num; i++) {
        vals[i] = std::to_string(rand() % 1000000 + 1);
    }

    clock_t start = clock();
    for (size_t i = 0; i < test_num; i++) {
        fieldstat_hll_add(instance, 0, 0, &TEST_TAG_INT, 1, vals[i].c_str(), vals[i].length());
    }
    clock_t end = clock();
    double seconds = (double)(end - start) / test_num;
    printf("performance_test_add_cells_hll_add time cost: %f\n", seconds);
    EXPECT_TRUE(seconds < 1);
    fieldstat_free(instance);
}

TEST(test_performance, performance_test_add_cells_comprehensive_5_tags)
{
    size_t cell_count = 100000;
    struct field *tag_v[cell_count];
    for (size_t i = 0; i < cell_count; i++) {
        struct field *fields = (struct field *)malloc(sizeof(struct field) * 5);

        fields[0] = TEST_TAG_INT;
        fields[1] = TEST_TAG_INT;
        fields[2] = TEST_TAG_INT;
        fields[3] = TEST_TAG_INT;
        fields[4] = TEST_TAG_INT;
        fields[0].value_longlong = i;

        tag_v[i] = fields;
    }
    // getchar();
    struct fieldstat *instance = fieldstat_new();
    test_fieldstat_cube_create(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_COMPREHENSIVE, cell_count);
    fieldstat_register_counter(instance, 0, "test");

    clock_t start = clock();
    for (size_t i = 0; i < cell_count; i++) {
        fieldstat_counter_incrby(instance, 0, 0, tag_v[i % cell_count], 5, 1);
    }
    clock_t end = clock();
    double seconds = (double)(end - start) / cell_count;
    printf("performance_test_add_cells_comprehensive time 5 fields cost: %f\n", seconds);
    EXPECT_TRUE(seconds < 2);
    fieldstat_free(instance);

    for (size_t i = 0; i < cell_count; i++) {
        free(tag_v[i]);
    }
}

TEST(test_performance, performance_test_add_cells_histogram_record_5tags)
{
    struct fieldstat *instance = fieldstat_new();
    test_fieldstat_cube_create(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_histogram(instance, 0, "test", 1, 100000, 3);
    size_t test_num = 100000;
    long long vals[test_num];
    for (size_t i = 0; i < test_num; i++) {
        vals[i] = rand() % 100000 + 1;
    }

    struct field fields[5];
    fields[0] = TEST_TAG_INT;
    fields[1] = TEST_TAG_STRING;
    fields[2] = TEST_TAG_DOUBLE;
    fields[3] = TEST_TAG_INT;
    fields[4] = TEST_TAG_INT;
    clock_t start = clock();
    for (size_t i = 0; i < test_num; i++) {
        fieldstat_histogram_record(instance, 0, 0, fields, 5, vals[i]);
    }
    clock_t end = clock();
    double seconds = (double)(end - start) / test_num;
    printf("performance_test_add_cells_histogram_record time 5 fields cost: %f\n", seconds);
    EXPECT_TRUE(seconds < 1);
    fieldstat_free(instance);
}

TEST(test_performance, performance_test_add_cells_hll_add_5tags)
{
    struct fieldstat *instance = fieldstat_new();
    test_fieldstat_cube_create(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_hll(instance, 0, "test", 6);
    size_t test_num = 100000;
    std::string vals[test_num];
    for (size_t i = 0; i < test_num; i++) {
        vals[i] = std::to_string(rand() % 1000000 + 1);
    }

    struct field fields[5];
    fields[0] = TEST_TAG_INT;
    fields[1] = TEST_TAG_STRING;
    fields[2] = TEST_TAG_DOUBLE;
    fields[3] = TEST_TAG_INT;
    fields[4] = TEST_TAG_INT;
    clock_t start = clock();
    for (size_t i = 0; i < test_num; i++) {
        fieldstat_hll_add(instance, 0, 0, fields, 5, vals[i].c_str(), vals[i].length());
    }
    clock_t end = clock();
    double seconds = (double)(end - start) / test_num;
    printf("performance_test_add_cells_hll_add time 5 fields cost: %f\n", seconds);
    EXPECT_TRUE(seconds < 1);
    fieldstat_free(instance);
}

/* -------------------------------------------------------------------------- */
/*                                   export                                   */
/* -------------------------------------------------------------------------- */
using namespace std;

TEST(test_performance, export_many_cells)
{
    const int MAX_CELL_NUM = 1000;
    const int TAG_NUM = 3000;
    const int CUBE_NUM = 10;
    const int METRIC_NUM = 10;

    Fieldstat_tag_list_wrapper *fields[TAG_NUM];
    for (int i = 0; i < TAG_NUM; i++) {
        fields[i] = new Fieldstat_tag_list_wrapper("my key", i);
    }

    struct fieldstat *instance = fieldstat_new();
    for (int i = 0; i < CUBE_NUM; i++) {
        Fieldstat_tag_list_wrapper cube_tag("shared key", i);
        int cube_id = test_fieldstat_cube_create(instance, cube_tag.get_tag(), cube_tag.get_tag_count(), SAMPLING_MODE_COMPREHENSIVE, MAX_CELL_NUM);

        for (int j = 0; j < METRIC_NUM; j++) {
            string metric_name = "metric name" + to_string(i) + to_string(j);
            int metric_id = fieldstat_register_counter(instance, cube_id, metric_name.c_str());

            for (int k = 0; k < MAX_CELL_NUM; k++) {
                fieldstat_counter_incrby(instance, cube_id, metric_id, fields[rand() % TAG_NUM]->get_tag(), 1, 1);
            }
        }
    }

    struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new();
    // getchar();
    clock_t start = clock();
    char *json_string = fieldstat_json_exporter_export(fieldstat_json_exporter, instance, &TEST_TIMEVAL);
    clock_t end = clock();
    // exit(0);
    free(json_string);
    fieldstat_json_exporter_free(fieldstat_json_exporter);

    printf("export_many_cells us: %ld\n", end - start);
    fieldstat_free(instance);

    for (int i = 0; i < TAG_NUM; i++) {
        delete fields[i];
    }
}

/* -------------------------------------------------------------------------- */
/*                                reset related                                */
/* -------------------------------------------------------------------------- */
TEST(test_performance, reset_empty_cubes_comprehensive)
{
    struct fieldstat *instance = construct_fs_with_many_empty_cubes(1000, 10, SAMPLING_MODE_COMPREHENSIVE); // many empty cubes, 10 metrics is a common case
    clock_t start = clock();
    fieldstat_reset(instance);
    clock_t end = clock();

    double seconds = (double)(end - start) / 1000;
    printf("reset_empty_cubes_comprehensive time cost: %f s\n", seconds);
    EXPECT_TRUE(seconds < 1);
    fieldstat_free(instance);
}

TEST(test_performance, reset_empty_cubes_topk)
{
    struct fieldstat *instance = construct_fs_with_many_empty_cubes(1000, 10, SAMPLING_MODE_TOPK); // many empty cubes, 10 metrics is a common case
    clock_t start = clock();
    fieldstat_reset(instance);
    clock_t end = clock();

    double seconds = (double)(end - start) / 1000;
    printf("reset_empty_cubes_topk time cost: %f s\n", seconds);
    EXPECT_TRUE(seconds < 1);
    fieldstat_free(instance);
}

TEST(test_performance, callibrate_unchanged)
{
    struct fieldstat *instance = construct_fs_with_many_empty_cubes(1000, 10, SAMPLING_MODE_COMPREHENSIVE); // many empty cubes, 10 metrics is a common case

    struct fieldstat *instance_dest = fieldstat_fork(instance);
    clock_t start = clock();
    fieldstat_calibrate(instance_dest, instance);
    clock_t end = clock();

    double seconds = (double)(end - start) / 1000;
    printf("callibrate_unchanged time cost: %f s\n", seconds);
    EXPECT_TRUE(seconds < 1);
    fieldstat_free(instance);
    fieldstat_free(instance_dest);
}

struct fieldstat *construct_fs_with_many_cells(int cell_num)
{
    struct fieldstat *instance = fieldstat_new();
    test_fieldstat_cube_create(instance, &TEST_TAG_INT, 1, SAMPLING_MODE_COMPREHENSIVE, cell_num);
    fieldstat_register_counter(instance, 0, "test");
    struct field tmp_tag = TEST_TAG_INT;
    for (int i = 0; i < cell_num; i++) {
        tmp_tag.value_longlong = i;
        fieldstat_counter_incrby(instance, 0, 0, &tmp_tag, 1, 1);
    }

    return instance;
}

TEST(test_performance, reset_many_cells)
{
    struct fieldstat *instance = construct_fs_with_many_cells(1000); // many empty cubes, 10 metrics is a common case
    clock_t start = clock();
    fieldstat_reset(instance);
    clock_t end = clock();

    double seconds = (double)(end - start) / 1000;
    printf("reset_many_cells time cost: %f s\n", seconds);
    EXPECT_TRUE(seconds < 1);
    fieldstat_free(instance);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}