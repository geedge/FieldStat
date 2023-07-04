
#include <gtest/gtest.h>

#include "utils.hpp"

#include "fieldstat.h"


using namespace std;

const int K = 10;
const int MAX_METRIC = 30;

class Query_ret {
public:
    string key_result[K];
    unsigned int count_result[K][MAX_METRIC];
    int n_result;
    int n_metric;
    string metric_name[MAX_METRIC];
    int primary_metric_id;

    explicit Query_ret(char **keys, size_t entry_num, 
        char **metric_name, size_t primary_idx, size_t metric_num,
        unsigned int **counts)
    {
        n_result = entry_num;
        n_metric = metric_num;
        primary_metric_id = primary_idx;
        for (size_t i = 0; i < entry_num; i++)
        {
            key_result[i] = keys[i];
            for (size_t j = 0; j < metric_num; j++)
            {
                count_result[i][j] = counts[i][j];
            }
        }
        for (size_t i = 0; i < metric_num; i++)
        {
            this->metric_name[i] = metric_name[i];
        }
    }
};

Query_ret topk_query(const struct fieldstat *instance, int field_id, const struct fieldstat_tag_list *tag_list)
{
    char **key_result = NULL;
    size_t n_result = 0;
    char **metric_name_result = NULL;
    size_t n_metric = 0;
    size_t primary_key_id = 0;
    unsigned int **count_result = NULL;
    fieldstat_topk_query(instance, field_id, tag_list,
                         &key_result, &n_result, &metric_name_result, &n_metric, &primary_key_id, &count_result);
    Query_ret ret(key_result, n_result, metric_name_result, primary_key_id, n_metric, count_result);

    for (size_t i = 0; i < n_result; i++)
    {
        free(key_result[i]);
    }
    free(key_result);
    for (size_t i = 0; i < n_metric; i++)
    {
        free(metric_name_result[i]);
    }
    free(metric_name_result);
    for (size_t i = 0; i < n_result; i++)
    {
        free(count_result[i]);
    }
    free(count_result);

    return ret;
}

void test_check_result_with_expected_query_ret(Query_ret &ret,
    int exp_n_ret, int exp_n_metric, int exp_primary_metric_id, 
    std::string *exp_key_result, unsigned int *exp_count_result, const char *exp_metric_name[])
{
    EXPECT_EQ(ret.n_result, exp_n_ret);
    EXPECT_EQ(ret.n_metric, exp_n_metric);
    EXPECT_EQ(ret.primary_metric_id, exp_primary_metric_id);
    for (int i = 0; i < exp_n_ret; i++)
    {
        EXPECT_STREQ(ret.key_result[i].c_str(), exp_key_result[i].c_str());
        for (int j = 0; j < exp_n_metric; j++)
        {
            EXPECT_EQ(ret.count_result[i][j], exp_count_result[i * exp_n_metric + j]);
        }
    }
    for (int i = 0; i < exp_n_metric; i++)
    {
        EXPECT_STREQ(ret.metric_name[i].c_str(), exp_metric_name[i]);
    }
}

TEST(metric_test_topk, serialize_and_deserialize_for_2_fields_1st_empty_2nd_added)
{
    struct fieldstat *instance = fieldstat_new();

    const char *metric_name[] = {"test_bytes", "test_packet_num"};
    unsigned int added_value[1][2] = {{1, 10000}};
    std::string value_string_2_1_2 = gen_rand_string(10);
    std::string value_string_2_1_1 = gen_rand_string(10);

    int id1_empty = fieldstat_register_topk(instance, "topk_empty", 10, metric_name, 2, 1);
    int id2_added = fieldstat_register_topk(instance, "topk_add", 10, metric_name, 2, 1);
    // register metric 
    fieldstat_topk_add(instance, id2_added, value_string_2_1_1.c_str(), value_string_2_1_1.length(), added_value[0], &TEST_TAG_STRING, 1);
    fieldstat_topk_add(instance, id2_added, value_string_2_1_1.c_str(), value_string_2_1_1.length(), added_value[0], &TEST_TAG_STRING, 1);
    fieldstat_topk_add(instance, id2_added, value_string_2_1_2.c_str(), value_string_2_1_2.length(), added_value[0], &TEST_TAG_INT, 1);

    struct fieldstat *instance_from_deserialize = test_serialize_and_deserialize(instance);

    // query and test
    auto ret_added = test_get_query_result<Query_ret>(topk_query, instance_from_deserialize, id2_added);
    auto ret_empty = test_get_query_result<Query_ret>(topk_query, instance_from_deserialize, id1_empty);
    EXPECT_TRUE(ret_added.size() == 2);
    EXPECT_TRUE(ret_empty.size() == 0);

    unsigned int added_value_double[1][2] = {{2, 20000}};
    test_check_result_with_expected_query_ret(ret_added[0], 1, 2, 1, &value_string_2_1_1, (unsigned int *)added_value_double, metric_name);
    test_check_result_with_expected_query_ret(ret_added[1], 1, 2, 1, &value_string_2_1_2, (unsigned int *)added_value, metric_name);

    fieldstat_free(instance);
    fieldstat_free(instance_from_deserialize);
}


