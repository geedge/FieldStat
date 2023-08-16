#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "fieldstat.h"
#include "fieldstat_internal.h"
#include "cJSON.h"

extern struct prometheus_endpoint_instance g_prometheus_endpoint_instance;

TEST(FeildStatDynamicAPI, FieldStatDynamicInstanceNew)
{
	struct fieldstat_dynamic_instance *instance = NULL;
	instance = fieldstat_dynamic_instance_new("firewall", 16);

	EXPECT_NE(nullptr, instance);
	EXPECT_STREQ("firewall", instance->name);
	EXPECT_EQ(16, instance->n_thread);

	for(int i = 0; i < instance->n_thread; i++)
	{
		EXPECT_EQ(nullptr, instance->n_thread_dynamic_metric[i]);
	}
	fieldstat_dynamic_instance_free(instance);

	instance = fieldstat_dynamic_instance_new("firewall-name-length-more-than-32", 16);
	EXPECT_EQ(nullptr, instance);

	instance = fieldstat_dynamic_instance_new("firewall", 0);
	EXPECT_EQ(nullptr, instance);

	instance = fieldstat_dynamic_instance_new("firewall\n", 16);
	EXPECT_EQ(nullptr, instance);

	instance = fieldstat_dynamic_instance_new("firewall|", 16);
	EXPECT_EQ(nullptr, instance);

	instance = fieldstat_dynamic_instance_new("firewall:", 16);
	EXPECT_EQ(nullptr, instance);

	instance = fieldstat_dynamic_instance_new("firewall.", 16);
	EXPECT_NE(nullptr, instance);
	fieldstat_dynamic_instance_free(instance);

	instance = fieldstat_dynamic_instance_new("firewall ", 16);
	EXPECT_EQ(nullptr, instance);

	instance = fieldstat_dynamic_instance_new("firewall\t", 16);
	EXPECT_EQ(nullptr, instance);

	instance = fieldstat_dynamic_instance_new("firewall<", 16);
	EXPECT_EQ(nullptr, instance);

	instance = fieldstat_dynamic_instance_new("firewall>", 16);
	EXPECT_EQ(nullptr, instance);

	instance = fieldstat_dynamic_instance_new("firewall[", 16);
	EXPECT_EQ(nullptr, instance);

	instance = fieldstat_dynamic_instance_new("firewall]", 16);
	EXPECT_EQ(nullptr, instance);

	instance = fieldstat_dynamic_instance_new("firewall#", 16);
	EXPECT_EQ(nullptr, instance);

	instance = fieldstat_dynamic_instance_new("firewall!", 16);
	EXPECT_EQ(nullptr, instance);

	instance = fieldstat_dynamic_instance_new("firewall@", 16);
	EXPECT_EQ(nullptr, instance);

}

TEST(FeildStatDynamicAPI, FieldStatDynamicInstanceSetPara)
{
	int ret = 0;
	int input_ip = 0;

	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", 16);
	EXPECT_NE(nullptr, instance);

	ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8080);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(8080, instance->line_protocol_output.server_port);
	ret = inet_pton(AF_INET, "127.0.0.1", (void *)&(input_ip));
	EXPECT_EQ(1, ret);
	EXPECT_EQ(input_ip, instance->line_protocol_output.server_ip);
	EXPECT_NE(-1, instance->line_protocol_output.send_socket);

	ret = fieldstat_dynamic_disable_background_thread(instance);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(1, instance->background_thread_disable);

	ret = fieldstat_dynamic_set_output_interval(instance, 5000);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(5000, instance->output_interval_ms);
	fieldstat_dynamic_instance_free(instance);
}


TEST(FeildStatDynamicAPI, FieldStatDynamicStart)
{
	int ret = 0;
	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", 16);
	EXPECT_NE(nullptr, instance);

	fieldstat_dynamic_instance_start(instance);
	EXPECT_EQ(1, instance->running);

	ret = pthread_kill(instance->background_thread, 0);
	EXPECT_EQ(0, ret);
	fieldstat_dynamic_instance_free(instance);
}

TEST(FeildStatDynamicAPI, FieldStatDynamicPassiveOutput)
{
	int ret = 0;

	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", 16);
	EXPECT_NE(nullptr, instance);
	ret = fieldstat_dynamic_set_output_interval(instance, 50);
	EXPECT_EQ(0, ret);
	fieldstat_dynamic_passive_output(instance);

	EXPECT_EQ(0, instance->last_output_time.tv_sec);
	EXPECT_EQ(0, instance->last_output_time.tv_nsec);

	usleep(50 * 1000);

	fieldstat_dynamic_passive_output(instance);
	EXPECT_EQ(0, instance->last_output_time.tv_sec);
	EXPECT_EQ(0, instance->last_output_time.tv_nsec);

	instance->running = 1;
	usleep(50 * 1000);

	fieldstat_dynamic_passive_output(instance);
	EXPECT_NE(0, instance->last_output_time.tv_sec);	
	EXPECT_NE(0, instance->last_output_time.tv_nsec);
	fieldstat_dynamic_instance_free(instance);
}

TEST(FeildStatDynamicAPI, FieldStatRegisterDynamicTable)
{
	int ret = 0;
	const char *column_name[] = {"in_pkts", "in_bytes", "out_pkts", "out_bytes"};
	enum field_type column_type[] = {FIELD_TYPE_GAUGE, FIELD_TYPE_COUNTER, FIELD_TYPE_GAUGE, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[4];
	char table_name[32];

	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", 16);
	EXPECT_NE(nullptr, instance);
	ret = fieldstat_register_dynamic_table(instance, "traffic", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);
	
	EXPECT_EQ(0, ret);
	EXPECT_EQ(1, instance->table_num);
	EXPECT_NE(nullptr, instance->table_metrics[0]);

	
	for(int i = 0; i < 4; i++)
	{
		EXPECT_EQ(i, out_column_ids[i]);
	}

	for(int i = 0; i < 64; i++)
	{
		snprintf(table_name, sizeof(table_name), "t_%d", i);
		ret = fieldstat_register_dynamic_table(instance, (const char *)table_name, column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);

		switch(i)
		{
			case 62:
				EXPECT_EQ(63, ret);
				EXPECT_EQ(64, instance->table_num);
				break;
			case 63:
				EXPECT_EQ(-1, ret);
				EXPECT_EQ(64, instance->table_num);
				break;
			default:
				break;
		}

	}
	fieldstat_dynamic_instance_free(instance);
}


TEST(FeildStatDynamicAPI, FieldStatDynamicMetricValueIncrby)
{
	int ret = 0;
	int n_thread = 64;
	struct fieldstat_tag tags[3];

	tags[0].key = "policy_id";
	tags[0].value_int = 1;
	tags[0].value_type = 0;

	tags[1].key = "quanlity";
	tags[1].value_double = 0.50;
	tags[1].value_type = 1;

	tags[2].key = "device_id";
	tags[2].value_str = "test_device";
	tags[2].value_type = 2;

	struct dynamic_metric *dyn_metric, *tmp_dyn_metric;
	struct dynamic_metric **head = NULL;
	struct metric **metrics = NULL;
	struct metric *metric = NULL;
	long long value = 0;

	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", n_thread);


	for(int i = 0; i < n_thread; i++)
	{
		ret = fieldstat_dynamic_metric_value_incrby(instance, FIELD_TYPE_GAUGE, "Active_sessions", 100 + i, tags, sizeof(tags)/sizeof(tags[0]), i); 
		EXPECT_EQ(0, ret);
		ret = fieldstat_dynamic_metric_value_incrby(instance, FIELD_TYPE_COUNTER, "Traffic_bytes", 10 + i, tags, sizeof(tags)/sizeof(tags[0]), i); 
		EXPECT_EQ(0, ret);
	}
	
	for(int i = 0; i < n_thread; i++)
	{
		head = &instance->n_thread_dynamic_metric[i];
		HASH_ITER(hh, *head, dyn_metric, tmp_dyn_metric)
		{
			metrics = dyn_metric->metrics;
			metric  = metrics[0];
			switch(metric->field_type)
			{
				case FIELD_TYPE_COUNTER:
					value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
					EXPECT_EQ(value, 10 + i);
					break;
				case FIELD_TYPE_GAUGE:
					value = get_metric_unit_val(metric, FS_CALC_CURRENT, 0);
					EXPECT_EQ(value, 100 + i);
					break;
				default:
					FAIL() << "Field type should be in {counter, gauge}";
			}			
		}	
	}

	fieldstat_dynamic_instance_free(instance);
}


TEST(FeildStatDynamicAPI, FieldStatDynamicMetricValueSet)
{
	int ret = 0;
	int n_thread = 64;
	struct fieldstat_tag tags[3];

	tags[0].key = "policy_id";
	tags[0].value_int = 1;
	tags[0].value_type = 0;

	tags[1].key = "quanlity";
	tags[1].value_double = 0.50;
	tags[1].value_type = 1;

	tags[2].key = "device_id";
	tags[2].value_str = "test_device";
	tags[2].value_type = 2;

	struct dynamic_metric *dyn_metric, *tmp_dyn_metric;
	struct dynamic_metric **head = NULL;
	struct metric **metrics = NULL;
	struct metric *metric = NULL;
	long long value = 0;

	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", n_thread);


	for(int i = 0; i < n_thread; i++)
	{
		ret = fieldstat_dynamic_metric_value_set(instance, FIELD_TYPE_GAUGE, "Active_sessions", 100 + i, tags, sizeof(tags)/sizeof(tags[0]), i); 
		EXPECT_EQ(0, ret);
		ret = fieldstat_dynamic_metric_value_set(instance, FIELD_TYPE_COUNTER, "Traffic_bytes", 10 + i, tags, sizeof(tags)/sizeof(tags[0]), i); 
		EXPECT_EQ(0, ret);
	}
	
	for(int i = 0; i < n_thread; i++)
	{
		head = &instance->n_thread_dynamic_metric[i];
		HASH_ITER(hh, *head, dyn_metric, tmp_dyn_metric)
		{
			metrics = dyn_metric->metrics;
			metric  = metrics[0];
			switch(metric->field_type)
			{
				case FIELD_TYPE_COUNTER:
					value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
					EXPECT_EQ(value, 10 + i);
					break;
				case FIELD_TYPE_GAUGE:
					value = get_metric_unit_val(metric, FS_CALC_CURRENT, 0);
					EXPECT_EQ(value, 100 + i);
					break;
				default:
					FAIL() << "Field type should be in {counter, gauge}";
			}
		}	
	}
	fieldstat_dynamic_instance_free(instance);
}

TEST(FeildStatDynamicAPI, FieldStatDynamicMetricValueDecrby)
{
	int ret = 0;
	int n_thread = 64;
	struct fieldstat_tag tags[3];

	tags[0].key = "policy_id";
	tags[0].value_int = 1;
	tags[0].value_type = 0;

	tags[1].key = "quanlity";
	tags[1].value_double = 0.50;
	tags[1].value_type = 1;

	tags[2].key = "device_id";
	tags[2].value_str = "test_device";
	tags[2].value_type = 2;

	struct dynamic_metric *dyn_metric, *tmp_dyn_metric;
	struct dynamic_metric **head = NULL;
	struct metric **metrics = NULL;
	struct metric *metric = NULL;
	long long value = 0;

	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", n_thread);


	for(int i = 0; i < n_thread; i++)
	{
		ret = fieldstat_dynamic_metric_value_decrby(instance, FIELD_TYPE_GAUGE, "Active_sessions", 100 + i, tags, sizeof(tags)/sizeof(tags[0]), i); 
		EXPECT_EQ(0, ret);
		ret = fieldstat_dynamic_metric_value_decrby(instance, FIELD_TYPE_COUNTER, "Traffic_bytes", 10 + i, tags, sizeof(tags)/sizeof(tags[0]), i); 
		EXPECT_EQ(0, ret);
	}
	
	for(int i = 0; i < n_thread; i++)
	{
		head = &instance->n_thread_dynamic_metric[i];
		HASH_ITER(hh, *head, dyn_metric, tmp_dyn_metric)
		{
			metrics = dyn_metric->metrics;
			metric  = metrics[0];
			switch(metric->field_type)
			{
				case FIELD_TYPE_COUNTER:
					value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
					EXPECT_EQ(value, -(10 + i));
					break;
				case FIELD_TYPE_GAUGE:
					value = get_metric_unit_val(metric, FS_CALC_CURRENT, 0);
					EXPECT_EQ(value, -(100 + i));
					break;
				default:
					FAIL() << "Field type should be in {counter, gauge}";
			}
		}	
	}
	fieldstat_dynamic_instance_free(instance);
}


TEST(FeildStatDynamicAPI, FieldStatDynamicTableMetricValueIncrby)
{
	int ret = 0;
	int n_thread = 64;
	struct fieldstat_tag tags[3];

	tags[0].key = "policy_id";
	tags[0].value_int = 1;
	tags[0].value_type = 0;

	tags[1].key = "quanlity";
	tags[1].value_double = 0.50;
	tags[1].value_type = 1;

	tags[2].key = "device_id";
	tags[2].value_str = "test_device";
	tags[2].value_type = 2;

	struct dynamic_metric *dyn_metric, *tmp_dyn_metric;
	struct dynamic_metric **head = NULL;
	struct metric **metrics = NULL;
	struct metric *metric = NULL;
	long long value = 0;

	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];
	int table_id = -1;


	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", n_thread);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);

	EXPECT_EQ(0, table_id);
	for(int i = 0; i < n_thread; i++)
	{
		ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[0], "security_rule_hits", 20 + i, tags, sizeof(tags)/sizeof(tags[0]), i); 
		EXPECT_EQ(0, ret);
		ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[1], "security_rule_hits", 200 + i, tags, sizeof(tags)/sizeof(tags[0]), i);
		EXPECT_EQ(0, ret);
	}
	//EXPECT Per thread add one value. 
	for(int i = 0; i < n_thread; i++)
	{
		head = &instance->n_thread_dynamic_metric[i];
		HASH_ITER(hh, *head, dyn_metric, tmp_dyn_metric)
		{
			metrics = dyn_metric->metrics;
			for(int j = 0; j < 2; j++)
			{
				metric = metrics[j];
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
				switch(j)
				{
					case 0:
						EXPECT_EQ(value, 20 + i);
						break;
					case 1:
						EXPECT_EQ(value, 200 + i);
						break;
					default:
						FAIL() << "Field type should be in {counter, gauge}";
				}
			}
		}	
	}
	fieldstat_dynamic_instance_free(instance);
}


