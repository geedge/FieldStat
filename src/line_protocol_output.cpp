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
	if(line_protocol_output == NULL || line_buf == NULL || line_buf_len == 0)
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

static long long read_single_metric_value(struct metric *metric, int output_type)
{
	long long value = 0;
	int is_refer = 1;
	(output_type >= 4 && output_type < 8)
		?is_refer = 0
		:is_refer = 1;
	switch(metric->field_type)
	{
		case FIELD_TYPE_GAUGE:
			value = get_metric_unit_val(metric, FS_CALC_CURRENT, is_refer);
			break;
		case FIELD_TYPE_COUNTER:
			value = get_metric_unit_val(metric, FS_CALC_SPEED, is_refer);
			break;
		default:
			assert(0);
			break;
	}
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

static int add_hdr_field_set(struct metric *metric, struct hdr_histogram *h_out, 
							 char *line_buf, int line_buf_size)
{
	
	int used_len = 0;
	long long value = 0;
	double * bins = metric->histogram.bins;
	int bins_num  = metric->histogram.bins_num;

	for(int i = 0; i < bins_num; i++)
	{
		switch(metric->field_type)
		{
			case FIELD_TYPE_SUMMARY:
				value = (long long)hdr_value_at_percentile(h_out, bins[i]*100);
				used_len += snprintf(line_buf + used_len, line_buf_size - used_len, 
									 "P%d=%lld,", (int)(bins[i]*100), value);
				break;
			case FILED_TYPE_HISTOGRAM:
				value = hdr_count_le_value(h_out, (long long)bins[i]);
				used_len += snprintf(line_buf + used_len, line_buf_size - used_len, 
									 "le%d=%lld,", (int)bins[i], value);
				break;
			default:
				assert(0);
				return 0;
		}
	}

	used_len += snprintf(line_buf + used_len, line_buf_size - used_len, "max=%lld,", 
						 h_out->total_count==0?0:(long long)hdr_max(h_out));
	used_len += snprintf(line_buf + used_len, line_buf_size - used_len, "min=%lld,", 
						 h_out->total_count==0?0:(long long)hdr_min(h_out));
	used_len += snprintf(line_buf + used_len, line_buf_size - used_len, "avg=%0.2f,", 
						 h_out->total_count==0?0:hdr_mean(h_out));
	used_len += snprintf(line_buf + used_len, line_buf_size - used_len, "stddev=%0.2f,", 
						 h_out->total_count==0?0:hdr_stddev(h_out));
	used_len += snprintf(line_buf + used_len, line_buf_size - used_len, "cnt=%lld", 
						 (long long)h_out->total_count);

	return used_len;
}


static int add_table_row_field_set(char *column_name[], long long *row_value, int n_column, char *line_buf, int line_buf_size)
{
	int used_len = 0;
	char unescape[256] = {0};

	for(int i = 0; i < n_column; i++)
	{
		escaping_special_chars_cpoy(unescape, column_name[i], sizeof(unescape));
		used_len += add_field_set(unescape, row_value[i], line_buf + used_len, line_buf_size - used_len);
		used_len += snprintf(line_buf + used_len, line_buf_size - used_len, ",");		
	}

	if(used_len > 0)
	{
		used_len--;
		line_buf[used_len] = '\0';
	}

	return used_len;
}



static int build_single_metric_line_buf(char *instance_name, int output_type, struct metric *metric, char *line_buf, int line_buf_size)
{
	int used_len = 0;
	long long value = 0;
	char unescape[256] = {0};

	value = read_single_metric_value(metric, output_type);

	if(value == 0)
	{
		return 0;
	}

	escaping_special_chars_cpoy(unescape, metric->field_name, sizeof(unescape));

	used_len += add_measurement(unescape, line_buf, line_buf_size);

	used_len += add_default_tag_set(instance_name, NULL, line_buf + used_len, line_buf_size - used_len);

	used_len += add_user_tag_set(metric, line_buf + used_len, line_buf_size - used_len);

	used_len += snprintf(line_buf + used_len, line_buf_size - used_len, " ");

	used_len += add_field_set(unescape, value, line_buf + used_len, line_buf_size - used_len);

	used_len += snprintf(line_buf + used_len, line_buf_size - used_len, "\n");

	return used_len;
}

static struct hdr_histogram *read_hdr_metric_value(struct metric *metric, int output_type)
{
	struct histogram_t *h=&(metric->histogram);
	struct hdr_histogram *h_out = NULL, *h_tmp = NULL;

	if(output_type >= 4 && output_type < 8)
	{
		hdr_init(h->lowest_trackable_value,  h->highest_trackable_value, 
				 h->significant_figures, &(h_tmp));

		if(h->previous_changed != NULL)	
		{
			hdr_close(h->previous_changed);
		}
		h->previous_changed = atomic_read(&(h->changing));
		h_tmp = atomic_set(&(h->changing), h_tmp);
		hdr_add(h->accumulated, h->previous_changed);
	}
	h_out = (metric->output_window == 0) ? h->accumulated : h->previous_changed;

	return h_out;
}

static int build_hdr_metric_line_buf(char *instance_name, int output_type, struct metric *metric,
									 char *line_buf, int line_buf_size)
{
	int used_len = 0;
	struct hdr_histogram *h_out = NULL;
	char unescape[256] = {0};

	h_out = read_hdr_metric_value(metric, output_type);

	if(h_out == NULL)
	{
		return 0;
	}

	escaping_special_chars_cpoy(unescape, metric->field_name, sizeof(unescape));

	used_len += add_measurement(unescape, line_buf, line_buf_size);

	used_len += add_default_tag_set(instance_name, NULL, line_buf + used_len, 
								    line_buf_size - used_len);

	used_len += add_user_tag_set(metric, line_buf + used_len, 
								 line_buf_size - used_len);

	used_len += snprintf(line_buf + used_len, line_buf_size - used_len, " ");

	used_len += add_hdr_field_set(metric, h_out, line_buf + used_len, 
								  line_buf_size - used_len);

	used_len += snprintf(line_buf + used_len, line_buf_size - used_len, "\n");

	return used_len;
}


static int read_table_row(struct metric **row_metric, char *column_name[], int output_type, int n_column, char *out_column_name[], long long *out_row_value)
{
	int i = 0;
	int n_send = 0;
	struct metric *metric = NULL;

	if(row_metric == NULL || n_column < 1)
	{
		return -1;
	}

	for(i = 0; i < n_column; i++)
	{
		metric = row_metric[i];
		if(metric->is_ratio == 1 || metric->is_invisible == 1)
		{
			continue;
		}
		out_row_value[n_send] = read_single_metric_value(metric, output_type);
		out_column_name[n_send] = column_name[i];
		n_send++;
	}
	return n_send;

}

static int build_table_row_line_buf(char *instance_name, int output_type, struct table_metric *table, struct metric **row_metric, char *line_buf, int line_buf_size)
{
	int used_len = 0;
	struct metric *metric = NULL;
	int n_send = 0;
	char unescape[256] = {0};

	if(table->column_cnt <= 0)
	{
		return 0;
	}
	long long row_value[table->column_cnt];
	char *column_name[table->column_cnt];
	n_send = read_table_row(row_metric, table->column_name, output_type, table->column_cnt, column_name, row_value);

	if(n_send < 1)
	{
		return 0;
	}

	if(1 != is_send_table_row(row_value, n_send))
	{
		return 0;
	}

	metric = row_metric[0];

	escaping_special_chars_cpoy(unescape, metric->field_name, sizeof(unescape));
	used_len += add_measurement(unescape, line_buf, line_buf_size);

	used_len += add_default_tag_set(instance_name, table->name, line_buf + used_len, line_buf_size - used_len);

	used_len += add_user_tag_set(metric, line_buf + used_len, line_buf_size - used_len);

	used_len += snprintf(line_buf + used_len, line_buf_size - used_len, " ");

	used_len += add_table_row_field_set(column_name, row_value, n_send, line_buf + used_len, line_buf_size - used_len);

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
		if(metric->is_ratio == 1)
		{
			continue;
		}
		memset(line_buf, 0, sizeof(line_buf));

		switch(metric->field_type)
		{
			case FIELD_TYPE_GAUGE:
			case FIELD_TYPE_COUNTER:
				used_len = build_single_metric_line_buf(instance->name, instance->output_type, 
														metric, line_buf, sizeof(line_buf));
				break;
			case FIELD_TYPE_SUMMARY:
			case FILED_TYPE_HISTOGRAM:
				used_len = build_hdr_metric_line_buf(instance->name, instance->output_type, 
													 metric, line_buf, sizeof(line_buf));
				break;
			default:
				assert(0);
				break;
		}

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

			used_len = build_table_row_line_buf(instance->name, instance->output_type, table, row_metrics, line_buf, sizeof(line_buf));

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
				used_len = build_table_row_line_buf(instance->name, instance->output_type, metric->table, metrics, line_buf, sizeof(line_buf));
			}
			else
			{
				used_len = build_single_metric_line_buf(instance->name, instance->output_type, metric, line_buf, sizeof(line_buf));
			}
			send_line_buf(&instance->line_protocol_output, line_buf, used_len);
		}

	}
	flush_send_buf(&instance->line_protocol_output);
	return 0;
}
