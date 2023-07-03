
// 大规模序列化测试

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
#include "heavy_keeper.h"

const size_t OPER_NUM = 10000;
std::string g_hll_standard_oper[OPER_NUM];
long long g_histogram_standard_oper[OPER_NUM];
struct ST_hyperloglog *g_hll_standard;
struct hdr_histogram *g_histogram_standard;
#define TEST_TOPK_STANDARD_K 3
const struct fieldstat_tag TEST_TAG_GLOBAL[3] = {
    {"test_tag_global 1", .type = TAG_INTEGER, .value_int = 1},
    {"test_tag_global 2", .type = TAG_DOUBLE, .value_double = 2.2},
    {"test_tag_global 3", .type = TAG_CSTRING, .value_str = {'s','t','r',' ','3','\0'}}};

// TEST(export_test, network_blob_test_for_histogram)
// {
//     struct fieldstat *instance = fieldstat_new();

//     int id = fieldstat_register_histogram(instance, "test_histogram",
//         g_histogram_standard->lowest_discernible_value, g_histogram_standard->highest_trackable_value, g_histogram_standard->significant_figures);

//     for (size_t i = 0; i < OPER_NUM; i++) {
//         fieldstat_histogram_record(instance, id, g_histogram_standard_oper[i], &TEST_TAG_INT, 1);
//     }
//     struct fieldstat_tag_list tag_list = {.tag = (struct fieldstat_tag *)&TEST_TAG_INT, .n_tag = 1};
//     char *blob = NULL;
//     size_t blob_size = 0;
//     fieldstat_get_serialized_blob(instance, id, &tag_list, &blob, &blob_size);

//     struct hdr_histogram *histogram = NULL;
//     hdr_log_decode(&histogram, blob, blob_size);
//     EXPECT_TRUE(histogram != NULL);
//     EXPECT_EQ(histogram->lowest_discernible_value, g_histogram_standard->lowest_discernible_value);
//     EXPECT_EQ(histogram->highest_trackable_value, g_histogram_standard->highest_trackable_value);
//     EXPECT_EQ(histogram->significant_figures, g_histogram_standard->significant_figures);
//     EXPECT_TRUE(memcmp(histogram->counts, g_histogram_standard->counts, sizeof(int64_t) * histogram->counts_len) == 0);

//     free(blob);
//     fieldstat_free(instance);
//     hdr_close(histogram);
// }

// #define BigLittleSwap32(A)        ((((uint32_t)(A) & 0xff000000) >> 24) | \
//                                                        (((uint32_t)(A) & 0x00ff0000) >> 8) | \
//                                                        (((uint32_t)(A) & 0x0000ff00) << 8) | \
//                                                        (((uint32_t)(A) & 0x000000ff) << 24))
// TEST(export_test, network_blob_test_for_hll)
// {
//     struct fieldstat *instance = fieldstat_new();

//     int id = fieldstat_register_hll(instance, "test_hll", g_hll_standard->cfg.precision);

//     for (size_t i = 0; i < OPER_NUM; i++) {
//         fieldstat_hll_add(instance, id, g_hll_standard_oper[i].c_str(), g_hll_standard_oper[i].length(), &TEST_TAG_INT, 1);
//     }
//     struct fieldstat_tag_list tag_list = {.tag = (struct fieldstat_tag *)&TEST_TAG_INT, .n_tag = 1};
//     char *blob = NULL;
//     size_t blob_size = 0;
//     fieldstat_get_serialized_blob(instance, id, &tag_list, &blob, &blob_size);
//     size_t dec_size = 0;
//     unsigned char *dec = b64_decode_ex(blob, blob_size, &dec_size);
//     free(blob);

//     unsigned char version;
//     memcpy(&version, dec, sizeof(unsigned char));
//     EXPECT_EQ(version, 1);
//     unsigned char precision;
//     memcpy(&precision, dec + sizeof(unsigned char), sizeof(unsigned char));
//     EXPECT_EQ(precision, g_hll_standard->cfg.precision);

//     int num_reg = NUM_REG(precision);
//     // Get the full words required
//     int words = INT_CEIL(num_reg, REG_PER_WORD);
//     size_t reg_size = words * sizeof(uint32_t);

//     uint32_t *recovered_reg = (uint32_t *)malloc(reg_size);
//     memcpy(recovered_reg, dec + 2, reg_size);
//     for (int i = 0; i < words; i++) {
//         uint32_t tmp = BigLittleSwap32(recovered_reg[i]);
//         EXPECT_EQ(tmp, g_hll_standard->registers[i]);
//     }

//     free(dec);
//     fieldstat_free(instance);
//     free(recovered_reg);
// }

