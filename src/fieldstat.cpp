#include "fieldstat.h"
#include "fieldstat_internal.h"
#include "threadsafe_counter.h"
#include "hdr_histogram.h"
#include "cJSON.h"
#define HTTPSERVER_IMPL
#include "httpserver.h"

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
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>


int FIELD_STAT_VERSION_2_8_20200805_fix_outOfBound=0;

//Automatically generate the version number
#ifdef __cplusplus
extern "C"
{
#endif

#define GIT_VERSION_CATTER(v) __attribute__((__used__)) const char * GIT_VERSION_##v = NULL
#define GIT_VERSION_EXPEND(v) GIT_VERSION_CATTER(v)

/* VERSION TAG */
#ifdef GIT_VERSION
GIT_VERSION_EXPEND(GIT_VERSION);
#else
static __attribute__((__used__)) const char * GIT_VERSION_UNKNOWN = NULL;
#endif
#undef GIT_VERSION_CATTER
#undef GIT_VERSION_EXPEND

#ifdef __cplusplus
}
#endif
//endof Automatically generate the version number

#define URL_MAX_LEN			2048
#define LEN_IP_MAX			32  //?????? 32 from document 
#define LEN_APP_NAME		32
#define LEN_FORMAT_MAX		32
#define LEN_PATH_MAX		256
#define	NUM_MAX_TABLE		64
#define TABLE_COLUMN_SIZE	32
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


enum field_op
{
	FS_OP_ADD=1,
	FS_OP_SET,
	FS_OP_SUB
};


struct fieldstat_instance
{
	char name[LEN_APP_NAME];
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
	int running;

	struct metric_t **metric_block_list[BLOCK_LIST_SIZE];

	int metric_cnt;
	int metric_size;
	//struct metric_t **metric;
	struct{
		struct metric_t **column;
		size_t column_size;
	} table_metrics[NUM_MAX_TABLE];
	int *index_table[NUM_MAX_TABLE][TABLE_COLUMN_SIZE];
	int per_table_line_number[NUM_MAX_TABLE];

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

struct prometheus_endpoint_instance g_prometheus_endpoint_instance = 
{
	9273,
	NULL,
	0,
	0,
	NULL,
	0,
	NULL,
	REALLOC_SCALE_SIZE,
};

const char* draw_line="________________________________________________________________________________________________________________________________________________";
const char* draw_boundary="============================================================";

static char* __str_dup(const char* str)
{
	char* dup=NULL;
	dup=(char*)calloc(sizeof(char),strlen(str)+1);
	memcpy(dup, str, strlen(str));
	return dup;
}

int is_valid_field_name(const char* name)
{
	const char* reserverd="|:\n\r. \t<>[]#!@";
	unsigned int i=0,j=0;
	for(i=0;i<strlen(name);i++)
	{
		for(j=0;j<strlen(reserverd);j++)
		if(name[i]==reserverd[j])
		{
			return 0;
		}
	}
	return 1;
}

static char* str_unescape(char* s, char *d, int d_len)
{
	int i=0,j=0;
	int len=strlen(s);
	for(i=0; i<len && j<d_len; i++)
	{
		if(s[i]=='(' || s[i]==')'  || s[i]=='{' || s[i]=='}' || 
		   s[i]=='/' || s[i]=='\\' || s[i]=='%' || s[i]=='*' || 
		   s[i]=='$' || s[i]=='-'  || s[i]==',' || s[i]==';')
		{
			if(i==0)
			{
				continue;
			}

			d[j++]='_';
		}
		else
		{
			d[j++]=s[i];
		}
	}

	if(d[j-1]=='_')
	{
		j-=1;
	}

	d[j]='\0';
	
	return 0;
}


struct metric_t * get_metric(struct fieldstat_instance *instance, int metric_id)
{
	int block_index = 0;
	int block_in_index = 0;
	struct metric_t ** metrics_array = NULL;
	struct metric_t * metric = NULL;

	if(instance == NULL)
	{
		return NULL;
	}

	block_index = metric_id / NUM_INIT_METRICS;
	block_in_index = metric_id % NUM_INIT_METRICS;
	metrics_array = instance->metric_block_list[block_index];
	metric = metrics_array[block_in_index];

	return metric;
}


struct metric_t* metric_new(enum field_type type, const char *field_name, const char *tag_key[], const char *tag_value[], size_t n_tag)
{
	int i = 0;
	struct metric_t* metric=(struct metric_t*)calloc(sizeof(struct metric_t),1);
	metric->field_name =__str_dup(field_name);
	metric->field_type = type;
	metric->is_ratio = 0;
	metric->output_scaling = 1;
	metric->n_tag = n_tag;

	for(i = 0; i < (int)n_tag; i++)
	{
		metric->tag_key[i]   = strdup(tag_key[i]);
		metric->tag_value[i] = strdup(tag_value[i]);
	}
	return metric;
}

struct metric_t * create_metric(struct fieldstat_instance *instance, int metric_id,enum field_type type, const char *field_name, const char *tag_key[], const char *tag_value[], size_t n_tag)
{
	int block_index = 0;
	int block_in_index = 0;
	struct metric_t ** metrics_array = NULL;
	struct metric_t * metric = NULL;

	if(instance == NULL)
	{
		return NULL;
	}
	block_index = metric_id / NUM_INIT_METRICS;
	block_in_index = metric_id % NUM_INIT_METRICS;
	if(block_in_index == 0 && instance->metric_block_list[block_index] == NULL)
	{
		instance->metric_block_list[block_index] = (struct metric_t **)calloc(sizeof(struct metric *), NUM_INIT_METRICS);	
	}
	else
	{
		while (instance->metric_block_list[block_index] == NULL);
	}
	metrics_array = (struct metric_t **)instance->metric_block_list[block_index];
	//assert(instance->metric_cnt < instance->metric_size);
	metric = metrics_array[block_in_index] = metric_new(type,field_name,tag_key,tag_value,n_tag);
	return metric;
}

void metric_free(struct metric_t* metric)
{
	int i = 0;

	free(metric->field_name);
	metric->field_name = NULL;

	for(i = 0; i < (int)metric->n_tag; i++)
	{
		free(metric->tag_key[i]);
		metric->tag_key[i] = NULL;

		free(metric->tag_value[i]);
		metric->tag_value[i] = NULL;
	}
	metric->n_tag = 0;

	free(metric);

	return;
}

static int startup_udp()
{
	int sd_udp=-1;
	int flags;
 	if(-1==(sd_udp = socket(AF_INET, SOCK_DGRAM, 0)))
	{
		printf("FS2: socket error: %d %s, restart socket.", errno, strerror(errno));
		sd_udp=-1;
	}
	flags=fcntl(sd_udp,F_GETFL);
	flags|=O_NONBLOCK;
	if(fcntl(sd_udp,F_SETFL,flags)==-1)
	{
		printf("FS2: socket error: %d %s, restart socket.", errno, strerror(errno));
		sd_udp=-1;
	}
	int opt=1;
	if (setsockopt (sd_udp, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt) ) < 0) {
		printf("FS2:setsockopt error: %d %s, restart socket.", errno, strerror(errno));
		close (sd_udp);
		sd_udp=-1;
		return sd_udp;
	}

	return sd_udp;
}

static int send_udp(int sd, unsigned int dest_ip, unsigned short dest_port, const char * data, int len)
{
	int to_send_len=len;
	int already_sended_len=0,this_sended_len=0;
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr =dest_ip;
	addr.sin_port = htons(dest_port);

	while(to_send_len>already_sended_len)
	{
		this_sended_len=sendto(sd,(void*)(data+already_sended_len),
								to_send_len-already_sended_len,
								0,
								(struct sockaddr *)&(addr),
								sizeof(addr));
		if(this_sended_len==-1)
		{
			if((EAGAIN == errno)||( EINTR == errno )|| (EWOULDBLOCK==errno))
			{
				continue;
			}
			else
			{
				printf("FS2: at send,socket error: %d %s", errno, strerror(errno));
				return -1;
			}
		}
		already_sended_len=+this_sended_len;			
	}
	
	return 0;
}

