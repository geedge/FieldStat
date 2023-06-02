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
	dup=(char*)calloc(strlen(str)+1, sizeof(char));
	memcpy(dup, str, strlen(str));
	return dup;
}

int is_valid_field_name(const char* name)
{
	const char* reserverd="|:\n\r \t<>[]#!@";
	unsigned int i=0,j=0;
	unsigned int len=strlen(name);
	if(len == 0)
	{
		return 0;
	}
	for(i=0;i<len;i++)
	{
		for(j=0;j<strlen(reserverd);j++)
		if(name[i]==reserverd[j])
		{
			return 0;
		}
	}
	return 1;
}

int is_valid_tag_value(const char* value)
{
	const char* reserverd="|:\n\r \t<>[]#!@";
	unsigned int i=0,j=0;
	for(i=0;i<strlen(value);i++)
	{
		for(j=0;j<strlen(reserverd);j++)
		if(value[i]==reserverd[j])
		{
			return 0;
		}
	}
	return 1;
}


int is_valid_tags(const struct fieldstat_tag *tags, size_t n_tags)
{
	int i = 0;
	struct fieldstat_tag *tag = NULL;

	for(i = 0; i < (int)n_tags; i++)
	{
		tag = (struct fieldstat_tag *)&tags[i];
		if (0 == is_valid_field_name(tag->key))
		{
			return 0;
		}

		if(tag->value_type == 2)
		{
			if (0 == is_valid_tag_value(tag->value_str))
			{
				return 0;
			}
		}
	}
	return 1;
}

void escaping_special_chars(char* str)
{
	const char *escaped = ".";
	unsigned i = 0, j = 0;
	unsigned int len = strlen(str);
	if(len == 0)
	{
		return;
	}

	for(i = 0; i < len; i++)
	{
		for(j = 0; j < strlen(escaped); j++)
		{
			if(str[i] == escaped[j])
			{
				str[i] = '_';
			}
		}
	}

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

struct metric * get_metric(struct fieldstat_instance *instance, int metric_id)
{
	int block_index = 0;
	int block_in_index = 0;
	struct metric ** metrics_array = NULL;
	struct metric * metric = NULL;

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

void add_tags_to_metric(const struct fieldstat_tag tags[], size_t n_tag, char *tag_key[], char *tag_value[])
{
	int i = 0;
	struct fieldstat_tag *tag = NULL;
	char str_longlong[32] = {0};
	char str_double[32] = {0};

	for(i = 0; i < (int)n_tag; i++)
	{
		tag = (struct fieldstat_tag *)&tags[i];
		tag_key[i] = __str_dup(tag->key);
		switch(tag->value_type)
		{
			case 0:
				memset(str_longlong, 0, sizeof(str_longlong));
				snprintf(str_longlong, sizeof(str_longlong), "%lld", tag->value_int);
				tag_value[i] = __str_dup((const char *)str_longlong);
				break;
			case 1:
				memset(str_double, 0, sizeof(str_double));
				snprintf(str_double, sizeof(str_double), "%lf", tag->value_double);
				tag_value[i] = __str_dup((const char *)str_double);
				break;
			case 2:
				tag_value[i] = __str_dup(tag->value_str);
				break;
			default:
				assert(0);
				break;
		}
	}
}

struct metric * metric_new(enum field_type type, const char *field_name, const struct fieldstat_tag tags[], size_t n_tag)
{
	struct metric *metric = (struct metric*)calloc(1, sizeof(struct metric));
	metric->field_name = __str_dup(field_name);
	escaping_special_chars(metric->field_name);
	metric->field_type = type;
	metric->is_ratio = 0;
	metric->output_scaling = 1;
	metric->n_tag = n_tag;

	add_tags_to_metric(tags, n_tag, metric->tag_key, metric->tag_value);

	return metric;
}

void metric_free(struct metric *metric)
{
	int i = 0;
	struct histogram_t *h = NULL;

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

	if(metric->table)
	{
		metric->table = NULL;
	}
	if(metric->field_type == FIELD_TYPE_SUMMARY || 
	   metric->field_type == FILED_TYPE_HISTOGRAM)
	{
		h = &metric->histogram;
		if(h->changing != NULL)
		{
			hdr_close(h->changing);
			h->changing = NULL;
		}
		if(h->accumulated != NULL)
		{
			hdr_close(h->accumulated);
			h->accumulated = NULL;
		}
		if(h->previous_changed != NULL)
		{
			hdr_close(h->previous_changed);
			h->previous_changed = NULL;
		}
		if(h->bins != NULL)
		{
			free(h->bins);
			h->bins = NULL;
		}
	}

	free(metric);

	return;
}

struct metric ** read_metric_slot(struct fieldstat_instance *instance, int metric_id)
{
	int block_index = 0;
	int in_block_index = 0;
	struct metric ** metrics_block = NULL;

	if(instance == NULL)
	{
		return NULL;
	}
	block_index = metric_id / NUM_INIT_METRICS;
	in_block_index = metric_id % NUM_INIT_METRICS;
	if(in_block_index == 0)
	{
		assert(instance->metric_block_list[block_index] == NULL);
		instance->metric_block_list[block_index] = (struct metric **)calloc(NUM_INIT_METRICS, sizeof(struct metric *));	
	}
	else
	{
		while (instance->metric_block_list[block_index] == NULL);
	}
	metrics_block = (struct metric **)instance->metric_block_list[block_index];

	return (struct metric **)&(metrics_block[in_block_index]);
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


int startup_udp()
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
		already_sended_len += this_sended_len;			
	}
	
	return 0;
}

int fieldstat_set_output_interval(struct fieldstat_instance *instance, int milliseconds)
{
	if(instance->running == 1 || milliseconds <= 0 )
	{
		return -1;
	}
	instance->output_interval_ms = milliseconds;
	return 0;
}

int fieldstat_disable_background_thread(struct fieldstat_instance *instance)
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
	instance->output_type |= 8;

	return 0;
}