TEST(FeildStatDynamicAPI, FieldStatDynamicTableMetricValueSet)
{
	int ret = 0;
	int n_thread = 64;
	struct fieldstat_tag tags[3];

	tags[0].key = "policy_id";
	tags[0].value_int = 1;
	tags[0].value_type = 0;

	tags[1].key = "quanlity";
	tags[1].value_double = 0.50;
	tags[1].value_type = 1;

	tags[2].key = "device_id";
	tags[2].value_str = "test_device";
	tags[2].value_type = 2;

	struct dynamic_metric *dyn_metric, *tmp_dyn_metric;
	struct dynamic_metric **head = NULL;
	struct metric **metrics = NULL;
	struct metric *metric = NULL;
	long long value = 0;

	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];
	int table_id = -1;


	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", n_thread);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);

	EXPECT_EQ(0, table_id);	

	for(int i = 0; i < n_thread; i++)
	{
		ret = fieldstat_dynamic_table_metric_value_set(instance, table_id, out_column_ids[0], "security_rule_hits", 20 + i, tags, sizeof(tags)/sizeof(tags[0]), i); 
		EXPECT_EQ(0, ret);
		ret = fieldstat_dynamic_table_metric_value_set(instance, table_id, out_column_ids[1], "security_rule_hits", 200 + i, tags, sizeof(tags)/sizeof(tags[0]), i);
		EXPECT_EQ(0, ret);
	}
	
	for(int i = 0; i < n_thread; i++)
	{
		head = &instance->n_thread_dynamic_metric[i];
		HASH_ITER(hh, *head, dyn_metric, tmp_dyn_metric)
		{
			metrics = dyn_metric->metrics;
			for(int j = 0; j < 2; j++)
			{
				metric = metrics[j];
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
				switch(j)
				{
					case 0:
						EXPECT_EQ(value, 20 + i);
						break;
					case 1:
						EXPECT_EQ(value, 200 + i);
						break;
					default:
						FAIL() << "Field type should be in {counter, gauge}";
				}
			}
		}	
	}
	fieldstat_dynamic_instance_free(instance);
}

TEST(FeildStatDynamicAPI, FieldStatDynamicTableMetricValueDecrby)
{
	int ret = 0;
	int n_thread = 64;
	struct fieldstat_tag tags[3];

	tags[0].key = "policy_id";
	tags[0].value_int = 1;
	tags[0].value_type = 0;

	tags[1].key = "quanlity";
	tags[1].value_double = 0.50;
	tags[1].value_type = 1;

	tags[2].key = "device_id";
	tags[2].value_str = "test_device";
	tags[2].value_type = 2;

	struct dynamic_metric *dyn_metric, *tmp_dyn_metric;
	struct dynamic_metric **head = NULL;
	struct metric **metrics = NULL;
	struct metric *metric = NULL;
	long long value = 0;

	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];
	int table_id = -1;


	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", n_thread);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);

	EXPECT_EQ(0, table_id);	

	for(int i = 0; i < n_thread; i++)
	{
		ret = fieldstat_dynamic_table_metric_value_decrby(instance, table_id, out_column_ids[0], "security_rule_hits", 20 + i, tags, sizeof(tags)/sizeof(tags[0]), i); 
		EXPECT_EQ(0, ret);
		ret = fieldstat_dynamic_table_metric_value_decrby(instance, table_id, out_column_ids[1], "security_rule_hits", 200 + i, tags, sizeof(tags)/sizeof(tags[0]), i);
		EXPECT_EQ(0, ret);
	}
	
	for(int i = 0; i < n_thread; i++)
	{
		head = &instance->n_thread_dynamic_metric[i];
		HASH_ITER(hh, *head, dyn_metric, tmp_dyn_metric)
		{
			metrics = dyn_metric->metrics;
			for(int j = 0; j < 2; j++)
			{
				metric = metrics[j];
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
				switch(j)
				{
					case 0:
						EXPECT_EQ(value, -(20 + i));
						break;
					case 1:
						EXPECT_EQ(value, -(200 + i));
						break;
					default:
						FAIL() << "Field type should be in {counter, gauge}";
				}
			}
		}	
	}
	fieldstat_dynamic_instance_free(instance);
}


void parse_telegraf_cjson_output(const char *compare)
{
	const char *telegraf_output_file = "/tmp/metrics.out";
	FILE *fp;
	char line[1024] = {0};
	char *cjson_metric_str = NULL;

	fp = fopen(telegraf_output_file, "r");
	EXPECT_NE(nullptr, fp);

	while(!feof(fp))
	{
		fgets(line, sizeof(line), fp);
	}
	fclose(fp);

//{"fields":{"Active_sessions":10},"name":"Active_sessions","tags":{"app_name":"firewall","host":"developer-testing-41.22"},"timestamp":1679308196}
//{"fields":{"Active_sessions":10},"name":"Active_sessions","tags":{"app_name":"firewall"}}
	if(strlen(line) == 0)
	{
		return;
	}
	cJSON *cjson_metric = NULL;
	cJSON *cjson_tags = NULL;
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
		EXPECT_STREQ(cjson_metric_str, compare);
		free(cjson_metric_str);
		cjson_metric_str = NULL;
	}
	cJSON_Delete(cjson_metric);
	return;
}


TEST(FeildStatDynamicAPI, FieldStatDynamicOutputLineProtocolNoTag)
{
	int n_thread = 48;
	int ret = 0;
	struct fieldstat_dynamic_instance *instance = NULL;

	instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	const char *compare_zero_tag = "{\"fields\":{\"Active_sessions\":10},\"name\":\"Active_sessions\",\"tags\":{\"app_name\":\"firewall\"}}";


	EXPECT_NE(nullptr, instance);

	ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);

	ret = fieldstat_dynamic_metric_value_incrby(instance, FIELD_TYPE_GAUGE, "Active_sessions", 10, NULL, 0, 0);
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(1);
	parse_telegraf_cjson_output(compare_zero_tag);
	fieldstat_dynamic_instance_free(instance);
}


TEST(FeildStatDynamicAPI, FieldStatDynamicOutputLineProtocolOneTag)
{
	int n_thread = 48;
	int ret = 0;
	struct fieldstat_dynamic_instance *instance = NULL;
	struct fieldstat_tag tags[1];
	instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	const char *compare_one_tag  = "{\"fields\":{\"Active_sessions\":11},\"name\":\"Active_sessions\",\"tags\":{\"app_name\":\"firewall\",\"policy_id\":\"1\"}}";

	tags[0].key = "policy_id";
	tags[0].value_int = 1;
	tags[0].value_type = 0;

	EXPECT_NE(nullptr, instance);

	ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);
	ret = fieldstat_dynamic_metric_value_incrby(instance, FIELD_TYPE_GAUGE, "Active_sessions", 11, tags, 1, 0);
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(1);
	parse_telegraf_cjson_output(compare_one_tag);
	fieldstat_dynamic_instance_free(instance);

}


