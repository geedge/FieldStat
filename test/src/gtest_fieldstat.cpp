#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "fieldstat.h"
#include "fieldstat_internal.h"

extern struct prometheus_endpoint_instance g_prometheus_endpoint_instance;

TEST(FeildStatAPI, CreateFieldStatInstance)
{
	struct fieldstat_instance *instance = fieldstat_instance_create("test");
	EXPECT_NE(nullptr, instance);
	EXPECT_STREQ("test", instance->name);
}

TEST(FeildStatAPI, FieldStatEnablePrometheusEndpoint)
{
	int ret_enable_prometheus_endpoint = 0;
	int ret_req_prometheus_endpoint = 0;

	ret_enable_prometheus_endpoint = fieldstat_global_enable_prometheus_endpoint(9020, "/metrcis");
	EXPECT_EQ(0, ret_enable_prometheus_endpoint);

	ret_req_prometheus_endpoint = system("curl http://127.0.0.1:9020/metrics");
	EXPECT_EQ(0,ret_req_prometheus_endpoint);
}

TEST(FeildStatAPI, FieldStatSetPrometheusOutput)
{
	struct fieldstat_instance *instance = NULL;
	int ret_enable_prometheus_endpoint = 0;
	int ret_set_prometheus_output = 0;

	instance = fieldstat_instance_create("test");
	EXPECT_STREQ("test", instance->name);

	ret_enable_prometheus_endpoint = fieldstat_global_enable_prometheus_endpoint(9020, "/metrcis");
	EXPECT_EQ(0, ret_enable_prometheus_endpoint);

	ret_set_prometheus_output = fieldstat_set_prometheus_output(instance);
	EXPECT_EQ(0, ret_set_prometheus_output);
	
	EXPECT_EQ(1, g_prometheus_endpoint_instance.running);
	EXPECT_EQ(instance, g_prometheus_endpoint_instance.fs_instance[0]);
	EXPECT_EQ(1, g_prometheus_endpoint_instance.fs_instance_cnt);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
    return 0;
}