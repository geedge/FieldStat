#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "fieldstat.h"
#include "fieldstat_internal.h"

extern struct prometheus_endpoint_instance g_prometheus_endpoint_instance;


const char *table_column_name[] = {
	"new_conn_num", "established_conn_num", "close_conn_num", "total_in_bytes", "total_out_bytes","total_in_packets",
	"total_out_packets", "default_conn_num", "default_in_bytes", "default_out_bytes","default_in_packets",
	"default_out_packets", "allow_conn_num","allow_in_bytes", "allow_out_bytes","allow_in_packets", "allow_out_packets",
	"deny_conn_num", "deny_in_bytes", "deny_out_bytes", "deny_in_packets", "deny_out_packets", "monitor_conn_num",
	"monitor_in_bytes","monitor_out_bytes", "monitor_in_packets", "monitor_out_packets", "intercept_conn_num",
	"intercept_in_bytes","intercept_out_bytes","intercept_in_packets", "intercept_out_packets", "ipv4_in_packets",
	"ipv4_in_bytes", "ipv4_out_packets", "ipv4_out_bytes", "ipv6_in_packets", "ipv6_in_bytes", "ipv6_out_packets",
	"ipv6_out_bytes", "tcp_conn_num", "tcp_in_packets","tcp_in_bytes","tcp_out_packets", "tcp_out_bytes", "udp_conn_num",
	"udp_in_packets", "udp_in_bytes", "udp_out_packets","udp_out_bytes", "alert_bytes", "block_bytes","pinning_num",
	"maybe_pinning_num", "not_pinning_num"
};

enum field_type table_column_type[] = {
	FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER,
	FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER,
	FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER,
	FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER,
	FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER,
	FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER,
	FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER,
	FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER,
	FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER,
	FIELD_TYPE_COUNTER
};

const char *file_output_path = "/tmp/fieldstat.txt";
const char *desired_file_table_name = "table name: network_activity\n";
const char *desired_file_table_info = "                              new_conn_num established_conn_nu"
									  "      close_conn_num      total_in_bytes     total_out_bytes    total_in_packets"
									  "   total_out_packets    default_conn_num    default_in_bytes   default_out_bytes"
									  "  default_in_packets default_out_packets      allow_conn_num      allow_in_bytes"
									  "     allow_out_bytes    allow_in_packets   allow_out_packets       deny_conn_num"
									  "       deny_in_bytes      deny_out_bytes     deny_in_packets    deny_out_packets"
									  "    monitor_conn_num    monitor_in_bytes   monitor_out_bytes  monitor_in_packets"
									  " monitor_out_packets  intercept_conn_num  intercept_in_bytes intercept_out_bytes"
									  " intercept_in_packet intercept_out_packe     ipv4_in_packets       ipv4_in_bytes"
									  "    ipv4_out_packets      ipv4_out_bytes     ipv6_in_packets       ipv6_in_bytes"
									  "    ipv6_out_packets      ipv6_out_bytes        tcp_conn_num      tcp_in_packets"
									  "        tcp_in_bytes     tcp_out_packets       tcp_out_bytes        udp_conn_num"
									  "      udp_in_packets        udp_in_bytes     udp_out_packets       udp_out_bytes"
									  "         alert_bytes         block_bytes         pinning_num   maybe_pinning_num"
									  "     not_pinning_num\n";
const char *desired_file_table_data = "TRAFFIC                           5.00e+02            0.00e+00"
									  "            0.00e+00            0.00e+00            0.00e+00            0.00e+00"
									  "            0.00e+00            0.00e+00            0.00e+00            0.00e+00"
									  "            0.00e+00            0.00e+00            0.00e+00            0.00e+00"
									  "            0.00e+00            0.00e+00            0.00e+00            0.00e+00"
									  "            0.00e+00            0.00e+00            0.00e+00            0.00e+00"
									  "            0.00e+00            0.00e+00            0.00e+00            0.00e+00"
									  "            0.00e+00            0.00e+00            0.00e+00            0.00e+00"
									  "            0.00e+00            0.00e+00            0.00e+00            0.00e+00"
									  "            0.00e+00            0.00e+00            0.00e+00            0.00e+00"
									  "            0.00e+00            0.00e+00            0.00e+00            0.00e+00"
									  "            0.00e+00            0.00e+00            0.00e+00            0.00e+00"
									  "            0.00e+00            0.00e+00            0.00e+00            0.00e+00"
									  "            0.00e+00            0.00e+00            0.00e+00            0.00e+00"
									  "            0.00e+00\n";

const char *lineprotocol_output_path = "/tmp/metrics.out";

const char *desired_lineprotocol_output = "{\"fields\":{\"alert_bytes\":0,\"allow_conn_num\":0,\"allow_in_bytes\":0,\"all"
										  "ow_in_packets\":0,\"allow_out_bytes\":0,\"allow_out_packets\":0,\"block_bytes\""
										  ":0,\"close_conn_num\":0,\"default_conn_num\":0,\"default_in_bytes\":0,\"default"
										  "_in_packets\":0,\"default_out_bytes\":0,\"default_out_packets\":0,\"deny_conn_n"
										  "um\":0,\"deny_in_bytes\":0,\"deny_in_packets\":0,\"deny_out_bytes\":0,\"deny_ou"
										  "t_packets\":0,\"established_conn_num\":0,\"intercept_conn_num\":0,\"intercept_i"
										  "n_bytes\":0,\"intercept_in_packets\":0,\"intercept_out_bytes\":0,\"intercept_ou"
										  "t_packets\":0,\"ipv4_in_bytes\":0,\"ipv4_in_packets\":0,\"ipv4_out_bytes\":0,\""
										  "ipv4_out_packets\":0,\"ipv6_in_bytes\":0,\"ipv6_in_packets\":0,\"ipv6_out_bytes"
										  "\":0,\"ipv6_out_packets\":0,\"maybe_pinning_num\":0,\"monitor_conn_num\":0,\"mo"
										  "nitor_in_bytes\":0,\"monitor_in_packets\":0,\"monitor_out_bytes\":0,\"monitor_o"
										  "ut_packets\":0,\"new_conn_num\":1000,\"not_pinning_num\":0,\"pinning_num\":0,\""
										  "tcp_conn_num\":0,\"tcp_in_bytes\":0,\"tcp_in_packets\":0,\"tcp_out_bytes\":0,\""
										  "tcp_out_packets\":0,\"total_in_bytes\":0,\"total_in_packets\":0,\"total_out_byt"
										  "es\":0,\"total_out_packets\":0,\"udp_conn_num\":0,\"udp_in_bytes\":0,\"udp_in_p"
										  "ackets\":0,\"udp_out_bytes\":0,\"udp_out_packets\":0},\"name\":\"TRAFFIC\",\"ta"
										  "gs\":{\"app_name\":\"tsg_statistic\",\"table_name\":\"network_activity\"}}";

