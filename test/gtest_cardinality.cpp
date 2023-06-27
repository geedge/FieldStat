
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

// TEST(metric_test_cardinality, serialize_and_deserialize_for_2_fields_1st_empty_2nd_added)
// {
//     struct fieldstat *instance = fieldstat_new();
//     printf("new ok\n");

//     int id1_empty = fieldstat_register_cardinality(instance, "empty", 10);
//     int id2_added = fieldstat_register_cardinality(instance, "add", 10);
//     printf("register OK\n");

//     // metric add
//     std::string add_to_1_1 = gen_rand_string(10);
//     std::string add_to_1_2 = gen_rand_string(10);
//     std::string add_to_1_3 = gen_rand_string(10);
//     std::string add_to_2_1 = gen_rand_string(10);
//     // add 3 elements to 1st field
//     fieldstat_cardinality_add(instance, id2_added, add_to_1_1.c_str(), add_to_1_1.size(), &TEST_TAG_INT, 1);
//     fieldstat_cardinality_add(instance, id2_added, add_to_1_2.c_str(), add_to_1_2.size(), &TEST_TAG_INT, 1);
//     fieldstat_cardinality_add(instance, id2_added, add_to_1_3.c_str(), add_to_1_3.size(), &TEST_TAG_INT, 1);
//     // add 1 elements to 2st field
//     fieldstat_cardinality_add(instance, id2_added, add_to_2_1.c_str(), add_to_2_1.size(), &TEST_TAG_INT_collided, 1);
//     printf("add ok\n");

//     // serialize 
//     char *blob = NULL;
//     size_t blob_size = 0;
//     fieldstat_serialize(instance, &blob, &blob_size);
//     printf("blob_size: %zu\n", blob_size);
//     printf("blob content: %s\n", blob);

//     // deserialize
//     struct fieldstat *instance_from_deserialize = fieldstat_deserialize(blob, blob_size);
//     printf("deserialize ok\n");
//     free(blob);

//     // query, tag
//     struct fieldstat_tag_list *tag_list_1, *tag_list_2;
//     size_t ret_empty_zerolength, ret_2cell_2length;
//     fieldstat_get_tag_lists(instance_from_deserialize, id1_empty, &tag_list_1, &ret_empty_zerolength);
//     fieldstat_get_tag_lists(instance_from_deserialize, id2_added, &tag_list_2, &ret_2cell_2length);

//     printf("query tag ok\n");
//     EXPECT_EQ(ret_empty_zerolength, 0);
//     EXPECT_EQ(ret_2cell_2length, 2);

//     EXPECT_EQ(tag_list_2[0].n_tag, 1);
//     EXPECT_EQ(tag_list_2[1].n_tag, 1);
//     EXPECT_STREQ(tag_list_2[0].tag[0].key, TEST_TAG_INT.key);
//     EXPECT_EQ(tag_list_2[0].tag[0].value_int, TEST_TAG_INT.value_int);
//     EXPECT_STREQ(tag_list_2[1].tag[0].key, TEST_TAG_INT_collided.key);
//     EXPECT_EQ(tag_list_2[1].tag[0].value_int, TEST_TAG_INT_collided.value_int);
//     printf("test tag done\n");

//     // query metric 
//     double ret1 = fieldstat_cardinality_get(instance_from_deserialize, id2_added, &tag_list_2[0]);
//     printf("query ok 1\n");
//     EXPECT_NEAR(ret1, 3, 0.5);

//     double ret2 = fieldstat_cardinality_get(instance_from_deserialize, id2_added, &tag_list_2[1]);
//     printf("query ok 2\n");
//     EXPECT_NEAR(ret2, 1, 0.5);

//     for (size_t i = 0; i < ret_2cell_2length; i++) {
//         tag_list_clear(&tag_list_2[i]);
//     }
//     free(tag_list_2);
//     free(tag_list_1);
//     printf("clear tag list ok\n");