int fieldstat_set_line_protocol_server(struct fieldstat_instance *instance, const char *ip, unsigned short port)
{
	int ret = 0;
	if(instance == NULL || instance->running == 1)
	{
		return -1;
	}
	ret = enable_line_protocol_output(&instance->line_protocol_output, ip, port);

	if(ret == 0)
	{
		instance->line_protocol_output_enable = 1;
		instance->output_type |= 4;
	}

	return ret;
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
	instance->output_type |= 2;
	return 0;
}

int fieldstat_register(struct fieldstat_instance *instance, enum field_type type, const char *field_name, const struct fieldstat_tag tags[], size_t n_tag)
{
	int metric_id = 0;
	struct metric *metric = NULL;
	struct metric **metric_slot = NULL;
	if(!is_valid_field_name(field_name))
	{
		return -1;
	}
	if(n_tag > N_TAG_MAX)
	{
		return -1;
	}

	if(0 == is_valid_tags(tags, n_tag))
	{
		return -1;
	}

	metric_id = atomic_inc(&instance->metric_cnt) - 1;
	metric_slot = read_metric_slot(instance, metric_id);
	metric = metric_new(type, field_name, tags, n_tag);

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

long long get_metric_unit_val(struct metric *metric,enum field_calc_algo calc_type,int is_refer)
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
			assert(0);
			return 0;
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

long long read_metric_current_value(struct metric *metric)
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
	value += target->accumulated;

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
		ret = file_output(instance, interval_ms);
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
		usleep(instance->output_interval_ms * 1000);
	}
	return NULL;
}


void fieldstat_instance_start(struct fieldstat_instance *instance)
{
	instance->running = 1;
	clock_gettime(CLOCK_MONOTONIC,&(instance->last_output_time));
	if(instance->background_thread_disable == 0)
	{
		pthread_create(&(instance->background_thread), NULL, fieldstat_thread_schema_output, (void*)instance);
		instance->background_thread_is_created = 1;
	}
	//append instance to prometheus output
}

struct fieldstat_instance * fieldstat_instance_new(const char *name)
{
	struct fieldstat_instance *instance =  NULL;

	if(strlen(name) >= INSTANCE_NAME_LEN)
	{
		return NULL;
	}
	if(!is_valid_field_name(name))
	{
		return NULL;
	}

	instance = (struct fieldstat_instance *)calloc(1, sizeof(struct fieldstat_instance));
	strncpy(instance->name, name, strlen(name));
	instance->running = 0;
	instance->output_interval_ms = 2000;
	instance->background_thread_disable = 0;

