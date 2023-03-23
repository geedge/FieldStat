#include "fieldstat_internal.h"

const char* draw_line="________________________________________________________________________________________________________________________________________________";
const char* draw_boundary="============================================================";

static int append_tags_to_print_buf(char *tag_key[], char *tag_value[], size_t n_tag, char *print_buf, unsigned int size)
{
	int i = 0;
	int used_len = 0;

	if(n_tag <= 0)
	{
		return 0;
	}

	used_len += snprintf(print_buf + used_len, size - used_len, "{");

	for(i = 0; i < (int)n_tag; i++)
	{
		used_len += snprintf(print_buf + used_len, size - used_len, "%s=\"%s\",", tag_key[i], tag_value[i]);
	}

	if(used_len > 0)
	{
		used_len--;
	}

	used_len += snprintf(print_buf + used_len, size - used_len, "}\t");

	return used_len;
}

static int output_file_format_default_type_gauge(struct fieldstat_instance *instance, int current_metric_cnt,long long interval_ms, char *print_buf, unsigned int size)
{
	int i = 0, j = 0;
	int used_len = 0;
	long long value = 0;
	struct metric *metric = NULL;
	char tags_buf[1024] = {0};

	for(i = 0; i < current_metric_cnt; i++)
	{
		metric = get_metric(instance, i);
		if(metric == NULL)
		{
			continue;
		}
		if(metric->field_type != FIELD_TYPE_GAUGE)
		{
			continue;
		}
		if(metric->table)
		{
			continue;
		}
		if(metric->is_invisible == 1)
		{
			value = get_metric_unit_val(metric, FS_CALC_SPEED, 0);
			continue;
		}

		value = get_metric_unit_val(metric, FS_CALC_CURRENT, 0);
		memset(tags_buf, 0, sizeof(tags_buf));
		append_tags_to_print_buf(metric->tag_key, metric->tag_value, metric->n_tag, tags_buf, sizeof(tags_buf));
		used_len += snprintf(print_buf + used_len, 
								size - used_len, 
								"%s %s: %-10lld\t", 
								metric->field_name,
								tags_buf, 
								value
							);
		j++;
		if(j == STATUS_PER_LINE)
		{
			used_len += snprintf(print_buf + used_len, size - used_len, "\n");
			j=0;
		}
	}

	if(used_len > 0)
	{
		if(print_buf[used_len - 1]  == '\n')
		{
			used_len--;
		}
		used_len += snprintf(print_buf + used_len, size - used_len, "\n%s\n", draw_line);
	}
	return used_len;
}


static int output_file_format_default_type_counter(struct fieldstat_instance *instance, int current_metric_cnt, long long interval_ms,char*print_buf, unsigned int size)
{
	int i = 0, j = 0;
	int used_len = 0;
	int metric_cnt = 0;
	long long value = 0;
	struct metric *metric = NULL;
	int metric_id[INIT_STAT_FIELD_NUM] = {0};
	char tags_buf[1024];


	for(i = 0;i < current_metric_cnt; i++)
	{
		metric = get_metric(instance, i);
		if(metric == NULL)
		{
			continue;
		}
		if(metric->field_type != FIELD_TYPE_COUNTER)
		{
			continue;
		}
		if(metric->table)
		{
			continue;
		}
		if(metric->is_invisible == 1)
		{
			get_metric_unit_val(metric, FS_CALC_CURRENT, 0);
			continue;
		}
		metric_id[metric_cnt] = i;
		metric_cnt++;
	}

	for(i = 0; i < metric_cnt; i++)
	{
		used_len += snprintf(print_buf + used_len, size - used_len, "\t");

		for(j = 0; j < FIELD_PER_LINE && i+j < metric_cnt; j++)
		{
			metric = get_metric(instance, metric_id[i+j]);
			memset(tags_buf, 0, sizeof(tags_buf));
	
			append_tags_to_print_buf(metric->tag_key, metric->tag_value, metric->n_tag, tags_buf, sizeof(tags_buf));

			used_len += snprintf(print_buf + used_len, 
									size - used_len, 
									"%10s %s\t", 
									metric->field_name, 
									tags_buf
								);
		}
		used_len += snprintf(print_buf + used_len, size - used_len, "\nsum\t");

		for(j=0; j < FIELD_PER_LINE && i+j < metric_cnt; j++)
		{
			metric = get_metric(instance, metric_id[i+j]);
			value  = get_metric_unit_val(metric,FS_CALC_CURRENT, 1);

			used_len += snprintf(print_buf + used_len, size - used_len, "%10lld\t", value);
		}

		used_len += snprintf(print_buf + used_len, size - used_len, "\nspeed/s\t");

		for(j = 0; j<FIELD_PER_LINE&&i+j<metric_cnt; j++)
		{
			metric = get_metric(instance, metric_id[i+j]);
			value  = get_metric_unit_val(metric,FS_CALC_SPEED, 0);

			used_len += snprintf(print_buf + used_len,
									size - used_len,
									"%10lld\t",
									value*1000/interval_ms
								);

		}
		i += (j-1);

		used_len += snprintf(print_buf + used_len, size - used_len, "\n");
	}

	if(used_len > 0)
	{
		if(print_buf[used_len - 1] == '\n')
		{
			used_len--;
		}

		used_len += snprintf(print_buf + used_len, size - used_len, "\n%s\n", draw_line);

	}
	return used_len;
}

