#include "fieldstat_internal.h"
#define HTTPSERVER_IMPL
#include "httpserver.h"

struct prometheus_endpoint_instance g_prometheus_endpoint_instance = 
{
	9273,
	NULL,
	0,
	0,
	0,
	NULL,
	0,
	NULL,
	REALLOC_SCALE_SIZE,
};

static char* str_unescape(char* s, char *d, int d_len)
{
	int i=0,j=0;
	int len=strlen(s);
	for(i=0; i<len && j<d_len; i++)
	{
		if(s[i]=='(' || s[i]==')'  || s[i]=='{' || s[i]=='}' || 
		   s[i]=='/' || s[i]=='\\' || s[i]=='%' || s[i]=='*' || 
		   s[i]=='$' || s[i]=='-'  || s[i]==',' || s[i]==';')
		{
			if(i==0)
			{
				continue;
			}

			d[j++]='_';
		}
		else
		{
			d[j++]=s[i];
		}
	}

	if(d[j-1]=='_')
	{
		j-=1;
	}

	d[j]='\0';
	
	return 0;
}

/*
* ret = -1, output not match fieldstat instance
	output:
		/metrics
		/sapp
		/tfe
		...
* ret >=0 && ret < instance_cnt  output sepecify fieldstat instance 
	output:
		http://127.0.0.1:9273/sapp content
* ret = instance_cnt output all fieldstat instance
   	output:  
		http://127.0.0.1:9273/metrics content
*/
static int prometheus_get_fs_instance_id(struct prometheus_endpoint_instance *global_prometheus_output, char *uri, int uri_len)
{
	int i = 0;
	char instance_name_url[INSTANCE_NAME_LEN + 1] = {0};
	int  instance_name_url_len = 0;

	if(uri_len == (int)strlen(global_prometheus_output->url_path) 
		  && 0 == memcmp(uri, global_prometheus_output->url_path, strlen(global_prometheus_output->url_path)))
	{		
		return global_prometheus_output->fs_instance_cnt;
	}
	
	for(i = 0; i < global_prometheus_output->fs_instance_cnt; i++)
	{
		memset(instance_name_url, 0, sizeof(instance_name_url));
		instance_name_url_len = snprintf(instance_name_url, sizeof(instance_name_url),"/%s",global_prometheus_output->fs_instance[i]->name);

		if(uri_len == instance_name_url_len  
			  && 0 == memcmp( uri, instance_name_url, instance_name_url_len))
		{
			return i;
		}
	}
	return -1;
}

static void prometheus_output_uri_list(struct prometheus_endpoint_instance *prometheus_output,struct http_request_s* request)
{
	int i = 0;
	int payload_len = 0;
	char *payload = NULL;
	char *payload_append_position = NULL;
	struct fieldstat_instance **fs_instance = NULL;
	struct http_response_s* response = NULL;

	fs_instance = prometheus_output->fs_instance;
	
	if(prometheus_output->fs_instance_cnt > 0)
	{
		payload_len = prometheus_output->fs_instance_cnt * 128; //TODO using marco, len?
		printf("payload_len =%d\n, n_instance =%d\n", payload_len, prometheus_output->fs_instance_cnt);
		payload = (char *)calloc(1,payload_len);
		payload_append_position = payload;
		payload_append_position += snprintf(payload_append_position, payload_len - (payload_append_position - payload),"url_path:\n\t%s\n", prometheus_output->url_path);

		for(i = 0; i < prometheus_output->fs_instance_cnt; i++)
		{
			payload_append_position += snprintf(payload_append_position, payload_len - (payload_append_position - payload),"\t/%s\n", fs_instance[i]->name);
		}
	}
	else
	{
		payload = (char *)calloc(1, 128);
		strncpy(payload, "Not Invaild instance\n", 128);
	}
	response = http_response_init();
	http_response_status(response, 404);
	http_response_header(response, "Content-Type", "text/plain; charset=utf-8");
	http_response_body(response, payload, strlen(payload));
	http_respond(request, response);
	
	free(payload);
	payload=NULL;
	return;
}