int fieldstat_set_output_interval(struct fieldstat_instance *instance, int seconds)
{
	if(instance->running == 1 || seconds <= 0 )
	{
		return -1;
	}
	instance->output_interval_s = seconds;
	return 0;
}

int fieldstat_backgroud_thead_disable(struct fieldstat_instance *instance)
{
	if(instance->running == 1)
	{
		return -1;
	}
	instance->background_thread_disable = 1;
	return 0;
}


int fieldstat_set_local_output(struct fieldstat_instance *instance, const char *filename, const char *format)
{
	int len_filename = strlen(filename);
	int len_format   = strlen(format);

	if(instance->running == 1)
	{
		return -1;
	}
	if(strcmp(format,"default") != 0 && strcmp(format,"json") != 0)
	{
		return -1;
	}
	if(len_filename <= 0 || len_filename >= LEN_PATH_MAX)
	{
		return -1;
	}
	if(len_format <= 0 || len_format >= LEN_FORMAT_MAX)
	{
		return -1;
	}
	instance->line_protocol_socket = startup_udp();
	strncpy(instance->local_output_filename, (char *)filename, len_filename);
	strncpy(instance->local_output_format, (char *)format, len_format);
	instance->local_output_enable  = 1;

	return 0;
}

int fieldstat_set_line_protocol_server(struct fieldstat_instance *instance, const char *ip, unsigned short port)
{
	int len_ip = strlen(ip);

	if(instance->running == 1)
	{
		return -1;
	}
	if(len_ip <= 0 || len_ip >= LEN_IP_MAX)
	{
		return -1;
	}
	if(1 != inet_pton(AF_INET, ip, (void *)&(instance->line_protocol_server_ip)))
	{
		return -1;
	}

	strncpy(instance->line_protocol_server_str_ip,(char *)ip,len_ip);
	instance->line_protocol_server_port   = port;
	instance->line_protocol_output_enable = 1;

	return 0;
}

int fieldstat_set_statsd_server(struct fieldstat_instance *instance, const char *ip, unsigned short port)
{
	int len_ip = strlen(ip);
	if(instance->running == 1)
	{
		return -1;
	}
	if(len_ip <= 0 || len_ip >= LEN_IP_MAX)
	{
		return -1;
	}

	if(1 != inet_pton(AF_INET, ip, (void *)&(instance->statsd_server_ip)))
	{
		return -1;
	}

	strncpy(instance->statsd_server_str_ip,(char *)ip,len_ip);
	instance->statsd_server_port   = port;
	instance->statsd_output_enable = 1;
	return 0;
}

int fieldstat_register(struct fieldstat_instance *instance, enum field_type type, const char *field_name, const char *tag_key[], const char *tag_value[], size_t n_tag)
{
	int metric_id = 0;
	struct metric_t * metric_choosen = NULL;
	if(!is_valid_field_name(field_name))
	{
		return -1;
	}
	if(n_tag > N_TAG_MAX)
	{
		return -1;
	}
	//TODO not block
	metric_id = atomic_inc(&instance->metric_cnt) - 1;	
	metric_choosen = create_metric(instance, metric_id, type,field_name,tag_key,tag_value,n_tag);
	//assert(instance->metric_cnt < instance->metric_size);
	//metric_choosen = instance->metric[metric_id] = metric_new(type,field_name,tag_key,tag_value,n_tag);
	//metric_choosen = metrics_array[block_in_index] = metric_new(type,field_name,tag_key,tag_value,n_tag);
	switch(type)
	{
		case FIELD_TYPE_COUNTER:
			instance->counter_cnt++;
			memset(&(metric_choosen->counter), 0, sizeof(metric_choosen->counter));
			break;
		case FIELD_TYPE_GAUGE:
			instance->gauge_cnt++;
			memset(&(metric_choosen->gauge), 0, sizeof(metric_choosen->gauge));
			break;
		case FILED_TYPE_HISTOGRAM:
			//instance->histogram_cnt++;
			// TODO  what?
			break;
		case FIELD_TYPE_SUMMARY:
			//instance->summary_cnt++;
			//TODO what ?
			break;
		default:
			assert(0);
	}
	return metric_id;
}


//long long get_metric_unit_val(display_manifest_t* p, int column_seq,enum field_calc_algo calc_type,int is_refer)
long long get_metric_unit_val(struct metric_t *metric,enum field_calc_algo calc_type,int is_refer)
{
	stat_unit_t* target = NULL;
	long long value = 0;
	switch(metric->field_type)
	{
		case FIELD_TYPE_COUNTER:
			target = &(metric->counter);
			break;
		case FIELD_TYPE_GAUGE:
			target = &(metric->gauge);
			break;
		case FILED_TYPE_HISTOGRAM:
		case FIELD_TYPE_SUMMARY:
		default:
			break;
	}
	value = threadsafe_counter_read(&(target->changing));
	//value= threadsafe_counter_read(&(target->changing));
	if(is_refer == 0)
	{
		target->previous_changed = value;
		target->accumulated += value;
		threadsafe_counter_set(&(target->changing), 0);
	}
	switch(calc_type)
	{
		case	FS_CALC_CURRENT:
			value=target->accumulated;
			break;
		case	FS_CALC_SPEED:
			value=target->previous_changed;
			break;
		default:
			assert(0);
	}
	return value;
}


void flush_line_protocol_metric(struct fieldstat_instance *instance)
{
	if(instance->line_protocol_send_buff_offset == 0)
	{
		return;
	}

	if(instance->line_protocol_server_ip > 0 && instance->line_protocol_server_port > 0)
	{
		send_udp(instance->line_protocol_socket, instance->line_protocol_server_ip,(unsigned short)instance->line_protocol_server_port, instance->line_protocol_send_buff, instance->line_protocol_send_buff_offset);
	}
	instance->line_protocol_send_buff_offset = 0;
	memset(instance->line_protocol_send_buff, 0, sizeof(instance->line_protocol_send_buff));	
	return;
}

void append_line_protocol_line(struct fieldstat_instance *instance, const char* measurement, char *tag_set, char *field_set)
{
	if(field_set==NULL)
	{
		return;
	}
	if(UDP_PAYLOAD_SIZE - (unsigned int)instance->line_protocol_send_buff_offset < strlen(measurement) + strlen(field_set) + strlen(tag_set) + 2)
	{
		flush_line_protocol_metric(instance);
	}
	printf("Line_protocol metric: %s%s %s\n",measurement,tag_set,field_set);
	instance->line_protocol_send_buff_offset += snprintf(instance->line_protocol_send_buff + instance->line_protocol_send_buff_offset,
														sizeof(instance->line_protocol_send_buff) - instance->line_protocol_send_buff_offset,
														"%s%s %s\n",
														measurement, tag_set, field_set);
	return;
}

static int output_line_protocol_tag_set_buf(metric_t *metric, char *tag_set_buff, unsigned int size)
{
	int i = 0;
	char *tag_set_buff_append = tag_set_buff;
	for(i = 0; i < (int)metric->n_tag; i++)
	{
		tag_set_buff_append += snprintf(tag_set_buff_append, size - (tag_set_buff_append - tag_set_buff), ",%s=%s", metric->tag_key[i],metric->tag_value[i]);
	}
	return tag_set_buff_append - tag_set_buff;
}



