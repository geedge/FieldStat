
#include <gtest/gtest.h>

#include "utils.hpp"

#include "fieldstat.h"
#include "base64/b64.h"
#include "st_hyperloglog.h"


void test_hll_add(int adding_time, struct fieldstat *instance, int id, const struct fieldstat_tag *tags, size_t tag_count) {
    for (int i = 0; i < adding_time; i++) {
        std::string rand_str = gen_rand_string(10);
        fieldstat_hll_add(instance, id, rand_str.c_str(), rand_str.size(), tags, tag_count);
    }
}

TEST(metric_test_hll, serialize_and_deserialize_for_2_fields_1st_empty_2nd_added)
{
    struct fieldstat *instance = fieldstat_new();
    printf("new ok\n");

    (void)fieldstat_register_hll(instance, "empty", 10);
    int id2_added = fieldstat_register_hll(instance, "add", 10);
    printf("register OK\n");

    // metric add
    test_hll_add(3, instance, id2_added, &TEST_TAG_INT, 1);
    test_hll_add(40, instance, id2_added, &TEST_TAG_INT_collided, 1);
    
    struct fieldstat *instance_from_deserialize = test_serialize_and_deserialize(instance);

    auto q_rets = test_get_query_result<double>(fieldstat_hll_get, instance_from_deserialize, id2_added);
    EXPECT_EQ(q_rets.size(), 2);
    EXPECT_NEAR(q_rets[0], 3, 3 * 0.15);
    EXPECT_NEAR(q_rets[1], 40, 40 * 0.15);

    fieldstat_free(instance);
    fieldstat_free(instance_from_deserialize);
}

TEST(metric_test_hll, serialize_and_merge_by_blob_twice_expecting_double_result)
{
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat *instance2 = fieldstat_new();
    int id_added = fieldstat_register_hll(instance, "add", 10);

    test_hll_add(3, instance, id_added, &TEST_TAG_INT, 1);
    test_hll_add(40, instance, id_added, &TEST_TAG_INT_collided, 1);
    test_merge_with_blob_twice(instance2, instance);

    auto q_rets = test_get_query_result<double>(fieldstat_hll_get, instance2, id_added);
    EXPECT_EQ(q_rets.size(), 2);
    EXPECT_NEAR(q_rets[0], 3, 3 * 0.15);
    EXPECT_NEAR(q_rets[1], 40, 40 * 0.15);

    fieldstat_free(instance);
    fieldstat_free(instance2);
}

TEST(metric_test_hll, serialize_and_merge_empty_register_to_total_empty)
{
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat *instance2 = fieldstat_new();

    (void)fieldstat_register_hll(instance, "add", 10);

    test_merge_with_blob_twice(instance2, instance);

    auto q_rets = test_get_query_result<double>(fieldstat_hll_get, instance2, 0);
    EXPECT_EQ(q_rets.size(), 0);
    EXPECT_EQ(fieldstat_get_max_field_id(instance2), 0); // 1 empty

    fieldstat_free(instance);
    fieldstat_free(instance2);
}

TEST(metric_test_hll, serialize_and_merge_empty_to_nonempty)
{
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat *instance2 = fieldstat_new();
    int id_added = fieldstat_register_hll(instance2, "add", 10);
    (void)fieldstat_register_hll(instance, "empty", 10);

    test_hll_add(3, instance2, id_added, &TEST_TAG_INT, 1);
    test_merge_with_blob_twice(instance2, instance);

    auto q_rets = test_get_query_result<double>(fieldstat_hll_get, instance2, id_added);
    EXPECT_EQ(q_rets.size(), 1);
    EXPECT_NEAR(q_rets[0], 3, 3 * 0.15);

    int all_field_num = fieldstat_get_max_field_id(instance2) + 1;
    EXPECT_EQ(all_field_num, 2); // one original, one empty(from merge)

    fieldstat_free(instance);
    fieldstat_free(instance2);
}

// TODO: 每个都加一个：merge new cell to summery 

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}