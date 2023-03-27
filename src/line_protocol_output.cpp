#include "fieldstat_internal.h"


static void flush_send_buf(struct line_protocol_output *line_protocol_output)
{
	if(line_protocol_output == NULL || line_protocol_output->send_buf_offset == 0)
	{
		return;
	}

	if(line_protocol_output->server_ip > 0 && line_protocol_output->server_port > 0)
	{
		send_udp(line_protocol_output->send_socket, line_protocol_output->server_ip,
				line_protocol_output->server_port, line_protocol_output->send_buf, line_protocol_output->send_buf_offset);

	}
	line_protocol_output->send_buf_offset = 0;
	memset(line_protocol_output->send_buf, 0, sizeof(line_protocol_output->send_buf));

	return;
}


static void send_line_buf(struct line_protocol_output *line_protocol_output, char *line_buf, unsigned int line_buf_len)
{
	if(line_protocol_output == NULL || line_buf == NULL)
	{
		return;
	}

	if(UDP_PAYLOAD_SIZE - line_protocol_output->send_buf_offset < line_buf_len)
	{
		flush_send_buf(line_protocol_output);
	}

	line_protocol_output->send_buf_offset += snprintf(line_protocol_output->send_buf + line_protocol_output->send_buf_offset,
													sizeof(line_protocol_output->send_buf) - line_protocol_output->send_buf_offset,
													"%s", line_buf);
	return;
}


static int add_measurement(const char* measurement, char *line_buf, unsigned int line_buf_size)
{
	int used_len = 0;

	used_len = snprintf(line_buf, line_buf_size, "%s", measurement);

	return used_len;	
}


static int add_default_tag_set(const char *instance_name, const char *table_name, char *line_buf, unsigned int line_buf_size)
{
	int used_len = 0;

	if(instance_name == NULL)
	{
		return 0;
	}

	if(table_name)
	{
		used_len += snprintf(line_buf, line_buf_size, ",app_name=%s,table_name=%s", instance_name, table_name);
	}
	else
	{
		used_len += snprintf(line_buf, line_buf_size, ",app_name=%s", instance_name);
	}

	return used_len;
}


static int add_user_tag_set(struct metric *metric, char *line_buf, unsigned int line_buf_size)
{
	int used_len = 0;

	for(int i = 0; i < (int)metric->n_tag; i++)
	{
		used_len += snprintf(line_buf + used_len, line_buf_size - used_len, ",%s=%s", metric->tag_key[i], metric->tag_value[i]);
	}

	return used_len;	
}

static long long read_single_metric_value(struct metric *metric)
{
	long long value = 0;
	metric->field_type == FIELD_TYPE_GAUGE
		?value = get_metric_unit_val(metric, FS_CALC_CURRENT, 0)
		:value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
	return value;
}

static int is_send_table_row(long long *row_value, int n_column)
{
	int i = 0;
	int is_send = 0;

	for(i = 0; i < n_column; i++)
	{
		if(row_value[i] != 0)
		{
			is_send = 1;
			break;
		}
	}
	return is_send;
}


static int add_field_set(char *field_key, long long field_value, char *line_buf, int line_buf_size)
{
	int used_len = 0;

	used_len += snprintf(line_buf, line_buf_size, "%s=%lld", field_key, field_value);

	return used_len;
}


static int add_table_row_field_set(char *column_name[], long long *row_value, int n_column, char *line_buf, int line_buf_size)
{
	int used_len = 0;

	for(int i = 0; i < n_column; i++)
	{
		used_len += add_field_set(column_name[i], row_value[i], line_buf + used_len, line_buf_size - used_len);
		used_len += snprintf(line_buf + used_len, line_buf_size - used_len, ",");		
	}

	if(used_len > 0)
	{
		used_len--;
		line_buf[used_len] = '\0';
	}

	return used_len;
}



static int build_single_metric_line_buf(char *instance_name, struct metric *metric, char *line_buf, int line_buf_size)
{
	int used_len = 0;
	long long value = 0;

	value = read_single_metric_value(metric);

	if(value == 0)
	{
		return 0;
	}

	used_len += add_measurement(metric->field_name, line_buf, line_buf_size);

	used_len += add_default_tag_set(instance_name, NULL, line_buf + used_len, line_buf_size - used_len);

	used_len += add_user_tag_set(metric, line_buf + used_len, line_buf_size - used_len);

	used_len += snprintf(line_buf + used_len, line_buf_size - used_len, " ");

	used_len += add_field_set(metric->field_name, value, line_buf + used_len, line_buf_size - used_len);

	used_len += snprintf(line_buf + used_len, line_buf_size - used_len, "\n");

	return used_len;
}


static int read_table_row_value(struct metric **row_metric, int n_column, long long *out_row_value)
{
	int i = 0;
	struct metric *metric = NULL;

	if(row_metric == NULL || n_column < 1)
	{
		return -1;
	}

	for(i = 0; i < n_column; i++)
	{
		metric = row_metric[i];
		out_row_value[i] = read_single_metric_value(metric);
	}
	return 0;

}

