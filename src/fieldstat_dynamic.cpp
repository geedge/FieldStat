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

	strncpy(instance->name, name, strlen(name));
	
	instance->running = 0;
	instance->output_interval_ms = 2000;
	instance->background_thread_disable = 0;

	instance->n_thread = n_thread;
	instance->n_thread_dynamic_metric = (struct dynamic_metric **)calloc(instance->n_thread, sizeof(struct dynamic_metric *));

#if USING_SPINLOCK
	instance->uthash_locks = (struct uthash_spinlock *)calloc(n_thread, sizeof(struct uthash_spinlock));
	for(int	i = 0; i < n_thread; i++)
	{
		struct uthash_spinlock *uthash_lock = instance->uthash_locks + i;
		pthread_spin_init(&(uthash_lock->lock), PTHREAD_PROCESS_SHARED);
	}
#endif
#if USING_RWLOCK
	instance->uthash_locks = (struct uthash_rwlock *)calloc(n_thread, sizeof(struct uthash_rwlock));
	for(int	i = 0; i < n_thread; i++)
	{
		struct uthash_rwlock *uthash_lock = instance->uthash_locks + i;
		pthread_rwlock_init(&(uthash_lock->lock), NULL);
	}
#endif
#if USING_MUTEX
	instance->uthash_locks = (struct uthash_mutex *)calloc(n_thread, sizeof(struct uthash_mutex));
	for(int	i = 0; i < n_thread; i++)
	{
		struct uthash_mutex *uthash_lock = instance->uthash_locks + i;
		pthread_mutex_init(&(uthash_lock->lock), NULL);
	}
#endif

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
#if USING_SPINLOCK
	if(instance->uthash_locks)
	{
		for(i = 0; i < instance->n_thread; i++)
		{
			struct uthash_spinlock *uthash_lock = instance->uthash_locks + i;
			pthread_spin_destroy(&(uthash_lock->lock));
		}
		free((void *)instance->uthash_locks);
		instance->uthash_locks = NULL;
	}
#endif
#if USING_RWLOCK
	if(instance->uthash_locks)
	{
		for(i = 0; i < instance->n_thread; i++)
		{
			struct uthash_rwlock *uthash_lock = instance->uthash_locks + i;
			pthread_rwlock_destroy(&(uthash_lock->lock));
		}
		free((void *)instance->uthash_locks);
		instance->uthash_locks = NULL;
	}
#endif
#if USING_MUTEX
	if(instance->uthash_locks)
	{
		for(i = 0; i < instance->n_thread; i++)
		{
			struct uthash_mutex *uthash_lock = instance->uthash_locks + i;
			pthread_mutex_destroy(&(uthash_lock->lock));
		}
		free((void *)instance->uthash_locks);
		instance->uthash_locks = NULL;
	}
#endif
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


static int build_dynamic_metric_key(int table_id, const char *field_name, const struct field_tag tags[], size_t n_tags, size_t out_key_size, char *out_key) {
    unsigned int used_len = 0;

    /* part1: field name */
    unsigned int field_name_len = strlen(field_name) + 1;

    if(field_name_len > out_key_size) 
    {
        return 0;
    }

    memcpy(out_key + used_len, field_name, field_name_len);
    //escaping_special_chars(out_key);
    used_len += field_name_len;

    /* part2: table id */
    if(used_len + sizeof(table_id) > out_key_size) 
    {
        return 0;
    }
    memcpy(out_key + used_len, &table_id, sizeof(table_id));
    used_len += sizeof(table_id);

    /* part3: tags and value */
    for (unsigned int i = 0; i < n_tags; i++) {
        struct field_tag *tag = (struct field_tag *) &tags[i];

        /* tag key len */
        unsigned int tag_key_len = strlen(tag->key) + 1;
        unsigned int tag_value_len;

        if(used_len + tag_key_len > out_key_size) 
        {
            return 0;
        }
        memcpy(out_key + used_len, tag->key, tag_key_len);
        used_len += tag_key_len;

        switch (tag->value_type) {
            case 0:
                if(used_len + sizeof(tag->value_int) > out_key_size) 
                {
                    return 0;
                }
                memcpy(out_key + used_len, &tag->value_int, sizeof(tag->value_int));
                used_len += sizeof(tag->value_int);
                break;
            case 1:
                if(used_len + sizeof(tag->value_double) > out_key_size) 
                {
                    return 0;
                }

                memcpy(out_key + used_len, &tag->value_double, sizeof(tag->value_double));
                used_len += sizeof(tag->value_double);
                break;
            case 2:
                tag_value_len = strlen(tag->value_str) + 1;
                if(used_len + tag_value_len > out_key_size)
                {
                    return 0;
                }

                memcpy(out_key + used_len, tag->value_str, tag_value_len);
                used_len += tag_value_len;
                break;

            default:
                assert(0);
                break;
        }
    }

    assert(used_len < out_key_size);
    return (int) used_len;
}