	return instance;
}

void fieldstat_instance_free(struct fieldstat_instance *instance)
{
	int i = 0;
	void *pthread_ret;
	struct metric *metric = NULL;
	if(instance == NULL)
	{
		return;
	}

	if(instance->background_thread_is_created == 1)
	{
		pthread_cancel(instance->background_thread);
		pthread_join(instance->background_thread, &pthread_ret);
		instance->background_thread_is_created = 0;
		instance->running = 0;
	}

	if(instance->local_output_fp)
	{
		fclose(instance->local_output_fp);
		instance->local_output_fp = NULL;
		instance->local_output_enable = 0;
	}

	if(instance->line_protocol_output_enable == 1)
	{
		disable_line_protocol_output(&instance->line_protocol_output);
		instance->line_protocol_output_enable  = 0;
	}

	for(i = 0; i < instance->metric_cnt; i++)
	{
		metric = get_metric(instance, i);
		metric_free(metric);
	}

	for(i = 0; i < instance->table_num; i++)
	{
		table_metric_free(instance->table_metrics[i]);
		instance->table_metrics[i] = NULL;
	}

	if(instance->metric_block_list != NULL)
	{
		for(i = 0; i < BLOCK_LIST_SIZE; i++)
		{
			if(instance->metric_block_list[i] != NULL)
			{
				free(instance->metric_block_list[i]);
				instance->metric_block_list[i] = NULL;
			}
		}
	}
	free(instance);
	return;
}


struct table_metric* table_metric_new(const char *name, const char *column_name[], enum field_type column_type[], size_t n_column)
{
	int i = 0;
	struct table_metric *table_metric = (struct table_metric *)calloc(1, sizeof(struct table_metric));

	table_metric->column_cnt = (int)n_column;
	table_metric->name = __str_dup(name);
	escaping_special_chars(table_metric->name);

	for(i = 0; i < (int)n_column; i++)
	{
		table_metric->column_name[i] = __str_dup(column_name[i]);
		table_metric->column_type[i] = column_type[i];
		escaping_special_chars(table_metric->column_name[i]);
	}
	return table_metric;
}

void table_metric_free(struct table_metric *table)
{
	int i = 0;
	struct table_line *table_line = NULL;

	if(table == NULL)
	{
		return;
	}
	free(table->name);
	table->name = NULL;
	for(i = 0; i < (int)table->column_cnt; i++)
	{
		if(table->column_name[i] == NULL)
		{
			continue;
		}
		free(table->column_name[i]);
		table->column_name[i] = NULL;	
	}

	for(i = 0; i < table->line_cnt; i++)
	{
		table_line = read_table_line(table, i);
		table_line_free(table_line);
	}

	if(table->line_block != NULL)
	{
		for(i = 0; i < BLOCK_LIST_SIZE; i++)
		{
			if(table->line_block[i] == NULL)
			{
				continue;
			}
			free(table->line_block[i]);
			table->line_block[i] = NULL;
		}
	}
	table->column_cnt = 0;
	free(table);
	return;
}


 int fieldstat_register_table(struct fieldstat_instance *instance, const char *table_name, const char *column_name[], enum field_type column_type[], size_t n_column)
 {
	int table_id = 0;

	struct table_metric *table_metric = NULL;

	if(!is_valid_field_name(table_name))
	{
		return -1;
	}

	for(int i = 0; i < (int)n_column; i++)
	{
		if(!is_valid_field_name(column_name[i]))
		{
			return -1;
		}
		if(column_type[i] != FIELD_TYPE_GAUGE && column_type[i] != FIELD_TYPE_COUNTER)
		{
			return -1;
		}
	}

	if(n_column <= 0 || n_column > TABLE_COLUMN_SIZE)
	{
		return -1;
	}

	if(instance->table_num >= TABLE_MAX_NUM)
	{
		return -1;
	}

	table_id = atomic_inc(&instance->table_num) - 1;
	table_metric = table_metric_new(table_name, column_name, column_type, n_column);
	instance->table_metrics[table_id] = table_metric;

	return table_id;
 }

struct table_line ** read_table_line_slot(struct table_metric *table, int line_id)
{
	int block_index = 0;
	int in_block_index = 0;
	struct table_line **line = NULL;