void test_check_if_tag_list_is_in_json(cJSON *tag_obj, const Fieldstat_tag_list_wrapper *benchmark)
{
    printf("going to parse tag, num: %zu\n", benchmark->get_tag_count());
    for (size_t tag_id = 0; tag_id < benchmark->get_tag_count(); tag_id++) {
        cJSON *tag_val = cJSON_GetObjectItem(tag_obj, benchmark->get_tag()[tag_id].key);
        printf("the tag key is : %s\n", benchmark->get_tag()[tag_id].key);
        EXPECT_NE(tag_val, nullptr);
        if (tag_val->type == cJSON_String) {
            EXPECT_STREQ(tag_val->valuestring, benchmark->get_tag()[tag_id].value_str);
        } else {
            if (benchmark->get_tag()[tag_id].type == TAG_INTEGER) {
                EXPECT_EQ(tag_val->valueint, benchmark->get_tag()[tag_id].value_int);
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

void test_check_if_topk_tag_in_json(cJSON *tag_obj, const char *name)
{
    cJSON *topk_dimension = cJSON_GetObjectItem(tag_obj, name);
    EXPECT_NE(topk_dimension, nullptr);
    bool match = false;
    for (int i = 0; i < TEST_TOPK_STANDARD_K; i ++) {
        std::string standard_flow = "flow id: " + std::to_string(i);
        if (strcmp(topk_dimension->valuestring, standard_flow.c_str()) == 0) {
            match = true;
            break;
        }
    }
    EXPECT_TRUE(match);
}

void fill_random_tag(Fieldstat_tag_list_wrapper *tags[], int tag_list_num)
{
    std::uniform_int_distribution<int> dist(1,100);
    std::mt19937 rng();

    for (int i = 0; i < tag_list_num; i++)
    {
        Fieldstat_tag_list_wrapper *tmp = new Fieldstat_tag_list_wrapper(dist, i + 1);
        tags[i] = tmp;
        tags[i]->print_tag_list();
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

void topk_standard_oper(std::function<void(const char *, size_t, unsigned int *)> &topk_add, size_t metric_arr_num, unsigned int test_expected_big_count)
{
    for (size_t i = 0; i < OPER_NUM; i++) {
        int tmp;
        if (i < test_expected_big_count * TEST_TOPK_STANDARD_K) {
            tmp = i / test_expected_big_count; // 0 ~ TEST_TOPK_STANDARD_K added for the most of times
        } else {
            tmp = rand() % 100 + TEST_TOPK_STANDARD_K + 1; // the rest is randomly generated, plus TEST_TOPK_STANDARD_K so that its count is larger than TEST_TOPK_STANDARD_K
        }
        std::string added_tmp = std::string("flow id: ") + std::to_string(tmp);
        unsigned int counts[metric_arr_num];
        for (size_t j = 0; j < metric_arr_num; j++) {
            counts[j] = 1 + j;
        }
        topk_add(added_tmp.c_str(), added_tmp.size(), counts);

    }
}

void topk_init(struct fieldstat *instance, Fieldstat_tag_list_wrapper *tags[], size_t tags_count, const char *topk_metric_name[], size_t arr_num, unsigned int test_expected_big_count[])
{
    int id_topk = fieldstat_register_topk(instance, "topk", TEST_TOPK_STANDARD_K, topk_metric_name, arr_num, 0);

    for (size_t j = 0; j < tags_count; j++) {
        const struct fieldstat_tag *tag_tmp = tags[j]->get_tag();
        size_t tag_count = tags[j]->get_tag_count();
        std::function<void(const char *, size_t, unsigned int *)> topk_add =
            std::bind(fieldstat_topk_add, instance, id_topk,
                std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, tag_tmp, tag_count);

        topk_standard_oper(topk_add, arr_num, test_expected_big_count[j]);
    }
}

TEST(export_test, cjson_export_with_fixed_tag_and_many_metrics)
{
    // new instance
    struct fieldstat *instance = fieldstat_new();
    // register 4 metrics
    int id_counter = fieldstat_register_counter(instance, "counter");
    int id_gauge = fieldstat_register_hll(instance, "gauge", g_hll_standard->cfg.precision);
    int id_histogram = fieldstat_register_histogram(instance, "histogram",
        g_histogram_standard->lowest_discernible_value, g_histogram_standard->highest_trackable_value, g_histogram_standard->significant_figures);

    const int tag_list_num = 3;
    Fieldstat_tag_list_wrapper *tags[tag_list_num];
    fill_random_tag(tags, tag_list_num);

    for (int j = 0; j < tag_list_num; j++) {
        const struct fieldstat_tag *tag_tmp = tags[j]->get_tag();
        size_t tag_count = tags[j]->get_tag_count();
        fieldstat_counter_incrby(instance, id_counter, 1, tag_tmp, tag_count);
        for (size_t i = 0; i < OPER_NUM; i++){
            fieldstat_hll_add(instance, id_gauge, g_hll_standard_oper[i].c_str(), g_hll_standard_oper[i].length(), tag_tmp, tag_count);
            fieldstat_histogram_record(instance, id_histogram, g_histogram_standard_oper[i], tag_tmp, tag_count);
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

TEST(export_test, cjson_export_with_fixed_tag_only_one_topk)
{
    // gen 2 tag_lists
    const size_t tags_count = 2;
    Fieldstat_tag_list_wrapper *tags[tags_count];
    fill_random_tag(tags, tags_count);

    struct fieldstat *instance = fieldstat_new();
    const char *topk_metric_name[] = {"metric_name1"};
    unsigned int test_expected_big_count[tags_count] = {1000, 1200};
    topk_init(instance, tags, tags_count, topk_metric_name, 1, test_expected_big_count);
    printf("init done\n");

    // export test
    cJSON *root_arr = test_exporter_extract_results(instance);
    int arr_num = cJSON_GetArraySize(root_arr);
    EXPECT_EQ(arr_num, TEST_TOPK_STANDARD_K * tags_count);

    for (int i = 0; i < arr_num; i++)
    {
        int field_id = i / TEST_TOPK_STANDARD_K;
        cJSON *root = cJSON_GetArrayItem(root_arr, i);

        // check tag
        cJSON *tag = cJSON_GetObjectItem(root, "tags");
        EXPECT_NE(tag, nullptr);
        
        test_check_if_tag_list_is_in_json(tag, tags[field_id]);
        test_check_if_topk_tag_in_json(tag, "topk");

        // check metrics
        cJSON *metrics = cJSON_GetObjectItem(root, "fields");
        EXPECT_NE(metrics, nullptr);
        test_check_if_metric_topk_correct(metrics, topk_metric_name, test_expected_big_count[field_id], 1);
    }

    for (size_t i = 0; i < tags_count; i++) {
        delete tags[i];
    }

    fieldstat_free(instance);
    cJSON_Delete(root_arr);
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
    const char *topk_metric_name[] = {"topk_income_bytes"};
    (void)fieldstat_register_topk(instance, "topk", TEST_TOPK_STANDARD_K, topk_metric_name, 1, 0);
    (void)fieldstat_register_counter(instance, "counter");
    (void)fieldstat_register_hll(instance, "gauge", g_hll_standard->cfg.precision);
    (void)fieldstat_register_histogram(instance, "histogram",
        g_histogram_standard->lowest_discernible_value, g_histogram_standard->highest_trackable_value, g_histogram_standard->significant_figures);

    // add global tag
    cJSON *root_arr = test_exporter_extract_results_with_standard_global(instance);
    EXPECT_EQ(root_arr, nullptr);

    fieldstat_free(instance);
}

TEST(export_test, skip_two_empty_field_and_export_last_two_with_global_tag_given_topk_of_2_metrics)
{
    struct fieldstat *instance = fieldstat_new();
    (void)fieldstat_register_hll(instance, "gauge", g_hll_standard->cfg.precision);
    (void)fieldstat_register_counter(instance, "counter");
    int id_histogram = fieldstat_register_histogram(instance, "histogram",
        g_histogram_standard->lowest_discernible_value, g_histogram_standard->highest_trackable_value, g_histogram_standard->significant_figures);

    const int tag_num = 1;
    Fieldstat_tag_list_wrapper *tags[tag_num];
    fill_random_tag(tags, tag_num);
    int topk_metric_num = 2;
    const char *topk_metric_name[topk_metric_num] = {"metric_name1", "metric_name2"};
    unsigned int test_expected_big_count[tag_num] = {1000};
    topk_init(instance, tags, 1, topk_metric_name, 2, test_expected_big_count);
    for (size_t i = 0; i < OPER_NUM; i++){
        fieldstat_histogram_record(instance, id_histogram, g_histogram_standard_oper[i], tags[0]->get_tag(), tags[0]->get_tag_count());
    }

    // export test
    cJSON *root_arr = test_exporter_extract_results_with_standard_global(instance);
    int arr_num = cJSON_GetArraySize(root_arr);
    EXPECT_EQ(arr_num, 1 * (1 + TEST_TOPK_STANDARD_K)); // 1 histogram, 1 topk with K = 3

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

        test_check_if_metric_topk_correct(metrics, topk_metric_name, test_expected_big_count[0], 2);

        test_check_if_topk_tag_in_json(tag, "topk");
    }

    delete tags[0];

    fieldstat_free(instance);
    cJSON_Delete(root_arr);
}

// TODO: 检查没有globaltag，且有写入的metric
TEST(export_test, export_and_then_skip_when_4_topk_field_registered_given_global_tags) {

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
