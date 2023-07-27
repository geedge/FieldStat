
#include <gtest/gtest.h>
#include <random>
#include "cjson/cJSON.h"
#include "base64/b64.h"


#include "fieldstat_exporter.h"
#include "fieldstat.h"
#include "utils.hpp"
#include "hdr/hdr_histogram.h"
#include "hdr/hdr_histogram_log.h"
#include "st_hyperloglog.h"

const size_t OPER_NUM = 10000;
std::string g_hll_standard_oper[OPER_NUM];
long long g_histogram_standard_oper[OPER_NUM];
struct ST_hyperloglog *g_hll_standard;
struct hdr_histogram *g_histogram_standard;
#define TEST_TOPK_STANDARD_K 5
#define TEST_METRIC_NUM 2
const struct fieldstat_tag TEST_TAG_GLOBAL[3] = {
    {"test_tag_global 1", .type = TAG_INTEGER, .value_longlong = 1},
    {"test_tag_global 2", .type = TAG_DOUBLE, .value_double = 2.2},
    {"test_tag_global 3", .type = TAG_CSTRING, .value_str = "string3"}};

const struct fieldstat_tag TEST_TAG_SHARED1[3] = {
    {"test_tag_shared 1", .type = TAG_INTEGER, .value_longlong = 3},
    {"test_tag_shared 2", .type = TAG_DOUBLE, .value_double = 0.2},
    {"test_tag_shared 3", .type = TAG_CSTRING, .value_str = "1string"}};
const struct fieldstat_tag TEST_TAG_SHARED2[3] = {
    {"test_tag_shared 11", .type = TAG_INTEGER, .value_longlong = 4},
    {"test_tag_shared 22", .type = TAG_DOUBLE, .value_double = 0.3},
    {"test_tag_shared 33", .type = TAG_CSTRING, .value_str = "2string"}};
const struct fieldstat_tag TEST_TAG_SHARED3[1] = {
    {"test_tag_shared 3", .type = TAG_INTEGER, .value_longlong = 5}};

void test_check_if_tag_list_is_in_json(cJSON *tag_obj, const Fieldstat_tag_list_wrapper *benchmark)
{
    for (size_t tag_id = 0; tag_id < benchmark->get_tag_count(); tag_id++) {
        cJSON *tag_val = cJSON_GetObjectItem(tag_obj, benchmark->get_tag()[tag_id].key);
        EXPECT_NE(tag_val, nullptr);
        if (tag_val->type == cJSON_String) {
            EXPECT_STREQ(tag_val->valuestring, benchmark->get_tag()[tag_id].value_str);
        } else {
            if (benchmark->get_tag()[tag_id].type == TAG_INTEGER) {
                EXPECT_EQ(tag_val->valueint, benchmark->get_tag()[tag_id].value_longlong);
            } else {
                EXPECT_NEAR(tag_val->valuedouble, benchmark->get_tag()[tag_id].value_double, 0.0001);
            }
        }
    }
}

void test_check_if_global_tag_is_in_json(cJSON *tag_obj)
{
    struct fieldstat_tag_list tag_list = {.tag = (struct fieldstat_tag *)TEST_TAG_GLOBAL, .n_tag = 3};
    const Fieldstat_tag_list_wrapper benchmark(&tag_list);
    test_check_if_tag_list_is_in_json(tag_obj, &benchmark);
}

void test_check_if_metric_histogram_correct(cJSON *metric_obj, const char *name)
{
    char *blob_histogram_benchmark = NULL;
    hdr_log_encode(g_histogram_standard, &blob_histogram_benchmark);

    cJSON *histogram_obj = cJSON_GetObjectItem(metric_obj, name);
    EXPECT_NE(histogram_obj, nullptr);
    EXPECT_STREQ(histogram_obj->valuestring, blob_histogram_benchmark);

    free(blob_histogram_benchmark);
    // no need to free histogram_obj
}

