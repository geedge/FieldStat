#include "fieldstat_internal.h"

/*
static void flush_line_protocol_metric_send_buff(int send_socket, unsigned int server_ip, unsigned short server_port, const char *send_buff, int *send_buff_offset)
{
	int offset = *send_buff_offset;
	if(offset == 0)
	{
		return;
	}	

	if(server_ip > 0 && server_port > 0)
	{
		send_udp(send_socket, server_ip, server_port, send_buff, offset);
	}

	*send_buff_offset = 0;
	memset(send_buff, 0, UDP_PAYLOAD_SIZE);
}
*/

static void flush_line_protocol_metric_send_buff(struct fieldstat_instance *instance)
{
	if(instance->line_protocol_send_buff_offset == 0)
	{
		return;
	}

	if(instance->line_protocol_server_ip > 0 && instance->line_protocol_server_port > 0)
	{
		send_udp(instance->line_protocol_socket, instance->line_protocol_server_ip,
				(unsigned short)instance->line_protocol_server_port, 
				instance->line_protocol_send_buff, 
				instance->line_protocol_send_buff_offset
			);
	}
	instance->line_protocol_send_buff_offset = 0;
	memset(instance->line_protocol_send_buff, 0, sizeof(instance->line_protocol_send_buff));
	return;
}

static void append_line_protocol_metric_to_send_buff(struct fieldstat_instance *instance, const char* measurement, char *tag_set, char *field_set)
{
	if(field_set==NULL)
	{
		return;
	}
	if(UDP_PAYLOAD_SIZE - (unsigned int)instance->line_protocol_send_buff_offset < strlen(measurement) + strlen(field_set) + strlen(tag_set) + 2)
	{
		flush_line_protocol_metric_send_buff(instance);
	}
	printf("Line_protocol metric: %s%s %s\n",measurement,tag_set,field_set);
	instance->line_protocol_send_buff_offset += snprintf(instance->line_protocol_send_buff + instance->line_protocol_send_buff_offset,
														sizeof(instance->line_protocol_send_buff) - instance->line_protocol_send_buff_offset,
														"%s%s %s\n",
														measurement, tag_set, field_set
													);
	return;
}

static int output_line_protocol_tag_set_buf(char *tag_key[], char *tag_value[], int n_tag, char *tag_set_buff, unsigned int size)
{
	int i = 0;
	char *tag_pos = tag_set_buff;
	for(i = 0; i < n_tag; i++)
	{
		tag_pos += snprintf(tag_pos, 
							size - (tag_pos - tag_set_buff), 
							",%s=%s", 
							tag_key[i],
							tag_value[i]
						);
	}
	return tag_pos - tag_set_buff;
}


static void output_line_protocol_table(struct fieldstat_instance *instance,int tables_line_cnt[], int current_table_cnt)
{
	int i = 0, j = 0, k = 0;
	struct metric *metric = NULL;
	long long value = 0;
	//double ratio = 0.0;
	char field_set_buff[UDP_PAYLOAD_SIZE];
	char tag_set_buff[UDP_PAYLOAD_SIZE];

	char *tag_pos = tag_set_buff;
	char *field_pos = field_set_buff;

	struct table_metric *table = NULL;
	struct table_line *line = NULL;
	int line_value_is_not_zero = 0;

	for(i = 0; i < current_table_cnt; i++)
	{
		table = instance->table_metrics[i];
		for(j = 0; j < tables_line_cnt[i]; j++)
		{
			line = read_table_line(table, j);
			if(line == NULL)
			{
				continue;
			}
			line_value_is_not_zero = 0;
			tag_pos = tag_set_buff;
			field_pos = field_set_buff;
			memset(field_set_buff, 0, sizeof(field_set_buff));
			memset(tag_set_buff,  0, sizeof(tag_set_buff));
			line_value_is_not_zero = 0;
			for(k = 0; k < table->column_cnt; k ++)
			{
				metric = get_metric(instance, line->metric_id_belong_to_line[k]);

				value = metric->field_type == FIELD_TYPE_GAUGE ? 
					get_metric_unit_val(metric, FS_CALC_CURRENT, 1):
					get_metric_unit_val(metric, FS_CALC_SPEED, 1);
				if(value != 0)
				{
					line_value_is_not_zero = 1;
				}
					field_pos += snprintf(field_pos, 
											sizeof(field_set_buff) - (field_pos - field_set_buff), 
											"%s=%lld,", 
											metric->table->column_name[metric->table_column_id], 
											value
										);

			}
			if(line_value_is_not_zero == 0)
			{
				continue;
			}
			tag_pos += snprintf(tag_pos, 
						sizeof(tag_set_buff) - (tag_pos - tag_set_buff), 
						",app_name=%s,table_name=%s",
						instance->name,
						table->name
					);
			
			tag_pos += output_line_protocol_tag_set_buf(line->tag_key, line->tag_value, line->n_tag, tag_pos, 
														sizeof(tag_set_buff) - (tag_pos - tag_set_buff));


			// measurement,tag_set field_set
			append_line_protocol_metric_to_send_buff(instance, metric->field_name, tag_set_buff, field_set_buff);
		}

	}
}

