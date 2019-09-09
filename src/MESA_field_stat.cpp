#include "field_stat2.h"
#include "hdr_histogram.h"
#include "cJSON.h"

#include <sys/socket.h>//socket
#include <sys/types.h>//socket
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h> //strerror
#include <errno.h>//strerror
#include <fcntl.h>//fcntl
#include <unistd.h>//fcntl
#include <net/if.h>//fcntl
#include <sys/ioctl.h>//ioctl
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>



#define	INIT_STAT_FIELD_NUM		1024
#define	MAX_STAT_COLUMN_NUM		32
#define MAX_PATH_LEN			256
#define	UDP_PAYLOAD_SIZE		1460

#define	STATUS_PER_LINE				6
#define	FIELD_PER_LINE				8



#define	HISOTGRAM_EXTRA_INF			0
#define	HISTOGRAM_EXTRA_SUM			1
#define	HISTOGRAM_EXTRA_MAXVAL		2
#define	HISTOGRAM_EXTRA_SIZE		3

double HISTOGRAM_DEFAULT_BINS[]={50.0, 80.0, 90.0, 95.0, 99.0};


int FIELD_STAT_VERSION_2_8_20190125=0;

#if(__GNUC__ * 100 + __GNUC_MINOR__ * 10 + __GNUC_PATCHLEVEL__ >= 410)
#define atomic_inc(x) __sync_add_and_fetch((x),1)
#define atomic_dec(x) __sync_sub_and_fetch((x),1)
#define atomic_add(x,y) __sync_add_and_fetch((x),(y))
#define atomic_sub(x,y) __sync_sub_and_fetch((x),(y))
typedef long atomic_t;
#define ATOMIC_INIT(i)  { (i) }
#define atomic_read(x) __sync_add_and_fetch((x),0)
#define atomic_set(x,y) __sync_lock_test_and_set((x),y)
#else 
typedef long atomic_t;
#define atomic_inc(x)	((*(x))++)
#define atomic_dec(x)	((*(x))--)
#define atomic_add(x,y) ((*(x))+=(y))
#define atomic_sub(x,y) ((*(x))-=(y))
#define ATOMIC_INIT(i)  { (i) }
#define atomic_read(x) 	(*(x))
#define atomic_set(x,y)	((*(x))=(y))
#endif 

//Automatically generate the version number
#ifdef __cplusplus
extern "C"
{
#endif

#define GIT_VERSION_CATTER(v) __attribute__((__used__)) const char * GIT_VERSION_##v = NULL
#define GIT_VERSION_EXPEND(v) GIT_VERSION_CATTER(v)

/* VERSION TAG */
#ifdef GIT_VERSION
GIT_VERSION_EXPEND(GIT_VERSION);
#else
static __attribute__((__used__)) const char * GIT_VERSION_UNKNOWN = NULL;
#endif
#undef GIT_VERSION_CATTER
#undef GIT_VERSION_EXPEND

#ifdef __cplusplus
}
#endif
//endof Automatically generate the version number

const char* draw_line="________________________________________________________________________________________________________________________________________________";
const char* draw_boundary="============================================================";


static char* __str_dup(const char* str)
{
	char* dup=NULL;
	dup=(char*)calloc(sizeof(char),strlen(str)+1);
	memcpy(dup, str, strlen(str));
	return dup;
}
//histogram bins format example:  "10,100,1000,2000" will return 4 bins which indicate 5 interval.
static int parse_histogram_bin_format(const char* format, double **output_bins)
{
	char *token=NULL,*sub_token=NULL,*saveptr;
	size_t i=0;
	int comma_num=0,ret=0;
	double *bins;
	char* dup_format=__str_dup(format);
	for(i=0;i<strlen(dup_format);i++)
	{
		if(dup_format[i]==',')
		{
			comma_num++;
		}
	}
	bins=(double*)calloc(sizeof(double),comma_num+1);
	for (token = dup_format,i=0; ; token= NULL, i++)
	{
		sub_token= strtok_r(token,",", &saveptr);
		if (sub_token == NULL)
			break;
		ret=sscanf(sub_token,"%lf",bins+i);
		if(bins[i]>1.0)
		{
			goto error_out;
		}
		bins[i] *= 100;
		if(ret!=1)
		{
			goto error_out;
		}
	}
	free(dup_format);
	*output_bins=bins;
	return i;
error_out:
	free(dup_format);
	free(bins);
	return -1;
}
struct stat_unit_t
{
	long long changing;
	long long accumulated;	
	long long previous_changed;
};
struct histogram_t
{
	struct hdr_histogram* changing;
	struct hdr_histogram* accumulated;
	struct hdr_histogram* previous_changed;
	int64_t lowest_trackable_value;
	int64_t highest_trackable_value;
	int significant_figures;

};
struct display_manifest_t
{
	char* name;
	int is_invisible;
	int is_ratio;
	int not_send_to_server;
	int numerator_id;
	int denominator_id;
	int output_scaling;	//negative value: zoom in; positive value: zoom out;
	enum field_dsp_style_t style;
	enum field_calc_algo calc_type;
	union
	{
		struct stat_unit_t single;//for status and field
		struct stat_unit_t* line; //for line
		struct histogram_t histogram;
		int column_seq;	//for column
	};
};
struct display_manifest_t* display_manifest_new(const char* name, enum field_dsp_style_t style,enum field_calc_algo calc_type)
{
	struct display_manifest_t* p=(struct display_manifest_t*)calloc(sizeof(struct display_manifest_t),1);
	p->calc_type=calc_type;
	p->style=style;
	p->is_ratio=0;
	p->output_scaling=1;
	p->name=__str_dup(name);
	return p;
}
void display_manifest_free(struct display_manifest_t* p)
{
	free(p->name);
	p->name=NULL;
	free(p);
	return;
}


struct FS_space_t
{
	int stat_cycle;
	int screen_print_trigger;
	int print_mode;	//1:Rewrite ,2: Append
	int create_thread;
	int running;
	
	int line_cnt;
	int display_cnt;
	int single_cnt;//including line_cnt;
	int metris_format;
	int column_cnt;
	int histogram_cnt;
	
	int histogram_bin_num;
	double* histogram_bins;

	int cloumn_id[MAX_STAT_COLUMN_NUM];
	int display_size;
	int current_date;
	int flush_by_date;
	char str_ip[32];
	char app_name[16];

	int statsd_switch;
	unsigned int server_ip;
	unsigned short server_port;
	int statsd_socket;

	size_t snd_buf_off;
	char send_buff[UDP_PAYLOAD_SIZE];
	
	pthread_mutex_t reg_lock;
	struct display_manifest_t **display;

	char appoint_output_file[MAX_PATH_LEN];
	char current_output_file[MAX_PATH_LEN];
	FILE* fp;
	pthread_t cfg_mon_t;
	struct timespec last_display_time;
	const char* write_mode;
};

