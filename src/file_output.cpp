#include "fieldstat_internal.h"

const char* draw_line="________________________________________________________________________________________________________________________________________________";
const char* draw_boundary="============================================================";

static int print_buf_tag_append_position(char *tag_key[], char *tag_value[], size_t n_tag, char *print_buf_tags, unsigned int size)
{
	int i = 0;
	char *append_pos = print_buf_tags;

	if(n_tag <= 0)
	{
		return 0;
	}

	append_pos += snprintf(append_pos, 
							size - (append_pos - print_buf_tags),
							"{"
						);

	for(i = 0; i < (int)n_tag; i++)
	{
		append_pos += snprintf(append_pos,
								size - (append_pos - print_buf_tags),
								"%s=\"%s\",", 
								tag_key[i],
								tag_value[i]
							);
	}

	if(append_pos - print_buf_tags > 0)
	{
		append_pos--;
	}

	append_pos += snprintf(append_pos, 
							size - (append_pos - print_buf_tags),
							"}\t"
						);

	return append_pos - print_buf_tags;
}

static int output_file_format_default_type_gauge(struct fieldstat_instance *instance,int current_metric_cnt,long long interval_ms,char *print_buf, unsigned int size)
{
	int i = 0, j = 0;
	//display_manifest_t* p = NULL;
	struct metric *metric = NULL;
	long long value = 0;
	//double ratio = 0.0;
	//char* pos=print_buf;
	char *append_pos = print_buf;
	char print_buf_tags[1024];

	for(i = 0; i < current_metric_cnt; i++)
	{
		//metric = instance->metric[i];
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
		/*
		if(metric->is_ratio==1)
		{
			ratio=get_stat_ratio(_handle->display[p->numerator_id],  _handle->display[p->denominator_id], NULL,
								p->output_scaling, p->calc_type);
			pos+=snprintf(pos,size-(pos-print_buf),"%s: %10.2e\t",p->name,ratio);
		}
		*/
		value = get_metric_unit_val(metric, FS_CALC_CURRENT, 0);
		//value=value * metric->output_scaling * 1000 / interval_ms;
		memset(print_buf_tags,0, sizeof(print_buf_tags));
		print_buf_tag_append_position(metric->tag_key,metric->tag_value, metric->n_tag, print_buf_tags, sizeof(print_buf_tags));
		append_pos += snprintf(append_pos, 
								size - (append_pos - print_buf), 
								"%s %s: %-10lld\t", 
								metric->field_name, 
								print_buf_tags, 
								value
							);
		j++;
		if(j == STATUS_PER_LINE)
		{
			append_pos += snprintf(append_pos, size - (append_pos - print_buf),"\n");
			j=0;
		}
	}

	if(append_pos - print_buf > 0)
	{
		if(*(append_pos - 1) == '\n')
		{
			append_pos --;
		}
		append_pos += snprintf(append_pos, size - (append_pos-print_buf),"\n%s\n",draw_line);
	}
	return append_pos - print_buf;
}