TEST(metric_test_topk, serialize_and_merge_by_blob_twice_expecting_double_result)
{
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat *instance2 = fieldstat_new();

    const char *metric_name[] = {"test_bytes", "test_packet_num"};
    unsigned int added_value[1][2] = {{1, 10000}};
    std::string value_string = gen_rand_string(10);

    int id_added = fieldstat_register_topk(instance, "topk_add", 10, metric_name, 2, 1);
    // register metric 
    // TODO: 这个地方还是加两个吧
    fieldstat_topk_add(instance, id_added, value_string.c_str(), value_string.length(), added_value[0], &TEST_TAG_INT, 1);
    fieldstat_topk_add(instance, id_added, value_string.c_str(), value_string.length(), added_value[0], &TEST_TAG_INT_collided, 1);

    test_merge_with_blob_twice(instance2, instance);

    // query and test
    auto ret_added = test_get_query_result<Query_ret>(topk_query, instance2, 0);

    EXPECT_TRUE(ret_added.size() == 2);

    unsigned int added_value_double[1][2] = {{2, 20000}};
    test_check_result_with_expected_query_ret(ret_added[0], 1, 2, 1, &value_string, (unsigned int *)added_value_double, metric_name);
    test_check_result_with_expected_query_ret(ret_added[1], 1, 2, 1, &value_string, (unsigned int *)added_value_double, metric_name);

    fieldstat_free(instance);
    fieldstat_free(instance2);
}

TEST(metric_test_topk, serialize_and_merge_empty_register_to_total_empty)
{
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat *instance2 = fieldstat_new();

    const char *metric_name[] = {"test_bytes", "test_packet_num"};
    (void)fieldstat_register_topk(instance, "topk_empty", 10, metric_name, 2, 1);

    test_merge_with_blob(instance2, instance);

    auto ret_added = test_get_query_result<Query_ret>(topk_query, instance2, 0);
    EXPECT_EQ(ret_added.size(), 0);

    EXPECT_EQ(fieldstat_get_max_field_id(instance2), 0); // one empty field

    fieldstat_free(instance);
    fieldstat_free(instance2);
}

TEST(metric_test_topk, serialize_and_merge_empty_to_nonempty)
{
    struct fieldstat *instance_empty = fieldstat_new();
    struct fieldstat *instance_added = fieldstat_new();
    const char *metric_name[] = {"test_bytes", "test_packet_num"};
    (void)fieldstat_register_topk(instance_empty, "topk_empty", 10, metric_name, 2, 1);
    int id_added = fieldstat_register_topk(instance_added, "topk_add", 10, metric_name, 2, 1);
    unsigned int added_value[] = {1, 10000};
    std::string value_string = gen_rand_string(10);
    fieldstat_topk_add(instance_added, id_added, value_string.c_str(), value_string.length(), added_value, &TEST_TAG_INT, 1);

    test_merge_with_blob(instance_added, instance_empty);

    auto ret_added = test_get_query_result<Query_ret>(topk_query, instance_added, id_added);
    EXPECT_TRUE(ret_added.size() == 1);
    test_check_result_with_expected_query_ret(ret_added[0], 1, 2, 1, &value_string, (unsigned int *)added_value, metric_name);

    int all_field_num = fieldstat_get_max_field_id(instance_added) + 1;
    EXPECT_EQ(all_field_num, 2); // one original, one empty(from merge)
    auto ret_merged_empty = test_get_query_result<Query_ret>(topk_query, instance_added, 1);
    EXPECT_TRUE(ret_merged_empty.size() == 0);

    fieldstat_free(instance_empty);
    fieldstat_free(instance_added);
}

TEST(metric_test_topk, serialize_and_merge_twice_with_different_values)
{
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat *instance2 = fieldstat_new();

    const char *metric_name[] = {"test_bytes", "test_packet_num"};
    unsigned int added_value[1][2] = {{1, 10000}};
    std::string value_string1 = gen_rand_string(10);
    std::string value_string2 = gen_rand_string(10);

    int id_added = fieldstat_register_topk(instance, "topk_add", 10, metric_name, 2, 1);

    fieldstat_topk_add(instance, id_added, value_string1.c_str(), value_string1.length(), added_value[0], &TEST_TAG_INT, 1);
    test_merge_with_blob(instance2, instance);
    fieldstat_refresh(instance);
    fieldstat_topk_add(instance, id_added, value_string2.c_str(), value_string2.length(), added_value[0], &TEST_TAG_DOUBLE, 1);
    test_merge_with_blob(instance2, instance);

    // query and test
    auto ret_added = test_get_query_result<Query_ret>(topk_query, instance2, 0);

    EXPECT_TRUE(ret_added.size() == 2);

    test_check_result_with_expected_query_ret(ret_added[0], 1, 2, 1, &value_string1, (unsigned int *)added_value, metric_name);
    test_check_result_with_expected_query_ret(ret_added[1], 1, 2, 1, &value_string2, (unsigned int *)added_value, metric_name);

    fieldstat_free(instance);
    fieldstat_free(instance2);
}

// TODO：性能测试等


int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}