void test_check_if_metric_gauge_correct(cJSON *metric_obj, const char *name)
{
    char *blob_gauge_benchmark = NULL;
    size_t size_dummy = 0;
    ST_hyperloglog_serialize_for_networking(g_hll_standard, &blob_gauge_benchmark, &size_dummy);

    cJSON *gauge_obj = cJSON_GetObjectItem(metric_obj, name);
    EXPECT_NE(gauge_obj, nullptr);
    EXPECT_STREQ(gauge_obj->valuestring, blob_gauge_benchmark);

    free(blob_gauge_benchmark);
    // no need to free gauge_obj
}

void test_check_if_metric_topk_correct(cJSON *metric_obj, const char **name, unsigned int expected_value, size_t metric_count)
{
    for (size_t i = 0; i < metric_count; i ++) {
        cJSON *topk = cJSON_GetObjectItem(metric_obj, name[i]);

        EXPECT_NE(topk, nullptr);
        EXPECT_NEAR(topk->valueint, expected_value * (i + 1), expected_value * (i + 1) * 0.002); // times (i + 1) :
            // because topk_add is called like this: topk add 1.2.3.4 "test_topk" expected_value "metric_2" 2*expected_value ..
    }
}

void fill_random_tag(Fieldstat_tag_list_wrapper *tags[], int tag_list_num)
{
    std::uniform_int_distribution<int> dist(1,100);
    std::mt19937 rng();

    for (int i = 0; i < tag_list_num; i++)
    {
        Fieldstat_tag_list_wrapper *tmp = new Fieldstat_tag_list_wrapper(dist, i + 1);
        tags[i] = tmp;
    }
}

cJSON *test_exporter_extract_results_with_standard_global(struct fieldstat *instance)
{
    struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new(instance);
    fieldstat_json_exporter_set_global_tag(fieldstat_json_exporter, TEST_TAG_GLOBAL, 3);

    char *json_string = fieldstat_json_exporter_export(fieldstat_json_exporter);
    cJSON *root_arr = cJSON_Parse(json_string);
    printf("test, fieldstat_json_exporter_export json_string: %s\n", json_string);
    free(json_string);
    fieldstat_json_exporter_free(fieldstat_json_exporter);

    return root_arr;
}

cJSON *test_exporter_extract_results(struct fieldstat *instance)
{
    struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new(instance);

    char *json_string = fieldstat_json_exporter_export(fieldstat_json_exporter);
    cJSON *root_arr = cJSON_Parse(json_string);
    printf("test, fieldstat_json_exporter_export json_string: %s\n", json_string);
    free(json_string);
    fieldstat_json_exporter_free(fieldstat_json_exporter);

    return root_arr;
}

void topk_standard_oper(std::function<void(Fieldstat_tag_list_wrapper *, unsigned int *)> &topk_add, size_t metric_num, unsigned int test_expected_big_count)
{
    for (size_t i = 0; i < OPER_NUM; i++) {
        int tmp;
        if (i < test_expected_big_count * TEST_TOPK_STANDARD_K) {
            tmp = i / test_expected_big_count; // 0 ~ TEST_TOPK_STANDARD_K added for the most of times
        } else {
            tmp = rand() % 100 + TEST_TOPK_STANDARD_K + 1; // the rest is randomly generated, plus TEST_TOPK_STANDARD_K so that its count is larger than TEST_TOPK_STANDARD_K
        }
        Fieldstat_tag_list_wrapper *added_tmp = new Fieldstat_tag_list_wrapper("flow id key", tmp);
        unsigned int counts[metric_num];
        for (size_t j = 0; j < metric_num; j++) {
            counts[j] = 1 + j;
        }

        topk_add(added_tmp, counts);
        delete added_tmp;
    }
}

