// TODO
// 不同tag组合
// 空的instance
// realloc for field
//  超长tag，测试动态key的realloc

// 大规模序列化测试

#include <gtest/gtest.h>

#include "fieldstat_exporter.h"

TEST(export_test, cjson_export_with_fixed_tag_and_many_metrics)
{
    // new instance
    // register 4 metrics
    // gen 3 tag_lists
    // for every tag, call metric add for 4 different type
    // export
    
    // expect result: 
    // blob len is 3
    // map tag OK 
}