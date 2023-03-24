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


int g_counter_id[3];
int g_gauge_id[3];
int g_histogram_id = -1;
int g_summary_id = -1;
int g_id_list[32];
int g_id_cnt;

struct thread_para
{
	int loops;
	struct fieldstat_instance * instance;
	int thread_id;
	int shaping_table_id;
	int sce_table_id;
};

static void* worker_thread(void* arg)
{
	
	struct thread_para* para=(struct thread_para*)arg;
	int loops = para->loops, i=0;
	struct fieldstat_instance *instance = para->instance;
	while (loops > 0)
	{
		loops--;
		for (i = 0; i < 3; i++)
		{
			fieldstat_value_incrby(instance,g_counter_id[i], i + 1);
		}
		for (i = 0; i < 3; i++)
		{
			fieldstat_value_incrby(instance, g_gauge_id[i], i + 10);
		}
		for(i = 0; i < 10; i ++)
		{
			fieldstat_value_set(instance,g_histogram_id, i + 1);
		}
		for (i = 101; i < 201; i++)
		{
			fieldstat_value_set(instance, g_summary_id, i);
		}

		for(i = 0; i < g_id_cnt; i++)
		{
			fieldstat_value_incrby(instance, g_id_list[i], 1);
		}

		sleep(1);
	}
	return NULL;
}

static void* dynamic_register(void* arg)
{
	char name[32] = {0}; 
	struct thread_para* para=(struct thread_para*)arg;
	int loops = para->loops;
	struct fieldstat_instance *instance = para->instance;
	int shaping_table_id = para->shaping_table_id;
	int sce_table_id = para->sce_table_id;
	const char * bins_htr = "10,20,30,40,50,60,70,80,90";
	const char * bins_sar = "0.1,0.5,0.8,0.9,0.95,0.99";

	struct fieldstat_tag tags[16];
	tags[0].key = "disable_output_prometheus";
	tags[0].value_type = 2;
	tags[0].value_str = "yes";

	while (loops > 0)
	{
		loops--;
		//fieldstat_value_incrby(instance,g_counter_id[i], i + 1);
		snprintf(name, sizeof(name), "counter_%d_%d", loops, rand()%10000);
		fieldstat_register(instance, FIELD_TYPE_COUNTER, name, tags, 1);

		memset(name, 0, sizeof(name));
		snprintf(name, sizeof(name), "gauge_%d_%d", loops, rand()%10000);
		fieldstat_register(instance,FIELD_TYPE_GAUGE, name, tags, 1);

		memset(name, 0, sizeof(name));
		snprintf(name, sizeof(name), "summary_%d_%d", loops, rand()%10000);
		fieldstat_register_histogram(instance, name, tags, 1, bins_htr, 1, 10000, 2, 0);

		memset(name, 0, sizeof(name));
		snprintf(name, sizeof(name), "histogram_%d_%d", loops, rand()%10000);
		fieldstat_register_summary(instance, name, tags, 1, bins_sar,1,10000,2, 1);

		memset(name, 0, sizeof(name));
		snprintf(name, sizeof(name), "shp_%d_%d", loops, rand()%10000);
		int id_list[16];
		fieldstat_register_table_row(instance, shaping_table_id, name, tags, 1, id_list);

		memset(name, 0, sizeof(name));
		snprintf(name, sizeof(name), "sce_%d_%d", loops, rand()%10000);
		fieldstat_register_table_row(instance, sce_table_id, name, tags, 1, id_list);

		sleep(1);
	}
	return NULL;
}



