
#include <gtest/gtest.h>
#include <random>
#include <unordered_set>
#include "cjson/cJSON.h"
#include "base64/b64.h"


#include "fieldstat_exporter.h"
#include "fieldstat.h"
#include "utils.hpp"
#include "hdr/hdr_histogram.h"
#include "hdr/hdr_histogram_log.h"
#include "hyperloglog.h"
#include "histogram_encoder.h"
#include "very_fast_json_writer.h"

const size_t OPER_NUM = 10000;
std::string g_hll_standard_oper[OPER_NUM];
long long g_histogram_standard_oper[OPER_NUM];
struct hyperloglog *g_hll_standard;
struct hdr_histogram *g_histogram_standard;
#define TEST_TOPK_STANDARD_K 5
#define TEST_METRIC_NUM 2

const struct field TEST_TAG_GLOBAL1 = {"test_tag_global 1", .type = FIELD_VALUE_INTEGER, {.value_longlong = 1}};
const struct field TEST_TAG_GLOBAL2 = {"test_tag_global 2", .type = FIELD_VALUE_DOUBLE, {.value_double = 2.2}};
const struct field TEST_TAG_GLOBAL3 = {"test_tag_global 3", .type = FIELD_VALUE_CSTRING, {.value_str = "string3"}};
const struct field TEST_TAG_GLOBAL[3] = {TEST_TAG_GLOBAL1, TEST_TAG_GLOBAL2, TEST_TAG_GLOBAL3};

const struct field TEST_TAG_SHARED1_1 = {"test_tag_shared 1", .type = FIELD_VALUE_INTEGER, {.value_longlong = 3}};
const struct field TEST_TAG_SHARED1_2 = {"test_tag_shared 2", .type = FIELD_VALUE_DOUBLE, {.value_double = 0.2}};
const struct field TEST_TAG_SHARED1_3 = {"test_tag_shared 3", .type = FIELD_VALUE_CSTRING, {.value_str = "1string"}};
const struct field TEST_TAG_SHARED1[3] = {TEST_TAG_SHARED1_1, TEST_TAG_SHARED1_2, TEST_TAG_SHARED1_3};

const struct field TEST_TAG_SHARED2_1 = {"test_tag_shared 11", .type = FIELD_VALUE_INTEGER, {.value_longlong = 4}};
const struct field TEST_TAG_SHARED2_2 = {"test_tag_shared 22", .type = FIELD_VALUE_DOUBLE, {.value_double = 0.3}};
const struct field TEST_TAG_SHARED2_3 = {"test_tag_shared 33", .type = FIELD_VALUE_CSTRING, {.value_str = "2string"}};
const struct field TEST_TAG_SHARED2[3] = {TEST_TAG_SHARED2_1, TEST_TAG_SHARED2_2, TEST_TAG_SHARED2_3};

const struct field TEST_TAG_SHARED3_1 = {"test_tag_shared 3", .type = FIELD_VALUE_INTEGER, {.value_longlong = 5}};
const struct field TEST_TAG_SHARED3[1] = {TEST_TAG_SHARED3_1};

void test_check_if_tag_list_is_in_json(cJSON *tag_obj, const Fieldstat_tag_list_wrapper *benchmark)
{
    for (size_t tag_id = 0; tag_id < benchmark->get_tag_count(); tag_id++) {
        cJSON *tag_val = cJSON_GetObjectItem(tag_obj, benchmark->get_tag()[tag_id].key);
        EXPECT_NE(tag_val, nullptr);
        if (tag_val->type == cJSON_String) {
            EXPECT_STREQ(tag_val->valuestring, benchmark->get_tag()[tag_id].value_str);
        } else {
            if (benchmark->get_tag()[tag_id].type == FIELD_VALUE_INTEGER) {
                EXPECT_EQ(tag_val->valueint, benchmark->get_tag()[tag_id].value_longlong);
            } else {
                EXPECT_NEAR(tag_val->valuedouble, benchmark->get_tag()[tag_id].value_double, 0.0001);
            }
        }
    }
}

void test_check_if_global_tag_is_in_json(cJSON *tag_obj)
{
    struct field_list tag_list = {.field = (struct field *)TEST_TAG_GLOBAL, .n_field = 3};
    const Fieldstat_tag_list_wrapper benchmark(&tag_list);
    test_check_if_tag_list_is_in_json(tag_obj, &benchmark);
}

void test_check_if_metric_histogram_correct(cJSON *metric_obj, const char *name)
{
    char *blob_histogram_benchmark = NULL;
    size_t size_dummy = 0;
    histogram_encode_into_b64(g_histogram_standard, &blob_histogram_benchmark, &size_dummy);
    // hdr_log_encode(g_histogram_standard, &blob_histogram_benchmark);

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
    hyperloglog_serialize_into_base64(g_hll_standard, &blob_gauge_benchmark, &size_dummy);

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

void fill_random_tag(Fieldstat_tag_list_wrapper *fields[], int tag_list_num)
{
    std::uniform_int_distribution<int> dist(1,100);
    std::mt19937 rng();

    for (int i = 0; i < tag_list_num; i++)
    {
        Fieldstat_tag_list_wrapper *tmp = new Fieldstat_tag_list_wrapper(dist, i + 1);
        fields[i] = tmp;
    }
}

cJSON *test_exporter_extract_results_with_standard_global(const struct fieldstat *instance)
{
    struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new();
    fieldstat_json_exporter_set_global_tag(fieldstat_json_exporter, TEST_TAG_GLOBAL, 3);

    char *json_string = fieldstat_json_exporter_export(fieldstat_json_exporter, instance, &TEST_TIMEVAL);
    cJSON *root_arr = cJSON_Parse(json_string);
    free(json_string);
    fieldstat_json_exporter_free(fieldstat_json_exporter);

    return root_arr;
}

cJSON *test_exporter_extract_results(const struct fieldstat *instance)
{
    struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new();

    // getchar();
    char *json_string = fieldstat_json_exporter_export(fieldstat_json_exporter, instance, &TEST_TIMEVAL);
    // exit(0);
    cJSON *root_arr = cJSON_Parse(json_string);
    free(json_string);
    fieldstat_json_exporter_free(fieldstat_json_exporter);

    return root_arr;
}

void topk_standard_oper(const std::function<void(Fieldstat_tag_list_wrapper *, unsigned int *)> &topk_add, size_t metric_num, unsigned int test_expected_big_count)
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
    const char *metric_name[TEST_METRIC_NUM] = {"topk1", "topk2"};
    int cube_id = fieldstat_cube_create(instance, TEST_TAG_SHARED1, 3);
    int m1 = fieldstat_register_counter(instance, cube_id, metric_name[0]);
    int m2 = fieldstat_register_counter(instance, cube_id, metric_name[1]);
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOPK, TEST_TOPK_STANDARD_K, 0);

    std::function<void(Fieldstat_tag_list_wrapper *, unsigned int *)> topk_add = 
        [instance, cube_id, m1, m2](const Fieldstat_tag_list_wrapper *my_tags, unsigned int counts[TEST_METRIC_NUM]) {
                fieldstat_counter_incrby(instance, cube_id, m1, my_tags->get_tag(), my_tags->get_tag_count(), counts[0]);
                fieldstat_counter_incrby(instance, cube_id, m2, my_tags->get_tag(), my_tags->get_tag_count(), counts[1]);
        };

    topk_standard_oper(topk_add, TEST_METRIC_NUM, test_expected_big_count);
}

