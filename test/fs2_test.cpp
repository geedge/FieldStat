#include "../inc/fieldstat.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>


#define TEST_STATUS_NUM 4
#define TEST_FIELD_NUM 9
#define TEST_LINE_NUM 7
#define TEST_COLUMN_NUM 8
#define TEST_HISTOGRAM_NUM 4

#define TEST_RUNTIME_REG_NUM 32
#define TEST_RUNTIME_REG_LINE_NUM 6



int main(int argc, char *argv[])
{
	int ret = 0;
	int i = 0;
	struct fieldstat_instance * test_instance = NULL;
	const char *tag_key[] = {"A","B","C"};
	const char *tag_value[] = {"a","b","c"};

	const char *tag_key0[] = {"A0","B0","C0"};
	const char *tag_value0[] = {"a0","b0","c0"};

	const char *tag_key1[] = {"A1","B1","C1"};
	const char *tag_value1[] = {"a1","b1","c1"};

	const char *tag_key2[] = {"A2","B2","C2"};
	const char *tag_value2[] = {"a2","b2","c2"};

	const char *tag_key3[] = {"A3","B3","C3"};
	const char *tag_value3[] = {"a3","b3","c3"};

	const char *tag_key4[] = {"A4","B4","C4"};
	const char *tag_value4[] = {"a4","b4","c4"};

	//const char *tag_key5[] = {"A5","B5","C5"};
	//const char *tag_value5[] = {"a5","b5","c5"};

	enum field_type table_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	const char *field_list[] = {"column_0", "colunm_1", "column_2"};
	const char *field_list_0[] = {"c0", "c1", "c2"};
	struct metric_id_list id_list; 

	fieldstat_global_enable_prometheus_endpoint(9010, "/metrics");

	test_instance = fieldstat_instance_create();
	ret = fieldstat_set_app_name(test_instance, "test");
	if(ret == -1)
	{
		printf("set fieldstat app_name failed!\n");
	}

	ret = fieldstat_set_local_output(test_instance, "./test.fs", "default");
	if(ret == -1)
	{
		printf("set fieldstat local_outpud failed!\n");
	}

	ret = fieldstat_set_output_interval(test_instance, 5);
	if(ret == -1)
	{
		printf("Set output_interval failed!\n");
	}

	ret = fieldstat_backgroud_thead_disable(test_instance);
	if(ret == -1)
	{
		printf("Set backgroud thread disable failed!\n");
	}

	ret = fieldstat_set_line_protocol_server(test_instance, "127.0.0.1", 8001);
	if(ret == -1)
	{
		printf("Failed to set line_protocol server\n");
	}

	ret = fieldstat_set_statsd_server(test_instance, "192.168.100.1", 8002);
	if(ret == -1)
	{
		printf("Failed to set statsd_server!\n");
	}

	ret = fieldstat_set_prometheus_output(test_instance);
	if(ret == -1)
	{
		printf("Failed to set prometheus_output_enable!\n");
	}

	ret = fieldstat_register(test_instance, FIELD_TYPE_COUNTER, "metric_0", tag_key, tag_value, sizeof(tag_key)/sizeof(tag_key[0]));
	if(ret == -1)
	{
		printf("Failed to register metric!\n");
	}

	ret = fieldstat_register(test_instance, FIELD_TYPE_COUNTER, "metric_1", tag_key0, tag_value0, sizeof(tag_key0)/sizeof(tag_key0[0]));
	if(ret == -1)
	{
		printf("Failed to register metric!\n");
	}
	
	ret = fieldstat_register(test_instance, FIELD_TYPE_GAUGE, "gauge_0",tag_key1, tag_value1, sizeof(tag_key1)/sizeof(tag_key1[0]));
	if(ret == -1)
	{
		printf("Failed to register metric gauge_0\n");
	}

	ret = fieldstat_register(test_instance, FIELD_TYPE_GAUGE, "gauge_0_tag", NULL, NULL, 0);
	if(ret == -1)
	{
		printf("Failed to register metric gauge_0_tag\n");
	}
/*
	enum field_type table_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER, FIELD_TYPE_COUNTER};
	const char *field_list[] = {"column_0", "colunm_1", "column_2"};
*/
	int table_id = -1;
	table_id = fieldstat_register_table(test_instance, table_type, field_list, sizeof(field_list)/sizeof(field_list[0]));
	if(table_id == -1)
	{
		printf("Failed to register metric table\n");
	}

	id_list = fieldstat_register_table_metrics(test_instance, table_id, "line_0", tag_key2, tag_value2, sizeof(tag_key2)/sizeof(tag_key2[0]));
	for(i = 0; i < id_list.count; i++)
	{
		printf("%d\n", id_list.id[i]);
	}

	id_list = fieldstat_register_table_metrics(test_instance, table_id, "line_2", tag_key3, tag_value3, sizeof(tag_key3)/sizeof(tag_key3[0]));
	for(i = 0; i < id_list.count; i++)
	{
		printf("%d\n", id_list.id[i]);
	}
	
	table_id = fieldstat_register_table(test_instance, table_type, field_list_0, sizeof(field_list_0)/sizeof(field_list_0[0]));
	if(table_id == -1)
	{
		printf("Failed to register metric table list_0\n");
	}

	id_list = fieldstat_register_table_metrics(test_instance, table_id, "line_0", tag_key4, tag_value4, sizeof(tag_key4)/sizeof(tag_key4[0]));
	printf("id_list.num:%d\n",id_list.count);
	for(i = 0; i < id_list.count; i++)
	{
		printf("%d\n", id_list.id[i]);
	}

	fieldstat_instance_start(test_instance);
	sleep(1);
	fieldstat_passive_output(test_instance);
	printf("Testing for fieldstat\n");
	sleep(10000);
	return 0;
}

