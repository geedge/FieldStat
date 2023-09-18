
#include <gtest/gtest.h>
#include "fieldstat.h"
#include "utils.hpp"

#include "hdr/hdr_histogram.h"
#include "histogram_encoder.h"

struct fieldstat *test_init_standard_instance_one_cube_one_metric_one_cell_hdr()
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    EXPECT_EQ(cube_id, 0);

    int metric_id = fieldstat_register_hist(instance, cube_id, "czz_test hdr metric", 1, 600000, 3);
    EXPECT_EQ(metric_id, 0);

    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_STRING, 1, 1);
    EXPECT_EQ(cell_id, 0);

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
    int ret_metric_id = fieldstat_get_max_metric_id(instance, ret_cell_id);
    EXPECT_EQ(ret_metric_id, 0);
    const char *name = fieldstat_get_metric_name(instance, 0, 0);
    EXPECT_STREQ(name, "czz_test hdr metric");

    int *ret_cell_ids = NULL;
    struct fieldstat_tag_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_get_cells(instance, 0, 0, &ret_cell_ids, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(ret_cell_ids[0], 0);
    EXPECT_EQ(tag_list->n_tag, 1);
    EXPECT_STREQ(tag_list->tag[0].key, TEST_TAG_STRING.key);

    free(ret_cell_ids);
    fieldstat_tag_list_arr_free(tag_list, n_cell);
}

TEST(metric_test_histogram, simple_register_and_query)
{
    struct fieldstat *instance = test_init_standard_instance_one_cube_one_metric_one_cell_hdr();

    fieldstat_hist_record(instance, 0, 0, 0, 1234);
    fieldstat_hist_record(instance, 0, 0, 0, 1234);
    fieldstat_hist_record(instance, 0, 0, 0, 123);

    test_assert_standard_instance(instance);
    EXPECT_EQ(fieldstat_hist_value_at_percentile(instance, 0, 0, 0, 50.0), 1234);
    EXPECT_EQ(fieldstat_hist_count_le_value(instance, 0, 0, 0, 1000), 1);

    fieldstat_free(instance);
}

TEST(metric_test_histogram, serialize_and_merge)
{
    struct fieldstat *instance = test_init_standard_instance_one_cube_one_metric_one_cell_hdr();
    fieldstat_hist_record(instance, 0, 0, 0, 1234);
    fieldstat_hist_record(instance, 0, 0, 0, 1234);
    fieldstat_hist_record(instance, 0, 0, 0, 123);

    char *blob = NULL;
    size_t blob_len = 0;
    fieldstat_serialize(instance, &blob, &blob_len);
    struct fieldstat *instance_from_blob = fieldstat_deserialize(blob, blob_len);
    free(blob);
    struct fieldstat *instance_total = fieldstat_new();
    fieldstat_merge(instance_total, instance_from_blob);

    // query
    test_assert_standard_instance(instance_total);

    EXPECT_EQ(fieldstat_hist_value_at_percentile(instance_total, 0, 0, 0, 50.0), 1234);
    EXPECT_EQ(fieldstat_hist_count_le_value(instance, 0, 0, 0, 1000), 1);

    fieldstat_free(instance);
    fieldstat_free(instance_from_blob);
    fieldstat_free(instance_total);
}

TEST(metric_test_histogram, serialization_and_merge_twice_with_reset)
{
    struct fieldstat *instance = test_init_standard_instance_one_cube_one_metric_one_cell_hdr();
    fieldstat_hist_record(instance, 0, 0, 0, 1234);
    fieldstat_hist_record(instance, 0, 0, 0, 123);

    char *blob = NULL;
    size_t blob_len = 0;
    fieldstat_serialize(instance, &blob, &blob_len);
    struct fieldstat *instance_from_blob = fieldstat_deserialize(blob, blob_len);
    free(blob);
    struct fieldstat *instance_total = fieldstat_new();
    fieldstat_merge(instance_total, instance_from_blob);

    fieldstat_free(instance_from_blob);
    fieldstat_reset(instance);
    int cell_id = fieldstat_cube_add(instance, 0, &TEST_TAG_STRING, 1, 1);
    EXPECT_EQ(cell_id, 0);
    fieldstat_hist_record(instance, 0, 0, 0, 1234);

    fieldstat_serialize(instance, &blob, &blob_len);
    instance_from_blob = fieldstat_deserialize(blob, blob_len);
    free(blob);
    fieldstat_merge(instance_total, instance_from_blob);

    test_assert_standard_instance(instance_total);
    EXPECT_EQ(fieldstat_hist_value_at_percentile(instance_total, 0, 0, 0, 50.0), 1234);
    EXPECT_EQ(fieldstat_hist_count_le_value(instance_total, 0, 0, 0, 1000), 1);

    fieldstat_free(instance);
    fieldstat_free(instance_from_blob);
    fieldstat_free(instance_total);
}

