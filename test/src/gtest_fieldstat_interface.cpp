#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "fieldstat.h"
#include "fieldstat_internal.h"

extern struct prometheus_endpoint_instance g_prometheus_endpoint_instance;

TEST(FeildStatAPI, FieldStatInstanceNew)
{
	struct fieldstat_instance *instance = fieldstat_instance_new("test");
	EXPECT_NE(nullptr, instance);
	EXPECT_STREQ("test", instance->name);
	fieldstat_instance_free(instance);
}


TEST(FeildStatAPI, FieldStatEnablePrometheusEndpoint)
{
	int ret = 0;

	ret = fieldstat_global_enable_prometheus_endpoint(9020, "/metrcis");
	EXPECT_EQ(0, ret);
	usleep(500 * 1000);
	fieldstat_global_disable_prometheus_endpoint();
}

TEST(FeildStatAPI, FieldStatSetPrometheusOutput)
{
	struct fieldstat_instance *instance = NULL;
	int ret_enable_prometheus_endpoint = 0;
	int ret_set_prometheus_output = 0;

	instance = fieldstat_instance_new("test");
	EXPECT_STREQ("test", instance->name);

	ret_enable_prometheus_endpoint = fieldstat_global_enable_prometheus_endpoint(9020, "/metrics");
	EXPECT_EQ(0, ret_enable_prometheus_endpoint);

	ret_set_prometheus_output = fieldstat_enable_prometheus_output(instance);
	EXPECT_EQ(0, ret_set_prometheus_output);
	
	EXPECT_EQ(1, g_prometheus_endpoint_instance.running);
	EXPECT_EQ(instance, g_prometheus_endpoint_instance.fs_instance[0]);
	EXPECT_EQ(1, g_prometheus_endpoint_instance.fs_instance_cnt);

	usleep(500 * 1000);
	fieldstat_global_disable_prometheus_endpoint();
	fieldstat_instance_free(instance);
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
	EXPECT_NE(-1, instance->line_protocol_output.send_socket);
	fieldstat_instance_free(instance);
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
	fieldstat_instance_free(instance);
}

TEST(FeildStatAPI, FieldStatBackgroundThreadDisable)
{
	struct fieldstat_instance *instance = NULL;
	int ret_background_thread_disable  = 0;
	instance = fieldstat_instance_new("test");
	EXPECT_STREQ("test", instance->name);

	ret_background_thread_disable = fieldstat_disable_background_thread(instance);
	EXPECT_EQ(0, ret_background_thread_disable);
	EXPECT_EQ(1, instance->background_thread_disable);
	fieldstat_instance_free(instance);
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
	fieldstat_instance_free(instance);
}

TEST(FeildStatAPI, FieldStatInstanceStart)
{
	struct fieldstat_instance *instance = NULL;

	instance = fieldstat_instance_new("test");
	EXPECT_STREQ("test", instance->name);

	fieldstat_instance_start(instance);
	usleep(500 * 1000);
	fieldstat_instance_free(instance);
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
	fieldstat_instance_free(instance);
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

	fieldstat_instance_free(instance);
}


TEST(FieldStatAPI, FieldStatRegisterTableMetrics)
{
	struct fieldstat_instance * instance = NULL;
	int table_id = -1;

	const char *sc_column_name[] = {"sent_pkts", "send_bytes", "recv_pkts", "recv_bytes"};
	enum field_type sc_column_type[] = {FIELD_TYPE_COUNTER,FIELD_TYPE_COUNTER,FIELD_TYPE_COUNTER,FIELD_TYPE_COUNTER};
	const char *sc_table_name = "service_chaining";


	instance = fieldstat_instance_new("test");
	EXPECT_STREQ("test", instance->name);
	table_id = fieldstat_register_table(instance, sc_table_name, sc_column_name, sc_column_type, sizeof(sc_column_name)/sizeof(sc_column_name[0]));
	EXPECT_EQ(0, table_id);
	fieldstat_instance_free(instance);
}