static int output_file_format_default_table(struct fieldstat_instance *instance,int tables_line_cnt[], int current_table_cnt,long long interval_ms,char*print_buf, unsigned int size)
{
	int i = 0, j = 0, k = 0;
	struct table_metric *table =  NULL;
	struct table_line  *line = NULL;
	int metric_id = 0;
	struct metric *metric = NULL;
	long long value = 0;
	int used_len = 0;

	for(i = 0; i < current_table_cnt; i++)   //per table
	{
		table = instance->table_metrics[i];
		used_len += snprintf(print_buf + used_len, size - used_len, "%-20s\t\t", table->name);
		for(j = 0; j < table->column_cnt; j ++)
		{
			//print table column
			used_len += snprintf(print_buf + used_len, size - used_len, "\t%10s", table->column_name[j]);
		}
		for(j = 0; j < tables_line_cnt[i]; j++)  //per line
		{
			line = read_table_line(table,j);
			if(line == NULL)
			{
				continue;
			}
			//print table line name + tag
			used_len += snprintf(print_buf + used_len, size - used_len, "\n%s ", line->name);

			used_len += append_tags_to_print_buf(line->tag_key, line->tag_value, line->n_tag, 
												print_buf + used_len, size - used_len);
			
			for(k = 0; k < table->column_cnt; k++) //per metric
			{
				//print table metric value
				metric_id = line->metric_id_belong_to_line[k];
				metric = get_metric(instance,metric_id);
				value = get_metric_unit_val(metric, FS_CALC_CURRENT, 0);

				used_len += snprintf(print_buf + used_len, size - used_len, "%10lld\t", value);

			}

		} 
		if(used_len > 0)
		{
			if(print_buf[used_len - 1] == '\n')
			{
				used_len--;
			}
			used_len += snprintf(print_buf + used_len, size - used_len, "\n%s\n", draw_line);
		}
	}
	return used_len;
}

