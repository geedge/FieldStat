#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "mongoose.h"
#include "field_stat2.h"
#include "field_stat_internal.h"

#define	STEP_SIZE	1024
#define	MAX_URL_PATH_LEN	128
#define	MAX_FS2_HANDLE_NUM	64

struct FS_library_runtime
{
 	int fs2_handle_cnt;
	unsigned short port;
	unsigned short init_flag;
	struct mg_mgr mgr;
	struct mg_connection *nc;	
	pthread_mutex_t library_lock;
	char url_path[MAX_URL_PATH_LEN];
	struct FS_space_t *fs2_handle[MAX_FS2_HANDLE_NUM];
};

struct FS_library_runtime *g_FS2_LIBRARY_handle;

static struct FS_library_runtime *create_runtime_handle(void)
{
	struct FS_library_runtime *_handle=(struct FS_library_runtime *)calloc(1, sizeof(struct FS_library_runtime));
	_handle->port=9273;
	memcpy(_handle->url_path, "/metrics", strlen("/metrics"));
	pthread_mutex_init(&_handle->library_lock, NULL);

	return _handle;
}

static int check_http_request(struct FS_library_runtime *_handle, char *uri, int uri_len)
{
	int i=0;
	if(uri_len==(int)strlen(_handle->url_path) && !(memcmp(uri, _handle->url_path, strlen(_handle->url_path))))
	{		
		return _handle->fs2_handle_cnt;
	}
	
	for(i=0; i<_handle->fs2_handle_cnt; i++)
	{
		if(uri_len-1==(int)strlen(_handle->fs2_handle[i]->app_name) 
			&& !(memcmp(uri+1, _handle->fs2_handle[i]->app_name, strlen(_handle->fs2_handle[i]->app_name))))
		{
			return i;
		}
	}

	return -1;
}

static int histgram_output_summary(display_manifest_t* p, char *app_name, double * bins, int bin_num, char*payload, int payload_len)
{
	long long value=0;
	int i=0,used_len=0;
	struct histogram_t* h=&(p->histogram);
	struct hdr_histogram* h_out=NULL, *h_tmp=NULL;

	hdr_init(h->lowest_trackable_value,  h->highest_trackable_value, h->significant_figures, &(h_tmp));

	hdr_add(h_tmp, h->accumulated);
	hdr_add(h_tmp, h->changing);
	h_out=h_tmp;

	for(i=0;i<bin_num;i++)
	{
		value=(long long)hdr_value_at_percentile(h_out, bins[i]);		
		used_len+=snprintf(payload+used_len,
							payload_len-used_len,
							"%s_%s{app_name=\"%s\",quantile=\"%f\"} %llu\n",
							app_name,
							p->name,
							app_name,
							bins[i]/100,
							value
					);		
	}
	
	used_len+=snprintf(payload+used_len,
							payload_len-used_len,
							"%s_%s_count{app_name=\"%s\"} %llu\n",
							app_name,
							p->name,
							app_name,
							(long long)(h_out->total_count)
					);

	hdr_close(h_tmp);
	h_tmp=NULL;

	return used_len;
}