static struct metric * read_dynamic_metric(struct fieldstat_dynamic_instance *instance, int table_id, int column_id, const char *field_name, const struct field_tag tags[], size_t n_tags, int thread_id)
{
	struct dynamic_metric **head = &instance->n_thread_dynamic_metric[thread_id];
	struct dynamic_metric *find = NULL;

	char dynamic_metric_key[METRIC_SIZE];
    unsigned int dynamic_metric_keylen = 0;
    dynamic_metric_keylen = build_dynamic_metric_key(table_id, field_name, tags, n_tags, sizeof(dynamic_metric_key), dynamic_metric_key);
    if(dynamic_metric_keylen == 0)
    {
        return NULL;
    }

#if USING_SPINLOCK
	struct uthash_spinlock *uthash_lock = instance->uthash_locks + thread_id;
	pthread_spin_lock(&(uthash_lock->lock));
#endif
#if USING_RWLOCK
	struct uthash_rwlock *uthash_lock = instance->uthash_locks + thread_id;
	pthread_rwlock_rdlock(&(uthash_lock->lock));
#endif

#if USING_MUTEX
	struct uthash_mutex *uthash_lock = instance->uthash_locks + thread_id;
	pthread_mutex_lock(&(uthash_lock->lock));
#endif

    HASH_FIND(hh, *head, dynamic_metric_key, dynamic_metric_keylen, find);
#if USING_SPINLOCK
	pthread_spin_unlock(&(uthash_lock->lock));
#endif
#if USING_RWLOCK
	pthread_rwlock_unlock(&(uthash_lock->lock));
#endif
#if USING_MUTEX
	pthread_mutex_unlock(&(uthash_lock->lock));
#endif
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

static struct metric * create_dynamic_table_metric(struct fieldstat_dynamic_instance *instance, int table_id, unsigned int column_id, const char *row_name, const struct field_tag tags[], size_t n_tags, int thread_id)
{
	int i = 0;
	struct dynamic_metric **head = NULL;
	struct dynamic_metric *value = NULL;
	struct table_metric *table = NULL;
	struct metric *metric = NULL;

	head = &instance->n_thread_dynamic_metric[thread_id];

    if(!is_valid_field_name(row_name))
    {
        return NULL;
    }
    if(0 == is_valid_tags(tags, n_tags))
    {
        return NULL;
    }

	value = (struct dynamic_metric *)calloc(1, sizeof(struct dynamic_metric));

    unsigned int metric_keylen;
	metric_keylen = build_dynamic_metric_key(table_id, row_name, tags, n_tags, sizeof(value->metric_key), value->metric_key);
    if(metric_keylen == 0)
    {
        free(value);
        value = NULL;
        return NULL;
    }

    value->metric_keylen = metric_keylen;

	table = instance->table_metrics[table_id];
	value->metrics = (struct metric **)calloc(table->column_cnt, sizeof(struct metric *));
	for(i = 0; i < table->column_cnt; i ++)
	{
		metric = metric_new(table->column_type[i], row_name, tags, n_tags);
		metric->is_atomic_counter = 1;
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

#if USING_SPINLOCK
	struct uthash_spinlock *uthash_lock = instance->uthash_locks + thread_id;
	pthread_spin_lock(&(uthash_lock->lock));
#endif
#if USING_RWLOCK
	struct uthash_rwlock *uthash_lock = instance->uthash_locks + thread_id;
	pthread_rwlock_wrlock(&(uthash_lock->lock));
#endif
#if USING_MUTEX
	struct uthash_mutex *uthash_lock = instance->uthash_locks + thread_id;
	pthread_mutex_lock(&(uthash_lock->lock));
#endif
    HASH_ADD_KEYPTR(hh, *head, value->metric_key, value->metric_keylen, value);
#if USING_SPINLOCK
	pthread_spin_unlock(&(uthash_lock->lock));
#endif
#if USING_RWLOCK
	pthread_rwlock_unlock(&(uthash_lock->lock));
#endif
#if USING_MUTEX
	pthread_mutex_unlock(&(uthash_lock->lock));
#endif
	return value->metrics[column_id];
}


static struct metric * create_dynamic_metric(struct fieldstat_dynamic_instance *instance, enum field_type type, const char *field_name, long long value, const struct field_tag tags[], size_t n_tags, int thread_id)
{
	struct dynamic_metric **head = NULL;
	struct dynamic_metric *insert = NULL;
	struct metric *metric = NULL;

