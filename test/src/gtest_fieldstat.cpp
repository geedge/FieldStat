#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "fieldstat.h"
#include "fieldstat_internal.h"

extern struct prometheus_endpoint_instance g_prometheus_endpoint_instance;

TEST(FeildStatAPI, CreateFieldStatInstance)
{
	struct fieldstat_instance *instance = fieldstat_instance_new("test");
	EXPECT_NE(nullptr, instance);
	EXPECT_STREQ("test", instance->name);
}

TEST(FeildStatAPI, FieldStatEnablePrometheusEndpoint)
{
	int ret_enable_prometheus_endpoint = 0;
	int ret_req_prometheus_endpoint = 0;

	ret_enable_prometheus_endpoint = fieldstat_global_enable_prometheus_endpoint(9020, "/metrcis");
	EXPECT_EQ(0, ret_enable_prometheus_endpoint);

	ret_req_prometheus_endpoint = system("curl -s -o /dev/null http://127.0.0.1:9020/metrics");
	EXPECT_EQ(0,ret_req_prometheus_endpoint);
}

TEST(FeildStatAPI, FieldStatSetPrometheusOutput)
{
	struct fieldstat_instance *instance = NULL;
	int ret_enable_prometheus_endpoint = 0;
	int ret_set_prometheus_output = 0;

	instance = fieldstat_instance_new("test");
	EXPECT_STREQ("test", instance->name);

	ret_enable_prometheus_endpoint = fieldstat_global_enable_prometheus_endpoint(9020, "/metrcis");
	EXPECT_EQ(0, ret_enable_prometheus_endpoint);

	ret_set_prometheus_output = fieldstat_enable_prometheus_output(instance);
	EXPECT_EQ(0, ret_set_prometheus_output);
	
	EXPECT_EQ(1, g_prometheus_endpoint_instance.running);
	EXPECT_EQ(instance, g_prometheus_endpoint_instance.fs_instance[0]);
	EXPECT_EQ(1, g_prometheus_endpoint_instance.fs_instance_cnt);
}

TEST(FeildStatAPI, FieldStatSetLineProtocolServer)
{
	struct fieldstat_instance *instance = NULL;	
	int ret_set_line_protocol_server = 0;

	instance = fieldstat_instance_new("test");
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
	instance = fieldstat_instance_new("test");
	EXPECT_STREQ("test", instance->name);

	ret_set_local_output = fieldstat_set_local_output(instance,"test.fs","default");
	EXPECT_EQ(0, ret_set_local_output);

	EXPECT_STREQ("test.fs", instance->local_output_filename);
	EXPECT_STREQ("default", instance->local_output_format);
	EXPECT_EQ(1, instance->local_output_enable);
}

int fieldstat_disable_background_thread(struct fieldstat_instance *instance);

TEST(FeildStatAPI, FieldStatBackgroundThreadDisable)
{
	struct fieldstat_instance *instance = NULL;
	int ret_background_thread_disable  = 0;
	instance = fieldstat_instance_new("test");
	EXPECT_STREQ("test", instance->name);

	ret_background_thread_disable = fieldstat_disable_background_thread(instance);
	EXPECT_EQ(0, ret_background_thread_disable);
	EXPECT_EQ(1, instance->background_thread_disable);
}

TEST(FeildStatAPI, FieldStatSetOutputInterval)
{
	struct fieldstat_instance *instance = NULL;
	int ret_set_output_interval = 0;

	instance = fieldstat_instance_new("test");
	EXPECT_STREQ("test", instance->name);

	ret_set_output_interval = fieldstat_set_output_interval(instance, 1);
	EXPECT_EQ(0, ret_set_output_interval);
	EXPECT_EQ(1, instance->output_interval_ms);
}

