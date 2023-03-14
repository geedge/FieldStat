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
struct metric_id_list g_id_list;

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

		for(i = 0; i < g_id_list.count; i++)
		{
			fieldstat_value_incrby(instance, g_id_list.id[i], 1);
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
	const char * tags_key[] = {"disable_output_prometheus"};
	const char * tags_value[] = {"yes"};

	while (loops > 0)
	{
		loops--;
		//fieldstat_value_incrby(instance,g_counter_id[i], i + 1);
		snprintf(name, sizeof(name), "counter_%d_%d", loops, rand()%10000);
		fieldstat_register(instance,FIELD_TYPE_COUNTER,name,tags_key,tags_value,sizeof(tags_key)/sizeof(tags_key[0]));

		memset(name, 0, sizeof(name));
		snprintf(name, sizeof(name), "gauge_%d_%d", loops, rand()%10000);
		fieldstat_register(instance,FIELD_TYPE_GAUGE, name, tags_key,tags_value,sizeof(tags_key)/sizeof(tags_key[0]));

		memset(name, 0, sizeof(name));
		snprintf(name, sizeof(name), "summary_%d_%d", loops, rand()%10000);
		fieldstat_register_histogram(instance,name,tags_key,tags_value,sizeof(tags_key)/sizeof(tags_key[0]),bins_htr,1,10000,2);

		memset(name, 0, sizeof(name));
		snprintf(name, sizeof(name), "histogram_%d_%d", loops, rand()%10000);
		fieldstat_register_summary(instance,name,tags_key,tags_value,sizeof(tags_key)/sizeof(tags_key[0]),bins_sar,1,10000,2);

		memset(name, 0, sizeof(name));
		snprintf(name, sizeof(name), "shp_%d_%d", loops, rand()%10000);
		fieldstat_register_table_metrics(instance,shaping_table_id,name,tags_key,tags_value,sizeof(tags_key)/sizeof(tags_key[0]));

		memset(name, 0, sizeof(name));
		snprintf(name, sizeof(name), "sce_%d_%d", loops, rand()%10000);
		fieldstat_register_table_metrics(instance,sce_table_id,name,tags_key,tags_value,sizeof(tags_key)/sizeof(tags_key[0]));

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

		ret = fieldstat_set_output_interval(instance, 1);
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
		ret = fieldstat_set_prometheus_output(instance);
		if(ret == -1)
		{
			printf("Failed to set prometheus_output_enable!\n");
			break;
		}
/*
		ret = fieldstat_background_thread_disable(instance);
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
	ret = fieldstat_register(instance, FIELD_TYPE_COUNTER, "counter_no_tags",NULL,NULL,0);
	if(ret == -1)
	{
		printf("Failed ot register counter_no_tags, type = counter\n");
		return ret;
	}
	g_counter_id[0] = ret;
	const char * counter_with_one_key[] = {"C_one_tag"};
	const char * counter_with_one_value[] = {"C_one_tag_yes"};

	ret = fieldstat_register(instance, FIELD_TYPE_COUNTER,"counter_with_one_tag", counter_with_one_key, 
							counter_with_one_value, sizeof(counter_with_one_key)/sizeof(counter_with_one_key[0]));
	if(ret == -1)
	{
		printf("Failed ot register counter_with_one_tag, type = counter\n");
		return ret;
	}
	g_counter_id[1] = ret;

	const char * counter_with_keys[] = {"C_tags_0","C_tags_1","C_tags_2"};
	const char * counter_with_values[] = {"C_tags_0_yes", "C_tags_1_yes","C_tags_2_yes"};

	ret = fieldstat_register(instance, FIELD_TYPE_COUNTER,"counter_with_tags", counter_with_keys, 
							counter_with_values, sizeof(counter_with_keys)/sizeof(counter_with_keys[0]));
	if(ret == -1)
	{
		printf("Failed ot register counter_with_tags, type = counter\n");
		return ret;
	}
	g_counter_id[2] = ret;
//Register counter end

//Register gauge start 

	ret = fieldstat_register(instance, FIELD_TYPE_GAUGE, "gauge_no_tags",NULL,NULL,0);
	if(ret == -1)
	{
		printf("Failed ot register gauge_no_tags, type = gauge\n");
		return ret;
	}
	g_gauge_id[0] = ret;

	const char * gauge_with_one_key[] = {"G_one_tag"};
	const char * gauge_with_one_value[] = {"G_one_tag_yes"};
	ret = fieldstat_register(instance, FIELD_TYPE_GAUGE,"gauge_with_one_tag", gauge_with_one_key, 
							gauge_with_one_value, sizeof(gauge_with_one_key)/sizeof(gauge_with_one_key[0]));
	if(ret == -1)
	{
		printf("Failed ot register gauge_with_one_tag, type = gauge\n");
		return ret;
	}
	g_gauge_id[1] = ret;

	const char * gauge_with_keys[] = {"G_tags_0","G_tags_1","G_tags_2"};
	const char * gauge_with_values[] = {"G_tags_0_yes", "G_tags_1_yes","G_tags_2_yes"};

	ret = fieldstat_register(instance, FIELD_TYPE_GAUGE,"gauge_with_tags", gauge_with_keys, 
							gauge_with_values, sizeof(gauge_with_keys)/sizeof(gauge_with_keys[0]));
	if(ret == -1)
	{
		printf("Failed ot register gauge_with_tags, type = gauge\n");
		return ret;
	}
	g_gauge_id[2] = ret;
//Register gauge end

//Register histogram and summary start
	const char * bins_htr = "10,20,30,40,50,60,70,80,90";
	ret = fieldstat_register_histogram(instance, "htr",NULL,NULL,0,bins_htr,1,1000,2);
	if(ret == -1)
	{
		printf("Failed ot register htr, type = histogram\n");
		return ret;
	}
	g_histogram_id = ret;
	const char * bins_sar = "0.1,0.5,0.8,0.9,0.95,0.99";
	ret = fieldstat_register_summary(instance, "sar",NULL,NULL,0,bins_sar,1,1000,2);
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
	struct metric_id_list id_list;
	table_id = fieldstat_register_table(instance, "table_test", column, table_type, sizeof(column)/sizeof(column[0]));
	if(table_id == -1)
	{
		printf("Failed to register metric table\n");
	}

	id_list = fieldstat_register_table_metrics(instance, table_id, "SUM", NULL, NULL, 0);
	if(id_list.count == 0)
	{
		return -1;
	}
	g_id_list = id_list;
//Register table end

	return ret;
}


int main(int argc, char *argv[])
{
	int ret = 0;

	struct fieldstat_instance *test_instance = fieldstat_instance_create("test");

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

