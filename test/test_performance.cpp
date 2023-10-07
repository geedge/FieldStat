


#include <gtest/gtest.h>
#include "fieldstat.h"
#include "fieldstat_exporter.h"
#include "utils.hpp"

// /* -------------------------------------------------------------------------- */
// /*                                    merge                                   */
// /* -------------------------------------------------------------------------- */
double perform_merge_test(std::function<void (struct fieldstat*, int, int, const struct fieldstat_tag *, int)> metric_add_func, 
    std::function<int(struct fieldstat*)> metric_register_func, enum sampling_mode mode, bool merge_empty_dest)
{
    const int MAX_CELL_NUM = 1000;
    Fieldstat_tag_list_wrapper *tags[MAX_CELL_NUM];
    for (int i = 0; i < MAX_CELL_NUM; i++) {
        tags[i] = new Fieldstat_tag_list_wrapper("my key", i);
    }
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, mode, MAX_CELL_NUM);
    int metric_id = metric_register_func(instance);
    for (int j = 0; j < MAX_CELL_NUM; j++) {
        metric_add_func(instance, cube_id, metric_id, tags[j]->get_tag(), 1);
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

    return (end - start) / 1000.0;
}

TEST(test_performance, merge_performance_one_instance_comprehensive_counter_empty_dest)
{   
    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag *tags, int n_tag) {
        fieldstat_counter_incrby(instance, cube_id, metric_id, tags, n_tag, 1);
    };
    auto metric_register_func = [](struct fieldstat *instance) {
        return fieldstat_register_counter(instance, "metric name");
    };

    double elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_COMPREHENSIVE, true);
    printf("merge_performance_one_instance_comprehensive_counter_empty_dest elapsed_secs: %f\n", elapsed);
    EXPECT_TRUE(elapsed < 1000);
}

TEST(test_performance, merge_performance_one_instance_comprehensive_hll_empty_dest)
{   
    //     int metric_id = fieldstat_register_hll(instance, cube_id, "czz_test hll metric", 10);
    // int ret = fieldstat_hll_add(instance, cube_id, metric_id, cell_id, "hello", 5);

    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag *tags, int n_tag) {
        fieldstat_hll_add(instance, cube_id, metric_id, tags, n_tag, "hello", 5);
    };
    auto metric_register_func = [](struct fieldstat *instance) {
        return fieldstat_register_hll(instance, "hll metric", 6);
    };

    double elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_COMPREHENSIVE, true);
    printf("merge_performance_one_instance_comprehensive_hll_empty_dest elapsed_secs: %f\n", elapsed);
    EXPECT_TRUE(elapsed < 1.7);
}

TEST(test_performance, merge_performance_one_instance_comprehensive_histogram_empty_dest)
{   
    // int metric_id = fieldstat_register_hist(instance, cube_id, "czz_test", 1, 100000, 1);
    // int ret = fieldstat_hist_record(instance, cube_id, metric_id, cell_id, 1234);

    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag *tags, int n_tag) {
        fieldstat_hist_record(instance, cube_id, metric_id, tags, n_tag, 1234);
    };
    auto metric_register_func = [](struct fieldstat *instance) {
        return fieldstat_register_hist(instance, "histogram metric", 1, 100000, 1);
    };

    double elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_COMPREHENSIVE, true);
    printf("merge_performance_one_instance_comprehensive_histogram_empty_dest elapsed_secs: %f\n", elapsed);
    EXPECT_TRUE(elapsed < 9);
}

TEST(test_performance, merge_performance_one_instance_topk_counter_empty_dest)
{   
    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag *tags, int n_tag) {
        fieldstat_counter_incrby(instance, cube_id, metric_id, tags, n_tag, rand() % 1000);
    };
    auto metric_register_func = [](struct fieldstat *instance) {
        return fieldstat_register_counter(instance, "metric name");
    };

    double elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_TOPK, true);
    printf("merge_performance_one_instance_topk_counter_empty_dest elapsed_secs: %f\n", elapsed);
    EXPECT_TRUE(elapsed < 1);
}

TEST(test_performance, merge_performance_one_instance_comprehensive_counter_full_dest)
{   
    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag *tags, int n_tag) {
        fieldstat_counter_incrby(instance, cube_id, metric_id, tags, n_tag, 1);
    };
    auto metric_register_func = [](struct fieldstat *instance) {
        return fieldstat_register_counter(instance, "metric name");
    };

    double elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_COMPREHENSIVE, false);
    printf("merge_performance_one_instance_comprehensive_counter_full_dest elapsed_secs: %f\n", elapsed);
    EXPECT_TRUE(elapsed < 1);
}

