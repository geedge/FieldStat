
#include <gtest/gtest.h>

// #include "utils.hpp"

#include "fieldstat.h"

const struct fieldstat_tag TAG_STRING = {"IP CLIENT", TAG_CSTRING, {.value_str = "100.1.1.1"}};

std::string gen_rand_string(int len)
{
    std::string s;
    for (int i = 0; i < len; i++)
    {
        s += 'a' + rand() % 26;
    }
    return s;
}

// serialize and merge by blob(empty dest), then create a new instance by the same register order, merge by blob(non-empty dest). query result on the merged one. Benchmark: tag, heavykeeper result.

// serialize and merge by blob(empty dest), then create a new instance by the same register order, refresh, merge by blob(non-empty dest). query result on the merged one. Benchmark: tag, heavykeeper result.

// multi field, same tag, add several times. 

// register and merge empty to empty

// register and merge empty to non-empty

// compare merged one to source, they are equal


TEST(myczzzzzzz, FieldStatDynamicInstanceSetPara)
{
    // Tester.instance_name;
    // instance = Tester.get_instance;
    // instance...
    // [auto]Tester_serialize and merge and query
    // Tester.parse_field(name, expectedvalue); // if string STREQ... double APPROX_EQ

    struct fieldstat *instance = fieldstat_new("czz");
    // new instance

    int topk1_id = fieldstat_register_topk(instance, "topk", 10);
    // register metric 
    std::string topk1_name = gen_rand_string(10);
    fieldstat_topk_add(instance, topk1_id, topk1_name.c_str(), topk1_name.length(), &TAG_STRING, 1);
    // dynamicly add several 

    // serialize 

    // merge with blob

    // query (read field)
    // get benchmark(vary)

    fieldstat_free(instance);

    EXPECT_TRUE(1);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}