static int send_udp(int sd, unsigned int dest_ip, unsigned short dest_port, const char * data, int len)
{
	int to_send_len=len;
	int already_sended_len=0,this_sended_len=0;
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr =dest_ip;
	addr.sin_port = htons(dest_port);

	while(to_send_len>already_sended_len)
	{
		this_sended_len=sendto(sd,(void*)(data+already_sended_len),
								to_send_len-already_sended_len,
								0,
								(struct sockaddr *)&(addr),
								sizeof(addr));
		if(this_sended_len==-1)
		{
			if((EAGAIN == errno)||( EINTR == errno )|| (EWOULDBLOCK==errno))
			{
				continue;
			}
			else
			{
				printf("FS2: at send,socket error: %d %s", errno, strerror(errno));
				return -1;
			}
		}
		already_sended_len=+this_sended_len;			
	}
	
	return 0;
}
void flush_metric(struct FS_space_t* _handle)
{
	if(_handle->snd_buf_off==0)
	{
		return;
	}
	send_udp(_handle->statsd_socket, _handle->server_ip,(unsigned short) _handle->server_port,
				_handle->send_buff, _handle->snd_buf_off);
	_handle->snd_buf_off=0;
	memset(_handle->send_buff,0,sizeof(_handle->send_buff));
	return;
}
void append_statsd_counter(struct FS_space_t* _handle,const char* name, long long value)
{
	if(value==0)
	{
		return;
	}
	
	size_t max_output_len=strlen(name)+strlen(name)+20+6;
	if(UDP_PAYLOAD_SIZE-_handle->snd_buf_off<max_output_len)
	{
		flush_metric(_handle);
	}
	_handle->snd_buf_off+=snprintf(_handle->send_buff+_handle->snd_buf_off,UDP_PAYLOAD_SIZE-_handle->snd_buf_off,
				"[%s]%s:%lld|c\n",
				_handle->app_name,name,value);
	return;
}
void append_statsd_histogram(struct FS_space_t* _handle,const char* name, long long value, long long count)
{
	if(count==0)
	{
		return;
	}
	size_t max_output_len=strlen(name)+strlen(name)+20+6;
	if(UDP_PAYLOAD_SIZE-_handle->snd_buf_off<max_output_len)
	{
		flush_metric(_handle);
	}

	if(count==1)
	{
		_handle->snd_buf_off+=snprintf(_handle->send_buff+_handle->snd_buf_off,UDP_PAYLOAD_SIZE-_handle->snd_buf_off,
					"[%s]%s:%lld|h\n",
					_handle->app_name,name,value);
	}
	else
	{
		_handle->snd_buf_off+=snprintf(_handle->send_buff+_handle->snd_buf_off,UDP_PAYLOAD_SIZE-_handle->snd_buf_off,
					"[%s]%s:%lld|h|@%f\n",
					_handle->app_name,name,value, (double)1/count);
	}
	return;
}


void appen_influx_line(struct FS_space_t* _handle,const char* measurement, char *field_set)
{
	if(field_set==NULL)
	{
		return;
	}
	if(UDP_PAYLOAD_SIZE-(unsigned int)_handle->snd_buf_off<strlen(measurement)+strlen(field_set)+strlen(_handle->app_name)+12)
	{
		flush_metric(_handle);
	}
	_handle->snd_buf_off+=snprintf(_handle->send_buff+_handle->snd_buf_off,UDP_PAYLOAD_SIZE-_handle->snd_buf_off,
								   "%s,app_name=%s %s\n",
								   measurement, _handle->app_name,field_set);
	return;
}

int is_valid_fs_name(const char* name)
{
	const char* reserverd="|:\n\r. \t<>[]#!@";
	unsigned int i=0,j=0;
	for(i=0;i<strlen(name);i++)
	{
		for(j=0;j<strlen(reserverd);j++)
		if(name[i]==reserverd[j])
		{
			return 0;
		}
	}
	return 1;
}
int current_day(char* day_buff,int size)
{
	
	int current_day;
	int year;
	int month;
	int day;

	time_t t;
	struct tm *local_time;

	t = time(NULL);
	local_time=localtime(&t); 

	snprintf(day_buff, size,"%04d-%02d-%02d", 	(local_time->tm_year + 1900), (local_time->tm_mon + 1), local_time->tm_mday);

	year = local_time->tm_year + 1900;
	month = local_time->tm_mon + 1;
	day = local_time->tm_mday;
	current_day = year*10000 + month*100 + day;

	return current_day;
}
static int startup_udp()
{
	int sd_udp=-1;
	int flags;
 	if(-1==(sd_udp = socket(AF_INET, SOCK_DGRAM, 0)))
	{
		printf("FS2: socket error: %d %s, restart socket.", errno, strerror(errno));
		sd_udp=-1;
	}
	flags=fcntl(sd_udp,F_GETFL);
	flags|=O_NONBLOCK;
	if(fcntl(sd_udp,F_SETFL,flags)==-1)
	{
		printf("FS2: socket error: %d %s, restart socket.", errno, strerror(errno));
		sd_udp=-1;
	}
	int opt=1;
	if (setsockopt (sd_udp, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt) ) < 0) {
		printf("FS2:setsockopt error: %d %s, restart socket.", errno, strerror(errno));
		close (sd_udp);
		sd_udp=-1;
		return sd_udp;
	}

	return sd_udp;
}

void *fs2_thread_screen_print(void *arg);

screen_stat_handle_t __attribute__((visibility("default"))) FS_create_handle(void)
{
	struct FS_space_t* handle=(struct FS_space_t*)calloc(sizeof(struct FS_space_t),1);
	handle->screen_print_trigger=1;
	handle->print_mode=1;
	handle->write_mode="w";
	handle->stat_cycle=2;
	handle->create_thread=1;
	handle->display_size=INIT_STAT_FIELD_NUM;
	handle->running=0;
	handle->fp=stdout;
	strcpy(handle->app_name,"?");
	memset(handle->appoint_output_file,0,sizeof(handle->appoint_output_file));
	memset(handle->current_output_file,0,sizeof(handle->current_output_file));
	pthread_mutex_init(&(handle->reg_lock),NULL);
	handle->histogram_bin_num=sizeof(HISTOGRAM_DEFAULT_BINS)/sizeof(HISTOGRAM_DEFAULT_BINS[0]);
	handle->histogram_bins=(double*)calloc(sizeof(double), handle->histogram_bin_num);
	memcpy(handle->histogram_bins, HISTOGRAM_DEFAULT_BINS, sizeof(HISTOGRAM_DEFAULT_BINS));
	handle->display=(struct display_manifest_t **)calloc(sizeof(struct display_manifest_t *),handle->display_size);
	return handle;
}

