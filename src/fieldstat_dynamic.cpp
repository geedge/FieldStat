#include "fieldstat_internal.h"

struct fieldstat_dynamic_instance * fieldstat_dynamic_instance_new(const char *name, int n_thread)
{
	struct fieldstat_dynamic_instance *instance = NULL;

	if(strlen(name) >= INSTANCE_NAME_LEN || n_thread < 1)
	{
		return NULL;
	}

	instance = (struct fieldstat_dynamic_instance *)calloc(sizeof(struct fieldstat_dynamic_instance), 1);

	strcpy(instance->name, name);
	instance->running = 0;
	instance->output_interval_ms = 2000;
	instance->background_thread_disable = 0;

	instance->n_thread = n_thread;
	instance->n_thread_dynamic_metric = (struct dynamic_metric **)calloc(sizeof(struct dynamic_metric *), instance->n_thread);

	return instance;
}

int fieldstat_dynamic_set_line_protocol_server(struct fieldstat_dynamic_instance *instance, const char *ip, unsigned short port)
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

int fieldstat_dynamic_disable_background_thread(struct fieldstat_dynamic_instance *instance)
{
	if(instance->running == 1)
	{
		return -1;
	}
	instance->background_thread_disable = 1;
	return 0;
}

int fieldstat_dynamic_set_output_interval(struct fieldstat_dynamic_instance *instance, int milliseconds)
{
	if(instance->running == 1 || milliseconds <= 0 )
	{
		return -1;
	}
	instance->output_interval_ms = milliseconds;
	return 0;
}


void fieldstat_dynamic_passive_output(struct fieldstat_dynamic_instance *instance)
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
	if(instance->line_protocol_output_enable)
	{
		ret = line_protocol_dynamic_metric_output(instance);
	}
	if(ret == -1)
	{
		return;
	}
	memcpy(&(instance->last_output_time),&this_output_time, sizeof(this_output_time));
}

void *fieldstat_dynamic_thread_schema_output(void *arg)
{
	struct fieldstat_dynamic_instance *instance = (struct fieldstat_dynamic_instance *)arg;
	while(instance->background_thread_disable == 0)
	{
		fieldstat_dynamic_passive_output(instance);
		usleep(instance->output_interval_ms * 1000);
	}
	return NULL;
}

void fieldstat_dynamic_instance_start(struct fieldstat_dynamic_instance *instance)
{
	instance->running = 1;
	clock_gettime(CLOCK_MONOTONIC, &(instance->last_output_time));
	if(instance->background_thread_disable == 0)
	{
		pthread_create(&(instance->cfg_mon_t), NULL, fieldstat_dynamic_thread_schema_output, (void*)instance);
	}
}

int fieldstat_register_dynamic_table(struct fieldstat_dynamic_instance *instance, const char *table_name, const char *column_name[], enum field_type column_type[], size_t n_column, unsigned int out_column_ids[])
{
	int table_id = 0;
	int i = 0;

	struct table_metric *table_metric = NULL;

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

	for(i = 0; i < (int)n_column; i++)
	{
		out_column_ids[i] = i;
	}

	return table_id;
}


static int build_dynamic_metric_key(int table_id, const char *field_name, const struct fieldstat_tag tags[], size_t n_tags, size_t out_key_size, char *out_key)
{
	int i = 0;
	int used_len = 0;
	struct fieldstat_tag *tag = NULL;

	used_len += snprintf(out_key + used_len, out_key_size - used_len, "%d%s", table_id, field_name);

	for(i = 0; i < (int)n_tags; i++)
	{
		tag = (struct fieldstat_tag *)&tags[i];
		switch(tag->value_type)
		{
			case 0:
				used_len += snprintf(out_key + used_len, out_key_size - used_len, "%s%lld", tag->key, tag->value_int);
				break;
			case 1:
				used_len += snprintf(out_key + used_len, out_key_size - used_len, "%s%lf", tag->key, tag->value_double);
				break;
			case 2:
				used_len += snprintf(out_key + used_len, out_key_size - used_len, "%s%s", tag->key, tag->value_str);
				break;
			default:
				assert(0);
				break;
		}
	}
	return  used_len;
}