TEST(FeildStatDynamicAPI, FieldStatDynamicOutputLineProtocolTwoTags)
{
	int n_thread = 48;
	int ret = 0;
	struct fieldstat_dynamic_instance *instance = NULL;
	struct fieldstat_tag tags[2];
	instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	const char *compare_two_tags = "{\"fields\":{\"Active_sessions\":12},\"name\":\"Active_sessions\",\"tags\":{\"app_name\":\"firewall\",\"policy_id\":\"1\",\"quanlity\":\"0.500000\"}}";

	tags[0].key = "policy_id";
	tags[0].value_int = 1;
	tags[0].value_type = 0;

	tags[1].key = "quanlity";
	tags[1].value_double = 0.50;
	tags[1].value_type = 1;

	EXPECT_NE(nullptr, instance);

	ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);

	ret = fieldstat_dynamic_metric_value_incrby(instance, FIELD_TYPE_GAUGE, "Active_sessions", 12, tags, 2, 0);
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(1);
	parse_telegraf_cjson_output(compare_two_tags);
	fieldstat_dynamic_instance_free(instance);
}

TEST(FeildStatDynamicAPI, FieldStatDynamicOutputLineProtocolTableMetricNoTag)
{
	int n_thread = 48;
	int ret = 0;
	struct fieldstat_dynamic_instance *instance = NULL;
	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];
	int table_id = -1;
	const char *compare_zero_tag = "{\"fields\":{\"bytes\":30,\"packages\":30},\"name\":\"security_rule_hits\",\"tags\":{\"app_name\":\"firewall\",\"table_name\":\"shaping\"}}";



	instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	EXPECT_NE(nullptr, instance);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);
	EXPECT_EQ(0, table_id);

	ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);

	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[0], "security_rule_hits", 30, NULL, 0, 0);
	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[1], "security_rule_hits", 30, NULL, 0, 0);
	EXPECT_EQ(0, ret);
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(1);
	parse_telegraf_cjson_output(compare_zero_tag);
	fieldstat_dynamic_instance_free(instance);
	//parse_telegraf_cjson_output(compare_zero_tag_column1);
}


TEST(FeildStatDynamicAPI, FieldStatDynamicOutputLineProtocolTableMetricOneTag)
{
	int n_thread = 48;
	int ret = 0;
	struct fieldstat_dynamic_instance *instance = NULL;
	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];
	int table_id = -1;
	struct fieldstat_tag tags[1];

	const char *compare_one_tag = "{\"fields\":{\"bytes\":30,\"packages\":30},\"name\":\"security_rule_hits\",\"tags\":{\"app_name\":\"firewall\",\"policy_id\":\"1\",\"table_name\":\"shaping\"}}";

	tags[0].key = "policy_id";
	tags[0].value_int = 1;
	tags[0].value_type = 0;


	instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	EXPECT_NE(nullptr, instance);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);
	EXPECT_EQ(0, table_id);

	ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);

	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[0], "security_rule_hits", 30, tags, sizeof(tags)/sizeof(tags[0]), 0);
	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[1], "security_rule_hits", 30, tags, sizeof(tags)/sizeof(tags[0]), 0);
	EXPECT_EQ(0, ret);
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(1);
	parse_telegraf_cjson_output(compare_one_tag);
	fieldstat_dynamic_instance_free(instance);
	//parse_telegraf_cjson_output(compare_zero_tag_column1);
}


TEST(FeildStatDynamicAPI, FieldStatDynamicOutputLineProtocolTableMetricTowTags)
{
	int n_thread = 48;
	int ret = 0;
	struct fieldstat_dynamic_instance *instance = NULL;
	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];
	int table_id = -1;
	struct fieldstat_tag tags[2];

	const char *compare_two_tags = "{\"fields\":{\"bytes\":30,\"packages\":30},\"name\":\"security_rule_hits\",\"tags\":{\"app_name\":\"firewall\",\"policy_id\":\"1\",\"quanlity\":\"0.500000\",\"table_name\":\"shaping\"}}";

	tags[0].key = "policy_id";
	tags[0].value_int = 1;
	tags[0].value_type = 0;

	tags[1].key = "quanlity";
	tags[1].value_double = 0.50;
	tags[1].value_type = 1;


	instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	EXPECT_NE(nullptr, instance);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);
	EXPECT_EQ(0, table_id);

	ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);

	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[0], "security_rule_hits", 30, tags, sizeof(tags)/sizeof(tags[0]), 0);
	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[1], "security_rule_hits", 30, tags, sizeof(tags)/sizeof(tags[0]), 0);
	EXPECT_EQ(0, ret);
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(1);
	parse_telegraf_cjson_output(compare_two_tags);
	fieldstat_dynamic_instance_free(instance);
}


TEST(FeildStatDynamicAPI, FieldStatDynamicOutputLineProtocolTypeGauge)
{
	int n_thread = 48;
	int ret = 0;
	struct fieldstat_dynamic_instance *instance = NULL;

	const char *compare_first_output = "{\"fields\":{\"Active_sessions\":12},\"name\":\"Active_sessions\",\"tags\":{\"app_name\":\"firewall\"}}";
	const char *compare_second_output = "{\"fields\":{\"Active_sessions\":24},\"name\":\"Active_sessions\",\"tags\":{\"app_name\":\"firewall\"}}";

	instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	EXPECT_NE(nullptr, instance);

	ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);

	ret = fieldstat_dynamic_metric_value_incrby(instance, FIELD_TYPE_GAUGE, "Active_sessions", 12, NULL, 0, 0);
	EXPECT_EQ(0, ret);
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(1);
	parse_telegraf_cjson_output(compare_first_output);

	ret = fieldstat_dynamic_metric_value_incrby(instance, FIELD_TYPE_GAUGE, "Active_sessions", 12, NULL, 0, 0);
	EXPECT_EQ(0, ret);
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(1);
	parse_telegraf_cjson_output(compare_second_output);
	fieldstat_dynamic_instance_free(instance);

}


TEST(FeildStatDynamicAPI, FieldStatDynamicOutputLineProtocolTypeCounter)
{
	int n_thread = 48;
	int ret = 0;
	struct fieldstat_dynamic_instance *instance = NULL;

	const char *compare_first_output = "{\"fields\":{\"Active_sessions\":12},\"name\":\"Active_sessions\",\"tags\":{\"app_name\":\"firewall\"}}";
	const char *compare_second_output = "{\"fields\":{\"Active_sessions\":12},\"name\":\"Active_sessions\",\"tags\":{\"app_name\":\"firewall\"}}";

	instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	EXPECT_NE(nullptr, instance);

	ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);

	ret = fieldstat_dynamic_metric_value_incrby(instance, FIELD_TYPE_COUNTER, "Active_sessions", 12, NULL, 0, 0);
	EXPECT_EQ(0, ret);
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(1);
	parse_telegraf_cjson_output(compare_first_output);

	ret = fieldstat_dynamic_metric_value_incrby(instance, FIELD_TYPE_COUNTER, "Active_sessions", 12, NULL, 0, 0);
	EXPECT_EQ(0, ret);
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(1);
	parse_telegraf_cjson_output(compare_second_output);
	fieldstat_dynamic_instance_free(instance);
}

TEST(FeildStatDynamicAPI, FieldStatDynamicOutputLineProtocolTableMetricTypeAllCounter)
{
	int n_thread = 48;
	int ret = 0;
	struct fieldstat_dynamic_instance *instance = NULL;
	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];
	int table_id = -1;
	const char *compare_update_column_0 = "{\"fields\":{\"bytes\":0,\"packages\":30},\"name\":\"security_rule_hits\",\"tags\":{\"app_name\":\"firewall\",\"table_name\":\"shaping\"}}";
	const char *compare_update_column_1 = "{\"fields\":{\"bytes\":30,\"packages\":0},\"name\":\"security_rule_hits\",\"tags\":{\"app_name\":\"firewall\",\"table_name\":\"shaping\"}}";
	const char *compare_update_column_all = "{\"fields\":{\"bytes\":30,\"packages\":30},\"name\":\"security_rule_hits\",\"tags\":{\"app_name\":\"firewall\",\"table_name\":\"shaping\"}}";

	instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	EXPECT_NE(nullptr, instance);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);
	EXPECT_EQ(0, table_id);

	ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);

	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[0], "security_rule_hits", 30, NULL, 0, 0);
	EXPECT_EQ(0, ret);
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(1);
	parse_telegraf_cjson_output(compare_update_column_0);

	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[1], "security_rule_hits", 30, NULL, 0, 0);
	EXPECT_EQ(0, ret);
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(1);
	parse_telegraf_cjson_output(compare_update_column_1);

	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[0], "security_rule_hits", 30, NULL, 0, 0);
	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[1], "security_rule_hits", 30, NULL, 0, 0);
	EXPECT_EQ(0, ret);
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(1);
	parse_telegraf_cjson_output(compare_update_column_all);
	fieldstat_dynamic_instance_free(instance);
}


TEST(FeildStatDynamicAPI, FieldStatDynamicOutputLineProtocolTableMetricTypeAllGauge)
{
	int n_thread = 48;
	int ret = 0;
	struct fieldstat_dynamic_instance *instance = NULL;
	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_GAUGE, FIELD_TYPE_GAUGE};
	unsigned int out_column_ids[2];
	int table_id = -1;
	const char *compare_update_column_0 = "{\"fields\":{\"bytes\":0,\"packages\":30},\"name\":\"security_rule_hits\",\"tags\":{\"app_name\":\"firewall\",\"table_name\":\"shaping\"}}";
	const char *compare_update_column_1 = "{\"fields\":{\"bytes\":30,\"packages\":30},\"name\":\"security_rule_hits\",\"tags\":{\"app_name\":\"firewall\",\"table_name\":\"shaping\"}}";
	const char *compare_update_column_all = "{\"fields\":{\"bytes\":60,\"packages\":60},\"name\":\"security_rule_hits\",\"tags\":{\"app_name\":\"firewall\",\"table_name\":\"shaping\"}}";

	instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	EXPECT_NE(nullptr, instance);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);
	EXPECT_EQ(0, table_id);

	ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);

	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[0], "security_rule_hits", 30, NULL, 0, 0);
	EXPECT_EQ(0, ret);
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(1);
	parse_telegraf_cjson_output(compare_update_column_0);

	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[1], "security_rule_hits", 30, NULL, 0, 0);
	EXPECT_EQ(0, ret);
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(1);
	parse_telegraf_cjson_output(compare_update_column_1);

	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[0], "security_rule_hits", 30, NULL, 0, 0);
	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[1], "security_rule_hits", 30, NULL, 0, 0);
	EXPECT_EQ(0, ret);
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(1);
	parse_telegraf_cjson_output(compare_update_column_all);
	fieldstat_dynamic_instance_free(instance);
}