TEST(test_performance, merge_performance_one_instance_comprehensive_hll_full_dest)
{   
    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag *tags, int n_tag) {
        fieldstat_hll_add(instance, cube_id, metric_id, tags, n_tag, "hello", 5);
    };
    auto metric_register_func = [](struct fieldstat *instance) {
        return fieldstat_register_hll(instance, "hll metric", 6);
    };

    double elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_COMPREHENSIVE, false);
    printf("merge_performance_one_instance_comprehensive_hll_full_dest elapsed_secs: %f\n", elapsed);
    EXPECT_TRUE(elapsed < 3);
}

TEST(test_performance, merge_performance_one_instance_comprehensive_histogram_full_dest)
{   
    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag *tags, int n_tag) {
        fieldstat_hist_record(instance, cube_id, metric_id, tags, n_tag, 1234);
    };
    auto metric_register_func = [](struct fieldstat *instance) {
        return fieldstat_register_hist(instance, "histogram metric", 1, 100000, 1);
    };

    double elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_COMPREHENSIVE, false);
    printf("merge_performance_one_instance_comprehensive_histogram_full_dest elapsed_secs: %f\n", elapsed);
    EXPECT_TRUE(elapsed < 9);
}

TEST(test_performance, merge_performance_one_instance_topk_counter_full_dest)
{   
    auto metric_add_func = [](struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag *tags, int n_tag) {
        fieldstat_counter_incrby(instance, cube_id, metric_id, tags, n_tag, rand() % 1000);
    };
    auto metric_register_func = [](struct fieldstat *instance) {
        return fieldstat_register_counter(instance, "metric name");
    };

    double elapsed = perform_merge_test(metric_add_func, metric_register_func, SAMPLING_MODE_TOPK, false);
    printf("merge_performance_one_instance_topk_counter_full_dest elapsed_secs: %f\n", elapsed);
    EXPECT_TRUE(elapsed < 2);
}

/* -------------------------------------------------------------------------- */
/*                                     add                                    */
/* -------------------------------------------------------------------------- */
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
    fieldstat_create_cube(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_COMPREHENSIVE, cell_count);
    fieldstat_register_counter(instance, "test");

    clock_t start = clock();
    for (size_t i = 0; i < cell_count; i++) {
        fieldstat_counter_incrby(instance, 0, 0, &tags[i % cell_count], 1, 1);
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
    fieldstat_create_cube(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_TOPK, 1000);
    fieldstat_register_counter(instance, "test");

    // getchar();
    clock_t start = clock();
    for (size_t i = 0; i < cell_count; i++) {
        fieldstat_counter_incrby(instance, 0, 0, &tags[i % cell_count], 1, 1);
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
    fieldstat_create_cube(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_hist(instance, "test", 1, 100000, 3);
    size_t test_num = 100000;
    long long vals[test_num];
    for (size_t i = 0; i < test_num; i++) {
        vals[i] = rand() % 100000 + 1;
    }
    clock_t start = clock();
    for (size_t i = 0; i < test_num; i++) {
        fieldstat_hist_record(instance, 0, 0, &TEST_TAG_INT, 1, vals[i]);
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
    fieldstat_create_cube(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    fieldstat_register_hll(instance, "test", 6);
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

    Fieldstat_tag_list_wrapper *tags[TAG_NUM];
    for (int i = 0; i < TAG_NUM; i++) {
        tags[i] = new Fieldstat_tag_list_wrapper("my key", i);
    }

    struct fieldstat *instance = fieldstat_new();
    for (int i = 0; i < CUBE_NUM; i++) {
        Fieldstat_tag_list_wrapper cube_tag("shared key", i);
        int cube_id = fieldstat_create_cube(instance, cube_tag.get_tag(), cube_tag.get_tag_count(), SAMPLING_MODE_COMPREHENSIVE, MAX_CELL_NUM);

        for (int j = 0; j < METRIC_NUM; j++) {
            string metric_name = "metric name" + to_string(i) + to_string(j);
            int metric_id = fieldstat_register_counter(instance, metric_name.c_str());

            for (int k = 0; k < MAX_CELL_NUM; k++) {
                fieldstat_counter_incrby(instance, cube_id, metric_id, tags[rand() % TAG_NUM]->get_tag(), 1, 1);
            }
        }
    }

    struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new(instance);
    // getchar();
    clock_t start = clock();
    char *json_string = fieldstat_json_exporter_export(fieldstat_json_exporter, &TEST_TIMEVAL);
    clock_t end = clock();
    // exit(0);
    free(json_string);
    fieldstat_json_exporter_free(fieldstat_json_exporter);

    printf("export_many_cells us: %ld\n", end - start);
    fieldstat_free(instance);

    for (int i = 0; i < TAG_NUM; i++) {
        delete tags[i];
    }
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}