static int output_file_format_default_type_counter(struct fieldstat_instance *instance, int current_metric_cnt, long long interval_ms,char*print_buf, unsigned int size)
{
	int i=0,j=0;
	//display_manifest_t* p=NULL;
	struct metric *metric = NULL;
	long long value = 0;
	//double ratio = 0.0;
	char* append_pos = print_buf;
	int metric_id[INIT_STAT_FIELD_NUM] = {0};
	int metric_cnt = 0;
	char print_buf_tags[1024];

	for(i = 0;i < current_metric_cnt; i++)
	{
		//p=_handle->display[i];
		//metric = instance->metric[i];
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
			get_metric_unit_val(metric,FS_CALC_CURRENT,0);
			continue;
		}
		metric_id[metric_cnt] = i;
		metric_cnt++;
	}

	for(i = 0; i < metric_cnt; i++)
	{
		append_pos += snprintf(append_pos, size - (append_pos - print_buf),"\t");

		for(j = 0; j < FIELD_PER_LINE && i+j < metric_cnt; j++)
		{
			//metric = instance->metric[metric_id[i+j]];
			metric = get_metric(instance, metric_id[i+j]);
			memset(print_buf_tags,0, sizeof(print_buf_tags));
			print_buf_tag_append_position(metric->tag_key,metric->tag_value, metric->n_tag, print_buf_tags, sizeof(print_buf_tags));
			append_pos += snprintf(append_pos, 
									size - (append_pos - print_buf), 
									"%10s %s\t", 
									metric->field_name, 
									print_buf_tags
								);
		}
		append_pos += snprintf(append_pos, size - (append_pos-print_buf), "\nsum\t");

		for(j=0; j < FIELD_PER_LINE && i+j < metric_cnt; j++)
		{
			//metric = instance->metric[metric_id[i+j]];
			metric = get_metric(instance, metric_id[i+j]);
			value  = get_metric_unit_val(metric,FS_CALC_CURRENT, 1);
			append_pos += snprintf(append_pos,
									sizeof(print_buf) - (append_pos - print_buf), 
									"%10lld\t", 
									value
								);
		}
		append_pos += snprintf(append_pos, 
								sizeof(print_buf) - (append_pos - print_buf), 
								"\nspeed/s\t"
							);

		for(j=0;j<FIELD_PER_LINE&&i+j<metric_cnt;j++)
		{
			//metric = instance->metric[metric_id[i+j]];
			metric = get_metric(instance, metric_id[i+j]);
			value  = get_metric_unit_val(metric,FS_CALC_SPEED, 0);
			append_pos += snprintf(append_pos, 
									size - (append_pos - print_buf), 
									"%10lld\t", 
									value*1000/interval_ms
								);
		}
		i += (j-1);
		append_pos += snprintf(append_pos, 
								size - (append_pos - print_buf), 
								"\n"
							);
	}

	if(append_pos - print_buf > 0)
	{
		if(*(append_pos - 1)=='\n')
		{
			append_pos--;
		}
		append_pos += snprintf(append_pos, 
								size - (append_pos - print_buf),
								"\n%s\n", 
								draw_line
							);
	}

	return append_pos - print_buf;
}

static int output_file_format_default_table(struct fieldstat_instance *instance,int tables_line_cnt[], int current_table_cnt,long long interval_ms,char*print_buf, unsigned int size)
{
	int i = 0, j = 0, k = 0;
	struct table_metric *table =  NULL;
	struct table_line  *line = NULL;
	int metric_id = 0;
	struct metric *metric = NULL;
	long long value = 0;
	char* append_pos = print_buf;

	for(i = 0; i < current_table_cnt; i++)   //per table
	{
		table = instance->table_metrics[i];
		append_pos += snprintf(append_pos, size - (append_pos - print_buf),"%-20s\t\t",table->name);
		for(j = 0; j < table->column_cnt; j ++)
		{
			//print table column
			append_pos += snprintf(append_pos,
									size - (append_pos - print_buf), 
									"\t%10s", 
									table->column_name[j]
								);
		}
		for(j = 0; j < tables_line_cnt[i]; j++)  //per line
		{
			line = read_table_line(table,j);
			if(line == NULL)
			{
				continue;
			}
			//print table line name + tag
			append_pos += snprintf(append_pos,
									size - (append_pos - print_buf), 
									"\n%s ", 
									line->name
								);

			append_pos += print_buf_tag_append_position(line->tag_key, line->tag_value, line->n_tag, 
														append_pos, size - (append_pos - print_buf));

			
			for(k = 0; k < table->column_cnt; k++) //per metric
			{
				//print table metric value
				metric_id = line->metric_id_belong_to_line[k];
				metric = get_metric(instance,metric_id);
				value = get_metric_unit_val(metric, FS_CALC_CURRENT, 0);
				append_pos += snprintf(append_pos, 
										size - (append_pos - print_buf), 
										"%10lld\t", 
										value
									);

			}

		} 

		if(append_pos - print_buf > 0)
		{
			if(*(append_pos - 1) == '\n')
			{
				append_pos--;
			}
			append_pos += snprintf(append_pos, 
									size - (append_pos - print_buf), 
									"\n%s\n", 
									draw_line
								);
		}

	}
	return append_pos - print_buf;
}