const char *prometheus_output_path = "/tmp/prometheus.txt";

const char *desired_prometheus_output = "tsg_statistic_new_conn_num{app_name=\"tsg_statistic\",table_name=\"network_activit"
										"y\",line_name=\"TRAFFIC\"} 1000\n";


 
void check_file_output_content(int desired_line_cnt)
{
	int n_line = 0;
	char read_line_buf[2048] = {0};
	FILE *fp = fopen(file_output_path, "r");
	EXPECT_NE(nullptr, fp);

	while(!feof(fp))
	{
		if(NULL == fgets(read_line_buf, sizeof(read_line_buf), fp))
		{
			continue;
		}
		if(n_line == 1)
		{
			EXPECT_STREQ(desired_file_table_name, read_line_buf);
		}
		if(n_line == 2)
		{
			EXPECT_STREQ(desired_file_table_info, read_line_buf);
		}
		if(n_line == 3)
		{
			EXPECT_STREQ(desired_file_table_data, read_line_buf);
		}
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(n_line, desired_line_cnt);
	system("cat /dev/null > /tmp/fieldstat.txt");
}


void check_lineprotocol_output_content(int desired_line_cnt)
{
	int n_line = 0;
	char read_line_buf[2048] = {0};

	cJSON *cjson_metric = NULL;
	cJSON *cjson_tags = NULL;
	char *cjson_metric_str = NULL;

	FILE *fp = fopen(lineprotocol_output_path, "r");
	EXPECT_NE(nullptr, fp);

	while(!feof(fp))
	{
		if(NULL == fgets(read_line_buf, sizeof(read_line_buf), fp))
		{
			continue;
		}
		cjson_metric = cJSON_Parse(read_line_buf);
		EXPECT_NE(nullptr, cjson_metric);
		cJSON_DeleteItemFromObject(cjson_metric, "timestamp");

		cjson_tags = cJSON_GetObjectItem(cjson_metric, "tags");
		EXPECT_NE(nullptr, cjson_tags);
		cJSON_DeleteItemFromObject(cjson_tags, "host");
		cjson_metric_str = cJSON_PrintUnformatted(cjson_metric);
		EXPECT_NE(nullptr, cjson_metric_str);
		if(cjson_metric_str)
		{
			EXPECT_STREQ(desired_lineprotocol_output, cjson_metric_str);
			free(cjson_metric_str);
			cjson_metric_str = NULL;
		}
		cJSON_Delete(cjson_metric);
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(n_line, desired_line_cnt);
	system("cat /dev/null > /tmp/metrics.out");
}

void check_prometheus_output_content(int desired_line_cnt, unsigned short listen_port)
{
	int n_line = 0;
	char read_line_buf[2048] = {0};
	char req_str[256] = {0};

	system("cat /dev/null > /tmp/prometheus.txt");
	snprintf(req_str, sizeof(req_str), 
			"curl -s  http://127.0.0.1:%u/metrics -o /tmp/prometheus.txt",
			listen_port);

	system(req_str);

	FILE *fp = fopen(prometheus_output_path, "r");
	EXPECT_NE(nullptr, fp);
	while(!feof(fp))
	{
		if(NULL == fgets(read_line_buf, sizeof(read_line_buf), fp))
		{
			continue;
		}
		if(n_line == 0)
		{
			EXPECT_STREQ(desired_prometheus_output, read_line_buf);
		}
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(n_line, desired_line_cnt);
	system("cat /dev/null > /tmp/prometheus.txt");
}

TEST(FeildStatOutput, OutputFile)
{
	int ret = 0, table_id = -1;
	struct fieldstat_instance * instance = NULL;
	int output_metric_ids[55] = {0};

	instance = fieldstat_instance_new("tsg_statistic");
	EXPECT_STREQ("tsg_statistic", instance->name);

	table_id = fieldstat_register_table(instance, "network_activity",
										table_column_name, table_column_type, 
										sizeof(table_column_name)/sizeof(table_column_name[0]));
	EXPECT_EQ(0, table_id);

	ret = fieldstat_register_table_row(instance, table_id, "TRAFFIC", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);

	ret = fieldstat_set_local_output(instance, file_output_path, "default");
	EXPECT_EQ(0, ret);

	system("cat /dev/null > /tmp/fieldstat.txt");

	fieldstat_instance_start(instance);
	sleep(1);
	fieldstat_value_incrby(instance, output_metric_ids[0], 1000);
	sleep(2);

	check_file_output_content(5);

	fieldstat_instance_free(instance);
}


TEST(FeildStatOutput, OutputLineProtocol)
{
	int ret = 0, table_id = -1;
	int output_metric_ids[55] = {0};

	struct fieldstat_instance * instance = NULL;
	instance = fieldstat_instance_new("tsg_statistic");
	EXPECT_STREQ("tsg_statistic", instance->name);

	table_id = fieldstat_register_table(instance, "network_activity", 
										table_column_name, table_column_type,
										sizeof(table_column_name)/sizeof(table_column_name[0]));
	EXPECT_EQ(0, table_id);
	ret = fieldstat_register_table_row(instance, table_id, "TRAFFIC", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);

	ret = fieldstat_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(4, instance->output_type);
	fieldstat_instance_start(instance);
	ret = system("cat /dev/null > /tmp/metrics.out");
	sleep(1);
	fieldstat_value_incrby(instance, output_metric_ids[0], 1000);
	sleep(2);

	check_lineprotocol_output_content(1);

	fieldstat_instance_free(instance);
}

TEST(FeildStatOutput, OutputPrometheus)
{
	int ret = 0, table_id = -1;
	int output_metric_ids[55] = {0};
	struct fieldstat_instance * instance = NULL;

	instance = fieldstat_instance_new("tsg_statistic");
	EXPECT_STREQ("tsg_statistic", instance->name);

	table_id = fieldstat_register_table(instance, "network_activity", 
										table_column_name, table_column_type,
										sizeof(table_column_name)/sizeof(table_column_name[0]));
	EXPECT_EQ(0, table_id);
	ret = fieldstat_register_table_row(instance, table_id, "TRAFFIC", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_global_enable_prometheus_endpoint(9020, NULL);
	EXPECT_EQ(0, ret);
	ret = fieldstat_enable_prometheus_output(instance);
	EXPECT_EQ(0, ret);
	fieldstat_instance_start(instance);
	fieldstat_value_incrby(instance, output_metric_ids[0], 1000);
	EXPECT_EQ(0, ret);
	sleep(1);
	check_prometheus_output_content(55, 9020);
	fieldstat_global_disable_prometheus_endpoint();
	fieldstat_instance_free(instance);
}

TEST(FeildStatOutput, OutputFileAndLineProtocol)
{
	int ret = 0, table_id = -1;
	struct fieldstat_instance * instance = NULL;
	int output_metric_ids[55] = {0};

	instance = fieldstat_instance_new("tsg_statistic");
	EXPECT_STREQ("tsg_statistic", instance->name);

	table_id = fieldstat_register_table(instance, "network_activity",
										table_column_name, table_column_type,
										sizeof(table_column_name)/sizeof(table_column_name[0]));
	EXPECT_EQ(0, table_id);
	ret = fieldstat_register_table_row(instance, table_id, "TRAFFIC", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);

	ret = fieldstat_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);
	ret = fieldstat_set_local_output(instance, file_output_path, "default");
	EXPECT_EQ(0, ret);

	EXPECT_EQ(12, instance->output_type);
	ret = system("cat /dev/null > /tmp/metrics.out");
	ret = system("cat /dev/null > /tmp/fieldstat.txt");
	fieldstat_instance_start(instance);
	sleep(1);
	fieldstat_value_incrby(instance, output_metric_ids[0], 1000);
	sleep(2);
	check_file_output_content(5);
	check_lineprotocol_output_content(1);

	fieldstat_instance_free(instance);

}


TEST(FeildStatOutput, OutputFileAndPrometheus)
{
	int ret = 0, table_id = -1;
	struct fieldstat_instance * instance = NULL;
	int output_metric_ids[55] = {0};

	instance = fieldstat_instance_new("tsg_statistic");
	EXPECT_STREQ("tsg_statistic", instance->name);

	table_id = fieldstat_register_table(instance, "network_activity", 
										table_column_name, table_column_type,
										sizeof(table_column_name)/sizeof(table_column_name[0]));
	EXPECT_EQ(0, table_id);
	ret = fieldstat_register_table_row(instance, table_id, "TRAFFIC", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_set_local_output(instance, file_output_path, "default");
	EXPECT_EQ(0, ret);
	ret = fieldstat_global_enable_prometheus_endpoint(9021, NULL);
	EXPECT_EQ(0, ret);
	ret = fieldstat_enable_prometheus_output(instance);
	EXPECT_EQ(0, ret);

	EXPECT_EQ(9, instance->output_type);
	ret = system("cat /dev/null > /tmp/prometheus.txt");
	ret = system("cat /dev/null > /tmp/fieldstat.txt");
	fieldstat_instance_start(instance);
	sleep(1);
	fieldstat_value_incrby(instance, output_metric_ids[0], 1000);
	sleep(2);
	check_file_output_content(5);
	check_prometheus_output_content(55, 9021);
	fieldstat_global_disable_prometheus_endpoint();
	fieldstat_instance_free(instance);

}

TEST(FeildStatOutput, OutputFileAndPrometheusAndLineProtocol)
{
	int ret = 0, table_id = -1;
	struct fieldstat_instance * instance = NULL;
	int output_metric_ids[55] = {0};

	instance = fieldstat_instance_new("tsg_statistic");
	EXPECT_STREQ("tsg_statistic", instance->name);

	table_id = fieldstat_register_table(instance, "network_activity",
										table_column_name, table_column_type,
										sizeof(table_column_name)/sizeof(table_column_name[0]));
	EXPECT_EQ(0, table_id);
	ret = fieldstat_register_table_row(instance, table_id, "TRAFFIC", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_set_local_output(instance, file_output_path, "default");
	EXPECT_EQ(0, ret);
	ret = fieldstat_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);
	ret = fieldstat_global_enable_prometheus_endpoint(9022, NULL);
	EXPECT_EQ(0, ret);
	ret = fieldstat_enable_prometheus_output(instance);
	EXPECT_EQ(0, ret);

	EXPECT_EQ(13, instance->output_type);
	system("cat /dev/null > /tmp/prometheus.txt");
	system("cat /dev/null > /tmp/fieldstat.txt");
	system("cat /dev/null > /tmp/lineprotocol.txt");
	fieldstat_instance_start(instance);
	sleep(1);
	fieldstat_value_incrby(instance, output_metric_ids[0], 1000);
	sleep(2);
	check_file_output_content(5);
	check_lineprotocol_output_content(1);
	check_prometheus_output_content(55, 9022);

	fieldstat_global_disable_prometheus_endpoint();
	fieldstat_instance_free(instance);

}

TEST(FeildStatOutput, SummaryOutputFile)
{
	int metric_id = -1;
	const char *quantiles = "0.1,0.5,0.8,0.9,0.95,0.99";
	const char *output_file = "/tmp/fieldstat.txt";
	const char *compared_data_line = "hit_rate  	        10        50"
									 "        80        90        95"
									 "        99       100         1"
									 "     50.50     28.87       100\n";
	FILE *fp;
	char read_line[2048] = {0};
	int n_line = 0;

	struct fieldstat_instance *instance = fieldstat_instance_new("maat");
	fieldstat_set_local_output(instance, "/tmp/fieldstat.txt", "default");

	metric_id = fieldstat_register_summary(instance, "hit_rate", NULL, 0, 
										   quantiles, 1, 100, 2, 0);
	EXPECT_EQ(0, metric_id);
	fieldstat_instance_start(instance);
	sleep(1);
	for(int i = 0; i < 100; i ++)
	{
		fieldstat_value_set(instance, metric_id, i + 1);
	}
	sleep(2);

	fp = fopen(output_file, "r");
	EXPECT_NE(nullptr, fp);
	while(!feof(fp))
	{
		if(NULL == fgets(read_line, sizeof(read_line), fp))
		{
			continue;
		}
		if(n_line == 2)
		{
			EXPECT_STREQ(compared_data_line, read_line);
		}
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(4, n_line);
	fieldstat_instance_free(instance);
}

TEST(FeildStatOutput, SummaryOutputLineProtocol)
{
	int metric_id = -1;
	const char * quantiles = "0.1,0.5,0.8,0.9,0.95,0.99";

	const char *telegraf_file = "/tmp/metrics.out";
	FILE *fp;
	char read_line[2048] = {0};
	int n_line = 0;
	cJSON *cjson_metric = NULL;
	cJSON *cjson_tags = NULL;
	char *cjson_metric_str = NULL;
	const char *compared_data = "{\"fields\":{\"P10\":10,\"P50\":50,"
								"\"P80\":80,\"P90\":90,\"P95\":95,"
								"\"P99\":99,\"avg\":50.500000,\"cnt\":100,"
								"\"max\":100,\"min\":1,\"stddev\":28.870000},"
								"\"name\":\"hit_rate\",\"tags\":"
								"{\"app_name\":\"maat\"}}";

	struct fieldstat_instance * instance = NULL;

	instance = fieldstat_instance_new("maat");
	fieldstat_set_line_protocol_server(instance, "127.0.0.1", 8700);

	metric_id = fieldstat_register_summary(instance, "hit_rate", NULL, 0, 
										   quantiles, 1, 100, 2, 0);
	EXPECT_EQ(0, metric_id);
	fieldstat_instance_start(instance);
	system("cat /dev/null > /tmp/metrics.out");
	sleep(1);
	for(int i = 0; i < 100; i ++)
	{
		fieldstat_value_set(instance, metric_id, i + 1);
	}
	sleep(2);

	fp = fopen(telegraf_file, "r");
	EXPECT_NE(nullptr, fp);
	while(!feof(fp))
	{
		if(NULL == fgets(read_line, sizeof(read_line), fp))
		{
			continue;
		}
		cjson_metric = cJSON_Parse(read_line);
		EXPECT_NE(nullptr, cjson_metric);
		cJSON_DeleteItemFromObject(cjson_metric, "timestamp");

		cjson_tags = cJSON_GetObjectItem(cjson_metric, "tags");
		EXPECT_NE(nullptr, cjson_tags);
		cJSON_DeleteItemFromObject(cjson_tags, "host");
		cjson_metric_str = cJSON_PrintUnformatted(cjson_metric);
		EXPECT_NE(nullptr, cjson_metric_str);
		if(cjson_metric_str)
		{
			EXPECT_STREQ(compared_data, cjson_metric_str);
			free(cjson_metric_str);
			cjson_metric_str = NULL;
		}
		cJSON_Delete(cjson_metric);
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(1, n_line);

	fieldstat_instance_free(instance);
}

TEST(FeildStatOutput, SummaryOutputPrometheus)
{
	int metric_id = -1;
	const char * quantiles = "0.1,0.5,0.8,0.9,0.95,0.99";

	
	const char *prometheus_file = "/tmp/prometheus.txt";
	FILE *fp;
	char read_line[2048] = {0};
	int n_line = 0;
	const char *compared_p10 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.10\"} 10\n";
	const char *compared_p50 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.50\"} 50\n";
	const char *compared_p80 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.80\"} 80\n";
	const char *compared_p90 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.90\"} 90\n";
	const char *compared_p95 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.95\"} 95\n";
	const char *compared_p99 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.99\"} 99\n";
	const char *compared_cnt = "maat_hit_rate_count{app_name=\"maat\"} 100\n";
	const char *compared_sum = "maat_hit_rate_sum{app_name=\"maat\"} 5050\n";

	struct fieldstat_instance * instance = NULL;

	instance = fieldstat_instance_new("maat");
	fieldstat_global_enable_prometheus_endpoint(9023, NULL);
	fieldstat_enable_prometheus_output(instance);
	metric_id = fieldstat_register_summary(instance, "hit_rate", NULL, 0, 
										   quantiles, 1, 100, 2, 0);
	EXPECT_EQ(0, metric_id);
	fieldstat_instance_start(instance);
	system("cat /dev/null > /tmp/prometheus.txt");
	for(int i = 0; i < 100; i ++)
	{
		fieldstat_value_set(instance, metric_id, i + 1);
	}
	system("curl -s  http://127.0.0.1:9023/metrics -o /tmp/prometheus.txt");


	fp = fopen(prometheus_file, "r");
	EXPECT_NE(nullptr, fp);
	while(!feof(fp))
	{
		if(NULL == fgets(read_line, sizeof(read_line), fp))
		{
			continue;
		}
		switch(n_line)
		{
			case 0:
				EXPECT_STREQ(compared_p10, read_line);
				break;
			case 1:
				EXPECT_STREQ(compared_p50, read_line);
				break;
			case 2:
				EXPECT_STREQ(compared_p80, read_line);
				break;
			case 3:
				EXPECT_STREQ(compared_p90, read_line);
				break;
			case 4:
				EXPECT_STREQ(compared_p95, read_line);
				break;
			case 5:
				EXPECT_STREQ(compared_p99, read_line);
				break;
			case 6:
				EXPECT_STREQ(compared_cnt, read_line);
				break;
			case 7:
				EXPECT_STREQ(compared_sum, read_line);
				break;
			default:
				break;
		}
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(8, n_line);
	fieldstat_global_disable_prometheus_endpoint();
	fieldstat_instance_free(instance);
}

TEST(FeildStatOutput, HistogramOutputFile)
{
	int metric_id = -1;
	const char *upper_inclusive_bounds = "10,50,80,90,95,99";
	const char *output_file = "/tmp/fieldstat.txt";
	const char *compared_data_line = "hit_rate  	        10        50"
									 "        80        90        95"
									 "        99       100         1"
									 "     50.50     28.87       100\n";
	FILE *fp;
	char read_line[2048] = {0};
	int n_line = 0;

	struct fieldstat_instance *instance = fieldstat_instance_new("maat");
	fieldstat_set_local_output(instance, "/tmp/fieldstat.txt", "default");

	metric_id = fieldstat_register_histogram(instance, "hit_rate", NULL, 0, 
										     upper_inclusive_bounds, 1, 100, 2, 0);
	EXPECT_EQ(0, metric_id);
	fieldstat_instance_start(instance);
	sleep(1);
	for(int i = 0; i < 100; i ++)
	{
		fieldstat_value_set(instance, metric_id, i + 1);
	}
	sleep(2);

	fp = fopen(output_file, "r");
	EXPECT_NE(nullptr, fp);
	while(!feof(fp))
	{
		if(NULL == fgets(read_line, sizeof(read_line), fp))
		{
			continue;
		}
		if(n_line == 2)
		{
			EXPECT_STREQ(compared_data_line, read_line);
		}
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(4, n_line);
	fieldstat_instance_free(instance);
}

TEST(FeildStatOutput, HistogramOutputLineProtocol)
{
	int metric_id = -1;
	const char *upper_inclusive_bounds = "10,50,80,90,95,99";

	const char *telegraf_file = "/tmp/metrics.out";
	FILE *fp;
	char read_line[2048] = {0};
	int n_line = 0;
	cJSON *cjson_metric = NULL;
	cJSON *cjson_tags = NULL;
	char *cjson_metric_str = NULL;
	const char *compared_data = "{\"fields\":{\"avg\":50.500000,\"cnt\":100,"
								"\"le10\":10,\"le50\":50,\"le80\":80,"
								"\"le90\":90,\"le95\":95,\"le99\":99,"
								"\"max\":100,\"min\":1,\"stddev\":28.870000},"
								"\"name\":\"hit_rate\",\"tags\":"
								"{\"app_name\":\"maat\"}}";

	struct fieldstat_instance * instance = NULL;

	instance = fieldstat_instance_new("maat");
	fieldstat_set_line_protocol_server(instance, "127.0.0.1", 8700);

	metric_id = fieldstat_register_histogram(instance, "hit_rate", NULL, 0, 
										     upper_inclusive_bounds, 1, 100, 2, 0);
	EXPECT_EQ(0, metric_id);
	fieldstat_instance_start(instance);
	system("cat /dev/null > /tmp/metrics.out");
	sleep(1);
	for(int i = 0; i < 100; i ++)
	{
		fieldstat_value_set(instance, metric_id, i + 1);
	}
	sleep(2);

	fp = fopen(telegraf_file, "r");
	EXPECT_NE(nullptr, fp);
	while(!feof(fp))
	{
		if(NULL == fgets(read_line, sizeof(read_line), fp))
		{
			continue;
		}
		cjson_metric = cJSON_Parse(read_line);
		EXPECT_NE(nullptr, cjson_metric);
		cJSON_DeleteItemFromObject(cjson_metric, "timestamp");

		cjson_tags = cJSON_GetObjectItem(cjson_metric, "tags");
		EXPECT_NE(nullptr, cjson_tags);
		cJSON_DeleteItemFromObject(cjson_tags, "host");
		cjson_metric_str = cJSON_PrintUnformatted(cjson_metric);
		EXPECT_NE(nullptr, cjson_metric_str);
		if(cjson_metric_str)
		{
			EXPECT_STREQ(compared_data, cjson_metric_str);
			free(cjson_metric_str);
			cjson_metric_str = NULL;
		}
		cJSON_Delete(cjson_metric);
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(1, n_line);

	fieldstat_instance_free(instance);
}


TEST(FeildStatOutput, HistogramOutputPrometheus)
{
	int metric_id = -1;
	const char *upper_inclusive_bounds = "10,50,80,90,95,99";

	const char *prometheus_file = "/tmp/prometheus.txt";
	FILE *fp;
	char read_line[2048] = {0};
	int n_line = 0;
	const char *compared_le10 = "maat_hit_rate_bucket{app_name=\"maat\",le=\"10.00\"} 10\n";
	const char *compared_le50 = "maat_hit_rate_bucket{app_name=\"maat\",le=\"50.00\"} 50\n";
	const char *compared_le80 = "maat_hit_rate_bucket{app_name=\"maat\",le=\"80.00\"} 80\n";
	const char *compared_le90 = "maat_hit_rate_bucket{app_name=\"maat\",le=\"90.00\"} 90\n";
	const char *compared_le95 = "maat_hit_rate_bucket{app_name=\"maat\",le=\"95.00\"} 95\n";
	const char *compared_le99 = "maat_hit_rate_bucket{app_name=\"maat\",le=\"99.00\"} 99\n";
	const char *compared_cnt = "maat_hit_rate_count{app_name=\"maat\"} 100\n";
	const char *compared_sum = "maat_hit_rate_sum{app_name=\"maat\"} 5050\n";

	struct fieldstat_instance * instance = NULL;

	instance = fieldstat_instance_new("maat");
	fieldstat_global_enable_prometheus_endpoint(9024, NULL);
	fieldstat_enable_prometheus_output(instance);
	metric_id = fieldstat_register_histogram(instance, "hit_rate", NULL, 0, 
										     upper_inclusive_bounds, 1, 100, 2, 0);
	EXPECT_EQ(0, metric_id);
	fieldstat_instance_start(instance);
	sleep(1);
	system("cat /dev/null > /tmp/prometheus.txt");
	for(int i = 0; i < 100; i ++)
	{
		fieldstat_value_set(instance, metric_id, i + 1);
	}
	system("curl -s  http://127.0.0.1:9024/metrics -o /tmp/prometheus.txt");


	fp = fopen(prometheus_file, "r");
	EXPECT_NE(nullptr, fp);
	while(!feof(fp))
	{
		if(NULL == fgets(read_line, sizeof(read_line), fp))
		{
			continue;
		}
		switch(n_line)
		{
			case 0:
				EXPECT_STREQ(compared_le10, read_line);
				break;
			case 1:
				EXPECT_STREQ(compared_le50, read_line);
				break;
			case 2:
				EXPECT_STREQ(compared_le80, read_line);
				break;
			case 3:
				EXPECT_STREQ(compared_le90, read_line);
				break;
			case 4:
				EXPECT_STREQ(compared_le95, read_line);
				break;
			case 5:
				EXPECT_STREQ(compared_le99, read_line);
				break;
			case 6:
				EXPECT_STREQ(compared_cnt, read_line);
				break;
			case 7:
				EXPECT_STREQ(compared_sum, read_line);
				break;
			default:
				break;
		}
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(8, n_line);
	fieldstat_global_disable_prometheus_endpoint();
	fieldstat_instance_free(instance);
}


TEST(FeildStatOutput, SummaryOutputFileCurrent)
{
	int metric_id = -1;
	const char *quantiles = "0.1,0.5,0.8,0.9,0.95,0.99";
	const char *output_file = "/tmp/fieldstat.txt";
	const char *compared_data_line = "hit_rate  	        20       100"
									 "       160       180       190"
									 "       198       200         1"
									 "    100.50     57.73       200\n";
	FILE *fp;
	char read_line[2048] = {0};
	int n_line = 0;

	struct fieldstat_instance *instance = fieldstat_instance_new("maat");
	fieldstat_set_local_output(instance, "/tmp/fieldstat.txt", "default");

	metric_id = fieldstat_register_summary(instance, "hit_rate", NULL, 0, 
										   quantiles, 1, 200, 2, 0);
	EXPECT_EQ(0, metric_id);
	fieldstat_instance_start(instance);
	sleep(1);
	for(int i = 0; i < 100; i ++)
	{
		fieldstat_value_set(instance, metric_id, i + 1);
	}
	sleep(2);
	for(int i = 0; i < 100; i++)
	{
		fieldstat_value_set(instance, metric_id, i + 101);
	}
	sleep(2);
	fp = fopen(output_file, "r");
	EXPECT_NE(nullptr, fp);
	while(!feof(fp))
	{
		if(NULL == fgets(read_line, sizeof(read_line), fp))
		{
			continue;
		}
		if(n_line == 2)
		{
			EXPECT_STREQ(compared_data_line, read_line);
		}
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(4, n_line);
	fieldstat_instance_free(instance);
}

TEST(FeildStatOutput, SummaryOutputFileChange)
{
	int metric_id = -1;
	const char *quantiles = "0.1,0.5,0.8,0.9,0.95,0.99";
	const char *output_file = "/tmp/fieldstat.txt";
	const char *compared_data_line = "hit_rate  	       110       150"
									 "       180       190       195"
									 "       199       200       101"
									 "    150.50     28.87       100\n";
	FILE *fp;
	char read_line[2048] = {0};
	int n_line = 0;

	struct fieldstat_instance *instance = fieldstat_instance_new("maat");
	fieldstat_set_local_output(instance, "/tmp/fieldstat.txt", "default");

	metric_id = fieldstat_register_summary(instance, "hit_rate", NULL, 0, 
										   quantiles, 1, 200, 2, 1);
	EXPECT_EQ(0, metric_id);
	fieldstat_instance_start(instance);
	sleep(1);
	for(int i = 0; i < 100; i ++)
	{
		fieldstat_value_set(instance, metric_id, i + 1);
	}
	sleep(2);
	for(int i = 0; i < 100; i++)
	{
		fieldstat_value_set(instance, metric_id, i + 101);
	}
	sleep(2);
	fp = fopen(output_file, "r");
	EXPECT_NE(nullptr, fp);
	while(!feof(fp))
	{
		if(NULL == fgets(read_line, sizeof(read_line), fp))
		{
			continue;
		}
		if(n_line == 2)
		{
			EXPECT_STREQ(compared_data_line, read_line);
		}
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(4, n_line);
	fieldstat_instance_free(instance);
}

TEST(FeildStatOutput, SummaryOutputLineProtocolCurrent)
{
	int metric_id = -1;
	const char * quantiles = "0.1,0.5,0.8,0.9,0.95,0.99";

	const char *telegraf_file = "/tmp/metrics.out";
	FILE *fp;
	char read_line[2048] = {0};
	int n_line = 0;
	cJSON *cjson_metric = NULL;
	cJSON *cjson_tags = NULL;
	char *cjson_metric_str = NULL;

	const char *compared_line0 = "{\"fields\":{\"P10\":10,\"P50\":50,"
								 "\"P80\":80,\"P90\":90,\"P95\":95,"
								 "\"P99\":99,\"avg\":50.500000,"
								 "\"cnt\":100,\"max\":100,\"min\":1,"
								 "\"stddev\":28.870000},\"name\":"
								 "\"hit_rate\",\"tags\":{\"app_name\""
								 ":\"maat\"}}";

	const char *compared_line1 = "{\"fields\":{\"P10\":20,\"P50\":100,"
								 "\"P80\":160,\"P90\":180,\"P95\":190,"
								 "\"P99\":198,\"avg\":100.500000,"
								 "\"cnt\":200,\"max\":200,\"min\":1,"
								 "\"stddev\":57.730000},\"name\":"
								 "\"hit_rate\",\"tags\":{\"app_name\""
								 ":\"maat\"}}";



	struct fieldstat_instance * instance = NULL;

	instance = fieldstat_instance_new("maat");
	fieldstat_set_line_protocol_server(instance, "127.0.0.1", 8700);

	metric_id = fieldstat_register_summary(instance, "hit_rate", NULL, 0, 
										   quantiles, 1, 100, 2, 0);
	EXPECT_EQ(0, metric_id);
	fieldstat_instance_start(instance);
	system("cat /dev/null > /tmp/metrics.out");
	sleep(1);
	for(int i = 0; i < 100; i ++)
	{
		fieldstat_value_set(instance, metric_id, i + 1);
	}
	sleep(2);
	
	for(int i = 0; i < 100; i ++)
	{
		fieldstat_value_set(instance, metric_id, i + 101);
	}
	sleep(2);

	fp = fopen(telegraf_file, "r");
	EXPECT_NE(nullptr, fp);
	while(!feof(fp))
	{
		if(NULL == fgets(read_line, sizeof(read_line), fp))
		{
			continue;
		}
		cjson_metric = cJSON_Parse(read_line);
		EXPECT_NE(nullptr, cjson_metric);
		cJSON_DeleteItemFromObject(cjson_metric, "timestamp");

		cjson_tags = cJSON_GetObjectItem(cjson_metric, "tags");
		EXPECT_NE(nullptr, cjson_tags);
		cJSON_DeleteItemFromObject(cjson_tags, "host");
		cjson_metric_str = cJSON_PrintUnformatted(cjson_metric);
		EXPECT_NE(nullptr, cjson_metric_str);
		if(cjson_metric_str)
		{
			if(n_line == 0)
			{
				EXPECT_STREQ(compared_line0, cjson_metric_str);
			}
			if(n_line == 1)
			{
				EXPECT_STREQ(compared_line1, cjson_metric_str);	
			}
			free(cjson_metric_str);
			cjson_metric_str = NULL;
		}
		cJSON_Delete(cjson_metric);
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(2, n_line);

	fieldstat_instance_free(instance);
}

TEST(FeildStatOutput, SummaryOutputLineProtocolChange)
{
	int metric_id = -1;
	const char * quantiles = "0.1,0.5,0.8,0.9,0.95,0.99";

	const char *telegraf_file = "/tmp/metrics.out";
	FILE *fp;
	char read_line[2048] = {0};
	int n_line = 0;
	cJSON *cjson_metric = NULL;
	cJSON *cjson_tags = NULL;
	char *cjson_metric_str = NULL;

	const char *compared_line0 = "{\"fields\":{\"P10\":10,\"P50\":50,"
								 "\"P80\":80,\"P90\":90,\"P95\":95,"
								 "\"P99\":99,\"avg\":50.500000,"
								 "\"cnt\":100,\"max\":100,\"min\":1,"
								 "\"stddev\":28.870000},\"name\":"
								 "\"hit_rate\",\"tags\":{\"app_name\""
								 ":\"maat\"}}";

	const char *compared_line1 = "{\"fields\":{\"P10\":110,\"P50\":150,"
								 "\"P80\":180,\"P90\":190,\"P95\":195,"
								 "\"P99\":199,\"avg\":150.500000,"
								 "\"cnt\":100,\"max\":200,\"min\":101,"
								 "\"stddev\":28.870000},\"name\":"
								 "\"hit_rate\",\"tags\":{\"app_name\""
								 ":\"maat\"}}";



	struct fieldstat_instance * instance = NULL;

	instance = fieldstat_instance_new("maat");
	fieldstat_set_line_protocol_server(instance, "127.0.0.1", 8700);

	metric_id = fieldstat_register_summary(instance, "hit_rate", NULL, 0, 
										   quantiles, 1, 100, 2, 1);
	EXPECT_EQ(0, metric_id);
	fieldstat_instance_start(instance);
	system("cat /dev/null > /tmp/metrics.out");
	sleep(1);
	for(int i = 0; i < 100; i ++)
	{
		fieldstat_value_set(instance, metric_id, i + 1);
	}
	sleep(2);
	
	for(int i = 0; i < 100; i ++)
	{
		fieldstat_value_set(instance, metric_id, i + 101);
	}
	sleep(2);

	fp = fopen(telegraf_file, "r");
	EXPECT_NE(nullptr, fp);
	while(!feof(fp))
	{
		if(NULL == fgets(read_line, sizeof(read_line), fp))
		{
			continue;
		}
		cjson_metric = cJSON_Parse(read_line);
		EXPECT_NE(nullptr, cjson_metric);
		cJSON_DeleteItemFromObject(cjson_metric, "timestamp");

		cjson_tags = cJSON_GetObjectItem(cjson_metric, "tags");
		EXPECT_NE(nullptr, cjson_tags);
		cJSON_DeleteItemFromObject(cjson_tags, "host");
		cjson_metric_str = cJSON_PrintUnformatted(cjson_metric);
		EXPECT_NE(nullptr, cjson_metric_str);
		if(cjson_metric_str)
		{
			if(n_line == 0)
			{
				EXPECT_STREQ(compared_line0, cjson_metric_str);
			}
			if(n_line == 1)
			{
				EXPECT_STREQ(compared_line1, cjson_metric_str);	
			}
			free(cjson_metric_str);
			cjson_metric_str = NULL;
		}
		cJSON_Delete(cjson_metric);
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(2, n_line);

	fieldstat_instance_free(instance);
}

TEST(FeildStatOutput, SummaryOutputPrometheusCurrent)
{
	int metric_id = -1;
	const char * quantiles = "0.1,0.5,0.8,0.9,0.95,0.99";

	const char *prometheus_file = "/tmp/prometheus.txt";
	FILE *fp;
	char read_line[2048] = {0};
	int n_line = 0;
	const char *compared_p10_0 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.10\"} 10\n";
	const char *compared_p50_0 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.50\"} 50\n";
	const char *compared_p80_0 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.80\"} 80\n";
	const char *compared_p90_0 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.90\"} 90\n";
	const char *compared_p95_0 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.95\"} 95\n";
	const char *compared_p99_0 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.99\"} 99\n";
	const char *compared_cnt_0 = "maat_hit_rate_count{app_name=\"maat\"} 100\n";
	const char *compared_sum_0 = "maat_hit_rate_sum{app_name=\"maat\"} 5050\n";
	const char *compared_p10_1 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.10\"} 20\n";
	const char *compared_p50_1 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.50\"} 100\n";
	const char *compared_p80_1 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.80\"} 160\n";
	const char *compared_p90_1 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.90\"} 180\n";
	const char *compared_p95_1 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.95\"} 190\n";
	const char *compared_p99_1 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.99\"} 198\n";
	const char *compared_cnt_1 = "maat_hit_rate_count{app_name=\"maat\"} 200\n";
	const char *compared_sum_1 = "maat_hit_rate_sum{app_name=\"maat\"} 20100\n";

	struct fieldstat_instance * instance = NULL;

	instance = fieldstat_instance_new("maat");
	fieldstat_global_enable_prometheus_endpoint(9026, NULL);
	fieldstat_enable_prometheus_output(instance);
	metric_id = fieldstat_register_summary(instance, "hit_rate", NULL, 0, 
										   quantiles, 1, 100, 2, 0);
	EXPECT_EQ(0, metric_id);
	fieldstat_instance_start(instance);
	system("cat /dev/null > /tmp/prometheus.txt");
	for(int i = 0; i < 100; i ++)
	{
		fieldstat_value_set(instance, metric_id, i + 1);
	}
	system("curl -s  http://127.0.0.1:9026/metrics -o /tmp/prometheus.txt");

	fp = fopen(prometheus_file, "r");
	EXPECT_NE(nullptr, fp);
	while(!feof(fp))
	{
		if(NULL == fgets(read_line, sizeof(read_line), fp))
		{
			continue;
		}
		switch(n_line)
		{
			case 0:
				EXPECT_STREQ(compared_p10_0, read_line);
				break;
			case 1:
				EXPECT_STREQ(compared_p50_0, read_line);
				break;
			case 2:
				EXPECT_STREQ(compared_p80_0, read_line);
				break;
			case 3:
				EXPECT_STREQ(compared_p90_0, read_line);
				break;
			case 4:
				EXPECT_STREQ(compared_p95_0, read_line);
				break;
			case 5:
				EXPECT_STREQ(compared_p99_0, read_line);
				break;
			case 6:
				EXPECT_STREQ(compared_cnt_0, read_line);
				break;
			case 7:
				EXPECT_STREQ(compared_sum_0, read_line);
				break;
			default:
				break;
		}
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(8, n_line);

	for(int i = 0; i < 100; i ++)
	{
		fieldstat_value_set(instance, metric_id, i + 101);
	}
	sleep(1);
	n_line = 0;
	system("curl -s  http://127.0.0.1:9026/metrics -o /tmp/prometheus.txt");
	fp = fopen(prometheus_file, "r");
	EXPECT_NE(nullptr, fp);
	while(!feof(fp))
	{
		if(NULL == fgets(read_line, sizeof(read_line), fp))
		{
			continue;
		}
		switch(n_line)
		{
			case 0:
				EXPECT_STREQ(compared_p10_1, read_line);
				break;
			case 1:
				EXPECT_STREQ(compared_p50_1, read_line);
				break;
			case 2:
				EXPECT_STREQ(compared_p80_1, read_line);
				break;
			case 3:
				EXPECT_STREQ(compared_p90_1, read_line);
				break;
			case 4:
				EXPECT_STREQ(compared_p95_1, read_line);
				break;
			case 5:
				EXPECT_STREQ(compared_p99_1, read_line);
				break;
			case 6:
				EXPECT_STREQ(compared_cnt_1, read_line);
				break;
			case 7:
				EXPECT_STREQ(compared_sum_1, read_line);
				break;
			default:
				break;
		}
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(8, n_line);

	fieldstat_global_disable_prometheus_endpoint();
	fieldstat_instance_free(instance);
}

TEST(FeildStatOutput, SummaryOutputPrometheusChange)
{
	int metric_id = -1;
	const char * quantiles = "0.1,0.5,0.8,0.9,0.95,0.99";

	const char *prometheus_file = "/tmp/prometheus.txt";
	FILE *fp;
	char read_line[2048] = {0};
	int n_line = 0;
	const char *compared_p10_0 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.10\"} 10\n";
	const char *compared_p50_0 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.50\"} 50\n";
	const char *compared_p80_0 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.80\"} 80\n";
	const char *compared_p90_0 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.90\"} 90\n";
	const char *compared_p95_0 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.95\"} 95\n";
	const char *compared_p99_0 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.99\"} 99\n";
	const char *compared_cnt_0 = "maat_hit_rate_count{app_name=\"maat\"} 100\n";
	const char *compared_sum_0 = "maat_hit_rate_sum{app_name=\"maat\"} 5050\n";
	const char *compared_p10_1 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.10\"} 20\n";
	const char *compared_p50_1 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.50\"} 100\n";
	const char *compared_p80_1 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.80\"} 160\n";
	const char *compared_p90_1 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.90\"} 180\n";
	const char *compared_p95_1 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.95\"} 190\n";
	const char *compared_p99_1 = "maat_hit_rate{app_name=\"maat\",quantile=\"0.99\"} 198\n";
	const char *compared_cnt_1 = "maat_hit_rate_count{app_name=\"maat\"} 200\n";
	const char *compared_sum_1 = "maat_hit_rate_sum{app_name=\"maat\"} 20100\n";

	struct fieldstat_instance * instance = NULL;

	instance = fieldstat_instance_new("maat");
	fieldstat_global_enable_prometheus_endpoint(9027, NULL);
	fieldstat_enable_prometheus_output(instance);
	metric_id = fieldstat_register_summary(instance, "hit_rate", NULL, 0, 
										   quantiles, 1, 100, 2, 1);
	EXPECT_EQ(0, metric_id);
	fieldstat_instance_start(instance);
	system("cat /dev/null > /tmp/prometheus.txt");
	for(int i = 0; i < 100; i ++)
	{
		fieldstat_value_set(instance, metric_id, i + 1);
	}
	system("curl -s  http://127.0.0.1:9027/metrics -o /tmp/prometheus.txt");

	fp = fopen(prometheus_file, "r");
	EXPECT_NE(nullptr, fp);
	while(!feof(fp))
	{
		if(NULL == fgets(read_line, sizeof(read_line), fp))
		{
			continue;
		}
		switch(n_line)
		{
			case 0:
				EXPECT_STREQ(compared_p10_0, read_line);
				break;
			case 1:
				EXPECT_STREQ(compared_p50_0, read_line);
				break;
			case 2:
				EXPECT_STREQ(compared_p80_0, read_line);
				break;
			case 3:
				EXPECT_STREQ(compared_p90_0, read_line);
				break;
			case 4:
				EXPECT_STREQ(compared_p95_0, read_line);
				break;
			case 5:
				EXPECT_STREQ(compared_p99_0, read_line);
				break;
			case 6:
				EXPECT_STREQ(compared_cnt_0, read_line);
				break;
			case 7:
				EXPECT_STREQ(compared_sum_0, read_line);
				break;
			default:
				break;
		}
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(8, n_line);

	for(int i = 0; i < 100; i ++)
	{
		fieldstat_value_set(instance, metric_id, i + 101);
	}
	sleep(1);
	n_line = 0;
	system("curl -s  http://127.0.0.1:9027/metrics -o /tmp/prometheus.txt");
	fp = fopen(prometheus_file, "r");
	EXPECT_NE(nullptr, fp);
	while(!feof(fp))
	{
		if(NULL == fgets(read_line, sizeof(read_line), fp))
		{
			continue;
		}
		switch(n_line)
		{
			case 0:
				EXPECT_STREQ(compared_p10_1, read_line);
				break;
			case 1:
				EXPECT_STREQ(compared_p50_1, read_line);
				break;
			case 2:
				EXPECT_STREQ(compared_p80_1, read_line);
				break;
			case 3:
				EXPECT_STREQ(compared_p90_1, read_line);
				break;
			case 4:
				EXPECT_STREQ(compared_p95_1, read_line);
				break;
			case 5:
				EXPECT_STREQ(compared_p99_1, read_line);
				break;
			case 6:
				EXPECT_STREQ(compared_cnt_1, read_line);
				break;
			case 7:
				EXPECT_STREQ(compared_sum_1, read_line);
				break;
			default:
				break;
		}
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(8, n_line);

	fieldstat_global_disable_prometheus_endpoint();
	fieldstat_instance_free(instance);
}

TEST(FeildStatOutput, OutputMetricsNameWithPoint)
{
	int ret = 0, table_id = -1;
	struct fieldstat_instance * instance = NULL;
	int output_metric_ids[55] = {0};

	const char *table_column_name_with_points[] = {
		"new.conn.num", "established.conn_num", "close.conn_num", "total.in.bytes", "total_out.bytes",
		"total_in_packets","total_out_packets", "default_conn_num","default_in_bytes", "default_out_bytes",
		"default_in_packets", "default_out_packets","allow_conn_num","allow_in_bytes", "allow_out_bytes",
		"allow_in_packets","allow_out_packets", "deny_conn_num", "deny_in_bytes", "deny_out_bytes",
		"deny_in_packets", "deny_out_packets", "monitor_conn_num", "monitor_in_bytes","monitor_out_bytes",
		"monitor_in_packets", "monitor_out_packets", "intercept_conn_num","intercept_in_bytes",
		"intercept_out_bytes", "intercept_in_packets", "intercept_out_packets","ipv4_in_packets", 
		"ipv4_in_bytes","ipv4_out_packets", "ipv4_out_bytes", "ipv6_in_packets", "ipv6_in_bytes",
		"ipv6_out_packets", "ipv6_out_bytes", "tcp_conn_num", "tcp_in_packets", "tcp_in_bytes",
		"tcp_out_packets", "tcp_out_bytes", "udp_conn_num","udp_in_packets", "udp_in_bytes",
		"udp_out_packets", "udp_out_bytes", "alert_bytes", "block_bytes","pinning_num", "maybe_pinning_num",
		"not_pinning_num"
	};

	instance = fieldstat_instance_new("tsg_statistic");
	EXPECT_STREQ("tsg_statistic", instance->name);

	table_id = fieldstat_register_table(instance, "network.activity", 
										table_column_name_with_points, table_column_type,
										sizeof(table_column_name_with_points)/sizeof(table_column_name_with_points[0]));
	EXPECT_EQ(0, table_id);
	ret = fieldstat_register_table_row(instance, table_id, "TRAFFIC", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_set_local_output(instance, file_output_path, "default");
	EXPECT_EQ(0, ret);
	ret = fieldstat_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);
	ret = fieldstat_global_enable_prometheus_endpoint(9028, NULL);
	EXPECT_EQ(0, ret);
	ret = fieldstat_enable_prometheus_output(instance);
	EXPECT_EQ(0, ret);

	EXPECT_EQ(13, instance->output_type);
	ret = system("cat /dev/null > /tmp/prometheus.txt");
	ret = system("cat /dev/null > /tmp/metrics.out");
	ret = system("cat /dev/null > /tmp/fieldstat.txt");
	fieldstat_instance_start(instance);
	sleep(1);
	fieldstat_value_incrby(instance, output_metric_ids[0], 1000);
	sleep(2);
	check_file_output_content(5);
	check_lineprotocol_output_content(1);
	check_prometheus_output_content(55, 9028);

	fieldstat_global_disable_prometheus_endpoint();
	fieldstat_instance_free(instance);

}


int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}