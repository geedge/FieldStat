// TODO
// 不同tag组合
// 空的instance
// realloc for field
//  超长tag，测试动态key的realloc

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
std::string g_cardinality_standard_oper[OPER_NUM];
long long g_histogram_standard_oper[OPER_NUM];
struct ST_hyperloglog *g_cardinality_standard;
struct hdr_histogram *g_histogram_standard;
struct heavy_keeper *g_topk_standard;
#define TEST_TOPK_STANDARD_K 3

TEST(export_test, network_blob_test_for_histogram)
{
    struct fieldstat *instance = fieldstat_new();

    int id = fieldstat_register_histogram(instance, "test_histogram",
        g_histogram_standard->lowest_discernible_value, g_histogram_standard->highest_trackable_value, g_histogram_standard->significant_figures);

    for (size_t i = 0; i < OPER_NUM; i++) {
        fieldstat_histogram_record(instance, id, g_histogram_standard_oper[i], &TEST_TAG_INT, 1);
    }
    struct fieldstat_tag_list tag_list = {.tag = (struct fieldstat_tag *)&TEST_TAG_INT, .n_tag = 1};
    char *blob = NULL;
    size_t blob_size = 0;
    fieldstat_get_serialized_blob(instance, id, &tag_list, &blob, &blob_size);

    struct hdr_histogram *histogram = NULL;
    hdr_log_decode(&histogram, blob, blob_size);
    EXPECT_TRUE(histogram != NULL);
    EXPECT_EQ(histogram->lowest_discernible_value, g_histogram_standard->lowest_discernible_value);
    EXPECT_EQ(histogram->highest_trackable_value, g_histogram_standard->highest_trackable_value);
    EXPECT_EQ(histogram->significant_figures, g_histogram_standard->significant_figures);
    EXPECT_TRUE(memcmp(histogram->counts, g_histogram_standard->counts, sizeof(int64_t) * histogram->counts_len) == 0);

    free(blob);
    fieldstat_free(instance);
    hdr_close(histogram);
}

#define BigLittleSwap32(A)        ((((uint32_t)(A) & 0xff000000) >> 24) | \
                                                       (((uint32_t)(A) & 0x00ff0000) >> 8) | \
                                                       (((uint32_t)(A) & 0x0000ff00) << 8) | \
                                                       (((uint32_t)(A) & 0x000000ff) << 24))
TEST(export_test, network_blob_test_for_cardinality)
{
    struct fieldstat *instance = fieldstat_new();

    int id = fieldstat_register_cardinality(instance, "test_cardinality", g_cardinality_standard->cfg.precision);

    for (size_t i = 0; i < OPER_NUM; i++) {
        fieldstat_cardinality_add(instance, id, g_cardinality_standard_oper[i].c_str(), g_cardinality_standard_oper[i].length(), &TEST_TAG_INT, 1);
    }
    struct fieldstat_tag_list tag_list = {.tag = (struct fieldstat_tag *)&TEST_TAG_INT, .n_tag = 1};
    char *blob = NULL;
    size_t blob_size = 0;
    fieldstat_get_serialized_blob(instance, id, &tag_list, &blob, &blob_size);
    size_t dec_size = 0;
    unsigned char *dec = b64_decode_ex(blob, blob_size, &dec_size);
    free(blob);

    unsigned char version;
    memcpy(&version, dec, sizeof(unsigned char));
    EXPECT_EQ(version, 1);
    unsigned char precision;
    memcpy(&precision, dec + sizeof(unsigned char), sizeof(unsigned char));
    EXPECT_EQ(precision, g_cardinality_standard->cfg.precision);

    int num_reg = NUM_REG(precision);
    // Get the full words required
    int words = INT_CEIL(num_reg, REG_PER_WORD);
    size_t reg_size = words * sizeof(uint32_t);

    uint32_t *recovered_reg = (uint32_t *)malloc(reg_size);
    memcpy(recovered_reg, dec + 2, reg_size);
    for (int i = 0; i < words; i++) {
        uint32_t tmp = BigLittleSwap32(recovered_reg[i]);
        EXPECT_EQ(tmp, g_cardinality_standard->registers[i]);
    }

    free(dec);
    fieldstat_free(instance);
    free(recovered_reg);
}

void test_check_if_tag_list_equal(cJSON *tag_obj, const Fieldstat_tag_list_wrapper *benchmark)
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
                EXPECT_EQ(tag_val->valuedouble, benchmark->get_tag()[tag_id].value_double);
            }
        }
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
        tags[i]->print_tag_list();
    }
}

cJSON *test_exporter_extract_results(struct fieldstat *instance)
{
    struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new(instance);
    char *json_string = fieldstat_json_exporter_export(fieldstat_json_exporter);
    cJSON *root_arr = cJSON_Parse(json_string);
    printf("test, fieldstat_json_exporter_export json_string: %s\n", json_string);
    free(json_string);

    return root_arr;
}