static void output_line_protocol_table(struct fieldstat_instance *instance)
{
	int i = 0, j = 0, k = 0;
	metric_t *metric = NULL;
	long long value = 0;
	//double ratio = 0.0;
	size_t table_column_size = 0;
	int metric_id = 0;
	char field_set_buff[UDP_PAYLOAD_SIZE];
	char tag_set_buff[UDP_PAYLOAD_SIZE];

	memset(field_set_buff, 0, sizeof(field_set_buff));
	memset(tag_set_buff,  0, sizeof(tag_set_buff));
	char *tag_set_buff_append = tag_set_buff;
	char *field_set_buff_append = field_set_buff;
	int *table_line = NULL;

	for(i = 0; i < instance->table_num; i++)
	{
		table_column_size = instance->table_metrics[i].column_size;

		for(j = 0; j < instance->per_table_line_number[i]; j++)
		{
			table_line = instance->index_table[i][j];

			for(k = 0; k < (int)table_column_size; k++)
			{
				metric_id = table_line[k];
				metric = get_metric(instance, metric_id);

				switch(metric->field_type)
				{
					case FIELD_TYPE_GAUGE:
						value = get_metric_unit_val(metric, FS_CALC_CURRENT, 1);
						break;
					case FIELD_TYPE_COUNTER:
						value = get_metric_unit_val(metric, FS_CALC_SPEED, 1);
						break;
					default:
						break;
				}

				field_set_buff_append += snprintf(field_set_buff_append, sizeof(field_set_buff) - (field_set_buff - field_set_buff), "%s=%lld,", metric->table_column_name, value);
			}

			tag_set_buff_append += snprintf(tag_set_buff_append, sizeof(tag_set_buff) - (tag_set_buff_append - tag_set_buff), ",app_name=%s", instance->name);
			output_line_protocol_tag_set_buf(metric, tag_set_buff_append, sizeof(tag_set_buff) - (tag_set_buff_append - tag_set_buff));

			if(field_set_buff_append - field_set_buff > 0)
			{
				field_set_buff[field_set_buff_append - field_set_buff - 1] = '\0';
			}
			append_line_protocol_line(instance, metric->field_name, tag_set_buff, field_set_buff);
			tag_set_buff_append = tag_set_buff;
			field_set_buff_append = field_set_buff;
		}
	}
}

int line_protocol_output(struct fieldstat_instance *instance)
{
	metric_t *metric = NULL;
	long long value=0;
	int i=0;
	char field_set_buff[UDP_PAYLOAD_SIZE];
	char tag_set_buff[UDP_PAYLOAD_SIZE];

	memset(field_set_buff, 0, sizeof(field_set_buff));
	memset(tag_set_buff,  0, sizeof(tag_set_buff));
	char *tag_set_buff_append = tag_set_buff;

	for(i = 0; i < instance->metric_cnt; i++)
	{
		metric = get_metric(instance, i);
		if(metric->is_ratio == 1)
		{
			continue;
		}

		if(metric->belong_to_table == 1)
		{
			continue;
		}

		switch(metric->field_type)
		{
			case FIELD_TYPE_GAUGE:
				value = get_metric_unit_val(metric, FS_CALC_CURRENT, 1);
				if(value != 0)
				{
					snprintf(field_set_buff, UDP_PAYLOAD_SIZE, "%s=%lld", metric->field_name, value);
					tag_set_buff_append += snprintf(tag_set_buff_append, sizeof(tag_set_buff) - (tag_set_buff_append - tag_set_buff), ",app_name=%s", instance->name);
					/*
					for(j = 0; j < (int)metric->n_tag; j++)
					{
						tag_set_buff_append += snprintf(tag_set_buff_append, sizeof(tag_set_buff) - (tag_set_buff_append - tag_set_buff), ",%s=%s", metric->tag_key[i],metric->tag_value[i]);
					}
					*/
					output_line_protocol_tag_set_buf(metric, tag_set_buff_append, sizeof(tag_set_buff) - (tag_set_buff_append - tag_set_buff));
					append_line_protocol_line(instance, metric->field_name, tag_set_buff, field_set_buff);
					tag_set_buff_append = tag_set_buff;

				}
				break;
			case FIELD_TYPE_COUNTER:
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 1);
				if(value != 0)
				{
					snprintf(field_set_buff, UDP_PAYLOAD_SIZE, "%s=%lld", metric->field_name, value);
					tag_set_buff_append += snprintf(tag_set_buff_append, sizeof(tag_set_buff) - (tag_set_buff_append - tag_set_buff), ",app_name=%s", instance->name);
					/*
					for(j = 0; j < (int)metric->n_tag; j++)
					{
						tag_set_buff_append += snprintf(tag_set_buff_append, sizeof(tag_set_buff) - (tag_set_buff_append - tag_set_buff), ",%s=%s", metric->tag_key[i],metric->tag_value[i]);
					}
					*/
					output_line_protocol_tag_set_buf(metric, tag_set_buff_append, sizeof(tag_set_buff) - (tag_set_buff_append - tag_set_buff));
					append_line_protocol_line(instance, metric->field_name, tag_set_buff, field_set_buff);
					tag_set_buff_append = tag_set_buff;
				}
				break;
			default:
				break;

		}
	}

	output_line_protocol_table(instance);
	flush_line_protocol_metric(instance);

	return 0;
}

static int print_buf_tag_append_position(metric_t *metric, char *print_buf_tags, unsigned int size)
{
	int i = 0;
	char *print_buf_tags_append_position = print_buf_tags;

	print_buf_tags_append_position += snprintf(print_buf_tags_append_position, size - (print_buf_tags_append_position - print_buf_tags),"{");
	for(; i < (int)metric->n_tag; i++)
	{
		if(i == 0)
		{
			print_buf_tags_append_position += snprintf(print_buf_tags_append_position, size - (print_buf_tags_append_position - print_buf_tags),"%s=\"%s\"", metric->tag_key[i],metric->tag_value[i]);
		}
		else
		{
			print_buf_tags_append_position += snprintf(print_buf_tags_append_position, size - (print_buf_tags_append_position - print_buf_tags),",%s=\"%s\"", metric->tag_key[i],metric->tag_value[i]);
		}
	}
	print_buf_tags_append_position += snprintf(print_buf_tags_append_position, size - (print_buf_tags_append_position - print_buf_tags),"}");

	return print_buf_tags_append_position - print_buf_tags;
}


static int output_file_format_default_type_gauge(struct fieldstat_instance *instance,long long interval_ms,char *print_buf, unsigned int size)
{
	int i = 0, j = 0;
	//display_manifest_t* p = NULL;
	metric_t *metric = NULL;
	long long value = 0;
	//double ratio = 0.0;
	//char* pos=print_buf;
	char *print_buf_append_position = print_buf;
	char print_buf_tags[1024];

	for(i = 0; i < instance->metric_cnt; i++)
	{
		//metric = instance->metric[i];
		metric = get_metric(instance, i);
		if(metric->field_type != FIELD_TYPE_GAUGE)
		{
			continue;
		}
		if(metric->belong_to_table == 1)
		{
			continue;
		}
		if(metric->is_invisible == 1)
		{
			value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
			continue;
		}
		/*
		if(metric->is_ratio==1)
		{
			ratio=get_stat_ratio(_handle->display[p->numerator_id],  _handle->display[p->denominator_id], NULL,
								p->output_scaling, p->calc_type);
			pos+=snprintf(pos,size-(pos-print_buf),"%s: %10.2e\t",p->name,ratio);
		}
		*/
		value = get_metric_unit_val(metric, FS_CALC_CURRENT, 0);
		//value=value * metric->output_scaling * 1000 / interval_ms;
		memset(print_buf_tags,0, sizeof(print_buf_tags));
		print_buf_tag_append_position(metric, print_buf_tags, sizeof(print_buf_tags));
		print_buf_append_position += snprintf(print_buf_append_position, size - (print_buf_append_position - print_buf), "%s %s: %-10lld\t", metric->field_name, print_buf_tags, value);
		j++;
		if(j == STATUS_PER_LINE)
		{
			print_buf_append_position += snprintf(print_buf_append_position, size - (print_buf_append_position - print_buf),"\n");
			j=0;
		}
	}

	if(print_buf_append_position - print_buf > 0)
	{
		if(*(print_buf_append_position - 1) == '\n')
		{
			print_buf_append_position --;
		}
		print_buf_append_position += snprintf(print_buf_append_position, size - (print_buf_append_position-print_buf),"\n%s\n",draw_line);
	}
	return print_buf_append_position - print_buf;
}