static int output_file_print_hdr_head(struct metric *metric, char *print_buf, size_t size)
{
	char * pos = print_buf;
	char bin_format[STR_LEN_256], str_format[STR_LEN_256];
	const char* extra[]={"MAX", "MIN", "AVG", "STDDEV", "CNT"};
	char buff[STR_LEN_32];
	int i=0;
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
		pos+=snprintf(pos,size-(pos-print_buf),"%-8s\t","summary");
	}
	if(metric->field_type == FILED_TYPE_HISTOGRAM)
	{
		pos+=snprintf(pos,size-(pos-print_buf),"%-8s\t","histogram");
	}
	for(i = 0;i < bins_num; i++)
	{
		if(metric->field_type == FIELD_TYPE_SUMMARY)
		{
			snprintf(buff,sizeof(buff),bin_format,bins[i]*100);
		}
		if(metric->field_type == FILED_TYPE_HISTOGRAM)
		{
			snprintf(buff,sizeof(buff),bin_format,bins[i]);
		}
		pos+=snprintf(pos,size-(pos-print_buf),str_format, buff);
	}
	for(i=0;(unsigned int)i<sizeof(extra)/sizeof(extra[0]);i++)
	{
		pos+=snprintf(pos,size-(pos-print_buf),str_format, extra[i]);
	}
	pos+=snprintf(pos,size-(pos-print_buf),"\n");
	return pos-print_buf;
}

static int output_file_print_hdr_unit(struct metric *metric, char *print_buf, size_t size)
{
	double * bins = metric->histogram.bins;
	int bins_num  = metric->histogram.bins_num;
	char* pos=print_buf;
	long long value=0;
	int i=0;
	struct histogram_t *h=&(metric->histogram);
	struct hdr_histogram *h_out=NULL, *h_tmp=NULL;
	char int_format[STR_LEN_256], double_format[STR_LEN_256];
	snprintf(int_format, sizeof(int_format), "%%%dlld",HISTOGRAM_WIDTH);
	snprintf(double_format, sizeof(double_format), "%%%d.2lf",HISTOGRAM_WIDTH);

	hdr_init(h->lowest_trackable_value,  h->highest_trackable_value, h->significant_figures, &(h_tmp));
	if(h->previous_changed!=NULL)	hdr_close(h->previous_changed);

	h->previous_changed=atomic_read(&(h->changing));
	h_tmp=atomic_set(&(h->changing), h_tmp);// left h_tmp is used to avoid warining [-Wunused-value]

	hdr_add(h->accumulated, h->previous_changed);
	h_out=h->accumulated; //TODO
/*
	if(metric->calc_type==FS_CALC_SPEED)
	{
		h_out=h->previous_changed;
	}
	else
	{
		h_out=h->accumulated;
	}
*/
	pos+=snprintf(pos,size-(pos-print_buf),"%-10s\t", metric->field_name);

	for(i=0;i<bins_num;i++)
	{
		if(metric->field_type == FIELD_TYPE_SUMMARY)
		{
			value=(long long)hdr_value_at_percentile(h_out, bins[i]*100);
		}
		if(metric->field_type == FILED_TYPE_HISTOGRAM)
		{
			value= hdr_count_le_value(h_out, (long long)bins[i]);
		}
		pos+=snprintf(pos,size-(pos-print_buf),int_format, value);
	}
	pos+=snprintf(pos,size-(pos-print_buf),int_format,h_out->total_count==0?0:(long long)hdr_max(h_out));
	pos+=snprintf(pos,size-(pos-print_buf),int_format,h_out->total_count==0?0:(long long)hdr_min(h_out));
	pos+=snprintf(pos,size-(pos-print_buf),double_format,h_out->total_count==0?0:hdr_mean(h_out));
	pos+=snprintf(pos,size-(pos-print_buf),double_format,h_out->total_count==0?0:hdr_stddev(h_out));
	pos+=snprintf(pos,size-(pos-print_buf),int_format,(long long)h_out->total_count);

	pos+=snprintf(pos,size-(pos-print_buf),"\n");

	h_tmp=NULL;

	return pos-print_buf;
}

