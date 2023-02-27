#ifndef H_SCHEMA_STAT_H_INCLUDE
#define H_SCHEMA_STAT_H_INCLUDE
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#ifndef __cplusplus
#error("This file should be compiled with C++ compiler")
#endif


#define URL_MAX_LEN   2048
#define LEN_IP_MAX     32  //?????? 32 from document 
#define LEN_APP_NAME   32
#define LEN_FORMAT_MAX 32
#define LEN_PATH_MAX   256
#define	NUM_MAX_TABLE	64
#define TABLE_COLUMN_SIZE 32
#define	UDP_PAYLOAD_SIZE 1460

struct fieldstat_global_prometheus
{
	unsigned short port;
	char *url_path;
};

struct fieldstat_instance
{
	//char *statsd_server_ip;
	char statsd_server_str_ip[LEN_IP_MAX];
	unsigned int statsd_server_ip;
	unsigned short statsd_server_port;
	int statsd_output_enable;

	char line_protocol_server_str_ip[LEN_IP_MAX];
	unsigned int line_protocol_server_ip;
	unsigned short line_protocol_server_port;
	int line_protocol_output_enable;

	char local_output_filename[LEN_PATH_MAX];
	char local_output_format[LEN_FORMAT_MAX];
	int local_output_enable;

	int background_thread_disable;                 //default:1
	int output_interval_s;                         //default:2
	char app_name[LEN_APP_NAME];
	int running;

	int metric_cnt;
	int metric_size;
	struct metric_t **metric;
	struct{
		struct metric_t **column;
		size_t column_size;
	} table_metrics[NUM_MAX_TABLE];

	int table_num;
	int line_seq;

	int prometheus_output_enable;

	int counter_cnt;
	int gauge_cnt;
	int histogram_cnt;
	int summary_cnt;

	struct timespec last_output_time;

	pthread_t cfg_mon_t;
	char line_protocol_send_buff[UDP_PAYLOAD_SIZE];
	size_t line_protocol_send_buff_offset;
	int line_protocol_socket;

	FILE* fp;
};

enum field_type
{
	FIELD_TYPE_COUNTER,
	FIELD_TYPE_GAUGE,
	FILED_TYPE_HISTOGRAM,
	FIELD_TYPE_SUMMARY
};

struct metric_id_list
{
	int num;
	int id[TABLE_COLUMN_SIZE];
};

/*
 * @param listen_port 
 * @param url if NULL use "/metrics" default
 * */
struct fieldstat_instance * fieldstat_instance_create(void);
int fieldstat_global_enable_prometheus_endpoint(unsigned short listen_port, const char *url);  //TODO
int fieldstat_prometheus_output_enable(struct fieldstat_instance *instance);
int fieldstat_set_statsd_server(struct fieldstat_instance *instance, const char *ip, unsigned short port);
int fieldstat_set_line_protocol_server(struct fieldstat_instance *instance, const char *ip, unsigned short port);
int fieldstat_set_local_output(struct fieldstat_instance *instance, const char *filename, const char *format);//format could be "json" or "default"
int fieldstat_backgroud_thead_disable(struct fieldstat_instance *instance);
int fieldstat_set_output_interval(struct fieldstat_instance *instance, int seconds);//default is 2 seconds
int fieldstat_set_app_name(struct fieldstat_instance *instance, const char *app_name);


/*
 * @param field_name the name of the field
 * @type counter, gauge summary and histogram
 * @n_tag size of tag_key[] and tag_value[]
 * 
 * return field_id to be used in fieldstat_value_xxx(),
 * 	 -1 if an error occured.
 * example field_name=packets, tag=[{"policy_id", "101"},{"profile_id", "a"}]
 * ouput: accumulate value or delta value
 * counter or histogram
 * */
int fieldstat_register(struct fieldstat_instance *instance, enum field_type type, const char *field_name, const char *tag_key[], const char *tag_value[], size_t n_tag);
int fieldstat_register_table(struct fieldstat_instance *instance, enum field_type table_type[], const char *field_list[], size_t n_field);
//int fieldstat_register_table_metrics(struct fieldstat_instance * instance, const char *field_name, const char *tag_key[], const char *tag_value[]);
struct metric_id_list fieldstat_register_table_metrics(struct fieldstat_instance * instance, int table_id, const char *field_name, const char *tag_key[], const char *tag_value[],size_t n_tag);


int fieldstat_value_incrby(struct fieldstat_instance *instance, int field_id, long long increment);
int fieldstat_value_set(struct fieldstat_instance *instance, int field_id, long long increment);
int fieldstat_value_decrby(struct fieldstat_instance *instance, int field_id, long long decrment);

void fieldstat_instance_start(struct fieldstat_instance *instance);
void fieldstat_passive_output(struct fieldstat_instance *instance);

#endif