TEST(FeildStatAPI, FieldStatRegister)
{
	struct fieldstat_instance * instance = NULL;
	int metric_id = -1;
	struct metric **in_block_metric = NULL;
	struct fieldstat_tag tags[2];
	tags[0].value_type = 2;
	tags[0].value_str = "127.0.0.1";
	tags[0].key = "dest_ip";

	tags[1].value_type = 2;
	tags[1].value_str = "127.0.0.1";
	tags[1].key = "src_ip";


	instance = fieldstat_instance_new("test");
	EXPECT_STREQ("test", instance->name);

	metric_id = fieldstat_register(instance, FIELD_TYPE_COUNTER, "Traffic_bytes",NULL,0);
	EXPECT_EQ(0, metric_id);
	EXPECT_EQ(1, instance->metric_cnt);
	EXPECT_NE(nullptr, instance->metric_block_list[0]);
	in_block_metric = instance->metric_block_list[0];
	EXPECT_NE(nullptr, in_block_metric[0]);

	metric_id = fieldstat_register(instance, FIELD_TYPE_GAUGE, "Active_sessions", NULL, 0);
	EXPECT_EQ(1, metric_id);
	EXPECT_EQ(2, instance->metric_cnt);
	EXPECT_NE(nullptr, in_block_metric[1]);

	metric_id = fieldstat_register(instance, FIELD_TYPE_COUNTER, "New_sessions", tags, sizeof(tags)/sizeof(tags[0]));
	EXPECT_EQ(2, metric_id);
	EXPECT_EQ(3, instance->metric_cnt);
	EXPECT_NE(nullptr, in_block_metric[2]);
}

TEST(FieldStatAPI, FieldStatRegisterTable)
{
	struct fieldstat_instance * instance = NULL;
	int table_id = -1;
	struct table_metric *table = NULL;

	const char *shaping_column_name[] = {"packets","bytes"};
	const char *shaping_table_name = "shaping";
	enum field_type shaping_column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};

	const char *sc_column_name[] = {"sent_pkts", "send_bytes", "recv_pkts", "recv_bytes"};
	enum field_type sc_column_type[] = {FIELD_TYPE_COUNTER,FIELD_TYPE_COUNTER,FIELD_TYPE_COUNTER,FIELD_TYPE_COUNTER};
	const char *sc_table_name = "service_chaining";

	instance = fieldstat_instance_new("test");
	EXPECT_STREQ("test", instance->name);

	table_id = fieldstat_register_table(instance, shaping_table_name, shaping_column_name, shaping_column_type, sizeof(shaping_column_name)/sizeof(shaping_column_name[0]));
	EXPECT_EQ(0, table_id);
	EXPECT_NE(nullptr, instance->table_metrics[0]);
	table = instance->table_metrics[0];
	EXPECT_EQ(2, table->column_cnt);
	EXPECT_EQ(0, table->line_cnt);
	EXPECT_EQ(nullptr, table->line_block[0]);

	table_id = fieldstat_register_table(instance, sc_table_name, sc_column_name, sc_column_type, sizeof(sc_column_name)/sizeof(sc_column_name[0]));

	EXPECT_EQ(1, table_id);
	EXPECT_NE(nullptr, instance->table_metrics[1]);
	table = instance->table_metrics[1];
	EXPECT_EQ(4, table->column_cnt);
	EXPECT_EQ(0, table->line_cnt);
	EXPECT_EQ(nullptr, table->line_block[0]);
}


TEST(FieldStatAPI, FieldStatRegisterTableMetrics)
{
	struct fieldstat_instance * instance = NULL;
	int table_id = -1;
	struct table_metric *table = NULL;

	const char *sc_column_name[] = {"sent_pkts", "send_bytes", "recv_pkts", "recv_bytes"};
	enum field_type sc_column_type[] = {FIELD_TYPE_COUNTER,FIELD_TYPE_COUNTER,FIELD_TYPE_COUNTER,FIELD_TYPE_COUNTER};
	const char *sc_table_name = "service_chaining";


	instance = fieldstat_instance_new("test");
	EXPECT_STREQ("test", instance->name);

	table_id = fieldstat_register_table(instance, sc_table_name, sc_column_name, sc_column_type, sizeof(sc_column_name)/sizeof(sc_column_name[0]));

}


int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
    return 0;
}