	head = &instance->n_thread_dynamic_metric[thread_id];
    if(!is_valid_field_name(field_name))
    {
        return NULL;
    }

    if(0 == is_valid_tags(tags, n_tags))
    {
        return NULL;
    }

	insert = (struct dynamic_metric *)calloc(1, sizeof(struct dynamic_metric));
    insert->metric_keylen = build_dynamic_metric_key(-1, field_name, tags, n_tags, sizeof(insert->metric_key), insert->metric_key);
    if(insert->metric_keylen == 0)
    {
        free(insert);
        insert = NULL;
        return NULL;
    }

	insert->metrics = (struct metric **)calloc(1, sizeof(struct metric *));
	metric = metric_new(type, field_name, tags, n_tags);
	metric->is_atomic_counter = 1;
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
#if USING_SPINLOCK
	struct uthash_spinlock *uthash_lock = instance->uthash_locks + thread_id;
	pthread_spin_lock(&(uthash_lock->lock));
#endif
#if USING_RWLOCK
	struct uthash_rwlock *uthash_lock = instance->uthash_locks + thread_id;
	pthread_rwlock_wrlock(&(uthash_lock->lock));
#endif
#if USING_MUTEX
	struct uthash_mutex *uthash_lock = instance->uthash_locks + thread_id;
	pthread_mutex_lock(&(uthash_lock->lock));
#endif
    HASH_ADD_KEYPTR(hh, *head, insert->metric_key, insert->metric_keylen, insert);
#if USING_SPINLOCK
	pthread_spin_unlock(&(uthash_lock->lock));
#endif
#if USING_RWLOCK
	pthread_rwlock_unlock(&(uthash_lock->lock));
#endif
#if USING_MUTEX
	pthread_mutex_unlock(&(uthash_lock->lock));
#endif
	return metric;
}


static int fieldstat_dynamic_metric_value_operate(struct fieldstat_dynamic_instance *instance, enum field_op op, enum field_type type, const char *field_name, long long value, const struct field_tag tags[], size_t n_tags, int thread_id)
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


int fieldstat_dynamic_metric_value_incrby(struct fieldstat_dynamic_instance *instance, enum field_type type, const char *field_name, long long value, const struct field_tag tags[], size_t n_tags, int thread_id)
{
	return fieldstat_dynamic_metric_value_operate(instance, FS_OP_ADD, type, field_name, value, tags, n_tags, thread_id);
}

int fieldstat_dynamic_metric_value_set(struct fieldstat_dynamic_instance *instance, enum field_type type, const char *field_name, long long value, const struct field_tag tags[], size_t n_tags, int thread_id)
{
	return fieldstat_dynamic_metric_value_operate(instance, FS_OP_SET, type, field_name, value, tags, n_tags, thread_id);
}

int fieldstat_dynamic_metric_value_decrby(struct fieldstat_dynamic_instance *instance, enum field_type type, const char *field_name, long long value, const struct field_tag tags[], size_t n_tags, int thread_id)
{
	return fieldstat_dynamic_metric_value_operate(instance, FS_OP_SUB, type, field_name, value, tags, n_tags, thread_id);
}

int fieldstat_dynamic_table_metric_value_operate(struct fieldstat_dynamic_instance *instance, enum field_op op, int table_id, unsigned int column_id, const char *row_name, long long value, const struct field_tag tags[], size_t n_tags, int thread_id)
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

int fieldstat_dynamic_table_metric_value_incrby(struct fieldstat_dynamic_instance *instance, int table_id, unsigned int column_id, const char *row_name, long long value, const struct field_tag tags[], size_t n_tags, int thread_id)
{
	return fieldstat_dynamic_table_metric_value_operate( instance, FS_OP_ADD, table_id, column_id, row_name, value, tags, n_tags, thread_id);
}

int fieldstat_dynamic_table_metric_value_set(struct fieldstat_dynamic_instance *instance, int table_id, unsigned int column_id, const char *row_name, long long value, const struct field_tag tags[], size_t n_tags, int thread_id)
{
	return fieldstat_dynamic_table_metric_value_operate(instance, FS_OP_SET, table_id, column_id, row_name, value, tags, n_tags, thread_id);
}

int fieldstat_dynamic_table_metric_value_decrby(struct fieldstat_dynamic_instance *instance, int table_id, unsigned int column_id, const char *row_name, long long value, const struct field_tag tags[], size_t n_tags, int thread_id)
{
	return fieldstat_dynamic_table_metric_value_operate( instance, FS_OP_SUB, table_id, column_id, row_name, value, tags, n_tags, thread_id);
}

static long long dynamic_metric_value_read(struct fieldstat_dynamic_instance *instance, int table_id, unsigned int column_id, const char *field_name, const struct field_tag tags[], size_t n_tags, int thread_id)
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

long long fieldstat_dynamic_metric_value_get(struct fieldstat_dynamic_instance *instance, const char *field_name, const struct field_tag tags[], size_t n_tags, int thread_id)
{
	long long value = 0;
	value = dynamic_metric_value_read(instance, -1, -1, field_name, tags, n_tags, thread_id);
	return value;
}

long long fieldstat_dynamic_table_metric_value_get(struct fieldstat_dynamic_instance *instance, int table_id, unsigned int column_id, const char *row_name, const struct field_tag tags[], size_t n_tags, int thread_id)
{
	long long value = 0;
	value = dynamic_metric_value_read(instance, table_id, column_id, row_name, tags, n_tags, thread_id);
	return value;
}

static struct metric **read_dynamic_row_metrics(
    struct fieldstat_dynamic_instance *instance, 
    char *metric_key, 
    size_t metric_keylen,
    int thread_id)
{
    struct dynamic_metric **head = NULL; 
    struct dynamic_metric *find = NULL;

    head = &instance->n_thread_dynamic_metric[thread_id];
#if USING_SPINLOCK
	struct uthash_spinlock *uthash_lock = instance->uthash_locks + thread_id;
	pthread_spin_lock(&(uthash_lock->lock));
#endif
#if USING_RWLOCK
	struct uthash_rwlock *uthash_lock = instance->uthash_locks + thread_id;
	pthread_rwlock_rdlock(&(uthash_lock->lock));
#endif
#if USING_MUTEX
	struct uthash_mutex *uthash_lock = instance->uthash_locks + thread_id;
	pthread_mutex_lock(&(uthash_lock->lock));
#endif
    HASH_FIND(hh, *head, metric_key, metric_keylen, find);
#if USING_SPINLOCK
	pthread_spin_unlock(&(uthash_lock->lock));
#endif
#if USING_RWLOCK
	pthread_rwlock_unlock(&(uthash_lock->lock));
#endif
#if USING_MUTEX
	pthread_mutex_unlock(&(uthash_lock->lock));
#endif
    if(find == NULL)
    {
        return NULL;
    }

    return find->metrics;
}

static struct metric **create_dynamic_table_row_metrics(
    struct fieldstat_dynamic_instance *instance, 
    int table_id,
    const char *row_name, 
    const struct field_tag tags[], 
    size_t n_tags,
    int thread_id, 
    char *metric_key,
    unsigned metric_keylen)
{
	int i = 0;
	struct dynamic_metric **head = NULL;
	struct dynamic_metric *value = NULL;
	struct table_metric *table = NULL;
	struct metric *metric = NULL;

	head = &instance->n_thread_dynamic_metric[thread_id];

    if(!is_valid_field_name(row_name))
    {
        return NULL;
    }
    if(0 == is_valid_tags(tags, n_tags))
    {
        return NULL;
    }

	value = (struct dynamic_metric *)calloc(1, sizeof(struct dynamic_metric));

    value->metric_keylen = metric_keylen;
    memcpy(value->metric_key, metric_key, metric_keylen);

	table = instance->table_metrics[table_id];
	value->metrics = (struct metric **)calloc(table->column_cnt, 
                                              sizeof(struct metric *));
	for(i = 0; i < table->column_cnt; i ++)
	{
		metric = metric_new(table->column_type[i], row_name, tags, n_tags);
		metric->is_atomic_counter = 1;
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
		value->metrics[i] = metric;
	}
#if USING_SPINLOCK
	struct uthash_spinlock *uthash_lock = instance->uthash_locks + thread_id;
	pthread_spin_lock(&(uthash_lock->lock));
#endif
#if USING_RWLOCK
	struct uthash_rwlock *uthash_lock = instance->uthash_locks + thread_id;
	pthread_rwlock_wrlock(&(uthash_lock->lock));
#endif
#if USING_MUTEX
	struct uthash_mutex *uthash_lock = instance->uthash_locks + thread_id;
	pthread_mutex_lock(&(uthash_lock->lock));
#endif
    HASH_ADD_KEYPTR(hh, *head, value->metric_key, value->metric_keylen, value);
#if USING_SPINLOCK
	pthread_spin_unlock(&(uthash_lock->lock));
#endif
#if USING_RWLOCK
	pthread_rwlock_unlock(&(uthash_lock->lock));
#endif
#if USING_MUTEX
	pthread_mutex_unlock(&(uthash_lock->lock));
#endif
	return value->metrics;
}

static int table_row_metric_values_operate(
    struct fieldstat_dynamic_instance *instance,
    int table_id,
    const char *row_name,
    long long values[],
    size_t n_values,
    const struct field_tag tags[],
    size_t n_tags,
    int thread_id,
    enum field_op op)
{
    struct metric **metrics = NULL;
    char metric_key[METRIC_SIZE];
    unsigned int metric_keylen = 0;