static int prometheus_output_read_metric_tags(struct metric *metric, char *instance_name, char *tags_buff, unsigned int size)
{
	int i = 0;//used_len = 0;
	char unescape[STR_LEN_256] = {0};
	char *append_pos = tags_buff;
	
	//used_len += snprint(tags_buff, size - used_len, "app_name=\"%s\"", instance_name);

	append_pos += snprintf(append_pos, size - (append_pos - tags_buff), "app_name=\"%s\"", instance_name);

	if(metric->table)
	{
		append_pos += snprintf(append_pos, size - (append_pos - tags_buff), ",line_name=\"%s\"", metric->field_name);
	}

	for(i = 0; i < (int)metric->n_tag; i++)
	{
		memset(unescape, 0, sizeof(unescape));
		str_unescape(metric->tag_key[i], unescape, sizeof(unescape));
		append_pos += snprintf(append_pos, size - (append_pos - tags_buff), ",%s=\"%s\"", unescape, metric->tag_value[i]);
	}
	return append_pos - tags_buff;
}


static int prometheus_output_read_metric_name(struct metric *metric, char *instance_app_name, char *name_buff, unsigned int size)
{
	char unescape[256] = {0};
	char *append_pos = name_buff;
	if(metric->table)
	{
		str_unescape(metric->table->column_name[metric->table_column_id], unescape, sizeof(unescape));
	}
	else
	{
		str_unescape(metric->field_name, unescape, sizeof(unescape));
	}
	append_pos += snprintf(append_pos, size - (append_pos - name_buff), "%s_%s", instance_app_name, unescape);

	return append_pos - name_buff;
}


static int prometheus_output_histogram_and_summary(struct metric *metric, char *payload, int payload_len, char *instance_app_name, char *metric_name, char *metric_tags)
{
	long long value=0;
	long long sum = 0;
	int i=0,used_len=0;

	struct hdr_iter iter;
	char *output_format = NULL;

	//struct histogram_t* h=&(p->histogram);
	struct histogram_t *h = &(metric->histogram);
	struct hdr_histogram* h_out=NULL, *h_tmp=NULL;
	int bin_num = metric->histogram.bins_num;
	double *bins = metric->histogram.bins;

	if(metric->field_type == FILED_TYPE_HISTOGRAM)
	{
		output_format = (char *)"%s_bucket{%s,le=\"%0.2f\"} %lld\n";
	}

	if(metric->field_type == FIELD_TYPE_SUMMARY)
	{
		output_format = (char *)"%s{%s,quantile=\"%0.2f\"} %lld\n";
	}

	hdr_init(h->lowest_trackable_value,  h->highest_trackable_value, h->significant_figures, &(h_tmp));

	hdr_add(h_tmp, h->accumulated);
	hdr_add(h_tmp, h->changing);
	h_out=h_tmp;

	for(i=0;i<bin_num;i++)
	{
		if(metric->field_type == FILED_TYPE_HISTOGRAM)
		{
			value = (long long)hdr_count_le_value(h_out, (long long)bins[i]);
		}
		if(metric->field_type == FIELD_TYPE_SUMMARY)
		{
			value=(long long)hdr_value_at_percentile(h_out, bins[i]);
		}
	
		used_len+=snprintf(payload+used_len,
							payload_len-used_len,
							output_format,
							metric_name,
							metric_tags,
							bins[i],
							value
					);
	}
	used_len+=snprintf(payload+used_len,
							payload_len-used_len,
							"%s_count{%s} %lld\n",
							metric_name,
							metric_tags,
							(long long)h_out->total_count
					);

	hdr_iter_recorded_init(&iter, h_out);
	while (hdr_iter_next(&iter))
	{
		sum+=(long long)iter.value;
	}
	used_len+=snprintf(payload+used_len,
			payload_len-used_len,
			"%s_sum{%s} %lld\n",
			metric_name,
			metric_tags,
			sum
			);

	hdr_close(h_tmp);
	h_tmp=NULL;

	return used_len;
}

