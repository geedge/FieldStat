#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "mongoose.h"
#include "field_stat2.h"
#include "field_stat_internal.h"

#define	STEP_SIZE	1024
#define	LEFT_MIN_BUFF_LEN	256
#define	MAX_URL_PATH_LEN	128
#define	FS2_HANDLE_STEP_NUM	16

struct FS_library_runtime
{
	pthread_t tid;
	int fs2_handle_cnt;
 	int using_fs2_handle_cnt;
	unsigned short create_thread;
	unsigned short port;
	char *url_path;
	struct mg_mgr mgr;
	struct mg_connection *nc;	
	pthread_mutex_t library_lock;
	struct FS_space_t **fs2_handle;
};

struct FS_library_runtime g_FS2_LIBRARY_handle={
					0, 0, 0, 0, 9273, NULL, {}, NULL, PTHREAD_MUTEX_INITIALIZER, NULL};

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

static int check_http_request(struct FS_library_runtime *_handle, char *uri, int uri_len)
{
	int i=0;
	if(uri_len==(int)strlen(_handle->url_path) && !(memcmp(uri, _handle->url_path, strlen(_handle->url_path))))
	{		
		return _handle->using_fs2_handle_cnt;
	}
	
	for(i=0; i<_handle->using_fs2_handle_cnt; i++)
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
	char line_name[128]={0};
	struct histogram_t* h=&(p->histogram);
	struct hdr_histogram* h_out=NULL, *h_tmp=NULL;

	hdr_init(h->lowest_trackable_value,  h->highest_trackable_value, h->significant_figures, &(h_tmp));

	hdr_add(h_tmp, h->accumulated);
	hdr_add(h_tmp, h->changing);
	h_out=h_tmp;

	for(i=0;i<bin_num;i++)
	{
		value=(long long)hdr_value_at_percentile(h_out, bins[i]);
		str_unescape(p->name, line_name, sizeof(line_name));
		used_len+=snprintf(payload+used_len,
							payload_len-used_len,
							"%s_%s{app_name=\"%s\",quantile=\"%f\"} %llu\n",
							app_name,
							line_name,
							app_name,
							bins[i]/100,
							value
					);		
	}

	str_unescape(p->name, line_name, sizeof(line_name));
	used_len+=snprintf(payload+used_len,
							payload_len-used_len,
							"%s_%s_count{app_name=\"%s\"} %llu\n",
							app_name,
							line_name,
							app_name,
							(long long)(h_out->total_count)
					);

	hdr_close(h_tmp);
	h_tmp=NULL;

	return used_len;
}

