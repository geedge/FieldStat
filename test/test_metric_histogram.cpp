
#include <gtest/gtest.h>
#include "fieldstat.h"
#include "utils.hpp"

#include "hdr/hdr_histogram.h"
#include "histogram_encoder.h"

int test_fieldstat_cube_create(struct fieldstat *instance, const struct field *dimensions, size_t n_dimensions, enum sampling_mode mode, int k)
{
    assert(mode == SAMPLING_MODE_COMPREHENSIVE);
    int ret = fieldstat_cube_create(instance, dimensions, n_dimensions);
    fieldstat_cube_set_sampling(instance, ret, mode, k, 0);
    return ret;
}

struct fieldstat *test_init_standard_instance_one_cube_one_metric_one_cell_hdr()
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    EXPECT_EQ(cube_id, 0);

    int metric_id = fieldstat_register_histogram(instance, cube_id, "czz_test hdr metric", 1, 600000, 3);
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
    const char *name = fieldstat_metric_get_name(instance, ret_cell_id, 0);
    EXPECT_STREQ(name, "czz_test hdr metric");


    struct field_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance, 0, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(tag_list->n_field, 1);
    EXPECT_STREQ(tag_list->field[0].key, TEST_FIELD_STRING.key);

    int *metric_id = NULL;
    size_t n_metric = 0;
    fieldstat_cell_get_metrics(instance, 0, &tag_list[0], &metric_id, &n_metric);
    EXPECT_EQ(n_metric, 1);
    EXPECT_EQ(metric_id[0], 0);
    free(metric_id);

    fieldstat_field_list_arr_free(tag_list, n_cell);
}

TEST(metric_test_histogram, simple_register_and_query)
{
    struct fieldstat *instance = test_init_standard_instance_one_cube_one_metric_one_cell_hdr();

    fieldstat_histogram_record(instance, 0, 0, &TEST_FIELD_STRING, 1, 1234);
    fieldstat_histogram_record(instance, 0, 0, &TEST_FIELD_STRING, 1, 1234);
    fieldstat_histogram_record(instance, 0, 0, &TEST_FIELD_STRING, 1, 123);

    test_assert_standard_instance(instance);
    EXPECT_EQ(fieldstat_histogram_value_at_percentile(instance, 0, &TEST_FIELD_LIST_STRING, 0, 50.0), 1234);
    EXPECT_EQ(fieldstat_histogram_count_le_value(instance, 0, &TEST_FIELD_LIST_STRING, 0, 1000), 1);

    fieldstat_free(instance);
}

TEST(metric_test_histogram, merge)
{
    struct fieldstat *instance = test_init_standard_instance_one_cube_one_metric_one_cell_hdr();
    fieldstat_histogram_record(instance, 0, 0, &TEST_FIELD_STRING, 1, 1234);
    fieldstat_histogram_record(instance, 0, 0, &TEST_FIELD_STRING, 1, 1234);
    fieldstat_histogram_record(instance, 0, 0, &TEST_FIELD_STRING, 1, 123);

    struct fieldstat *instance_total = fieldstat_new();
    fieldstat_merge(instance_total, instance);

    // query
    test_assert_standard_instance(instance_total);

    EXPECT_EQ(fieldstat_histogram_value_at_percentile(instance_total, 0, &TEST_FIELD_LIST_STRING, 0, 50.0), 1234);
    EXPECT_EQ(fieldstat_histogram_count_le_value(instance, 0, &TEST_FIELD_LIST_STRING, 0, 1000), 1);

    fieldstat_free(instance);
    fieldstat_free(instance_total);
}

TEST(metric_test_histogram, merge_twice_with_reset)
{
    struct fieldstat *instance = test_init_standard_instance_one_cube_one_metric_one_cell_hdr();
    fieldstat_histogram_record(instance, 0, 0, &TEST_FIELD_STRING, 1, 1234);
    fieldstat_histogram_record(instance, 0, 0, &TEST_FIELD_STRING, 1, 123);

    struct fieldstat *instance_total = fieldstat_new();
    fieldstat_merge(instance_total, instance);

    fieldstat_reset(instance);
    fieldstat_histogram_record(instance, 0, 0, &TEST_FIELD_STRING, 1, 1234);
    fieldstat_merge(instance_total, instance);

    test_assert_standard_instance(instance_total);
    EXPECT_EQ(fieldstat_histogram_value_at_percentile(instance_total, 0, &TEST_FIELD_LIST_STRING, 0, 50.0), 1234);
    EXPECT_EQ(fieldstat_histogram_count_le_value(instance_total, 0, &TEST_FIELD_LIST_STRING, 0, 1000), 1);

    fieldstat_free(instance);
    fieldstat_free(instance_total);
}