//     fieldstat_free(instance);
//     fieldstat_free(instance_from_deserialize);
// }


#define BigLittleSwap32(A)        ((((uint32_t)(A) & 0xff000000) >> 24) | \
                                                       (((uint32_t)(A) & 0x00ff0000) >> 8) | \
                                                       (((uint32_t)(A) & 0x0000ff00) << 8) | \
                                                       (((uint32_t)(A) & 0x000000ff) << 24))

TEST(metric_test_cardinality, encode_and_decode_cell_with_base64)
{
    struct fieldstat *instance = fieldstat_new();
    const int precision_benchmark = 12;

    int id = fieldstat_register_cardinality(instance, "add", precision_benchmark);

    // metric add
    int added_num = 10000;
    for (int i = 0; i < added_num; i++) {
        std::string added_tmp = std::to_string(i);
        fieldstat_cardinality_add(instance, id, added_tmp.c_str(), added_tmp.size(), &TEST_TAG_INT, 1);
    }

    // query, tag
    struct fieldstat_tag_list *tag_list;
    size_t tag_len;
    fieldstat_get_tag_lists(instance, id, &tag_list, &tag_len);
    EXPECT_EQ(tag_len, 1);

    char *blob = NULL;
    size_t blob_size = 0;
    fieldstat_get_serialized_blob(instance, id, &tag_list[0], &blob, &blob_size);
    printf("base 64 result: \n%s\n", blob);
    printf("\n------------------------------------------------\n");

    size_t dec_size = 0;
    unsigned char *dec = b64_decode_ex(blob, blob_size, &dec_size);
    // printf("base 64 decode result:\n");
    // for (size_t i = 0; i < dec_size; i++) {
    //     printf("%02x ", dec[i]);
    // }
    // printf("\n------------------------------------------------\n");

    struct ST_hyperloglog *hll = ST_hyperloglog_new(precision_benchmark);
    for (int i = 0; i < added_num; i++) {
        std::string added_tmp = std::to_string(i);
        ST_hyperloglog_add(hll, added_tmp.c_str(), added_tmp.size());
    }

    int num_reg = NUM_REG(precision_benchmark);
    // Get the full words required
    int words = INT_CEIL(num_reg, REG_PER_WORD);
    size_t reg_size = words * sizeof(uint32_t);
    uint32_t *recovered_reg = (uint32_t *)malloc(reg_size);
    memcpy(recovered_reg, dec + 2, reg_size);
    for (int i = 0; i < words; i++) {
        uint32_t tmp = BigLittleSwap32(recovered_reg[i]);
        EXPECT_EQ(tmp, hll->registers[i]);
    }
    unsigned char precision;
    memcpy(&precision, dec, 1);
    EXPECT_EQ(precision, precision_benchmark);

    double ret = fieldstat_cardinality_get(instance, id, &tag_list[0]);
    double benchmark = ST_hyperloglog_count(hll);
    EXPECT_EQ(ret, benchmark);

    for (size_t i = 0; i < tag_len; i++) {
        tag_list_clear(&tag_list[i]);
    }
    free(tag_list);


    // debug: add another metric
    // TODO: 删了
    for (int i = 0; i < added_num; i++) {
        std::string added_tmp = std::to_string(i) + std::string("b ver");
        fieldstat_cardinality_add(instance, id, added_tmp.c_str(), added_tmp.size(), &TEST_TAG_INT_collided, 1);
    }
    char *blob2 = NULL;
    size_t blob_size2 = 0;

    struct fieldstat_tag_list tag_list2 = {.tag = (struct fieldstat_tag *)&TEST_TAG_INT_collided, .n_tag = 1};
    
    fieldstat_get_serialized_blob(instance, id, &tag_list2, &blob2, &blob_size2);
    printf("base 64 result blob2: \n%s\n", blob2);

    fieldstat_free(instance);
    free(blob);
}


int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}