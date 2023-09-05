#pragma once

#include <pthread.h>
#include "hdr_histogram.h"
#include "threadsafe_counter.h"
#include "fieldstat.h"
#include <assert.h>
#include <string.h>
#include "cJSON.h"
#include <sys/socket.h>//socket
#include <sys/types.h>//socket
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h> //strerror
#include <errno.h>//strerror
#include <fcntl.h>//fcntl
#include <unistd.h>//fcntl
#include <net/if.h>//fcntl
#include <sys/ioctl.h>//ioctl
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>
#include "uthash.h"

#define	INIT_STAT_FIELD_NUM		1024
#define MAX_PATH_LEN			256

#define	STATUS_PER_LINE				6
#define	FIELD_PER_LINE				8



#define	HISOTGRAM_EXTRA_INF			0
#define	HISTOGRAM_EXTRA_SUM			1
#define	HISTOGRAM_EXTRA_MAXVAL		2
#define	HISTOGRAM_EXTRA_SIZE		3



#define N_TAG_MAX				32
#define	NUM_INIT_METRICS		16384
#define	BLOCK_LIST_SIZE			16384


#define URL_MAX_LEN			2048
#define LEN_IP_MAX			32  //?????? 32 from document 
#define LEN_FORMAT_MAX		32
#define LEN_PATH_MAX		256
#define	TABLE_MAX_NUM		64
#define TABLE_COLUMN_SIZE	64
#define	UDP_PAYLOAD_SIZE	512
#define	LEFT_MIN_BUFF_LEN	1024
#define	REALLOC_SCALE_SIZE	2048
#define STR_LEN_32			32
#define STR_LEN_64			64
#define STR_LEN_256			256
#define STR_LEN_1024		1024
#define NUM_MAX_METRIC_IN_TABLE 1024
#define HISTOGRAM_WIDTH	10
#define INSTANCE_NAME_LEN	32
#define PROMETHEUS_ENDPOINT_DEFAULT_URL "/metrics"
#define	TABLE_LINE_SCALE_NUM		1024

#define METRIC_SIZE	1024

#define CACHE_LINE_SIZE 64

enum field_calc_algo
{
	FS_CALC_CURRENT=0,
	FS_CALC_SPEED
};

enum field_op
{
	FS_OP_ADD=1,
	FS_OP_SET,
	FS_OP_SUB
};


struct stat_unit_t
{
	struct threadsafe_counter changing;
	long long accumulated;
	long long previous_changed;
};
struct histogram_t
{
	struct hdr_histogram* changing;
	struct hdr_histogram* accumulated;
	struct hdr_histogram* previous_changed;
	int64_t lowest_trackable_value;
	int64_t highest_trackable_value;
	int significant_figures;
	int bins_num;
	double *bins;
};

struct table_line
{
	char *name;
	size_t n_tag;
	char *tag_key[N_TAG_MAX];
	char *tag_value[N_TAG_MAX];

	int metric_id_belong_to_line[TABLE_COLUMN_SIZE];
};

struct table_metric
{
	char *name;
	char *column_name[TABLE_COLUMN_SIZE];
	enum field_type column_type[TABLE_COLUMN_SIZE];
	int column_invisible[TABLE_COLUMN_SIZE];
	int column_cnt;

	int line_cnt;
	struct table_line **line_block[BLOCK_LIST_SIZE];
};


struct metric
{
	char *field_name;
	enum field_type field_type;
	size_t n_tag;
	char *tag_key[N_TAG_MAX];
	char *tag_value[N_TAG_MAX];
	int output_scaling;

	int is_invisible;
	int is_ratio;
	int not_send_to_server;
	int numerator_id;
	int denominator_id;
	struct table_metric *table;
	int table_column_id;
	int output_window;

	union
	{
		struct stat_unit_t counter;
		struct stat_unit_t gauge;
		struct histogram_t histogram;
	};
};


struct line_protocol_output
{
	unsigned int server_ip;
	unsigned short server_port;
	int send_socket;
	char send_buf[UDP_PAYLOAD_SIZE];
	unsigned int send_buf_offset;
};

struct fieldstat_instance
{
	char name[INSTANCE_NAME_LEN];
	unsigned int statsd_server_ip;
	unsigned short statsd_server_port;
	int statsd_output_enable;

