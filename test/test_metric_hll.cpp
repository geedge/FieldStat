#include <gtest/gtest.h>
#include "fieldstat.h"
#include "utils.hpp"

struct fieldstat *test_init_standard_instance_one_cube_one_metric_one_cell_hll(bool is_gauge = false)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_create_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    EXPECT_EQ(cube_id, 0);

    int metric_id = fieldstat_register_hll(instance, "czz_test hll metric", 10);
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
    const char *name = fieldstat_get_metric_name(instance, 0);
    EXPECT_STREQ(name, "czz_test hll metric");

    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance, 0, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(tag_list->n_tag, 1);
    EXPECT_STREQ(tag_list->tag[0].key, TEST_TAG_INT.key);
    
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
#include "st_hyperloglog.h"
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

    struct ST_hyperloglog *hll_from_blob = ST_hyperloglog_new(precision);
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

    EXPECT_NEAR(ST_hyperloglog_count(hll_from_blob), 3, 0.5);
    free(blob);
    free(dec);
    fieldstat_free(instance);
    ST_hyperloglog_free(hll_from_blob);
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
    EXPECT_NEAR(ST_hyperloglog_count((struct ST_hyperloglog *)hll_from_blob), 3, 0.5);
    free(blob);
    fieldstat_free(instance);
    ST_hyperloglog_free((struct ST_hyperloglog *)hll_from_blob);
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
    int metric_id = fieldstat_register_hll(instance, "czz_test hll metric", 10);

    int ret = fieldstat_hll_add(instance, cube_id, metric_id + 1, &TEST_TAG_INT, 1, "hello", 5);
    EXPECT_EQ(ret, FS_ERR_INVALID_METRIC_ID);
    ret = fieldstat_hll_add(instance, cube_id, -1, &TEST_TAG_INT, 1, "hello", 5);
    EXPECT_EQ(ret, FS_ERR_INVALID_METRIC_ID);

    fieldstat_free(instance);
}


int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