TEST(export_test, cjson_export_with_fixed_tag_and_many_metrics_on_one_cube_of_comprehensive_sampling)
{
    const int tag_list_num = 3;

    // new instance
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, TEST_TAG_SHARED1, 3);
    int id_counter = fieldstat_register_counter(instance, cube_id, "counter");
    int id_gauge = fieldstat_register_hll(instance, cube_id, "gauge", g_hll_standard->cfg.precision);
    int id_histogram = fieldstat_register_histogram(instance, cube_id, "histogram",
        g_histogram_standard->lowest_discernible_value, g_histogram_standard->highest_trackable_value, g_histogram_standard->significant_figures);

    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_COMPREHENSIVE, tag_list_num, 0);

    Fieldstat_tag_list_wrapper *fields[tag_list_num];
    fill_random_tag(fields, tag_list_num);

    for (int j = 0; j < tag_list_num; j++) {
        const struct field *tag_tmp = fields[j]->get_tag();
        size_t tag_count = fields[j]->get_tag_count();

        fieldstat_counter_incrby(instance, cube_id, id_counter, tag_tmp, tag_count, 1);
        for (size_t i = 0; i < OPER_NUM; i++){
            fieldstat_hll_add(instance, cube_id, id_gauge, tag_tmp, tag_count, g_hll_standard_oper[i].c_str(), g_hll_standard_oper[i].length());
            fieldstat_histogram_record(instance, cube_id, id_histogram, tag_tmp, tag_count, g_histogram_standard_oper[i]);
        }
    }

    // export test
    cJSON *root_arr = test_exporter_extract_results(instance);
    int arr_num = cJSON_GetArraySize(root_arr);
    EXPECT_EQ(arr_num, 3);

    for (int i = 0; i < arr_num; i++)
    {
        cJSON *root = cJSON_GetArrayItem(root_arr, i);

        // check field
        cJSON *field = cJSON_GetObjectItem(root, "tags");
        EXPECT_NE(field, nullptr);
        test_check_if_tag_list_is_in_json(field, fields[i]);

        // check metrics
        cJSON *metrics = cJSON_GetObjectItem(root, "fields");
        EXPECT_NE(metrics, nullptr);
        cJSON *counter = cJSON_GetObjectItem(metrics, "counter");
        EXPECT_EQ(counter->valueint, 1);
        test_check_if_metric_gauge_correct(metrics, "gauge");
        test_check_if_metric_histogram_correct(metrics, "histogram");
    }

    for (int i = 0; i < 3; i++) {
        delete fields[i];
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

    std::unordered_set<long long> topk_tag_value_set;
    for (int i = 0; i < arr_num; i++)
    {
        cJSON *root = cJSON_GetArrayItem(root_arr, i);

        // check tag
        cJSON *tag = cJSON_GetObjectItem(root, "tags");
        EXPECT_NE(tag, nullptr);
        
        cJSON *tag_val = cJSON_GetObjectItem(tag, "flow id key");
        EXPECT_NE(tag_val, nullptr);
        EXPECT_EQ(tag_val->type, cJSON_Number);
        topk_tag_value_set.insert(tag_val->valueint);

        struct field_list tmp_tag = {(struct field *)&TEST_TAG_SHARED1, 3};
        Fieldstat_tag_list_wrapper shared_wrapper = Fieldstat_tag_list_wrapper(&tmp_tag);
        test_check_if_tag_list_is_in_json(tag, &shared_wrapper);

        // check metrics
        cJSON *metrics = cJSON_GetObjectItem(root, "fields");
        EXPECT_NE(metrics, nullptr);
        const char *metric_name[TEST_METRIC_NUM] = {"topk1", "topk2"};
        test_check_if_metric_topk_correct(metrics, (const char **)metric_name, test_expected_big_count, TEST_METRIC_NUM);
    }

    EXPECT_EQ(topk_tag_value_set.size(), TEST_TOPK_STANDARD_K);
    for (int i = 0; i < TEST_TOPK_STANDARD_K; i++) { // the top k flow id is [0, TEST_TOPK_STANDARD_K - 1]
        EXPECT_NE(topk_tag_value_set.find(i), topk_tag_value_set.end());
    }

    cJSON_Delete(root_arr);

    fieldstat_free(instance);
}