static int is_output_prometheus(struct metric *metric)
{
	int i = 0, ret = 0;
	for(i = 0; i < (int)metric->n_tag; i++)
	{
		if(strcmp(metric->tag_key[i], "disable_output_prometheus") == 0
			&& strcmp(metric->tag_value[i], "yes") == 0)
		{
			ret = 1;
			break;
		}
	}
	return ret;
}


static int prometheus_get_instance_metric_playload(struct fieldstat_instance *instance, char **payload, int *payload_size, int offset)
{
	int i = 0;
	struct metric *metric = NULL;
	long long value = 0;
	char metric_name[256] = {0};     //match the regex [a-zA-Z_:][a-zA-Z0-9_:]*
	char instance_name[256] = {0};
	char metric_tags[1024] = {0};    //label name match the regex [a-zA-Z_:][a-zA-Z0-9_:]* 
	int append_offset = offset;
	char *new_payload = NULL;
	int new_payload_size = 0;	
	int current_metric_cnt = instance->metric_cnt;

	if(instance->running != 1)
	{
		return -1;
	}

	if(payload == NULL)
	{
		return -1;
	}

	new_payload = *payload;
	new_payload_size = *payload_size;

	str_unescape(instance->name, instance_name, sizeof(instance_name));

	for(i = 0; i < current_metric_cnt; i++)
	{
		metric = get_metric(instance, i);
		if(metric == NULL)
		{
			continue;
		}
		if(is_output_prometheus(metric) == 1)
		{
			continue;
		}
		if(metric->is_ratio == 1)
		{
			continue;
		}
		if(metric->is_invisible == 1)
		{
			continue;
		}

		if( new_payload_size - append_offset <= LEFT_MIN_BUFF_LEN)
		{
			new_payload_size += REALLOC_SCALE_SIZE;
			new_payload = (char *)realloc(new_payload, new_payload_size);
		}

		prometheus_output_read_metric_name(metric, instance_name, metric_name, sizeof(metric_name));
		prometheus_output_read_metric_tags(metric, instance_name, metric_tags, sizeof(metric_tags));

		switch(metric->field_type)
		{
			case FIELD_TYPE_COUNTER:
			case FIELD_TYPE_GAUGE:
				value = get_metric_unit_val(metric, FS_CALC_CURRENT, 1);
				append_offset += snprintf(new_payload + append_offset, 
											new_payload_size - append_offset,
											"%s{%s} %lld\n", 
											metric_name, 
											metric_tags, 
											value
										);
				break;
			case FIELD_TYPE_SUMMARY:
			case FILED_TYPE_HISTOGRAM:
				append_offset += prometheus_output_histogram_and_summary(metric,new_payload + append_offset,
								new_payload_size - append_offset, instance_name, metric_name, metric_tags);
				break;
			default:
				break;
		}
	}

	*payload = new_payload;
	*payload_size = new_payload_size;

	return append_offset;

}

static void prometheus_output_instance_metric(struct prometheus_endpoint_instance *prometheus_output, struct http_request_s* request, int fs_instance_idx)
{
	int i = 0;
	int payload_size = 0;
	int payload_offset = 0;
	char *payload = NULL;
	struct fieldstat_instance *instance = NULL;
	struct http_response_s* response = NULL;

	if(fs_instance_idx == prometheus_output->fs_instance_cnt)
	{
		for(i = 0; i < prometheus_output->fs_instance_cnt; i++)
		{
			instance = prometheus_output->fs_instance[i];
			payload_offset = prometheus_get_instance_metric_playload(instance, &payload, &payload_size, payload_offset);				
		}
	}
	else
	{
		instance = prometheus_output->fs_instance[fs_instance_idx];
		payload_offset = prometheus_get_instance_metric_playload(instance, &payload, &payload_size, payload_offset);	
	}

	if(payload != NULL)
	{	
		response = http_response_init();
		http_response_status(response, 200);
		http_response_header(response, "Content-Type", "text/plain; charset=utf-8");
		http_response_body(response, payload, strlen(payload));
		http_respond(request, response);
	
		free(payload);
		payload=NULL;
	}

}