static int output_file_format_default_type_counter(struct fieldstat_instance *instance,long long interval_ms,char*print_buf, unsigned int size)
{
	int i=0,j=0;
	//display_manifest_t* p=NULL;
	metric_t *metric = NULL;
	long long value = 0;
	//double ratio = 0.0;
	char* print_buf_append_position = print_buf;
	int metric_id[INIT_STAT_FIELD_NUM] = {0};
	int metric_cnt = 0;
	char print_buf_tags[1024];

	for(i = 0;i < instance->metric_cnt; i++)
	{
		//p=_handle->display[i];
		//metric = instance->metric[i];
		metric = get_metric(instance, i);
		if(metric->field_type != FIELD_TYPE_COUNTER)
		{
			continue;
		}
		if(metric->belong_to_table == 1)
		{
			continue;
		}
		if(metric->is_invisible == 1)
		{
			get_metric_unit_val(metric,FS_CALC_CURRENT,0);
			continue;
		}
		metric_id[metric_cnt] = i;
		metric_cnt++;
	}

	for(i = 0; i < metric_cnt; i++)
	{
		print_buf_append_position += snprintf(print_buf_append_position, size - (print_buf_append_position - print_buf),"\t");

		for(j = 0; j < FIELD_PER_LINE && i+j < metric_cnt; j++)
		{
			//metric = instance->metric[metric_id[i+j]];
			metric = get_metric(instance, metric_id[i+j]);
			memset(print_buf_tags,0, sizeof(print_buf_tags));
			print_buf_tag_append_position(metric, print_buf_tags, sizeof(print_buf_tags));
			print_buf_append_position += snprintf(print_buf_append_position, size - (print_buf_append_position - print_buf), "%10s %s\t", metric->field_name, print_buf_tags);
		}
		print_buf_append_position += snprintf(print_buf_append_position, size - (print_buf_append_position-print_buf), "\nsum\t");

		for(j=0; j < FIELD_PER_LINE && i+j < metric_cnt; j++)
		{
			//metric = instance->metric[metric_id[i+j]];
			metric = get_metric(instance, metric_id[i+j]);
			value  = get_metric_unit_val(metric,FS_CALC_CURRENT, 1);
			print_buf_append_position += snprintf(print_buf_append_position, sizeof(print_buf) - (print_buf_append_position - print_buf), "%10lld\t", value);
		}
		print_buf_append_position += snprintf(print_buf_append_position, sizeof(print_buf) - (print_buf_append_position - print_buf), "\nspeed/s\t");

		for(j=0;j<FIELD_PER_LINE&&i+j<metric_cnt;j++)
		{
			//metric = instance->metric[metric_id[i+j]];
			metric = get_metric(instance, metric_id[i+j]);
			value  = get_metric_unit_val(metric,FS_CALC_SPEED, 0);
			print_buf_append_position += snprintf(print_buf_append_position, size - (print_buf_append_position - print_buf), "%10lld\t", value*1000/interval_ms);
		}
		i += (j-1);
		print_buf_append_position += snprintf(print_buf_append_position, size - (print_buf_append_position - print_buf), "\n");
	}

	if(print_buf_append_position - print_buf > 0)
	{
		if(*(print_buf_append_position - 1)=='\n')
		{
			print_buf_append_position--;
		}
		print_buf_append_position += snprintf(print_buf_append_position, size - (print_buf_append_position - print_buf),"\n%s\n", draw_line);
	}

	return print_buf_append_position - print_buf;
}


static int output_file_format_default_table(struct fieldstat_instance *instance,long long interval_ms,char*print_buf, unsigned int size)
{
	int i = 0, j = 0, k = 0;
	metric_t *metric = NULL;
	long long value = 0;
	//double ratio = 0.0;
	struct metric_t **table_column = NULL;
	size_t table_column_size = 0;

	char* print_buf_append_position = print_buf;

	int table_metric_number[NUM_MAX_TABLE];
	memset(table_metric_number,0, sizeof(table_metric_number));
	int metric_id = 0;
	char print_buf_tags[1024] = {0};
	int *table_line = NULL;

	for(i = 0; i < instance->table_num; i++)
	{
		table_column = instance->table_metrics[i].column;
		table_column_size = instance->table_metrics[i].column_size;

		print_buf_append_position += snprintf(print_buf_append_position, size - (print_buf - print_buf),"\t\t\t");
		for(k = 0; k < (int)table_column_size; k++)
		{
			print_buf_append_position += snprintf(print_buf_append_position ,size - (print_buf_append_position - print_buf), "\t%10s", table_column[k]->field_name);			
		}

		for(j = 0; j < instance->per_table_line_number[i]; j++)
		{

			table_line = instance->index_table[i][j];
			for(k = 0; k < (int)table_column_size; k ++)
			{
				metric_id = table_line[k];
				metric = get_metric(instance ,metric_id);
				if(k == 0)
				{
					print_buf_tag_append_position(metric, print_buf_tags, sizeof(print_buf_tags));
					print_buf_append_position += snprintf(print_buf_append_position, size - (print_buf_append_position - print_buf), "\n%s %-20s\t", metric->field_name,print_buf_tags);
				}
				value = get_metric_unit_val(metric, FS_CALC_CURRENT, 0);
				print_buf_append_position += snprintf(print_buf_append_position, size - (print_buf_append_position - print_buf), "%10lld\t", value);
			}
		}
		print_buf_append_position += snprintf(print_buf_append_position,size - (print_buf_append_position - print_buf), "\n");

		if(print_buf_append_position - print_buf > 0)
		{
			if(*(print_buf_append_position - 1) == '\n')
			{
				print_buf_append_position--;
			}
			print_buf_append_position += snprintf(print_buf_append_position, size - (print_buf_append_position - print_buf), "\n%s\n", draw_line);
		}
	}
	return print_buf_append_position - print_buf;
}

static int output_file_print_hdr_head(struct metric_t *metric, char *print_buf, size_t size)
{
	char * pos = print_buf;
	char bin_format[STR_LEN_256], str_format[STR_LEN_256];
	const char* extra[]={"MAX", "MIN", "AVG", "STDDEV", "CNT"};
	char buff[STR_LEN_32];
	int i=0;
	double * bins = metric->histogram.bins;
	int bins_num  = metric->histogram.bins_num;
	if(metric->field_type == FIELD_TYPE_SUMMARY)
	{
		snprintf(bin_format, sizeof(bin_format), "%%%d.2lf%%%%", HISTOGRAM_WIDTH-1);
	}
	if(metric->field_type == FILED_TYPE_HISTOGRAM)
	{
		snprintf(bin_format, sizeof(bin_format), "le=%%0.0f");
	}
	snprintf(str_format, sizeof(str_format), "%%%ds", HISTOGRAM_WIDTH);
	if(metric->field_type == FIELD_TYPE_SUMMARY)
	{
		pos+=snprintf(pos,size-(pos-print_buf),"%-8s\t","summary");
	}
	if(metric->field_type == FILED_TYPE_HISTOGRAM)
	{
		pos+=snprintf(pos,size-(pos-print_buf),"%-8s\t","histogram");
	}
	for(i = 0;i < bins_num; i++)
	{
		if(metric->field_type == FIELD_TYPE_SUMMARY)
		{
			snprintf(buff,sizeof(buff),bin_format,bins[i]*100);
		}
		if(metric->field_type == FILED_TYPE_HISTOGRAM)
		{
			snprintf(buff,sizeof(buff),bin_format,bins[i]);
		}
		pos+=snprintf(pos,size-(pos-print_buf),str_format, buff);
	}
	for(i=0;(unsigned int)i<sizeof(extra)/sizeof(extra[0]);i++)
	{
		pos+=snprintf(pos,size-(pos-print_buf),str_format, extra[i]);
	}
	pos+=snprintf(pos,size-(pos-print_buf),"\n");
	return pos-print_buf;
}