static int build_table_row_line_buf(char *instance_name, struct table_metric *table, struct metric **row_metric, char *line_buf, int line_buf_size)
{
	int used_len = 0;
	struct metric *metric = NULL;

	if(table->column_cnt <= 0)
	{
		return 0;
	}
	long long row_value[table->column_cnt];

	if(-1 == read_table_row_value(row_metric, table->column_cnt, row_value))
	{
		return 0;
	}

	if(1 != is_send_table_row(row_value, table->column_cnt))
	{
		return 0;
	}

	metric = row_metric[0];

	used_len += add_measurement(metric->field_name, line_buf, line_buf_size);

	used_len += add_default_tag_set(instance_name, table->name, line_buf + used_len, line_buf_size - used_len);

	used_len += add_user_tag_set(metric, line_buf + used_len, line_buf_size - used_len);

	used_len += snprintf(line_buf + used_len, line_buf_size - used_len, " ");

	used_len += add_table_row_field_set(table->column_name, row_value, table->column_cnt, line_buf + used_len, line_buf_size - used_len);

	used_len += snprintf(line_buf + used_len, line_buf_size - used_len, "\n");

	return used_len;
}


static void output_line_protocol_single_metric(struct fieldstat_instance *instance, int n_cur_metric)
{
	int i = 0;
	int used_len = 0;

	struct metric *metric = NULL;
	char line_buf[UDP_PAYLOAD_SIZE];

	for(i = 0; i < n_cur_metric; i++)
	{
		metric = get_metric(instance, i);

		if(metric == NULL || metric->table != NULL)
		{
			continue;
		}
		memset(line_buf, 0, sizeof(line_buf));

		used_len = build_single_metric_line_buf(instance->name, metric, line_buf, sizeof(line_buf));

		send_line_buf(&instance->line_protocol_output, line_buf, used_len);
	}
	return;
}

static void output_line_protocol_table_row(struct fieldstat_instance *instance, int n_cur_table, int n_cur_table_row[])
{
	int i = 0, j = 0, k = 0;
	int used_len = 0;

	char line_buf[UDP_PAYLOAD_SIZE];
	struct table_metric *table = NULL;
	struct table_line *row = NULL;
	struct metric *row_metrics[TABLE_COLUMN_SIZE];

	for(i = 0; i < n_cur_table; i++)
	{
		table = instance->table_metrics[i];

		for(j = 0; j < n_cur_table_row[i]; j++)
		{
			row = read_table_line(table, j);
			if(row == NULL)
			{
				continue;
			}
			memset(line_buf, 0, sizeof(line_buf));

			for(k = 0; k < table->column_cnt; k++)
			{
				row_metrics[k] = get_metric(instance, row->metric_id_belong_to_line[k]);
			}

			used_len = build_table_row_line_buf(instance->name, table, row_metrics, line_buf, sizeof(line_buf));

			send_line_buf(&instance->line_protocol_output, line_buf, used_len);
		}
	}
	
	return;
}



int line_protocol_output(struct fieldstat_instance *instance)
{

	//print current time instance start
	int tables_row_cnt[TABLE_MAX_NUM];
	int n_cur_table = instance->table_num;
	get_current_table_line_cnt(instance, n_cur_table, tables_row_cnt);
	int n_cur_metric = instance->metric_cnt;
	//print current time instance end

	output_line_protocol_single_metric(instance, n_cur_metric);

	output_line_protocol_table_row(instance, n_cur_table, tables_row_cnt);

	flush_send_buf(&instance->line_protocol_output);
	return 0;
}

int line_protocol_dynamic_metric_output(struct fieldstat_dynamic_instance *instance)
{
	struct dynamic_metric **head = NULL;
	struct dynamic_metric *dyn_metric, *tmp_dyn_metric;
	struct metric **metrics = NULL;
	struct metric *metric = NULL;

	char line_buf[UDP_PAYLOAD_SIZE];
	int used_len = 0;

	for(int i = 0; i < instance->n_thread; i++)
	{
		head = &instance->n_thread_dynamic_metric[i];
		HASH_ITER(hh, *head, dyn_metric, tmp_dyn_metric)
		{
			metrics = dyn_metric->metrics;
			metric = metrics[0];
			used_len = 0;
			memset(line_buf, 0, sizeof(line_buf));
			if(metric->table)
			{
				used_len = build_table_row_line_buf(instance->name, metric->table, metrics, line_buf, sizeof(line_buf));
			}
			else
			{
				used_len = build_single_metric_line_buf(instance->name, metric, line_buf, sizeof(line_buf));
			}
			send_line_buf(&instance->line_protocol_output, line_buf, used_len);
		}

	}
	flush_send_buf(&instance->line_protocol_output);
	return 0;
}