void prometheus_endpoint_instance_output(struct http_request_s* request)
{
	int fs_instance_idx = -1;
	struct http_string_s uri;
	struct prometheus_endpoint_instance *prometheus_endpoint_instance = &g_prometheus_endpoint_instance;

	uri = http_request_target(request);
	fs_instance_idx = prometheus_get_fs_instance_id(prometheus_endpoint_instance, (char *)uri.buf, uri.len);

	fs_instance_idx == -1 ? 
		prometheus_output_uri_list(prometheus_endpoint_instance, request) 
		:prometheus_output_instance_metric(prometheus_endpoint_instance, request, fs_instance_idx);

	return;
}


void *prometheus_endpoint_listen_thread_entry(void *arg)
{
	struct prometheus_endpoint_instance *global_prometheus = (struct prometheus_endpoint_instance *)arg;
	http_server_listen(global_prometheus->server_handle);
	return NULL;
} 

int fieldstat_global_enable_prometheus_endpoint(unsigned short listen_port, const char *url_path)
{
	g_prometheus_endpoint_instance.server_handle = http_server_init(listen_port, prometheus_endpoint_instance_output);
	if(g_prometheus_endpoint_instance.server_handle == NULL)
	{
		return -1;
	}

	g_prometheus_endpoint_instance.url_path = url_path == NULL ? strdup(PROMETHEUS_ENDPOINT_DEFAULT_URL):strdup(url_path);
	g_prometheus_endpoint_instance.port = listen_port;
	g_prometheus_endpoint_instance.running = 1;
	g_prometheus_endpoint_instance.fs_instance = (struct fieldstat_instance **)calloc( sizeof(struct fieldstat_instance *), g_prometheus_endpoint_instance.fs_instance_size);
	pthread_create(&g_prometheus_endpoint_instance.tid, NULL, prometheus_endpoint_listen_thread_entry, (void *)&g_prometheus_endpoint_instance);
	g_prometheus_endpoint_instance.thread_created = 1;
	return 0;
}

int fieldstat_enable_prometheus_output(struct fieldstat_instance *instance)
{
	int fs_instance_id = 0;

	if(g_prometheus_endpoint_instance.running != 1)
	{
		return -1;
	}

	if(g_prometheus_endpoint_instance.fs_instance_cnt >= g_prometheus_endpoint_instance.fs_instance_size)
	{
		g_prometheus_endpoint_instance.fs_instance_size += REALLOC_SCALE_SIZE;
		g_prometheus_endpoint_instance.fs_instance = (struct fieldstat_instance **)realloc(g_prometheus_endpoint_instance.fs_instance, g_prometheus_endpoint_instance.fs_instance_size);
	}

	fs_instance_id = g_prometheus_endpoint_instance.fs_instance_cnt++;
	g_prometheus_endpoint_instance.fs_instance[fs_instance_id] = instance;
	return 0;
}


void fieldstat_global_disable_prometheus_endpoint()
{
	void *pthread_ret;

	if(g_prometheus_endpoint_instance.thread_created == 1)
	{
		pthread_cancel(g_prometheus_endpoint_instance.tid);
		pthread_join(g_prometheus_endpoint_instance.tid, &pthread_ret);
		g_prometheus_endpoint_instance.running = 0;
		g_prometheus_endpoint_instance.thread_created = 0;
		free(g_prometheus_endpoint_instance.server_handle);
		g_prometheus_endpoint_instance.server_handle = NULL;
	}

	if(g_prometheus_endpoint_instance.url_path)
	{
		free(g_prometheus_endpoint_instance.url_path);
		g_prometheus_endpoint_instance.url_path = NULL;
	}

	if(g_prometheus_endpoint_instance.fs_instance)
	{
		free(g_prometheus_endpoint_instance.fs_instance);
		g_prometheus_endpoint_instance.fs_instance = NULL;
		g_prometheus_endpoint_instance.fs_instance_cnt = 0;
		g_prometheus_endpoint_instance.fs_instance_size = 0;
	}

	return;
}