static struct metric * read_dynamic_metric(struct fieldstat_dynamic_instance *instance, int table_id, int column_id, const char *field_name, const struct fieldstat_tag tags[], size_t n_tags, int thread_id)
{
	struct dynamic_metric **head = &instance->n_thread_dynamic_metric[thread_id];
	struct dynamic_metric *find = NULL;
 
	char dynamic_metric_key[512];

	build_dynamic_metric_key(table_id, field_name, tags, n_tags, sizeof(dynamic_metric_key), dynamic_metric_key);

	HASH_FIND_STR(*head, dynamic_metric_key, find);

	if(find == NULL)
	{
		return NULL;
	}
	if(table_id == -1)
	{
		return *(find->metrics);
	}

	return find->metrics[column_id];
}

static struct metric * create_dynamic_table_metric(struct fieldstat_dynamic_instance *instance, int table_id, unsigned int column_id, const char *row_name, const struct fieldstat_tag tags[], size_t n_tags, int thread_id)
{
	int i = 0;
	struct dynamic_metric **head = NULL;
	struct dynamic_metric *value = NULL;
	struct table_metric *table = NULL;
	struct metric *metric = NULL;

	head = &instance->n_thread_dynamic_metric[thread_id];

	value = (struct dynamic_metric *)calloc(sizeof(struct dynamic_metric), 1);
	build_dynamic_metric_key(table_id, row_name, tags, n_tags, sizeof(value->metric_key), value->metric_key);


	table = instance->table_metrics[table_id];
	value->metrics = (struct metric **)calloc(sizeof(struct metric *), table->column_cnt);
	for(i = 0; i < table->column_cnt; i ++)
	{
		metric = metric_new(table->column_type[i], row_name, tags, n_tags);

		switch(table->column_type[i])
		{
			case FIELD_TYPE_COUNTER:
				memset(&(metric->counter), 0, sizeof(metric->counter));
				break;
			case FIELD_TYPE_GAUGE:
				memset(&(metric->gauge), 0, sizeof(metric->gauge));
				break;
			default:
				assert(0);
				break;
		}
		metric->table_id = table_id;
		metric->table_column_name = __str_dup(table->column_name[i]); 
		metric->table_name = __str_dup(table->name);
		metric->belong_to_table = 1;

		value->metrics[i] = metric;
	}

	HASH_ADD_STR(*head, metric_key, value);

	return value->metrics[column_id];
}


static struct metric * create_dynamic_metric(struct fieldstat_dynamic_instance *instance, enum field_type type, const char *field_name, long long value, const struct fieldstat_tag tags[], size_t n_tags, int thread_id)
{
	struct dynamic_metric **head = NULL;
	struct dynamic_metric *insert = NULL;
	struct metric *metric = NULL;

	head = &instance->n_thread_dynamic_metric[thread_id];

	insert = (struct dynamic_metric *)calloc(sizeof(struct dynamic_metric), 1);
	build_dynamic_metric_key(-1, field_name, tags, n_tags, sizeof(insert->metric_key), insert->metric_key);

	insert->metrics = (struct metric **)calloc(sizeof(struct metric *), 1);
	metric = metric_new(type, field_name, tags, n_tags);

	switch(metric->field_type)
	{
		case FIELD_TYPE_COUNTER:
			memset(&(metric->counter), 0, sizeof(metric->counter));
			break;
		case FIELD_TYPE_GAUGE:
			memset(&(metric->gauge), 0, sizeof(metric->gauge));
			break;
		default:
			assert(0);
			break;
	}
	*(insert->metrics) = metric;

	HASH_ADD_STR(*head, metric_key, insert);

	return metric;
}