extern "C" {
    double fieldstat_hll_base64_to_count(char *buf);
}
TEST(export_test, cjson_export_on_one_cube_of_spreadsketch_sampling) {
    int K = 10;
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1);
    int metric_id = fieldstat_register_hll(instance, cube_id, "metric", 7);
    int metric_count = fieldstat_register_counter(instance, cube_id, "oper cnt");
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOP_CARDINALITY, K, 0);

    SpreadSketchZipfGenerator flow_generator(1.0, K * 10);

    for (int i = 0; i < 100000; i++) {
        Flow flow = flow_generator.next();
        Fieldstat_tag_list_wrapper dimension = Fieldstat_tag_list_wrapper("bigger fanouts", flow.src_ip.c_str());
        Fieldstat_tag_list_wrapper item = Fieldstat_tag_list_wrapper("dummy", flow.dst_ip.c_str());
        fieldstat_hll_add_field(instance, cube_id, metric_id, dimension.get_tag(), dimension.get_tag_count(), item.get_tag(), item.get_tag_count());
        fieldstat_counter_incrby(instance, cube_id, metric_count, dimension.get_tag(), dimension.get_tag_count(), 1);
    }
    for (int i = 0; i < 100; i++) {
        Flow flow = flow_generator.next();
        Fieldstat_tag_list_wrapper dimension = Fieldstat_tag_list_wrapper("smaller fanouts", flow.src_ip.c_str());
        Fieldstat_tag_list_wrapper item = Fieldstat_tag_list_wrapper("dummy", flow.dst_ip.c_str());
        fieldstat_hll_add_field(instance, cube_id, metric_id, dimension.get_tag(), dimension.get_tag_count(), item.get_tag(), item.get_tag_count());
        fieldstat_counter_incrby(instance, cube_id, metric_count, dimension.get_tag(), dimension.get_tag_count(), 1);
    }

    cJSON *root_arr = test_exporter_extract_results(instance);
    int arr_num = cJSON_GetArraySize(root_arr);
    EXPECT_EQ(arr_num, K);

    for (int i = 0; i < arr_num; i++) {
        cJSON *root = cJSON_GetArrayItem(root_arr, i);

        // check tag
        cJSON *tag = cJSON_GetObjectItem(root, "tags");
        EXPECT_NE(tag, nullptr);
        cJSON *tag_val = cJSON_GetObjectItem(tag, "bigger fanouts");
        EXPECT_NE(tag_val, nullptr);

        // check metrics
        cJSON *metrics = cJSON_GetObjectItem(root, "fields");
        EXPECT_NE(metrics, nullptr);
        cJSON *metric = cJSON_GetObjectItem(metrics, "metric");
        cJSON *metric_count = cJSON_GetObjectItem(metrics, "oper cnt");
        double hll_value = fieldstat_hll_base64_to_count(metric->valuestring);
        EXPECT_NEAR(hll_value, metric_count->valueint, metric_count->valueint * 0.2);
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

int test_fieldstat_cube_create(struct fieldstat *instance, const struct field *dimensions, size_t n_dimensions, enum sampling_mode mode, int k, int primary_metric_id=0)
{
    assert(mode == SAMPLING_MODE_COMPREHENSIVE);
    int ret = fieldstat_cube_create(instance, dimensions, n_dimensions);
    fieldstat_cube_set_sampling(instance, ret, mode, k, primary_metric_id);
    return ret;
}

TEST(export_test, only_registered_but_not_added_export_null_with_global_tag)
{
    struct fieldstat *instance = fieldstat_new();

    int cube_id = test_fieldstat_cube_create(instance, TEST_TAG_SHARED1, 3, SAMPLING_MODE_COMPREHENSIVE, 3);
    fieldstat_register_counter(instance, cube_id, "counter");
    fieldstat_register_hll(instance, cube_id, "gauge", g_hll_standard->cfg.precision);
    fieldstat_register_histogram(instance, cube_id, "histogram",
        g_histogram_standard->lowest_discernible_value, g_histogram_standard->highest_trackable_value, g_histogram_standard->significant_figures);

    // add global field
    cJSON *root_arr = test_exporter_extract_results_with_standard_global(instance);
    EXPECT_EQ(root_arr, nullptr);

    fieldstat_free(instance);
}

TEST(export_test, skip_two_empty_cube_and_export_last_one_with_global_tag)
{
    struct fieldstat *instance = fieldstat_new();
    (void)test_fieldstat_cube_create(instance, TEST_TAG_SHARED1, 3, SAMPLING_MODE_COMPREHENSIVE, 3);
    (void)test_fieldstat_cube_create(instance, TEST_TAG_SHARED2, 3, SAMPLING_MODE_COMPREHENSIVE, 3);
    int cube_id_3 = test_fieldstat_cube_create(instance, TEST_TAG_SHARED3, 1, SAMPLING_MODE_COMPREHENSIVE, 3);
    (void)fieldstat_register_counter(instance, cube_id_3, "counter");
    (void)fieldstat_register_hll(instance, cube_id_3, "gauge", g_hll_standard->cfg.precision);
    int id_histogram = fieldstat_register_histogram(instance, cube_id_3, "histogram",
        g_histogram_standard->lowest_discernible_value, g_histogram_standard->highest_trackable_value, g_histogram_standard->significant_figures);

    const int tag_num = 1;
    Fieldstat_tag_list_wrapper *fields[tag_num];
    fill_random_tag(fields, tag_num);
    const Fieldstat_tag_list_wrapper *the_tag = fields[0];
    for (size_t i = 0; i < OPER_NUM; i++){
        fieldstat_histogram_record(instance, cube_id_3, id_histogram, the_tag->get_tag(), the_tag->get_tag_count(), g_histogram_standard_oper[i]);
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
        test_check_if_tag_list_is_in_json(tag, fields[0]);
        test_check_if_global_tag_is_in_json(tag);

        cJSON *metrics = cJSON_GetObjectItem(root, "fields");
        EXPECT_NE(metrics, nullptr);
        if (i == 0) {
            test_check_if_metric_histogram_correct(metrics, "histogram");
            continue;
        }

        FAIL();
    }

    delete fields[0];

    fieldstat_free(instance);
    cJSON_Delete(root_arr);
}

TEST(export_test, skip_empty_metrics_given_cube_deleted) {
    struct fieldstat *instance = fieldstat_new();
    int cube_id_del = test_fieldstat_cube_create(instance, TEST_TAG_SHARED1, 3, SAMPLING_MODE_COMPREHENSIVE, 3);
    int cube_id = test_fieldstat_cube_create(instance, TEST_TAG_SHARED2, 3, SAMPLING_MODE_COMPREHENSIVE, 3);
    fieldstat_cube_destroy(instance, cube_id_del);
    (void)fieldstat_register_counter(instance, cube_id, "counter");
    (void)fieldstat_register_counter(instance, cube_id, "counter2");
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter3");
    (void)fieldstat_register_counter(instance, cube_id, "counter4");

    const int tag_num = 1;
    Fieldstat_tag_list_wrapper *fields[tag_num];
    fill_random_tag(fields, tag_num);
    const Fieldstat_tag_list_wrapper *the_tag = fields[0];

    fieldstat_counter_incrby(instance, cube_id, metric_id, the_tag->get_tag(), the_tag->get_tag_count(), 1234);

    cJSON *root_arr = test_exporter_extract_results_with_standard_global(instance);
    int arr_num = cJSON_GetArraySize(root_arr);
    EXPECT_EQ(arr_num, 1);

    for (int i = 0; i < arr_num; i++) {
        cJSON *root = cJSON_GetArrayItem(root_arr, i);

        // check field
        cJSON *field = cJSON_GetObjectItem(root, "tags");
        test_check_if_tag_list_is_in_json(field, fields[0]);
        test_check_if_global_tag_is_in_json(field);
        struct field_list tmp_tag = {(struct field *)&TEST_TAG_SHARED2, 3};
        Fieldstat_tag_list_wrapper shared_wrapper = Fieldstat_tag_list_wrapper(&tmp_tag);
        test_check_if_tag_list_is_in_json(field, &shared_wrapper);

        cJSON *metrics = cJSON_GetObjectItem(root, "fields");
        EXPECT_NE(metrics, nullptr);
        if (i == 0) {
            cJSON *counter = cJSON_GetObjectItem(metrics, "counter3");
            EXPECT_EQ(counter->valueint, 1234);
            continue;
        }

        FAIL();
    }

    delete fields[0];
    fieldstat_free(instance);
    cJSON_Delete(root_arr);
}

TEST(export_test, enable_delta_and_export_twice_without_new_metric)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, TEST_TAG_SHARED1, 3, SAMPLING_MODE_COMPREHENSIVE, 10);
    int id_counter = fieldstat_register_counter(instance, cube_id, "counter");
    fieldstat_counter_incrby(instance, cube_id, id_counter, &TEST_TAG_INT, 1, 1);

    // export test
    struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new();
    fieldstat_json_exporter_enable_delta(fieldstat_json_exporter);
    char *json_string = fieldstat_json_exporter_export(fieldstat_json_exporter, instance, &TEST_TIMEVAL);
    cJSON *root_arr = cJSON_Parse(json_string);
    free(json_string);

    int arr_num = cJSON_GetArraySize(root_arr);
    EXPECT_EQ(arr_num, 1);
    for (int i = 0; i < arr_num; i++)
    {
        cJSON *root = cJSON_GetArrayItem(root_arr, i);

        // check metrics
        cJSON *metrics = cJSON_GetObjectItem(root, "fields");
        cJSON *metrics_delta = cJSON_GetObjectItem(root, "fields_delta");
        cJSON *counter = cJSON_GetObjectItem(metrics, "counter");
        EXPECT_EQ(counter->valueint, 1);
        cJSON *counter_delta = cJSON_GetObjectItem(metrics_delta, "counter");
        EXPECT_EQ(counter_delta->valueint, 1);

        // timestamp
        cJSON *timestamp = cJSON_GetObjectItem(root, "timestamp_ms");
        EXPECT_EQ(timestamp->valueint, TEST_TIMEVAL_LONG);
        cJSON *timestamp_delta = cJSON_GetObjectItem(root, "timestamp_ms_delta");
        EXPECT_EQ(timestamp_delta->valueint, TEST_TIMEVAL_LONG);
    }
    cJSON_Delete(root_arr);

    struct timeval new_ts = TEST_TIMEVAL;
    new_ts.tv_sec += 1;
    json_string = fieldstat_json_exporter_export(fieldstat_json_exporter, instance, &new_ts);
    root_arr = cJSON_Parse(json_string);
    free(json_string);

    arr_num = cJSON_GetArraySize(root_arr);
    EXPECT_EQ(arr_num, 1);
    for (int i = 0; i < arr_num; i++)
    {
        cJSON *root = cJSON_GetArrayItem(root_arr, i);

        // check metrics
        cJSON *metrics = cJSON_GetObjectItem(root, "fields");
        cJSON *metrics_delta = cJSON_GetObjectItem(root, "fields_delta");
        cJSON *counter = cJSON_GetObjectItem(metrics, "counter");
        EXPECT_EQ(counter->valueint, 1);
        cJSON *counter_delta = cJSON_GetObjectItem(metrics_delta, "counter");
        EXPECT_EQ(counter_delta->valueint, 0);

        // timestamp
        cJSON *timestamp = cJSON_GetObjectItem(root, "timestamp_ms");
        EXPECT_EQ(timestamp->valueint, TEST_TIMEVAL_LONG + 1000);
        cJSON *timestamp_delta = cJSON_GetObjectItem(root, "timestamp_ms_delta");
        EXPECT_EQ(timestamp_delta->valueint, 1000);
    }
    cJSON_Delete(root_arr);

    fieldstat_json_exporter_free(fieldstat_json_exporter);

    fieldstat_free(instance);
} 