int set_instance_parameter(struct fieldstat_instance *instance)
{
	int ret = 0;

	do {
		ret = fieldstat_global_enable_prometheus_endpoint(9010, "/metrics");
		if(ret == -1)
		{
			printf("Failed to enable prometheus endpoint\n");
			break;
		}

		ret = fieldstat_set_local_output(instance, "./test.fs", "default");
		if(ret == -1)
		{
			printf("set fieldstat local_outpud failed!\n");
			break;
		}

		ret = fieldstat_set_output_interval(instance, 1000);
		if(ret == -1)
		{
			printf("Set output_interval failed!\n");
			break;
		}		
		
		ret = fieldstat_set_line_protocol_server(instance, "127.0.0.1", 8001);
		if(ret == -1)
		{
			printf("Failed to set line_protocol server\n");
			break;
		}
		ret = fieldstat_enable_prometheus_output(instance);
		if(ret == -1)
		{
			printf("Failed to set prometheus_output_enable!\n");
			break;
		}
/*
		ret = fieldstat_disable_background_thread(instance);
		if(ret == -1)
		{
			printf("Failed to disable filedstat background thread\n");
		}
*/
	}while(0);


	return ret;
}

int test_register_by_fieldstat_type(struct fieldstat_instance *instance)
{
	int ret = 0;
//Register counter start
	ret = fieldstat_register(instance, FIELD_TYPE_COUNTER, "counter_no_tags",NULL,0);
	if(ret == -1)
	{
		printf("Failed ot register counter_no_tags, type = counter\n");
		return ret;
	}
	g_counter_id[0] = ret;

	struct fieldstat_tag tag_counter;
	tag_counter.value_type = 2;
	tag_counter.value_str = "C_one_tag";
	tag_counter.key = "C_one_tag_yes";

	ret = fieldstat_register(instance, FIELD_TYPE_COUNTER,"counter_with_one_tag", &tag_counter, 1);
	if(ret == -1)
	{
		printf("Failed ot register counter_with_one_tag, type = counter\n");
		return ret;
	}
	g_counter_id[1] = ret;

	struct fieldstat_tag tags_counter[3];
	tags_counter[0].value_type = 2;
	tags_counter[0].value_str = "C_tags_0";
	tags_counter[0].key = "C_tags_0_yes";

	tags_counter[1].value_type = 2;
	tags_counter[1].value_str = "C_tags_1";
	tags_counter[1].key = "C_tags_1_yes";

	tags_counter[2].value_type = 2;
	tags_counter[2].value_str = "C_tags_2";
	tags_counter[2].key = "C_tags_2_yes";


	ret = fieldstat_register(instance, FIELD_TYPE_COUNTER,"counter_with_tags", tags_counter, sizeof(tags_counter)/sizeof(tags_counter[0]));
	if(ret == -1)
	{
		printf("Failed ot register counter_with_tags, type = counter\n");
		return ret;
	}
	g_counter_id[2] = ret;
//Register counter end

//Register gauge start 

	ret = fieldstat_register(instance, FIELD_TYPE_GAUGE, "gauge_no_tags",NULL,0);
	if(ret == -1)
	{
		printf("Failed ot register gauge_no_tags, type = gauge\n");
		return ret;
	}
	g_gauge_id[0] = ret;

	struct fieldstat_tag tag_gauge;
	tag_gauge.value_type = 2;
	tag_gauge.value_str = "G_one_tag";
	tag_gauge.key = "G_one_tag_yes";

	ret = fieldstat_register(instance, FIELD_TYPE_GAUGE,"gauge_with_one_tag", &tag_gauge, 1);
	if(ret == -1)
	{
		printf("Failed ot register gauge_with_one_tag, type = gauge\n");
		return ret;
	}
	g_gauge_id[1] = ret;

	struct fieldstat_tag tags_gauge[3];
	tags_gauge[0].value_type = 2;
	tags_gauge[0].value_str = "G_tags_0";
	tags_gauge[0].key = "G_tags_0_yes";

	tags_gauge[1].value_type = 2;
	tags_gauge[1].value_str = "G_tags_1";
	tags_gauge[1].key = "G_tags_1_yes";

	tags_gauge[2].value_type = 2;
	tags_gauge[2].value_str = "G_tags_2";
	tags_gauge[2].key = "G_tags_2_yes";

	ret = fieldstat_register(instance, FIELD_TYPE_GAUGE, "gauge_with_tags", tags_gauge, sizeof(tags_gauge)/sizeof(tags_gauge[0]));
	if(ret == -1)
	{
		printf("Failed ot register gauge_with_tags, type = gauge\n");
		return ret;
	}
	g_gauge_id[2] = ret;
//Register gauge end

//Register histogram and summary start
	const char * bins_htr = "10,20,30,40,50,60,70,80,90";
	ret = fieldstat_register_histogram(instance, "htr", NULL, 0, bins_htr, 1, 1000, 2, 0);
	if(ret == -1)
	{
		printf("Failed ot register htr, type = histogram\n");
		return ret;
	}
	g_histogram_id = ret;
	const char * bins_sar = "0.1,0.5,0.8,0.9,0.95,0.99";
	ret = fieldstat_register_summary(instance, "sar", NULL, 0, bins_sar, 1, 1000, 2, 0);
	if(ret == -1)
	{
		printf("Failed ot register sar, type = summary\n");
		return ret;
	}
	g_summary_id = ret;
//Register histogram and summary end

//Register table start
	int table_id = -1;
	const char *column[] = {"T_success_log","T_fail_log"};
	enum field_type table_type[] = {FIELD_TYPE_COUNTER, FIELD_TYPE_GAUGE};
	table_id = fieldstat_register_table(instance, "table_test", column, table_type, sizeof(column)/sizeof(column[0]));
	if(table_id == -1)
	{
		printf("Failed to register metric table\n");
	}
	g_id_cnt = sizeof(column)/sizeof(column[0]);
	ret = fieldstat_register_table_row(instance, table_id, "SUM", NULL, 0, g_id_list);
	if(ret == -1)
	{
		return -1;
	}
//Register table end

	return ret;
}