	char local_output_filename[LEN_PATH_MAX];
	char local_output_format[LEN_FORMAT_MAX];
	int local_output_enable;
	FILE* local_output_fp;

	struct line_protocol_output line_protocol_output;
	int line_protocol_output_enable;

	int background_thread_disable;
	pthread_t background_thread;
	int background_thread_is_created;

	int output_interval_ms;
	int running;

	struct metric **metric_block_list[BLOCK_LIST_SIZE];

	int metric_cnt;
	//struct metric **metric;

	struct table_metric *table_metrics[TABLE_MAX_NUM];

	int table_num;

	int prometheus_output_enable;

	int histogram_cnt;
	int summary_cnt;

	struct timespec last_output_time;
	int output_type;   // 0b0000:not output, 0b1000:output file, 0b0100:output line_protocol, 0b0010: output statsd, 0b0001: output prometheus 
};

struct prometheus_endpoint_instance
{
	unsigned short port;
	char *url_path;
	pthread_t tid;
	int thread_created;
	int running;
	struct http_server_s *server_handle;
	int fs_instance_cnt;
	struct fieldstat_instance **fs_instance; //TO refactor
	int fs_instance_size;                    // number
};

struct dynamic_metric
{
	char metric_key[METRIC_SIZE];
	unsigned int metric_keylen;

	struct metric **metrics;
	UT_hash_handle hh;
};

struct uthash_spinlock
{
	pthread_spinlock_t lock;
} __attribute__((aligned(CACHE_LINE_SIZE)));

struct fieldstat_dynamic_instance
{
	char name[INSTANCE_NAME_LEN];
	struct line_protocol_output line_protocol_output;
	int line_protocol_output_enable;

	int background_thread_disable;
	int output_interval_ms;
	int running;

	struct table_metric *table_metrics[TABLE_MAX_NUM];
	int table_num;
	struct timespec last_output_time;

	pthread_t background_thread;
	int background_thread_is_created;

	struct dynamic_metric **n_thread_dynamic_metric;
	int n_thread;
	int output_type;   // 0b0000:not output, 0b1000:output file, 0b0100:output line_protocol, 0b0010: output statsd, 0b0001: output prometheus 
	//pthread_spinlock_t *uthash_locks;
	struct uthash_spinlock *uthash_locks;
};

void prometheus_endpoint_instance_output(struct http_request_s* request);

char* __str_dup(const char* str);
long long hdr_count_le_value(const struct hdr_histogram* h, long long value);
struct metric * get_metric(struct fieldstat_instance *instance, int metric_id);
long long get_metric_unit_val(struct metric *metric,enum field_calc_algo calc_type,int is_refer);
long long read_metric_current_value(struct metric *metric);
int is_valid_field_name(const char* name);
int is_valid_tags(const struct fieldstat_tag *tags, size_t n_tags);
struct metric ** read_metric_slot(struct fieldstat_instance *instance, int metric_id);
struct metric * metric_new(enum field_type type, const char *field_name, const struct fieldstat_tag tags[], size_t n_tag);
void metric_free(struct metric *metric);
int file_output(struct fieldstat_instance *instance,long long interval_ms);
struct table_line * read_table_line(struct table_metric *table, int line_id);
int send_udp(int sd, unsigned int dest_ip, unsigned short dest_port, const char * data, int len);
int line_protocol_output(struct fieldstat_instance *instance);
void get_current_table_line_cnt(struct fieldstat_instance *instance, int n_table, int *table_line_cnt);
int startup_udp();

void metric_value_operate(struct metric *metric, enum field_op op, long long value);
struct table_metric* table_metric_new(const char *name, const char *column_name[], enum field_type column_type[], size_t n_column);
void table_metric_free(struct table_metric *table);

struct table_line *table_line_new(const char *name, const struct fieldstat_tag tags[],size_t n_tag);
void table_line_free(struct table_line *table_line);
int line_protocol_dynamic_metric_output(struct fieldstat_dynamic_instance *instance);

struct table_line ** read_table_line_slot(struct table_metric *table, int line_id);

void fieldstat_global_disable_prometheus_endpoint();

int enable_line_protocol_output(struct line_protocol_output *line_protocol_output, const char *ip, unsigned short port);
void disable_line_protocol_output(struct line_protocol_output *line_protocol_output);

void escaping_special_chars(char* str);

int escaping_special_chars_cpoy(char *dest, char *src, size_t n);