TEST(export_test, enable_delta_and_export_twice_with_new_metric_and_omit_histogram)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, TEST_TAG_SHARED1, 3, SAMPLING_MODE_COMPREHENSIVE, 10);
    int id_counter = fieldstat_register_counter(instance, cube_id, "counter");
    int id_histogram = fieldstat_register_histogram(instance, cube_id, "histogram",
        g_histogram_standard->lowest_discernible_value, g_histogram_standard->highest_trackable_value, g_histogram_standard->significant_figures);
    fieldstat_counter_incrby(instance, cube_id, id_counter, &TEST_TAG_INT, 1, 1);
    fieldstat_histogram_record(instance, cube_id, id_histogram, &TEST_TAG_INT, 1, 123);

    // export test
    struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new();
    fieldstat_json_exporter_enable_delta(fieldstat_json_exporter);
    char *json_string = fieldstat_json_exporter_export(fieldstat_json_exporter, instance, &TEST_TIMEVAL);
    cJSON *root_arr = cJSON_Parse(json_string);
    free(json_string);

    int arr_num = cJSON_GetArraySize(root_arr);
    EXPECT_EQ(arr_num, 1);
    for (int i = 0; i < arr_num; i++)
    {
        cJSON *root = cJSON_GetArrayItem(root_arr, i);

        // check metrics
        cJSON *metrics = cJSON_GetObjectItem(root, "fields");
        cJSON *metrics_delta = cJSON_GetObjectItem(root, "fields_delta");
        cJSON *counter = cJSON_GetObjectItem(metrics, "counter");
        EXPECT_EQ(counter->valueint, 1);
        cJSON *counter_delta = cJSON_GetObjectItem(metrics_delta, "counter");
        EXPECT_EQ(counter_delta->valueint, 1);
        EXPECT_TRUE(cJSON_GetObjectItem(metrics, "histogram") != nullptr);
        EXPECT_TRUE(cJSON_GetObjectItem(metrics_delta, "histogram") == NULL);

        // timestamp
        cJSON *timestamp = cJSON_GetObjectItem(root, "timestamp_ms");
        EXPECT_EQ(timestamp->valueint, TEST_TIMEVAL_LONG);
        cJSON *timestamp_delta = cJSON_GetObjectItem(root, "timestamp_ms_delta");
        EXPECT_EQ(timestamp_delta->valueint, TEST_TIMEVAL_LONG);
    }
    cJSON_Delete(root_arr);

    struct timeval new_ts = TEST_TIMEVAL;
    new_ts.tv_sec += 1;
    int id_counter2 = fieldstat_register_counter(instance, cube_id, "counter2");
    fieldstat_counter_incrby(instance, cube_id, id_counter2, &TEST_TAG_INT, 1, 1);
    fieldstat_counter_incrby(instance, cube_id, id_counter, &TEST_TAG_INT, 1, 10);

    json_string = fieldstat_json_exporter_export(fieldstat_json_exporter, instance, &new_ts);
    root_arr = cJSON_Parse(json_string);
    free(json_string);

    arr_num = cJSON_GetArraySize(root_arr);
    EXPECT_EQ(arr_num, 1);
    for (int i = 0; i < arr_num; i++)
    {
        cJSON *root = cJSON_GetArrayItem(root_arr, i);

        // check metrics
        cJSON *metrics = cJSON_GetObjectItem(root, "fields");
        cJSON *metrics_delta = cJSON_GetObjectItem(root, "fields_delta");
        cJSON *counter = cJSON_GetObjectItem(metrics, "counter");
        EXPECT_EQ(counter->valueint, 11);
        cJSON *counter_delta = cJSON_GetObjectItem(metrics_delta, "counter");
        EXPECT_EQ(counter_delta->valueint, 10);
        EXPECT_TRUE(cJSON_GetObjectItem(metrics, "histogram") != nullptr);
        EXPECT_TRUE(cJSON_GetObjectItem(metrics_delta, "histogram") == NULL);
        cJSON *counter2 = cJSON_GetObjectItem(metrics, "counter2");
        EXPECT_EQ(counter2->valueint, 1);
        cJSON *counter2_delta = cJSON_GetObjectItem(metrics_delta, "counter2");
        EXPECT_EQ(counter2_delta->valueint, 1);

        // timestamp
        cJSON *timestamp = cJSON_GetObjectItem(root, "timestamp_ms");
        EXPECT_EQ(timestamp->valueint, TEST_TIMEVAL_LONG + 1000);
        cJSON *timestamp_delta = cJSON_GetObjectItem(root, "timestamp_ms_delta");
        EXPECT_EQ(timestamp_delta->valueint, 1000);
    }
    cJSON_Delete(root_arr);
    fieldstat_json_exporter_free(fieldstat_json_exporter);
    fieldstat_free(instance);
} 