static void FS_library_promethues_output(struct mg_connection *c, int ev, void *message, void* user_data)
{
	int i=0,j=0;
	int payload_len=0;
	int used_len=0;
	long long value=0;
	char* payload=NULL;
	long long sum=0;
	int output_cnt=0;
	struct hdr_iter iter;
	struct FS_space_t *fs2_handle=NULL;
	struct display_manifest_t *p=NULL,*p_column=NULL;
	struct FS_library_runtime *_handle=(struct FS_library_runtime *)user_data;
	
	if (ev != MG_EV_HTTP_REQUEST)
	{
		return;
	}
	struct http_message *hm = (struct http_message *)message;

	int fs2_handle_idx=check_http_request(_handle, (char *)hm->uri.p, hm->uri.len);
	if(fs2_handle_idx==-1)
	{
		return ;
	}

	if(fs2_handle_idx==_handle->fs2_handle_cnt)
	{
		i=0;
		output_cnt=_handle->fs2_handle_cnt;
	}
	else
	{
		i=fs2_handle_idx;
		output_cnt=fs2_handle_idx;
	}
	
	for(; i<=output_cnt; i++)
	{
		fs2_handle=_handle->fs2_handle[i];
		if(fs2_handle->running==0)
		{
			continue;
		}

		pthread_mutex_lock(&(fs2_handle->reg_lock));
		for(i=0;i<fs2_handle->display_cnt;i++)
		{
			p=fs2_handle->display[i];
			if(p->not_send_to_server==1||p->is_ratio==1)
			{
				continue;
			}

			if(payload_len-used_len<=100)
			{
				payload_len+=STEP_SIZE;
				payload=(char *)realloc(payload, payload_len);
			}
						
			switch(p->style)
			{
				case FS_STYLE_STATUS:
				case FS_STYLE_FIELD:
					value=get_stat_unit_val(p, 0, FS_CALC_CURRENT, 1);
					used_len+=snprintf(payload+used_len, 
										payload_len-used_len, 
										"%s_%s{app_name=\"%s\"} %llu\n",
										fs2_handle->app_name,
										p->name,
										fs2_handle->app_name,
										value
									);
					break;
				case FS_STYLE_LINE:
					for(j=0;j<fs2_handle->column_cnt;j++)
					{
						p_column=fs2_handle->display[fs2_handle->cloumn_id[j]];
						if(p_column->not_send_to_server==1||p_column->is_ratio==1)
						{
							continue;
						}
						value=get_stat_unit_val(p, p_column->column_seq, FS_CALC_CURRENT, 1);						
						used_len+=snprintf(payload+used_len, 
											payload_len-used_len, 
											"%s_%s{app_name=\"%s\",line_name=\"%s\"} %llu\n",
											fs2_handle->app_name,
											p_column->name,
											fs2_handle->app_name,
											p->name, 
											value
										);

						if(payload_len-used_len<=100)
						{
							payload_len+=STEP_SIZE;
							payload=(char *)realloc(payload, payload_len);
						}
					}
					break;
				case FS_STYLE_HISTOGRAM:
					for(i=0;i<fs2_handle->display_cnt;i++)
					{
						p=fs2_handle->display[i];
						if(p->style!=FS_STYLE_HISTOGRAM)
						{
							continue;
						}

						if(payload_len-used_len<=STEP_SIZE)
						{
								payload_len+=STEP_SIZE;
								payload=(char *)realloc(payload, payload_len);
						}

						used_len+=histgram_output_summary(p, 
															fs2_handle->app_name, 
															fs2_handle->histogram_bins,
															fs2_handle->histogram_bin_num,
															payload+used_len,
															payload_len-used_len
														);
						
						if(p->histogram.previous_changed!=NULL)
						{
							hdr_iter_recorded_init(&iter, p->histogram.accumulated);
							while (hdr_iter_next(&iter))
							{
								sum+=(long long)iter.value;
							}

							used_len+=snprintf(payload+used_len,
													payload_len-used_len,
													"%s_%s_sum{app_name=\"%s\"} %llu\n",
													fs2_handle->app_name,
													p->name,
													fs2_handle->app_name,
													sum
											);

							if(payload_len-used_len<=100)
							{
								payload_len+=STEP_SIZE;
								payload=(char *)realloc(payload, payload_len);
							}
						}
					}
					break;
				default:
					break;
			}
		}
        pthread_mutex_unlock(&(fs2_handle->reg_lock));
	}

	if(payload!=NULL)
	{
		mg_send_head(c, 200, strlen(payload), "Content-Type: text/plain; charset=utf-8");
		mg_printf(c, "%s", payload);
	
		free(payload);
		payload=NULL;
	}
	
	return;
}

void *FS_library_promethues_listen(void *arg)
{
	char s_http_port[16]={0};
	struct FS_library_runtime *_handle=(struct FS_library_runtime *)arg;

	if(_handle!=NULL)
	{
		mg_mgr_init(&(_handle->mgr), NULL);

		snprintf(s_http_port, sizeof(s_http_port), "%hu", _handle->port);	
		_handle->nc = mg_bind(&(_handle->mgr), s_http_port, FS_library_promethues_output, _handle);
		if (_handle->nc == NULL)
		{
			printf("%s, Mongoose failed to create listener on port %s\n", (char *)"Prometheus", s_http_port);
			return NULL;
		}
		// Set up HTTP server parameters
		mg_set_protocol_http_websocket(_handle->nc);

		for (;;)
		{
	    	mg_mgr_poll(&_handle->mgr, 1000);
	  	}

		mg_mgr_free(&_handle->mgr);
	}
	
	return NULL;
} 

int FS_library_promethues_register(screen_stat_handle_t handle)
{	
	if(handle!=NULL)
	{	
		if(g_FS2_LIBRARY_handle->fs2_handle_cnt<MAX_FS2_HANDLE_NUM)
		{
			pthread_mutex_lock(&g_FS2_LIBRARY_handle->library_lock);
			int i=g_FS2_LIBRARY_handle->fs2_handle_cnt;
			g_FS2_LIBRARY_handle->fs2_handle[i]=(struct FS_space_t *)handle;
			g_FS2_LIBRARY_handle->fs2_handle_cnt++;
			pthread_mutex_unlock(&g_FS2_LIBRARY_handle->library_lock);
			return 1;
		}
	}

	return 0;
}


int FS_library_set_prometheus_port(unsigned short port)
{
	if(g_FS2_LIBRARY_handle==NULL)
	{
		g_FS2_LIBRARY_handle=create_runtime_handle();
	}
	
	g_FS2_LIBRARY_handle->port=port;
	
	return 1;
}
int FS_library_set_prometheus_url_path(const char *url_path)
{
	if(g_FS2_LIBRARY_handle==NULL)
	{
		g_FS2_LIBRARY_handle=create_runtime_handle();
	}

	if(MAX_URL_PATH_LEN>strlen(url_path)+1)
	{
		memcpy(g_FS2_LIBRARY_handle->url_path, url_path, strlen(url_path));
		return 1;
	}
	
	return 0;
}

int FS_library_init(void)
{
	pthread_t tid=0;
	
	pthread_create(&tid, NULL, FS_library_promethues_listen, g_FS2_LIBRARY_handle);

	return 0;

}