	block_index = line_id / NUM_INIT_METRICS;
	in_block_index = line_id % NUM_INIT_METRICS;
	if(in_block_index == 0)
	{
		assert(table->line_block[block_index] == NULL);
		table->line_block[block_index] = (struct table_line **)calloc(NUM_INIT_METRICS, sizeof(struct table_line *)); 	
	}
	else
	{
		while (table->line_block[block_index] == NULL);
	}
	line = table->line_block[block_index];

	return (struct table_line **)&(line[in_block_index]);
}

struct table_line *table_line_new(const char *name, const struct fieldstat_tag tags[],size_t n_tag)
{
	struct table_line *table_line = (struct table_line *)calloc(1, sizeof(struct table_line));

	table_line->name = __str_dup(name);
	escaping_special_chars(table_line->name);
	table_line->n_tag = n_tag;

	add_tags_to_metric(tags, n_tag, table_line->tag_key, table_line->tag_value);

	return table_line;
}


void table_line_free(struct table_line *table_line)
{
	if(table_line == NULL)
	{
		return;
	}
	
	free(table_line->name);

	table_line->name = NULL;

	for(int i = 0; i < (int)table_line->n_tag; i++)
	{
		free(table_line->tag_key[i]);
		table_line->tag_key[i] = NULL;

		free(table_line->tag_value[i]);
		table_line->tag_value[i] = NULL;
	}
	table_line->n_tag = 0;

	free(table_line);
}



int fieldstat_register_table_row(struct fieldstat_instance * instance, int table_id, const char *row_name, const struct fieldstat_tag tags[], size_t n_tag, int output_metric_ids[])
{
	int metric_id = 0;
	struct metric *metric = NULL;

	int line_id = 0;
	int i = 0;

	struct table_metric *table = NULL;
	struct table_line **line_slot = NULL;
	struct table_line *table_line = NULL;

	if(table_id < 0 || table_id >= TABLE_MAX_NUM)
	{
		return -1;
	}
	if(!is_valid_field_name(row_name) || n_tag > N_TAG_MAX)
	{
		return -1;
	}

	if(0 == is_valid_tags(tags, n_tag))
	{
		return -1;
	}

	table = instance->table_metrics[table_id];
	line_id = atomic_inc(&(table->line_cnt)) - 1;
	line_slot = read_table_line_slot(table, line_id);
	table_line = table_line_new(row_name, tags, n_tag);

	for(i = 0; i < table->column_cnt; i++)
	{
		metric_id = fieldstat_register(instance, table->column_type[i], row_name, tags, n_tag);

		table_line->metric_id_belong_to_line[i] = metric_id;

		metric = get_metric(instance, metric_id);

		metric->table = table;
		metric->table_column_id = i;
/*
		metric->table_id = table_id;
		metric->table_column_name = __str_dup(table->column_name[i]); 
		metric->table_name = __str_dup(table->name);
		metric->belong_to_table = 1;
*/
		output_metric_ids[i] = metric_id;
	}
	*line_slot = table_line;
	return 0;
}

void metric_value_operate(struct metric *metric, enum field_op op, long long value)
{
	struct stat_unit_t *target = NULL;
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
			return;
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
}

static int fieldstat_value_operate(struct fieldstat_instance *instance, int metric_id, enum field_op op, long long value)
{
	struct metric * metric = NULL;

	if(metric_id >= instance->metric_cnt)
	{
		return -1;
	}

	metric = get_metric(instance, metric_id);

	metric_value_operate(metric, op, value);

	return 0;
}


int fieldstat_value_incrby(struct fieldstat_instance *instance, int metric_id, long long increment)
{
	int ret = 0;
	ret = fieldstat_value_operate(instance, metric_id, FS_OP_ADD, increment);
	return ret;
}

int fieldstat_value_set(struct fieldstat_instance *instance, int metric_id, long long retain)
{
	int ret = 0;
	ret = fieldstat_value_operate(instance, metric_id, FS_OP_SET, retain);
	return ret;
}