TEST(FeildStatDynamicAPI, FieldStatDynamicOutputLineProtocolTableMetricTypeOneCounterOneGauge)
{
	int n_thread = 48;
	int ret = 0;
	struct fieldstat_dynamic_instance *instance = NULL;
	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_GAUGE};
	unsigned int out_column_ids[2];
	int table_id = -1;
	const char *compare_update_column_0 = "{\"fields\":{\"bytes\":0,\"packages\":30},\"name\":\"security_rule_hits\",\"tags\":{\"app_name\":\"firewall\",\"table_name\":\"shaping\"}}";
	const char *compare_update_column_1 = "{\"fields\":{\"bytes\":30,\"packages\":0},\"name\":\"security_rule_hits\",\"tags\":{\"app_name\":\"firewall\",\"table_name\":\"shaping\"}}";
	const char *compare_update_column_all = "{\"fields\":{\"bytes\":60,\"packages\":30},\"name\":\"security_rule_hits\",\"tags\":{\"app_name\":\"firewall\",\"table_name\":\"shaping\"}}";

	instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	EXPECT_NE(nullptr, instance);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);
	EXPECT_EQ(0, table_id);

	ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);

	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[0], "security_rule_hits", 30, NULL, 0, 0);
	EXPECT_EQ(0, ret);
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(1);
	parse_telegraf_cjson_output(compare_update_column_0);

	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[1], "security_rule_hits", 30, NULL, 0, 0);
	EXPECT_EQ(0, ret);
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(1);
	parse_telegraf_cjson_output(compare_update_column_1);

	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[0], "security_rule_hits", 30, NULL, 0, 0);
	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[1], "security_rule_hits", 30, NULL, 0, 0);
	EXPECT_EQ(0, ret);
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(1);
	parse_telegraf_cjson_output(compare_update_column_all);
	fieldstat_dynamic_instance_free(instance);
}

void parse_telegraf_cjson_output_not_equal(const char *compare)
{
	const char *telegraf_output_file = "/tmp/metrics.out";
	FILE *fp;
	char line[1024] = {0};
	char *cjson_metric_str = NULL;

	fp = fopen(telegraf_output_file, "r");
	EXPECT_NE(nullptr, fp);

	while(!feof(fp))
	{
		fgets(line, sizeof(line), fp);
	}
	fclose(fp);

	if(strlen(line) == 0)
	{
		return;
	}

	cJSON *cjson_metric = NULL;
	cJSON *cjson_tags = NULL;

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
		EXPECT_STRNE(cjson_metric_str, compare);
		free(cjson_metric_str);
		cjson_metric_str = NULL;
	}
	cJSON_Delete(cjson_metric);
	return;
}


TEST(FeildStatDynamicAPI, FieldStatDynamicOutputLineProtocolValueZero)
{
	int n_thread = 48;
	int ret = 0;
	struct fieldstat_dynamic_instance *instance = NULL;

	const char *compare_output = "{\"fields\":{\"Active_sessions\":0},\"name\":\"Active_sessions\",\"tags\":{\"app_name\":\"firewall\"}}";

	instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	EXPECT_NE(nullptr, instance);

	ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);

	ret = fieldstat_dynamic_metric_value_incrby(instance, FIELD_TYPE_GAUGE, "Active_sessions", 0, NULL, 0, 0);
	EXPECT_EQ(0, ret);
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(1);
	parse_telegraf_cjson_output_not_equal(compare_output);
	fieldstat_dynamic_instance_free(instance);
}


TEST(FeildStatDynamicAPI, FieldStatDynamicOutputLineProtocolTableMetricValueZero)
{
	int n_thread = 48;
	int ret = 0;
	struct fieldstat_dynamic_instance *instance = NULL;
	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];
	int table_id = -1;
	const char *compare_column = "{\"fields\":{\"bytes\":0,\"packages\":0},\"name\":\"security_rule_hits\",\"tags\":{\"app_name\":\"firewall\",\"table_name\":\"shaping\"}}";


	instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	EXPECT_NE(nullptr, instance);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);
	EXPECT_EQ(0, table_id);

	ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);

	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[0], "security_rule_hits", 0, NULL, 0, 0);
	EXPECT_EQ(0, ret);
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(1);
	parse_telegraf_cjson_output_not_equal(compare_column);

	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[1], "security_rule_hits", 0, NULL, 0, 0);
	EXPECT_EQ(0, ret);
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(1);
	parse_telegraf_cjson_output_not_equal(compare_column);

	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[0], "security_rule_hits", 0, NULL, 0, 0);
	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[1], "security_rule_hits", 0, NULL, 0, 0);
	EXPECT_EQ(0, ret);
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(1);
	parse_telegraf_cjson_output_not_equal(compare_column);
	fieldstat_dynamic_instance_free(instance);
}


TEST(FeildStatDynamicAPI, FieldStatDynamicOutputLineProtocolMultiMetric)
{
	int n_thread = 48;
	int ret = 0;
	int loops = 5000;
	struct fieldstat_dynamic_instance *instance = NULL;
	char field_name[32];
	const char *telegraf_output_file = "/tmp/metrics.out";
	FILE *fp;
	char line[1024] = {0};
	cJSON *cjson_metric = NULL;
	cJSON *cjson_tags = NULL;
	char compare[1024];
	char *cjson_metric_str = NULL;

	instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	EXPECT_NE(nullptr, instance);
	ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);
	for(int i = 0; i < loops; i++)
	{
		memset(field_name, 0, sizeof(field_name));
		snprintf(field_name, sizeof(field_name), "Active_sessions_%d", i);
		ret = fieldstat_dynamic_metric_value_incrby(instance, FIELD_TYPE_GAUGE, (const char *)field_name, 12, NULL, 0, 0);
		EXPECT_EQ(0, ret);
	}
	ret = system("cat /dev/null > /tmp/metrics.out");
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(5);

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
		memset(compare, 0, sizeof(compare));
		snprintf(compare, sizeof(compare), "{\"fields\":{\"Active_sessions_%d\":12},\"name\":\"Active_sessions_%d\",\"tags\":{\"app_name\":\"firewall\"}}", n_line, n_line);
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
	EXPECT_EQ(n_line, loops);
	fieldstat_dynamic_instance_free(instance);
}


TEST(FeildStatDynamicAPI, FieldStatDynamicOutputLineProtocolMultiTableRow)
{
	int n_thread = 48;
	int ret = 0;
	int loops = 5000;
	struct fieldstat_dynamic_instance *instance = NULL;
	char row_name[32];
	const char *telegraf_output_file = "/tmp/metrics.out";
	FILE *fp;
	char line[1024] = {0};
	cJSON *cjson_metric = NULL;
	cJSON *cjson_tags = NULL;
	char compare[1024];
	char *cjson_metric_str = NULL;


	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];
	int table_id = -1;

	instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	EXPECT_NE(nullptr, instance);
	ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);
	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);
	EXPECT_EQ(0, table_id);
	for(int i = 0; i < loops; i++)
	{
		memset(row_name, 0, sizeof(row_name));
		snprintf(row_name, sizeof(row_name), "security_rule_hits_%d", i);
		ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[0], row_name, 30, NULL, 0, 0);
		ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[1], row_name, 30, NULL, 0, 0);
		EXPECT_EQ(0, ret);
	}
	ret = system("cat /dev/null > /tmp/metrics.out");
	ret = line_protocol_dynamic_metric_output(instance);
	EXPECT_EQ(0, ret);
	sleep(5);

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
		memset(compare, 0, sizeof(compare));

		snprintf(compare, sizeof(compare), "{\"fields\":{\"bytes\":30,\"packages\":30},\"name\":\"security_rule_hits_%d\",\"tags\":{\"app_name\":\"firewall\",\"table_name\":\"shaping\"}}", n_line);
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
	EXPECT_EQ(n_line, loops);

	fieldstat_dynamic_instance_free(instance);
}



struct thread_para
{
	int loops;
	struct fieldstat_dynamic_instance * instance;
	int thread_id;
	int table_id;
	unsigned int *out_column_ids;
};

void _worker_thread_multi_incrby(void *arg)
{
	
	struct thread_para *para = (struct thread_para*)arg;
	int loops = para->loops;
	int thread_id = para->thread_id;
	int table_id = para->table_id;
	unsigned int *out_column_ids = para->out_column_ids;

	struct fieldstat_dynamic_instance *instance = para->instance;
	int ret = 0;
	struct fieldstat_tag tags[3];

	int i = 0;

	tags[0].key = "policy_id";
	tags[0].value_int = 1;
	tags[0].value_type = 0;

	tags[1].key = "quanlity";
	tags[1].value_double = 0.50;
	tags[1].value_type = 1;

	tags[2].key = "device_id";
	tags[2].value_str = "test_device";
	tags[2].value_type = 2;

	for(i = 0; i < loops; i++)
	{
		ret = fieldstat_dynamic_metric_value_incrby(instance, FIELD_TYPE_GAUGE, "Active_sessions", 10, tags, sizeof(tags)/sizeof(tags[0]), thread_id); 
		EXPECT_EQ(0, ret);
		ret = fieldstat_dynamic_metric_value_incrby(instance, FIELD_TYPE_COUNTER, "Traffic_bytes", 20, tags, sizeof(tags)/sizeof(tags[0]), thread_id); 
		EXPECT_EQ(0, ret);
	
		ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[0], "security_rule_hits", 30, tags, sizeof(tags)/sizeof(tags[0]), thread_id);
		EXPECT_EQ(0, ret);
		ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[1], "security_rule_hits", 31, tags, sizeof(tags)/sizeof(tags[0]), thread_id);
		EXPECT_EQ(0, ret);
	}
	return;
}
void * worker_thread_multi_incrby(void *arg)
{
	_worker_thread_multi_incrby(arg);
	return NULL;
}


TEST(FeildStatDynamicAPI, FieldStatDynamicInstanceMultiIncrby)
{
	int ret = 0;
	int n_thread = 64;
	int n_loops = 50000;

	struct dynamic_metric *dyn_metric, *tmp_dyn_metric;
	struct dynamic_metric **head = NULL;
	struct metric **metrics = NULL;
	struct metric *metric = NULL;
	long long value = 0;

	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];
	int table_id = -1;

	struct thread_para para[n_thread];
	pthread_t thread_ids[n_thread];


	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", n_thread);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);

	EXPECT_EQ(0, table_id);	

	for(int i = 0; i < n_thread; i++)
	{
		para[i].loops = n_loops;
		para[i].instance = instance;
		para[i].thread_id = i;
		para[i].table_id = table_id;
		para[i].out_column_ids = out_column_ids;
	}

	for(int i = 0; i < n_thread; i++)
	{
		ret = pthread_create(&(thread_ids[i]), NULL, worker_thread_multi_incrby, &(para[i]));
		EXPECT_EQ(0, ret);
	}

	void *temp;
	for(int i = 0; i < n_thread; i++)
	{
		pthread_join(thread_ids[i], (void**)&temp);
	}

	for(int i = 0; i < n_thread; i++)
	{
		int n_metrics = 0;
		head = &instance->n_thread_dynamic_metric[i];
		HASH_ITER(hh, *head, dyn_metric, tmp_dyn_metric)
		{
			metrics = dyn_metric->metrics;
			metric  = metrics[0];
			if(metric->table)
			{
				metric = metrics[0];
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
				EXPECT_EQ((30 * n_loops), value);
				metric = metrics[1];
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
				EXPECT_EQ((31 * n_loops), value);
			}
			else
			{
				switch(metric->field_type)
				{
					case FIELD_TYPE_GAUGE:
						value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
						EXPECT_EQ((10 * n_loops), value);
						break;
					case FIELD_TYPE_COUNTER:
						value = get_metric_unit_val(metric, FS_CALC_CURRENT, 0);
						EXPECT_EQ((20 * n_loops), value);
						break;
					default:
						FAIL() << "Field type should be in {counter, gauge}";
				}
			}
			n_metrics++;
		}
		EXPECT_EQ(3, n_metrics);
	}
	fieldstat_dynamic_instance_free(instance);
}