static int output_file_print_hdr_head(struct metric *metric, char *print_buf, size_t size)
{
	int used_len = 0;
	char bin_format[256];
	char str_format[256];
	char buff[32];
	const char* extra[]={"MAX", "MIN", "AVG", "STDDEV", "CNT"};
	int i = 0;
	double * bins = metric->histogram.bins;
	int bins_num  = metric->histogram.bins_num;

	if(metric->field_type == FIELD_TYPE_SUMMARY)
	{
		snprintf(bin_format, sizeof(bin_format), "%%%d.2lf%%%%", HISTOGRAM_WIDTH-1);
	}
	if(metric->field_type == FILED_TYPE_HISTOGRAM)
	{
		snprintf(bin_format, sizeof(bin_format), "le=%%0.0f");
	}
	snprintf(str_format, sizeof(str_format), "%%%ds", HISTOGRAM_WIDTH);
	if(metric->field_type == FIELD_TYPE_SUMMARY)
	{
		used_len += snprintf(print_buf + used_len, size - used_len, "%-8s\t", "summary");
	}
	if(metric->field_type == FILED_TYPE_HISTOGRAM)
	{
		used_len += snprintf(print_buf + used_len, size - used_len, "%-8s\t", "histogram");
	}
	for(i = 0;i < bins_num; i++)
	{
		if(metric->field_type == FIELD_TYPE_SUMMARY)
		{
			snprintf(buff, sizeof(buff), bin_format, bins[i]*100);
		}
		if(metric->field_type == FILED_TYPE_HISTOGRAM)
		{
			snprintf(buff, sizeof(buff), bin_format, bins[i]);
		}
		used_len += snprintf(print_buf + used_len, size - used_len, str_format, buff);
	}
	for( i = 0; (unsigned int) i < sizeof(extra)/sizeof(extra[0]); i++)
	{
		used_len += snprintf(print_buf + used_len, size - used_len, str_format, extra[i]);
	}
	used_len += snprintf(print_buf + used_len, size - used_len, "\n");

	return used_len;
}

static int output_file_print_hdr_unit(struct metric *metric, char *print_buf, size_t size)
{
	int used_len = 0;
	double * bins = metric->histogram.bins;
	int bins_num  = metric->histogram.bins_num;
	long long value = 0;
	int i = 0;
	struct histogram_t *h=&(metric->histogram);
	struct hdr_histogram *h_out=NULL, *h_tmp=NULL;
	char int_format[STR_LEN_256], double_format[STR_LEN_256];

	snprintf(int_format, sizeof(int_format), "%%%dlld",HISTOGRAM_WIDTH);
	snprintf(double_format, sizeof(double_format), "%%%d.2lf",HISTOGRAM_WIDTH);

	hdr_init(h->lowest_trackable_value,  h->highest_trackable_value, h->significant_figures, &(h_tmp));

	if(h->previous_changed != NULL)	
	{
		hdr_close(h->previous_changed);
	}

	h->previous_changed=atomic_read(&(h->changing));
	h_tmp=atomic_set(&(h->changing), h_tmp);// left h_tmp is used to avoid warining [-Wunused-value]

	hdr_add(h->accumulated, h->previous_changed);
	h_out = h->accumulated; //TODO

	used_len += snprintf(print_buf + used_len, size - used_len, "%-10s\t", metric->field_name);

	for(i = 0; i < bins_num; i++)
	{
		if(metric->field_type == FIELD_TYPE_SUMMARY)
		{
			value = (long long)hdr_value_at_percentile(h_out, bins[i]*100);
		}
		if(metric->field_type == FILED_TYPE_HISTOGRAM)
		{
			value = hdr_count_le_value(h_out, (long long)bins[i]);
		}
		used_len += snprintf(print_buf + used_len, size - used_len, int_format, value);
	}

	used_len += snprintf(print_buf + used_len, size - used_len, int_format, h_out->total_count==0?0:(long long)hdr_max(h_out));
	used_len += snprintf(print_buf + used_len, size - used_len, int_format, h_out->total_count==0?0:(long long)hdr_min(h_out));
	used_len += snprintf(print_buf + used_len, size - used_len, double_format, h_out->total_count==0?0:hdr_mean(h_out));
	used_len += snprintf(print_buf + used_len, size - used_len, double_format, h_out->total_count==0?0:hdr_stddev(h_out));
	used_len += snprintf(print_buf + used_len, size - used_len, int_format, (long long)h_out->total_count);

	used_len += snprintf(print_buf + used_len, size - used_len,"\n");

	h_tmp = NULL;

	return used_len;
}

