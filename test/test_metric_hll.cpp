#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <math.h>

#include "fieldstat.h"
#include "utils.hpp"

struct fieldstat *test_init_standard_instance_one_cube_one_metric_one_cell_hll(bool is_gauge = false)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    EXPECT_EQ(cube_id, 0);

    int metric_id = fieldstat_register_hll(instance, cube_id, "czz_test hll metric", 10);
    EXPECT_EQ(metric_id, 0);

    return instance;
}

void test_assert_standard_instance(const struct fieldstat *instance)
{
    int *ret_cube_id_arr = NULL;
    int n_cube = 0;
    fieldstat_get_cubes(instance, &ret_cube_id_arr, &n_cube);
    EXPECT_EQ(n_cube, 1);
    int ret_cell_id = ret_cube_id_arr[0];
    free(ret_cube_id_arr);
    EXPECT_EQ(ret_cell_id, 0);
    const char *name = fieldstat_get_metric_name(instance, ret_cell_id, 0);
    EXPECT_STREQ(name, "czz_test hll metric");

    struct field_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance, 0, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(tag_list->n_field, 1);
    EXPECT_STREQ(tag_list->field[0].key, TEST_TAG_INT.key);
    
    int *metric_id = NULL;
    size_t n_metric = 0;
    fieldstat_get_metric_in_cell(instance, 0, &tag_list[0], &metric_id, &n_metric);
    EXPECT_EQ(n_metric, 1);
    EXPECT_EQ(metric_id[0], 0);
    free(metric_id);

    fieldstat_tag_list_arr_free(tag_list, n_cell);
}

double my_fieldstat_hll_get(const struct fieldstat *instance, int cube_id, int metric_id)
{
    double ret = 0;
    fieldstat_hll_get(instance, cube_id, &TEST_TAG_LIST_INT, metric_id,  &ret);
    return ret;
}

TEST(metric_test_hll, simple_register_and_query)
{
    struct fieldstat *instance = test_init_standard_instance_one_cube_one_metric_one_cell_hll();

    fieldstat_hll_add(instance, 0, 0, &TEST_TAG_INT, 1, "hello", 5);
    fieldstat_hll_add(instance, 0, 0, &TEST_TAG_INT, 1, "wor", 3);
    fieldstat_hll_add(instance, 0, 0, &TEST_TAG_INT, 1, "world", 5);

    test_assert_standard_instance(instance);
    EXPECT_NEAR(my_fieldstat_hll_get(instance, 0, 0), 3, 0.5);

    fieldstat_free(instance);
}

TEST(metric_test_hll, add_with_tags) {
    struct fieldstat *instance = test_init_standard_instance_one_cube_one_metric_one_cell_hll();

    fieldstat_hll_add_field(instance, 0, 0, &TEST_TAG_INT, 1, &TEST_TAG_INT, 1);
    fieldstat_hll_add_field(instance, 0, 0, &TEST_TAG_INT, 1, &TEST_TAG_DOUBLE, 1);
    fieldstat_hll_add_field(instance, 0, 0, &TEST_TAG_INT, 1, &TEST_TAG_STRING, 1);
    
    test_assert_standard_instance(instance);
    EXPECT_NEAR(my_fieldstat_hll_get(instance, 0, 0), 3, 0.5);

    fieldstat_free(instance);
}

TEST(metric_test_hll, merge)
{
    struct fieldstat *instance = test_init_standard_instance_one_cube_one_metric_one_cell_hll();
    fieldstat_hll_add(instance, 0, 0, &TEST_TAG_INT, 1, "hello", 5);
    fieldstat_hll_add(instance, 0, 0, &TEST_TAG_INT, 1, "wor", 3);
    fieldstat_hll_add(instance, 0, 0, &TEST_TAG_INT, 1, "world", 5);

    struct fieldstat *instance_total = fieldstat_new();
    fieldstat_merge(instance_total, instance);

    // query
    test_assert_standard_instance(instance_total);

    EXPECT_NEAR(my_fieldstat_hll_get(instance, 0, 0), 3, 0.5);

    fieldstat_free(instance);
    fieldstat_free(instance_total);
}