void _worker_thread_multi_decrby(void *arg)
{
	
	struct thread_para *para = (struct thread_para*)arg;
	int loops = para->loops;
	int thread_id = para->thread_id;
	int table_id = para->table_id;
	unsigned int *out_column_ids = para->out_column_ids;

	struct fieldstat_dynamic_instance *instance = para->instance;
	int ret = 0;
	struct fieldstat_tag tags[3];

	int i = 0;

	tags[0].key = "policy_id";
	tags[0].value_int = 1;
	tags[0].value_type = 0;

	tags[1].key = "quanlity";
	tags[1].value_double = 0.50;
	tags[1].value_type = 1;

	tags[2].key = "device_id";
	tags[2].value_str = "test_device";
	tags[2].value_type = 2;

	for(i = 0; i < loops; i++)
	{
		ret = fieldstat_dynamic_metric_value_decrby(instance, FIELD_TYPE_GAUGE, "Active_sessions", 10, tags, sizeof(tags)/sizeof(tags[0]), thread_id); 
		EXPECT_EQ(0, ret);
		ret = fieldstat_dynamic_metric_value_decrby(instance, FIELD_TYPE_COUNTER, "Traffic_bytes", 20, tags, sizeof(tags)/sizeof(tags[0]), thread_id); 
		EXPECT_EQ(0, ret);
	
		ret = fieldstat_dynamic_table_metric_value_decrby(instance, table_id, out_column_ids[0], "security_rule_hits", 30, tags, sizeof(tags)/sizeof(tags[0]), thread_id);
		EXPECT_EQ(0, ret);
		ret = fieldstat_dynamic_table_metric_value_decrby(instance, table_id, out_column_ids[1], "security_rule_hits", 31, tags, sizeof(tags)/sizeof(tags[0]), thread_id);
		EXPECT_EQ(0, ret);
	}
	return;
}
void * worker_thread_multi_decrby(void *arg)
{
	_worker_thread_multi_decrby(arg);
	return NULL;
}


TEST(FeildStatDynamicAPI, FieldStatDynamicInstanceMultiDecrby)
{
	int ret = 0;
	int n_thread = 64;
	int n_loops = 50000;

	struct dynamic_metric *dyn_metric, *tmp_dyn_metric;
	struct dynamic_metric **head = NULL;
	struct metric **metrics = NULL;
	struct metric *metric = NULL;
	long long value = 0;

	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];
	int table_id = -1;

	struct thread_para para[n_thread];
	pthread_t thread_ids[n_thread];


	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", n_thread);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);

	EXPECT_EQ(0, table_id);	

	for(int i = 0; i < n_thread; i++)
	{
		para[i].loops = n_loops;
		para[i].instance = instance;
		para[i].thread_id = i;
		para[i].table_id = table_id;
		para[i].out_column_ids = out_column_ids;
	}

	for(int i = 0; i < n_thread; i++)
	{
		ret = pthread_create(&(thread_ids[i]), NULL, worker_thread_multi_decrby, &(para[i]));
		EXPECT_EQ(0, ret);
	}

	void *temp;
	for(int i = 0; i < n_thread; i++)
	{
		pthread_join(thread_ids[i], (void**)&temp);
	}

	for(int i = 0; i < n_thread; i++)
	{
		int n_metrics = 0;
		head = &instance->n_thread_dynamic_metric[i];
		HASH_ITER(hh, *head, dyn_metric, tmp_dyn_metric)
		{
			metrics = dyn_metric->metrics;
			metric  = metrics[0];
			if(metric->table)
			{
				metric = metrics[0];
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
				EXPECT_EQ((-30 * n_loops), value);
				metric = metrics[1];
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
				EXPECT_EQ((-31 * n_loops), value);
			}
			else
			{
				switch(metric->field_type)
				{
					case FIELD_TYPE_GAUGE:
						value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
						EXPECT_EQ((-10 * n_loops), value);
						break;
					case FIELD_TYPE_COUNTER:
						value = get_metric_unit_val(metric, FS_CALC_CURRENT, 0);
						EXPECT_EQ((-20 * n_loops), value);
						break;
					default:
						FAIL() << "Field type should be in {counter, gauge}";
				}
			}
			n_metrics++;
		}
		EXPECT_EQ(3, n_metrics);
	}
	fieldstat_dynamic_instance_free(instance);
}


void _worker_thread_multi_set(void *arg)
{
	
	struct thread_para *para = (struct thread_para*)arg;
	int loops = para->loops;
	int thread_id = para->thread_id;
	int table_id = para->table_id;
	unsigned int *out_column_ids = para->out_column_ids;

	struct fieldstat_dynamic_instance *instance = para->instance;
	int ret = 0;
	struct fieldstat_tag tags[3];

	int i = 0;

	tags[0].key = "policy_id";
	tags[0].value_int = 1;
	tags[0].value_type = 0;

	tags[1].key = "quanlity";
	tags[1].value_double = 0.50;
	tags[1].value_type = 1;

	tags[2].key = "device_id";
	tags[2].value_str = "test_device";
	tags[2].value_type = 2;

	for(i = 0; i < loops; i++)
	{
		ret = fieldstat_dynamic_metric_value_set(instance, FIELD_TYPE_GAUGE, "Active_sessions", 10, tags, sizeof(tags)/sizeof(tags[0]), thread_id); 
		EXPECT_EQ(0, ret);
		ret = fieldstat_dynamic_metric_value_set(instance, FIELD_TYPE_COUNTER, "Traffic_bytes", 20, tags, sizeof(tags)/sizeof(tags[0]), thread_id); 
		EXPECT_EQ(0, ret);
	
		ret = fieldstat_dynamic_table_metric_value_set(instance, table_id, out_column_ids[0], "security_rule_hits", 30, tags, sizeof(tags)/sizeof(tags[0]), thread_id);
		EXPECT_EQ(0, ret);
		ret = fieldstat_dynamic_table_metric_value_set(instance, table_id, out_column_ids[1], "security_rule_hits", 31, tags, sizeof(tags)/sizeof(tags[0]), thread_id);
		EXPECT_EQ(0, ret);
	}
	return;
}
void * worker_thread_multi_set(void *arg)
{
	_worker_thread_multi_set(arg);
	return NULL;
}


TEST(FeildStatDynamicAPI, FieldStatDynamicInstanceMultiSet)
{
	int ret = 0;
	int n_thread = 64;
	int n_loops = 50000;

	struct dynamic_metric *dyn_metric, *tmp_dyn_metric;
	struct dynamic_metric **head = NULL;
	struct metric **metrics = NULL;
	struct metric *metric = NULL;
	long long value = 0;

	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];
	int table_id = -1;

	struct thread_para para[n_thread];
	pthread_t thread_ids[n_thread];


	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", n_thread);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);

	EXPECT_EQ(0, table_id);	

	for(int i = 0; i < n_thread; i++)
	{
		para[i].loops = n_loops;
		para[i].instance = instance;
		para[i].thread_id = i;
		para[i].table_id = table_id;
		para[i].out_column_ids = out_column_ids;
	}

	for(int i = 0; i < n_thread; i++)
	{
		ret = pthread_create(&(thread_ids[i]), NULL, worker_thread_multi_set, &(para[i]));
		EXPECT_EQ(0, ret);
	}

	void *temp;
	for(int i = 0; i < n_thread; i++)
	{
		pthread_join(thread_ids[i], (void**)&temp);
	}

	for(int i = 0; i < n_thread; i++)
	{
		int n_metrics = 0;
		head = &instance->n_thread_dynamic_metric[i];
		HASH_ITER(hh, *head, dyn_metric, tmp_dyn_metric)
		{
			metrics = dyn_metric->metrics;
			metric  = metrics[0];
			if(metric->table)
			{
				metric = metrics[0];
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
				EXPECT_EQ(30, value);
				metric = metrics[1];
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
				EXPECT_EQ(31, value);
			}
			else
			{
				switch(metric->field_type)
				{
					case FIELD_TYPE_GAUGE:
						value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
						EXPECT_EQ(10, value);
						break;
					case FIELD_TYPE_COUNTER:
						value = get_metric_unit_val(metric, FS_CALC_CURRENT, 0);
						EXPECT_EQ(20, value);
						break;
					default:
						FAIL() << "Field type should be in {counter, gauge}";
				}
			}
			n_metrics++;
		}
		EXPECT_EQ(3, n_metrics);
	}
	fieldstat_dynamic_instance_free(instance);
}


void _worker_thread_incrby_multi_metric(void *arg)
{
	
	struct thread_para *para = (struct thread_para*)arg;
	int loops = para->loops;
	int thread_id = para->thread_id;
	int table_id = para->table_id;
	unsigned int *out_column_ids = para->out_column_ids;

	struct fieldstat_dynamic_instance *instance = para->instance;
	int ret = 0;
	struct fieldstat_tag tags[3];

	int i = 0;

	tags[0].key = "policy_id";
	tags[0].value_int = 1;
	tags[0].value_type = 0;

	tags[1].key = "quanlity";
	tags[1].value_double = 0.50;
	tags[1].value_type = 1;

	tags[2].key = "device_id";
	tags[2].value_str = "test_device";
	tags[2].value_type = 2;

	for(i = 0; i < loops; i++)
	{
		char gauge_name[32];
		char counter_name[32];
		char table_column_name[32];
		snprintf(gauge_name, sizeof(gauge_name), "Active_sessions_%d", i);
		snprintf(counter_name, sizeof(gauge_name), "Traffic_bytes_%d", i);
		snprintf(table_column_name, sizeof(gauge_name), "security_rule_hits_%d", i);
		ret = fieldstat_dynamic_metric_value_incrby(instance, FIELD_TYPE_GAUGE, gauge_name, 10, tags, sizeof(tags)/sizeof(tags[0]), thread_id); 
		EXPECT_EQ(0, ret);
		ret = fieldstat_dynamic_metric_value_incrby(instance, FIELD_TYPE_COUNTER, counter_name, 20, tags, sizeof(tags)/sizeof(tags[0]), thread_id); 
		EXPECT_EQ(0, ret);
	
		ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[0], table_column_name, 30, tags, sizeof(tags)/sizeof(tags[0]), thread_id);
		EXPECT_EQ(0, ret);
		ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[1], table_column_name, 31, tags, sizeof(tags)/sizeof(tags[0]), thread_id);
		EXPECT_EQ(0, ret);
	}
}
void * worker_thread_incrby_multi_metric(void *arg)
{
	_worker_thread_incrby_multi_metric(arg);
	return NULL;
}


