
#include <gtest/gtest.h>

#include "utils.hpp"

#include "fieldstat.h"
#include "base64/b64.h"
#include "st_hyperloglog.h"


// serialize and merge by blob(empty dest), then create a new instance by the same register order, merge by blob(non-empty dest). query result on the merged one. Benchmark: tag, heavykeeper result.

// serialize and merge by blob(empty dest), then create a new instance by the same register order, refresh, merge by blob(non-empty dest). query result on the merged one. Benchmark: tag, heavykeeper result.

// multi field, same tag, add several times. 

// register and merge empty to empty

// register and merge empty to non-empty

// compare merged one to source, they are equal

// 简单的tag 测试：输入一个key， value 是”1“， 再输入一个value是1的tag，两次添加，只有一个tag，查阅出的tag也只有一个。 

// TODO: 准度测试

// 性能测试


// C++ 写： 
// 对于每个函数，均为随机（自己写一个随机生成器）进值。 create_fuzz_tester(Rand_gen) fuzz_tester.operfunc(函数指针) . first(FIXTO, ), rangeFrom()...  

TEST(metric_test_hll, serialize_and_deserialize_for_2_fields_1st_empty_2nd_added)
{
    struct fieldstat *instance = fieldstat_new();
    printf("new ok\n");

    int id1_empty = fieldstat_register_hll(instance, "empty", 10);
    int id2_added = fieldstat_register_hll(instance, "add", 10);
    printf("register OK\n");

    // metric add
    std::string add_to_1_1 = gen_rand_string(10);
    std::string add_to_1_2 = gen_rand_string(10);
    std::string add_to_1_3 = gen_rand_string(10);
    std::string add_to_2_1 = gen_rand_string(10);
    // add 3 elements to 1st field
    fieldstat_hll_add(instance, id2_added, add_to_1_1.c_str(), add_to_1_1.size(), &TEST_TAG_INT, 1);
    fieldstat_hll_add(instance, id2_added, add_to_1_2.c_str(), add_to_1_2.size(), &TEST_TAG_INT, 1);
    fieldstat_hll_add(instance, id2_added, add_to_1_3.c_str(), add_to_1_3.size(), &TEST_TAG_INT, 1);
    // add 1 elements to 2st field
    fieldstat_hll_add(instance, id2_added, add_to_2_1.c_str(), add_to_2_1.size(), &TEST_TAG_INT_collided, 1);
    printf("add ok\n");

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
    EXPECT_STREQ(tag_list_2[0].tag[0].key, TEST_TAG_INT.key);
    EXPECT_EQ(tag_list_2[0].tag[0].value_int, TEST_TAG_INT.value_int);
    EXPECT_STREQ(tag_list_2[1].tag[0].key, TEST_TAG_INT_collided.key);
    EXPECT_EQ(tag_list_2[1].tag[0].value_int, TEST_TAG_INT_collided.value_int);
    printf("test tag done\n");

    // query metric 
    double ret1 = fieldstat_hll_get(instance_from_deserialize, id2_added, &tag_list_2[0]);
    printf("query ok 1\n");
    EXPECT_NEAR(ret1, 3, 0.5);

    double ret2 = fieldstat_hll_get(instance_from_deserialize, id2_added, &tag_list_2[1]);
    printf("query ok 2\n");
    EXPECT_NEAR(ret2, 1, 0.5);

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