TEST(metric_test_histogram, add_with_wrong_cube_id_expecting_fail)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, &TEST_FIELD_INT_2, 1, SAMPLING_MODE_COMPREHENSIVE, 10);

    int ret = fieldstat_histogram_record(instance, cube_id + 1, 0, &TEST_FIELD_INT, 1, 1);
    EXPECT_EQ(ret, FS_ERR_INVALID_CUBE_ID);
    ret = fieldstat_histogram_record(instance, -1, 0, &TEST_FIELD_INT, 1, 1);
    EXPECT_EQ(ret, FS_ERR_INVALID_CUBE_ID);

    fieldstat_free(instance);
}

TEST(metric_test_histogram, add_with_wrong_metric_id_expecting_fail)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, &TEST_FIELD_INT_2, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_histogram(instance, cube_id, "czz_test", 1, 600000, 3);

    int ret = fieldstat_histogram_record(instance, cube_id, metric_id + 1, &TEST_FIELD_INT, 1, 1);
    EXPECT_EQ(ret, FS_ERR_INVALID_METRIC_ID);
    ret = fieldstat_histogram_record(instance, cube_id, -1, &TEST_FIELD_INT, 1, 1);
    EXPECT_EQ(ret, FS_ERR_INVALID_METRIC_ID);

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

TEST(metric_test_histogram, can_add_0value) // histogram only allow min_val > 0, but it can accept value == 0
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, &TEST_FIELD_INT_2, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_histogram(instance, cube_id, "czz_test", 1, 600000, 3);

    int ret = fieldstat_histogram_record(instance, cube_id, metric_id, &TEST_FIELD_INT, 1, 0);
    EXPECT_EQ(ret, 0);
    ret = fieldstat_histogram_record(instance, cube_id, metric_id, &TEST_FIELD_INT, 1, 0);
    EXPECT_EQ(ret, 0);
    ret = fieldstat_histogram_record(instance, cube_id, metric_id, &TEST_FIELD_INT, 1, 500);

    EXPECT_EQ(fieldstat_histogram_value_at_percentile(instance, cube_id, &TEST_FIELD_LIST_INT, metric_id,  60.0), 0);

    fieldstat_free(instance);
}

TEST(metric_test_histogram, histogram_batch_transaction) {
    struct fieldstat *instance = fieldstat_new();
    int cube_id = test_fieldstat_cube_create(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 10);
    int metric_id = fieldstat_register_histogram(instance, cube_id, "test_histogram_metric", 1, 10000, 5);

    struct histogram *histogram = histogram_new(1, 10000, 5);
    ASSERT_NE(histogram, nullptr);

    long long packet_lengths[] = {1234, 1234, 123, 567, 890, 1234, 1500};
    size_t packet_count = sizeof(packet_lengths) / sizeof(packet_lengths[0]);

    for (size_t i = 0; i < packet_count; ++i) {
        int tmp_ret = histogram_record(histogram, packet_lengths[i]);
        EXPECT_EQ(tmp_ret, 0);
    }

    int merge_result = fieldstat_histogram_merge(instance, cube_id, metric_id, &TEST_FIELD_STRING, 1, histogram);
    EXPECT_EQ(merge_result, 0);

    double percentile_value = fieldstat_histogram_value_at_percentile(instance, cube_id, &TEST_FIELD_LIST_STRING, metric_id, 50.0);
    EXPECT_EQ(percentile_value, 1234);
    int count_le = fieldstat_histogram_count_le_value(instance, cube_id, &TEST_FIELD_LIST_STRING, metric_id, 1000);
    EXPECT_EQ(count_le, 3);

    // test reset
    histogram_reset(histogram);
    histogram_record(histogram, 2000);
    histogram_record(histogram, 2500);
    merge_result = fieldstat_histogram_merge(instance, cube_id, metric_id, &TEST_FIELD_STRING, 1, histogram);
    EXPECT_EQ(merge_result, 0);

    percentile_value = fieldstat_histogram_value_at_percentile(instance, cube_id, &TEST_FIELD_LIST_STRING, metric_id, 95.0);
    EXPECT_EQ(percentile_value, 2500);
    count_le = fieldstat_histogram_count_le_value(instance, cube_id, &TEST_FIELD_LIST_STRING, metric_id, 1000);
    EXPECT_EQ(count_le, 3);

    histogram_free(histogram);
    fieldstat_free(instance);
}


int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}