int line_protocol_output(struct fieldstat_instance *instance)
{
	struct metric *metric = NULL;
	long long value=0;
	int i=0;
	char field_set_buff[UDP_PAYLOAD_SIZE];
	char tag_set_buff[UDP_PAYLOAD_SIZE];

	memset(field_set_buff, 0, sizeof(field_set_buff));
	memset(tag_set_buff,  0, sizeof(tag_set_buff));
	char *tag_pos = tag_set_buff;
	//print current time instance start
	int tables_line_cnt[TABLE_MAX_NUM];
	int current_table_cnt = instance->table_num;
	get_current_table_line_cnt(instance, current_table_cnt, tables_line_cnt);
	int current_metric_cnt = instance->metric_cnt;
	//print current time instance end

	for(i = 0; i < current_metric_cnt; i++)
	{
		metric = get_metric(instance, i);
		if(metric == NULL)
		{
			continue;
		}
		if(metric->is_ratio == 1)
		{
			continue;
		}

		if(metric->table)
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
					tag_pos += snprintf(tag_pos, 
										sizeof(tag_set_buff) - (tag_pos - tag_set_buff), 
										",app_name=%s", 
										instance->name
									);
					output_line_protocol_tag_set_buf(metric->tag_key, metric->tag_value, metric->n_tag, tag_pos, sizeof(tag_set_buff) - (tag_pos - tag_set_buff));
					append_line_protocol_metric_to_send_buff(instance, metric->field_name, tag_set_buff, field_set_buff);
					tag_pos = tag_set_buff;

				}
				break;
			case FIELD_TYPE_COUNTER:
				value = get_metric_unit_val(metric, FS_CALC_SPEED, 1);
				if(value != 0)
				{
					snprintf(field_set_buff, UDP_PAYLOAD_SIZE, "%s=%lld", metric->field_name, value);
					tag_pos += snprintf(tag_pos, 
											sizeof(tag_set_buff) - (tag_pos - tag_set_buff), 
											",app_name=%s", 
											instance->name
										);
					output_line_protocol_tag_set_buf(metric->tag_key, metric->tag_value, metric->n_tag, tag_pos, sizeof(tag_set_buff) - (tag_pos - tag_set_buff));
					append_line_protocol_metric_to_send_buff(instance, metric->field_name, tag_set_buff, field_set_buff);
					tag_pos = tag_set_buff;
				}
				break;
			default:
				break;

		}
	}

	output_line_protocol_table(instance,tables_line_cnt,current_table_cnt);
	flush_line_protocol_metric_send_buff(instance);

	return 0;
}

static void flush_line_protocol_dynamic_metric(struct fieldstat_dynamic_instance *instance)
{
	if(instance->line_protocol_send_buff_offset == 0)
	{
		return;
	}

	if(instance->line_protocol_server_ip > 0 && instance->line_protocol_server_port > 0)
	{
		send_udp(instance->line_protocol_socket, instance->line_protocol_server_ip,
				(unsigned short)instance->line_protocol_server_port, 
				instance->line_protocol_send_buff, 
				instance->line_protocol_send_buff_offset
			);
	}
	instance->line_protocol_send_buff_offset = 0;
	memset(instance->line_protocol_send_buff, 0, sizeof(instance->line_protocol_send_buff));	
	return;
}