int FS_set_para(screen_stat_handle_t handle, enum FS_option  type,const void* value,int size)
{
	struct FS_space_t* _handle=(struct FS_space_t*)handle;
	int int_val=0,ret=0;
	if(_handle->running==1 && type!=ID_INVISBLE)
	{
		return -1;
	}
	if(type!=OUTPUT_DEVICE)
	{
		int_val=*(const int*)value;
	}
	switch(type)
	{
		case	OUTPUT_DEVICE:
			assert((unsigned int)size<sizeof(_handle->appoint_output_file));
			memcpy(_handle->appoint_output_file,value,size);
			break;
		case	PRINT_MODE:
			if(size!=4||(int_val!=1&&int_val!=2))
			{
				return -1;
			}
			_handle->print_mode=int_val;
			if(_handle->print_mode==1)
			{
				_handle->write_mode="w";
			}
			else
			{
				_handle->write_mode="a+";
			}
			break;
		case	STAT_CYCLE:
			if(size!=4||(int_val==0))
			{
				return -1;
			}
			_handle->stat_cycle=int_val;
			break;
		case	PRINT_TRIGGER:
			if(size!=4||(int_val!=0&&int_val!=1))
			{
				return -1;
			}
			_handle->screen_print_trigger=int_val;
			break;
		case 	CREATE_THREAD:
			if(size!=4||(int_val!=0&&int_val!=1))
			{
				return -1;
			}
			_handle->create_thread=int_val;
			break;
		case	ID_INVISBLE:
			if(int_val<0||int_val>=_handle->display_cnt)
			{
				return -1;
			}
			_handle->display[int_val]->is_invisible=1;
			break;
		case	NOT_SEND_METRIC_TO_SERVER:
			if(int_val<0||int_val>=_handle->display_cnt)
			{
				return -1;
			}
			_handle->display[int_val]->not_send_to_server=1;
			break;
		case	FLUSH_BY_DATE:
			if(int_val==1)
			{
				_handle->flush_by_date=1;
			}	
			break;
		case	APP_NAME:
			if((unsigned int)size>sizeof(_handle->app_name)-1)
			{
				return -1;
			}
			strncpy(_handle->app_name,(char*)value, size);
			break;
		case	STATS_SERVER_IP:
			if((unsigned int)size>sizeof(_handle->str_ip)-1)
			{
				return -1;
			}
			strncpy(_handle->str_ip,(char*)value, size);
			ret=inet_pton(AF_INET, _handle->str_ip, &(_handle->server_ip));
			if(ret<0)
			{
				return -1;
			}
			_handle->statsd_switch=FS_OUTPUT_STATSD;
			break;
		case	STATS_SERVER_PORT:
			if((size_t)size==sizeof(unsigned short))
			{
				_handle->server_port=*((unsigned short *)value);
			}
			else if((size_t)size==sizeof(int))
			{
				_handle->server_port=*((int*)value);
			}
			else
			{
				return -1;
			}
			break;
		case	STATS_FORMAT:
			if(size!=4||(int_val!=FS_OUTPUT_STATSD&&int_val!=FS_OUTPUT_INFLUX_LINE))
			{
				return -1;
			}
			_handle->statsd_switch=int_val;
			break;
		case MAX_STAT_FIELD_NUM:
			if((size_t)size!=sizeof(int))
			{
				return -1;
			}
			_handle->display_size=*((int*)value);
			_handle->display=(struct display_manifest_t **)realloc(_handle->display,sizeof(struct display_manifest_t *)*_handle->display_size);
			break;
		case HISTOGRAM_GLOBAL_BINS:
			if(_handle->histogram_bins!=NULL)
			{
				free(_handle->histogram_bins);
				_handle->histogram_bins=NULL;
			}
			_handle->histogram_bin_num=parse_histogram_bin_format((const char*)value, &_handle->histogram_bins);
			assert(_handle->histogram_bin_num>0);
        case	METRIS_FORMAT:
            if(size!=4||(int_val!=FS_METRIS_OUTPUT_DEFAULT&&int_val!=FS_METRIS_OUTPUT_JSON))
            {
                return -1;
            }
            _handle->metris_format=int_val;
            break;
		default:
			return -1;
	}
	return 0;
}
void FS_start(screen_stat_handle_t handle)
{
	struct FS_space_t* _handle=(struct FS_space_t*)handle;
	int i=0,j=0;
	char date_buff[32]={0};

	struct display_manifest_t *p=NULL;
	for(i=0;i<_handle->display_cnt;i++)
	{
		p=_handle->display[i];
		switch(p->style)
		{
			case	FS_STYLE_FIELD:
			case	FS_STYLE_STATUS:
			case	FS_STYLE_HISTOGRAM:
				break;
			case	FS_STYLE_COLUMN:
				_handle->cloumn_id[j]=i;
				p->column_seq=j;
				j++;
				break;
			case	FS_STYLE_LINE:
				p->line=(struct stat_unit_t*)realloc(p->line,sizeof(struct stat_unit_t)*_handle->column_cnt);
				memset(p->line,0,sizeof(struct stat_unit_t)*_handle->column_cnt);
				break;
			default:
				assert(0);
				break;
		}		
	}
	assert(j==_handle->column_cnt);
	_handle->running=1;
	if(strlen(_handle->appoint_output_file)>0)//otherwise use stdout;
	{
		if(_handle->flush_by_date==1)
		{
			_handle->current_date=current_day(date_buff, sizeof(date_buff));
			snprintf(_handle->current_output_file,sizeof(_handle->current_output_file)
						,"%s.%s",_handle->appoint_output_file,date_buff);
		}
		else
		{
			snprintf(_handle->current_output_file,sizeof(_handle->current_output_file)
						,"%s",_handle->appoint_output_file);
		}
		
		_handle->fp=fopen(_handle->current_output_file,_handle->write_mode);
		if(_handle->fp==NULL)
		{
			printf("Field Stat: open %s failed.\n",_handle->appoint_output_file);
			assert(0);
			return;
		}
	}
	if(_handle->statsd_switch > 0)
	{
		_handle->statsd_socket=startup_udp();
		if(_handle->statsd_switch == FS_OUTPUT_STATSD)
		{
			append_statsd_counter(_handle, "RESTART", 1);
			flush_metric(_handle);
		}
	}
	clock_gettime(CLOCK_MONOTONIC,&(_handle->last_display_time));
	if(_handle->create_thread==1)
	{
		pthread_create(&(_handle->cfg_mon_t), NULL, fs2_thread_screen_print, (void*)handle);
	}
	return;
}
void FS_stop(screen_stat_handle_t* handle)
{
	int i=0;
	void * ret=NULL;
	struct FS_space_t* _handle=*(struct FS_space_t**)handle;
	if(_handle->create_thread==1 && _handle->cfg_mon_t!=0)
	{		
		_handle->create_thread=0;	
		pthread_join(_handle->cfg_mon_t, &ret);
	}
	struct display_manifest_t * p=NULL;
	for(i=0;i<_handle->display_cnt;i++)
	{
		p=_handle->display[i];
		switch (p->style)
		{
			case FS_STYLE_LINE:
				free(p->line);
				p->line=NULL;
				break;
			case FS_STYLE_HISTOGRAM:
				if(p->histogram.previous_changed!=NULL)
				{
					hdr_close(p->histogram.previous_changed);
					p->histogram.previous_changed=NULL;
				}
				if(p->histogram.changing!=NULL)
				{
					hdr_close(p->histogram.changing);
					p->histogram.changing=NULL;
				}
				if(p->histogram.accumulated!=NULL)
				{
					hdr_close(p->histogram.accumulated);
					p->histogram.accumulated=NULL;
				}	
			default:
				break;
		}
		free(p->name);
		p->name=NULL;
		free(p);
		_handle->display[i]=NULL;
	}
	if(strlen(_handle->appoint_output_file)>0)
	{
		fclose(_handle->fp);
		_handle->fp=NULL;
	}
	free(_handle->display);
	if(_handle->fp!=NULL)
	{
		fclose(_handle->fp);
		_handle->fp=NULL;
	}
	free(_handle->histogram_bins);
	_handle->histogram_bins=NULL;	
	free(_handle);

	*handle=NULL;
	return;
}
int FS_register(screen_stat_handle_t handle,enum field_dsp_style_t style,enum field_calc_algo calc_type,const char* name)
{
	struct FS_space_t* _handle=(struct FS_space_t*)handle;
	struct display_manifest_t * choosen=NULL;
	int id=0;
	if(!is_valid_fs_name(name))
	{
		return -1;
	}
	if(_handle->running==1&&style==FS_STYLE_COLUMN)
	{
		return -1;
	}
	pthread_mutex_lock(&(_handle->reg_lock));
	id=_handle->display_cnt;
	_handle->display_cnt++;
	assert(_handle->display_cnt<_handle->display_size);
	choosen=_handle->display[id]=display_manifest_new(name, style, calc_type);

	switch(style)
	{
		case	FS_STYLE_FIELD:
		case	FS_STYLE_STATUS:
			_handle->single_cnt++;
			memset(&(choosen->single), 0, sizeof(choosen->single));
			break;
		case	FS_STYLE_LINE:	
			choosen->line=(struct stat_unit_t*)calloc(sizeof(struct stat_unit_t), _handle->column_cnt);
			_handle->line_cnt++;
			break;
		case	FS_STYLE_COLUMN:
			_handle->column_cnt++;
			break;	
		case	FS_STYLE_HISTOGRAM:
		default:
			assert(0);
	}
	pthread_mutex_unlock(&(_handle->reg_lock));
	return id;
}
int FS_register_histogram(screen_stat_handle_t handle, enum field_calc_algo calc_type, const char* name,
		long long lowest_trackable_value,
        long long highest_trackable_value,
        int significant_figures)
{
	struct FS_space_t* _handle=(struct FS_space_t*)handle;
	struct display_manifest_t * choosen=NULL;
	int id=0, ret=0;
	if(!is_valid_fs_name(name))
	{
		return -1;
	}
    if (lowest_trackable_value < 1 ||
            significant_figures < 1 || 5 < significant_figures)
    {
        return -1;
    }
    else if (lowest_trackable_value * 2 > highest_trackable_value)
    {
        return -1;
    }

	pthread_mutex_lock(&(_handle->reg_lock));
	id=_handle->display_cnt;
	_handle->display_cnt++;
	assert(_handle->display_cnt<_handle->display_size);
	choosen=_handle->display[id]=display_manifest_new(name, FS_STYLE_HISTOGRAM, calc_type);
	choosen->histogram.highest_trackable_value=(int64_t)highest_trackable_value;
	choosen->histogram.lowest_trackable_value=(int64_t)lowest_trackable_value;
	choosen->histogram.significant_figures=significant_figures;
	
	ret=hdr_init((int64_t)lowest_trackable_value, (int64_t)highest_trackable_value, significant_figures, &(choosen->histogram.changing));
	assert(ret==0);	
	ret=hdr_init((int64_t)lowest_trackable_value, (int64_t)highest_trackable_value, significant_figures, &(choosen->histogram.accumulated));
	assert(ret==0);
	_handle->histogram_cnt++; 
	pthread_mutex_unlock(&(_handle->reg_lock));
	return id;
}


