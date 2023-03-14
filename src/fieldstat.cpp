#include "fieldstat_internal.h"

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

char* __str_dup(const char* str)
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


void get_current_table_line_cnt(struct fieldstat_instance *instance, int n_table, int *tables_line_cnt)
{
	for(int i = 0; i < n_table; i++)
	{
		if(instance->table_metrics[i] == NULL)
		{
			continue;
		}
		tables_line_cnt[i] = instance->table_metrics[i]->line_cnt;
	}
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
		metric->tag_key[i]   = __str_dup(tag_key[i]);
		metric->tag_value[i] = __str_dup(tag_value[i]);
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

struct metric_t ** read_metric_slot(struct fieldstat_instance *instance, int metric_id)
{
	int block_index = 0;
	int in_block_index = 0;
	struct metric_t ** metrics_block = NULL;

	if(instance == NULL)
	{
		return NULL;
	}
	block_index = metric_id / NUM_INIT_METRICS;
	in_block_index = metric_id % NUM_INIT_METRICS;
	if(in_block_index == 0)
	{
		assert(instance->metric_block_list[block_index] == NULL);
		instance->metric_block_list[block_index] = (struct metric_t **)calloc(sizeof(struct metric *), NUM_INIT_METRICS);	
	}
	else
	{
		while (instance->metric_block_list[block_index] == NULL);
	}
	metrics_block = (struct metric_t **)instance->metric_block_list[block_index];

	return (struct metric_t **)&(metrics_block[in_block_index]);
}


struct table_line * read_table_line(struct table_metric *table, int line_id)
{
	int block_index = 0;
	int block_in_index = 0;
	struct table_line **line_array = NULL;
	struct table_line *line = NULL;

	block_index = line_id / NUM_INIT_METRICS;
	block_in_index = line_id % NUM_INIT_METRICS;
	line_array = table->line_block[block_index];
	line = line_array[block_in_index];

	return line;
}


static int startup_udp()
{
	int sd_udp=-1;
	int flags;
	int opt=1;
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
	if (setsockopt (sd_udp, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt) ) < 0) 
	{
		printf("FS2:setsockopt error: %d %s, restart socket.", errno, strerror(errno));
		close (sd_udp);
		sd_udp=-1;
		return sd_udp;
	}

	return sd_udp;
}

int send_udp(int sd, unsigned int dest_ip, unsigned short dest_port, const char * data, int len)
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

int fieldstat_background_thread_disable(struct fieldstat_instance *instance)
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
	strncpy(instance->local_output_filename, (char *)filename, len_filename);
	strncpy(instance->local_output_format, (char *)format, len_format);
	instance->local_output_enable  = 1;

	return 0;
}

int fieldstat_set_line_protocol_server(struct fieldstat_instance *instance, const char *ip, unsigned short port)
{
	if(instance->running == 1)
	{
		return -1;
	}

	if(1 != inet_pton(AF_INET, ip, (void *)&(instance->line_protocol_server_ip)))
	{
		return -1;
	}
	instance->line_protocol_socket = startup_udp();
	instance->line_protocol_server_port   = port;
	instance->line_protocol_output_enable = 1;

	return 0;
}

int fieldstat_set_statsd_server(struct fieldstat_instance *instance, const char *ip, unsigned short port)
{
	if(instance->running == 1)
	{
		return -1;
	}

	if(1 != inet_pton(AF_INET, ip, (void *)&(instance->statsd_server_ip)))
	{
		return -1;
	}

	instance->statsd_server_port   = port;
	instance->statsd_output_enable = 1;
	return 0;
}