static int output_file_format_default_type_histogram_and_summary(struct fieldstat_instance *instance, int current_metric_cnt,long long interval_ms, char*print_buf, size_t size)
{
	int i = 0, j = 0, metric_num = 0;
	char *pos = print_buf;
	//display_manifest_t* p=NULL;
	struct metric *metric = NULL;
	struct metric *metric_array[current_metric_cnt];
	int metric_is_print[current_metric_cnt];

	if(instance->histogram_cnt == 0 
		&& instance->summary_cnt == 0)
	{
		return 0;
	}

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
		if(metric_is_print[i] == 1)
		{
			continue;
		}
		pos += output_file_print_hdr_head(metric_array[i], pos, size-(pos-print_buf));
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
			pos += output_file_print_hdr_unit(metric_array[j], pos, size-(pos-print_buf));
			metric_is_print[j] = 1;
		}
		if(pos-print_buf>0)
		{
			if(*(pos-1)=='\n')
			{
				pos--;
			}
			pos+=snprintf(pos,size-(pos-print_buf),"\n%s\n", draw_line);
		}
	}
	return pos-print_buf;	
}


int fieldstat_output_file(struct fieldstat_instance *instance,long long interval_ms)
{
	int tables_line_cnt[TABLE_MAX_NUM];
	int current_table_cnt = instance->table_num;
	get_current_table_line_cnt(instance, current_table_cnt, tables_line_cnt);
	int current_metric_cnt = instance->metric_cnt;
	size_t print_buf_sz = current_metric_cnt*1024;
	char *print_buf = NULL;
	char *append_pos = NULL;
	time_t current = 0;
	char ctime_buff[STR_LEN_32]={0};

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
		print_buf = (char*)calloc(sizeof(char), print_buf_sz);
		append_pos = print_buf;
		append_pos += snprintf(append_pos, print_buf_sz - (append_pos - print_buf), "%s%s", draw_boundary, ctime_buff);
		append_pos --;//jump '\n' generate by ctime()
		append_pos += snprintf(append_pos, print_buf_sz - (append_pos - print_buf),"%s\n",draw_boundary);

		//pthread_mutex_lock(&(_handle->reg_lock)); //TODO
		append_pos += output_file_format_default_type_gauge(instance, current_metric_cnt, interval_ms, append_pos, print_buf_sz - (append_pos - print_buf));
		append_pos += output_file_format_default_type_counter(instance, current_metric_cnt, interval_ms, append_pos, print_buf_sz - (append_pos - print_buf));
		append_pos += output_file_format_default_table(instance, tables_line_cnt, current_table_cnt, interval_ms, append_pos, print_buf_sz - (append_pos - print_buf));
		append_pos += output_file_format_default_type_histogram_and_summary(instance,current_metric_cnt,interval_ms, append_pos, print_buf_sz - (append_pos - print_buf));
		//TODO  output table,output histogram,output summary 
		//pthread_mutex_unlock(&(_handle->reg_lock));//TODO
	}

    if(!strcmp(instance->local_output_format, "json"))
    {
		//TODO from json output
    }

	fseek(instance->local_output_fp, 0, SEEK_SET);
	if(print_buf)
	{
		fwrite(print_buf, append_pos - print_buf, 1,instance->local_output_fp);
	}
	fflush(instance->local_output_fp);

	if(print_buf)
	{
		free(print_buf);
		print_buf = NULL;
	}
	return 0;
}


