#include "field_stat2.h"

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

#define	HISTOGRAM_DEFAULT_BINS		"10,100,1000,10000"

#define	HISOTGRAM_EXTRA_INF			0
#define	HISTOGRAM_EXTRA_SUM			1
#define	HISTOGRAM_EXTRA_MAXVAL		2
#define	HISTOGRAM_EXTRA_SIZE		3

int FIELD_STAT_VERSION_1_0_20180712=0;

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
const char* draw_line="________________________________________________________________________________________________________________________________________________";
const char* draw_boundary="============================================================";
struct stat_field_t
{
	char	*filed_name;
	long long current_value;
	long long last_output_value;
};
struct stat_unit_t
{
	long long current_value;
	long long last_output_value;
	long long last_diff;
};

struct display_manifest_t
{
	char* name;
	int is_invisible;
	int is_ratio;
	int numerator_id;
	int denominator_id;
	int output_scaling;	//negative value: zoom in; positive value: zoom out;
	int histogram_use_default_bins;
	int histogram_bin_num;
	long long *histogram_bins;
	enum field_dsp_style_t style;
	enum field_calc_algo calc_type;
	union
	{
		struct stat_unit_t stat_unit;//for status and field
		struct stat_unit_t* stat_array; //for line and histogram
		int column_seq;	//for column
	};
};


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
	int matrix_base;
	int column_cnt;
	int field_cnt;
	int histogram_cnt;
	int histogram_default_cnt;
	int histogram_default_bin_num;
	long long *historgram_default_bins;
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

	int snd_buf_off;
	char send_buff[UDP_PAYLOAD_SIZE];
	
	pthread_mutex_t reg_lock;
	struct display_manifest_t **display;

	char appoint_output_file[MAX_PATH_LEN];
	char current_output_file[MAX_PATH_LEN];
	FILE* fp;
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
void append_metric(struct FS_space_t* _handle,const char* name, long long value)
{
	if(value==0)
	{
		return;
	}
	if(UDP_PAYLOAD_SIZE-(unsigned int)_handle->snd_buf_off<strlen(name)+24)
	{
		flush_metric(_handle);
	}
	_handle->snd_buf_off+=snprintf(_handle->send_buff+_handle->snd_buf_off,UDP_PAYLOAD_SIZE-_handle->snd_buf_off,
				"[%s]%s:%lld|c\n",
				_handle->app_name,name,value);
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

screen_stat_handle_t FS_create_handle(void)
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
	handle->display=(struct display_manifest_t **)calloc(sizeof(struct display_manifest_t *),handle->display_size);
	return handle;
}
//histogram bins format example:  "10,100,1000,2000" will return 4 bins which indicate 5 interval.
static int parse_histogram_bin_format(const char* format, long long **output_bins)
{
	char *token=NULL,*sub_token=NULL,*saveptr;
	size_t i=0;
	int comma_num=0,ret=0;
	long long *bins;
	char* dup_format=(char*)malloc(sizeof(char)*strlen(format));
	strncpy(dup_format,format,strlen(format));
	for(i=0;i<strlen(dup_format);i++)
	{
		if(dup_format[i]==',')
		{
			comma_num++;
		}
	}
	bins=(long long*)calloc(sizeof(long long),comma_num+1);
	for (token = dup_format,i=0; ; token= NULL, i++)
	{
		sub_token= strtok_r(token,",", &saveptr);
		if (sub_token == NULL)
			break;
		ret=sscanf(sub_token,"%lld",bins+i);
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
int FS_set_para(screen_stat_handle_t handle, enum FS_option  type,const void* value,int size)
{
	struct FS_space_t* _handle=(struct FS_space_t*)handle;
	int int_val=0,ret=0;
	if(_handle->running==1)
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
			_handle->statsd_switch=1;
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
			if(size!=sizeof(unsigned short))
			{
				return -1;
			}
			_handle->server_port=*((unsigned short *)value);
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
			_handle->histogram_default_bin_num=parse_histogram_bin_format((const char*)value, &_handle->historgram_default_bins);
			if(_handle->histogram_default_bin_num<0)
			{
				return -1;
			}
		default:
			return -1;
	}
	return 0;
}
void FS_start(screen_stat_handle_t handle)
{
	struct FS_space_t* _handle=(struct FS_space_t*)handle;
	pthread_t cfg_mon_t;
	int i=0,j=0;
	char date_buff[32]={0};
	if(_handle->histogram_default_bin_num==0)
	{
		_handle->histogram_default_bin_num=parse_histogram_bin_format(HISTOGRAM_DEFAULT_BINS, &_handle->historgram_default_bins);
	}
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
				p->stat_array=(struct stat_unit_t*)realloc(p->stat_array,sizeof(struct stat_unit_t)*_handle->column_cnt);
				memset(p->stat_array,0,sizeof(struct stat_unit_t)*_handle->column_cnt);
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
	if(_handle->statsd_switch==1)
	{
		_handle->statsd_socket=startup_udp();
		append_metric(_handle, "RESTART", 1);
		flush_metric(_handle);
	}
	clock_gettime(CLOCK_MONOTONIC,&(_handle->last_display_time));
	if(_handle->create_thread==1)
	{
		pthread_create(&cfg_mon_t, NULL, fs2_thread_screen_print, (void*)handle);
	}
	return;
}
void FS_stop(screen_stat_handle_t* handle)
{
	int i=0;
	struct FS_space_t* _handle=*(struct FS_space_t**)handle;
	_handle->create_thread=0;
	struct display_manifest_t * p=NULL;
	for(i=0;i<_handle->display_cnt;i++)
	{
		p=_handle->display[i];
		switch (p->style)
		{
			case FS_STYLE_LINE:
			case FS_STYLE_HISTOGRAM:
				free(p->stat_array);
				p->stat_array=NULL;
				break;
			default:
				break;
		}
		if(p->style==FS_STYLE_HISTOGRAM&&p->histogram_use_default_bins==0)
		{
			free(p->histogram_bins);
			p->histogram_bins=NULL;
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
	free(_handle->historgram_default_bins);
	_handle->historgram_default_bins=NULL;
	free(_handle->display);
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
		case	FS_STYLE_HISTOGRAM:
			_handle->histogram_cnt++;
			break;
		default:
			return -1;
	}
	pthread_mutex_lock(&(_handle->reg_lock));
	id=_handle->display_cnt;
	_handle->display_cnt++;
	assert(_handle->display_cnt<_handle->display_size);
	_handle->display[id]=(struct display_manifest_t*)calloc(sizeof(struct display_manifest_t),1);

	choosen=_handle->display[id];
	choosen->calc_type=calc_type;
	choosen->style=style;
	choosen->is_ratio=0;
	choosen->output_scaling=1;
	choosen->name=(char*)calloc(sizeof(char),strlen(name)+1);
	memcpy(choosen->name,name,strlen(name));
	if(choosen->style==FS_STYLE_LINE)
	{
		choosen->stat_array=(struct stat_unit_t*)calloc(sizeof(struct stat_unit_t),_handle->column_cnt);
	}
	else if(choosen->style==FS_STYLE_HISTOGRAM)
	{
		choosen->histogram_use_default_bins=1;
		choosen->histogram_bin_num=_handle->histogram_default_bin_num;
		choosen->stat_array=(struct stat_unit_t*)calloc(sizeof(struct stat_unit_t),choosen->histogram_bin_num+HISTOGRAM_EXTRA_SIZE);//for +inf and sum
		_handle->histogram_default_cnt++;
	}
	else
	{
		memset(&(choosen->stat_unit),0,sizeof(choosen->stat_unit));
	}
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
	if(style==FS_STYLE_LINE)
	{
		return -1;
	}
	if(scaling==0)
	{
		return -1;
	}
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
			return -1;
	}
	id=_handle->display_cnt;
	_handle->display_cnt++;
	assert(_handle->display_cnt<_handle->display_size);

	_handle->display[id]=(struct display_manifest_t*)calloc(sizeof(struct display_manifest_t),1);
	choosen=_handle->display[id];
	choosen->calc_type=calc_type;
	choosen->style=style;
	choosen->is_ratio=1;
	choosen->output_scaling=scaling;
	choosen->denominator_id=denominator_id;
	choosen->numerator_id=numerator_id;
	choosen->name=(char*)calloc(sizeof(char),strlen(name)+1);
	memcpy(choosen->name,name,strlen(name));
	return id;
}
int FS_histogram_set_bins(screen_stat_handle_t handle, int id, const char* bins)
{
	struct FS_space_t* _handle=(struct FS_space_t*)handle;
	struct display_manifest_t* p=NULL;
	int ret=0;
	if(id>=_handle->display_cnt)
	{
		return -1;
	}
	p=_handle->display[id];
	if(p->style!=FS_STYLE_HISTOGRAM)
	{
		return -1;
	}
	pthread_mutex_lock(&(_handle->reg_lock));
	p->histogram_bin_num=parse_histogram_bin_format(bins, &p->histogram_bins);
	if(p->histogram_bin_num>0)
	{	
		p->histogram_use_default_bins=0;
		free(p->stat_array);
		p->stat_array=(struct stat_unit_t*)calloc(sizeof(struct stat_unit_t),p->histogram_bin_num+HISTOGRAM_EXTRA_SIZE);
		_handle->histogram_default_cnt--;
		ret=0;
	}
	else
	{
		ret=-1;
	}
	pthread_mutex_unlock(&(_handle->reg_lock));
	return ret;
}
static int find_histogram_bin(long long* bins, int bin_num, long long val)
{
	int i=0;
	for(i=0;i<bin_num;i++)
	{
		if(val<bins[i])
		{
			break;
		}
	}
	return i;
}
int FS_operate(screen_stat_handle_t handle,int id,int column_id,enum field_op op,long long value)
{
	struct FS_space_t* _handle=(struct FS_space_t*)handle;
	int i=0;
	struct display_manifest_t* p=NULL;
	struct stat_unit_t* target=NULL, *bin_sum=NULL,*max=NULL;
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
			target=&(p->stat_array[i]);
			break;
		case	FS_STYLE_HISTOGRAM:
			if(p->histogram_use_default_bins)
			{
				i=find_histogram_bin(_handle->historgram_default_bins, _handle->histogram_default_bin_num, value);
			}
			else
			{
				i=find_histogram_bin(p->histogram_bins, p->histogram_bin_num, value);
			}
			target=&(p->stat_array[i]);
			bin_sum=&(p->stat_array[p->histogram_bin_num+HISTOGRAM_EXTRA_SUM]);
			max=&(p->stat_array[p->histogram_bin_num+HISTOGRAM_EXTRA_MAXVAL]);
			atomic_add(&(target->current_value),1);
			atomic_add(&(bin_sum->current_value),value);
			if(value>atomic_read(&(max->current_value)))
			{
				atomic_set(&(max->current_value),value);
			}
			return 0;
			break;
		default:
			target=&(p->stat_unit);
			break;
	}
	switch(op)
	{
		case	FS_OP_ADD:
			atomic_add(&(target->current_value),value);
			break;
		case	FS_OP_SET:
			atomic_set(&(target->current_value),value);
			break;	
		default:
			return -1;
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
			target=&(p->stat_unit);
			break;
		case	FS_STYLE_LINE:
			target=&(p->stat_array[column_seq]);	
			break;
		case	FS_STYLE_HISTOGRAM:
			target=&(p->stat_array[column_seq]);
			break;
		case	FS_STYLE_COLUMN:
		default:
			assert(0);
			break;
	}
	value= atomic_read(&(target->current_value));
	if(is_refer==0)
	{
		target->last_diff=value-target->last_output_value;
		target->last_output_value=value;
	}
	switch(calc_type)
	{
		case	FS_CALC_CURRENT:
			break;
		case	FS_CALC_SPEED:
			value=target->last_diff;
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

void StatsD_output(struct FS_space_t* _handle)
{
	display_manifest_t* p=NULL,*p_column=NULL;
	long long value=0;
	int i=0,j=0;
	char name_buff[UDP_PAYLOAD_SIZE];
	
	for(i=0;i<_handle->display_cnt;i++)
	{
		p=_handle->display[i];
		if(p->is_invisible==1||p->is_ratio==1)
		{
			continue;
		}
		if(p->style!=FS_STYLE_STATUS&&p->style!=FS_STYLE_FIELD)
		{
			continue;
		}
		value=get_stat_unit_val(p, 0, FS_CALC_SPEED, 1);
		append_metric(_handle, p->name,value);
	}
	for(i=0;i<_handle->display_cnt;i++)
	{
		p=_handle->display[i];
		if(p->style!=FS_STYLE_LINE)
		{
			continue;
		}
		if(p->is_invisible==1||p->is_ratio==1)
		{
			continue;
		}
		for(j=0;j<_handle->column_cnt;j++)
		{
			p_column=_handle->display[_handle->cloumn_id[j]];
			if(p_column->is_invisible==1||p_column->is_ratio==1)
			{
				continue;
			}
			snprintf(name_buff,sizeof(name_buff),"%s#%s",p->name,p_column->name);
			value=get_stat_unit_val(p, p_column->column_seq, FS_CALC_SPEED, 1);
			append_metric(_handle,name_buff,value);		
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
static int print_histogram_head(long long * bins, int bin_num, char*print_buf, unsigned int size)
{
	char* pos=print_buf;
	const char* extra[4]={"+inf","MAX","AVG","CNT"};
	char buff[32];
	int i=0;
	pos+=snprintf(pos,size-(pos-print_buf),"%-8s\t","histogram");
	for(i=0;i<bin_num;i++)
	{
		snprintf(buff,sizeof(buff),"<%lld",bins[i]);
		pos+=snprintf(pos,size-(pos-print_buf),"%8s\t",buff);
	}
	for(i=0;i<4;i++)
	{
		pos+=snprintf(pos,size-(pos-print_buf),"%8s\t",extra[i]);
	}
	pos+=snprintf(pos,size-(pos-print_buf),"\n");
	return pos-print_buf;
}
static int print_histogram_unit(display_manifest_t* p, char*print_buf, unsigned int size)
{
	char* pos=print_buf;
	long long value[p->histogram_bin_num+1]={0};
	long long sum=0,events=0,max=0;
	double avg=0.0;
	int i=0;
	
	pos+=snprintf(pos,size-(pos-print_buf),"%-10s\t",p->name);
	for(i=0;i<p->histogram_bin_num+1;i++)
	{
		value[i]=get_stat_unit_val(p, i, p->calc_type, 0);
		events+=value[i];
	}
	sum=get_stat_unit_val(p, p->histogram_bin_num+HISTOGRAM_EXTRA_SUM, p->calc_type, 0);
	max=get_stat_unit_val(p, p->histogram_bin_num+HISTOGRAM_EXTRA_MAXVAL, FS_CALC_CURRENT, 0);
	avg=(double)sum/events;
	for(i=0;i<p->histogram_bin_num+1;i++)
	{	
		pos+=snprintf(pos,sizeof(print_buf)-(pos-print_buf),"%8.2lf%%\t",((double)value[i]/events)*100);
	}	
	pos+=snprintf(pos,sizeof(print_buf)-(pos-print_buf),"%8lld\t",max);
	pos+=snprintf(pos,sizeof(print_buf)-(pos-print_buf),"%8.2e\t",avg);			
	pos+=snprintf(pos,sizeof(print_buf)-(pos-print_buf),"%8lld\n",events);

	return pos-print_buf;
}
static int output_style_histogram(struct FS_space_t* _handle,long long interval_ms,char*print_buf, unsigned int size)
{
	int i=0;
	char* pos=print_buf;
	display_manifest_t* p=NULL;
	if(_handle->histogram_cnt==0)
	{
		return 0;
	}
	if(_handle->histogram_default_cnt>0)
	{
		pos+=print_histogram_head(_handle->historgram_default_bins, _handle->histogram_default_bin_num, pos, size-(pos-print_buf));
		
		for(i=0;i<_handle->display_cnt;i++)
		{
			p=_handle->display[i];
			if(p->style!=FS_STYLE_HISTOGRAM||p->histogram_use_default_bins==0)
			{
				continue;
			}
			pos+=print_histogram_unit(p, pos, size-(pos-print_buf));
		}
	}
	if(_handle->histogram_cnt>_handle->histogram_default_cnt)
	{

		for(i=0;i<_handle->display_cnt;i++)
		{
			p=_handle->display[i];
			if(p->style!=FS_STYLE_HISTOGRAM||p->histogram_use_default_bins==1)
			{
				continue;
			}
			pos+=print_histogram_head(p->histogram_bins,p->histogram_bin_num, pos, size-(pos-print_buf));
			pos+=print_histogram_unit(p, pos, size-(pos-print_buf));
		}		
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

void FS_passive_output(screen_stat_handle_t handle)
{
	struct FS_space_t* _handle=(struct FS_space_t*)handle;
	struct timespec now;
	time_t current=0;
	char date_buff[32];
	char print_buf[1024*64]={0};
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
	clock_gettime(CLOCK_MONOTONIC,&now);
	long long interval_ms=(now.tv_sec-_handle->last_display_time.tv_sec)*1000+(now.tv_nsec-_handle->last_display_time.tv_nsec)/1000000;
	if(interval_ms<1)
	{
		return;
	}
	pos=print_buf;
	pos+=snprintf(pos,sizeof(print_buf)-(pos-print_buf),"%s%s",draw_boundary,ctime(&current));
	pos--;//jump '\n' generate by ctime()
	pos+=snprintf(pos,sizeof(print_buf)-(pos-print_buf),"%s\n",draw_boundary);

	pthread_mutex_lock(&(_handle->reg_lock));
	pos+=output_style_status(_handle, interval_ms,pos, sizeof(print_buf)-(pos-print_buf));
	pos+=output_style_field(_handle, interval_ms,pos, sizeof(print_buf)-(pos-print_buf));
	pos+=output_style_table(_handle, interval_ms,pos, sizeof(print_buf)-(pos-print_buf));
	pos+=output_style_histogram(_handle, interval_ms,pos, sizeof(print_buf)-(pos-print_buf));
	pthread_mutex_unlock(&(_handle->reg_lock));

	if(_handle->fp==NULL)
	{
		printf("Field Stat: open %s failed.\n",_handle->appoint_output_file);
		return;
	}
	if(_handle->print_mode==1)
	{
		fseek(_handle->fp,0,SEEK_SET);
	}
	fwrite(print_buf,pos-print_buf,1,_handle->fp);
	fflush(_handle->fp);
	memcpy(&(_handle->last_display_time),&now,sizeof(now));
	if(_handle->statsd_switch==1)
	{		
		pthread_mutex_lock(&(_handle->reg_lock));
		StatsD_output(_handle);
		pthread_mutex_unlock(&(_handle->reg_lock));
	}
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


