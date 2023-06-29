
#include <gtest/gtest.h>

#include "utils.hpp"

#include "fieldstat.h"


// serialize and merge by blob(empty dest), then create a new instance by the same register order, merge by blob(non-empty dest). query result on the merged one. Benchmark: tag, heavykeeper result.

// serialize and merge by blob(empty dest), then create a new instance by the same register order, refresh, merge by blob(non-empty dest). query result on the merged one. Benchmark: tag, heavykeeper result.

// multi field, same tag, add several times. 

// register and merge empty to empty

// register and merge empty to non-empty

// compare merged one to source, they are equal

// 简单的tag 测试：输入一个key， value 是”1“， 再输入一个value是1的tag，两次添加，只有一个tag，查阅出的tag也只有一个。 

// 性能测试

using namespace std;

const int K = 10;

class Query_ret {
public:
    string key_result[K];
    unsigned int count_result[K];
    int n_result;

    explicit Query_ret(char **keys, unsigned int *counts, int n)
    {
        n_result = n;
        for (int i = 0; i < n; i++)
        {
            key_result[i] = keys[i];
            count_result[i] = counts[i];
        }
    }
    
    bool operator==(const Query_ret& other) const
    {
        if (n_result != other.n_result)
        {
            return false;
        }
        for (int i = 0; i < n_result; i++)
        {
            if (key_result[i] != other.key_result[i] || count_result[i] != other.count_result[i])
            {
                return false;
            }
        }
        return true;
    }
};

TEST(metric_test_topk, serialize_and_deserialize_for_2_fields_1st_empty_2nd_added)
{
    struct fieldstat *instance = fieldstat_new();
    printf("new ok\n");

    const char *metric_name[] = {"test_bytes"};
    unsigned int added_value[] = {1};

    int id1_empty = fieldstat_register_topk(instance, "topk_empty", 10, metric_name, 1, 0);
    int id2_added = fieldstat_register_topk(instance, "topk_add", 10, metric_name, 1, 0);
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

    // serialize 
    char *blob = NULL;
    size_t blob_size = 0;
    fieldstat_serialize(instance, &blob, &blob_size);
    printf("blob_size: %zu\n", blob_size);
    printf("blob content: %s\n", blob);

    // deserialize
    struct fieldstat *instance_from_deserialize = fieldstat_deserialize(blob, blob_size);
    printf("deserialize ok\n");
    free(blob);

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


    // todo: 补这里的测试用例
    fieldstat_topk_query(instance_from_deserialize, id2_added, &tag_list_2[0],
                            &key_result, &n_result, &metric_name_result, &n_metric, &primary_key_id, &count_result);

    printf("query topk ok\n");
    EXPECT_EQ(n_result, 1);
    EXPECT_STREQ(key_result[0], value_string_2_1_1.c_str());
    EXPECT_EQ(count_result[0], 2);
    free(key_result[0]);
    free(key_result);
    free(count_result);

    fieldstat_topk_query(instance_from_deserialize, id2_added, &tag_list_2[1],
                            &key_result, &n_result, &metric_name_result, &n_metric, &primary_key_id, &count_result);
    EXPECT_EQ(n_result, 1);
    EXPECT_STREQ(key_result[0], value_string_2_1_2.c_str());
    EXPECT_EQ(count_result[0], 1);
    printf("check finish\n");
    free(key_result[0]);
    free(key_result);
    free(count_result);
    printf("clear query result ok\n");
    for (size_t i = 0; i < ret_2cell_2length; i++) {
        tag_list_clear(&tag_list_2[i]);
    }
    free(tag_list_2);
    free(tag_list_1);
    printf("clear tag list ok\n");

    fieldstat_free(instance);
    fieldstat_free(instance_from_deserialize);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}