long long hdr_count_le_value(const struct hdr_histogram* h, long long value)
{
	struct hdr_iter iter;
	long long count = 0;
	hdr_iter_recorded_init(&iter, h);
	while (hdr_iter_next(&iter))
	{
		if((long long)iter.value <= value)
			count ++;
	}
	return count;
}
static int output_file_print_hdr_unit(struct metric_t *metric, char*print_buf, size_t size)
{
	double * bins = metric->histogram.bins;
	int bins_num  = metric->histogram.bins_num;
	char* pos=print_buf;
	long long value=0;
	int i=0;
	struct histogram_t* h=&(metric->histogram);
	struct hdr_histogram* h_out=NULL, *h_tmp=NULL;
	char int_format[STR_LEN_256], double_format[STR_LEN_256];
	snprintf(int_format, sizeof(int_format), "%%%dlld",HISTOGRAM_WIDTH);
	snprintf(double_format, sizeof(double_format), "%%%d.2lf",HISTOGRAM_WIDTH);

	hdr_init(h->lowest_trackable_value,  h->highest_trackable_value, h->significant_figures, &(h_tmp));
	if(h->previous_changed!=NULL)	hdr_close(h->previous_changed);

	h->previous_changed=atomic_read(&(h->changing));
	h_tmp=atomic_set(&(h->changing), h_tmp);// left h_tmp is used to avoid warining [-Wunused-value]

	hdr_add(h->accumulated, h->previous_changed);
	h_out=h->accumulated; //TODO
/*
	if(metric->calc_type==FS_CALC_SPEED)
	{
		h_out=h->previous_changed;
	}
	else
	{
		h_out=h->accumulated;
	}
*/
	pos+=snprintf(pos,size-(pos-print_buf),"%-10s\t", metric->field_name);

	for(i=0;i<bins_num;i++)
	{
		if(metric->field_type == FIELD_TYPE_SUMMARY)
		{
			value=(long long)hdr_value_at_percentile(h_out, bins[i]*100);
		}
		if(metric->field_type == FILED_TYPE_HISTOGRAM)
		{
			value= hdr_count_le_value(h_out, (long long)bins[i]);
		}
		pos+=snprintf(pos,size-(pos-print_buf),int_format, value);
	}
	pos+=snprintf(pos,size-(pos-print_buf),int_format,h_out->total_count==0?0:(long long)hdr_max(h_out));
	pos+=snprintf(pos,size-(pos-print_buf),int_format,h_out->total_count==0?0:(long long)hdr_min(h_out));
	pos+=snprintf(pos,size-(pos-print_buf),double_format,h_out->total_count==0?0:hdr_mean(h_out));
	pos+=snprintf(pos,size-(pos-print_buf),double_format,h_out->total_count==0?0:hdr_stddev(h_out));
	pos+=snprintf(pos,size-(pos-print_buf),int_format,(long long)h_out->total_count);

	pos+=snprintf(pos,size-(pos-print_buf),"\n");

	h_tmp=NULL;

	return pos-print_buf;
}

static int output_file_format_default_type_histogram(struct fieldstat_instance *instance, enum field_type type, long long interval_ms, char*print_buf, size_t size)
{
	int i = 0, j = 0, metric_num = 0;
	char *pos = print_buf;
	//display_manifest_t* p=NULL;
	struct metric_t *metric = NULL;
	struct metric_t  *metric_array[INIT_STAT_FIELD_NUM] = {NULL};
	int metric_is_print[INIT_STAT_FIELD_NUM] = {0};

	if(type == FILED_TYPE_HISTOGRAM)
	{
		if(instance->histogram_cnt == 0)
		{
			return 0;
		}
	}

	if(type == FIELD_TYPE_SUMMARY)
	{
		if(instance->summary_cnt == 0)
		{
			return 0;
		}
	}

	for(i = 0; i < instance->metric_cnt; i ++)
	{
		metric = get_metric(instance, i);
		if(metric->field_type != type)
		{
			continue;
		}
		metric_array[metric_num++] = metric;	

	}
	for(i = 0; i < metric_num; i++)
	{
		if(metric_is_print[i] == 1)
		{
			continue;
		}
		pos += output_file_print_hdr_head(metric_array[i], pos, size-(pos-print_buf));
		for(j = i; j < metric_num; j ++)
		{
			if(metric_array[j] == NULL)
			{
				continue;
			}
			if(metric_array[i]->histogram.bins_num != metric_array[j]->histogram.bins_num)
			{
				continue;
			}
			if(memcmp(metric_array[i]->histogram.bins, metric_array[j]->histogram.bins, metric_array[i]->histogram.bins_num))
			{
				continue;
			}
			pos += output_file_print_hdr_unit(metric_array[j], pos, size-(pos-print_buf));
			metric_is_print[j] = 1;
		}
		if(pos-print_buf>0)
		{
			if(*(pos-1)=='\n')
			{
				pos--;
			}
			pos+=snprintf(pos,size-(pos-print_buf),"\n%s\n", draw_line);
		}
	}
	return pos-print_buf;	
}


int fieldstat_output_file(struct fieldstat_instance *instance,long long interval_ms)
{
	size_t print_buf_sz = instance->metric_cnt*1024;
	char *print_buf = NULL;
	char *print_buf_append_position = NULL;
	time_t current = 0;
	char ctime_buff[STR_LEN_32]={0};

	if(instance->fp == NULL)
	{
		instance->fp = fopen(instance->local_output_filename, "w");
		if(instance->fp == NULL)
		{
			printf("Field Stat: open %s failed.\n",instance->local_output_filename);
			assert(0);
			return -1;
		}
	}

	if(!strcmp(instance->local_output_format, "default"))
	{
		time(&current);
		ctime_r(&current, ctime_buff);
		print_buf = (char*)calloc(sizeof(char), print_buf_sz);
		print_buf_append_position = print_buf;
		print_buf_append_position += snprintf(print_buf_append_position, print_buf_sz - (print_buf_append_position - print_buf), "%s%s", draw_boundary, ctime_buff);
		print_buf_append_position --;//jump '\n' generate by ctime()
		print_buf_append_position += snprintf(print_buf_append_position, print_buf_sz - (print_buf_append_position - print_buf),"%s\n",draw_boundary);

		//pthread_mutex_lock(&(_handle->reg_lock)); //TODO
		print_buf_append_position += output_file_format_default_type_gauge(instance, interval_ms, print_buf_append_position, print_buf_sz - (print_buf_append_position - print_buf));
		print_buf_append_position += output_file_format_default_type_counter(instance, interval_ms, print_buf_append_position, print_buf_sz - (print_buf_append_position - print_buf));
		print_buf_append_position += output_file_format_default_table(instance, interval_ms, print_buf_append_position, print_buf_sz - (print_buf_append_position - print_buf));
		print_buf_append_position += output_file_format_default_type_histogram(instance, FIELD_TYPE_SUMMARY, interval_ms, print_buf_append_position, print_buf_sz - (print_buf_append_position - print_buf));
		print_buf_append_position += output_file_format_default_type_histogram(instance, FILED_TYPE_HISTOGRAM,  interval_ms, print_buf_append_position, print_buf_sz - (print_buf_append_position - print_buf));
		//TODO  output table,output histogram,output summary 
		//pthread_mutex_unlock(&(_handle->reg_lock));//TODO
	}

    if(!strcmp(instance->local_output_format, "json"))
    {
		//TODO from json output
    }

	fseek(instance->fp,0,SEEK_SET);
	fwrite(print_buf,print_buf_append_position - print_buf,1,instance->fp);

	fflush(instance->fp);

	if(print_buf)
	{
		free(print_buf);
		print_buf = NULL;
	}
	return 0;
}


