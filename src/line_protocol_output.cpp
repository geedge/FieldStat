#include "fieldstat_internal.h"

static void flush_line_protocol_metric(struct fieldstat_instance *instance)
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

static void append_line_protocol_line(struct fieldstat_instance *instance, const char* measurement, char *tag_set, char *field_set)
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


static void output_line_protocol_table(struct fieldstat_instance *instance)
{
	int i = 0, j = 0, k = 0;
	metric_t *metric = NULL;
	long long value = 0;
	//double ratio = 0.0;
	char field_set_buff[UDP_PAYLOAD_SIZE];
	char tag_set_buff[UDP_PAYLOAD_SIZE];

	memset(field_set_buff, 0, sizeof(field_set_buff));
	memset(tag_set_buff,  0, sizeof(tag_set_buff));
	char *tag_pos = tag_set_buff;
	char *field_pos = field_set_buff;

	struct table_metric *table = NULL;
	struct table_line *line = NULL;

	for(i = 0; i < instance->table_num; i++)
	{
		table = instance->table_metrics[i];
		for(j = 0; j < table->line_cnt; j++)
		{
			line = read_table_line(table, j);
			tag_pos += snprintf(tag_pos, 
						sizeof(tag_set_buff) - (tag_pos - tag_set_buff), 
						",app_name=%s,table_name=%s",
						instance->name,
						table->name
					);
			
			tag_pos += output_line_protocol_tag_set_buf(line->tag_key, line->tag_value, line->n_tag, tag_pos, 
														sizeof(tag_set_buff) - (tag_pos - tag_set_buff));

			for(k = 0; k < table->column_cnt; k ++)
			{
				metric = get_metric(instance, line->metric_id_belong_to_line[k]);

				value = metric->field_type == FIELD_TYPE_GAUGE ? 
					get_metric_unit_val(metric, FS_CALC_CURRENT, 1):
					get_metric_unit_val(metric, FS_CALC_SPEED, 1);

					field_pos += snprintf(field_pos, 
											sizeof(field_set_buff) - (field_set_buff - field_set_buff), 
											"%s=%lld,", 
											metric->table_column_name, 
											value
										);

			}
			if(field_pos - field_set_buff > 0)
			{
				*(field_pos - 1) = '\0';
			}
			// measurement,tag_set field_set
			append_line_protocol_line(instance, metric->field_name, tag_set_buff, field_set_buff);
			tag_pos = tag_set_buff;
			field_pos = field_set_buff;

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
	char *tag_pos = tag_set_buff;

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
					tag_pos += snprintf(tag_pos, 
										sizeof(tag_set_buff) - (tag_pos - tag_set_buff), 
										",app_name=%s", 
										instance->name
									);
					output_line_protocol_tag_set_buf(metric->tag_key, metric->tag_value, metric->n_tag, tag_pos, sizeof(tag_set_buff) - (tag_pos - tag_set_buff));
					append_line_protocol_line(instance, metric->field_name, tag_set_buff, field_set_buff);
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
					append_line_protocol_line(instance, metric->field_name, tag_set_buff, field_set_buff);
					tag_pos = tag_set_buff;
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