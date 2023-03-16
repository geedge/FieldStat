#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "fieldstat.h"
#include "fieldstat_internal.h"

extern struct prometheus_endpoint_instance g_prometheus_endpoint_instance;

TEST(FeildStatDynamicAPI, FieldStatDynamicInstanceNew)
{
	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", 16);

	EXPECT_NE(nullptr, instance);
	EXPECT_STREQ("firewall", instance->name);
	EXPECT_EQ(16, instance->n_thread);

	for(int i = 0; i < instance->n_thread; i++)
	{
		EXPECT_EQ(nullptr, instance->n_thread_dynamic_metric[i]);
	}
}

TEST(FeildStatDynamicAPI, FieldStatDynamicInstanceSetPara)
{
	int ret = 0;
	int input_ip = 0;

	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", 16);
	EXPECT_NE(nullptr, instance);

	ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8080);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(8080, instance->line_protocol_server_port);
	ret = inet_pton(AF_INET, "127.0.0.1", (void *)&(input_ip));
	EXPECT_EQ(1, ret);
	EXPECT_EQ(input_ip, instance->line_protocol_server_ip);
	EXPECT_NE(-1, instance->line_protocol_socket);

	ret = fieldstat_dynamic_disable_background_thread(instance);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(1, instance->background_thread_disable);

	ret = fieldstat_dynamic_set_output_interval(instance, 5000);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(5000, instance->output_interval_ms);
}


TEST(FeildStatDynamicAPI, FieldStatDynamicStart)
{
	int ret = 0;
	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", 16);
	EXPECT_NE(nullptr, instance);

	fieldstat_dynamic_instance_start(instance);
	EXPECT_EQ(1, instance->running);

	ret = pthread_kill(instance->cfg_mon_t, 0);
	EXPECT_EQ(0, ret);
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
}




int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
    return 0;
}