int FS_register_ratio(screen_stat_handle_t handle,int numerator_id,int denominator_id,int scaling,enum field_dsp_style_t style,enum field_calc_algo calc_type,const char* name)
{
	struct FS_space_t* _handle=(struct FS_space_t*)handle;
	struct display_manifest_t * choosen=NULL;
	int id=0;
	if(_handle->running==1&&(style==FS_STYLE_LINE||style==FS_STYLE_COLUMN))
	{
		return -1;
	}
	if(!is_valid_fs_name(name))
	{
		return -1;
	}
	if(numerator_id>=_handle->display_cnt||denominator_id>=_handle->display_cnt)
	{
		return -1;
	}
	if(_handle->display[denominator_id]->is_ratio==1||_handle->display[numerator_id]->is_ratio==1)
	{
		return -1;
	}
	if(_handle->display[denominator_id]->style==FS_STYLE_LINE||_handle->display[numerator_id]->style==FS_STYLE_LINE)
	{
		return -1;
	}
	if(style==FS_STYLE_LINE||scaling==0)
	{
		return -1;
	}
	pthread_mutex_lock(&(_handle->reg_lock));

	switch(style)
	{
		case	FS_STYLE_FIELD:
		case	FS_STYLE_STATUS:
			_handle->single_cnt++;
			break;
		case	FS_STYLE_LINE:
			_handle->line_cnt++;
			break;
		case	FS_STYLE_COLUMN:
			_handle->column_cnt++;
			break;
		default:			
			pthread_mutex_unlock(&(_handle->reg_lock));
			assert(0);
			return -1;
	}

	id=_handle->display_cnt;
	_handle->display_cnt++;
	assert(_handle->display_cnt<_handle->display_size);
	choosen=_handle->display[id]=display_manifest_new(name, style, calc_type);	
	choosen->is_ratio=1;
	choosen->output_scaling=scaling;
	choosen->denominator_id=denominator_id;
	choosen->numerator_id=numerator_id;
	pthread_mutex_unlock(&(_handle->reg_lock));

	return id;
}

