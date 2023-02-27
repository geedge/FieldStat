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
	struct fieldstat_instance * test_instance = NULL;
	const char *tag_key[] = {"A","B","C"};
	const char *tag_value[] = {"a1","b1","c1"};

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

	ret = fieldstat_prometheus_output_enable(test_instance);
	if(ret == -1)
	{
		printf("Failed to set prometheus_output_enable!\n");
	}

	ret = fieldstat_register(test_instance, FIELD_TYPE_COUNTER, "metric_0", tag_key, tag_value, sizeof(tag_key)/sizeof(tag_key[0]));
	if(ret == -1)
	{
		printf("Failed to register metric!\n");
	}


        ret = fieldstat_register(test_instance, FIELD_TYPE_COUNTER, "metric_1", tag_key, tag_value, sizeof(tag_key)/sizeof(tag_key[0]));
        if(ret == -1)
        {
                printf("Failed to register metric!\n");
        }
	
	ret = fieldstat_register(test_instance, FIELD_TYPE_GAUGE, "gauge_0",tag_key, tag_value, sizeof(tag_key)/sizeof(tag_key[0]));

	fieldstat_instance_start(test_instance);
	sleep(1);
	fieldstat_passive_output(test_instance);
	printf("Testing for fieldstat\n");
	return 0;
}
