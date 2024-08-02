#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "fieldstat.h"
#include "fieldstat_internal.h"
#include "cJSON.h"

extern struct prometheus_endpoint_instance g_prometheus_endpoint_instance;



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
	struct field_tag tags[3];

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
		ret = fieldstat_dynamic_metric_value_incrby(instance, FIELD_METRIC_TYPE_GAUGE, "Active_sessions", 10, tags, sizeof(tags)/sizeof(tags[0]), thread_id); 
		EXPECT_EQ(0, ret);
		ret = fieldstat_dynamic_metric_value_incrby(instance, FIELD_METRIC_TYPE_COUNTER, "Traffic_bytes", 20, tags, sizeof(tags)/sizeof(tags[0]), thread_id); 
		EXPECT_EQ(0, ret);
	
		ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[0], "security_rule_hits", 30, tags, sizeof(tags)/sizeof(tags[0]), thread_id);
		EXPECT_EQ(0, ret);
		ret = fieldstat_dynamic_table_metric_value_incrby(instance, table_id, out_column_ids[1], "security_rule_hits", 31, tags, sizeof(tags)/sizeof(tags[0]), thread_id);
		EXPECT_EQ(0, ret);
		usleep(1000);
	}
	return;
}
void * worker_thread_multi_incrby(void *arg)
{
	_worker_thread_multi_incrby(arg);
	usleep(1000 * 100);
	return NULL;
}


TEST(FeildStatDynamicAPI, FieldStatDynamicInstanceMultiIncrby)
{
	int ret = 0;
	int n_thread = 64;
	int n_loops = 10000;

	const char *column_name[] = {"packages", "bytes"};
	enum field_metric_type column_type[] = {FIELD_METRIC_TYPE_COUNTER, FIELD_METRIC_TYPE_COUNTER};
	unsigned int out_column_ids[2];
	int table_id = -1;

	struct thread_para para[n_thread];
	pthread_t thread_ids[n_thread];


	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name, column_type, sizeof(column_name)/sizeof(column_name[0]), out_column_ids);
	EXPECT_EQ(0, table_id);	
	system("cat /dev/null > /tmp/metrics.out");
	fieldstat_dynamic_instance_start(instance);

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
	sleep(6);
	fieldstat_dynamic_instance_free(instance);
}

void _worker_thread_table_incrby(void *arg)
{	
	struct thread_para *para = (struct thread_para*)arg;
	int loops = para->loops;
	int table_id = para->table_id;

	const char *row_name = "security_rule_hits";
	struct fieldstat_dynamic_instance *instance = para->instance;
	int ret = 0;
	struct field_tag tags[14];
	long long values[20];


	tags[0].key = "policy_id";
	tags[0].value_int = 1;
	tags[0].value_type = 0;

	tags[1].key = "quanlity";
	tags[1].value_double = 0.50;
	tags[1].value_type = 1;

	tags[2].key = "device_id";
	tags[2].value_str = "xxg-cabinet0";
	tags[2].value_type = 2;

	tags[3].key = "device_id333333333333333333333333333333333333333333333333333"
				  "333333333333333333333333333333333333333";
	tags[3].value_str = "device_id_3";
	tags[3].value_type = 2;

	tags[4].key = "quanlity_444444444444444444444444444444444444444444444444444"
				  "444444444444444444444444444444444444444";
	tags[4].value_str = "quanlity_4";
	tags[4].value_type = 2;

	tags[5].key = "policy_id_55555555555555555555555555555555555555555555555555"
				  "555555555555555555555555555555555555555";
	tags[5].value_str = "policy_id_5";
	tags[5].value_type = 2;

	tags[6].key = "vsys_id_6666666666666666666666666666666666666666666666666666"
				  "666666666666666666666666666666666666666";
	tags[6].value_str = "vsys_id_6";
	tags[6].value_type = 2;

	tags[7].key = "system_id_77777777777777777777777777777777777777777777777777"
				  "777777777777777777777777777777777777777";
	tags[7].value_int  = 0;
	tags[7].value_type = 0;


	tags[8].key = "vsys_id_8666666666666666666666666666666666666666666666666666"
				  "666666666666666666666666666666666666666";
	tags[8].value_str = "vsys_id_6";
	tags[8].value_type = 2;

	tags[9].key = "system_id_97777777777777777777777777777777777777777777777777"
				  "777777777777777777777777777777777777777";
	tags[9].value_int  = 0;
	tags[9].value_type = 0;

	tags[10].key = "vsys_id_106666666666666666666666666666666666666666666666666"
				   "6666666666666666666666666666666666666666";
	tags[10].value_str = "vsys_id_6";
	tags[10].value_type = 2;

	tags[11].key = "system_id_1177777777777777777777777777777777777777777777777"
				   "7777777777777777777777777777777777777777";
	tags[11].value_int  = 0;
	tags[11].value_type = 0;


	tags[12].key = "system_id_1277777777777777777777777777777777777777777777777"
				   "7777777777777777777777777777777777777777";
	tags[12].value_int  = 0;
	tags[12].value_type = 0;


	tags[13].key = "system_id_1377777777777777777777777777777777777777777777777"
				   "7777777777777777777777777777777777777777";
	tags[13].value_int  = 0;
	tags[13].value_type = 0;


	for(int i = 0; i < loops; i++)
	{
		for(int j = 0; j < 20; j++)
		{
			values[j] = 0;
			values[j] = j + i;
		}
		tags[0].value_int = i;
		
		if(i % 2 == 0)
		{
			ret = fieldstat_dynamic_table_row_metric_values_decrby(
				instance, table_id,
				row_name, values,
				20, tags, 14, 0); 
			EXPECT_EQ(-1, ret);
		}
		else
		{
			ret = fieldstat_dynamic_table_row_metric_values_decrby(
				instance, table_id,
				row_name, values,
				20, tags, 3, 0); 
			EXPECT_EQ(0, ret);
		}
		
		usleep(1000);
	}
	return;
}

