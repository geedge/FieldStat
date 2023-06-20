
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

// 满规格测试，instance里增加的数量超过初始值，realloc
// 性能测试

TEST(metric_test_histogram, serialize_and_deserialize_for_2_fields_1st_empty_2nd_added)
{
    struct fieldstat *instance = fieldstat_new();
    printf("new ok\n");

    int id1_empty = fieldstat_register_histogram(instance, "empty", 1, 60000000, 3);
    int id2_added = fieldstat_register_histogram(instance, "add", 1, 60000000, 3);
    printf("register OK\n");

    // metric add
    long long value_add_twice = 1234;
    long long value_add_once = 123;
    fieldstat_histogram_record(instance, id2_added, value_add_twice, &TEST_TAG_DOUBLE, 1);
    fieldstat_histogram_record(instance, id2_added, value_add_twice, &TEST_TAG_DOUBLE, 1);
    printf("add ok0\n");
    fieldstat_histogram_record(instance, id2_added, value_add_once, &TEST_TAG_DOUBLE, 1);

    printf("add ok1\n");
    long long value_add_to_new_metric = 1;
    fieldstat_histogram_record(instance, id2_added, value_add_to_new_metric, &TEST_TAG_INT, 1);
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
    struct fieldstat_tag_list tag_list_1[1] = {{0}};
    struct fieldstat_tag_list tag_list_2[2] = {{0}};
    int ret_empty_zerolength = fieldstat_get_tag_lists(instance_from_deserialize, id1_empty, tag_list_1, 0);
    int ret_2cell_2length = fieldstat_get_tag_lists(instance_from_deserialize, id2_added, tag_list_2, 2);
    int ret_2cell_1length = fieldstat_get_tag_lists(instance_from_deserialize, id2_added, tag_list_2, 1);
    printf("query tag ok\n");
    EXPECT_EQ(ret_empty_zerolength, 0);
    EXPECT_EQ(ret_2cell_2length, 2);
    EXPECT_EQ(ret_2cell_1length, -1);

    EXPECT_EQ(tag_list_2[0].n_tag, 1);
    EXPECT_EQ(tag_list_2[1].n_tag, 1);
    EXPECT_STREQ(tag_list_2[0].tag[0].key, TEST_TAG_DOUBLE.key);
    EXPECT_EQ(tag_list_2[0].tag[0].value_double, TEST_TAG_DOUBLE.value_double);
    EXPECT_STREQ(tag_list_2[1].tag[0].key, TEST_TAG_INT.key);
    EXPECT_EQ(tag_list_2[1].tag[0].value_int, TEST_TAG_INT.value_int);
    printf("test tag done\n");

    // query metric 
    long long ret_double_tagged = fieldstat_histogram_value_at_percentile(instance_from_deserialize, id2_added, &tag_list_2[0], 50.0);
    printf("query ok 1\n");
    EXPECT_EQ(ret_double_tagged, value_add_twice);

    long long ret_int_tagged = fieldstat_histogram_value_at_percentile(instance_from_deserialize, id2_added, &tag_list_2[1], 50.0);
    printf("query ok 2\n");
    EXPECT_EQ(ret_int_tagged, value_add_to_new_metric);
    printf("check finish\n");

    for (int i = 0; i < ret_2cell_2length; i++) {
        tag_list_clear(&tag_list_2[i]);
    }
    printf("clear tag list ok\n");

    fieldstat_free(instance);
    fieldstat_free(instance_from_deserialize);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}