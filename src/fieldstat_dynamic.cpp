#include "fieldstat_internal.h"

struct fieldstat_dynamic_instance * fieldstat_dynamic_instance_new(const char *name, int n_thread)
{
	struct fieldstat_dynamic_instance *instance = NULL;

	if(strlen(name) >= INSTANCE_NAME_LEN || n_thread < 1)
	{
		return NULL;
	}
	
	if(!is_valid_field_name(name))
	{
		return NULL;
	}

	instance = (struct fieldstat_dynamic_instance *)calloc(1, sizeof(struct fieldstat_dynamic_instance));

	strcpy(instance->name, name);
	instance->running = 0;
	instance->output_interval_ms = 2000;
	instance->background_thread_disable = 0;

	instance->n_thread = n_thread;
	instance->n_thread_dynamic_metric = (struct dynamic_metric **)calloc(instance->n_thread, sizeof(struct dynamic_metric *));

	return instance;
}

void fieldstat_dynamic_instance_free(struct fieldstat_dynamic_instance *instance)
{
	int i = 0;
	struct dynamic_metric **head = NULL;
	struct dynamic_metric *dyn_metric, *tmp_dyn_metric;
	void *pthread_ret;

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

	if(instance->line_protocol_output_enable == 1)
	{
		disable_line_protocol_output(&instance->line_protocol_output);
		instance->line_protocol_output_enable  = 0;
	}

	for(i = 0; i < instance->n_thread; i++)
	{
		head = &instance->n_thread_dynamic_metric[i];
		HASH_ITER(hh, *head, dyn_metric, tmp_dyn_metric)
		{
			HASH_DEL(*head, dyn_metric);
			if(dyn_metric->metrics)
			{
				int n_loop = 0;
				struct metric *metric = dyn_metric->metrics[0];
				if(metric->table)
				{
					n_loop = metric->table->column_cnt;
				}
				else
				{
					n_loop = 1;
				}
				for(int j = 0; j < n_loop; j ++)
				{
					metric_free(dyn_metric->metrics[j]);
					dyn_metric->metrics[j] = NULL;
				}
				free(dyn_metric->metrics);
				dyn_metric->metrics = NULL;
			}
			free(dyn_metric);
		}
		instance->n_thread_dynamic_metric[i] = NULL;
	}

	for(i = 0; i < instance->table_num; i++)
	{
		table_metric_free(instance->table_metrics[i]);
		instance->table_metrics[i] = NULL;
	}

	free(instance->n_thread_dynamic_metric);
	instance->n_thread_dynamic_metric = NULL;
	free(instance);

	return;
}


int fieldstat_dynamic_set_line_protocol_server(struct fieldstat_dynamic_instance *instance, const char *ip, unsigned short port)
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
		printf("Passive return: output ret -1\n");
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
		pthread_create(&(instance->background_thread), NULL, fieldstat_dynamic_thread_schema_output, (void*)instance);
		instance->background_thread_is_created = 1;
	}
}

int fieldstat_register_dynamic_table(struct fieldstat_dynamic_instance *instance, const char *table_name, const char *column_name[], enum field_type column_type[], size_t n_column, unsigned int out_column_ids[])
{
	int table_id = 0;
	int i = 0;

	struct table_metric *table_metric = NULL;

	if(!is_valid_field_name(table_name))
	{
		return -1;
	}

	for(i = 0; i < (int)n_column; i++)
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

	value = (struct dynamic_metric *)calloc(1, sizeof(struct dynamic_metric));
	build_dynamic_metric_key(table_id, row_name, tags, n_tags, sizeof(value->metric_key), value->metric_key);


	table = instance->table_metrics[table_id];
	value->metrics = (struct metric **)calloc(table->column_cnt, sizeof(struct metric *));
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
		metric->table = table;
		metric->table_column_id = i;
/*
		metric->table_id = table_id;
		metric->table_column_name = __str_dup(table->column_name[i]); 
		metric->table_name = __str_dup(table->name);
		metric->belong_to_table = 1;
		metric->table_column_cnt = table->column_cnt;
*/
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

	insert = (struct dynamic_metric *)calloc(1, sizeof(struct dynamic_metric));
	build_dynamic_metric_key(-1, field_name, tags, n_tags, sizeof(insert->metric_key), insert->metric_key);

	insert->metrics = (struct metric **)calloc(1, sizeof(struct metric *));
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
	if(!is_valid_field_name(field_name))
	{
		return -1;
	}

	if(0 == is_valid_tags(tags, n_tags))
	{
		return -1;
	}

	if(type != FIELD_TYPE_GAUGE && type != FIELD_TYPE_COUNTER)
	{
		return -1;
	}

	ret = fieldstat_dynamic_metric_value_operate(instance, FS_OP_ADD, type, field_name, value, tags, n_tags, thread_id);
	return ret;
}