int main(int argc, char *argv[])
{
	int ret = 0;

	struct fieldstat_instance *test_instance = fieldstat_instance_new("test");

	if(test_instance == NULL)
	{
		printf("Create fieldstat instance error\n");
		return -1;
	}

	ret = set_instance_parameter(test_instance);

	if(ret == -1)
	{
		printf("Set fieldstat instance parameter error\n");
		return ret;
	}

	ret = test_register_by_fieldstat_type(test_instance);

	if(ret == -1)
	{
		printf("Set fieldstat instance parameter error\n");
		return ret;
	}


	fieldstat_instance_start(test_instance);
	sleep(1);

	int thread_num=100;
	pthread_t threads[thread_num];

	pthread_t threads_reg[thread_num];

	struct thread_para para;
	const char *shaping_column[] = {"in_rx_pkts","in_tx_pkts"};
	enum field_type shaping_type[] = {FIELD_TYPE_COUNTER,FIELD_TYPE_GAUGE};
	para.shaping_table_id = fieldstat_register_table(test_instance,"shaping",shaping_column,shaping_type,sizeof(shaping_column)/sizeof(shaping_column[0]));
	const char *sce_column[] = {"sent_bytes","recv_pkts"};
	enum field_type sce_type[] = {FIELD_TYPE_COUNTER,FIELD_TYPE_COUNTER};
	para.sce_table_id = fieldstat_register_table(test_instance, "sce", sce_column,sce_type, sizeof(sce_column)/sizeof(sce_column[0]));
	para.loops=10;
	para.instance= test_instance;
	for(int i=0; i<thread_num; i++)
	{
		pthread_create(&(threads[i]), NULL, worker_thread, &para);
		pthread_create(&(threads_reg[i]), NULL, dynamic_register, &para);
	}
	void *temp;
	for(int i=0; i<thread_num; i++)
	{
		pthread_join(threads[i], (void**)&temp);
		pthread_join(threads_reg[i], (void**)&temp);
	}
	fieldstat_passive_output(test_instance);
	sleep(1);
	fieldstat_passive_output(test_instance);
	sleep(1000);

	return 0;
}