static int fieldstat_dynamic_metric_value_operate(struct fieldstat_dynamic_instance *instance, enum field_op op, enum field_type type, const char *field_name, long long value, const struct fieldstat_tag tags[], size_t n_tags, int thread_id)
{
	struct metric * metric = NULL;

	metric = read_dynamic_metric(instance, -1, -1, field_name, tags, n_tags, thread_id);
	if(metric == NULL)
	{
		metric = create_dynamic_metric(instance, type, field_name, value, tags, n_tags, thread_id);	
	}
	if(metric == NULL)
	{
		return -1;
	}

	metric_value_operate(metric, op, value);
	return 0;
}


int fieldstat_dynamic_metric_value_incrby(struct fieldstat_dynamic_instance *instance, enum field_type type, const char *field_name, long long value, const struct fieldstat_tag tags[], size_t n_tags, int thread_id)
{
	int ret = 0;
	fieldstat_dynamic_metric_value_operate(instance, FS_OP_ADD, type, field_name, value, tags, n_tags, thread_id);
	return ret;
}

int fieldstat_dynamic_metric_value_set(struct fieldstat_dynamic_instance *instance, enum field_type type, const char *field_name, long long value, const struct fieldstat_tag tags[], size_t n_tags, int thread_id)
{
	int ret = 0;
	fieldstat_dynamic_metric_value_operate(instance, FS_OP_SET, type, field_name, value, tags, n_tags, thread_id);
	return ret;
}

int fieldstat_dynamic_metric_value_decrby(struct fieldstat_dynamic_instance *instance, enum field_type type, const char *field_name, long long value, const struct fieldstat_tag tags[], size_t n_tags, int thread_id)
{
	int ret = 0;
	fieldstat_dynamic_metric_value_operate(instance, FS_OP_SUB, type, field_name, value, tags, n_tags, thread_id);
	return ret;
}


int fieldstat_dynamic_table_metric_value_operate(struct fieldstat_dynamic_instance *instance, enum field_op op, int table_id, unsigned int column_id, const char *row_name, long long value, const struct fieldstat_tag tags[], size_t n_tags, int thread_id)
{
	struct metric * metric = NULL;
	metric = read_dynamic_metric(instance, table_id, column_id, row_name, tags, n_tags, thread_id);
	if(metric == NULL)
	{
		metric = create_dynamic_table_metric(instance, table_id, column_id, row_name, tags, n_tags, thread_id);
	}
	if(metric == NULL)
	{
		return -1;
	}

	metric_value_operate(metric, op, value);
	return 0;
}

int fieldstat_dynamic_table_metric_value_incrby(struct fieldstat_dynamic_instance *instance, int table_id, unsigned int column_id, const char *row_name, long long value, const struct fieldstat_tag tags[], size_t n_tags, int thread_id)
{
	int ret = 0;
	fieldstat_dynamic_table_metric_value_operate( instance, FS_OP_ADD, table_id, column_id, row_name, value, tags, n_tags, thread_id);
	return ret;
}

int fieldstat_dynamic_table_metric_value_set(struct fieldstat_dynamic_instance *instance, int table_id, unsigned int column_id, const char *row_name, long long value, const struct fieldstat_tag tags[], size_t n_tags, int thread_id)
{
	int ret = 0;
	fieldstat_dynamic_table_metric_value_operate( instance, FS_OP_SET, table_id, column_id, row_name, value, tags, n_tags, thread_id);
	return ret;
}

int fieldstat_dynamic_table_metric_value_decrby(struct fieldstat_dynamic_instance *instance, int table_id, unsigned int column_id, const char *row_name, long long value, const struct fieldstat_tag tags[], size_t n_tags, int thread_id)
{
	int ret = 0;
	fieldstat_dynamic_table_metric_value_operate( instance, FS_OP_SUB, table_id, column_id, row_name, value, tags, n_tags, thread_id);
	return ret;
}