TEST(export_test, enable_delta_and_export_three_times_skipping_cube_with_no_counter)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, TEST_TAG_SHARED1, 3, SAMPLING_MODE_COMPREHENSIVE, 10);
    int id_counter = fieldstat_register_counter(instance, cube_id, "counter");
    int id_histogram = fieldstat_register_histogram(instance, cube_id, "histogram",
        g_histogram_standard->lowest_discernible_value, g_histogram_standard->highest_trackable_value, g_histogram_standard->significant_figures);
    fieldstat_counter_incrby(instance, cube_id, id_counter, &TEST_TAG_INT, 1, 1);
    fieldstat_histogram_record(instance, cube_id, id_histogram, &TEST_TAG_STRING, 1, 123);

    /* -------------------------- export test, 1st time ------------------------- */
    struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new();
    fieldstat_json_exporter_enable_delta(fieldstat_json_exporter);
    char *json_string = fieldstat_json_exporter_export(fieldstat_json_exporter, instance, &TEST_TIMEVAL);
    cJSON *root_arr = cJSON_Parse(json_string);
    free(json_string);

    int arr_num = cJSON_GetArraySize(root_arr);
    EXPECT_EQ(arr_num, 2);

    cJSON *root = cJSON_GetArrayItem(root_arr, 0);
    cJSON *metrics = cJSON_GetObjectItem(root, "fields");
    cJSON *metrics_delta = cJSON_GetObjectItem(root, "fields_delta");
    cJSON *counter = cJSON_GetObjectItem(metrics, "counter");
    EXPECT_EQ(counter->valueint, 1);
    cJSON *counter_delta = cJSON_GetObjectItem(metrics_delta, "counter");
    EXPECT_EQ(counter_delta->valueint, 1);
    EXPECT_TRUE(cJSON_GetObjectItem(metrics, "histogram") == NULL);
    EXPECT_TRUE(cJSON_GetObjectItem(metrics_delta, "histogram") == NULL);

    root = cJSON_GetArrayItem(root_arr, 1);
    metrics = cJSON_GetObjectItem(root, "fields");
    metrics_delta = cJSON_GetObjectItem(root, "fields_delta");
    EXPECT_TRUE(cJSON_GetObjectItem(metrics, "counter") == NULL);
    EXPECT_TRUE(cJSON_GetObjectItem(metrics_delta, "counter") == NULL);
    EXPECT_TRUE(cJSON_GetObjectItem(metrics_delta, "histogram") == NULL);
    EXPECT_TRUE(cJSON_GetObjectItem(metrics, "histogram") != NULL);
    cJSON_Delete(root_arr);

    /* -------------------------------- 2nd time -------------------------------- */
    json_string = fieldstat_json_exporter_export(fieldstat_json_exporter, instance, &TEST_TIMEVAL);
    root_arr = cJSON_Parse(json_string);
    free(json_string);
    root = cJSON_GetArrayItem(root_arr, 0);
    metrics = cJSON_GetObjectItem(root, "fields");
    metrics_delta = cJSON_GetObjectItem(root, "fields_delta");
    counter = cJSON_GetObjectItem(metrics, "counter");
    EXPECT_EQ(counter->valueint, 1);
    counter_delta = cJSON_GetObjectItem(metrics_delta, "counter");
    EXPECT_EQ(counter_delta->valueint, 0);
    EXPECT_TRUE(cJSON_GetObjectItem(metrics, "histogram") == NULL);
    EXPECT_TRUE(cJSON_GetObjectItem(metrics_delta, "histogram") == NULL);
    root = cJSON_GetArrayItem(root_arr, 1);
    metrics = cJSON_GetObjectItem(root, "fields");
    metrics_delta = cJSON_GetObjectItem(root, "fields_delta");
    EXPECT_TRUE(cJSON_GetObjectItem(metrics, "counter") == NULL);
    EXPECT_TRUE(cJSON_GetObjectItem(metrics_delta, "counter_delta") == NULL);
    EXPECT_TRUE(cJSON_GetObjectItem(metrics_delta, "histogram_delta") == NULL);
    EXPECT_TRUE(cJSON_GetObjectItem(metrics, "histogram") != NULL);
    cJSON_Delete(root_arr);

    /* -------------------------------- 3rd time -------------------------------- */
    // add some new recs
    fieldstat_counter_incrby(instance, cube_id, id_counter, &TEST_TAG_INT, 1, 10);
    fieldstat_histogram_record(instance, cube_id, id_histogram, &TEST_TAG_STRING, 1, 1234);
    json_string = fieldstat_json_exporter_export(fieldstat_json_exporter, instance, &TEST_TIMEVAL);
    root_arr = cJSON_Parse(json_string);
    free(json_string);
    root = cJSON_GetArrayItem(root_arr, 0);
    metrics = cJSON_GetObjectItem(root, "fields");
    metrics_delta = cJSON_GetObjectItem(root, "fields_delta");
    counter = cJSON_GetObjectItem(metrics, "counter");
    EXPECT_EQ(counter->valueint, 11);
    counter_delta = cJSON_GetObjectItem(metrics_delta, "counter");
    EXPECT_EQ(counter_delta->valueint, 10);
    EXPECT_TRUE(cJSON_GetObjectItem(metrics, "histogram") == NULL);
    EXPECT_TRUE(cJSON_GetObjectItem(metrics_delta, "histogram") == NULL);
    root = cJSON_GetArrayItem(root_arr, 1);
    metrics = cJSON_GetObjectItem(root, "fields");
    metrics_delta = cJSON_GetObjectItem(root, "fields_delta");
    EXPECT_TRUE(cJSON_GetObjectItem(metrics, "counter") == NULL);
    EXPECT_TRUE(cJSON_GetObjectItem(metrics_delta, "counter") == NULL);
    EXPECT_TRUE(cJSON_GetObjectItem(metrics_delta, "histogram") == NULL);
    EXPECT_TRUE(cJSON_GetObjectItem(metrics, "histogram") != NULL);

    cJSON_Delete(root_arr);
    fieldstat_json_exporter_free(fieldstat_json_exporter);
    fieldstat_free(instance);
}

void test_check_delta_for_one_json(const struct field_list *expect_cell_tag, const struct field_list *expect_shared_tag, long long expect_value, long long expect_delta, cJSON *root)
{
    // check field
    cJSON *field = cJSON_GetObjectItem(root, "tags");
    Fieldstat_tag_list_wrapper cell_wrapper = Fieldstat_tag_list_wrapper(expect_cell_tag);
    test_check_if_tag_list_is_in_json(field, &cell_wrapper);
    Fieldstat_tag_list_wrapper shared_wrapper = Fieldstat_tag_list_wrapper(expect_shared_tag);
    test_check_if_tag_list_is_in_json(field, &shared_wrapper);
    test_check_if_global_tag_is_in_json(field);

    // check metrics
    cJSON *metrics = cJSON_GetObjectItem(root, "fields");
    cJSON *metrics_delta = cJSON_GetObjectItem(root, "fields_delta");
    cJSON *counter = cJSON_GetObjectItem(metrics, "counter");
    EXPECT_EQ(counter->valueint, expect_value);
    cJSON *counter_delta = cJSON_GetObjectItem(metrics_delta, "counter");
    EXPECT_EQ(counter_delta->valueint, expect_delta);
}