TEST(metric_test_histogram, add_or_set_with_wrong_cell_id_expecting_fail)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_hist(instance, cube_id, "czz_test", 1, 600000, 3);

    int ret = fieldstat_hist_record(instance, cube_id, metric_id, 1, 1234);
    EXPECT_EQ(ret, -3);
    ret = fieldstat_hist_record(instance, cube_id, metric_id, -1, 1234);
    EXPECT_EQ(ret, -3);

    fieldstat_free(instance);
}

TEST(metric_test_histogram, add_with_wrong_cube_id_expecting_fail)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_COMPREHENSIVE, 10);

    int ret = fieldstat_hist_record(instance, cube_id + 1, 0, 1, 1);
    EXPECT_EQ(ret, -1);
    ret = fieldstat_hist_record(instance, -1, 0, 1, 1);
    EXPECT_EQ(ret, -1);

    fieldstat_free(instance);
}

TEST(metric_test_histogram, add_with_wrong_metric_id_expecting_fail)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_hist(instance, cube_id, "czz_test", 1, 600000, 3);

    int ret = fieldstat_hist_record(instance, cube_id, metric_id + 1, 1, 1);
    EXPECT_EQ(ret, -2);
    ret = fieldstat_hist_record(instance, cube_id, -1, 1, 1);
    EXPECT_EQ(ret, -2);

    fieldstat_free(instance);
}

TEST(metric_test_histogram, can_add_0value) // histogram only allow min_val > 0, but it can accept value == 0
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_register_cube(instance, &TEST_TAG_INT_collided, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_hist(instance, cube_id, "czz_test", 1, 600000, 3);
    int cell_id = fieldstat_cube_add(instance, cube_id, &TEST_TAG_INT_collided, 1, 1);

    int ret = fieldstat_hist_record(instance, cube_id, metric_id, cell_id, 0);
    EXPECT_EQ(ret, 0);
    ret = fieldstat_hist_record(instance, cube_id, metric_id, cell_id, 0);
    EXPECT_EQ(ret, 0);
    ret = fieldstat_hist_record(instance, cube_id, metric_id, cell_id, 500);

    EXPECT_EQ(fieldstat_hist_value_at_percentile(instance, cube_id, metric_id, cell_id, 60.0), 0);

    fieldstat_free(instance);
}

extern "C" {
    bool fieldstat_is_hll(char *buf);
}

TEST(metric_test_histogram, encode_decode_b64)
{
    struct hdr_histogram *h = NULL;
    hdr_init(1, 10000, 5, &h);
    hdr_record_value(h, 1234);
    hdr_record_value(h, 1234);
    hdr_record_value(h, 4312);

    char *b64 = NULL;
    size_t b64_len = 0;
    histogram_encode_into_b64(h, &b64, &b64_len);

    EXPECT_FALSE(fieldstat_is_hll(b64));
    
    struct hdr_histogram *h2 = histogram_decode_from_b64(b64, b64_len);

    EXPECT_TRUE(h2 != NULL);
    EXPECT_EQ(h->lowest_discernible_value, h2->lowest_discernible_value);
    EXPECT_EQ(h->highest_trackable_value, h2->highest_trackable_value);
    EXPECT_EQ(h->significant_figures, h2->significant_figures);
    EXPECT_EQ(h->counts_len, h2->counts_len);
    for (int i = 0; i < h2->counts_len; i++)
    {
        EXPECT_EQ(h->counts[i], h2->counts[i]);
    }

    free(b64);
    hdr_close(h);
    hdr_close(h2);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}