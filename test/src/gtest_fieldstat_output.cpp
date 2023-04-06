#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "fieldstat.h"
#include "fieldstat_internal.h"

extern struct prometheus_endpoint_instance g_prometheus_endpoint_instance;

TEST(FeildStatOutput, OutputFile)
{
	int ret = 0;
	int n_loops = 4;
	struct fieldstat_instance * instance = NULL;
	int table_id = -1;
	FILE *fp;
	char read_line_buf[2048] = {0};

	int output_metric_ids[54] = {0};

	const char *output_file_path = "/tmp/fieldstat.txt"; 
	const char *table_column = "network_activity    			new_conn_num	established_conn_num"
								"	close_conn_num	total_in_bytes	total_out_bytes	total_in_packets	"
								"total_out_packets	default_conn_num	default_in_bytes	default_out_bytes	"
								"default_in_packets	default_out_packets	allow_conn_num	allow_in_bytes	"
								"allow_out_bytes	allow_in_packets	allow_out_packets	deny_conn_num	"
								"deny_in_bytes	deny_out_bytes	deny_in_packets	deny_out_packets	monitor_conn_num	"
								"monitor_in_bytes	monitor_out_bytes	monitor_in_packets	monitor_out_packets	intercept_conn_num	"
								"intercept_in_bytes	intercept_out_bytes	intercept_in_packets	intercept_out_packets	"
								"ipv4_in_packets	ipv4_in_bytes	ipv4_out_packetsipv4_out_bytes	ipv6_in_packets	ipv6_in_bytes	"
								"ipv6_out_packets	ipv6_out_bytes	tcp_conn_num	tcp_in_packets	tcp_in_bytes	tcp_out_packets	"
								"tcp_out_bytes	udp_conn_num	udp_in_packets	udp_in_bytes	udp_out_packets	udp_out_bytes	"
								"alert_bytes	block_bytes	pinning_num	maybe_pinning_num	not_pinning_num\n";
	
	const char *table_row_value = "TRAFFIC       1000	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	\n";

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
	ret = fieldstat_set_local_output(instance, output_file_path, "default");
	EXPECT_EQ(0, ret);
	fieldstat_instance_start(instance);
	fieldstat_value_incrby(instance, output_metric_ids[0], 1000);
	sleep(3);
	fp = fopen(output_file_path, "r");
	EXPECT_NE(nullptr, fp);
	int n_line = 0;

	while(!feof(fp))
	{
		if(NULL == fgets(read_line_buf, sizeof(read_line_buf), fp))
		{
			continue;
		}
		if(n_line == 1)
		{
			EXPECT_STREQ(table_column, read_line_buf);
		}
		if(n_line == 2)
		{
			EXPECT_STREQ(table_row_value, read_line_buf);
		}
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(n_line, n_loops);
	fieldstat_instance_free(instance);

}


TEST(FeildStatOutput, OutputLineProtocol)
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
	fieldstat_instance_start(instance);
	fieldstat_value_incrby(instance, output_metric_ids[0], 1000);
	ret = system("cat /dev/null > /tmp/metrics.out");
	sleep(3);
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

TEST(FeildStatOutput, OutputPrometheus)
{
	int ret = 0;
	int table_id = -1;
	int n_loops = 54;
	int output_metric_ids[54] = {0};
	struct fieldstat_instance * instance = NULL;
	FILE *fp;
	const char *prometheus_output_file = "/tmp/prometheus.txt";
	char line[2048] = {0};
	const char *compare = "tsg_statistic_new_conn_num{app_name=\"tsg_statistic\",table_name=\"network_activity\",line_name=\"TRAFFIC\"} 1000\n";

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
	ret = fieldstat_global_enable_prometheus_endpoint(9020, NULL);
	EXPECT_EQ(0, ret);
	ret = fieldstat_enable_prometheus_output(instance);
	EXPECT_EQ(0, ret);
	fieldstat_instance_start(instance);
	fieldstat_value_incrby(instance, output_metric_ids[0], 1000);
	EXPECT_EQ(0, ret);
	sleep(1);
	system("cat /dev/null > /tmp/prometheus.txt");
	system("curl -s  http://127.0.0.1:9020/metrics -o /tmp/prometheus.txt");

	int n_line = 0;
	fp = fopen(prometheus_output_file, "r");
	EXPECT_NE(nullptr, fp);
	while(!feof(fp))
	{
		if(NULL == fgets(line, sizeof(line), fp))
		{
			continue;
		}
		if(n_line == 0)
		{
			EXPECT_STREQ(compare, line);
		}
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(n_line, n_loops);

	fieldstat_global_disable_prometheus_endpoint();
	fieldstat_instance_free(instance);
}

TEST(FeildStatOutput, OutputFileAndLineProtocol)
{
	int ret = 0;
	int n_loops = 1;
	struct fieldstat_instance * instance = NULL;
	int table_id = -1;
	int output_metric_ids[54] = {0};
	const char *telegraf_output_file = "/tmp/metrics.out";
	FILE *fp;
	char line[2048] = {0};
	char read_line_buf[2048] = {0};
	cJSON *cjson_metric = NULL;
	cJSON *cjson_tags = NULL;

	const char *output_file_path = "/tmp/fieldstat.txt"; 
	const char *table_column = "network_activity    			new_conn_num	established_conn_num"
								"	close_conn_num	total_in_bytes	total_out_bytes	total_in_packets	"
								"total_out_packets	default_conn_num	default_in_bytes	default_out_bytes	"
								"default_in_packets	default_out_packets	allow_conn_num	allow_in_bytes	"
								"allow_out_bytes	allow_in_packets	allow_out_packets	deny_conn_num	"
								"deny_in_bytes	deny_out_bytes	deny_in_packets	deny_out_packets	monitor_conn_num	"
								"monitor_in_bytes	monitor_out_bytes	monitor_in_packets	monitor_out_packets	intercept_conn_num	"
								"intercept_in_bytes	intercept_out_bytes	intercept_in_packets	intercept_out_packets	"
								"ipv4_in_packets	ipv4_in_bytes	ipv4_out_packetsipv4_out_bytes	ipv6_in_packets	ipv6_in_bytes	"
								"ipv6_out_packets	ipv6_out_bytes	tcp_conn_num	tcp_in_packets	tcp_in_bytes	tcp_out_packets	"
								"tcp_out_bytes	udp_conn_num	udp_in_packets	udp_in_bytes	udp_out_packets	udp_out_bytes	"
								"alert_bytes	block_bytes	pinning_num	maybe_pinning_num	not_pinning_num\n";
	
	const char *table_row_value = "TRAFFIC       1000	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	\n";


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
	ret = fieldstat_set_local_output(instance, output_file_path, "default");
	EXPECT_EQ(0, ret);

	EXPECT_EQ(12, instance->output_type);
	ret = system("cat /dev/null > /tmp/metrics.out");
	ret = system("cat /dev/null > /tmp/fieldstat.txt");
	fieldstat_instance_start(instance);
	fieldstat_value_incrby(instance, output_metric_ids[0], 1000);
	sleep(3);
	int n_line = 0;
	fp = fopen(telegraf_output_file, "r");
	EXPECT_NE(nullptr, fp);
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

	fp = fopen("/tmp/fieldstat.txt", "r");
	EXPECT_NE(nullptr, fp);
	n_line = 0;
	n_loops = 4;

	while(!feof(fp))
	{
		if(NULL == fgets(read_line_buf, sizeof(read_line_buf), fp))
		{
			continue;
		}
		if(n_line == 1)
		{
			EXPECT_STREQ(table_column, read_line_buf);
		}
		if(n_line == 2)
		{
			EXPECT_STREQ(table_row_value, read_line_buf);
		}
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(n_line, n_loops);


	fieldstat_instance_free(instance);

}


TEST(FeildStatOutput, OutputFileAndPrometheus)
{
	int ret = 0;
	int n_loops = 0;
	struct fieldstat_instance * instance = NULL;
	int table_id = -1;
	int output_metric_ids[54] = {0};
	FILE *fp;
	char line[2048] = {0};

	const char *prometheus_output_path = "/tmp/prometheus.txt";
	const char *file_output_path = "/tmp/fieldstat.txt";
	const char *prometheus_output_result = "tsg_statistic_new_conn_num{app_name=\"tsg_statistic\",table_name=\"network_activity\",line_name=\"TRAFFIC\"} 1000\n";
	const char *table_column = "network_activity    			new_conn_num	established_conn_num"
								"	close_conn_num	total_in_bytes	total_out_bytes	total_in_packets	"
								"total_out_packets	default_conn_num	default_in_bytes	default_out_bytes	"
								"default_in_packets	default_out_packets	allow_conn_num	allow_in_bytes	"
								"allow_out_bytes	allow_in_packets	allow_out_packets	deny_conn_num	"
								"deny_in_bytes	deny_out_bytes	deny_in_packets	deny_out_packets	monitor_conn_num	"
								"monitor_in_bytes	monitor_out_bytes	monitor_in_packets	monitor_out_packets	intercept_conn_num	"
								"intercept_in_bytes	intercept_out_bytes	intercept_in_packets	intercept_out_packets	"
								"ipv4_in_packets	ipv4_in_bytes	ipv4_out_packetsipv4_out_bytes	ipv6_in_packets	ipv6_in_bytes	"
								"ipv6_out_packets	ipv6_out_bytes	tcp_conn_num	tcp_in_packets	tcp_in_bytes	tcp_out_packets	"
								"tcp_out_bytes	udp_conn_num	udp_in_packets	udp_in_bytes	udp_out_packets	udp_out_bytes	"
								"alert_bytes	block_bytes	pinning_num	maybe_pinning_num	not_pinning_num\n";
	
	const char *table_row_value = "TRAFFIC       1000	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	\n";



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
	fieldstat_value_incrby(instance, output_metric_ids[0], 1000);
	sleep(3);
	system("curl -s  http://127.0.0.1:9021/metrics -o /tmp/prometheus.txt");
	int n_line = 0;

	fp = fopen("/tmp/fieldstat.txt", "r");
	EXPECT_NE(nullptr, fp);
	n_line = 0;
	n_loops = 4;

	while(!feof(fp))
	{
		if(NULL == fgets(line, sizeof(line), fp))
		{
			continue;
		}
		if(n_line == 1)
		{
			EXPECT_STREQ(table_column, line);
		}
		if(n_line == 2)
		{
			EXPECT_STREQ(table_row_value, line);
		}
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(n_line, n_loops);
	n_loops = 54;
	n_line = 0;
	fp = fopen(prometheus_output_path, "r");
	EXPECT_NE(nullptr, fp);
	while(!feof(fp))
	{
		if(NULL == fgets(line, sizeof(line), fp))
		{
			continue;
		}
		if(n_line == 0)
		{
			EXPECT_STREQ(prometheus_output_result, line);
		}
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(n_line, n_loops);
	fieldstat_global_disable_prometheus_endpoint();
	fieldstat_instance_free(instance);

}

TEST(FeildStatOutput, OutputFileAndPrometheusAndLineProtocol)
{
	int ret = 0;
	int n_loops = 0;
	struct fieldstat_instance * instance = NULL;
	int table_id = -1;
	int output_metric_ids[54] = {0};
	FILE *fp;
	char line[2048] = {0};

	const char *prometheus_output_path = "/tmp/prometheus.txt";
	const char *file_output_path = "/tmp/fieldstat.txt";
	const char *telegraf_output_file = "/tmp/metrics.out";
	const char *prometheus_output_result = "tsg_statistic_new_conn_num{app_name=\"tsg_statistic\",table_name=\"network_activity\",line_name=\"TRAFFIC\"} 1000\n";
	const char *table_column = "network_activity    			new_conn_num	established_conn_num"
								"	close_conn_num	total_in_bytes	total_out_bytes	total_in_packets	"
								"total_out_packets	default_conn_num	default_in_bytes	default_out_bytes	"
								"default_in_packets	default_out_packets	allow_conn_num	allow_in_bytes	"
								"allow_out_bytes	allow_in_packets	allow_out_packets	deny_conn_num	"
								"deny_in_bytes	deny_out_bytes	deny_in_packets	deny_out_packets	monitor_conn_num	"
								"monitor_in_bytes	monitor_out_bytes	monitor_in_packets	monitor_out_packets	intercept_conn_num	"
								"intercept_in_bytes	intercept_out_bytes	intercept_in_packets	intercept_out_packets	"
								"ipv4_in_packets	ipv4_in_bytes	ipv4_out_packetsipv4_out_bytes	ipv6_in_packets	ipv6_in_bytes	"
								"ipv6_out_packets	ipv6_out_bytes	tcp_conn_num	tcp_in_packets	tcp_in_bytes	tcp_out_packets	"
								"tcp_out_bytes	udp_conn_num	udp_in_packets	udp_in_bytes	udp_out_packets	udp_out_bytes	"
								"alert_bytes	block_bytes	pinning_num	maybe_pinning_num	not_pinning_num\n";
	
	const char *table_row_value = "TRAFFIC       1000	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	         "
									"0	         0	         0	         0	\n";



	const char *table_column_name[] = {"new_conn_num", "established_conn_num", "close_conn_num", "total_in_bytes", "total_out_bytes", "total_in_packets", 
	"total_out_packets", "default_conn_num", "default_in_bytes", "default_out_bytes", "default_in_packets", "default_out_packets", "allow_conn_num",
	"allow_in_bytes", "allow_out_bytes", "allow_in_packets", "allow_out_packets", "deny_conn_num", "deny_in_bytes", "deny_out_bytes", "deny_in_packets",
	"deny_out_packets", "monitor_conn_num", "monitor_in_bytes", "monitor_out_bytes", "monitor_in_packets", "monitor_out_packets", "intercept_conn_num",
	"intercept_in_bytes", "intercept_out_bytes", "intercept_in_packets", "intercept_out_packets", "ipv4_in_packets", "ipv4_in_bytes", "ipv4_out_packets"
	"ipv4_out_bytes", "ipv6_in_packets", "ipv6_in_bytes", "ipv6_out_packets", "ipv6_out_bytes", "tcp_conn_num", "tcp_in_packets", "tcp_in_bytes",
	"tcp_out_packets", "tcp_out_bytes", "udp_conn_num", "udp_in_packets", "udp_in_bytes", "udp_out_packets", "udp_out_bytes", "alert_bytes", "block_bytes",
	"pinning_num", "maybe_pinning_num", "not_pinning_num"};

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

	enum field_type table_column_type[54] = {FIELD_TYPE_COUNTER};

	instance = fieldstat_instance_new("tsg_statistic");
	EXPECT_STREQ("tsg_statistic", instance->name);

	table_id = fieldstat_register_table(instance, "network_activity", table_column_name, table_column_type, sizeof(table_column_name)/sizeof(table_column_name[0]));
	EXPECT_EQ(0, table_id);
	ret = fieldstat_register_table_row(instance, table_id, "TRAFFIC", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_set_local_output(instance, file_output_path, "default");
	EXPECT_EQ(0, ret);
	ret = fieldstat_global_enable_prometheus_endpoint(9022, NULL);
	EXPECT_EQ(0, ret);
	ret = fieldstat_enable_prometheus_output(instance);
	EXPECT_EQ(0, ret);

	EXPECT_EQ(9, instance->output_type);
	ret = system("cat /dev/null > /tmp/prometheus.txt");
	ret = system("cat /dev/null > /tmp/fieldstat.txt");
	fieldstat_instance_start(instance);
	fieldstat_value_incrby(instance, output_metric_ids[0], 1000);
	sleep(3);
	system("curl -s  http://127.0.0.1:9022/metrics -o /tmp/prometheus.txt");
	int n_line = 0;

	fp = fopen("/tmp/fieldstat.txt", "r");
	EXPECT_NE(nullptr, fp);
	n_line = 0;
	n_loops = 4;

	while(!feof(fp))
	{
		if(NULL == fgets(line, sizeof(line), fp))
		{
			continue;
		}
		if(n_line == 1)
		{
			EXPECT_STREQ(table_column, line);
		}
		if(n_line == 2)
		{
			EXPECT_STREQ(table_row_value, line);
		}
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(n_line, n_loops);
	n_loops = 54;
	n_line = 0;
	fp = fopen(prometheus_output_path, "r");
	EXPECT_NE(nullptr, fp);
	while(!feof(fp))
	{
		if(NULL == fgets(line, sizeof(line), fp))
		{
			continue;
		}
		if(n_line == 0)
		{
			EXPECT_STREQ(prometheus_output_result, line);
		}
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(n_line, n_loops);

	n_loops = 1;
	n_line = 0;
	fp = fopen(telegraf_output_file, "r");
	EXPECT_NE(nullptr, fp);

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

	fieldstat_global_disable_prometheus_endpoint();
	fieldstat_instance_free(instance);

}


int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}