int FS_operate(screen_stat_handle_t handle,int id,int column_id,enum field_op op,long long value)
{
	struct FS_space_t* _handle=(struct FS_space_t*)handle;
	int i=0;
	struct display_manifest_t* p=NULL;
	struct stat_unit_t* target=NULL;
	if(id>=_handle->display_cnt)
	{
		return -1;
	}
	p=_handle->display[id];
	switch(p->style)
	{
		case	FS_STYLE_LINE:
			if(column_id>=_handle->display_cnt||column_id>=_handle->display_cnt||_handle->display[column_id]->style!=FS_STYLE_COLUMN)
			{
				return -1;
			}
			i=_handle->display[column_id]->column_seq;
			target=&(p->line[i]);
			break;
		case	FS_STYLE_HISTOGRAM:
			hdr_record_value(p->histogram.changing, (int64_t) value);
			return 0;
		default:
			target=&(p->single);
			break;
	}
	switch(op)
	{
		case	FS_OP_ADD:
			atomic_add(&(target->changing), value);
			break;
		case	FS_OP_SET:
			atomic_set(&(target->changing), value-target->accumulated);
			break;
		case	FS_OP_SUB:
			atomic_sub(&(target->changing), value);
			break;
		default:
			assert(0);
			break;
	}
	return 0;
}
static long long get_stat_unit_val(display_manifest_t* p, int column_seq,enum field_calc_algo calc_type,int is_refer)
{
	stat_unit_t* target=NULL;
	long long value=0;
	switch(p->style)
	{
		case	FS_STYLE_FIELD:
		case	FS_STYLE_STATUS:
			target=&(p->single);
			break;
		case	FS_STYLE_LINE:
			target=&(p->line[column_seq]);	
			break;
		case	FS_STYLE_HISTOGRAM:
		case	FS_STYLE_COLUMN:
		default:
			break;
	}
	value= atomic_read(&(target->changing));
	if(is_refer==0)
	{
		target->previous_changed=value;
		target->accumulated+=value;
		atomic_set(&(target->changing), 0);
	}
	switch(calc_type)
	{
		case	FS_CALC_CURRENT:
			value=target->accumulated;
			break;
		case	FS_CALC_SPEED:
			value=target->previous_changed;
			break;
		default:
			assert(0);
	}

	return value;
}
static double get_stat_ratio(struct display_manifest_t* numerator, struct display_manifest_t* denominator, struct display_manifest_t* line, int scaling,enum field_calc_algo calc_type)
{
	long long value_n=0,value_d=0;
	double ratio=0.0;
	
	if(numerator->style==FS_STYLE_COLUMN)
	{
		assert(denominator->style==FS_STYLE_COLUMN);
		value_n=get_stat_unit_val(line, numerator->column_seq,calc_type,1);
		value_d=get_stat_unit_val(line, denominator->column_seq,calc_type,1);
	}
	else
	{
		value_n=get_stat_unit_val(numerator,0,calc_type,1);
		value_d=get_stat_unit_val(denominator,0,calc_type,1);
	}
	if(value_d==0)
	{
		ratio=0.0;
	}
	else
	{
		if(scaling>0)
		{
			ratio=((double)value_n*scaling/value_d);
		}
		else
		{
			ratio=((double)value_n/(value_d*scaling*-1));
		}
	}
	return ratio;
}

void influx_output(struct FS_space_t* _handle)
{
	display_manifest_t* p=NULL,*p_column=NULL;
	long long value=0;
	int i=0,j=0;
	char field_buff[UDP_PAYLOAD_SIZE];
	int buff_off = 0, not_zero_column_cnt = 0;

	memset(field_buff, 0, UDP_PAYLOAD_SIZE);

	for(i=0;i<_handle->display_cnt;i++)
	{
		p=_handle->display[i];
		if(p->not_send_to_server==1||p->is_ratio==1)
		{
			continue;
		}
		switch(p->style)
		{
			case FS_STYLE_STATUS:
				if(p->calc_type==FS_CALC_SPEED)
				{
					value=get_stat_unit_val(p, 0, FS_CALC_CURRENT, 1);
					if(value != 0)
					{
						snprintf(field_buff, UDP_PAYLOAD_SIZE, "%s=%lld", p->name, value);
						appen_influx_line(_handle, p->name, field_buff);
					}
					break;
				}
				//not break
			case FS_STYLE_FIELD:
				value=get_stat_unit_val(p, 0, FS_CALC_SPEED, 1);
				if(value != 0)
				{
					snprintf(field_buff, UDP_PAYLOAD_SIZE, "%s=%lld", p->name, value);
					appen_influx_line(_handle, p->name, field_buff);
				}
				break;
			case FS_STYLE_LINE:
				for(j=0;j<_handle->column_cnt;j++)
				{
					p_column=_handle->display[_handle->cloumn_id[j]];
					if(p_column->not_send_to_server==1||p_column->is_ratio==1)
					{
						continue;
					}
					value=get_stat_unit_val(p, p_column->column_seq, FS_CALC_SPEED, 1);
					buff_off+=snprintf(field_buff+buff_off, UDP_PAYLOAD_SIZE-buff_off,"%s=%lld,",p_column->name, value);
					if(value != 0)
					{
						not_zero_column_cnt += 1;
					}
					if(buff_off >= UDP_PAYLOAD_SIZE)continue;
				}
				if(not_zero_column_cnt > 0)
				{
					field_buff[buff_off-1] = '\0';
					appen_influx_line(_handle, p->name, field_buff);
				}
				buff_off=0;
				not_zero_column_cnt = 0;
				break;
			case FS_STYLE_HISTOGRAM:
				//
				break;
			default:
				break;
		}

	}
	flush_metric(_handle);
}

void StatsD_output(struct FS_space_t* _handle)
{
	display_manifest_t* p=NULL,*p_column=NULL;
	long long value=0;
	int i=0,j=0;
	char name_buff[UDP_PAYLOAD_SIZE];
	struct hdr_iter iter;
	int index=0;

	for(i=0;i<_handle->display_cnt;i++)
	{
		p=_handle->display[i];
		if(p->not_send_to_server==1||p->is_ratio==1)
		{
			continue;
		}
		switch(p->style)
		{
			case FS_STYLE_STATUS:
				if(p->calc_type==FS_CALC_CURRENT)
				{
					value=get_stat_unit_val(p, 0, FS_CALC_CURRENT, 1);
					append_statsd_counter(_handle, p->name, value);
					break;
				}
				//not break;
			case FS_STYLE_FIELD:
				value=get_stat_unit_val(p, 0, FS_CALC_SPEED, 1);
				append_statsd_counter(_handle, p->name, value);
				break;
			case FS_STYLE_LINE:
				for(j=0;j<_handle->column_cnt;j++)
				{
					p_column=_handle->display[_handle->cloumn_id[j]];
					if(p_column->not_send_to_server==1||p_column->is_ratio==1)
					{
						continue;
					}
					snprintf(name_buff,sizeof(name_buff),"%s#%s",p->name,p_column->name);
					value=get_stat_unit_val(p, p_column->column_seq, FS_CALC_SPEED, 1);
					append_statsd_counter(_handle, name_buff, value); 
				}
				break;
			case FS_STYLE_HISTOGRAM:
				// Raw Histogram
				if(p->histogram.previous_changed!=NULL)
				{
					hdr_iter_recorded_init(&iter, p->histogram.previous_changed);
					while (hdr_iter_next(&iter))
					{
						append_statsd_histogram(_handle, p->name, (long long)iter.value, (long long)iter.count);
						index++;
					}
				}
				break;
			default:
				break;
		}

	}
	flush_metric(_handle);
}