    metric_keylen = build_dynamic_metric_key(table_id, row_name, tags, n_tags, 
                                             sizeof(metric_key), metric_key);
    if(metric_keylen == 0)
    {
        return -1;
    }

    metrics = read_dynamic_row_metrics(instance, metric_key, metric_keylen, 
                                       thread_id);
    if(metrics == NULL)
    {
        metrics = create_dynamic_table_row_metrics(instance, table_id, row_name, 
                                                   tags, n_tags, thread_id, 
                                                   metric_key, metric_keylen);
    }
    if(metrics == NULL)
    {
        return -1;
    }
    for(int i = 0; i <(int)n_values; i++)
    {
        metric_value_operate(metrics[i], op, values[i]);   
    }

    //metric_value_operate(metric, FS_OP_ADD, value);
    return 0;
}

int fieldstat_dynamic_table_row_metric_values_incrby(
    struct fieldstat_dynamic_instance *instance,
    int table_id,
    const char *row_name,
    long long values[],
    size_t n_values,
    const struct field_tag tags[],
    size_t n_tags,
    int thread_id)
{
    int ret = 0;
    ret = table_row_metric_values_operate(instance, table_id, row_name, 
                                          values, n_values, tags, n_tags,
                                          thread_id, FS_OP_ADD);
    return ret;
}

int fieldstat_dynamic_table_row_metric_values_decrby(
    struct fieldstat_dynamic_instance *instance,
    int table_id,
    const char *row_name,
    long long values[],
    size_t n_values,
    const struct field_tag tags[],
    size_t n_tags,
    int thread_id)
{
    int ret = 0;
    ret = table_row_metric_values_operate(instance, table_id, row_name, 
                                          values, n_values, tags, n_tags,
                                          thread_id, FS_OP_SUB);
    return ret;
}

int fieldstat_dynamic_table_row_metric_values_set(
    struct fieldstat_dynamic_instance *instance,
    int table_id,
    const char *row_name,
    long long values[],
    size_t n_values,
    const struct field_tag tags[],
    size_t n_tags,
    int thread_id)
{
    int ret = 0;
    ret = table_row_metric_values_operate(instance, table_id, row_name, 
                                          values, n_values, tags, n_tags,
                                          thread_id, FS_OP_SET);
    return ret;
}


unsigned int fieldstat_dynamic_read_htable_item_cnt(
    struct fieldstat_dynamic_instance *instance,
    int thread_id)
{
	struct dynamic_metric **head = NULL; 
	head = &instance->n_thread_dynamic_metric[thread_id];	
	return HASH_COUNT(*head);
}


unsigned int fieldstat_dynamic_read_metrics_cnt(
	struct fieldstat_dynamic_instance *instance,
	int thread_id)
{
	struct dynamic_metric **head = NULL;
	struct dynamic_metric *dyn_metric, *tmp_dyn_metric;
	unsigned int n_metrics = 0;

	head = &instance->n_thread_dynamic_metric[thread_id];

	HASH_ITER(hh, *head, dyn_metric, tmp_dyn_metric)
	{
		struct metric **metrics = dyn_metric->metrics;
		struct metric  *metric  = metrics[0];
		if(metric->table)
		{
			n_metrics += metric->table->column_cnt;
		}
		else 
		{
			n_metrics++;
		}
	}
	return n_metrics;
}
