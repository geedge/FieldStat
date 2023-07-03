
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

Query_ret topk_query(struct fieldstat *instance, int field_id, const struct fieldstat_tag_list *tag_list)
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

TEST(metric_test_topk, serialize_and_deserialize_for_2_fields_1st_empty_2nd_added)
{
    struct fieldstat *instance = fieldstat_new();

    const char *metric_name[] = {"test_bytes", "test_packet_num"};
    unsigned int added_value[] = {1, 10000};
    std::string value_string_2_1_2 = gen_rand_string(10);
    std::string value_string_2_1_1 = gen_rand_string(10);

    int id1_empty = fieldstat_register_topk(instance, "topk_empty", 10, metric_name, 2, 1);
    int id2_added = fieldstat_register_topk(instance, "topk_add", 10, metric_name, 2, 1);
    // register metric 
    fieldstat_topk_add(instance, id2_added, value_string_2_1_1.c_str(), value_string_2_1_1.length(), added_value, &TEST_TAG_STRING, 1);
    fieldstat_topk_add(instance, id2_added, value_string_2_1_1.c_str(), value_string_2_1_1.length(), added_value, &TEST_TAG_STRING, 1);
    fieldstat_topk_add(instance, id2_added, value_string_2_1_2.c_str(), value_string_2_1_2.length(), added_value, &TEST_TAG_INT, 1);

    struct fieldstat *instance_from_deserialize = test_serialize_and_deserialize(instance);

    // query and test
    auto ret_added = test_get_query_result<Query_ret>(topk_query, instance_from_deserialize, id2_added);
    auto ret_empty = test_get_query_result<Query_ret>(topk_query, instance_from_deserialize, id1_empty);
    EXPECT_TRUE(ret_added.size() == 2);
    EXPECT_TRUE(ret_empty.size() == 0);

    EXPECT_EQ(ret_added[0].n_result, 1);
    EXPECT_STREQ(ret_added[0].key_result[0].c_str(), value_string_2_1_1.c_str());
    EXPECT_EQ(ret_added[0].n_metric, 2);
    EXPECT_EQ(ret_added[0].count_result[0][0], 2);
    EXPECT_EQ(ret_added[0].count_result[0][1], 20000);
    EXPECT_STREQ(ret_added[0].metric_name[0].c_str(), metric_name[0]);
    EXPECT_STREQ(ret_added[0].metric_name[1].c_str(), metric_name[1]);
    EXPECT_EQ(ret_added[0].primary_metric_id, 1);

    EXPECT_EQ(ret_added[1].n_result, 1);
    EXPECT_STREQ(ret_added[1].key_result[0].c_str(), value_string_2_1_2.c_str());
    EXPECT_EQ(ret_added[1].n_metric, 2);
    EXPECT_EQ(ret_added[1].count_result[0][0], 1);
    EXPECT_EQ(ret_added[1].count_result[0][1], 10000);
    EXPECT_STREQ(ret_added[1].metric_name[0].c_str(), metric_name[0]);
    EXPECT_STREQ(ret_added[1].metric_name[1].c_str(), metric_name[1]);
    EXPECT_EQ(ret_added[1].primary_metric_id, 1);

    fieldstat_free(instance);
    fieldstat_free(instance_from_deserialize);
}


TEST(metric_test_topk, serialize_and_merge_by_blob_twice_expecting_double_result)
{
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat *instance2 = fieldstat_new();

    const char *metric_name[] = {"test_bytes", "test_packet_num"};
    unsigned int added_value[] = {1, 10000};
    std::string value_string = gen_rand_string(10);

    int id_added = fieldstat_register_topk(instance, "topk_add", 10, metric_name, 2, 1);
    // register metric 
    fieldstat_topk_add(instance, id_added, value_string.c_str(), value_string.length(), added_value, &TEST_TAG_INT, 1);

    char *blob = NULL;
    size_t blob_size = 0;
    fieldstat_serialize(instance, &blob, &blob_size);
    fieldstat_merge_with_blob(instance2, blob, blob_size);
    fieldstat_merge_with_blob(instance2, blob, blob_size);
    free(blob);

    // query and test
    auto ret_added = test_get_query_result<Query_ret>(topk_query, instance2, 0);

    EXPECT_TRUE(ret_added.size() == 1);

    EXPECT_EQ(ret_added[0].n_result, 1);
    EXPECT_STREQ(ret_added[0].key_result[0].c_str(), value_string.c_str());
    EXPECT_EQ(ret_added[0].n_metric, 2);
    EXPECT_EQ(ret_added[0].count_result[0][0], 2);
    EXPECT_EQ(ret_added[0].count_result[0][1], 20000);
    EXPECT_STREQ(ret_added[0].metric_name[0].c_str(), metric_name[0]);
    EXPECT_STREQ(ret_added[0].metric_name[1].c_str(), metric_name[1]);
    EXPECT_EQ(ret_added[0].primary_metric_id, 1);

    fieldstat_free(instance);
    fieldstat_free(instance2);
}

TEST(metric_test_topk, serialize_and_merge_empty_to_empty)
{
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat *instance2 = fieldstat_new();

    const char *metric_name[] = {"test_bytes", "test_packet_num"};
    (void)fieldstat_register_topk(instance, "topk_add", 10, metric_name, 2, 1);

    char *blob = NULL;
    size_t blob_size = 0;
    fieldstat_serialize(instance, &blob, &blob_size);
    fieldstat_merge_with_blob(instance2, blob, blob_size);
    free(blob);

    auto ret_added = test_get_query_result<Query_ret>(topk_query, instance2, 0);
    EXPECT_TRUE(ret_added.size() == 0);

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

    char *blob = NULL;
    size_t blob_size = 0;
    fieldstat_serialize(instance_empty, &blob, &blob_size);
    fieldstat_merge_with_blob(instance_added, blob, blob_size);
    free(blob);

    auto ret_added = test_get_query_result<Query_ret>(topk_query, instance_added, id_added);
    EXPECT_TRUE(ret_added.size() == 1);
    EXPECT_EQ(ret_added[0].n_result, 1);
    EXPECT_STREQ(ret_added[0].key_result[0].c_str(), value_string.c_str());
    EXPECT_EQ(ret_added[0].n_metric, 2);
    EXPECT_EQ(ret_added[0].count_result[0][0], 1);
    EXPECT_EQ(ret_added[0].count_result[0][1], 10000);
    EXPECT_STREQ(ret_added[0].metric_name[0].c_str(), metric_name[0]);
    EXPECT_STREQ(ret_added[0].metric_name[1].c_str(), metric_name[1]);
    EXPECT_EQ(ret_added[0].primary_metric_id, 1);

    int all_field_num = fieldstat_get_max_field_id(instance_added) + 1;
    EXPECT_EQ(all_field_num, 2);
    auto ret_merged_empty = test_get_query_result<Query_ret>(topk_query, instance_added, 1);
    EXPECT_TRUE(ret_merged_empty.size() == 0);

    fieldstat_free(instance_empty);
    fieldstat_free(instance_added);
}

//TODO: 不同的metric个数，再来一遍

// TODO：性能测试等



int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}