TEST(export_test, cjson_export_with_fixed_tag_and_many_metrics)
{
    // new instance
    struct fieldstat *instance = fieldstat_new();
    // register 4 metrics
    int id_counter = fieldstat_register_counter(instance, "counter");
    int id_gauge = fieldstat_register_cardinality(instance, "gauge", g_cardinality_standard->cfg.precision);
    int id_histogram = fieldstat_register_histogram(instance, "histogram",
        g_histogram_standard->lowest_discernible_value, g_histogram_standard->highest_trackable_value, g_histogram_standard->significant_figures);
    char *blob_gauge_benchmark = NULL;
    char *blob_histogram_benchmark = NULL;
    size_t size_dummy = 0;
    ST_hyperloglog_serialize_for_networking(g_cardinality_standard, &blob_gauge_benchmark, &size_dummy);
    hdr_log_encode(g_histogram_standard, &blob_histogram_benchmark);

    const int tag_list_num = 3;
    Fieldstat_tag_list_wrapper *tags[tag_list_num];
    fill_random_tag(tags, tag_list_num);

    for (int j = 0; j < tag_list_num; j++) {
        const struct fieldstat_tag *tag_tmp = tags[j]->get_tag();
        size_t tag_count = tags[j]->get_tag_count();
        fieldstat_counter_incrby(instance, id_counter, 1, tag_tmp, tag_count);
        for (size_t i = 0; i < OPER_NUM; i++){
            fieldstat_cardinality_add(instance, id_gauge, g_cardinality_standard_oper[i].c_str(), g_cardinality_standard_oper[i].length(), tag_tmp, tag_count);
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
        test_check_if_tag_list_equal(tag, tags[i]);

        // check metrics
        cJSON *metrics = cJSON_GetObjectItem(root, "fields");
        EXPECT_NE(metrics, nullptr);
        cJSON *counter = cJSON_GetObjectItem(metrics, "counter");
        EXPECT_EQ(counter->valueint, 1);
        cJSON *gauge = cJSON_GetObjectItem(metrics, "gauge");
        EXPECT_STREQ(gauge->valuestring, blob_gauge_benchmark);
        cJSON *histogram = cJSON_GetObjectItem(metrics, "histogram");
        EXPECT_STREQ(histogram->valuestring, blob_histogram_benchmark);
    }

    for (int i = 0; i < 3; i++) {
        delete tags[i];
    }

    fieldstat_free(instance);
    free(blob_gauge_benchmark);
    free(blob_histogram_benchmark);
    cJSON_Delete(root_arr);
}

void topk_standard_oper(std::function<void(const char *, size_t, int)> &topk_add)
{
    for (size_t i = 0; i < OPER_NUM; i++) {
        int tmp;
        if (i < 3000) {
            tmp = i / 1000; // 0, 1, 2 added for the most of times
        } else {
            tmp = rand() % 1000; // 0-999 added for mouse flow
        }
        std::string added_tmp = std::to_string(tmp);
        topk_add(added_tmp.c_str(), added_tmp.size(), 1);
    }
}

void topk_init(struct fieldstat *instance, Fieldstat_tag_list_wrapper *tags[], size_t tags_count)
{
    int id_topk = fieldstat_register_topk(instance, "topk", TEST_TOPK_STANDARD_K);

    for (size_t j = 0; j < tags_count; j++) {
        const struct fieldstat_tag *tag_tmp = tags[j]->get_tag();
        size_t tag_count = tags[j]->get_tag_count();
        std::function<void(const char *, size_t, int)> topk_add =
            std::bind(fieldstat_topk_add, instance, id_topk, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, tag_tmp, tag_count);

        topk_standard_oper(topk_add);
    }
}

TEST(export_test, cjson_export_with_fixed_tag_only_topk)
{
    std::string topk_tag_key(JSON_EXPORTER_TOPK_DEMENSION);
    // gen 2 tag_lists
    const size_t tags_count = 2;
    Fieldstat_tag_list_wrapper *tags[tags_count];
    fill_random_tag(tags, tags_count);


    struct fieldstat *instance = fieldstat_new();
    topk_init(instance, tags, tags_count);

    // export test
    cJSON *root_arr = test_exporter_extract_results(instance);
    int arr_num = cJSON_GetArraySize(root_arr);
    EXPECT_EQ(arr_num, TEST_TOPK_STANDARD_K * tags_count);

    for (int i = 0; i < arr_num; i++)
    {
        cJSON *root = cJSON_GetArrayItem(root_arr, i);

        // check tag
        cJSON *tag = cJSON_GetObjectItem(root, "tags");
        EXPECT_NE(tag, nullptr);
        
        cJSON *topk_dimension = cJSON_GetObjectItem(tag, topk_tag_key.c_str());
        EXPECT_NE(topk_dimension, nullptr);
        EXPECT_TRUE(strcmp(topk_dimension->valuestring, "0") == 0 || strcmp(topk_dimension->valuestring, "1") == 0 || strcmp(topk_dimension->valuestring, "2") == 0
            ); // 0, 1, 2 added for the most of times(refer to init_topk_standard_oper)

        test_check_if_tag_list_equal(tag, tags[i / TEST_TOPK_STANDARD_K]);

        // check metrics
        cJSON *metrics = cJSON_GetObjectItem(root, "fields");
        EXPECT_NE(metrics, nullptr);
        cJSON *topk = cJSON_GetObjectItem(metrics, "topk");
        EXPECT_NE(topk, nullptr);
        printf("the topk value is : %d\n", topk->valueint);
        EXPECT_NEAR(topk->valueint, 1000, 20);
    }

    for (size_t i = 0; i < tags_count; i++) {
        delete tags[i];
    }

    fieldstat_free(instance);
    cJSON_Delete(root_arr);
}

void init_cardinality_standard_oper()
{
    g_cardinality_standard = ST_hyperloglog_new(12);
    for (size_t i = 0; i < OPER_NUM; i++) {
        std::string added_tmp = std::to_string(i);
        g_cardinality_standard_oper[i] = added_tmp;
        ST_hyperloglog_add(g_cardinality_standard, added_tmp.c_str(), added_tmp.size());
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
    init_cardinality_standard_oper();
    init_histogram_standard_oper();
	testing::InitGoogleTest(&argc, argv);
	int ret = RUN_ALL_TESTS();

    ST_hyperloglog_free(g_cardinality_standard);
    hdr_close(g_histogram_standard);
    return ret;
}