TEST(export_test, enable_delta_and_export_instance_with_many_cells_with_global_tags)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id1 = test_fieldstat_cube_create(instance, TEST_TAG_SHARED1, 3, SAMPLING_MODE_COMPREHENSIVE, 10);
    int id_counter1 = fieldstat_register_counter(instance, cube_id1, "counter");
    fieldstat_counter_incrby(instance, cube_id1, id_counter1, &TEST_TAG_INT, 1, 11);
    fieldstat_counter_incrby(instance, cube_id1, id_counter1, &TEST_TAG_STRING, 1, 12);

    int cube_id2 = test_fieldstat_cube_create(instance, TEST_TAG_SHARED3, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int id_counter2 = fieldstat_register_counter(instance, cube_id2, "counter");
    fieldstat_counter_incrby(instance, cube_id2, id_counter2, &TEST_TAG_INT, 1, 21);
    fieldstat_counter_incrby(instance, cube_id2, id_counter2, &TEST_TAG_DOUBLE, 1, 22);

    // export test
    struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new();
    fieldstat_json_exporter_set_global_tag(fieldstat_json_exporter, TEST_TAG_GLOBAL, 3);
    fieldstat_json_exporter_set_name(fieldstat_json_exporter, "test_instance");
    fieldstat_json_exporter_enable_delta(fieldstat_json_exporter);
    char *json_string = fieldstat_json_exporter_export(fieldstat_json_exporter, instance, &TEST_TIMEVAL);
    cJSON *root_arr = cJSON_Parse(json_string);
    free(json_string);

    int arr_num = cJSON_GetArraySize(root_arr);
    EXPECT_EQ(arr_num, 4);

    /* ------------------------------ cube 0 cell 0 ----------------------------- */
    cJSON *root = cJSON_GetArrayItem(root_arr, 0);
    struct field_list tmp_tag_cell = {(struct field *)&TEST_TAG_INT, 1};
    struct field_list tmp_tag_shared = {(struct field *)&TEST_TAG_SHARED1, 3};
    test_check_delta_for_one_json(&tmp_tag_cell, &tmp_tag_shared, 11, 11, root);
    /* ------------------------------ cube 0 cell 1 ----------------------------- */
    root = cJSON_GetArrayItem(root_arr, 1);
    tmp_tag_cell = (struct field_list){(struct field *)&TEST_TAG_STRING, 1};
    test_check_delta_for_one_json(&tmp_tag_cell, &tmp_tag_shared, 12, 12, root);
    /* ------------------------------ cube 1 cell 0 ----------------------------- */
    root = cJSON_GetArrayItem(root_arr, 2);
    tmp_tag_cell = (struct field_list){(struct field *)&TEST_TAG_INT, 1};
    tmp_tag_shared = (struct field_list){(struct field *)&TEST_TAG_SHARED3, 1};
    test_check_delta_for_one_json(&tmp_tag_cell, &tmp_tag_shared, 21, 21, root);
    /* ------------------------------ cube 1 cell 1 ----------------------------- */
    root = cJSON_GetArrayItem(root_arr, 3);
    tmp_tag_cell = (struct field_list){(struct field *)&TEST_TAG_DOUBLE, 1};
    test_check_delta_for_one_json(&tmp_tag_cell, &tmp_tag_shared, 22, 22, root);

    // new turn 
    fieldstat_counter_incrby(instance, cube_id1, id_counter1, &TEST_TAG_INT, 1, 100);
    fieldstat_counter_incrby(instance, cube_id1, id_counter1, &TEST_TAG_STRING, 1, 200);
    fieldstat_counter_incrby(instance, cube_id2, id_counter1, &TEST_TAG_INT, 1, 300);
    fieldstat_counter_incrby(instance, cube_id2, id_counter1, &TEST_TAG_DOUBLE, 1, 400);

    cJSON_Delete(root_arr);

    struct timeval new_ts = TEST_TIMEVAL;
    new_ts.tv_sec += 1;
    json_string = fieldstat_json_exporter_export(fieldstat_json_exporter, instance, &new_ts);
    root_arr = cJSON_Parse(json_string);
    free(json_string);

    arr_num = cJSON_GetArraySize(root_arr);
    EXPECT_EQ(arr_num, 4);

    /* ------------------------------ cube 0 cell 0 ----------------------------- */
    root = cJSON_GetArrayItem(root_arr, 0);
    tmp_tag_cell = (struct field_list){(struct field *)&TEST_TAG_INT, 1};
    tmp_tag_shared = (struct field_list){(struct field *)&TEST_TAG_SHARED1, 3};
    test_check_delta_for_one_json(&tmp_tag_cell, &tmp_tag_shared, 111, 100, root);
    /* ------------------------------ cube 0 cell 1 ----------------------------- */
    root = cJSON_GetArrayItem(root_arr, 1);
    tmp_tag_cell = (struct field_list){(struct field *)&TEST_TAG_STRING, 1};
    test_check_delta_for_one_json(&tmp_tag_cell, &tmp_tag_shared, 212, 200, root);
    /* ------------------------------ cube 1 cell 0 ----------------------------- */
    root = cJSON_GetArrayItem(root_arr, 2);
    tmp_tag_cell = (struct field_list){(struct field *)&TEST_TAG_INT, 1};
    tmp_tag_shared = (struct field_list){(struct field *)&TEST_TAG_SHARED3, 1};
    test_check_delta_for_one_json(&tmp_tag_cell, &tmp_tag_shared, 321, 300, root);
    /* ------------------------------ cube 1 cell 1 ----------------------------- */
    root = cJSON_GetArrayItem(root_arr, 3);
    tmp_tag_cell = (struct field_list){(struct field *)&TEST_TAG_DOUBLE, 1};
    test_check_delta_for_one_json(&tmp_tag_cell, &tmp_tag_shared, 422, 400, root);

    cJSON_Delete(root_arr);

    fieldstat_json_exporter_free(fieldstat_json_exporter);
    fieldstat_free(instance);
}

void test_reset_one_round(std::function<void(struct fieldstat *, struct fieldstat_json_exporter *)> trigger_reset)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 1);
    int id_counter = fieldstat_register_counter(instance, cube_id, "counter");
    fieldstat_counter_incrby(instance, cube_id, id_counter, &TEST_TAG_INT, 1, 11);

    // export test
    struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new();
    fieldstat_json_exporter_enable_delta(fieldstat_json_exporter);
    char *json_string = fieldstat_json_exporter_export(fieldstat_json_exporter, instance, &TEST_TIMEVAL);
    cJSON *root_arr = cJSON_Parse(json_string);
    free(json_string);

    int arr_num = cJSON_GetArraySize(root_arr);
    EXPECT_EQ(arr_num, 1);
    cJSON *root = cJSON_GetArrayItem(root_arr, 0);
    cJSON *metrics = cJSON_GetObjectItem(root, "fields");
    cJSON *metrics_delta = cJSON_GetObjectItem(root, "fields_delta");
    cJSON *counter = cJSON_GetObjectItem(metrics, "counter");
    EXPECT_EQ(counter->valueint, 11);
    cJSON *counter_delta = cJSON_GetObjectItem(metrics_delta, "counter");
    EXPECT_EQ(counter_delta->valueint, 11);
    cJSON_Delete(root_arr);

    trigger_reset(instance, fieldstat_json_exporter);

    fieldstat_counter_set(instance, cube_id, id_counter, &TEST_TAG_INT, 1, 123);

    json_string = fieldstat_json_exporter_export(fieldstat_json_exporter, instance, &TEST_TIMEVAL);
    printf("test_reset_one_round :\n%s\n", json_string);
    root_arr = cJSON_Parse(json_string);
    free(json_string);

    arr_num = cJSON_GetArraySize(root_arr);
    EXPECT_EQ(arr_num, 1);
    root = cJSON_GetArrayItem(root_arr, 0);
    metrics = cJSON_GetObjectItem(root, "fields");
    metrics_delta = cJSON_GetObjectItem(root, "fields_delta");
    counter = cJSON_GetObjectItem(metrics, "counter");
    EXPECT_EQ(counter->valueint, 123);
    counter_delta = cJSON_GetObjectItem(metrics_delta, "counter");
    EXPECT_EQ(counter_delta->valueint, 123);
    cJSON_Delete(root_arr);

    fieldstat_json_exporter_free(fieldstat_json_exporter);
    fieldstat_free(instance);
}

TEST(export_test, enable_delta_and_reset_on_change_exporter_name) {
    auto trigger = [](struct fieldstat *instance, struct fieldstat_json_exporter *fieldstat_json_exporter) {
        fieldstat_json_exporter_set_name(fieldstat_json_exporter, "test_instance");
    };

    test_reset_one_round(trigger);
}

TEST(export_test, enable_delta_and_reset_on_change_exporter_tag) {
    auto trigger = [](struct fieldstat *instance, struct fieldstat_json_exporter *fieldstat_json_exporter) {
        fieldstat_json_exporter_set_global_tag(fieldstat_json_exporter, TEST_TAG_GLOBAL, 3);
    };

    test_reset_one_round(trigger);
}