void fieldstat_passive_output(struct fieldstat_instance *instance)
{
	struct timespec this_output_time;
	long long interval_ms = 0;
	int ret = 0;

	if(instance->running == 0)
	{
		return;
	}

	clock_gettime(CLOCK_MONOTONIC ,&this_output_time);
	interval_ms = (this_output_time.tv_sec - instance->last_output_time.tv_sec) * 1000 + (this_output_time.tv_nsec - instance->last_output_time.tv_nsec) / 1000000;
	if(interval_ms < 1)
	{
		printf("Passive return\n");
		return;
	}

	if(instance->local_output_enable)
	{
		ret = fieldstat_output_file(instance, interval_ms);
	}
	if(instance->line_protocol_output_enable)
	{
		ret = line_protocol_output(instance);
	}
	if(ret == -1)
	{
		return;
	}
	memcpy(&(instance->last_output_time),&this_output_time, sizeof(this_output_time));
}

void *fieldstat_thread_schema_output(void *arg)
{
	struct fieldstat_instance *instance=(struct fieldstat_instance *)arg;
	while(instance->background_thread_disable == 0)
	{
		fieldstat_passive_output(instance);
		sleep(instance->output_interval_s);
	}
	return NULL;
}


void fieldstat_instance_start(struct fieldstat_instance *instance)
{
	instance->running = 1;
	clock_gettime(CLOCK_MONOTONIC,&(instance->last_output_time));
	if(instance->background_thread_disable == 0)
	{
		pthread_create(&(instance->cfg_mon_t), NULL, fieldstat_thread_schema_output, (void*)instance);
	}
	//append instance to prometheus output
}

struct fieldstat_instance * fieldstat_instance_create(const char *name)
{
	struct fieldstat_instance *instance = (struct fieldstat_instance *)calloc(sizeof(struct fieldstat_instance),1);

	if(strlen(name) >= INSTANCE_NAME_LEN)
	{
		return NULL;
	}

	strcpy(instance->name, name);
	instance->running = 0;
	instance->output_interval_s = 2;		//default 2s
	instance->background_thread_disable = 0;
	instance->metric_size = NUM_INIT_METRICS;
	instance->metric_block_list[0] = (struct metric_t **)calloc(sizeof(struct metric *), instance->metric_size);
	//instance->table_num = NUM_INIT_TABLES;
	//instance->metric_table = (struct metric_t **)calloc(sizeof(struct metric *), instance->table_num); 
	return instance;
}


 int fieldstat_register_table(struct fieldstat_instance *instance, enum field_type table_type[], const char *field_list[], size_t n_field)
 {
	int table_id = 0;
	int i = 0;
	metric_t **table_column = NULL;

	if(n_field <= 0)
	{
		return -1;
	}
	if(instance->table_num > NUM_MAX_TABLE)
	{
		return -1;
	}
	if(n_field > TABLE_COLUMN_SIZE)
	{
		return -1;
	}

	//table_id = instance->table_num++;
	table_id = atomic_inc(&instance->table_num) - 1;
	//instance->metric_table[table_id];
	table_column = instance->table_metrics[table_id].column = (struct metric_t **)calloc(sizeof(struct metric *), n_field);
	instance->table_metrics[table_id].column_size = n_field;
	for(; i < (int)n_field; i ++)
	{
		table_column[i] = metric_new(table_type[i],field_list[i],NULL,NULL,0);
	}
	return table_id;
 }


struct metric_id_list fieldstat_register_table_metrics(struct fieldstat_instance * instance, int table_id, const char *field_name, const char *tag_key[], const char *tag_value[],size_t n_tag)
{
	int metric_id = 0;
	int i = 0;
	int per_table_line_num = 0;
	struct metric_t * metric_choosen = NULL;
	int *table_line = NULL;
	metric_t **table_column = instance->table_metrics[table_id].column;
	int table_column_size = instance->table_metrics[table_id].column_size;
	struct metric_id_list metric_id_list;
	memset(&(metric_id_list),0,sizeof(struct metric_id_list));

	if(table_id < 0 || table_id >= NUM_MAX_TABLE)
	{
		return metric_id_list;
	}

	if(!is_valid_field_name(field_name))
	{
		return metric_id_list;
	}
	if(n_tag > N_TAG_MAX)
	{
		return metric_id_list;
	}

	if(table_column_size < 1)
	{
		return metric_id_list;
	}

	//TODO not block
	table_column = instance->table_metrics[table_id].column;

	per_table_line_num = atomic_inc(&(instance->per_table_line_number[table_id])) - 1;
	table_line =  instance->index_table[table_id][per_table_line_num] = (int *)calloc(sizeof(int *), table_column_size);

	for( i = 0; i < table_column_size; i ++)
	{	
		metric_id = atomic_inc(&instance->metric_cnt) - 1;
		metric_choosen = create_metric(instance,metric_id, table_column[i]->field_type,field_name,tag_key,tag_value,n_tag);

		table_line[i] = metric_id;

		metric_choosen->table_id = table_id;
		metric_choosen->belong_to_table = 1;
		metric_choosen->table_column_id = i;
		metric_choosen->line_seq ++;
		metric_choosen->table_column_name = strdup(table_column[i]->field_name); 

		switch(metric_choosen->field_type)
		{
			case FIELD_TYPE_COUNTER:
				instance->counter_cnt++;
				memset(&(metric_choosen->counter), 0, sizeof(metric_choosen->counter));
				break;
			case FIELD_TYPE_GAUGE:
				instance->gauge_cnt++;
				memset(&(metric_choosen->gauge), 0, sizeof(metric_choosen->gauge));
				break;
			case FILED_TYPE_HISTOGRAM:
				//instance->histogram_cnt++;
				// TODO  what?
				break;
			case FIELD_TYPE_SUMMARY:
				//instance->summary_cnt++;
				//TODO what ?
				break;
			default:
				assert(0);
				break;
		}
		if(table_column_size < (int)sizeof(metric_id_list.id))
		{
			metric_id_list.id[metric_id_list.count ++] = metric_id;
		}

	}

	return metric_id_list;
}


/*
* ret = -1, output not match fieldstat instance
	output:
		/metrics
		/sapp
		/tfe
		...
* ret >=0 && ret < instance_cnt  output sepecify fieldstat instance 
	output:
		http://127.0.0.1:9273/sapp content
* ret = instance_cnt output all fieldstat instance
   	output:  
		http://127.0.0.1:9273/metrics content
*/
static int prometheus_get_fs_instance_id(struct prometheus_endpoint_instance *global_prometheus_output, char *uri, int uri_len)
{
	int i = 0;

	if(uri_len == (int)strlen(global_prometheus_output->url_path) && 
			 0 == memcmp(uri, global_prometheus_output->url_path, strlen(global_prometheus_output->url_path)))
	{		
		return global_prometheus_output->fs_instance_cnt;
	}
	
	for( i = 0; i < global_prometheus_output->fs_instance_cnt; i++)
	{
		if(uri_len - 1 == (int)strlen(global_prometheus_output->fs_instance[i]->name) && 
					 0 == memcmp( uri + 1, global_prometheus_output->fs_instance[i]->name, strlen(global_prometheus_output->fs_instance[i]->name)))
		{
			return i;
		}
	}
	return -1;
}

