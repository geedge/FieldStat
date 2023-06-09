
#include <gtest/gtest.h>

#include "fieldstat.h"

TEST(myczzzzzzz, FieldStatDynamicInstanceSetPara)
{
    struct fieldstat *instance = fieldstat_new("czz");

    fieldstat_free(instance);

    EXPECT_TRUE(1);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}