TEST(metric_test_hll, merge_twice_with_reset)
{
    struct fieldstat *instance = test_init_standard_instance_one_cube_one_metric_one_cell_hll();
    fieldstat_hll_add(instance, 0, 0, &TEST_TAG_INT, 1, "hello", 5);
    fieldstat_hll_add(instance, 0, 0, &TEST_TAG_INT, 1, "wor", 3);

    struct fieldstat *instance_total = fieldstat_new();
    fieldstat_merge(instance_total, instance);

    fieldstat_reset(instance);
    fieldstat_hll_add(instance, 0, 0, &TEST_TAG_INT, 1, "world", 5);
    fieldstat_merge(instance_total, instance);

    test_assert_standard_instance(instance_total);
    EXPECT_NEAR(my_fieldstat_hll_get(instance_total, 0, 0), 3, 0.5);

    fieldstat_free(instance);
    fieldstat_free(instance_total);
}


#define BigLittleSwap32(A)        ((((uint32_t)(A) & 0xff000000) >> 24) | \
                                                       (((uint32_t)(A) & 0x00ff0000) >> 8) | \
                                                       (((uint32_t)(A) & 0x0000ff00) << 8) | \
                                                       (((uint32_t)(A) & 0x000000ff) << 24))
#include "base64/b64.h"
#include "hyperloglog.h"
TEST(metric_test_hll, serialize_with_b64_and_query)
{
    struct fieldstat *instance = test_init_standard_instance_one_cube_one_metric_one_cell_hll();
    fieldstat_hll_add(instance, 0, 0, &TEST_TAG_INT, 1, "hello", 5);
    fieldstat_hll_add(instance, 0, 0, &TEST_TAG_INT, 1, "wor", 3);
    fieldstat_hll_add(instance, 0, 0, &TEST_TAG_INT, 1, "world", 5);

    char *blob = NULL;
    size_t blob_len = 0;
    fieldstat_get_serialized_blob(instance, 0, 0, &TEST_TAG_LIST_INT, &blob, &blob_len);
    size_t dec_size = 0;
    unsigned char *dec = b64_decode_ex(blob, blob_len, &dec_size);

    unsigned char version;
    memcpy(&version, dec, sizeof(unsigned char));
    EXPECT_EQ(version, 1);
    unsigned char precision;
    memcpy(&precision, dec + sizeof(unsigned char), sizeof(unsigned char));
    EXPECT_EQ(precision, 10); // the one initialized in test_init_standard_instance_one_cube_one_metric_one_cell_hll

    struct hyperloglog *hll_from_blob = hyperloglog_new(precision);
    int num_reg = NUM_REG(precision);
    int words = INT_CEIL(num_reg, REG_PER_WORD);
    size_t reg_size = words * sizeof(uint32_t);

    uint32_t *registers = (uint32_t *)malloc(reg_size);
    memcpy(registers, dec + 2 * sizeof(unsigned char), reg_size);
    for (int i = 0; i < words; i++) {
        registers[i] = BigLittleSwap32(registers[i]);
    }
    memcpy(hll_from_blob->registers, registers, reg_size);
    free(registers);

    EXPECT_NEAR(hyperloglog_count(hll_from_blob), 3, 0.5);
    free(blob);
    free(dec);
    fieldstat_free(instance);
    hyperloglog_free(hll_from_blob);
}


extern "C" {
    void *hll_base64_decode(char *buf);
    bool fieldstat_is_hll(char *buf);
}
TEST(metric_test_hll, serialize_with_b64_and_query_with_python_api)
{
    struct fieldstat *instance = test_init_standard_instance_one_cube_one_metric_one_cell_hll();
    fieldstat_hll_add(instance, 0, 0, &TEST_TAG_INT, 1, "hello", 5);
    fieldstat_hll_add(instance, 0, 0, &TEST_TAG_INT, 1, "wor", 3);
    fieldstat_hll_add(instance, 0, 0, &TEST_TAG_INT, 1, "world", 5);

    char *blob = NULL;
    size_t blob_len = 0;
    fieldstat_get_serialized_blob(instance, 0, 0, &TEST_TAG_LIST_INT, &blob, &blob_len);

    bool flag = fieldstat_is_hll(blob);
    EXPECT_EQ(flag, true);

    void *hll_from_blob = hll_base64_decode(blob);
    EXPECT_NEAR(hyperloglog_count((struct hyperloglog *)hll_from_blob), 3, 0.5);
    free(blob);
    fieldstat_free(instance);
    hyperloglog_free((struct hyperloglog *)hll_from_blob);
}

