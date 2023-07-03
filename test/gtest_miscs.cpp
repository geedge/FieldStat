// TODO
// 不同tag组合
// 空的instance
// realloc for field
//  超长tag，测试动态key的realloc
// 超多metric，直到realloc

// name map id

#include <gtest/gtest.h>

#include "utils.hpp"

#include "fieldstat.h"



TEST(metric_test_miscs, tag_parsing_empty)
{
    struct fieldstat *instance = fieldstat_new();
    printf("new ok\n");

    const char *metric_name[] = {"test_bytes", "test_packet_num"};
    unsigned int added_value[] = {1, 10000};

    int id1_empty = fieldstat_register_topk(instance, "topk_empty", 10, metric_name, 2, 1);
    int id2_added = fieldstat_register_topk(instance, "topk_add", 10, metric_name, 2, 1);
    printf("register OK\n");
    // register metric 
    std::string value_string_2_1_1 = gen_rand_string(10);
    printf("going to add\n");
    fieldstat_topk_add(instance, id2_added, value_string_2_1_1.c_str(), value_string_2_1_1.length(), added_value, &TEST_TAG_STRING, 1);
    printf("add ok0\n");

    fieldstat_topk_add(instance, id2_added, value_string_2_1_1.c_str(), value_string_2_1_1.length(), added_value, &TEST_TAG_STRING, 1);
    printf("add ok1\n");
    std::string value_string_2_1_2 = gen_rand_string(10);
    fieldstat_topk_add(instance, id2_added, value_string_2_1_2.c_str(), value_string_2_1_2.length(), added_value, &TEST_TAG_INT, 1);
    printf("add ok2\n");

    struct fieldstat *instance_from_deserialize = test_serialize_and_deserialize(instance);

    // query, tag
    struct fieldstat_tag_list *tag_list_1, *tag_list_2;
    size_t ret_empty_zerolength, ret_2cell_2length;
    fieldstat_get_tag_lists(instance_from_deserialize, id1_empty, &tag_list_1, &ret_empty_zerolength);
    fieldstat_get_tag_lists(instance_from_deserialize, id2_added, &tag_list_2, &ret_2cell_2length);
    printf("query tag ok\n");
    EXPECT_EQ(ret_empty_zerolength, 0);
    EXPECT_EQ(ret_2cell_2length, 2);

    EXPECT_EQ(tag_list_2[0].n_tag, 1);
    EXPECT_EQ(tag_list_2[1].n_tag, 1);
    EXPECT_STREQ(tag_list_2[0].tag[0].key, TEST_TAG_STRING.key);
    EXPECT_STREQ(tag_list_2[0].tag[0].value_str, TEST_TAG_STRING.value_str);
    EXPECT_STREQ(tag_list_2[1].tag[0].key, TEST_TAG_INT.key);
    EXPECT_EQ(tag_list_2[1].tag[0].value_int, TEST_TAG_INT.value_int);
    printf("test tag done\n");

    // query topk result
    char **key_result = NULL;
    size_t n_result = 0;
    size_t n_metric = 0;
    char **metric_name_result = NULL;
    size_t primary_key_id;
    unsigned int **count_result = NULL;

    fieldstat_topk_query(instance_from_deserialize, id2_added, &tag_list_2[0],
                         &key_result, &n_result, &metric_name_result, &n_metric, &primary_key_id, &count_result);

    printf("query topk ok\n");
    EXPECT_EQ(n_result, 1);
    EXPECT_STREQ(key_result[0], value_string_2_1_1.c_str());
    EXPECT_EQ(n_metric, 2);
    EXPECT_EQ(count_result[0][0], 2);
    EXPECT_EQ(count_result[0][1], 20000);
    EXPECT_STREQ(metric_name_result[0], metric_name[0]);
    EXPECT_STREQ(metric_name_result[1], metric_name[1]);
    EXPECT_EQ(primary_key_id, 1);

    free(key_result[0]);
    free(key_result);
    for (size_t i = 0; i < n_metric; i++) {
        free(metric_name_result[i]);
    }
    free(metric_name_result);
    for (size_t i = 0; i < n_result; i++) {
        free(count_result[i]);
    }
    free(count_result);

    fieldstat_topk_query(instance_from_deserialize, id2_added, &tag_list_2[1],
                            &key_result, &n_result, &metric_name_result, &n_metric, &primary_key_id, &count_result);
    EXPECT_EQ(n_result, 1);
    EXPECT_STREQ(key_result[0], value_string_2_1_2.c_str());
    EXPECT_EQ(n_metric, 2);
    EXPECT_EQ(count_result[0][0], 1);
    EXPECT_EQ(count_result[0][1], 10000);
    EXPECT_STREQ(metric_name_result[0], metric_name[0]);
    EXPECT_STREQ(metric_name_result[1], metric_name[1]);
    EXPECT_EQ(primary_key_id, 1);

    printf("check finish\n");
    free(key_result[0]);
    free(key_result);
    for (size_t i = 0; i < n_metric; i++) {
        free(metric_name_result[i]);
    }
    free(metric_name_result);
    free(count_result[0]);
    free(count_result);
    
    printf("clear query result ok\n");
    for (size_t i = 0; i < ret_2cell_2length; i++) {
        fieldstat_clear_tag_list(&tag_list_2[i]);
    }
    free(tag_list_2);
    free(tag_list_1);
    printf("clear tag list ok\n");

    fieldstat_free(instance);
    fieldstat_free(instance_from_deserialize);
}

//TODO: 不同的metric个数，再来一遍

// TODO：性能测试等



int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}