void * worker_thread_table_incrby(void *arg)
{
	_worker_thread_table_incrby(arg);
	usleep(1000 * 100);
	return NULL;
}

TEST(DynamicOutput, HashKeyOutOfSize)
{
	int ret = 0;
	int n_thread = 1;
	int n_loops = 1000;
	unsigned int out_column_ids[20];
	int table_id = -1;
	struct fieldstat_dynamic_instance *instance = NULL;

	const char *column_name[] = {
		"packages01", "packages02", "packages03", "packages04", "packages05",
		"packages06", "packages07", "packages08", "packages09", "packages10",
		"packages11", "packages12", "packages13", "packages14", "packages15",
		"packages16", "packages17", "packages18", "packages19", "packages20"};

	enum field_metric_type column_type[] = {
		FIELD_METRIC_TYPE_COUNTER, FIELD_METRIC_TYPE_COUNTER, FIELD_METRIC_TYPE_COUNTER, FIELD_METRIC_TYPE_COUNTER,
		FIELD_METRIC_TYPE_COUNTER, FIELD_METRIC_TYPE_COUNTER, FIELD_METRIC_TYPE_COUNTER, FIELD_METRIC_TYPE_COUNTER,
		FIELD_METRIC_TYPE_COUNTER, FIELD_METRIC_TYPE_COUNTER, FIELD_METRIC_TYPE_COUNTER, FIELD_METRIC_TYPE_COUNTER,
		FIELD_METRIC_TYPE_COUNTER, FIELD_METRIC_TYPE_COUNTER, FIELD_METRIC_TYPE_COUNTER, FIELD_METRIC_TYPE_COUNTER,
		FIELD_METRIC_TYPE_COUNTER, FIELD_METRIC_TYPE_COUNTER, FIELD_METRIC_TYPE_COUNTER, FIELD_METRIC_TYPE_COUNTER};


	struct thread_para para[n_thread];
	pthread_t thread_ids[n_thread];

	instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8700);
	EXPECT_EQ(0, ret);

	table_id = fieldstat_register_dynamic_table(instance, "shaping", column_name,
												column_type, 20, out_column_ids);
	EXPECT_EQ(0, table_id);	

	fieldstat_dynamic_set_output_interval(instance, 1);
	system("cat /dev/null > /tmp/metrics.out");
	fieldstat_dynamic_instance_start(instance);

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
		ret = pthread_create(&(thread_ids[i]), NULL, worker_thread_table_incrby,
							 &(para[i]));
		EXPECT_EQ(0, ret);
	}

	void *temp;
	for(int i = 0; i < n_thread; i++)
	{
		pthread_join(thread_ids[i], (void**)&temp);
	}
	sleep(6);
	fieldstat_dynamic_instance_free(instance);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}