void topk_init(struct fieldstat *instance, unsigned int test_expected_big_count)
{
    const char *field_name[TEST_METRIC_NUM] = {"topk1", "topk2"};
    int cube_id = fieldstat_register_cube(instance, TEST_TAG_SHARED1, 3, SAMPLING_MODE_TOPK, TEST_TOPK_STANDARD_K);
    int m1 = fieldstat_register_counter(instance, cube_id, field_name[0], 0);
    int m2 = fieldstat_register_counter(instance, cube_id, field_name[1], 0);

    std::function<void(Fieldstat_tag_list_wrapper *, unsigned int *)> topk_add = [instance, cube_id, m1, m2](
            Fieldstat_tag_list_wrapper *my_tags, unsigned int counts[TEST_METRIC_NUM]) {
                int cell_id = fieldstat_cube_add(instance, cube_id, my_tags->get_tag(), my_tags->get_tag_count(), counts[0]);
                if (cell_id >= 0) {
                    fieldstat_counter_incrby(instance, cube_id, m1, cell_id, counts[0]);
                    fieldstat_counter_incrby(instance, cube_id, m2, cell_id, counts[1]);
                    return;
            }
    };

    topk_standard_oper(topk_add, TEST_METRIC_NUM, test_expected_big_count);
}

TEST(export_test, cjson_export_with_fixed_tag_and_many_metrics_on_one_cube_of_comprehensive_sampling)
{
    const int tag_list_num = 3;

    // new instance
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, TEST_TAG_SHARED1, 3, SAMPLING_MODE_COMPREHENSIVE, tag_list_num);
    int id_counter = fieldstat_register_counter(instance, cube_id, "counter", 0);
    int id_gauge = fieldstat_register_hll(instance, cube_id, "gauge", g_hll_standard->cfg.precision);
    int id_histogram = fieldstat_register_histogram(instance, cube_id, "histogram",
        g_histogram_standard->lowest_discernible_value, g_histogram_standard->highest_trackable_value, g_histogram_standard->significant_figures);

    Fieldstat_tag_list_wrapper *tags[tag_list_num];
    fill_random_tag(tags, tag_list_num);

    for (int j = 0; j < tag_list_num; j++) {
        const struct fieldstat_tag *tag_tmp = tags[j]->get_tag();
        size_t tag_count = tags[j]->get_tag_count();
        int cell_id = fieldstat_cube_add(instance, cube_id, tag_tmp, tag_count, 1);

        fieldstat_counter_incrby(instance, cube_id, id_counter, cell_id, 1);
        for (size_t i = 0; i < OPER_NUM; i++){
            fieldstat_hll_add(instance, cube_id, id_gauge, cell_id, g_hll_standard_oper[i].c_str(), g_hll_standard_oper[i].length());
            fieldstat_histogram_record(instance, cube_id, id_histogram, cell_id, g_histogram_standard_oper[i]);
        }
    }

    // export test
    cJSON *root_arr = test_exporter_extract_results(instance);
    int arr_num = cJSON_GetArraySize(root_arr);
    EXPECT_EQ(arr_num, 3);

    for (int i = 0; i < arr_num; i++)
    {
        cJSON *root = cJSON_GetArrayItem(root_arr, i);

        // check tag
        cJSON *tag = cJSON_GetObjectItem(root, "tags");
        EXPECT_NE(tag, nullptr);
        test_check_if_tag_list_is_in_json(tag, tags[i]);

        // check metrics
        cJSON *metrics = cJSON_GetObjectItem(root, "fields");
        EXPECT_NE(metrics, nullptr);
        cJSON *counter = cJSON_GetObjectItem(metrics, "counter");
        EXPECT_EQ(counter->valueint, 1);
        test_check_if_metric_gauge_correct(metrics, "gauge");
        test_check_if_metric_histogram_correct(metrics, "histogram");
    }

    for (int i = 0; i < 3; i++) {
        delete tags[i];
    }

    fieldstat_free(instance);
    cJSON_Delete(root_arr);
}