static int output_style_status(struct FS_space_t* _handle,long long interval_ms,char*print_buf, unsigned int size)
{
	int i=0,j=0;
	display_manifest_t* p=NULL;
	long long value=0;
	double ratio=0.0;
	char* pos=print_buf;

	for(i=0;i<_handle->display_cnt;i++)
	{
		p=_handle->display[i];
		if(p->style!=FS_STYLE_STATUS)
		{
			continue;
		}
		if(p->is_invisible==1)
		{
			value=get_stat_unit_val(p, 0, p->calc_type, 0);
			continue;
		}
		if(p->is_ratio==1)
		{
			ratio=get_stat_ratio(_handle->display[p->numerator_id],  _handle->display[p->denominator_id], NULL,
								p->output_scaling, p->calc_type);
			pos+=snprintf(pos,size-(pos-print_buf),"%s: %10.2e\t",p->name,ratio);
		}
		else
		{
			value=get_stat_unit_val(p, 0, p->calc_type, 0);
			if(p->calc_type==FS_CALC_SPEED)
			{
				value=value*p->output_scaling*1000/interval_ms;
			}
			pos+=snprintf(pos,size-(pos-print_buf),"%s: %-10lld\t",p->name,value);
		}
		j++;
		if(j==STATUS_PER_LINE)
		{
			pos+=snprintf(pos,sizeof(print_buf)-(pos-print_buf),"\n");
			j=0;
		}
	}
	if(pos-print_buf>0)
	{
		if(*(pos-1)=='\n')
		{
			pos--;
		}
		pos+=snprintf(pos,sizeof(print_buf)-(pos-print_buf),"\n%s\n",draw_line);
	}
	return pos-print_buf;
}
static int output_style_field(struct FS_space_t* _handle,long long interval_ms,char*print_buf, unsigned int size)
{
	int i=0,j=0;
	display_manifest_t* p=NULL;
	long long value=0;
	double ratio=0.0;
	char* pos=print_buf;
	int field_id[INIT_STAT_FIELD_NUM]={0};
	int field_cnt=0;
	
	for(i=0;i<_handle->display_cnt;i++)
	{
		p=_handle->display[i];
		if(p->style!=FS_STYLE_FIELD)
		{
			continue;
		}
		if(p->is_invisible==1)
		{
			//for update last_output
			get_stat_unit_val(p, 0,FS_CALC_CURRENT, 0);
			continue;
		}
		field_id[field_cnt]=i;
		field_cnt++;
	}
	for(i=0;i<field_cnt;i++)
	{
		pos+=snprintf(pos,size-(pos-print_buf),"\t");
		for(j=0;j<FIELD_PER_LINE&&i+j<field_cnt;j++)
		{
			p=_handle->display[field_id[i+j]];
			pos+=snprintf(pos,size-(pos-print_buf),"%10s\t",p->name);
		}
		pos+=snprintf(pos,size-(pos-print_buf),"\nsum\t");
		for(j=0;j<FIELD_PER_LINE&&i+j<field_cnt;j++)
		{
			p=_handle->display[field_id[i+j]];
			if(p->is_ratio==1)
			{
				ratio=get_stat_ratio(_handle->display[p->numerator_id], _handle->display[p->denominator_id], NULL,
									p->output_scaling, FS_CALC_CURRENT);
				pos+=snprintf(pos,size-(pos-print_buf),"%f\t",ratio);
			}
			else
			{
				value=get_stat_unit_val(p, 0,FS_CALC_CURRENT, 1);
				pos+=snprintf(pos,sizeof(print_buf)-(pos-print_buf),"%10lld\t",value);
			}
		}
		pos+=snprintf(pos,sizeof(print_buf)-(pos-print_buf),"\nspeed/s\t");
		for(j=0;j<FIELD_PER_LINE&&i+j<field_cnt;j++)
		{
			p=_handle->display[field_id[i+j]];
			if(p->is_ratio==1)
			{
				ratio=get_stat_ratio(_handle->display[p->numerator_id],_handle->display[p->denominator_id],NULL,
									p->output_scaling, FS_CALC_SPEED);
				pos+=snprintf(pos,size-(pos-print_buf),"%10.2e\t",ratio);
			}
			else
			{
				value=get_stat_unit_val(p,0,FS_CALC_SPEED, 0);
				pos+=snprintf(pos,size-(pos-print_buf),"%10lld\t",value*1000/interval_ms);
			}
		}
		i+=(j-1);
		pos+=snprintf(pos,size-(pos-print_buf),"\n");
	}
	if(pos-print_buf>0)
	{
		if(*(pos-1)=='\n')
		{
			pos--;
		}
		pos+=snprintf(pos,size-(pos-print_buf),"\n%s\n",draw_line);
	}
	return pos-print_buf;
}
static int output_style_table(struct FS_space_t* _handle,long long interval_ms,char*print_buf, unsigned int size)
{
	int i=0,j=0;
	char* pos=print_buf;
	display_manifest_t* p_column=NULL,*p_line=NULL;
	double ratio=0.0,speed=0.0;
	long long value=0;
	if(_handle->column_cnt==0)
	{
		return 0;
	}
	for(i=0;i<_handle->column_cnt;i++)
	{
		if(i==0)
		{
			pos+=snprintf(pos,size-(pos-print_buf),"\t\t");
		}
		p_column=_handle->display[_handle->cloumn_id[i]];
		if(p_column->is_invisible==1)
		{
			continue;
		}
		pos+=snprintf(pos,size-(pos-print_buf),"\t%10s",p_column->name);
	}
	pos+=snprintf(pos,sizeof(print_buf)-(pos-print_buf),"\n");
	for(i=0;i<_handle->display_cnt;i++)
	{
		p_line=_handle->display[i];
		if(p_line->style!=FS_STYLE_LINE)
		{
			continue;
		}
		pos+=snprintf(pos,size-(pos-print_buf),"%-20s\t",p_line->name);
		for(j=0;j<_handle->column_cnt;j++)
		{
			p_column=_handle->display[_handle->cloumn_id[j]];
			if(p_column->is_invisible==1)
			{
				//for flush last_output_value
				get_stat_unit_val(p_line, p_column->column_seq, p_column->calc_type, 0);
				continue;
			}
			if(p_column->is_ratio==1)
			{
				ratio=get_stat_ratio(_handle->display[p_column->numerator_id],  _handle->display[p_column->denominator_id], p_line,
								p_column->output_scaling, p_column->calc_type);
				pos+=snprintf(pos,size-(pos-print_buf),"%10.2e\t",ratio);
			}
			else
			{
				value=get_stat_unit_val(p_line,p_column->column_seq, p_column->calc_type, 0);
				if(p_column->calc_type==FS_CALC_SPEED)
				{
					speed=(double)value*1000/interval_ms;
					pos+=snprintf(pos,size-(pos-print_buf),"%10.2e\t",speed);

				}
				else
				{
					pos+=snprintf(pos,size-(pos-print_buf),"%10lld\t",value);
				}
			}
		}
		pos+=snprintf(pos,size-(pos-print_buf),"\n");

	}
	if(pos-print_buf>0)
	{
		if(*(pos-1)=='\n')
		{
			pos--;
		}
		pos+=snprintf(pos,size-(pos-print_buf),"\n%s\n",draw_line);
	}
	return pos-print_buf;
}
#define HISTOGRAM_WIDTH	10
static int print_histogram_head(double * bins, int bin_num, char*print_buf, size_t size)
{
	char* pos=print_buf;
	char bin_format[256], str_format[256];
	const char* extra[]={"MAX", "MIN", "AVG", "STDDEV", "CNT"};
	char buff[32];
	int i=0;
	snprintf(bin_format, sizeof(bin_format), "%%%d.2lf%%%%", HISTOGRAM_WIDTH-1);
	snprintf(str_format, sizeof(str_format), "%%%ds", HISTOGRAM_WIDTH);
	pos+=snprintf(pos,size-(pos-print_buf),"%-8s\t","histogram");
	for(i=0;i<bin_num;i++)
	{
		snprintf(buff,sizeof(buff),bin_format,bins[i]);
		pos+=snprintf(pos,size-(pos-print_buf),str_format, buff);
	}
	for(i=0;(unsigned int)i<sizeof(extra)/sizeof(extra[0]);i++)
	{
		pos+=snprintf(pos,size-(pos-print_buf),str_format, extra[i]);
	}
	pos+=snprintf(pos,size-(pos-print_buf),"\n");
	return pos-print_buf;
}
static int print_histogram_unit(display_manifest_t* p, double * bins, int bin_num, char*print_buf, size_t size)
{
	char* pos=print_buf;
	long long value=0;
	int i=0;
	struct histogram_t* h=&(p->histogram);
	struct hdr_histogram* h_out=NULL, *h_tmp=NULL;
	char int_format[256], double_format[256];
	snprintf(int_format, sizeof(int_format), "%%%dlld",HISTOGRAM_WIDTH);
	snprintf(double_format, sizeof(double_format), "%%%d.2lf",HISTOGRAM_WIDTH);

	hdr_init(h->lowest_trackable_value,  h->highest_trackable_value, h->significant_figures, &(h_tmp));
	if(h->previous_changed!=NULL)	hdr_close(h->previous_changed);

	h->previous_changed=atomic_read(&(h->changing));
	h_tmp=atomic_set(&(h->changing), h_tmp);// left h_tmp is used to avoid warining [-Wunused-value]

	hdr_add(h->accumulated, h->previous_changed);
	if(p->calc_type==FS_CALC_SPEED)
	{
		h_out=h->previous_changed;
	}
	else
	{
		h_out=h->accumulated;
	}
	pos+=snprintf(pos,size-(pos-print_buf),"%-10s\t",p->name);

	for(i=0;i<bin_num;i++)
	{
		value=(long long)hdr_value_at_percentile(h_out, bins[i]);
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
static int output_style_histogram(struct FS_space_t* _handle, long long interval_ms, char*print_buf, size_t size)
{
	int i=0;
	char* pos=print_buf;
	display_manifest_t* p=NULL;
	if(_handle->histogram_cnt==0)
	{
		return 0;
	}
	pos+=print_histogram_head(_handle->histogram_bins, _handle->histogram_bin_num, pos, size-(pos-print_buf));

	for(i=0;i<_handle->display_cnt;i++)
	{
		p=_handle->display[i];
		if(p->style!=FS_STYLE_HISTOGRAM)
		{
			continue;
		}
		pos+=print_histogram_unit(p, _handle->histogram_bins, _handle->histogram_bin_num, pos, size-(pos-print_buf));

	}
	if(pos-print_buf>0)
	{
		if(*(pos-1)=='\n')
		{
			pos--;
		}
		pos+=snprintf(pos,size-(pos-print_buf),"\n%s\n",draw_line);
	}
	return pos-print_buf;	
}

cJSON *fs2_metrics_to_json(FS_space_t* _handle, long long timestamp)
{
    if(_handle == NULL)
    {
        return NULL;
    }
    display_manifest_t* p=NULL, *p_column=NULL;;
    long long value=0;
    cJSON *root_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(root_obj, "unix timestamp", timestamp);
    cJSON_AddNumberToObject(root_obj, "output interval", _handle->stat_cycle);
    cJSON *metrics_array_obj = cJSON_CreateArray();
    cJSON *tmp_obj = NULL;
    char tmp_output_name[64];
    const char* extra[]={"MAX", "MIN", "AVG", "STDDEV", "CNT"};
    long long tmp_histogram_values[sizeof(extra)/sizeof(char*)];
    for(int i=0;i<_handle->display_cnt;i++)
    {
        p=_handle->display[i];
        if(p->not_send_to_server==1||p->is_ratio==1)
        {
            continue;
        }
        switch(p->style)
        {
            case FS_STYLE_STATUS:
                if(p->calc_type==FS_CALC_SPEED)
                {
                    //value=get_stat_unit_val(p, 0, FS_CALC_CURRENT, 1);
                    tmp_obj = cJSON_CreateObject();
                    cJSON_AddStringToObject(tmp_obj, "name", p->name);
                    cJSON_AddStringToObject(tmp_obj, "type", "single");
                    cJSON_AddNumberToObject(tmp_obj, "acc", p->single.accumulated);
                    cJSON_AddNumberToObject(tmp_obj, "diff", p->single.changing);
                    cJSON_AddItemToArray(metrics_array_obj, tmp_obj);
                    //cJSON_Delete(tmp_obj);
                    break;
                }
                //not break
            case FS_STYLE_FIELD:
                //value=get_stat_unit_val(p, 0, FS_CALC_SPEED, 1);
                tmp_obj = cJSON_CreateObject();
                cJSON_AddStringToObject(tmp_obj, "name", p->name);
                cJSON_AddStringToObject(tmp_obj, "type", "single");
                cJSON_AddNumberToObject(tmp_obj, "acc", p->single.accumulated);
                cJSON_AddNumberToObject(tmp_obj, "diff", p->single.changing);
                cJSON_AddItemToArray(metrics_array_obj, tmp_obj);
                //cJSON_Delete(tmp_obj);
                break;
            case FS_STYLE_LINE:
                for(int j=0;j<_handle->column_cnt;j++)
                {
                    p_column=_handle->display[_handle->cloumn_id[j]];
                    if(p_column->not_send_to_server==1||p_column->is_ratio==1)
                    {
                        continue;
                    }
                    //value=get_stat_unit_val(p, p_column->column_seq, FS_CALC_SPEED, 1);
                    tmp_obj = cJSON_CreateObject();
                    snprintf(tmp_output_name, sizeof(tmp_output_name), "%s.%s", p->name, p_column->name);
                    cJSON_AddStringToObject(tmp_obj, "name", tmp_output_name);
                    //cJSON_AddStringToObject(tmp_obj, "type", "line");
                    cJSON_AddNumberToObject(tmp_obj, "acc", p_column->single.accumulated);
                    cJSON_AddNumberToObject(tmp_obj, "diff", p_column->single.changing);
                    cJSON_AddItemToArray(metrics_array_obj, tmp_obj);
                    //cJSON_Delete(tmp_obj);
                }
                break;
            case FS_STYLE_HISTOGRAM:
                {
                    struct histogram_t* h=&(p->histogram);
                    struct hdr_histogram* h_out=NULL, *h_tmp=NULL;
                    hdr_init(h->lowest_trackable_value,  h->highest_trackable_value, h->significant_figures, &(h_tmp));
                    if(h->previous_changed!=NULL)	hdr_close(h->previous_changed);

                    h->previous_changed=atomic_read(&(h->changing));
                    h_tmp=atomic_set(&(h->changing), h_tmp);// left h_tmp is used to avoid warining [-Wunused-value]

                    hdr_add(h->accumulated, h->previous_changed);
                    if(p->calc_type==FS_CALC_SPEED)
                    {
                        h_out=h->previous_changed;
                    }
                    else
                    {
                        h_out=h->accumulated;
                    }

                    for(int j=0;j<_handle->histogram_bin_num;j++)
                    {
                        value=(long long)hdr_value_at_percentile(h_out, _handle->histogram_bins[j]);
                        tmp_obj = cJSON_CreateObject();
                        snprintf(tmp_output_name, sizeof(tmp_output_name), "%s.p%.2f", p->name,
                                _handle->histogram_bins[j]);
                        cJSON_AddStringToObject(tmp_obj, "name", tmp_output_name);
                        cJSON_AddNumberToObject(tmp_obj, "acc", value);
                        cJSON_AddNumberToObject(tmp_obj, "diff", value);
                        cJSON_AddItemToArray(metrics_array_obj, tmp_obj);
                    }
                    tmp_histogram_values[0] = h_out->total_count==0?0:(long long)hdr_max(h_out);
                    tmp_histogram_values[1] = h_out->total_count==0?0:(long long)hdr_min(h_out);
                    tmp_histogram_values[2] = h_out->total_count==0?0:hdr_mean(h_out);
                    tmp_histogram_values[3] = h_out->total_count==0?0:hdr_stddev(h_out);
                    tmp_histogram_values[4] = (long long)h_out->total_count;
                    for(unsigned int j = 0; j < sizeof(extra)/sizeof(char*); j++)
                    {
                        tmp_obj = cJSON_CreateObject();
                        snprintf(tmp_output_name, sizeof(tmp_output_name), "%s.%s", p->name, extra[j]);
                        cJSON_AddStringToObject(tmp_obj, "name", tmp_output_name);
                        cJSON_AddNumberToObject(tmp_obj, "acc", tmp_histogram_values[j]);
                        cJSON_AddNumberToObject(tmp_obj, "diff", tmp_histogram_values[j]);
                        cJSON_AddItemToArray(metrics_array_obj, tmp_obj);
                    }
                }
                break;
            default:
                break;
        }

    }
    cJSON_AddItemToObject(root_obj, "metrics", metrics_array_obj);
    //cJSON_Delete(metrics_array_obj);
    return root_obj;
}

void FS_passive_output(screen_stat_handle_t handle)
{
	struct FS_space_t* _handle=(struct FS_space_t*)handle;
	struct timespec now;
	time_t current=0;
	char date_buff[32];
	//he reentrant version ctime_r() does the same, but stores the string in a user-supplied buffer which should have room for at least 26 bytes.
	char ctime_buff[32]={0};
	char* print_buf=NULL;
	size_t print_buf_sz=_handle->display_cnt*1024;
	char*pos=NULL;
	int today=0;
	time(&current);
	if(_handle->running==0)
	{
		return;
	}
	if(_handle->screen_print_trigger==0)
	{
		return;
	}
	if(strlen(_handle->appoint_output_file)>0&&_handle->flush_by_date==1)
	{
		today=current_day(date_buff, sizeof(date_buff));
		if(today!=_handle->current_date)
		{
			snprintf(_handle->current_output_file,sizeof(_handle->current_output_file)
						,"%s.%s",_handle->appoint_output_file,date_buff);
			_handle->current_date=today;
			fclose(_handle->fp);
			_handle->fp=fopen(_handle->current_output_file,_handle->write_mode);
		}
	}
	if(_handle->fp==NULL)
	{
		printf("Field Stat: open %s failed.\n",_handle->appoint_output_file);
		return;
	}
	clock_gettime(CLOCK_MONOTONIC,&now);
	long long interval_ms=(now.tv_sec-_handle->last_display_time.tv_sec)*1000+(now.tv_nsec-_handle->last_display_time.tv_nsec)/1000000;
	if(interval_ms<1)
	{
		return;
	}
	if(_handle->metris_format == FS_METRIS_OUTPUT_DEFAULT)
    {
        print_buf=(char*)calloc(sizeof(char), print_buf_sz);
        pos=print_buf;
        ctime_r(&current, ctime_buff);
        pos+=snprintf(pos, print_buf_sz-(pos-print_buf), "%s%s", draw_boundary, ctime_buff);
        pos--;//jump '\n' generate by ctime()
        pos+=snprintf(pos, print_buf_sz-(pos-print_buf),"%s\n",draw_boundary);

        pthread_mutex_lock(&(_handle->reg_lock));
        pos+=output_style_status(_handle, interval_ms, pos, print_buf_sz-(pos-print_buf));
        pos+=output_style_field(_handle, interval_ms, pos, print_buf_sz-(pos-print_buf));
        pos+=output_style_table(_handle, interval_ms, pos, print_buf_sz-(pos-print_buf));
        pos+=output_style_histogram(_handle, interval_ms, pos, print_buf_sz-(pos-print_buf));
        pthread_mutex_unlock(&(_handle->reg_lock));
    }
    if(_handle->metris_format == FS_METRIS_OUTPUT_JSON)
    {
        pthread_mutex_lock(&(_handle->reg_lock));
        cJSON *output_obj = fs2_metrics_to_json(_handle, now.tv_sec);
        pthread_mutex_unlock(&(_handle->reg_lock));
        if(output_obj != NULL)
        {
            print_buf = cJSON_PrintUnformatted(output_obj);
            cJSON_Delete(output_obj);
            pos = print_buf+strlen(print_buf)+1;
        }
    }

	if(_handle->print_mode==1)
	{
		fseek(_handle->fp,0,SEEK_SET);
	}
	fwrite(print_buf,pos-print_buf,1,_handle->fp);
	fflush(_handle->fp);
	memcpy(&(_handle->last_display_time),&now,sizeof(now));
	if(_handle->statsd_switch > 0)
	{
		pthread_mutex_lock(&(_handle->reg_lock));
		if(_handle->statsd_switch == FS_OUTPUT_STATSD)
		{
			StatsD_output(_handle);
		}
		if(_handle->statsd_switch == FS_OUTPUT_INFLUX_LINE)
		{
			influx_output(_handle);
		}
		pthread_mutex_unlock(&(_handle->reg_lock));
	}
	free(print_buf);
	print_buf=NULL;
}

void *fs2_thread_screen_print(void *arg)
{
	struct FS_space_t* handle=(struct FS_space_t*)arg;
	while(handle->create_thread)
	{
		FS_passive_output(handle);
		sleep(handle->stat_cycle);
	}
	return NULL;
}