static void prometheus_output_uri_list(struct prometheus_endpoint_instance *prometheus_output,struct http_request_s* request)
{
	int i = 0;
	int payload_len = 0;
	char *payload = NULL;
	char *payload_append_position = NULL;
	struct fieldstat_instance **fs_instance = NULL;
	struct http_response_s* response = NULL;

	fs_instance = prometheus_output->fs_instance;
	

	payload_len = prometheus_output->fs_instance_cnt * 128; //TODO using marco, len?
	payload_append_position = payload = (char *)calloc(1,payload_len);
	
	payload_append_position += snprintf(payload_append_position, payload_len - (payload_append_position - payload),"url_path:\n\t%s\n", prometheus_output->url_path);

	for(i = 0; i < prometheus_output->fs_instance_cnt; i++)
	{
		payload_append_position += snprintf(payload_append_position, payload_len - (payload_append_position - payload),"\t/%s\n", fs_instance[i]->name);
	}

	response = http_response_init();
	http_response_status(response, 404);
	http_response_header(response, "Content-Type", "text/plain; charset=utf-8");
	http_response_body(response, payload, strlen(payload));
	http_respond(request, response);
	
	free(payload);
	payload=NULL;
	return;
}

static int prometheus_output_get_metric_tags(struct metric_t *metric, char *tags_buff, unsigned int size, char *instance_app_name)
{
	int i = 0;
	char unescape[STR_LEN_256] = {0};
	char *tags_buff_append_position = tags_buff;
	
	tags_buff_append_position += snprintf(tags_buff_append_position, size - (tags_buff_append_position - tags_buff), "app_name=\"%s\"", instance_app_name);

	if(metric->belong_to_table == 1)
	{
		tags_buff_append_position += snprintf(tags_buff_append_position, size - (tags_buff_append_position - tags_buff), ",line_name=\"%s\"", metric->field_name);
	}

	for(i = 0; i < (int)metric->n_tag; i++)
	{
		memset(unescape, 0, sizeof(unescape));
		str_unescape(metric->tag_key[i], unescape, sizeof(unescape));
		tags_buff_append_position += snprintf(tags_buff_append_position, size - (tags_buff_append_position - tags_buff), ",%s=\"%s\"", unescape, metric->tag_value[i]);
	}
	return tags_buff_append_position - tags_buff;
}


static int prometheus_output_get_metric_name(struct metric_t *metric, char *name_buff, unsigned int size, char *instance_app_name)
{
	char unescape[256] = {0};
	char *name_buff_append_position = name_buff;
	
	if(metric->belong_to_table == 1)
	{
		str_unescape(metric->table_column_name, unescape, sizeof(unescape));
	}
	else
	{
		str_unescape(metric->field_name, unescape, sizeof(unescape));
	}
	name_buff_append_position += snprintf(name_buff_append_position, size - (name_buff_append_position - name_buff), "%s_%s", instance_app_name, unescape);

	return name_buff_append_position - name_buff;
}


static int prometheus_output_histogram_and_summary(struct metric_t *metric, char *payload, int payload_len, char *instance_app_name, char *metric_name, char *metric_tags)
{
	long long value=0;
	long long sum = 0;
	int i=0,used_len=0;

	struct hdr_iter iter;
	char output_format[STR_LEN_64] = {0};

	//struct histogram_t* h=&(p->histogram);
	struct histogram_t *h = &(metric->histogram);
	struct hdr_histogram* h_out=NULL, *h_tmp=NULL;
	int bin_num = metric->histogram.bins_num;
	double *bins = metric->histogram.bins;

	if(metric->field_type == FILED_TYPE_HISTOGRAM)
	{
		snprintf(output_format, sizeof(output_format), "%%s_bucket{%%s,le=\"%%0.2f\"} %%llu\n");
	}

	if(metric->field_type == FIELD_TYPE_SUMMARY)
	{
		snprintf(output_format, sizeof(output_format), "%%s{%%s,quantile=\"%%0.2f\"} %%llu\n");
	}

	hdr_init(h->lowest_trackable_value,  h->highest_trackable_value, h->significant_figures, &(h_tmp));

	hdr_add(h_tmp, h->accumulated);
	hdr_add(h_tmp, h->changing);
	h_out=h_tmp;

	for(i=0;i<bin_num;i++)
	{
		value= hdr_count_le_value(h_out, (long long)bins[i]);
		used_len+=snprintf(payload+used_len,
							payload_len-used_len,
							output_format,
							metric_name,
							metric_tags,
							bins[i],
							value
					);
	}
	used_len+=snprintf(payload+used_len,
							payload_len-used_len,
							"%s_count{%s} %llu\n",
							metric_name,
							metric_tags,
							(long long)h_out->total_count
					);

	hdr_iter_recorded_init(&iter, h_out);
	while (hdr_iter_next(&iter))
	{
		sum+=(long long)iter.value;
	}
	used_len+=snprintf(payload+used_len,
			payload_len-used_len,
			"%s_sum{%s} %llu\n",
			metric_name,
			metric_tags,
			sum
			);

	hdr_close(h_tmp);
	h_tmp=NULL;

	return used_len;
}



static int prometheus_get_instance_metric_playload(struct fieldstat_instance *instance, char **payload, int *payload_size, int offset)
{
	int i = 0;
	struct metric_t *metric = NULL;
	long long value = 0;
	char metric_name[STR_LEN_256] = {0};  //need to macro  match the regex [a-zA-Z_:][a-zA-Z0-9_:]*
	char app_name[STR_LEN_256] = {0};       //using macro
	char metric_tags[STR_LEN_1024] = {0};   //label name match the regex [a-zA-Z_:][a-zA-Z0-9_:]* 
	int append_offset = offset;

	if(instance == NULL || instance->running != 1)
	{
		return -1;
	}
	if(payload == NULL)
	{
		return -1;
	}


	str_unescape(instance->name, app_name, sizeof(app_name));

	for(i = 0; i < instance->metric_cnt; i++)
	{
		//metric = instance->metric[i];
		metric = get_metric(instance, i);

		if(metric->is_ratio == 1)
		{
			continue;
		}
		if(metric->is_invisible == 1)
		{
			continue;
		}

		//relloc
		//type = [counter, gauge]
		if( *payload_size - append_offset <= LEFT_MIN_BUFF_LEN)
		{
			*payload_size += REALLOC_SCALE_SIZE;
			*payload = (char *)realloc(*payload, *payload_size);
		}

		prometheus_output_get_metric_name(metric, metric_name, sizeof(metric_name), app_name);    //TODO refactor
		prometheus_output_get_metric_tags(metric, metric_tags, sizeof(metric_tags), app_name);    //TODO refactor

		switch(metric->field_type)
		{
			case FIELD_TYPE_COUNTER:
			case FIELD_TYPE_GAUGE:
				value = get_metric_unit_val(metric, FS_CALC_CURRENT, 1);
				append_offset += snprintf(*payload + append_offset, *payload_size - append_offset,
													"%s{%s} %llu\n", metric_name, metric_tags, value);
				break;
			case FIELD_TYPE_SUMMARY:
			case FILED_TYPE_HISTOGRAM:
				append_offset += prometheus_output_histogram_and_summary(metric,*payload + append_offset, *payload_size - append_offset, app_name, metric_name, metric_tags);
				break;
			default:
				break;
		}
	}
	return append_offset;

}

static void prometheus_output_instance_metric(struct prometheus_endpoint_instance *prometheus_output, struct http_request_s* request, int fs_instance_id)
{
	int i = 0;
	int payload_size = 0;
	int payload_offset = 0;
	char *payload = NULL;
	struct fieldstat_instance *instance = NULL;
	struct http_response_s* response = NULL;

	if(fs_instance_id == prometheus_output->fs_instance_cnt)
	{
		for(i = 0; i < prometheus_output->fs_instance_cnt; i++)
		{
			instance = prometheus_output->fs_instance[i];
			payload_offset = prometheus_get_instance_metric_playload(instance, &payload, &payload_size, payload_offset);				
		}
	}
	else
	{
		instance = prometheus_output->fs_instance[fs_instance_id];
		payload_offset = prometheus_get_instance_metric_playload(instance, &payload, &payload_size, payload_offset);	
	}

	if(payload != NULL)
	{	
		response = http_response_init();
		http_response_status(response, 200);
		http_response_header(response, "Content-Type", "text/plain; charset=utf-8");
		http_response_body(response, payload, strlen(payload));
		http_respond(request, response);
	
		free(payload);
		payload=NULL;
	}

}