TEST(export_test, cjson_export_on_one_cube_of_topk_sampling)
{
    struct fieldstat *instance = fieldstat_new();
    unsigned int test_expected_big_count = 1000;
    topk_init(instance, test_expected_big_count);

    // export test
    cJSON *root_arr = test_exporter_extract_results(instance);
    int arr_num = cJSON_GetArraySize(root_arr);
    EXPECT_EQ(arr_num, TEST_TOPK_STANDARD_K);

    for (int i = 0; i < arr_num; i++)
    {
        cJSON *root = cJSON_GetArrayItem(root_arr, i);

        // check tag
        cJSON *tag = cJSON_GetObjectItem(root, "tags");
        EXPECT_NE(tag, nullptr);
        
        Fieldstat_tag_list_wrapper *added_tmp = new Fieldstat_tag_list_wrapper("flow id key", i);
        test_check_if_tag_list_is_in_json(tag, added_tmp);
        delete added_tmp;

        struct fieldstat_tag_list tmp_tag = {(struct fieldstat_tag *)&TEST_TAG_SHARED1, 3};
        Fieldstat_tag_list_wrapper shared_wrapper = Fieldstat_tag_list_wrapper(&tmp_tag);
        test_check_if_tag_list_is_in_json(tag, &shared_wrapper);

        // check metrics
        cJSON *metrics = cJSON_GetObjectItem(root, "fields");
        EXPECT_NE(metrics, nullptr);
        const char *metric_name[TEST_METRIC_NUM] = {"topk1", "topk2"};
        test_check_if_metric_topk_correct(metrics, (const char **)metric_name, test_expected_big_count, TEST_METRIC_NUM);
    }
    cJSON_Delete(root_arr);

    fieldstat_free(instance);
}

TEST(export_test, empty_fieldstat_export_null) {
    struct fieldstat *instance = fieldstat_new();
    cJSON *root_arr = test_exporter_extract_results_with_standard_global(instance);
    EXPECT_EQ(root_arr, nullptr);
    fieldstat_free(instance);
}

TEST(export_test, only_registered_but_not_added_export_null_with_global_tag)
{
    struct fieldstat *instance = fieldstat_new();

    int cube_id = fieldstat_register_cube(instance, TEST_TAG_SHARED1, 3, SAMPLING_MODE_COMPREHENSIVE, 3);
    fieldstat_register_counter(instance, cube_id, "counter", 0);
    fieldstat_register_hll(instance, cube_id, "gauge", g_hll_standard->cfg.precision);
    fieldstat_register_histogram(instance, cube_id, "histogram",
        g_histogram_standard->lowest_discernible_value, g_histogram_standard->highest_trackable_value, g_histogram_standard->significant_figures);

    // add global tag
    cJSON *root_arr = test_exporter_extract_results_with_standard_global(instance);
    EXPECT_EQ(root_arr, nullptr);

    fieldstat_free(instance);
}

TEST(export_test, skip_two_empty_cube_and_export_last_one_with_global_tag)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id_1 = fieldstat_register_cube(instance, TEST_TAG_SHARED1, 3, SAMPLING_MODE_COMPREHENSIVE, 3);
    (void)fieldstat_register_hll(instance, cube_id_1, "gauge", g_hll_standard->cfg.precision);
    int cube_id_2 = fieldstat_register_cube(instance, TEST_TAG_SHARED2, 3, SAMPLING_MODE_COMPREHENSIVE, 3);
    (void)fieldstat_register_counter(instance, cube_id_2, "counter", false);
    int cube_id_3 = fieldstat_register_cube(instance, TEST_TAG_SHARED3, 1, SAMPLING_MODE_COMPREHENSIVE, 3);
    int id_histogram = fieldstat_register_histogram(instance, cube_id_3, "histogram",
        g_histogram_standard->lowest_discernible_value, g_histogram_standard->highest_trackable_value, g_histogram_standard->significant_figures);

    const int tag_num = 1;
    Fieldstat_tag_list_wrapper *tags[tag_num];
    fill_random_tag(tags, tag_num);
    Fieldstat_tag_list_wrapper *the_tag = tags[0];
    int cell_id = fieldstat_cube_add(instance, cube_id_3, the_tag->get_tag(), the_tag->get_tag_count(), 1);
    for (size_t i = 0; i < OPER_NUM; i++){
        fieldstat_histogram_record(instance, cube_id_3, id_histogram, cell_id, g_histogram_standard_oper[i]);
    }

    // export test
    cJSON *root_arr = test_exporter_extract_results_with_standard_global(instance);
    int arr_num = cJSON_GetArraySize(root_arr);
    EXPECT_EQ(arr_num, 1);

    for (int i = 0; i < arr_num; i++)
    {
        cJSON *root = cJSON_GetArrayItem(root_arr, i);

        // check tag
        cJSON *tag = cJSON_GetObjectItem(root, "tags");
        test_check_if_tag_list_is_in_json(tag, tags[0]);
        test_check_if_global_tag_is_in_json(tag);

        cJSON *metrics = cJSON_GetObjectItem(root, "fields");
        EXPECT_NE(metrics, nullptr);
        if (i == 0) {
            test_check_if_metric_histogram_correct(metrics, "histogram");
            continue;
        }

        FAIL();
    }

    delete tags[0];

    fieldstat_free(instance);
    cJSON_Delete(root_arr);
}