TEST(FeildStatDynamicAPI, FieldStatDynamicInstanceIncrbyMultiMetric)
{
	int ret = 0;
	int n_thread = 64;
	int n_loops = 100;

	struct dynamic_metric *dyn_metric, *tmp_dyn_metric;
	struct dynamic_metric **head = NULL;
	struct metric **metrics = NULL;
	struct metric *metric = NULL;
	long long value = 0;

	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];
	int table_id = -1;

	struct thread_para para[n_thread];
	pthread_t thread_ids[n_thread];


	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", n_thread);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);

	EXPECT_EQ(0, table_id);	

	for(int i = 0; i < n_thread; i++)
	{
		para[i].loops = n_loops;
		para[i].instance = instance;
		para[i].thread_id = i;
		para[i].table_id = table_id;
		para[i].out_column_ids = out_column_ids;
	}

	for(int i = 0; i < n_thread; i++)
	{
		ret = pthread_create(&(thread_ids[i]), NULL, worker_thread_incrby_multi_metric, &(para[i]));
		EXPECT_EQ(0, ret);
	}

	void *temp;
	for(int i = 0; i < n_thread; i++)
	{
		pthread_join(thread_ids[i], (void**)&temp);
	}

	for(int i = 0; i < n_thread; i++)
	{
		int n_metrics = 0;
		head = &instance->n_thread_dynamic_metric[i];
		HASH_ITER(hh, *head, dyn_metric, tmp_dyn_metric)
		{
			metrics = dyn_metric->metrics;
			metric  = metrics[0];
			if(metric->table)
			{
				metric = metrics[0];
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
				EXPECT_EQ(30, value);
				metric = metrics[1];
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
				EXPECT_EQ(31, value);
			}
			else
			{
				switch(metric->field_type)
				{
					case FIELD_TYPE_GAUGE:
						value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
						EXPECT_EQ(10, value);
						break;
					case FIELD_TYPE_COUNTER:
						value = get_metric_unit_val(metric, FS_CALC_CURRENT, 0);
						EXPECT_EQ(20, value);
						break;
					default:
						FAIL() << "Field type should be in {counter, gauge}";
				}
			}
			n_metrics++;
		}
		EXPECT_EQ((3 * n_loops), n_metrics);
	}
	fieldstat_dynamic_instance_free(instance);
}


void _worker_thread_decrby_multi_metric(void *arg)
{
	
	struct thread_para *para = (struct thread_para*)arg;
	int loops = para->loops;
	int thread_id = para->thread_id;
	int table_id = para->table_id;
	unsigned int *out_column_ids = para->out_column_ids;

	struct fieldstat_dynamic_instance *instance = para->instance;
	int ret = 0;
	struct fieldstat_tag tags[3];

	int i = 0;

	tags[0].key = "policy_id";
	tags[0].value_int = 1;
	tags[0].value_type = 0;

	tags[1].key = "quanlity";
	tags[1].value_double = 0.50;
	tags[1].value_type = 1;

	tags[2].key = "device_id";
	tags[2].value_str = "test_device";
	tags[2].value_type = 2;

	for(i = 0; i < loops; i++)
	{
		char gauge_name[32];
		char counter_name[32];
		char table_column_name[32];
		snprintf(gauge_name, sizeof(gauge_name), "Active_sessions_%d", i);
		snprintf(counter_name, sizeof(gauge_name), "Traffic_bytes_%d", i);
		snprintf(table_column_name, sizeof(gauge_name), "security_rule_hits_%d", i);
		ret = fieldstat_dynamic_metric_value_decrby(instance, FIELD_TYPE_GAUGE, gauge_name, 10, tags, sizeof(tags)/sizeof(tags[0]), thread_id); 
		EXPECT_EQ(0, ret);
		ret = fieldstat_dynamic_metric_value_decrby(instance, FIELD_TYPE_COUNTER, counter_name, 20, tags, sizeof(tags)/sizeof(tags[0]), thread_id); 
		EXPECT_EQ(0, ret);
	
		ret = fieldstat_dynamic_table_metric_value_decrby(instance, table_id, out_column_ids[0], table_column_name, 30, tags, sizeof(tags)/sizeof(tags[0]), thread_id);
		EXPECT_EQ(0, ret);
		ret = fieldstat_dynamic_table_metric_value_decrby(instance, table_id, out_column_ids[1], table_column_name, 31, tags, sizeof(tags)/sizeof(tags[0]), thread_id);
		EXPECT_EQ(0, ret);
	}
	return;
}
void * worker_thread_decrby_multi_metric(void *arg)
{
	_worker_thread_decrby_multi_metric(arg);
	return NULL;
}


TEST(FeildStatDynamicAPI, FieldStatDynamicInstanceDecrbyMultiMetric)
{
	int ret = 0;
	int n_thread = 64;
	int n_loops = 100;

	struct dynamic_metric *dyn_metric, *tmp_dyn_metric;
	struct dynamic_metric **head = NULL;
	struct metric **metrics = NULL;
	struct metric *metric = NULL;
	long long value = 0;

	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];
	int table_id = -1;

	struct thread_para para[n_thread];
	pthread_t thread_ids[n_thread];


	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", n_thread);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);

	EXPECT_EQ(0, table_id);	

	for(int i = 0; i < n_thread; i++)
	{
		para[i].loops = n_loops;
		para[i].instance = instance;
		para[i].thread_id = i;
		para[i].table_id = table_id;
		para[i].out_column_ids = out_column_ids;
	}

	for(int i = 0; i < n_thread; i++)
	{
		ret = pthread_create(&(thread_ids[i]), NULL, worker_thread_decrby_multi_metric, &(para[i]));
		EXPECT_EQ(0, ret);
	}

	void *temp;
	for(int i = 0; i < n_thread; i++)
	{
		pthread_join(thread_ids[i], (void**)&temp);
	}

	for(int i = 0; i < n_thread; i++)
	{
		int n_metrics = 0;
		head = &instance->n_thread_dynamic_metric[i];
		HASH_ITER(hh, *head, dyn_metric, tmp_dyn_metric)
		{
			metrics = dyn_metric->metrics;
			metric  = metrics[0];
			if(metric->table)
			{
				metric = metrics[0];
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
				EXPECT_EQ(-30, value);
				metric = metrics[1];
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
				EXPECT_EQ(-31, value);
			}
			else
			{
				switch(metric->field_type)
				{
					case FIELD_TYPE_GAUGE:
						value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
						EXPECT_EQ(-10, value);
						break;
					case FIELD_TYPE_COUNTER:
						value = get_metric_unit_val(metric, FS_CALC_CURRENT, 0);
						EXPECT_EQ(-20, value);
						break;
					default:
						FAIL() << "Field type should be in {counter, gauge}";
				}
			}
			n_metrics++;
		}
		EXPECT_EQ((3 * n_loops), n_metrics);
	}
	fieldstat_dynamic_instance_free(instance);
}



void _worker_thread_set_multi_metric(void *arg)
{
	
	struct thread_para *para = (struct thread_para*)arg;
	int loops = para->loops;
	int thread_id = para->thread_id;
	int table_id = para->table_id;
	unsigned int *out_column_ids = para->out_column_ids;

	struct fieldstat_dynamic_instance *instance = para->instance;
	int ret = 0;
	struct fieldstat_tag tags[3];

	int i = 0;

	tags[0].key = "policy_id";
	tags[0].value_int = 1;
	tags[0].value_type = 0;

	tags[1].key = "quanlity";
	tags[1].value_double = 0.50;
	tags[1].value_type = 1;

	tags[2].key = "device_id";
	tags[2].value_str = "test_device";
	tags[2].value_type = 2;

	for(i = 0; i < loops; i++)
	{
		char gauge_name[32];
		char counter_name[32];
		char table_column_name[32];
		snprintf(gauge_name, sizeof(gauge_name), "Active_sessions_%d", i);
		snprintf(counter_name, sizeof(gauge_name), "Traffic_bytes_%d", i);
		snprintf(table_column_name, sizeof(gauge_name), "security_rule_hits_%d", i);
		ret = fieldstat_dynamic_metric_value_set(instance, FIELD_TYPE_GAUGE, gauge_name, 10, tags, sizeof(tags)/sizeof(tags[0]), thread_id); 
		EXPECT_EQ(0, ret);
		ret = fieldstat_dynamic_metric_value_set(instance, FIELD_TYPE_COUNTER, counter_name, 20, tags, sizeof(tags)/sizeof(tags[0]), thread_id); 
		EXPECT_EQ(0, ret);
	
		ret = fieldstat_dynamic_table_metric_value_set(instance, table_id, out_column_ids[0], table_column_name, 30, tags, sizeof(tags)/sizeof(tags[0]), thread_id);
		EXPECT_EQ(0, ret);
		ret = fieldstat_dynamic_table_metric_value_set(instance, table_id, out_column_ids[1], table_column_name, 31, tags, sizeof(tags)/sizeof(tags[0]), thread_id);
		EXPECT_EQ(0, ret);
	}
	return;
}
void * worker_thread_set_multi_metric(void *arg)
{
	_worker_thread_set_multi_metric(arg);
	return NULL;
}


TEST(FeildStatDynamicAPI, FieldStatDynamicInstanceSetMultiMetric)
{
	int ret = 0;
	int n_thread = 64;
	int n_loops = 100;

	struct dynamic_metric *dyn_metric, *tmp_dyn_metric;
	struct dynamic_metric **head = NULL;
	struct metric **metrics = NULL;
	struct metric *metric = NULL;
	long long value = 0;

	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];
	int table_id = -1;

	struct thread_para para[n_thread];
	pthread_t thread_ids[n_thread];


	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", n_thread);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);

	EXPECT_EQ(0, table_id);	

	for(int i = 0; i < n_thread; i++)
	{
		para[i].loops = n_loops;
		para[i].instance = instance;
		para[i].thread_id = i;
		para[i].table_id = table_id;
		para[i].out_column_ids = out_column_ids;
	}

	for(int i = 0; i < n_thread; i++)
	{
		ret = pthread_create(&(thread_ids[i]), NULL, worker_thread_set_multi_metric, &(para[i]));
		EXPECT_EQ(0, ret);
	}

	void *temp;
	for(int i = 0; i < n_thread; i++)
	{
		pthread_join(thread_ids[i], (void**)&temp);
	}

	for(int i = 0; i < n_thread; i++)
	{
		int n_metrics = 0;
		head = &instance->n_thread_dynamic_metric[i];
		HASH_ITER(hh, *head, dyn_metric, tmp_dyn_metric)
		{
			metrics = dyn_metric->metrics;
			metric  = metrics[0];
			if(metric->table)
			{
				metric = metrics[0];
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
				EXPECT_EQ(30, value);
				metric = metrics[1];
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
				EXPECT_EQ(31, value);
			}
			else
			{
				switch(metric->field_type)
				{
					case FIELD_TYPE_GAUGE:
						value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
						EXPECT_EQ(10, value);
						break;
					case FIELD_TYPE_COUNTER:
						value = get_metric_unit_val(metric, FS_CALC_CURRENT, 0);
						EXPECT_EQ(20, value);
						break;
					default:
						FAIL() << "Field type should be in {counter, gauge}";
				}
			}
			n_metrics++;
		}
		EXPECT_EQ((3 * n_loops), n_metrics);
	}
	fieldstat_dynamic_instance_free(instance);
}





