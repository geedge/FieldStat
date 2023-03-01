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

double HISTOGRAM_DEFAULT_BINS[]={50.0, 80.0, 90.0, 95.0, 99.0};

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
#define	LEFT_MIN_BUFF_LEN	256
#define	REALLOC_SCALE_SIZE	1024



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
	int index_table[NUM_MAX_TABLE][1024];
	int per_table_metric_number[NUM_MAX_TABLE];

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

struct fieldstat_global_prometheus
{
	unsigned short port;
	char *url_path;
	pthread_t tid;
	int running;
	struct http_server_s *server_handle;
	int output_instance_cnt;
	struct fieldstat_instance **output_instance;
	int output_instance_size;
};

struct fieldstat_global_prometheus g_fieldstat_global_prometheus = 
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



int fieldstat_set_app_name(struct fieldstat_instance *instance, const char *app_name)
{
	int len_app_name = strlen(app_name);
	if(instance->running == 1)
	{
		return -1;
	}
	if(len_app_name <= 0 && len_app_name >= LEN_APP_NAME )
	{
		return -1;
	}
	strncpy(instance->app_name,(char*)app_name, len_app_name);
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
	metric_id = instance->metric_cnt++;
	assert(instance->metric_cnt < instance->metric_size);
	metric_choosen = instance->metric[metric_id] = metric_new(type,field_name,tag_key,tag_value,n_tag);
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

/*
static int output_file_format_default_table(struct fieldstat_instance *instance)
{
	int i = 0, j = 0, k = 0;
	metric_t *metric = NULL;
	long long value = 0;
	//double ratio = 0.0;
	struct metric_t **table_column = NULL;
	size_t table_column_size = 0;

	char* print_buf_append_position = print_buf;

	int index_table[NUM_MAX_TABLE][1024];
	int table_metric_number[NUM_MAX_TABLE];
	memset(table_metric_number,0, sizeof(table_metric_number));
	char *line_name = NULL;
	int table_id = 0;
	int per_table_counter = 0;
	int metric_id = 0;

	char field_set_buff[UDP_PAYLOAD_SIZE];
	char tag_set_buff[UDP_PAYLOAD_SIZE];

	memset(field_set_buff, 0, sizeof(field_set_buff));
	memset(tag_set_buff,  0, sizeof(tag_set_buff));
	char *tag_set_buff_append   = tag_set_buff;
	char *field_set_buff_append = field_set_buff;
	char *measurement = NULL;

	for(i = 0; i < instance->metric_cnt; i++)
	{
		metric = instance->metric[i];
		if(metric->belong_to_table != 1)
		{
			continue;
		}
		table_id = metric->table_id;
		per_table_counter = table_metric_number[table_id]++;
		index_table[table_id][per_table_counter] = i;
	}

	for(i = 0; i < instance->table_num; i++)
	{
		if(table_metric_number[i] == 0)
		{
			continue;
		}

		table_column = instance->table_metrics[i].column;
		table_column_size = instance->table_metrics[i].column_size;

		for(j = 0; j < table_metric_number[i]; j++)
		{
			metric_id = index_table[i][j];
			metric = instance->metric[metric_id];
			if(line_name == NULL || strcmp(line_name, metric->field_name))
			{
				if(line_name != NULL)
				{
					append_line_protocol_line(instance, measurement, tag_set_buff, field_set_buff);
				}
				line_name = metric->field_name;
				tag_set_buff_append += snprintf(tag_set_buff, sizeof(tag_set_buff) - (tag_set_buff_append - tag_set_buff), ",app_name=", instance->app_name);
				for(j = 0; j < (int)metric->n_tag; j++)
				{
					tag_set_buff_append += snprintf(tag_set_buff_append, sizeof(tag_set_buff) - (tag_set_buff_append - tag_set_buff), ",%s=%s", metric->tag_key[i],metric->tag_value[i]);
				}
			}
			if(metric->field_type == FIELD_TYPE_COUNTER)
			{
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 1);
			}
			if(metric->field_type == FIELD_TYPE_GAUGE)
			{
				value = get_metric_unit_val(metric, FS_CALC_CURRENT, 1);
			}
			field_set_buff_append += snprintf(field_set_buff_append, sizeof(field_set_buff) - (field_set_buff_append - tag_set_buff), "%s=%lld,", metric->table_column_name, value);
		}
		
		line_name = NULL;
	}

	return print_buf_append_position - print_buf;
}
*/

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
	int i = 0, j = 0;
	metric_t *metric = NULL;
	long long value = 0;
	//double ratio = 0.0;
	size_t table_column_size = 0;

	int table_metric_number[NUM_MAX_TABLE];
	memset(table_metric_number,0, sizeof(table_metric_number));
	int metric_id = 0;
	char field_set_buff[UDP_PAYLOAD_SIZE];
	char tag_set_buff[UDP_PAYLOAD_SIZE];

	memset(field_set_buff, 0, sizeof(field_set_buff));
	memset(tag_set_buff,  0, sizeof(tag_set_buff));
	char *tag_set_buff_append = tag_set_buff;
	char *field_set_buff_append = field_set_buff;


	for(i = 0; i < instance->table_num; i++)
	{
		table_column_size = instance->table_metrics[i].column_size;

		for(j = 0; j < instance->per_table_metric_number[i]; j++)
		{
			metric_id = instance->index_table[i][j];
			metric = instance->metric[metric_id];

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

			if((j + 1) % table_column_size == 0)  //per line edge
			{
				tag_set_buff_append += snprintf(tag_set_buff_append, sizeof(tag_set_buff) - (tag_set_buff_append - tag_set_buff), ",app_name=%s", instance->app_name);
				output_line_protocol_tag_set_buf(metric, tag_set_buff_append, sizeof(tag_set_buff) - (tag_set_buff_append - tag_set_buff));
				/*
				for(k = 0; k < (int)metric->n_tag; k++)
				{
					tag_set_buff_append += snprintf(tag_set_buff_append, sizeof(tag_set_buff) - (tag_set_buff_append - tag_set_buff), ",%s=%s", metric->tag_key[i],metric->tag_value[i]);
				}
				*/

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
		metric = instance->metric[i];
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
					tag_set_buff_append += snprintf(tag_set_buff_append, sizeof(tag_set_buff) - (tag_set_buff_append - tag_set_buff), ",app_name=%s", instance->app_name);
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
					tag_set_buff_append += snprintf(tag_set_buff_append, sizeof(tag_set_buff) - (tag_set_buff_append - tag_set_buff), ",app_name=%s", instance->app_name);
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
		metric = instance->metric[i];
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
		metric = instance->metric[i];
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
			metric = instance->metric[metric_id[i+j]];
			memset(print_buf_tags,0, sizeof(print_buf_tags));
			print_buf_tag_append_position(metric, print_buf_tags, sizeof(print_buf_tags));
			print_buf_append_position += snprintf(print_buf_append_position, size - (print_buf_append_position - print_buf), "%10s %s\t", metric->field_name, print_buf_tags);
		}
		print_buf_append_position += snprintf(print_buf_append_position, size - (print_buf_append_position-print_buf), "\nsum\t");

		for(j=0; j < FIELD_PER_LINE && i+j < metric_cnt; j++)
		{
			metric = instance->metric[metric_id[i+j]];
			value  = get_metric_unit_val(metric,FS_CALC_CURRENT, 1);
			print_buf_append_position += snprintf(print_buf_append_position, sizeof(print_buf) - (print_buf_append_position - print_buf), "%10lld\t", value);
		}
		print_buf_append_position += snprintf(print_buf_append_position, sizeof(print_buf) - (print_buf_append_position - print_buf), "\nspeed/s\t");

		for(j=0;j<FIELD_PER_LINE&&i+j<metric_cnt;j++)
		{
			metric = instance->metric[metric_id[i+j]];
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

	for(i = 0; i < instance->table_num; i++)
	{
		table_column = instance->table_metrics[i].column;
		table_column_size = instance->table_metrics[i].column_size;

		print_buf_append_position += snprintf(print_buf_append_position, size - (print_buf - print_buf),"\t\t\t");
		for(k = 0; k < (int)table_column_size; k++)
		{
			print_buf_append_position += snprintf(print_buf_append_position ,size - (print_buf_append_position - print_buf), "\t%10s", table_column[k]->field_name);			
		}

		for(j = 0; j < instance->per_table_metric_number[i]; j++)
		{
			metric_id = instance->index_table[i][j];
			metric = instance->metric[metric_id];
			if( (j + 1) % table_column_size == 1)
			{
				print_buf_tag_append_position(metric, print_buf_tags, sizeof(print_buf_tags));
				print_buf_append_position += snprintf(print_buf_append_position, size - (print_buf_append_position - print_buf), "\n%s %-20s\t", metric->field_name,print_buf_tags);
			}
			value = get_metric_unit_val(metric, FS_CALC_CURRENT, 0);
			print_buf_append_position += snprintf(print_buf_append_position, size - (print_buf_append_position - print_buf), "%10lld\t", value);
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


int fieldstat_output_file(struct fieldstat_instance *instance,long long interval_ms)
{
	size_t print_buf_sz = instance->metric_cnt*1024;
	char *print_buf = NULL;
	char *print_buf_append_position = NULL;
	time_t current = 0;
	char ctime_buff[32]={0};

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

struct fieldstat_instance * fieldstat_instance_create(void)
{
	struct fieldstat_instance *instance = (struct fieldstat_instance *)calloc(sizeof(struct fieldstat_instance),1);

	strcpy(instance->app_name, "?");
	instance->running = 0;
	instance->output_interval_s = 2;		//default 2s
	instance->background_thread_disable = 0;
	instance->metric_size = NUM_INIT_METRICS;
	instance->metric = (struct metric_t **)calloc(sizeof(struct metric *), instance->metric_size);
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

	table_id = instance->table_num++;
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

//int fieldstat_register(struct fieldstat_instance *instance, enum field_type type, const char *field_name, const char *tag_key[], const char *tag_value[], size_t n_tag)
{
	
	int metric_id = 0;
	int i = 0;
	int per_table_metrics_num = 0;
	struct metric_t * metric_choosen = NULL;
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

	for( i = 0; i < table_column_size; i ++)
	{	
		metric_id = instance->metric_cnt++;
		assert(instance->metric_cnt < instance->metric_size);
		metric_choosen = instance->metric[metric_id] = metric_new(table_column[i]->field_type,field_name,tag_key,tag_value,n_tag);

		per_table_metrics_num = instance->per_table_metric_number[table_id]++;
		instance->index_table[table_id][per_table_metrics_num] = metric_id;

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
* ret >=0 && ret < instance_cnt  output sepecify fieldstat instance 
* ret = instance_cnt output all fieldstat instance
*/
static int prometheus_get_output_instance_id(struct fieldstat_global_prometheus *global_prometheus_output, char *uri, int uri_len)
{
	int i = 0;

	if(uri_len == (int)strlen(global_prometheus_output->url_path) && 
	         0 == memcmp(uri, global_prometheus_output->url_path, strlen(global_prometheus_output->url_path)))
	{		
		return global_prometheus_output->output_instance_cnt;
	}
	
	for( i = 0; i < global_prometheus_output->output_instance_cnt; i++)
	{
		if(uri_len - 1 == (int)strlen(global_prometheus_output->output_instance[i]->app_name) && 
					 0 == memcmp( uri + 1, global_prometheus_output->output_instance[i]->app_name, strlen(global_prometheus_output->output_instance[i]->app_name)))
		{
			return i;
		}
	}
	return -1;
}

static void prometheus_output_uri_list(struct fieldstat_global_prometheus *prometheus_output,struct http_request_s* request)
{
	int i = 0;
	int payload_len = 0;
	char *payload = NULL;
	char *payload_append_position = NULL;
	struct fieldstat_instance  **output_instance = NULL;
	struct http_response_s* response = NULL;

	output_instance = prometheus_output->output_instance;
	

	payload_len = prometheus_output->output_instance_cnt * 128;
	payload_append_position = payload = (char *)calloc(1,payload_len);
	
	payload_append_position += snprintf(payload_append_position, payload_len - (payload_append_position - payload),"url_path:\n\t%s\n", prometheus_output->url_path);

	for(i = 0; i < prometheus_output->output_instance_cnt; i++)
	{
		payload_append_position += snprintf(payload_append_position, payload_len - (payload_append_position - payload),"\t/%s\n", output_instance[i]->app_name);
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
	char unescape[256] = {0};
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


static int prometheus_get_instance_metric_playload(struct fieldstat_instance *instance, char **payload, int *payload_size, int offset)
{
	int i = 0;
	struct metric_t *metric = NULL;
	long long value = 0;
	char metric_name[256] = {0};  //match the regex [a-zA-Z_:][a-zA-Z0-9_:]*
	char app_name[256] = {0};
	char metric_tags[1024] = {0};   //label name match the regex [a-zA-Z_:][a-zA-Z0-9_:]* 
	int append_offset = offset;

	if(instance == NULL || instance->running != 1)
	{
		return -1;
	}
	if(payload == NULL)
	{
		return -1;
	}


	str_unescape(instance->app_name, app_name, sizeof(app_name));

	for(i = 0; i < instance->metric_cnt; i++)
	{
		metric = instance->metric[i];

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

		switch(metric->field_type)
		{
			case FIELD_TYPE_COUNTER:
			case FIELD_TYPE_GAUGE:
				value = get_metric_unit_val(metric, FS_CALC_CURRENT, 1);
				prometheus_output_get_metric_name(metric, metric_name, sizeof(metric_name), app_name);
				prometheus_output_get_metric_tags(metric, metric_tags, sizeof(metric_tags), app_name);

				append_offset += snprintf(*payload + append_offset, *payload_size - append_offset,
													"%s{%s} %llu\n", metric_name, metric_tags, value);
				break;
			case FIELD_TYPE_SUMMARY:
			case FILED_TYPE_HISTOGRAM:
			default:
				break;
		}
	}
	return append_offset;

}

static void prometheus_output_instance_metric(struct fieldstat_global_prometheus *prometheus_output, struct http_request_s* request, int output_instance_id)
{
	int i = 0;
	int payload_size = 0;
	int payload_offset = 0;
	char *payload = NULL;
	struct fieldstat_instance *instance = NULL;
	struct http_response_s* response = NULL;

	if(output_instance_id == prometheus_output->output_instance_cnt)
	{
		for(i = 0; i < prometheus_output->output_instance_cnt; i++)
		{
			instance = prometheus_output->output_instance[i];
			payload_offset = prometheus_get_instance_metric_playload(instance, &payload, &payload_size, payload_offset);				
		}
	}
	else
	{
		instance = prometheus_output->output_instance[output_instance_id];
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


static void fieldstat_global_prometheus_output(struct http_request_s* request)
{
	struct http_string_s uri;
	struct fieldstat_global_prometheus *prometheus_output = NULL;
	int prometheus_output_instance_id = -1;	

	prometheus_output = &g_fieldstat_global_prometheus;
	uri = http_request_target(request);
	prometheus_output_instance_id = prometheus_get_output_instance_id(prometheus_output, (char *)uri.buf, uri.len);

	if(prometheus_output_instance_id == -1)
	{
		prometheus_output_uri_list(prometheus_output, request);
		return;
	}
	else
	{
		prometheus_output_instance_metric(prometheus_output, request, prometheus_output_instance_id);
	}
	return;
}


void *fieldstat_global_promethues_listen(void *arg)
{
	struct fieldstat_global_prometheus *global_prometheus = (struct fieldstat_global_prometheus *)arg;

	if(global_prometheus != NULL)
	{
		global_prometheus->server_handle = http_server_init(global_prometheus->port , fieldstat_global_prometheus_output);
		http_server_listen(global_prometheus->server_handle);
	}

	return NULL;
} 


int fieldstat_global_enable_prometheus_endpoint(unsigned short listen_port, const char *url)
{
	if(url == NULL)
	{
		g_fieldstat_global_prometheus.url_path = (char *)calloc(strlen((char *)"/metrics")+1, 1);
		memcpy(g_fieldstat_global_prometheus.url_path, (char *)"/metrics", strlen((char *)"/metrics"));
	}

	g_fieldstat_global_prometheus.port = listen_port;
	g_fieldstat_global_prometheus.url_path = strdup(url);
	g_fieldstat_global_prometheus.running = 1;
	g_fieldstat_global_prometheus.output_instance = (struct fieldstat_instance **)calloc( sizeof(struct fieldstat_instance *), g_fieldstat_global_prometheus.output_instance_size);

	pthread_create(&g_fieldstat_global_prometheus.tid, NULL, fieldstat_global_promethues_listen, (void *)&g_fieldstat_global_prometheus);
	return 0;
}


int fieldstat_prometheus_output_enable(struct fieldstat_instance *instance)
{
	int i = 0;

	if(g_fieldstat_global_prometheus.running != 1)
	{
		return -1;
	}

	if(g_fieldstat_global_prometheus.output_instance_cnt >= g_fieldstat_global_prometheus.output_instance_size)
	{
		g_fieldstat_global_prometheus.output_instance_size += REALLOC_SCALE_SIZE;
		g_fieldstat_global_prometheus.output_instance = (struct fieldstat_instance **)realloc(g_fieldstat_global_prometheus.output_instance, g_fieldstat_global_prometheus.output_instance_size);
	}

	i = g_fieldstat_global_prometheus.output_instance_cnt++;
	g_fieldstat_global_prometheus.output_instance[i] = instance;
	return 0;
}