int fieldstat_value_decrby(struct fieldstat_instance *instance, int metric_id, long long decrment)
{
	int ret = 0;
	ret = fieldstat_value_operate(instance, metric_id, FS_OP_SUB, decrment);
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
	bins=(double*)calloc(comma_num+1, sizeof(double));
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


static int fieldstat_register_histogram_and_summary(struct fieldstat_instance *instance, enum field_type type, const char *field_name, const struct fieldstat_tag tags[], size_t n_tag, 
		const char * bins, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures, int output_window)
{
	struct metric *metric = NULL;
	struct metric **metric_slot = NULL;
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

	if(output_window != 0 && output_window != 1)
	{
		return -1;
	}

	metric_id = atomic_inc(&instance->metric_cnt) - 1;
	metric_slot = read_metric_slot(instance, metric_id);
	metric = metric_new(type, field_name, tags, n_tag);
	metric->output_window = output_window;

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


int fieldstat_register_histogram(struct fieldstat_instance *instance, const char *field_name,const struct fieldstat_tag tags[], size_t n_tag, 
		const char * bins, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures, int output_window)
{
	return fieldstat_register_histogram_and_summary(instance, FILED_TYPE_HISTOGRAM, field_name, tags, n_tag, 
		bins, lowest_trackable_value, highest_trackable_value, significant_figures, output_window);
}

int fieldstat_register_summary(struct fieldstat_instance *instance, const char *field_name, const struct fieldstat_tag tags[], size_t n_tag, 
		const char * bins, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures, int output_window)
{
	return fieldstat_register_histogram_and_summary(instance, FIELD_TYPE_SUMMARY, field_name, tags, n_tag, 
		bins, lowest_trackable_value, highest_trackable_value, significant_figures, output_window);
}


int enable_line_protocol_output(struct line_protocol_output *line_protocol_output, const char *ip, unsigned short port)
{
	if(line_protocol_output == NULL)
	{
		return -1;
	}

	if(1 != inet_pton(AF_INET, ip, (void *)&(line_protocol_output->server_ip)))
	{
		return -1;
	}
	line_protocol_output->server_port = port;
	line_protocol_output->send_socket = startup_udp();

	return 0;
}

void disable_line_protocol_output(struct line_protocol_output *line_protocol_output)
{
	if(line_protocol_output == NULL)
	{
		return;
	}

	if(line_protocol_output->send_socket != -1)
	{
		close(line_protocol_output->send_socket);
		line_protocol_output->send_socket = -1;
	}
	return;
}


int fieldstat_set_metric_ratio_para(struct fieldstat_instance *instance, int metric_id, int numerator_metric_id, int denominator_metric_id, int output_scaling)
{
	struct metric *metric = NULL;
	struct metric *metric_n = NULL;
	struct metric *metric_d = NULL;
	if(instance->running == 1)
	{
		return -1;
	}
	if(metric_id  >= instance->metric_cnt || numerator_metric_id  >= instance->metric_cnt ||denominator_metric_id >= instance->metric_cnt)
	{
		return -1;
	}
	
	metric   = get_metric(instance, metric_id);
	metric_n = get_metric(instance, numerator_metric_id);
	metric_d = get_metric(instance, denominator_metric_id);

	if(metric->field_type != FIELD_TYPE_GAUGE && metric->field_type != FIELD_TYPE_COUNTER)
	{
		return -1;
	}

	if(metric_n->field_type != FIELD_TYPE_GAUGE && metric_n->field_type != FIELD_TYPE_COUNTER)
	{
		return -1;
	}

	if(metric_d->field_type != FIELD_TYPE_GAUGE && metric_d->field_type != FIELD_TYPE_COUNTER)
	{
		return -1;
	}
	if(output_scaling == 0)
	{
		return -1;
	}

	metric->is_ratio = 1;
	metric->numerator_id = numerator_metric_id;
	metric->denominator_id = denominator_metric_id;
	metric->output_scaling = output_scaling;

	return 0;
}


int fieldstat_set_metric_invisible_para(struct fieldstat_instance *instance, int metric_id)
{
	struct metric *metric = NULL;
	if(instance->running == 1)
	{
		return -1;
	}
	if(metric_id >= instance->metric_cnt)
	{
		return -1;
	}
	
	metric = get_metric(instance, metric_id);
	metric->is_invisible = 1;

	if(metric->table != NULL)
	{
		metric->table->column_invisible[metric->table_column_id] = 1;
	}

	return 0;
}