int fieldstat_register(struct fieldstat_instance *instance, enum field_type type, const char *field_name, const char *tag_key[], const char *tag_value[], size_t n_tag)
{
	int metric_id = 0;
	struct metric_t * metric = NULL;
	struct metric_t **metric_slot = NULL;
	if(!is_valid_field_name(field_name))
	{
		return -1;
	}
	if(n_tag > N_TAG_MAX)
	{
		return -1;
	}

	metric_id = atomic_inc(&instance->metric_cnt) - 1;
	metric_slot = read_metric_slot(instance, metric_id);
	metric = metric_new(type,field_name,tag_key,tag_value,n_tag);

	switch(type)
	{
		case FIELD_TYPE_COUNTER:
			memset(&(metric->counter), 0, sizeof(metric->counter));
			break;
		case FIELD_TYPE_GAUGE:
			memset(&(metric->gauge), 0, sizeof(metric->gauge));
			break;
		default:
			assert(0);
	}
	*metric_slot = metric;
	return metric_id;
}

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
		default:
			break;
	}

	value = threadsafe_counter_read(&(target->changing));
	//value= threadsafe_counter_read(&(target->changing));
	if(is_refer == 0)
	{
		target->previous_changed = value;
		target->accumulated += value;
//		threadsafe_counter_set(&(target->changing), 0);
		threadsafe_counter_sub(&(target->changing), value);
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
			break;
	}
	return value;
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
	struct fieldstat_instance *instance =  NULL;

	if(strlen(name) >= INSTANCE_NAME_LEN)
	{
		return NULL;
	}

	instance = (struct fieldstat_instance *)calloc(sizeof(struct fieldstat_instance),1);
	strcpy(instance->name, name);
	instance->running = 0;
	instance->output_interval_s = 2;
	instance->background_thread_disable = 0;

	return instance;
}

struct table_metric* table_metric_new(const char *name, const char *column_name[], enum field_type column_type[], size_t n_column)
{
	int i = 0;
	struct table_metric *table_metric = (struct table_metric *)calloc(sizeof(struct table_metric), 1);

	table_metric->column_cnt = (int)n_column;
	table_metric->name = __str_dup(name);

	for(i = 0; i < (int)n_column; i++)
	{
		table_metric->column_name[i] = __str_dup(column_name[i]);
		table_metric->column_type[i] = column_type[i];
	}
	return table_metric;
}

 int fieldstat_register_table(struct fieldstat_instance *instance, const char *name, const char *column_name[], enum field_type column_type[], size_t n_column)
 {
	int table_id = 0;

	struct table_metric *table_metric = NULL;

	if(n_column <= 0 || n_column > TABLE_COLUMN_SIZE)
	{
		return -1;
	}

	if(instance->table_num > TABLE_MAX_NUM)
	{
		return -1;
	}

	table_id = atomic_inc(&instance->table_num) - 1;
	table_metric = table_metric_new(name, column_name, column_type, n_column);
	instance->table_metrics[table_id] = table_metric;

	return table_id;
 }

static struct table_line ** read_table_line_slot(struct table_metric *table, int line_id)
{
	int block_index = 0;
	int in_block_index = 0;
	struct table_line **line = NULL;

	block_index = line_id / NUM_INIT_METRICS;
	in_block_index = line_id % NUM_INIT_METRICS;
	if(in_block_index == 0)
	{
		assert(table->line_block[in_block_index] == NULL);
		table->line_block[block_index] = (struct table_line **)calloc(sizeof(struct table_line *), NUM_INIT_METRICS); 	
	}
	else
	{
		while (table->line_block[block_index] == NULL);
	}
	line = table->line_block[block_index];

	return (struct table_line **)&(line[in_block_index]);
}

static struct table_line *table_line_new(const char *name, const char *tag_key[], const char *tag_value[],size_t n_tag)
{
	int i = 0;
	struct table_line *table_line = (struct table_line *)calloc(sizeof(struct table_line), 1);

	table_line->name = __str_dup(name);
	table_line->n_tag = n_tag;

	for(i = 0; i < (int)n_tag; i++)
	{
		table_line->tag_key[i] = __str_dup(tag_key[i]);
		table_line->tag_value[i] = __str_dup(tag_value[i]);
	}
	return table_line;
}