static void FS_library_promethues_output(struct mg_connection *c, int ev, void *message, void* user_data)
{
	int i=0,j=0,k=0;
	int payload_len=0;
	int used_len=0;
	long long value=0;
	char* payload=NULL;
	long long sum=0;
	int output_cnt=0;
	int check_flag=0;
	char line_name[128]={0};
	char column_name[128]={0};
	char app_name[128]={0};
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
		payload_len=_handle->using_fs2_handle_cnt * 128;
		payload=(char *)calloc(1, payload_len);

		used_len=strlen("url_path:\n\t");
		memcpy(payload, "url_path:\n\t", used_len);

		memcpy(payload+used_len, _handle->url_path, strlen(_handle->url_path));
		used_len+=strlen(_handle->url_path);
		
		for(i=0; i<_handle->using_fs2_handle_cnt; i++)
		{
			used_len+=snprintf(payload+used_len, payload_len-used_len, "\n\t/%s",_handle->fs2_handle[i]->app_name);
		}

		payload[used_len++]='\n';
		payload[used_len]='\0';
		
		mg_send_head(c, 404, strlen(payload), "Content-Type: text/plain; charset=utf-8");
		mg_printf(c, "%s", payload);
	
		free(payload);
		payload=NULL;
		return ;
	}

	if(fs2_handle_idx==_handle->using_fs2_handle_cnt)
	{
		i=0;
		check_flag=1;
		output_cnt=_handle->using_fs2_handle_cnt;
	}
	else
	{
		check_flag=0;
		i=fs2_handle_idx;
		output_cnt=fs2_handle_idx+1;
	}
	
	for(; i<output_cnt; i++)
	{
		fs2_handle=_handle->fs2_handle[i];
		if(fs2_handle->running==0 || (check_flag==1 && fs2_handle->output_prometheus!=1))
		{
			continue;
		}

		pthread_mutex_lock(&(fs2_handle->reg_lock));
		for(k=0;k<fs2_handle->display_cnt;k++)
		{
			p=fs2_handle->display[k];
			if(p->not_send_to_server==1||p->is_ratio==1)
			{
				continue;
			}

			if(payload_len-used_len<=LEFT_MIN_BUFF_LEN)
			{
				payload_len+=STEP_SIZE;
				payload=(char *)realloc(payload, payload_len);
			}
						
			switch(p->style)
			{
				case FS_STYLE_STATUS:
				case FS_STYLE_FIELD:
					str_unescape(p->name, line_name, sizeof(line_name));
					str_unescape(fs2_handle->app_name, app_name, sizeof(app_name));
					value=get_stat_unit_val(p, 0, FS_CALC_CURRENT, 1);
					used_len+=snprintf(payload+used_len, 
										payload_len-used_len, 
										"%s_%s{app_name=\"%s\"} %llu\n",
										app_name,
										line_name,
										app_name,
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
						str_unescape(p->name, line_name, sizeof(line_name));
						str_unescape(p_column->name, column_name, sizeof(column_name));
						str_unescape(fs2_handle->app_name, app_name, sizeof(app_name));
						value=get_stat_unit_val(p, p_column->column_seq, FS_CALC_CURRENT, 1);						
						used_len+=snprintf(payload+used_len, 
											payload_len-used_len, 
											"%s_%s{app_name=\"%s\",line_name=\"%s\"} %llu\n",
											app_name,
											column_name,
											app_name,
											line_name, 
											value
										);

						if(payload_len-used_len<=LEFT_MIN_BUFF_LEN)
						{
							payload_len+=STEP_SIZE;
							payload=(char *)realloc(payload, payload_len);
						}
					}
					break;
				case FS_STYLE_HISTOGRAM:
					if(p->style!=FS_STYLE_HISTOGRAM)
					{
						continue;
					}

					if(payload_len-used_len<=STEP_SIZE)
					{
						payload_len+=STEP_SIZE;
						payload=(char *)realloc(payload, payload_len);
					}

					str_unescape(fs2_handle->app_name, app_name, sizeof(app_name));

					used_len+=histgram_output_summary(p, 
							app_name, 
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

						str_unescape(p->name, line_name, sizeof(line_name));
						str_unescape(fs2_handle->app_name, app_name, sizeof(app_name));
						used_len+=snprintf(payload+used_len,
								payload_len-used_len,
								"%s_%s_sum{app_name=\"%s\"} %llu\n",
								app_name,
								line_name,
								app_name,
								sum
								);

						if(payload_len-used_len<=LEFT_MIN_BUFF_LEN)
						{
							payload_len+=STEP_SIZE;
							payload=(char *)realloc(payload, payload_len);
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

		while(g_FS2_LIBRARY_handle.create_thread > 0)
		{
	    		mg_mgr_poll(&_handle->mgr, 1000);
	  	}

		mg_mgr_free(&_handle->mgr);
	}
	
	return NULL;
} 

int FS_library_promethues_register(screen_stat_handle_t handle)
{
	struct FS_library_runtime *_handle=&g_FS2_LIBRARY_handle;
	if(handle!=NULL)
	{
		if(_handle->fs2_handle==NULL 
			|| _handle->using_fs2_handle_cnt >= _handle->fs2_handle_cnt)
		{
			_handle->fs2_handle_cnt+=FS2_HANDLE_STEP_NUM;
			_handle->fs2_handle=(struct FS_space_t **)realloc(_handle->fs2_handle, sizeof(struct FS_space_t *)*(_handle->fs2_handle_cnt));
		}
	
		pthread_mutex_lock(&_handle->library_lock);
		int i=_handle->using_fs2_handle_cnt;
		_handle->fs2_handle[i]=(struct FS_space_t *)handle;
		_handle->using_fs2_handle_cnt++;
		pthread_mutex_unlock(&_handle->library_lock);

		return 1;
	}

	return 0;
}


int FS_library_set_prometheus_port(unsigned short port)
{
	g_FS2_LIBRARY_handle.port=port;
	
	return 1;
}
int FS_library_set_prometheus_url_path(const char *url_path)
{
	if(g_FS2_LIBRARY_handle.url_path != NULL)
	{
		free(g_FS2_LIBRARY_handle.url_path);
		g_FS2_LIBRARY_handle.url_path = NULL;
	}
	g_FS2_LIBRARY_handle.url_path=(char *)calloc(1, strlen(url_path)+1);
	memcpy(g_FS2_LIBRARY_handle.url_path, url_path, strlen(url_path));

	return 1;
}

int FS_library_init(void)
{
	if(g_FS2_LIBRARY_handle.url_path == NULL)
	{
		g_FS2_LIBRARY_handle.url_path = (char *)calloc(strlen((char *)"/metrics")+1, 1);
		memcpy(g_FS2_LIBRARY_handle.url_path, (char *)"/metrics", strlen((char *)"/metrics"));
	}
	g_FS2_LIBRARY_handle.create_thread = 1;
	pthread_create(&g_FS2_LIBRARY_handle.tid, NULL, FS_library_promethues_listen, (void *)&g_FS2_LIBRARY_handle);

	return 0;

}

void FS_library_destroy(void)
{
	void* ret ;
	if(g_FS2_LIBRARY_handle.create_thread == 1)
	{
		g_FS2_LIBRARY_handle.create_thread = 0;
		pthread_join(g_FS2_LIBRARY_handle.tid, &ret);
	}
	if(g_FS2_LIBRARY_handle.url_path != NULL)
	{
		free(g_FS2_LIBRARY_handle.url_path);
		g_FS2_LIBRARY_handle.url_path = NULL;
	}
	if(g_FS2_LIBRARY_handle.fs2_handle)
	{
		free(g_FS2_LIBRARY_handle.fs2_handle);
		g_FS2_LIBRARY_handle.fs2_handle=NULL;
	}

	g_FS2_LIBRARY_handle.using_fs2_handle_cnt=0;
	g_FS2_LIBRARY_handle.fs2_handle_cnt=0;
		
	return ;

}