static void prometheus_endpoint_instance_output(struct http_request_s* request)
{
	struct http_string_s uri;
	struct prometheus_endpoint_instance *prometheus_instance = NULL;
	int fs_instance_idx = -1;	

	prometheus_instance = &g_prometheus_endpoint_instance;
	uri = http_request_target(request);
	fs_instance_idx = prometheus_get_fs_instance_id(prometheus_instance, (char *)uri.buf, uri.len);

	if(fs_instance_idx == -1)
	{
		prometheus_output_uri_list(prometheus_instance, request);
		return;
	}
	else
	{
		prometheus_output_instance_metric(prometheus_instance, request, fs_instance_idx);
	}
	return;
}


void *prometheus_endpoin_listen(void *arg)
{
	struct prometheus_endpoint_instance *global_prometheus = (struct prometheus_endpoint_instance *)arg;

	if(global_prometheus != NULL)
	{
		global_prometheus->server_handle = http_server_init(global_prometheus->port , prometheus_endpoint_instance_output);
		http_server_listen(global_prometheus->server_handle);
	}

	return NULL;
} 


int fieldstat_global_enable_prometheus_endpoint(unsigned short listen_port, const char *url)
{
	if(url == NULL)
	{
		g_prometheus_endpoint_instance.url_path = (char *)calloc(strlen((char *)"/metrics")+1, 1);
		memcpy(g_prometheus_endpoint_instance.url_path, (char *)"/metrics", strlen((char *)"/metrics"));
	}

	g_prometheus_endpoint_instance.port = listen_port;
	g_prometheus_endpoint_instance.url_path = strdup(url);
	g_prometheus_endpoint_instance.running = 1;
	g_prometheus_endpoint_instance.fs_instance = (struct fieldstat_instance **)calloc( sizeof(struct fieldstat_instance *), g_prometheus_endpoint_instance.fs_instance_size);

	pthread_create(&g_prometheus_endpoint_instance.tid, NULL, prometheus_endpoin_listen, (void *)&g_prometheus_endpoint_instance);
	return 0;
}


int fieldstat_set_prometheus_output(struct fieldstat_instance *instance) //TODO 
{
	int i = 0;

	if(g_prometheus_endpoint_instance.running != 1)
	{
		return -1;
	}

	if(g_prometheus_endpoint_instance.fs_instance_cnt >= g_prometheus_endpoint_instance.fs_instance_size)
	{
		g_prometheus_endpoint_instance.fs_instance_size += REALLOC_SCALE_SIZE;
		g_prometheus_endpoint_instance.fs_instance = (struct fieldstat_instance **)realloc(g_prometheus_endpoint_instance.fs_instance, g_prometheus_endpoint_instance.fs_instance_size);
	}

	i = g_prometheus_endpoint_instance.fs_instance_cnt++;
	g_prometheus_endpoint_instance.fs_instance[i] = instance;
	return 0;
}



static int fieldstat_value_operate(struct fieldstat_instance *instance, int field_id, enum field_op op, long long value)
{
	struct metric_t * metric = NULL;
	struct stat_unit_t *target = NULL;

	if(field_id >= instance->metric_cnt)
	{
		return -1;
	}

	metric = get_metric(instance, field_id);

	switch(metric->field_type)
	{
		case FIELD_TYPE_COUNTER:
			target = &(metric->counter);
			break;
		case FIELD_TYPE_GAUGE:
			target = &(metric->gauge);
			break;
		case FIELD_TYPE_SUMMARY:
		case FILED_TYPE_HISTOGRAM: 
			hdr_record_value(metric->histogram.changing, (int64_t) value);
			return 0;
		default:
			break;
	}

	switch(op)
	{
		case	FS_OP_ADD:
			threadsafe_counter_add(&(target->changing), value);
			break;
		case	FS_OP_SET:
			threadsafe_counter_set(&(target->changing), value-target->accumulated);
			break;
		case	FS_OP_SUB:
			threadsafe_counter_sub(&(target->changing), value);
			break;
		default:
			assert(0);
			break;
	}

	return 0;
}


int fieldstat_value_incrby(struct fieldstat_instance *instance, int field_id, long long increment)
{
	int ret = 0;
	ret = fieldstat_value_operate(instance, field_id, FS_OP_ADD, increment);
	return ret;
}

int fieldstat_value_set(struct fieldstat_instance *instance, int field_id, long long retain)
{
	int ret = 0;
	ret = fieldstat_value_operate(instance, field_id, FS_OP_SET, retain);
	return ret;
}

int fieldstat_value_decrby(struct fieldstat_instance *instance, int field_id, long long decrment)
{
	int ret = 0;
	ret = fieldstat_value_operate(instance, field_id, FS_OP_SUB, decrment);
	return ret;
}

static int parse_histogram_bin_format(const char* format, double **output_bins, enum field_type type)
{
	char *token=NULL,*sub_token=NULL,*saveptr;
	size_t i=0;
	int comma_num=0,ret=0;
	double *bins;
	char* dup_format=__str_dup(format);
	for(i=0;i<strlen(dup_format);i++)
	{
		if(dup_format[i]==',')
		{
			comma_num++;
		}
	}
	bins=(double*)calloc(sizeof(double),comma_num+1);
	for (token = dup_format,i=0; ; token= NULL, i++)
	{
		sub_token= strtok_r(token,",", &saveptr);
		if (sub_token == NULL)
			break;
		ret=sscanf(sub_token,"%lf",bins+i);
		if(type == FIELD_TYPE_SUMMARY)
		{
			if(bins[i]>1.0)
			{
				goto error_out;
			}
		}
		if(ret!=1)
		{
			goto error_out;
		}
	}
	free(dup_format);
	*output_bins=bins;
	return i;
error_out:
	free(dup_format);
	free(bins);
	return -1;
}


int fieldstat_register_histogram(struct fieldstat_instance *instance, enum field_type type, const char *field_name, const char *tag_key[], const char *tag_value[], size_t n_tag, 
		const char * bins,const long long lowest_trackable_value,long long highest_trackable_value,int significant_figures)
{
	struct metric_t *metric_choosen = NULL;
	int metric_id = -1;
	int ret;

	if(!is_valid_field_name(field_name))
	{
		return -1;
	}
	if(n_tag > N_TAG_MAX)
	{
		return -1;
	}

	if (lowest_trackable_value < 1 || significant_figures < 1 || 5 < significant_figures)
	{
		return -1;
	}

	if (lowest_trackable_value * 2 > highest_trackable_value)
	{
		return -1;
	}

	metric_id = atomic_inc(&instance->metric_cnt) - 1;
	metric_choosen = create_metric(instance, metric_id, type, field_name, tag_key, tag_value, n_tag);

	metric_choosen->histogram.highest_trackable_value = (int64_t)highest_trackable_value;
	metric_choosen->histogram.lowest_trackable_value  = (int64_t)lowest_trackable_value;
	metric_choosen->histogram.significant_figures     = significant_figures;
	metric_choosen->histogram.bins_num = parse_histogram_bin_format((const char*)bins, &(metric_choosen->histogram.bins), type);

	ret = hdr_init((int64_t)lowest_trackable_value, (int64_t)highest_trackable_value, significant_figures, &(metric_choosen->histogram.changing));
	assert(ret==0);	
	ret = hdr_init((int64_t)lowest_trackable_value, (int64_t)highest_trackable_value, significant_figures, &(metric_choosen->histogram.accumulated));
	assert(ret==0);

	switch(type)
	{
		case FIELD_TYPE_SUMMARY:
			atomic_inc(&instance->summary_cnt);
			break;
		case FILED_TYPE_HISTOGRAM:
			atomic_inc(&instance->histogram_cnt);
			break;
		default:
			break;
	}
	return metric_id;
}
