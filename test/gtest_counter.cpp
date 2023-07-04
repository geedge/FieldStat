
#include <gtest/gtest.h>

#include "utils.hpp"

#include "fieldstat.h"

TEST(metric_test_counter, serialize_and_deserialize_for_2_fields_1st_empty_2nd_added)
{
    struct fieldstat *instance = fieldstat_new();

    int id1_empty = fieldstat_register_counter(instance, "empty");
    int id2_added = fieldstat_register_counter(instance, "add");

    // metric add
    fieldstat_counter_incrby(instance, id2_added, 10, &TEST_TAG_STRING, 1);
    fieldstat_counter_incrby(instance, id2_added, -1, &TEST_TAG_STRING, 1);
    fieldstat_counter_incrby(instance, id2_added, 12345, &TEST_TAG_INT, 1);

    struct fieldstat *instance_from_deserialize = test_serialize_and_deserialize(instance);

    auto q_rets = test_get_query_result<long long>(fieldstat_counter_get, instance_from_deserialize, id2_added);
    EXPECT_EQ(q_rets[0], 9);
    EXPECT_EQ(q_rets[1], 12345);

    auto q_rets_empty = test_get_query_result<long long>(fieldstat_counter_get, instance_from_deserialize, id1_empty);
    EXPECT_EQ(q_rets_empty.size(), 0); 

    fieldstat_free(instance);
    fieldstat_free(instance_from_deserialize);
}

TEST(metric_test_counter, serialize_and_merge_by_blob_twice_expecting_double_result)
{
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat *instance2 = fieldstat_new();
    int id_added = fieldstat_register_counter(instance, "add");

    fieldstat_counter_incrby(instance, id_added, 1000, &TEST_TAG_INT, 1);
    fieldstat_counter_incrby(instance, id_added, -70, &TEST_TAG_STRING, 1);
    test_merge_with_blob_twice(instance2, instance);

    auto q_rets = test_get_query_result<long long>(fieldstat_counter_get, instance2, id_added);
    EXPECT_EQ(q_rets[0], 2000);
    EXPECT_EQ(q_rets[1], -140);

    fieldstat_free(instance);
    fieldstat_free(instance2);
}

TEST(metric_test_counter, serialize_and_merge_empty_register_to_total_empty)
{
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat *instance2 = fieldstat_new();

    (void)fieldstat_register_counter(instance, "empty");

    test_merge_with_blob(instance2, instance);

    auto q_rets = test_get_query_result<long long>(fieldstat_counter_get, instance2, 0);
    EXPECT_EQ(q_rets.size(), 0);

    EXPECT_EQ(fieldstat_get_max_field_id(instance2), 0);

    fieldstat_free(instance);
    fieldstat_free(instance2);
}

TEST(metric_test_counter, serialize_and_merge_empty_to_nonempty)
{
    struct fieldstat *instance_empty = fieldstat_new();
    struct fieldstat *instance_added = fieldstat_new();

    (void)fieldstat_register_counter(instance_empty, "empty");
    int id_added = fieldstat_register_counter(instance_added, "add");

    // metric add
    fieldstat_counter_incrby(instance_added, id_added, 10, &TEST_TAG_STRING, 1);
    fieldstat_counter_incrby(instance_added, id_added, 12345, &TEST_TAG_INT, 1);

    test_merge_with_blob(instance_added, instance_empty);

    auto q_rets = test_get_query_result<long long>(fieldstat_counter_get, instance_added, id_added);
    EXPECT_EQ(q_rets.size(), 2);
    EXPECT_EQ(q_rets[0], 10);
    EXPECT_EQ(q_rets[1], 12345);
    int all_field_num = fieldstat_get_max_field_id(instance_added) + 1;
    EXPECT_EQ(all_field_num, 2); // one original, one empty(from merge)

    fieldstat_free(instance_empty);
    fieldstat_free(instance_added);
}

TEST(metric_test_counter, serialize_and_merge_twice_with_different_values)
{
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat *instance2 = fieldstat_new();
    int id_added = fieldstat_register_counter(instance, "add");

    fieldstat_counter_incrby(instance, id_added, 1000, &TEST_TAG_INT, 1);
    test_merge_with_blob(instance2, instance);
    fieldstat_refresh(instance);
    
    fieldstat_counter_incrby(instance, id_added, -70, &TEST_TAG_STRING, 1);
    test_merge_with_blob(instance2, instance);

    auto q_rets = test_get_query_result<long long>(fieldstat_counter_get, instance2, id_added);
    EXPECT_EQ(q_rets.size(), 2);
    EXPECT_EQ(q_rets[0], 1000);
    EXPECT_EQ(q_rets[1], -70);

    fieldstat_free(instance);
    fieldstat_free(instance2);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}