TEST(export_test, skip_empty_metrics_given_cube_deleted) {
    struct fieldstat *instance = fieldstat_new();
    int cube_id_del = fieldstat_register_cube(instance, TEST_TAG_SHARED1, 3, SAMPLING_MODE_COMPREHENSIVE, 3);
    int cube_id = fieldstat_register_cube(instance, TEST_TAG_SHARED2, 3, SAMPLING_MODE_COMPREHENSIVE, 3);
    fieldstat_unregister_cube(instance, cube_id_del);
    (void)fieldstat_register_counter(instance, cube_id, "counter", false);
    (void)fieldstat_register_counter(instance, cube_id, "counter2", false);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter3", false);
    (void)fieldstat_register_counter(instance, cube_id, "counter4", false);

    const int tag_num = 1;
    Fieldstat_tag_list_wrapper *tags[tag_num];
    fill_random_tag(tags, tag_num);
    Fieldstat_tag_list_wrapper *the_tag = tags[0];
    int cell_id = fieldstat_cube_add(instance, cube_id, the_tag->get_tag(), the_tag->get_tag_count(), 1);

    fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, 1234);

    cJSON *root_arr = test_exporter_extract_results_with_standard_global(instance);
    int arr_num = cJSON_GetArraySize(root_arr);
    EXPECT_EQ(arr_num, 1);

    for (int i = 0; i < arr_num; i++) {
        cJSON *root = cJSON_GetArrayItem(root_arr, i);

        // check tag
        cJSON *tag = cJSON_GetObjectItem(root, "tags");
        test_check_if_tag_list_is_in_json(tag, tags[0]);
        test_check_if_global_tag_is_in_json(tag);
        struct fieldstat_tag_list tmp_tag = {(struct fieldstat_tag *)&TEST_TAG_SHARED2, 3};
        Fieldstat_tag_list_wrapper shared_wrapper = Fieldstat_tag_list_wrapper(&tmp_tag);
        test_check_if_tag_list_is_in_json(tag, &shared_wrapper);

        cJSON *metrics = cJSON_GetObjectItem(root, "fields");
        EXPECT_NE(metrics, nullptr);
        if (i == 0) {
            cJSON *counter = cJSON_GetObjectItem(metrics, "counter3");
            EXPECT_EQ(counter->valueint, 1234);
            continue;
        }

        FAIL();
    }

    delete tags[0];
    fieldstat_free(instance);
    cJSON_Delete(root_arr);
}

void init_hll_standard_oper()
{
    g_hll_standard = ST_hyperloglog_new(12);
    for (size_t i = 0; i < OPER_NUM; i++) {
        std::string added_tmp = std::to_string(i);
        g_hll_standard_oper[i] = added_tmp;
        ST_hyperloglog_add(g_hll_standard, added_tmp.c_str(), added_tmp.size());
    }
}

void init_histogram_standard_oper()
{
    hdr_init(1, 20000, 3, &g_histogram_standard);
    for (size_t i = 0; i < OPER_NUM; i++) {
        g_histogram_standard_oper[i] = i;
        hdr_record_value(g_histogram_standard, i);
    }
}

int main(int argc, char *argv[]) 
{
    init_hll_standard_oper();
    init_histogram_standard_oper();
	testing::InitGoogleTest(&argc, argv);
	int ret = RUN_ALL_TESTS();

    ST_hyperloglog_free(g_hll_standard);
    hdr_close(g_histogram_standard);
    return ret;
}