TEST(export_test, enable_delta_and_reset_on_delete_cube) {
    auto trigger = [](struct fieldstat *instance, struct fieldstat_json_exporter *fieldstat_json_exporter) {
        fieldstat_cube_destroy(instance, 0);
        test_fieldstat_cube_create(instance, TEST_TAG_SHARED1, 3, SAMPLING_MODE_COMPREHENSIVE, 3);
        fieldstat_register_counter(instance, 0, "counter");
    };

    test_reset_one_round(trigger);
}

TEST(export_test, delta_with_two_instance_same_config)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, NULL, 0, SAMPLING_MODE_COMPREHENSIVE, 0);
    int id_counter = fieldstat_register_counter(instance, cube_id, "counter");
    fieldstat_counter_incrby(instance, 0, id_counter, &TEST_TAG_INT, 1, 123);
    int id_hist = fieldstat_register_histogram(instance, cube_id, "histogram", 1, 1000, 3);
    fieldstat_histogram_record(instance, cube_id, id_hist, &TEST_TAG_INT, 1, 5);
    
    struct fieldstat *acc = fieldstat_new();
    fieldstat_merge(acc, instance);
    fieldstat_counter_incrby(acc, 0, id_counter, &TEST_TAG_INT, 1, 1000);

    // export test
    struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new();
    char *json = fieldstat_json_exporter_export_with_delta(fieldstat_json_exporter, acc, instance, &TEST_TIMEVAL, &TEST_TIMEVAL);
    cJSON *root_arr = cJSON_Parse(json);
    free(json);

    cJSON *root = cJSON_GetArrayItem(root_arr, 0);
    cJSON *metrics = cJSON_GetObjectItem(root, "fields");
    cJSON *metrics_delta = cJSON_GetObjectItem(root, "fields_delta");
    cJSON *counter = cJSON_GetObjectItem(metrics, "counter");
    EXPECT_EQ(counter->valueint, 1123);
    cJSON *counter_delta = cJSON_GetObjectItem(metrics_delta, "counter");
    EXPECT_EQ(counter_delta->valueint, 123);
    const cJSON *histogram = cJSON_GetObjectItem(metrics, "histogram");
    const cJSON *histogram_delta = cJSON_GetObjectItem(metrics_delta, "histogram");
    EXPECT_STREQ(histogram->valuestring, histogram_delta->valuestring);
    cJSON *timestamp = cJSON_GetObjectItem(root, "timestamp_ms");
    EXPECT_EQ(timestamp->valueint, TEST_TIMEVAL_LONG);
    cJSON *timestamp_delta = cJSON_GetObjectItem(root, "timestamp_ms_delta");
    EXPECT_EQ(timestamp_delta->valueint, TEST_TIMEVAL_LONG);

    cJSON_Delete(root_arr);
    fieldstat_json_exporter_free(fieldstat_json_exporter);
    fieldstat_free(instance);
    fieldstat_free(acc);
}

TEST(export_test, delta_with_two_instance_one_empty)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, NULL, 0, SAMPLING_MODE_COMPREHENSIVE, 0);
    int id_counter = fieldstat_register_counter(instance, cube_id, "counter");
    fieldstat_counter_incrby(instance, 0, id_counter, &TEST_TAG_INT, 1, 123);
    int id_hist = fieldstat_register_histogram(instance, cube_id, "histogram", 1, 1000, 3);
    fieldstat_histogram_record(instance, 0, id_hist, &TEST_TAG_INT, 1, 5);
    
    struct fieldstat *delta = fieldstat_fork(instance);

    // export test
    struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new();
    char *json = fieldstat_json_exporter_export_with_delta(fieldstat_json_exporter, instance, delta, &TEST_TIMEVAL, &TEST_TIMEVAL);
    cJSON *root_arr = cJSON_Parse(json);
    free(json);

    cJSON *root = cJSON_GetArrayItem(root_arr, 0);
    cJSON *metrics = cJSON_GetObjectItem(root, "fields");
    cJSON *metrics_delta = cJSON_GetObjectItem(root, "fields_delta");
    // metrics_delta is empty
    const cJSON *counter_delta = cJSON_GetObjectItem(metrics_delta, "counter");
    EXPECT_TRUE(counter_delta == NULL);

    cJSON *counter = cJSON_GetObjectItem(metrics, "counter");
    EXPECT_EQ(counter->valueint, 123);
    const cJSON *histogram = cJSON_GetObjectItem(metrics, "histogram");
    EXPECT_TRUE(histogram != NULL);
    cJSON *timestamp = cJSON_GetObjectItem(root, "timestamp_ms");
    EXPECT_EQ(timestamp->valueint, TEST_TIMEVAL_LONG);
    cJSON *timestamp_delta = cJSON_GetObjectItem(root, "timestamp_ms_delta");
    EXPECT_EQ(timestamp_delta->valueint, TEST_TIMEVAL_LONG);

    cJSON_Delete(root_arr);
    fieldstat_json_exporter_free(fieldstat_json_exporter);
    fieldstat_free(instance);
    fieldstat_free(delta);
}

TEST(export_test, delta_with_two_instance_different_cell)
{
    struct fieldstat *instance = fieldstat_new();
    test_fieldstat_cube_create(instance, NULL, 0, SAMPLING_MODE_COMPREHENSIVE, 0);
    int id_counter = fieldstat_register_counter(instance, 0, "counter");
    fieldstat_counter_incrby(instance, 0, id_counter, &TEST_TAG_INT, 1, 123);

    struct fieldstat *delta = fieldstat_fork(instance);
    fieldstat_counter_incrby(delta, 0, id_counter, &TEST_TAG_DOUBLE, 1, 1);
    fieldstat_merge(instance, delta);

    // export test
    struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new();
    char *json = fieldstat_json_exporter_export_with_delta(fieldstat_json_exporter, instance, delta, &TEST_TIMEVAL, &TEST_TIMEVAL);
    printf("delta_with_two_instance_different_cell :\n%s\n", json);
    cJSON *root_arr = cJSON_Parse(json);
    free(json);

    cJSON *root = cJSON_GetArrayItem(root_arr, 0);
    cJSON *metrics = cJSON_GetObjectItem(root, "fields");
    cJSON *metrics_delta = cJSON_GetObjectItem(root, "fields_delta");
    cJSON *counter = cJSON_GetObjectItem(metrics, "counter");
    EXPECT_EQ(counter->valueint, 123);
    cJSON *counter_delta = cJSON_GetObjectItem(metrics_delta, "counter");
    EXPECT_TRUE(counter_delta == NULL);
    cJSON *timestamp = cJSON_GetObjectItem(root, "timestamp_ms");
    EXPECT_EQ(timestamp->valueint, TEST_TIMEVAL_LONG);
    cJSON *timestamp_delta = cJSON_GetObjectItem(root, "timestamp_ms_delta");
    EXPECT_EQ(timestamp_delta->valueint, TEST_TIMEVAL_LONG);

    root = cJSON_GetArrayItem(root_arr, 1);
    metrics = cJSON_GetObjectItem(root, "fields");
    metrics_delta = cJSON_GetObjectItem(root, "fields_delta");
    counter = cJSON_GetObjectItem(metrics, "counter");
    EXPECT_EQ(counter->valueint, 1);
    counter_delta = cJSON_GetObjectItem(metrics_delta, "counter");
    EXPECT_EQ(counter_delta->valueint, 1);
    timestamp = cJSON_GetObjectItem(root, "timestamp_ms");
    EXPECT_EQ(timestamp->valueint, TEST_TIMEVAL_LONG);
    timestamp_delta = cJSON_GetObjectItem(root, "timestamp_ms_delta");
    EXPECT_EQ(timestamp_delta->valueint, TEST_TIMEVAL_LONG);

    cJSON_Delete(root_arr);
    fieldstat_json_exporter_free(fieldstat_json_exporter);
    fieldstat_free(instance);
    fieldstat_free(delta);
}

