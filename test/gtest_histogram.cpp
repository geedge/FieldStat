
#include <gtest/gtest.h>

#include "utils.hpp"

#include "fieldstat.h"
#include "base64/b64.h"

long long histogram_query(const struct fieldstat *instance, int field_id, const struct fieldstat_tag_group *tag_list)
{
    long long ret = fieldstat_histogram_value_at_percentile(instance, field_id, tag_list, 50.0);
    return ret;
}

TEST(metric_test_histogram, serialize_and_deserialize_for_2_fields_1st_empty_2nd_added)
{
    struct fieldstat *instance = fieldstat_new();

    int id1_empty = fieldstat_register_histogram(instance, "empty", 1, 600000, 3);
    int id2_added = fieldstat_register_histogram(instance, "add", 1, 600000, 3);
    // metric add
    long long value_add_twice = 1234;
    long long value_add_once = 123;
    fieldstat_histogram_record(instance, id2_added, value_add_twice, &TEST_TAG_DOUBLE, 1);
    fieldstat_histogram_record(instance, id2_added, value_add_twice, &TEST_TAG_DOUBLE, 1);
    fieldstat_histogram_record(instance, id2_added, value_add_once, &TEST_TAG_DOUBLE, 1);
    long long value_add_to_new_metric = 1;
    fieldstat_histogram_record(instance, id2_added, value_add_to_new_metric, &TEST_TAG_INT, 1);

    struct fieldstat *instance_from_deserialize = test_serialize_and_deserialize(instance);

    auto q_rets = test_get_query_result<long long>(histogram_query, instance_from_deserialize, id2_added);
    EXPECT_EQ(q_rets.size(), 2);
    EXPECT_EQ(q_rets[0], value_add_twice);
    EXPECT_EQ(q_rets[1], value_add_to_new_metric);

    auto q_rets_empty = test_get_query_result<long long>(histogram_query, instance_from_deserialize, id1_empty);
    EXPECT_EQ(q_rets_empty.size(), 0);

    fieldstat_free(instance);
    fieldstat_free(instance_from_deserialize);
}

TEST(metric_test_histogram, serialize_and_merge_by_blob_twice_expecting_double_result)
{
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat *instance2 = fieldstat_new();

    int id1_empty = fieldstat_register_histogram(instance, "empty", 1, 600000, 3);
    int id2_added = fieldstat_register_histogram(instance, "add", 1, 600000, 3);
    // metric add
    long long value_add_twice = 1234;
    long long value_add_once = 123;
    fieldstat_histogram_record(instance, id2_added, value_add_twice, &TEST_TAG_DOUBLE, 1);
    fieldstat_histogram_record(instance, id2_added, value_add_twice, &TEST_TAG_DOUBLE, 1);
    fieldstat_histogram_record(instance, id2_added, value_add_once, &TEST_TAG_DOUBLE, 1);
    long long value_add_to_new_metric = 1;
    fieldstat_histogram_record(instance, id2_added, value_add_to_new_metric, &TEST_TAG_INT, 1);

    test_merge_with_blob_twice(instance2, instance);

    auto q_rets = test_get_query_result<long long>(histogram_query, instance2, id2_added);
    EXPECT_EQ(q_rets.size(), 2);
    EXPECT_EQ(q_rets[0], value_add_twice);
    EXPECT_EQ(q_rets[1], value_add_to_new_metric);

    auto q_rets_empty = test_get_query_result<long long>(histogram_query, instance2, id1_empty);
    EXPECT_EQ(q_rets_empty.size(), 0);

    fieldstat_free(instance);
    fieldstat_free(instance2);
}

TEST(metric_test_histogram, serialize_and_merge_empty_register_to_total_empty)
{
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat *instance2 = fieldstat_new();

    (void)fieldstat_register_histogram(instance, "empty", 1, 600000, 3);

    test_merge_with_blob(instance2, instance);

    auto q_rets = test_get_query_result<long long>(histogram_query, instance2, 0);
    EXPECT_EQ(q_rets.size(), 0);

    EXPECT_EQ(fieldstat_get_max_field_id(instance2), 0); // 1 empty

    fieldstat_free(instance);
    fieldstat_free(instance2);
}

TEST(metric_test_histogram, serialize_and_merge_empty_to_nonempty)
{
    struct fieldstat *instance_empty = fieldstat_new();
    struct fieldstat *instance_added = fieldstat_new();

    (void)fieldstat_register_histogram(instance_empty, "empty", 1, 600000, 3);
    int id_added = fieldstat_register_histogram(instance_added, "add", 1, 600000, 3);
    // metric add
    long long value_add_twice = 1234;
    long long value_add_once = 123;
    fieldstat_histogram_record(instance_added, id_added, value_add_twice, &TEST_TAG_DOUBLE, 1);
    fieldstat_histogram_record(instance_added, id_added, value_add_twice, &TEST_TAG_DOUBLE, 1);
    fieldstat_histogram_record(instance_added, id_added, value_add_once, &TEST_TAG_DOUBLE, 1);

    test_merge_with_blob(instance_added, instance_empty);

    auto q_rets = test_get_query_result<long long>(histogram_query, instance_added, id_added);
    EXPECT_EQ(q_rets.size(), 1);
    EXPECT_EQ(q_rets[0], value_add_twice);

    int all_field_num = fieldstat_get_max_field_id(instance_added) + 1;
    EXPECT_EQ(all_field_num, 2); // one original, one empty(from merge)

    fieldstat_free(instance_empty);
    fieldstat_free(instance_added);
}

TEST(metric_test_histogram, serialize_and_merge_twice_with_different_values)
{
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat *instance2 = fieldstat_new();

    int id_added = fieldstat_register_histogram(instance, "add", 1, 600000, 3);
    // metric add
    long long value_add_twice = 1234;
    long long value_add_once = 123;
    fieldstat_histogram_record(instance, id_added, value_add_twice, &TEST_TAG_DOUBLE, 1);
    fieldstat_histogram_record(instance, id_added, value_add_twice, &TEST_TAG_DOUBLE, 1);
    fieldstat_histogram_record(instance, id_added, value_add_once, &TEST_TAG_DOUBLE, 1);
    test_merge_with_blob(instance2, instance);

    fieldstat_reset(instance);
    long long value_add_to_new_metric = 1;
    fieldstat_histogram_record(instance, id_added, value_add_to_new_metric, &TEST_TAG_INT, 1);
    test_merge_with_blob(instance2, instance);

    auto q_rets = test_get_query_result<long long>(histogram_query, instance2, id_added);
    EXPECT_EQ(q_rets.size(), 2);
    EXPECT_EQ(q_rets[0], value_add_twice);
    EXPECT_EQ(q_rets[1], value_add_to_new_metric);

    fieldstat_free(instance);
    fieldstat_free(instance2);
}


int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}