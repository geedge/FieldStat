#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "mongoose.h"
#include "field_stat2.h"
#include "field_stat_internal.h"

#define	STEP_SIZE	1024
#define	MAX_FS2_HANDLE_NUM	64

struct _prom_handle_t
{
	int out_by_appname;
	int fs2_handle_cnt;
	unsigned short port;
	char *path;
	struct mg_mgr mgr;
	struct mg_connection *nc;
	struct FS_space_t *fs2_handle[MAX_FS2_HANDLE_NUM];
};

struct _prom_handle_t *g_PROMETHEUS_handle;

static int PROM_histogram_summary(display_manifest_t* p, char *app_name, double * bins, int bin_num, char*payload, int payload_len)
{
	long long value=0;
	int i=0,used_len=0;
	struct histogram_t* h=&(p->histogram);
	struct hdr_histogram* h_out=NULL, *h_tmp=NULL;

	hdr_init(h->lowest_trackable_value,  h->highest_trackable_value, h->significant_figures, &(h_tmp));
	if(h->previous_changed!=NULL)	hdr_close(h->previous_changed);

	h->previous_changed=atomic_read(&(h->changing));
	h_tmp=atomic_set(&(h->changing), h_tmp);// left h_tmp is used to avoid warining [-Wunused-value]

	hdr_add(h->accumulated, h->previous_changed);
	h_out=h->accumulated;

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
	

	return used_len;
}

static void PROM_output(struct mg_connection *c, int ev, void *message, void* user_data)
{
	int i=0,j=0;
	int payload_len=0;
	int used_len=0;
	long long value=0;
	char* payload=NULL;
	long long sum=0;
	struct hdr_iter iter;
	struct FS_space_t *fs2_handle=NULL;
	struct display_manifest_t *p=NULL,*p_column=NULL;
	struct _prom_handle_t *_handle=(struct _prom_handle_t *)user_data;
	
	if (ev != MG_EV_HTTP_REQUEST)
	{
		return;
	}
	struct http_message *hm = (struct http_message *)message;
	if(hm->uri.len!=strlen(_handle->path) || (memcmp(hm->uri.p, _handle->path, strlen(_handle->path))))
	{
		return ;
	}

	payload_len=STEP_SIZE;
	payload=(char *)calloc(1, payload_len);

	for(i=0; i<_handle->fs2_handle_cnt; i++)
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

						used_len+=PROM_histogram_summary(p, 
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

			if(payload_len-used_len<=100)
							{
								payload_len+=1024;
								payload=(char *)realloc(payload, payload_len);
							}
		}
        pthread_mutex_unlock(&(fs2_handle->reg_lock));
	}
	
	mg_send_head(c, 200, strlen(payload), "Content-Type: text/plain; charset=utf-8");
	mg_printf(c, "%s", payload);

	free(payload);
	payload=NULL;
	payload_len=0;
	
	return;
}

void *prometheus_listen(void *arg)
{
	char s_http_port[16]={0};
	struct _prom_handle_t *_handle=(struct _prom_handle_t *)arg;

	if(_handle!=NULL)
	{
		mg_mgr_init(&(_handle->mgr), NULL);

		snprintf(s_http_port, sizeof(s_http_port), "%hu", _handle->port);	
		_handle->nc = mg_bind(&(_handle->mgr), s_http_port, PROM_output, _handle);
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

int PROM_init_handle(PROM_handle_t handle)
{
	pthread_t tid=0;
	pthread_create(&tid, NULL, prometheus_listen, handle);

	return 0;
}

PROM_handle_t PROM_create_handle(void)
{	
	struct _prom_handle_t *_handle=(struct _prom_handle_t *)calloc(1, sizeof(struct _prom_handle_t));
	_handle->port=9273;
	_handle->path=(char *)"/metrics";

	g_PROMETHEUS_handle=_handle;
	
	return (void *)_handle;
}

int PROM_set_opt(PROM_handle_t handle, enum PROM_option  type,const void* value,int size)
{
	struct _prom_handle_t *_handle=(struct _prom_handle_t *)handle;
	if(_handle!=NULL)
	{
		switch(type)
		{
			case	PROM_OPT_LISTEN_PORT:
				if(value!=NULL && size==sizeof(unsigned short))
				{
					_handle->port=*(unsigned short *)value;
					return 1;
				}
				break;
			case	PROM_OPT_OUT_PATH:
				if(value!=NULL && size>0)
				{
					_handle->path=(char *)calloc(1, size+1);
					memcpy(_handle->path, value, size);
					return 1;
				}
				break;
			case	PROM_OPT_OUT_BY_APPNAME:
				if(value!=NULL && size==sizeof(int))
				{
					_handle->out_by_appname=*(int *)value;
					return 1;
				}
				break;
			default:
				break;
		}
	}

	return 0;
}

int PROM_register(screen_stat_handle_t handle)
{	
	if(handle!=NULL && g_PROMETHEUS_handle!=NULL)
	{	
		if(g_PROMETHEUS_handle->fs2_handle_cnt<MAX_FS2_HANDLE_NUM)
		{
			int i=g_PROMETHEUS_handle->fs2_handle_cnt;
			g_PROMETHEUS_handle->fs2_handle[i]=(struct FS_space_t *)handle;
			g_PROMETHEUS_handle->fs2_handle_cnt++;
			return 1;
		}
	}

	return 0;
}