static void append_line_protocol_dynamic_row(struct fieldstat_dynamic_instance *instance, const char* measurement, char *tag_set, char *field_set)
{
	if(field_set==NULL)
	{
		return;
	}
	if(UDP_PAYLOAD_SIZE - (unsigned int)instance->line_protocol_send_buff_offset < strlen(measurement) + strlen(field_set) + strlen(tag_set) + 2)
	{
		flush_line_protocol_dynamic_metric(instance);
	}
	//printf("Line_protocol metric: %s:%s %s\n",measurement,tag_set,field_set);
	instance->line_protocol_send_buff_offset += snprintf(instance->line_protocol_send_buff + instance->line_protocol_send_buff_offset,
														sizeof(instance->line_protocol_send_buff) - instance->line_protocol_send_buff_offset,
														"%s%s %s\n",
														measurement, tag_set, field_set
													);
	return;
}


int line_protocol_dynamic_metric_output(struct fieldstat_dynamic_instance *instance)
{
	struct dynamic_metric **head = NULL;
	struct dynamic_metric *dyn_metric, *tmp_dyn_metric;
	struct metric **metrics = NULL;
	struct metric *metric = NULL;
	struct table_metric *table = NULL;

	char tag_set_buff[UDP_PAYLOAD_SIZE];
	char field_set_buff[UDP_PAYLOAD_SIZE];

	int field_used_len = 0;
	int tag_used_len = 0;
	int i = 0, j = 0;
	long long value;
	int row_value_is_not_zero = 0;

	for(i = 0; i < instance->n_thread; i++)
	{
		head = &instance->n_thread_dynamic_metric[i];
		HASH_ITER(hh, *head, dyn_metric, tmp_dyn_metric)
		{
			row_value_is_not_zero = 0;
			metrics = dyn_metric->metrics;
			metric = metrics[0];
			memset(tag_set_buff, 0, sizeof(tag_set_buff));
			memset(field_set_buff, 0,sizeof(field_set_buff));
			field_used_len = 0;
			tag_used_len = 0;

			if(metric->table)
			{
				table = metric->table;
				for(j = 0; j < table->column_cnt; j++)
				{
					metric = metrics[j];
					value = metric->field_type == FIELD_TYPE_GAUGE ? 
						get_metric_unit_val(metric, FS_CALC_CURRENT, 0):
						get_metric_unit_val(metric, FS_CALC_SPEED, 0);
					if(value != 0)
					{
						row_value_is_not_zero = 1;
					}
					field_used_len += snprintf(field_set_buff + field_used_len, 
											sizeof(field_set_buff) - field_used_len, 
											"%s=%lld,", 
											metric->table->column_name[metric->table_column_id], 
											value
										);
					
				}
				if(row_value_is_not_zero != 1)
				{
					continue;
				}
				metric = metrics[0];
				field_used_len--;
				field_set_buff[field_used_len] = '\0';
				tag_used_len += snprintf(tag_set_buff + tag_used_len, 
										sizeof(tag_set_buff) - tag_used_len, 
										",app_name=%s,table_name=%s",
										instance->name,
										table->name
									);
				
			}
			else
			{
				value = metric->field_type == FIELD_TYPE_GAUGE ? 
						get_metric_unit_val(metric, FS_CALC_CURRENT, 0):
						get_metric_unit_val(metric, FS_CALC_SPEED, 0);
				if(value == 0)
				{
					continue;
				}
				snprintf(field_set_buff, sizeof(field_set_buff), "%s=%lld", metric->field_name, value);
				tag_used_len += snprintf(tag_set_buff + tag_used_len, 
										sizeof(tag_set_buff) - tag_used_len, 
										",app_name=%s", 
										instance->name
									);

			}
			tag_used_len += output_line_protocol_tag_set_buf(metric->tag_key, metric->tag_value, metric->n_tag, 
							tag_set_buff + tag_used_len, sizeof(tag_set_buff) - tag_used_len);
			append_line_protocol_dynamic_row(instance, metric->field_name, tag_set_buff, field_set_buff);
		}    

	}
	flush_line_protocol_dynamic_metric(instance);
	return 0;
}
