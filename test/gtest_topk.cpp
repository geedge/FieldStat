
#include <gtest/gtest.h>

// #include "utils.hpp"

#include "fieldstat.h"
#include "field_and_metric.h"

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

TEST(myczzzzzzz, FieldStatDynamicInstanceSetPara)
{
    struct fieldstat *instance = fieldstat_new("czz");

    int topk1_id = fieldstat_register_topk(instance, "topk", 10);
    std::string topk1_name = gen_rand_string(10);
    fieldstat_topk_add(instance, topk1_id, topk1_name.c_str(), topk1_name.length(), &TAG_STRING, 1);

    fieldstat_free(instance);

    EXPECT_TRUE(1);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}