struct metric_id_list fieldstat_register_table_metrics(struct fieldstat_instance * instance, int table_id, const char *line_name, const char *tag_key[], const char *tag_value[],size_t n_tag)
{
	int metric_id = 0;
	struct metric_t *metric = NULL;

	int line_id = 0;
	int i = 0;

	struct table_metric *table = NULL;
	struct table_line **line_slot = NULL;
	struct table_line *table_line = NULL;
	struct metric_id_list ret_metric_id_list;
	memset(&ret_metric_id_list, 0, sizeof(struct metric_id_list));
	

	if(table_id < 0 || table_id >= TABLE_MAX_NUM)
	{
		return ret_metric_id_list;
	}
	if(!is_valid_field_name(line_name) || n_tag > N_TAG_MAX)
	{
		return ret_metric_id_list;
	}

	table = instance->table_metrics[table_id];
	line_id = atomic_inc(&(table->line_cnt)) - 1;
	line_slot = read_table_line_slot(table,line_id);
	table_line = table_line_new(line_name, tag_key, tag_value, n_tag);

	for(i = 0; i < table->column_cnt; i++)
	{
		metric_id = fieldstat_register(instance, table->column_type[i], line_name,tag_key, tag_value, n_tag);

		table_line->metric_id_belong_to_line[i] = metric_id;

		metric = get_metric(instance, metric_id);
		metric->table_id = table_id;
		metric->table_column_name = __str_dup(table->column_name[i]); 
		metric->table_name = __str_dup(table->name);
		metric->belong_to_table = 1;
		ret_metric_id_list.id[ret_metric_id_list.count ++] = metric_id;

	}
	*line_slot = table_line;
	return ret_metric_id_list;
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


static int fieldstat_register_histogram_and_summary(struct fieldstat_instance *instance, enum field_type type, const char *field_name, const char *tag_key[], const char *tag_value[], size_t n_tag, 
		const char * bins, long long lowest_trackable_value,long long highest_trackable_value,int significant_figures)
{
	struct metric_t *metric = NULL;
	struct metric_t **metric_slot = NULL;
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
	metric_slot = read_metric_slot(instance, metric_id);
	metric = metric_new(type,field_name,tag_key,tag_value,n_tag);

	metric->histogram.highest_trackable_value = (int64_t)highest_trackable_value;
	metric->histogram.lowest_trackable_value  = (int64_t)lowest_trackable_value;
	metric->histogram.significant_figures     = significant_figures;
	metric->histogram.bins_num = parse_histogram_bin_format((const char*)bins, &(metric->histogram.bins), type);

	ret = hdr_init((int64_t)lowest_trackable_value, (int64_t)highest_trackable_value, significant_figures, &(metric->histogram.changing));
	assert(ret==0);	
	ret = hdr_init((int64_t)lowest_trackable_value, (int64_t)highest_trackable_value, significant_figures, &(metric->histogram.accumulated));
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
	*metric_slot = metric;
	return metric_id;
}


int fieldstat_register_histogram(struct fieldstat_instance *instance, const char *field_name, const char *tag_key[], const char *tag_value[], size_t n_tag, 
		const char * bins, long long lowest_trackable_value,long long highest_trackable_value,int significant_figures)
{
	return fieldstat_register_histogram_and_summary(instance, FILED_TYPE_HISTOGRAM, field_name, tag_key, tag_value, n_tag, 
		bins, lowest_trackable_value, highest_trackable_value, significant_figures);
}

int fieldstat_register_summary(struct fieldstat_instance *instance, const char *field_name, const char *tag_key[], const char *tag_value[], size_t n_tag, 
		const char * bins,long long lowest_trackable_value,long long highest_trackable_value,int significant_figures)
{
	return fieldstat_register_histogram_and_summary(instance, FIELD_TYPE_SUMMARY, field_name, tag_key, tag_value, n_tag, 
		bins, lowest_trackable_value, highest_trackable_value, significant_figures);
}

