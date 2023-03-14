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

TEST(FeildStatAPI, FieldStatSetLineProtocolServer)
{
	struct fieldstat_instance *instance = NULL;	
	int ret_set_line_protocol_server = 0;

	instance = fieldstat_instance_create("test");
	EXPECT_STREQ("test", instance->name);

	ret_set_line_protocol_server = fieldstat_set_line_protocol_server(instance, "127.0.0.1", 8001);

	EXPECT_EQ(0, ret_set_line_protocol_server);
	EXPECT_EQ(1, instance->line_protocol_output_enable);
	EXPECT_NE(-1, instance->line_protocol_socket);
}

TEST(FeildStatAPI, FieldStatSetLocalOutput)
{
	struct fieldstat_instance *instance = NULL;
	int ret_set_local_output  = 0;
	instance = fieldstat_instance_create("test");
	EXPECT_STREQ("test", instance->name);

	ret_set_local_output = fieldstat_set_local_output(instance,"test.fs","default");
	EXPECT_EQ(0, ret_set_local_output);

	EXPECT_STREQ("test.fs", instance->local_output_filename);
	EXPECT_STREQ("default", instance->local_output_format);
	EXPECT_EQ(1, instance->local_output_enable);
}

int fieldstat_background_thread_disable(struct fieldstat_instance *instance);

TEST(FeildStatAPI, FieldStatBackgroundThreadDisable)
{
	struct fieldstat_instance *instance = NULL;
	int ret_background_thread_disable  = 0;
	instance = fieldstat_instance_create("test");
	EXPECT_STREQ("test", instance->name);

	ret_background_thread_disable = fieldstat_background_thread_disable(instance);
	EXPECT_EQ(0, ret_background_thread_disable);
	EXPECT_EQ(1, instance->background_thread_disable);
}

TEST(FeildStatAPI, FieldStatSetOutputInterval)
{
	struct fieldstat_instance *instance = NULL;
	int ret_set_output_interval = 0;

	instance = fieldstat_instance_create("test");
	EXPECT_STREQ("test", instance->name);

	ret_set_output_interval = fieldstat_set_output_interval(instance, 1);
	EXPECT_EQ(0, ret_set_output_interval);
	EXPECT_EQ(1, instance->output_interval_s);
}

TEST(FeildStatAPI, FieldStatRegister)
{
	struct fieldstat_instance * instance = NULL;
	int metric_id = -1;
	struct metric_t **metric_in_block = NULL;
	const char *tag_key[] = {"dest_ip", "src_ip"};
	const char *tag_value[] = {"127.0.0.1", "127.0.0.1"};

	instance = fieldstat_instance_create("test");
	EXPECT_STREQ("test", instance->name);

	metric_id = fieldstat_register(instance, FIELD_TYPE_COUNTER, "Traffic_bytes",NULL,NULL,0);
	EXPECT_EQ(0, metric_id);
	EXPECT_EQ(1, instance->metric_cnt);
	EXPECT_NE(nullptr, instance->metric_block_list[0]);
	metric_in_block = instance->metric_block_list[0];
	EXPECT_NE(nullptr, metric_in_block[0]);

	metric_id = fieldstat_register(instance, FIELD_TYPE_GAUGE, "Active_sessions", NULL, NULL, 0);
	EXPECT_EQ(1, metric_id);
	EXPECT_EQ(2, instance->metric_cnt);
	EXPECT_NE(nullptr, metric_in_block[1]);

	metric_id = fieldstat_register(instance, FIELD_TYPE_COUNTER, "New_sessions",tag_key, tag_value, sizeof(tag_key)/sizeof(tag_key[0]));
	EXPECT_EQ(2, metric_id);
	EXPECT_EQ(3, instance->metric_cnt);
	EXPECT_NE(nullptr, metric_in_block[2]);

}





int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
    return 0;
}