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
	char buff[128];
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

	test_instance = fieldstat_instance_create("test");

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

	ret = fieldstat_background_thead_disable(test_instance);
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

	fieldstat_value_set(test_instance, ret, 1);

	ret = fieldstat_register(test_instance, FIELD_TYPE_COUNTER, "metric_1", tag_key0, tag_value0, sizeof(tag_key0)/sizeof(tag_key0[0]));
	if(ret == -1)
	{
		printf("Failed to register metric!\n");
	}
	fieldstat_value_set(test_instance, ret, 2);
	ret = fieldstat_register(test_instance, FIELD_TYPE_GAUGE, "gauge_0",tag_key1, tag_value1, sizeof(tag_key1)/sizeof(tag_key1[0]));
	if(ret == -1)
	{
		printf("Failed to register metric gauge_0\n");
	}
	fieldstat_value_set(test_instance, ret, 9999);
	ret = fieldstat_register(test_instance, FIELD_TYPE_GAUGE, "gauge_0_tag", NULL, NULL, 0);
	if(ret == -1)
	{
		printf("Failed to register metric gauge_0_tag\n");
	}
	//fieldstat_value_set(test_instance, ret, 1111);
	fieldstat_value_decrby(test_instance, ret, 22);
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
		fieldstat_value_set(test_instance, id_list.id[i], 100 + i);
		printf("%d\n", id_list.id[i]);
	}

	id_list = fieldstat_register_table_metrics(test_instance, table_id, "line_2", tag_key3, tag_value3, sizeof(tag_key3)/sizeof(tag_key3[0]));
	for(i = 0; i < id_list.count; i++)
	{
		fieldstat_value_set(test_instance,id_list.id[i], 1000 + i);
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
		fieldstat_value_set(test_instance,id_list.id[i], 2000 + i);
		printf("%d\n", id_list.id[i]);
	}


	for (i = 0; i < TEST_HISTOGRAM_NUM; i++)
	{
		snprintf(buff, sizeof(buff), "{rpc_%d}", i);
		if (i < 2)
		{
			fieldstat_register_histogram(test_instance, FIELD_TYPE_SUMMARY, buff, NULL, NULL, 0, 
			"0.1,0.5,0.8,0.9,0.95,0.99",1,10000,3);
			//histogram_ids[i] = FS_register_histogram(handle, FS_CALC_SPEED, buff, 1, 100000, 3);
		}
		else
		{
			fieldstat_register_histogram(test_instance, FILED_TYPE_HISTOGRAM, buff, NULL, NULL, 0, 
			"1.0,2.0,3.0,4.0,5.0,6.0",1,10000,3);
		}
	}

	int abcd = fieldstat_register_histogram(test_instance, FILED_TYPE_HISTOGRAM, "re_hi", NULL, NULL, 0, 
	"10,20,30,40,50",1,10000,3);
	abcd = fieldstat_register_histogram(test_instance, FILED_TYPE_HISTOGRAM, "re_hi000", NULL, NULL, 0, 
	"10,20,30,40,60",1,10000,3);
	fieldstat_instance_start(test_instance);

	for(i = 0; i < id_list.count; i++)
	{
		fieldstat_value_set(test_instance,id_list.id[i], 5000 + i);
		printf("%d\n", id_list.id[i]);
	}

	//long long preset[] = {1, 4, 10, 50, 20, 30, 40, 200, 300, 400, 600, 1000, 2000, 4000, 5000};
	long long preset[] = {1,2,3,4,5,6,3000,3500,3600,3700,3800,4000,4000};
	for (i = 0; (size_t)i < sizeof(preset) / sizeof(long long); i++)
	{
		fieldstat_value_set(test_instance,abcd, preset[i]);
		//FS_operate(handle, histogram_ids[i], 0, FS_OP_SET, preset[j]);
	}


	sleep(1);
	fieldstat_passive_output(test_instance);
	printf("Testing for fieldstat\n");
	sleep(10000);
	return 0;
}

