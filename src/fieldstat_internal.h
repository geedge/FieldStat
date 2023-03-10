#ifndef	__FIELD_STAT2_INTERNAL_H__
#define	__FIELD_STAT2_INTERNAL_H__

#include <pthread.h>
#include "hdr_histogram.h"
#include "threadsafe_counter.h"

#define	INIT_STAT_FIELD_NUM		1024
#define	MAX_STAT_COLUMN_NUM		64
#define MAX_PATH_LEN			256
#define	UDP_PAYLOAD_SIZE		1460

#define	STATUS_PER_LINE				6
#define	FIELD_PER_LINE				8



#define	HISOTGRAM_EXTRA_INF			0
#define	HISTOGRAM_EXTRA_SUM			1
#define	HISTOGRAM_EXTRA_MAXVAL		2
#define	HISTOGRAM_EXTRA_SIZE		3



#define N_TAG_MAX				32
#define	NUM_INIT_METRICS		1024*16
#define	BLOCK_LIST_SIZE			1024*16


#define URL_MAX_LEN			2048
#define LEN_IP_MAX			32  //?????? 32 from document 
#define LEN_FORMAT_MAX		32
#define LEN_PATH_MAX		256
#define	TABLE_MAX_NUM		64
#define TABLE_COLUMN_SIZE	64
#define	UDP_PAYLOAD_SIZE	1460
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


struct metric_t
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

	int belong_to_table;
	int table_id;
	size_t line_seq;
	int table_column_id;
	char *table_column_name;
	char *table_name;

	union
	{
		struct stat_unit_t counter;
		struct stat_unit_t gauge;
		struct histogram_t histogram;
	};
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
	int column_cnt;

	int line_cnt;
	struct table_line **line_block[BLOCK_LIST_SIZE];
};


struct fieldstat_instance
{
	char name[INSTANCE_NAME_LEN];
	//char *statsd_server_ip;
	//char statsd_server_str_ip[LEN_IP_MAX];
	unsigned int statsd_server_ip;
	unsigned short statsd_server_port;
	int statsd_output_enable;

	//char line_protocol_server_str_ip[LEN_IP_MAX];
	unsigned int line_protocol_server_ip;
	unsigned short line_protocol_server_port;
	int line_protocol_output_enable;

	char local_output_filename[LEN_PATH_MAX];
	char local_output_format[LEN_FORMAT_MAX];
	int local_output_enable;

	int background_thread_disable;                 //default:1
	int output_interval_s;                         //default:2
	int running;

	struct metric_t **metric_block_list[BLOCK_LIST_SIZE];

	int metric_cnt;
	//struct metric_t **metric;

	struct table_metric *table_metrics[TABLE_MAX_NUM];

	int *index_table[TABLE_MAX_NUM][TABLE_COLUMN_SIZE];
	int per_table_line_number[TABLE_MAX_NUM];

	int table_num;
	int line_seq;

	int prometheus_output_enable;

	int histogram_cnt;
	int summary_cnt;

	struct timespec last_output_time;

	pthread_t cfg_mon_t;
	char line_protocol_send_buff[UDP_PAYLOAD_SIZE];
	size_t line_protocol_send_buff_offset;
	int line_protocol_socket;
	pthread_mutex_t output_lock;


	FILE* fp;
};

struct prometheus_endpoint_instance
{
	unsigned short port;
	char *url_path;
	pthread_t tid;
	int running;
	struct http_server_s *server_handle;
	int fs_instance_cnt;
	struct fieldstat_instance **fs_instance; //TO refactor
	int fs_instance_size;                    // number
};



#endif