void _worker_thread_multi_incrby_multi_metric(void *arg)
{
	struct thread_para *para = (struct thread_para*)arg;
	int loops = para->loops;
	int thread_id = para->thread_id;
	int table_id = para->table_id;
	unsigned int *out_column_ids = para->out_column_ids;

	struct fieldstat_dynamic_instance *instance = para->instance;
	int ret = 0;
	struct fieldstat_tag tags[3];

	int i = 0;

	tags[0].key = "policy_id";
	tags[0].value_int = 1;
	tags[0].value_type = 0;

	tags[1].key = "quanlity";
	tags[1].value_double = 0.50;
	tags[1].value_type = 1;

	tags[2].key = "device_id";
	tags[2].value_str = "test_device";
	tags[2].value_type = 2;

	for(i = 0; i < loops; i++)
	{
		char gauge_name[32];
		char counter_name[32];
		char table_column_name[32];
		snprintf(gauge_name, sizeof(gauge_name), "Active_sessions_%d", i);
		snprintf(counter_name, sizeof(gauge_name), "Traffic_bytes_%d", i);
		snprintf(table_column_name, sizeof(gauge_name), "security_rule_hits_%d", i);

		for(int j = 0; j < loops; j++)
		{
			ret = fieldstat_dynamic_metric_value_incrby(instance, FIELD_TYPE_GAUGE, (const char *)gauge_name, 10, tags, sizeof(tags)/sizeof(tags[0]), thread_id); 
			EXPECT_EQ(0, ret);
			ret = fieldstat_dynamic_metric_value_incrby(instance, FIELD_TYPE_COUNTER, (const char *)counter_name, 20, tags, sizeof(tags)/sizeof(tags[0]), thread_id); 
			EXPECT_EQ(0, ret);
		
			ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[0], (const char *)table_column_name, 30, tags, sizeof(tags)/sizeof(tags[0]), thread_id);
			EXPECT_EQ(0, ret);
			ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[1], (const char *)table_column_name, 31, tags, sizeof(tags)/sizeof(tags[0]), thread_id);
			EXPECT_EQ(0, ret);
		}
	}
	return;
}
void * worker_thread_multi_incrby_multi_metric(void *arg)
{
	_worker_thread_multi_incrby_multi_metric(arg);
	return NULL;
}


TEST(FeildStatDynamicAPI, FieldStatDynamicInstanceMultiIncrbyMultiMetric)
{
	int ret = 0;
	int n_thread = 64;
	int n_loops = 100;

	struct dynamic_metric *dyn_metric, *tmp_dyn_metric;
	struct dynamic_metric **head = NULL;
	struct metric **metrics = NULL;
	struct metric *metric = NULL;
	long long value = 0;

	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];
	int table_id = -1;

	struct thread_para para[n_thread];
	pthread_t thread_ids[n_thread];


	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", n_thread);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);

	EXPECT_EQ(0, table_id);	

	for(int i = 0; i < n_thread; i++)
	{
		para[i].loops = n_loops;
		para[i].instance = instance;
		para[i].thread_id = i;
		para[i].table_id = table_id;
		para[i].out_column_ids = out_column_ids;
	}

	for(int i = 0; i < n_thread; i++)
	{
		ret = pthread_create(&(thread_ids[i]), NULL, worker_thread_multi_incrby_multi_metric, &(para[i]));
		EXPECT_EQ(0, ret);
	}

	void *temp;
	for(int i = 0; i < n_thread; i++)
	{
		pthread_join(thread_ids[i], (void**)&temp);
	}

	for(int i = 0; i < n_thread; i++)
	{
		int n_metrics = 0;
		head = &instance->n_thread_dynamic_metric[i];
		HASH_ITER(hh, *head, dyn_metric, tmp_dyn_metric)
		{
			metrics = dyn_metric->metrics;
			metric  = metrics[0];
			if(metric->table)
			{
				metric = metrics[0];
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
				EXPECT_EQ((30 * n_loops), value);
				metric = metrics[1];
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
				EXPECT_EQ((31 * n_loops), value);
			}
			else
			{
				switch(metric->field_type)
				{
					case FIELD_TYPE_GAUGE:
						value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
						EXPECT_EQ((10 * n_loops), value);
						break;
					case FIELD_TYPE_COUNTER:
						value = get_metric_unit_val(metric, FS_CALC_CURRENT, 0);
						EXPECT_EQ((20 * n_loops), value);
						break;
					default:
						FAIL() << "Field type should be in {counter, gauge}";
				}
			}
			n_metrics++;
		}
		EXPECT_EQ((3 * n_loops), n_metrics);
	}
	fieldstat_dynamic_instance_free(instance);
}


void _worker_thread_multi_decrby_multi_metric(void *arg)
{
	struct thread_para *para = (struct thread_para*)arg;
	int loops = para->loops;
	int thread_id = para->thread_id;
	int table_id = para->table_id;
	unsigned int *out_column_ids = para->out_column_ids;

	struct fieldstat_dynamic_instance *instance = para->instance;
	int ret = 0;
	struct fieldstat_tag tags[3];

	int i = 0;

	tags[0].key = "policy_id";
	tags[0].value_int = 1;
	tags[0].value_type = 0;

	tags[1].key = "quanlity";
	tags[1].value_double = 0.50;
	tags[1].value_type = 1;

	tags[2].key = "device_id";
	tags[2].value_str = "test_device";
	tags[2].value_type = 2;

	for(i = 0; i < loops; i++)
	{
		char gauge_name[32];
		char counter_name[32];
		char table_column_name[32];
		snprintf(gauge_name, sizeof(gauge_name), "Active_sessions_%d", i);
		snprintf(counter_name, sizeof(gauge_name), "Traffic_bytes_%d", i);
		snprintf(table_column_name, sizeof(gauge_name), "security_rule_hits_%d", i);

		for(int j = 0; j < loops; j++)
		{
			ret = fieldstat_dynamic_metric_value_decrby(instance, FIELD_TYPE_GAUGE, (const char *)gauge_name, 10, tags, sizeof(tags)/sizeof(tags[0]), thread_id); 
			EXPECT_EQ(0, ret);
			ret = fieldstat_dynamic_metric_value_decrby(instance, FIELD_TYPE_COUNTER, (const char *)counter_name, 20, tags, sizeof(tags)/sizeof(tags[0]), thread_id); 
			EXPECT_EQ(0, ret);
		
			ret = fieldstat_dynamic_table_metric_value_decrby(instance, table_id, out_column_ids[0], (const char *)table_column_name, 30, tags, sizeof(tags)/sizeof(tags[0]), thread_id);
			EXPECT_EQ(0, ret);
			ret = fieldstat_dynamic_table_metric_value_decrby(instance, table_id, out_column_ids[1], (const char *)table_column_name, 31, tags, sizeof(tags)/sizeof(tags[0]), thread_id);
			EXPECT_EQ(0, ret);
		}
	}
	return;
}
void * worker_thread_multi_decrby_multi_metric(void *arg)
{
	_worker_thread_multi_decrby_multi_metric(arg);
	return NULL;
}


TEST(FeildStatDynamicAPI, FieldStatDynamicInstanceMultiDecrbyMultiMetric)
{
	int ret = 0;
	int n_thread = 64;
	int n_loops = 100;

	struct dynamic_metric *dyn_metric, *tmp_dyn_metric;
	struct dynamic_metric **head = NULL;
	struct metric **metrics = NULL;
	struct metric *metric = NULL;
	long long value = 0;

	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];
	int table_id = -1;

	struct thread_para para[n_thread];
	pthread_t thread_ids[n_thread];


	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", n_thread);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);

	EXPECT_EQ(0, table_id);	

	for(int i = 0; i < n_thread; i++)
	{
		para[i].loops = n_loops;
		para[i].instance = instance;
		para[i].thread_id = i;
		para[i].table_id = table_id;
		para[i].out_column_ids = out_column_ids;
	}

	for(int i = 0; i < n_thread; i++)
	{
		ret = pthread_create(&(thread_ids[i]), NULL, worker_thread_multi_decrby_multi_metric, &(para[i]));
		EXPECT_EQ(0, ret);
	}

	void *temp;
	for(int i = 0; i < n_thread; i++)
	{
		pthread_join(thread_ids[i], (void**)&temp);
	}

	for(int i = 0; i < n_thread; i++)
	{
		int n_metrics = 0;
		head = &instance->n_thread_dynamic_metric[i];
		HASH_ITER(hh, *head, dyn_metric, tmp_dyn_metric)
		{
			metrics = dyn_metric->metrics;
			metric  = metrics[0];
			if(metric->table)
			{
				metric = metrics[0];
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
				EXPECT_EQ((-1 * 30 * n_loops), value);
				metric = metrics[1];
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
				EXPECT_EQ((-1 * 31 * n_loops), value);
			}
			else
			{
				switch(metric->field_type)
				{
					case FIELD_TYPE_GAUGE:
						value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
						EXPECT_EQ((-1 * 10 * n_loops), value);
						break;
					case FIELD_TYPE_COUNTER:
						value = get_metric_unit_val(metric, FS_CALC_CURRENT, 0);
						EXPECT_EQ((-1 * 20 * n_loops), value);
						break;
					default:
						FAIL() << "Field type should be in {counter, gauge}";
				}
			}
			n_metrics++;
		}
		EXPECT_EQ((3 * n_loops), n_metrics);
	}
	fieldstat_dynamic_instance_free(instance);
}