TEST(FeildStatAPI, FieldStatLocalOutputFormatJson)
{
	int metric_id = -1;
	int ret_set_local_output = 0;
	struct fieldstat_instance * instance = NULL;
	int ret = 0;

	instance = fieldstat_instance_new("test");
	EXPECT_STREQ("test", instance->name);

	metric_id = fieldstat_register(instance, FIELD_TYPE_COUNTER, "Traffic_bytes",NULL,0);
	EXPECT_EQ(0, metric_id);

	ret_set_local_output = fieldstat_set_local_output(instance,"test.json","json");
	EXPECT_EQ(0, ret_set_local_output);
	instance->running = 1;

	ret = fieldstat_value_incrby(instance, metric_id, 10);
	EXPECT_EQ(0, ret);
	fieldstat_passive_output(instance);
	sleep(1);
	ret = fieldstat_value_incrby(instance, metric_id, 10);
	sleep(1);
	fieldstat_passive_output(instance);
	EXPECT_EQ(0, ret);
	instance->running = 0;
	fieldstat_instance_free(instance);	
}

TEST(FeildStatAPI, FieldStatLineProtocolOutputTableMetric)
{
	int ret = 0;
	int n_loops = 1;
	struct fieldstat_instance * instance = NULL;
	int table_id = -1;
	int output_metric_ids[54] = {0};
	const char *telegraf_output_file = "/tmp/metrics.out";
	FILE *fp;
	char line[2048] = {0};
	cJSON *cjson_metric = NULL;
	cJSON *cjson_tags = NULL;
	const char *compare = "{\"fields\":{\"alert_bytes\":0,\"allow_conn_num\":0,\"allow_in_bytes\":0,\"allow_in_packets\":0,\"allow_out_bytes\":0,"\
							"\"allow_out_packets\":0,\"block_bytes\":0,\"close_conn_num\":0,\"default_conn_num\":0,\"default_in_bytes\":0,"\
							"\"default_in_packets\":0,\"default_out_bytes\":0,\"default_out_packets\":0,\"deny_conn_num\":0,\"deny_in_bytes\":0,"\
							"\"deny_in_packets\":0,\"deny_out_bytes\":0,\"deny_out_packets\":0,\"established_conn_num\":0,\"intercept_conn_num\":0,"\
							"\"intercept_in_bytes\":0,\"intercept_in_packets\":0,\"intercept_out_bytes\":0,\"intercept_out_packets\":0,"\
							"\"ipv4_in_bytes\":0,\"ipv4_in_packets\":0,\"ipv4_out_packetsipv4_out_bytes\":0,\"ipv6_in_bytes\":0,\"ipv6_in_packets\":0,"\
							"\"ipv6_out_bytes\":0,\"ipv6_out_packets\":0,\"maybe_pinning_num\":0,\"monitor_conn_num\":0,\"monitor_in_bytes\":0,"\
							"\"monitor_in_packets\":0,\"monitor_out_bytes\":0,\"monitor_out_packets\":0,\"new_conn_num\":1000,\"not_pinning_num\":0,"\
							"\"pinning_num\":0,\"tcp_conn_num\":0,\"tcp_in_bytes\":0,\"tcp_in_packets\":0,\"tcp_out_bytes\":0,\"tcp_out_packets\":0,"\
							"\"total_in_bytes\":0,\"total_in_packets\":0,\"total_out_bytes\":0,\"total_out_packets\":0,\"udp_conn_num\":0,"\
							"\"udp_in_bytes\":0,\"udp_in_packets\":0,\"udp_out_bytes\":0,\"udp_out_packets\":0},\"name\":\"TRAFFIC\","\
							"\"tags\":{\"app_name\":\"tsg_statistic\",\"table_name\":\"network_activity\"}}";
	char *cjson_metric_str = NULL;

	const char *table_column_name[] = {"new_conn_num", "established_conn_num", "close_conn_num", "total_in_bytes", "total_out_bytes", "total_in_packets", 
	"total_out_packets", "default_conn_num", "default_in_bytes", "default_out_bytes", "default_in_packets", "default_out_packets", "allow_conn_num",
	"allow_in_bytes", "allow_out_bytes", "allow_in_packets", "allow_out_packets", "deny_conn_num", "deny_in_bytes", "deny_out_bytes", "deny_in_packets",
	"deny_out_packets", "monitor_conn_num", "monitor_in_bytes", "monitor_out_bytes", "monitor_in_packets", "monitor_out_packets", "intercept_conn_num",
	"intercept_in_bytes", "intercept_out_bytes", "intercept_in_packets", "intercept_out_packets", "ipv4_in_packets", "ipv4_in_bytes", "ipv4_out_packets"
	"ipv4_out_bytes", "ipv6_in_packets", "ipv6_in_bytes", "ipv6_out_packets", "ipv6_out_bytes", "tcp_conn_num", "tcp_in_packets", "tcp_in_bytes",
	"tcp_out_packets", "tcp_out_bytes", "udp_conn_num", "udp_in_packets", "udp_in_bytes", "udp_out_packets", "udp_out_bytes", "alert_bytes", "block_bytes",
	"pinning_num", "maybe_pinning_num", "not_pinning_num"};
	enum field_type table_column_type[54] = {FIELD_TYPE_COUNTER};

	instance = fieldstat_instance_new("tsg_statistic");
	EXPECT_STREQ("tsg_statistic", instance->name);

	table_id = fieldstat_register_table(instance, "network_activity", table_column_name, table_column_type, sizeof(table_column_name)/sizeof(table_column_name[0]));
	EXPECT_EQ(0, table_id);
	ret = fieldstat_register_table_row(instance, table_id, "TRAFFIC", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);

	ret = fieldstat_set_line_protocol_server(instance, "127.0.0.1", 8600);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(4, instance->output_type);
	ret = fieldstat_disable_background_thread(instance);
	EXPECT_EQ(0, ret);
	fieldstat_instance_start(instance);
	fieldstat_value_incrby(instance, output_metric_ids[0], 1000);
	ret = system("cat /dev/null > /tmp/metrics.out");
	sleep(1);
	fieldstat_passive_output(instance);
	sleep(1);
	fp = fopen(telegraf_output_file, "r");
	EXPECT_NE(nullptr, fp);
	int n_line = 0;

	while(!feof(fp))
	{
		if(NULL == fgets(line, sizeof(line), fp))
		{
			continue;
		}
		cjson_metric = cJSON_Parse(line);
		EXPECT_NE(nullptr, cjson_metric);
		cJSON_DeleteItemFromObject(cjson_metric, "timestamp");

		cjson_tags = cJSON_GetObjectItem(cjson_metric, "tags");
		EXPECT_NE(nullptr, cjson_tags);
		cJSON_DeleteItemFromObject(cjson_tags, "host");
		cjson_metric_str = cJSON_PrintUnformatted(cjson_metric);
		EXPECT_NE(nullptr, cjson_metric_str);
		if(cjson_metric_str)
		{
			EXPECT_STREQ(compare, cjson_metric_str);
			free(cjson_metric_str);
			cjson_metric_str = NULL;
		}
		cJSON_Delete(cjson_metric);
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(n_line, n_loops);

	fieldstat_instance_free(instance);
}

TEST(FeildStatAPI, SetMetricRatioPara)
{
	int ret = 0;
	struct fieldstat_instance * instance = NULL;
	int table_id = -1;
	int output_metric_ids[10] = {0};
	FILE *fp;
	int n_line = 0;
	int n_loops = 5;
	char line[2048] = {0};
	cJSON *cjson_metric = NULL;
	cJSON *cjson_tags = NULL;
	char *cjson_metric_str = NULL;

	const char *output_file_path = "/tmp/fieldstat.txt"; 
	const char *output_telegraf_path = "/tmp/metrics.out";
	const char *table_column_name[] = {"rule", "reg/v6", "stream", "IN_Bps", "cpu_us", "PROC_Bps", "IN_Tps", "PROC_Tps","hit_cnt", "hit_rate"};
	const char *compared_file_output = "Sum                                      0"
									"                   0            0.00e+00            5.00e+02"
									"            5.00e+00            5.00e+01            0.00e+00"
									"            0.00e+00            0.00e+00            0.00e+00\n";

	const char *compare = "{\"fields\":{\"IN_Bps\":1000,\"IN_Tps\":0,\"PROC_Tps\":0,\"cpu_us\":10,"
						  "\"hit_cnt\":0,\"hit_rate\":0,\"reg/v6\":0,\"rule\":0,\"stream\":0},"
						  "\"name\":\"Sum\",\"tags\":{\"app_name\":\"maat\",\"table_name\":\"maat_info\"}}";


	enum field_type table_column_type[10] = {FIELD_TYPE_GAUGE, FIELD_TYPE_GAUGE, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};

	instance = fieldstat_instance_new("maat");
	EXPECT_STREQ("maat", instance->name);

	table_id = fieldstat_register_table(instance, "maat_info", table_column_name, table_column_type, sizeof(table_column_name)/sizeof(table_column_name[0]));
	EXPECT_EQ(0, table_id);
	ret = fieldstat_register_table_row(instance, table_id, "Sum", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_set_metric_ratio_para(instance, output_metric_ids[5], output_metric_ids[3], output_metric_ids[4], 1);
	EXPECT_EQ(0, ret);
	ret = fieldstat_set_local_output(instance, output_file_path, "default");
	EXPECT_EQ(0, ret);
	ret = fieldstat_set_line_protocol_server(instance, "127.0.0.1", 8600);
	EXPECT_EQ(0, ret);
	ret = system("cat /dev/null > /tmp/metrics.out");
	EXPECT_EQ(0, ret);
	fieldstat_instance_start(instance);
	sleep(1);
	fieldstat_value_incrby(instance, output_metric_ids[3], 1000);
	fieldstat_value_incrby(instance, output_metric_ids[4], 10);
	sleep(2);

	fp = fopen("/tmp/fieldstat.txt", "r");
	EXPECT_NE(nullptr, fp);

	while(!feof(fp))
	{
		if(NULL == fgets(line, sizeof(line), fp))
		{
			continue;
		}
		if(n_line == 3)
		{
			EXPECT_STREQ(compared_file_output, line);
		}
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(n_line, n_loops);

	fp = fopen(output_telegraf_path, "r");
	EXPECT_NE(nullptr, fp);
	n_line = 0;

	while(!feof(fp))
	{
		if(NULL == fgets(line, sizeof(line), fp))
		{
			continue;
		}
		cjson_metric = cJSON_Parse(line);
		EXPECT_NE(nullptr, cjson_metric);
		cJSON_DeleteItemFromObject(cjson_metric, "timestamp");

		cjson_tags = cJSON_GetObjectItem(cjson_metric, "tags");
		EXPECT_NE(nullptr, cjson_tags);
		cJSON_DeleteItemFromObject(cjson_tags, "host");
		cjson_metric_str = cJSON_PrintUnformatted(cjson_metric);
		EXPECT_NE(nullptr, cjson_metric_str);
		if(cjson_metric_str)
		{
			EXPECT_STREQ(compare, cjson_metric_str);
			free(cjson_metric_str);
			cjson_metric_str = NULL;
		}
		cJSON_Delete(cjson_metric);
		n_line++;
	}
	fclose(fp);

	fieldstat_instance_free(instance);
}

TEST(FeildStatAPI, SetMetricInvisible)
{
	int ret = 0;
	struct fieldstat_instance * instance = NULL;
	int table_id = -1;
	int output_metric_ids[10] = {0};
	FILE *fp;
	int n_line = 0;
	int n_loops = 5;
	char line[2048] = {0};
	cJSON *cjson_metric = NULL;
	cJSON *cjson_tags = NULL;
	char *cjson_metric_str = NULL;

	const char *output_file_path = "/tmp/fieldstat.txt"; 
	const char *output_telegraf_path = "/tmp/metrics.out";
	const char *table_column_name[] = {"rule", "reg/v6", "stream", "IN_Bps", "cpu_us", "PROC_Bps", "IN_Tps", "PROC_Tps","hit_cnt", "hit_rate"};
	const char *compared_file_output = "Sum                   "
									"                   0                   0            0.00e+00"
									"            5.00e+02            5.00e+01            0.00e+00"
									"            0.00e+00            0.00e+00            0.00e+00\n";

	const char *compare = "{\"fields\":{\"IN_Bps\":1000,\"IN_Tps\":0,\"PROC_Tps\":0,"
						  "\"hit_cnt\":0,\"hit_rate\":0,\"reg/v6\":0,\"rule\":0,\"stream\":0},"
						  "\"name\":\"Sum\",\"tags\":{\"app_name\":\"maat\",\"table_name\":\"maat_info\"}}";


	enum field_type table_column_type[10] = {FIELD_TYPE_GAUGE, FIELD_TYPE_GAUGE, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};

	instance = fieldstat_instance_new("maat");
	EXPECT_STREQ("maat", instance->name);

	table_id = fieldstat_register_table(instance, "maat_info", table_column_name, table_column_type, sizeof(table_column_name)/sizeof(table_column_name[0]));
	EXPECT_EQ(0, table_id);
	ret = fieldstat_register_table_row(instance, table_id, "Sum", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_set_metric_ratio_para(instance, output_metric_ids[5], output_metric_ids[3], output_metric_ids[4], 1);
	EXPECT_EQ(0, ret);
	ret = fieldstat_set_metric_invisible_para(instance, output_metric_ids[4]);
	EXPECT_EQ(0, ret);
	ret = fieldstat_set_local_output(instance, output_file_path, "default");
	EXPECT_EQ(0, ret);
	ret = fieldstat_set_line_protocol_server(instance, "127.0.0.1", 8600);
	EXPECT_EQ(0, ret);
	ret = system("cat /dev/null > /tmp/metrics.out");
	EXPECT_EQ(0, ret);
	fieldstat_instance_start(instance);
	sleep(1);
	fieldstat_value_incrby(instance, output_metric_ids[3], 1000);
	fieldstat_value_incrby(instance, output_metric_ids[4], 10);
	sleep(2);

	fp = fopen("/tmp/fieldstat.txt", "r");
	EXPECT_NE(nullptr, fp);

	while(!feof(fp))
	{
		if(NULL == fgets(line, sizeof(line), fp))
		{
			continue;
		}
		if(n_line == 3)
		{
			EXPECT_STREQ(compared_file_output, line);
		}
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(n_line, n_loops);

	fp = fopen(output_telegraf_path, "r");
	EXPECT_NE(nullptr, fp);
	n_line = 0;

	while(!feof(fp))
	{
		if(NULL == fgets(line, sizeof(line), fp))
		{
			continue;
		}
		cjson_metric = cJSON_Parse(line);
		EXPECT_NE(nullptr, cjson_metric);
		cJSON_DeleteItemFromObject(cjson_metric, "timestamp");

		cjson_tags = cJSON_GetObjectItem(cjson_metric, "tags");
		EXPECT_NE(nullptr, cjson_tags);
		cJSON_DeleteItemFromObject(cjson_tags, "host");
		cjson_metric_str = cJSON_PrintUnformatted(cjson_metric);
		EXPECT_NE(nullptr, cjson_metric_str);
		if(cjson_metric_str)
		{
			EXPECT_STREQ(compare, cjson_metric_str);
			free(cjson_metric_str);
			cjson_metric_str = NULL;
		}
		cJSON_Delete(cjson_metric);
		n_line++;
	}
	fclose(fp);

	fieldstat_instance_free(instance);
}


int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}