static int output_file_format_default_type_histogram_and_summary(struct fieldstat_instance *instance, int current_metric_cnt,long long interval_ms, char*print_buf, size_t size)
{
	int i = 0, j = 0;
	int metric_num = 0;
	int used_len = 0;
	int *printed_metric_id = NULL;
	struct metric *metric = NULL;
	struct metric **metric_array = NULL;


	if(current_metric_cnt < 1)
	{
		return 0;
	}

	if(instance->histogram_cnt == 0 
		&& instance->summary_cnt == 0)
	{
		return 0;
	}

	metric_array = (struct metric **)calloc(current_metric_cnt, sizeof(struct metric *));
	printed_metric_id = (int *)calloc(current_metric_cnt, sizeof(int));

	for(i = 0; i < current_metric_cnt; i ++)
	{
		metric = get_metric(instance, i);
		if(metric == NULL)
		{
			continue;
		}
		if(metric->field_type != FIELD_TYPE_SUMMARY 
			&& metric->field_type != FILED_TYPE_HISTOGRAM)
		{
			continue;
		}
		metric_array[metric_num++] = metric;	
	}

	for(i = 0; i < metric_num; i++)
	{
		if(printed_metric_id[i] == 1)
		{
			continue;
		}
		used_len += output_file_print_hdr_head(metric_array[i], print_buf + used_len, size - used_len);
		for(j = i; j < metric_num; j ++)
		{
			if(metric_array[j] == NULL)
			{
				continue;
			}
			if(metric_array[i]->histogram.bins_num != metric_array[j]->histogram.bins_num)
			{
				continue;
			}
			if(memcmp(metric_array[i]->histogram.bins, metric_array[j]->histogram.bins, metric_array[i]->histogram.bins_num))
			{
				continue;
			}
			used_len += output_file_print_hdr_unit(metric_array[j], print_buf + used_len, size - used_len);
			printed_metric_id[j] = 1;
		}
		if(used_len > 0)
		{
			if(print_buf[used_len - 1] == '\n')
			{
				used_len--;
			}
			used_len += snprintf(print_buf + used_len, size - used_len, "\n%s\n", draw_line);
		}
	}
	if(metric_array)
	{
		free(metric_array);
		metric_array = NULL;
	}
	if(printed_metric_id)
	{
		free(printed_metric_id);
		printed_metric_id = NULL;
	}

	return used_len;	
}


int fieldstat_output_file(struct fieldstat_instance *instance,long long interval_ms)
{
	int used_len = 0;
	int current_table_cnt = 0;
	int current_metric_cnt = 0;

	time_t current = 0;
	char *print_buf = NULL;

	size_t print_buf_sz = 0;
	char ctime_buff[32]={0};
	int tables_line_cnt[TABLE_MAX_NUM];

	current_table_cnt = instance->table_num;
	get_current_table_line_cnt(instance, current_table_cnt, tables_line_cnt);
	current_metric_cnt = instance->metric_cnt;
	print_buf_sz = current_metric_cnt*1024;

	if(current_metric_cnt == 0)
	{
		return 0;
	}

	if(instance->local_output_fp == NULL)
	{
		instance->local_output_fp = fopen(instance->local_output_filename, "w");
		if(instance->local_output_fp == NULL)
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
		print_buf = (char*)calloc(print_buf_sz, sizeof(char));
		used_len += snprintf(print_buf + used_len, print_buf_sz - used_len, "%s%s", draw_boundary, ctime_buff);
		used_len --;//jump '\n' generate by ctime()
		used_len += snprintf(print_buf + used_len, print_buf_sz - used_len, "%s\n", draw_boundary);

		used_len += output_file_format_default_type_gauge(instance, current_metric_cnt, interval_ms, print_buf + used_len, print_buf_sz - used_len);
		used_len += output_file_format_default_type_counter(instance, current_metric_cnt, interval_ms, print_buf + used_len, print_buf_sz - used_len);
		used_len += output_file_format_default_table(instance, tables_line_cnt, current_table_cnt, interval_ms, print_buf + used_len, print_buf_sz - used_len);
		used_len += output_file_format_default_type_histogram_and_summary(instance, current_metric_cnt,interval_ms, print_buf + used_len, print_buf_sz - used_len);
	}

    if(!strcmp(instance->local_output_format, "json"))
    {
		//TODO from json output
    }

	fseek(instance->local_output_fp, 0, SEEK_SET);
	if(print_buf)
	{
		fwrite(print_buf, used_len, 1, instance->local_output_fp);
	}
	fflush(instance->local_output_fp);

	if(print_buf)
	{
		free(print_buf);
		print_buf = NULL;
	}
	return 0;
}