void _worker_thread_multi_set_multi_metric(void *arg)
{
	struct thread_para *para = (struct thread_para*)arg;
	int loops = para->loops;
	int thread_id = para->thread_id;
	int table_id = para->table_id;
	unsigned int *out_column_ids = para->out_column_ids;

	struct fieldstat_dynamic_instance *instance = para->instance;
	int ret = 0;
	struct fieldstat_tag tags[3];

	int i = 0;

	tags[0].key = "policy_id";
	tags[0].value_int = 1;
	tags[0].value_type = 0;

	tags[1].key = "quanlity";
	tags[1].value_double = 0.50;
	tags[1].value_type = 1;

	tags[2].key = "device_id";
	tags[2].value_str = "test_device";
	tags[2].value_type = 2;

	for(i = 0; i < loops; i++)
	{
		char gauge_name[32];
		char counter_name[32];
		char table_column_name[32];
		snprintf(gauge_name, sizeof(gauge_name), "Active_sessions_%d", i);
		snprintf(counter_name, sizeof(gauge_name), "Traffic_bytes_%d", i);
		snprintf(table_column_name, sizeof(gauge_name), "security_rule_hits_%d", i);

		for(int j = 0; j < loops; j++)
		{
			ret = fieldstat_dynamic_metric_value_set(instance, FIELD_TYPE_GAUGE, (const char *)gauge_name, 10, tags, sizeof(tags)/sizeof(tags[0]), thread_id); 
			EXPECT_EQ(0, ret);
			ret = fieldstat_dynamic_metric_value_set(instance, FIELD_TYPE_COUNTER, (const char *)counter_name, 20, tags, sizeof(tags)/sizeof(tags[0]), thread_id); 
			EXPECT_EQ(0, ret);
		
			ret = fieldstat_dynamic_table_metric_value_set(instance, table_id, out_column_ids[0], (const char *)table_column_name, 30, tags, sizeof(tags)/sizeof(tags[0]), thread_id);
			EXPECT_EQ(0, ret);
			ret = fieldstat_dynamic_table_metric_value_set(instance, table_id, out_column_ids[1], (const char *)table_column_name, 31, tags, sizeof(tags)/sizeof(tags[0]), thread_id);
			EXPECT_EQ(0, ret);
		}
	}
	return;
}
void * worker_thread_multi_set_multi_metric(void *arg)
{
	_worker_thread_multi_set_multi_metric(arg);
	return NULL;
}


TEST(FeildStatDynamicAPI, FieldStatDynamicInstanceMultiSetMultiMetric)
{
	int ret = 0;
	int n_thread = 64;
	int n_loops = 100;

	struct dynamic_metric *dyn_metric, *tmp_dyn_metric;
	struct dynamic_metric **head = NULL;
	struct metric **metrics = NULL;
	struct metric *metric = NULL;
	long long value = 0;

	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];
	int table_id = -1;

	struct thread_para para[n_thread];
	pthread_t thread_ids[n_thread];


	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", n_thread);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);

	EXPECT_EQ(0, table_id);	

	for(int i = 0; i < n_thread; i++)
	{
		para[i].loops = n_loops;
		para[i].instance = instance;
		para[i].thread_id = i;
		para[i].table_id = table_id;
		para[i].out_column_ids = out_column_ids;
	}

	for(int i = 0; i < n_thread; i++)
	{
		ret = pthread_create(&(thread_ids[i]), NULL, worker_thread_multi_set_multi_metric, &(para[i]));
		EXPECT_EQ(0, ret);
	}

	void *temp;
	for(int i = 0; i < n_thread; i++)
	{
		pthread_join(thread_ids[i], (void**)&temp);
	}

	for(int i = 0; i < n_thread; i++)
	{
		int n_metrics = 0;
		head = &instance->n_thread_dynamic_metric[i];
		HASH_ITER(hh, *head, dyn_metric, tmp_dyn_metric)
		{
			metrics = dyn_metric->metrics;
			metric  = metrics[0];
			if(metric->table)
			{
				metric = metrics[0];
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
				EXPECT_EQ(30, value);
				metric = metrics[1];
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
				EXPECT_EQ(31, value);
			}
			else
			{
				switch(metric->field_type)
				{
					case FIELD_TYPE_GAUGE:
						value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
						EXPECT_EQ(10, value);
						break;
					case FIELD_TYPE_COUNTER:
						value = get_metric_unit_val(metric, FS_CALC_CURRENT, 0);
						EXPECT_EQ(20, value);
						break;
					default:
						FAIL() << "Field type should be in {counter, gauge}";
				}
			}
			n_metrics++;
		}
		EXPECT_EQ((3 * n_loops), n_metrics);
	}
	fieldstat_dynamic_instance_free(instance);
}


TEST(FeildStatDynamicAPI, FieldStatDynamicMetricValueGet)
{
	int n_thread = 48;
	int ret = 0;
	long long value = 0;
	struct fieldstat_dynamic_instance *instance = NULL;

	instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	EXPECT_NE(nullptr, instance);

	ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);

	fieldstat_dynamic_instance_start(instance);

	ret = fieldstat_dynamic_metric_value_incrby(instance, FIELD_TYPE_GAUGE, "Active_sessions", 1000, NULL, 0, 0);
	EXPECT_EQ(0, ret);
	value = fieldstat_dynamic_metric_value_get(instance, "Active_sessions", NULL, 0, 0);
	EXPECT_EQ(1000, value);
	sleep(3);
	value = fieldstat_dynamic_metric_value_get(instance, "Active_sessions", NULL, 0, 0);
	EXPECT_EQ(1000, value);
	fieldstat_dynamic_instance_free(instance);
}


TEST(FeildStatDynamicAPI, FieldStatDynamicTableMetricValueGet)
{
	int n_thread = 48;
	int ret = 0;
	int table_id = -1;
	long long value = 0;

	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];

	struct fieldstat_dynamic_instance *instance = NULL;

	instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	EXPECT_NE(nullptr, instance);

	ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);

	fieldstat_dynamic_instance_start(instance);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);
	EXPECT_EQ(0, table_id);

	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[0], "security_rule_hits", 1000, NULL, 0, 0); 
	EXPECT_EQ(0, ret);
	ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[1], "security_rule_hits", 2000, NULL, 0, 0);
	EXPECT_EQ(0, ret);

	value = fieldstat_dynamic_table_metric_value_get(instance, table_id, out_column_ids[0], "security_rule_hits", NULL, 0, 0);
	EXPECT_EQ(1000, value);

	value = fieldstat_dynamic_table_metric_value_get(instance, table_id, out_column_ids[1], "security_rule_hits", NULL, 0, 0);
	EXPECT_EQ(2000, value);

	sleep(3);

	value = fieldstat_dynamic_table_metric_value_get(instance, table_id, out_column_ids[0], "security_rule_hits", NULL, 0, 0);
	EXPECT_EQ(0, value);

	value = fieldstat_dynamic_table_metric_value_get(instance, table_id, out_column_ids[1], "security_rule_hits", NULL, 0, 0);
	EXPECT_EQ(0, value);

	fieldstat_dynamic_instance_free(instance);
}

TEST(FeildStatDynamicAPI, TableRowMetricValueIncrby)
{
	int n_thread = 48;
	int ret = 0;
	int table_id = -1;
	long long value = 0;
	long long values[2] = {0, 0};

	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];

	struct fieldstat_dynamic_instance *instance = NULL;

	instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	EXPECT_NE(nullptr, instance);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);
	EXPECT_EQ(0, table_id);


	values[0] = 1000; 
	values[1] = 2000;
	ret = fieldstat_dynamic_table_row_metric_values_incrby(instance, table_id, "security_rule_hits", values, 2, NULL, 0, 0); 
	EXPECT_EQ(0, ret);

	value = fieldstat_dynamic_table_metric_value_get(instance, table_id, out_column_ids[0], "security_rule_hits", NULL, 0, 0);
	EXPECT_EQ(1000, value);

	value = fieldstat_dynamic_table_metric_value_get(instance, table_id, out_column_ids[1], "security_rule_hits", NULL, 0, 0);
	EXPECT_EQ(2000, value);

	fieldstat_dynamic_instance_free(instance);
}

TEST(FeildStatDynamicAPI, TableRowMetricValueDecrby)
{
	int n_thread = 48;
	int ret = 0;
	int table_id = -1;
	long long value = 0;
	long long values[2] = {0, 0};

	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];

	struct fieldstat_dynamic_instance *instance = NULL;

	instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	EXPECT_NE(nullptr, instance);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);
	EXPECT_EQ(0, table_id);


	values[0] = 1000; 
	values[1] = 2000;
	ret = fieldstat_dynamic_table_row_metric_values_decrby(instance, table_id, "security_rule_hits", values, 2, NULL, 0, 0); 
	EXPECT_EQ(0, ret);

	value = fieldstat_dynamic_table_metric_value_get(instance, table_id, out_column_ids[0], "security_rule_hits", NULL, 0, 0);
	EXPECT_EQ(-1000, value);

	value = fieldstat_dynamic_table_metric_value_get(instance, table_id, out_column_ids[1], "security_rule_hits", NULL, 0, 0);
	EXPECT_EQ(-2000, value);

	fieldstat_dynamic_instance_free(instance);
}


TEST(FeildStatDynamicAPI, TableRowMetricValueSet)
{
	int n_thread = 48;
	int ret = 0;
	int table_id = -1;
	long long value = 0;
	long long values[2] = {0, 0};

	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];

	struct fieldstat_dynamic_instance *instance = NULL;

	instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	EXPECT_NE(nullptr, instance);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);
	EXPECT_EQ(0, table_id);


	values[0] = 1000; 
	values[1] = 2000;
	ret = fieldstat_dynamic_table_row_metric_values_set(instance, table_id, "security_rule_hits", values, 2, NULL, 0, 0); 
	EXPECT_EQ(0, ret);

	value = fieldstat_dynamic_table_metric_value_get(instance, table_id, out_column_ids[0], "security_rule_hits", NULL, 0, 0);
	EXPECT_EQ(1000, value);

	value = fieldstat_dynamic_table_metric_value_get(instance, table_id, out_column_ids[1], "security_rule_hits", NULL, 0, 0);
	EXPECT_EQ(2000, value);

	fieldstat_dynamic_instance_free(instance);
}



TEST(FeildStatDynamicAPI, SendLenEqualUDPPayload)
{
	int ret = 0;
	int n_thread = 1;
	int table_id = -1;
	char row_name[256];

	const char *desired_output = "{\"fields\":{\"bytes\":0,\"packages\":1},"
								 "\"name\":\"security_rule_hits_000009\","
								 "\"tags\":{\"app_name\":\"firewall\",\"device_id\":"
								 "\"test_device\",\"policy_id\":\"1\",\"quanlity\":"
								 "\"0.500000\",\"table_name\":\"shaping\","
								 "\"thread_id\":\"1\"}}";

	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];

	struct fieldstat_tag tags[4];

	tags[0].key = "policy_id";
	tags[0].value_int = 1;
	tags[0].value_type = 0;

	tags[1].key = "quanlity";
	tags[1].value_double = 0.50;
	tags[1].value_type = 1;

	tags[2].key = "device_id";
	tags[2].value_str = "test_device";
	tags[2].value_type = 2;

	tags[3].key = "thread_id";
	tags[3].value_int = 1;
	tags[3].value_type = 0;

	size_t ntags = sizeof(tags)/sizeof(tags[0]);

	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, 
												column_type, 2, out_column_ids);
	EXPECT_EQ(0, table_id);	

	fieldstat_dynamic_instance_start(instance);

	for(int i = 0; i < 10; i ++)
	{
		memset(row_name, 0, sizeof(row_name));
		snprintf(row_name, sizeof(row_name), "security_rule_hits_%06d", i);
		ret = fieldstat_dynamic_table_metric_value_incrby(
				instance, table_id, out_column_ids[0],
				row_name, 1, tags,
				ntags, 0);
		EXPECT_EQ(0, ret);
	}
	sleep(3);
	parse_telegraf_cjson_output(desired_output);
	fieldstat_dynamic_instance_free(instance);
}


int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}