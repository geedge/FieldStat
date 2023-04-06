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
	int n_loops = 100000;

	long long value = 0;

	const char *column_name[] = {"packages", "bytes"};
	enum field_type column_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	unsigned int out_column_ids[2];
	int table_id = -1;

	struct thread_para para[n_thread];
	pthread_t thread_ids[n_thread];


	struct fieldstat_dynamic_instance *instance = fieldstat_dynamic_instance_new("firewall", n_thread);
	ret = fieldstat_dynamic_set_line_protocol_server(instance, "127.0.0.1", 8600);
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

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}