int fieldstat_dynamic_metric_value_set(struct fieldstat_dynamic_instance *instance, enum field_type type, const char *field_name, long long value, const struct fieldstat_tag tags[], size_t n_tags, int thread_id)
{
	int ret = 0;
	if(!is_valid_field_name(field_name))
	{
		return -1;
	}
	if(0 == is_valid_tags(tags, n_tags))
	{
		return -1;
	}
	if(type != FIELD_TYPE_GAUGE && type != FIELD_TYPE_COUNTER)
	{
		return -1;
	}

	ret = fieldstat_dynamic_metric_value_operate(instance, FS_OP_SET, type, field_name, value, tags, n_tags, thread_id);
	return ret;
}

int fieldstat_dynamic_metric_value_decrby(struct fieldstat_dynamic_instance *instance, enum field_type type, const char *field_name, long long value, const struct fieldstat_tag tags[], size_t n_tags, int thread_id)
{
	int ret = 0;
	if(!is_valid_field_name(field_name))
	{
		return -1;
	}
	if(0 == is_valid_tags(tags, n_tags))
	{
		return -1;
	}

	if(type != FIELD_TYPE_GAUGE && type != FIELD_TYPE_COUNTER)
	{
		return -1;
	}

	ret = fieldstat_dynamic_metric_value_operate(instance, FS_OP_SUB, type, field_name, value, tags, n_tags, thread_id);
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
	if(!is_valid_field_name(row_name))
	{
		return -1;
	}
	if(0 == is_valid_tags(tags, n_tags))
	{
		return -1;
	}

	ret = fieldstat_dynamic_table_metric_value_operate( instance, FS_OP_ADD, table_id, column_id, row_name, value, tags, n_tags, thread_id);
	return ret;
}

int fieldstat_dynamic_table_metric_value_set(struct fieldstat_dynamic_instance *instance, int table_id, unsigned int column_id, const char *row_name, long long value, const struct fieldstat_tag tags[], size_t n_tags, int thread_id)
{
	int ret = 0;
	if(!is_valid_field_name(row_name))
	{
		return -1;
	}
	if(0 == is_valid_tags(tags, n_tags))
	{
		return -1;
	}

	ret = fieldstat_dynamic_table_metric_value_operate( instance, FS_OP_SET, table_id, column_id, row_name, value, tags, n_tags, thread_id);
	return ret;
}

int fieldstat_dynamic_table_metric_value_decrby(struct fieldstat_dynamic_instance *instance, int table_id, unsigned int column_id, const char *row_name, long long value, const struct fieldstat_tag tags[], size_t n_tags, int thread_id)
{
	int ret = 0;
	if(!is_valid_field_name(row_name))
	{
		return -1;
	}
	if(0 == is_valid_tags(tags, n_tags))
	{
		return -1;
	}

	ret = fieldstat_dynamic_table_metric_value_operate( instance, FS_OP_SUB, table_id, column_id, row_name, value, tags, n_tags, thread_id);
	return ret;
}


static long long dynamic_metric_value_read(struct fieldstat_dynamic_instance *instance, int table_id, unsigned int column_id, const char *field_name, const struct fieldstat_tag tags[], size_t n_tags, int thread_id)
{
	long long value = 0;
	struct metric * metric = NULL;
	metric = read_dynamic_metric(instance, table_id, column_id, field_name, tags, n_tags, thread_id);
	if(metric == NULL)
	{
		return 0;
	}
	value = read_metric_current_value(metric);

	return value;
}

long long fieldstat_dynamic_metric_value_get(struct fieldstat_dynamic_instance *instance, const char *field_name, const struct fieldstat_tag tags[], size_t n_tags, int thread_id)
{
	long long value = 0;
	value = dynamic_metric_value_read(instance, -1, -1, field_name, tags, n_tags, thread_id);
	return value;
}

long long fieldstat_dynamic_table_metric_value_get(struct fieldstat_dynamic_instance *instance, int table_id, unsigned int column_id, const char *row_name, const struct fieldstat_tag tags[], size_t n_tags, int thread_id)
{
	long long value = 0;
	value = dynamic_metric_value_read(instance, table_id, column_id, row_name, tags, n_tags, thread_id);
	return value;
}