TEST(export_test, export_flat_null_with_only_global_tag) {
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new();
    fieldstat_json_exporter_set_global_tag(fieldstat_json_exporter, TEST_TAG_GLOBAL, 3);

    char **out_array = NULL;
    size_t out_array_len = 0;
    fieldstat_json_exporter_export_flat(fieldstat_json_exporter, instance, &TEST_TIMEVAL, &out_array, &out_array_len);
    EXPECT_EQ(out_array_len, 0);
    EXPECT_EQ(out_array, nullptr);

    fieldstat_json_exporter_free(fieldstat_json_exporter);
    free(out_array);
    fieldstat_free(instance);
}

TEST(export_test, export_flat_many_tags_many_cell) {
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, &TEST_TAG_STRING, 1, SAMPLING_MODE_COMPREHENSIVE, 3);
    int metric_id = fieldstat_register_counter(instance, cube_id, "counter metric");


    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_INT, 1, 123);  
    fieldstat_counter_incrby(instance, cube_id, metric_id, &TEST_TAG_DOUBLE, 1, 321);  

    struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new();
    fieldstat_json_exporter_set_global_tag(fieldstat_json_exporter, TEST_TAG_GLOBAL, 3);

    char **out_array = NULL;
    size_t out_array_len = 0;
    fieldstat_json_exporter_export_flat(fieldstat_json_exporter, instance, &TEST_TIMEVAL, &out_array, &out_array_len);
    EXPECT_EQ(out_array_len, 2);

    cJSON *root = cJSON_Parse(out_array[0]);
    Fieldstat_tag_list_wrapper cell_dimension = Fieldstat_tag_list_wrapper(&TEST_TAG_LIST_INT);
    Fieldstat_tag_list_wrapper cube_dimension = Fieldstat_tag_list_wrapper(&TEST_TAG_LIST_STRING);
    Fieldstat_tag_list_wrapper cell_dimension2 = Fieldstat_tag_list_wrapper(&TEST_TAG_LIST_DOUBLE);

    test_check_if_tag_list_is_in_json(root, &cell_dimension);
    test_check_if_global_tag_is_in_json(root);
    test_check_if_tag_list_is_in_json(root, &cube_dimension);
    cJSON *metrics = cJSON_GetObjectItem(root, "counter metric");
    EXPECT_EQ(metrics->valueint, 123);

    cJSON_Delete(root);
    root = cJSON_Parse(out_array[1]);
    test_check_if_tag_list_is_in_json(root, &cell_dimension2);
    test_check_if_global_tag_is_in_json(root);
    test_check_if_tag_list_is_in_json(root, &cube_dimension);
    metrics = cJSON_GetObjectItem(root, "counter metric");
    EXPECT_EQ(metrics->valueint, 321);

    cJSON_Delete(root);
    fieldstat_free(instance);
    fieldstat_json_exporter_free(fieldstat_json_exporter);
    for (size_t i = 0; i < out_array_len; i++) {
        free(out_array[i]);
    }
    free(out_array);
}

extern "C" {
extern int add_object_to_json_array_start(char *buf, int buf_len);
extern int add_object_to_json_array_end(char **buf, int buf_len, int start);
extern int add_object_to_json_array(char **buf, int *buf_len, int start, const char *str);
}

TEST(export_unit_test, test_add_json_length_is_on_margin_4096)
{
    int buf_len = 4096; // the initial buffer max len is 4096
    char *buf = (char *)malloc(buf_len);
    char str[4096 + 1];
    memset(str, 'a', 4096);
    str[4096] = '\0';

    int used_len = add_object_to_json_array_start(buf, buf_len);
    used_len = add_object_to_json_array(&buf, &buf_len, used_len, str);
    used_len = add_object_to_json_array_end(&buf, buf_len, used_len);

    EXPECT_EQ(used_len, 4096 + 3);
    std::string target = "[" + std::string(str) + "]";
    EXPECT_STREQ(buf, target.c_str());

    free(buf);
}

TEST(export_unit_test, test_add_json_length_is_on_margin_4095)
{
    int buf_len = 4096; // the initial buffer max len is 4096
    char *buf = (char *)malloc(buf_len);
    char str[4096];
    memset(str, 'a', 4095);
    str[4095] = '\0';

    int used_len = add_object_to_json_array_start(buf, buf_len);
    used_len = add_object_to_json_array(&buf, &buf_len, used_len, str);
    used_len = add_object_to_json_array_end(&buf, buf_len, used_len);

    std::string target = "[" + std::string(str) + "]";
    EXPECT_STREQ(buf, target.c_str());

    free(buf);
}

TEST(export_unit_test, json_writer_length_is_on_margin_4095_int)
{
    char str[4090];
    memset(str, 'a', 4089); // 4089 + ':' + '"'*2 + ‘{’ + ‘}’ = 4095
    str[4089] = '\0';

    struct json_writer *writer = json_writer_init();
    json_writer_start_map(writer);
    json_writer_int_field(writer, str, 6);
    json_writer_end_map(writer);
    char *result;
    size_t len;
    json_writer_finish(writer, &result, &len);
    EXPECT_EQ(len, 4095);
    std::string target = "{\"" + std::string(str) + "\":6}";
    EXPECT_STREQ(result, target.c_str());

    free(result);
}

TEST(export_unit_test, json_writer_length_is_on_margin_4096_string)
{
    char str[4090];
    memset(str, 'a', 4087); // 4087 + ':' + '"'*4 + ‘{’ + ‘}’ + int1 = 4096
    str[4087] = '\0';

    struct json_writer *writer = json_writer_init();
    json_writer_start_map(writer);
    json_writer_str_field(writer, str, "6", 1);
    json_writer_end_map(writer);
    char *result;
    size_t len;
    json_writer_finish(writer, &result, &len);
    EXPECT_EQ(len, 4095);
    std::string target = "{\"" + std::string(str) + "\":\"6\"}";
    EXPECT_STREQ(result, target.c_str());

    free(result);
}

void init_hll_standard_oper()
{
    g_hll_standard = hyperloglog_new(12);
    for (size_t i = 0; i < OPER_NUM; i++) {
        std::string added_tmp = std::to_string(i);
        g_hll_standard_oper[i] = added_tmp;
        hyperloglog_add(g_hll_standard, added_tmp.c_str(), added_tmp.size());
    }
}

void init_histogram_standard_oper()
{
    hdr_init(1, 20000, 3, &g_histogram_standard);
    for (size_t i = 0; i < OPER_NUM; i++) {
        g_histogram_standard_oper[i] = i + 1;
        hdr_record_value(g_histogram_standard, i + 1);
    }
}



int main(int argc, char *argv[]) 
{
    init_hll_standard_oper();
    init_histogram_standard_oper();

	testing::InitGoogleTest(&argc, argv);
    // testing::GTEST_FLAG(filter) = "*flat*";
	int ret = RUN_ALL_TESTS();

    hyperloglog_free(g_hll_standard);
    hdr_close(g_histogram_standard);
    return ret;
}