TEST(metric_test_hll, add_with_wrong_cube_id_expecting_fail)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_COMPREHENSIVE, 10);

    int ret = fieldstat_hll_add(instance, cube_id + 1, 0, &TEST_TAG_INT, 1, "hello", 5);
    EXPECT_EQ(ret, FS_ERR_INVALID_CUBE_ID);
    ret = fieldstat_hll_add(instance, -1, 0, &TEST_TAG_INT, 1, "hello", 5);
    EXPECT_EQ(ret, FS_ERR_INVALID_CUBE_ID);

    fieldstat_free(instance);
}

TEST(metric_test_hll, add_with_wrong_metric_id_expecting_fail)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_hll(instance, cube_id, "czz_test hll metric", 10);

    int ret = fieldstat_hll_add(instance, cube_id, metric_id + 1, &TEST_TAG_INT, 1, "hello", 5);
    EXPECT_EQ(ret, FS_ERR_INVALID_METRIC_ID);
    ret = fieldstat_hll_add(instance, cube_id, -1, &TEST_TAG_INT, 1, "hello", 5);
    EXPECT_EQ(ret, FS_ERR_INVALID_METRIC_ID);

    fieldstat_free(instance);
}

TEST(metric_test_hll, spread_sketch_add_and_test_accuracy)
{
    struct fieldstat *instance = fieldstat_new();
    int K = 10;
    fieldstat_create_cube(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_TOP_CARDINALITY, K);
    fieldstat_register_hll(instance, 0, "testss", 6);

    int n_flows = 100000;
    std::unordered_map<std::string, std::unordered_set<std::string>> flow_cnt;
    SpreadSketchZipfGenerator generator(1.0, K * 10); // give much bigger distribution, so that we can test the accuracy
    for (int i = 0; i < n_flows; i++)
    {   
        Flow f = generator.next();
        Fieldstat_tag_list_wrapper dimension("src ip", f.src_ip.c_str());
        Fieldstat_tag_list_wrapper counted("dst ip", f.dst_ip.c_str());

        fieldstat_hll_add_field(instance, 0, 0, dimension.get_tag(), dimension.get_tag_count(), counted.get_tag(), counted.get_tag_count());

        flow_cnt[dimension.to_string()].insert(counted.to_string());
    }

    // recall
    std::unordered_map<std::string, int> expected_unique_cnt;
    std::vector<struct Fieldstat_tag_list_wrapper *> test_result;
    for (auto &kv : flow_cnt) {
        expected_unique_cnt[kv.first] = kv.second.size();
    }

    struct field_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance, 0, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, K);
    for (size_t i = 0; i < n_cell; i++) {
        Fieldstat_tag_list_wrapper tmp = Fieldstat_tag_list_wrapper(&tag_list[i]);
        test_result.push_back(new Fieldstat_tag_list_wrapper(tmp));
    }
    double recall = test_cal_topk_accuracy(test_result, expected_unique_cnt);
    printf("spread_sketch_add_and_test_accuracy recall: %f\n", recall);
    EXPECT_GE(recall, 0.8);

    // MRE
    double mre = 0;
    for (size_t i = 0; i < n_cell; i++) {
        Fieldstat_tag_list_wrapper tmp = Fieldstat_tag_list_wrapper(&tag_list[i]);
        double value_true = expected_unique_cnt[tmp.to_string()];
        double value_est;
        fieldstat_hll_get(instance, 0, &tag_list[i], 0, &value_est);
        // printf("the estimated value for %s is %f, the true value is %f\n", tmp.to_string().c_str(), value_est, value_true);

        mre += fabs(value_true - value_est) / value_true;
    }
    mre = mre / n_cell;
    printf("topk_add_and_test_accuracy Mean ratio e: %f\n", mre);
    EXPECT_LE(mre, 0.2);

    fieldstat_tag_list_arr_free(tag_list, n_cell);
    fieldstat_free(instance);
    for (auto &ptr : test_result) {
        delete ptr;
    }
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
    // testing::GTEST_FLAG(filter) = "metric_test_hll.spread_sketch_add_and_test_accuracy";
	return RUN_ALL_TESTS();
}
