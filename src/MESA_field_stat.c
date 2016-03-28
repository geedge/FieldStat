#include "field_stat2.h"
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>

#define	MAX_STAT_FIELD_NUM	64
#define	MAX_STAT_COLUMN_NUM	32

#define	STATUS_PER_LINE	6
#define	FIELD_PER_LINE		8
int FIELD_STAT_VERSION_2_20160211=0;
#ifndef atomic_read
#define atomic_read(x) __sync_add_and_fetch((x),0)
#endif

#ifndef atomic_add 
#define atomic_add(x,y) __sync_add_and_fetch((x),(y))
#endif

#ifndef atomic_set
#define atomic_set(x,y) __sync_lock_test_and_set((x),y)
#endif 
const char* draw_line="________________________________________________________________________________________________________________________________________________";
const char* draw_boundary="============================================================";
struct stat_field_t
{
	char	*filed_name;
	long long current_value;
	long long last_output_value;
};

struct display_manifest_t
{
	char* name;
	int is_invisible;
	int idx;
	int is_ratio;
	int numerator_id;
	int denominator_id;
	int output_scaling;	//negative value: zoom in; positive value: zoom out;
	enum field_dsp_style_t style;
	enum field_calc_algo calc_type;
};
struct stat_unit_t
{
	long long current_value;
	long long last_output_value;
	long long last_diff;
};

struct FS_space_t
{
	int stat_cycle;
	int screen_print_trigger;
	int print_mode;	//1:Rewrite ,2: Append
	int create_thread;
	
	int line_cnt;
	int display_cnt;
	int single_cnt;//including line_cnt;
	int matrix_base;
	int column_cnt;
	int cloumn_id[MAX_STAT_COLUMN_NUM];
	struct display_manifest_t display[MAX_STAT_FIELD_NUM];

	struct stat_unit_t* stat_array;//for styple STATUS/FIELD
	char* output_file;
	FILE* fp;
	struct timespec last_display_time;
};

void *thread_screen_print(void *arg);

screen_stat_handle_t FS_create_handle(void)
{
	struct FS_space_t* handle=(struct FS_space_t*)calloc(sizeof(struct FS_space_t),1);
	handle->screen_print_trigger=1;
	handle->print_mode=1;
	handle->stat_cycle=2;
	handle->create_thread=1;
	
	return handle;
}
int FS_set_para(screen_stat_handle_t handle, enum FS_option  type,const void* value,int size)
{
	struct FS_space_t* _handle=(struct FS_space_t*)handle;
	FILE* fp=NULL;
	int int_val=0;
	if(type!=OUTPUT_DEVICE)
	{
		int_val=*(const int*)value;
	}
	switch(type)
	{
		case	OUTPUT_DEVICE:
			fp=fopen((const char*)value,"w");
			if(fp==NULL)
			{
				printf("Field Stat: open %s failed.\n",(const char*)value);
				return -1;
			}
			_handle->fp=fp;
			_handle->output_file=(char*)calloc(sizeof(char),size);
			memcpy(_handle->output_file,value,size);
			fp=NULL;
			break;
		case	PRINT_MODE:
			if(size!=4||(int_val!=1&&int_val!=2))
			{
				return -1;
			}
			_handle->print_mode=int_val;
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
			_handle->display[int_val].is_invisible=1;
			break;
		default:
			return -1;
	}
	return 0;
}
void FS_start(screen_stat_handle_t handle)
{
	struct FS_space_t* _handle=(struct FS_space_t*)handle;
	pthread_t cfg_mon_t;
	int i=0,j=0,pos=0;
	int stat_unit_cnt=0;
	stat_unit_cnt=_handle->single_cnt+_handle->line_cnt*_handle->column_cnt;
	for(i=0;i<_handle->display_cnt;i++)
	{
		switch(_handle->display[i].style)
		{
			case	FS_STYLE_FIELD:
			case	FS_STYLE_STATUS:
				_handle->display[i].idx=pos;
				pos++;
				break;
			case	FS_STYLE_COLUMN:
				_handle->display[i].idx=j;
				_handle->cloumn_id[j]=i;
				j++;
				break;
			case	FS_STYLE_LINE:
				_handle->display[i].idx=pos;
				pos+=_handle->column_cnt;
				break;
			default:
				assert(0);
				break;
		}		
	}
	assert(j==_handle->column_cnt);
	_handle->stat_array=(struct stat_unit_t*)calloc(sizeof(struct stat_unit_t),stat_unit_cnt);
	clock_gettime(CLOCK_MONOTONIC,&(_handle->last_display_time));
	if(_handle->create_thread==1)
	{
		pthread_create(&cfg_mon_t, NULL, thread_screen_print, (void*)handle);
	}
	return;
}
void FS_stop(screen_stat_handle_t* handle)
{
	int i=0;
	struct FS_space_t* _handle=*(struct FS_space_t**)handle;
	_handle->create_thread=0;
	for(i=0;i<_handle->display_cnt;i++)
	{
		free(_handle->display[i].name);
		_handle->display[i].name=NULL;
	}
	free(_handle->stat_array);
	_handle->stat_array=NULL;
	free(_handle->output_file);
	_handle->output_file=NULL;
	free(_handle);
	if(strlen(_handle->output_file)>0)
	{
		fclose(_handle->fp);
		_handle->fp=NULL;
	}
	*handle=NULL;
	return;
}
int FS_register(screen_stat_handle_t handle,enum field_dsp_style_t style,enum field_calc_algo calc_type,const char* name)
{
	struct FS_space_t* _handle=(struct FS_space_t*)handle;
	struct display_manifest_t * choosen=NULL;
	int id=0;
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
	choosen=&(_handle->display[id]);
	choosen->calc_type=calc_type;
	choosen->style=style;
	choosen->is_ratio=0;
	choosen->output_scaling=1;
	choosen->name=(char*)calloc(sizeof(char),strlen(name)+1);
	memcpy(choosen->name,name,strlen(name));
	return id;
}
int FS_register_ratio(screen_stat_handle_t handle,int numerator_id,int denominator_id,int scaling,enum field_dsp_style_t style,enum field_calc_algo calc_type,const char* name)
{
	struct FS_space_t* _handle=(struct FS_space_t*)handle;
	struct display_manifest_t * choosen=NULL;
	int id=0;
	if(numerator_id>=_handle->display_cnt||denominator_id>=_handle->display_cnt)
	{
		return -1;
	}
	if(_handle->display[denominator_id].is_ratio==1||_handle->display[numerator_id].is_ratio==1)
	{
		return -1;
	}
	if(_handle->display[denominator_id].style==FS_STYLE_LINE||_handle->display[numerator_id].style==FS_STYLE_LINE)
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
	if(style==FS_STYLE_COLUMN)
	{
		_handle->column_cnt++;
	}

	id=_handle->display_cnt;
	_handle->display_cnt++;
	choosen=&(_handle->display[id]);
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
int FS_operate(screen_stat_handle_t handle,int id,int column_id,enum field_op op,long long value)
{
	struct FS_space_t* _handle=(struct FS_space_t*)handle;
	int i=0,j=0;
	struct stat_unit_t* target;
	if(id>=_handle->display_cnt)
	{
		return -1;
	}
	if(_handle->display[id].style==FS_STYLE_LINE)
	{
		if(column_id>=_handle->display_cnt||column_id>=_handle->display_cnt||_handle->display[column_id].style!=FS_STYLE_COLUMN)
		{
			return -1;
		}
		i=_handle->display[id].idx;
		j=_handle->display[column_id].idx;
		target=&(_handle->stat_array[i+j]);
	}
	else
	{
		i=_handle->display[id].idx;
		target=&(_handle->stat_array[i]);
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
static long long get_stat_unit_val(struct FS_space_t* _handle,int id,int column_id,enum field_calc_algo calc_type,int is_refer)
{
	display_manifest_t* p=NULL;
	stat_unit_t* target=NULL;
	p=&(_handle->display[id]);
	long long value=0;
	switch(p->style)
	{
		case	FS_STYLE_FIELD:
		case	FS_STYLE_STATUS:
			target=&(_handle->stat_array[p->idx]);
			break;
		case	FS_STYLE_LINE:
			target=&(_handle->stat_array[p->idx+_handle->display[column_id].idx]);
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
static double get_stat_ratio(struct FS_space_t* _handle,int numerator_id,int denominator_id,int scaling,int line_id,enum field_calc_algo calc_type)
{
	long long value_n=0,value_d=0;
	double ratio=0.0;

	if(_handle->display[numerator_id].style==FS_STYLE_COLUMN)
	{
		assert(_handle->display[denominator_id].style==FS_STYLE_COLUMN);
		value_n=get_stat_unit_val(_handle,line_id,numerator_id,calc_type,1);
		value_d=get_stat_unit_val(_handle,line_id,denominator_id,calc_type,1);
	}
	else
	{
		value_n=get_stat_unit_val(_handle,numerator_id,0,calc_type,1);
		value_d=get_stat_unit_val(_handle,denominator_id,0,calc_type,1);
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
static int output_style_status(struct FS_space_t* _handle,long long interval_ms,char*print_buf, unsigned int size)
{
	int i=0,j=0;
	display_manifest_t* p=NULL;
	long long value=0;
	double ratio=0.0;
	char* pos=print_buf;

	for(i=0;i<_handle->display_cnt;i++)
	{
		p=&(_handle->display[i]);
		if(p->style!=FS_STYLE_STATUS)
		{
			continue;
		}
		if(p->is_invisible==1)
		{
			value=get_stat_unit_val(_handle,i, 0, p->calc_type, 0);
			continue;
		}
		if(p->is_ratio==1)
		{
			ratio=get_stat_ratio(_handle,p->numerator_id,p->denominator_id,p->output_scaling, 0,p->calc_type);
			pos+=snprintf(pos,size-(pos-print_buf),"%s: %10.2e\t",p->name,ratio);
		}
		else
		{
			value=get_stat_unit_val(_handle,i, 0, p->calc_type, 0);
			if(p->calc_type==FS_CALC_SPEED)
			{
				value=value*p->output_scaling*1000/interval_ms;
			}
			pos+=snprintf(pos,size-(pos-print_buf),"%s: %lld\t",p->name,value);
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
	int field_id[MAX_STAT_FIELD_NUM]={0};
	int field_cnt=0;
	
	for(i=0;i<_handle->display_cnt;i++)
	{
		p=&(_handle->display[i]);
		if(p->style!=FS_STYLE_FIELD)
		{
			continue;
		}
		if(p->is_invisible==1)
		{
			//for update last_output
			get_stat_unit_val( _handle, field_id[i+j], 0,FS_CALC_CURRENT, 0);
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
			p=&(_handle->display[field_id[i+j]]);
			pos+=snprintf(pos,size-(pos-print_buf),"%10s\t",p->name);
		}
		pos+=snprintf(pos,size-(pos-print_buf),"\nsum\t");
		for(j=0;j<FIELD_PER_LINE&&i+j<field_cnt;j++)
		{
			p=&(_handle->display[field_id[i+j]]);
			if(p->is_ratio==1)
			{
				ratio=get_stat_ratio(_handle,p->numerator_id,p->denominator_id,p->output_scaling, 0,FS_CALC_CURRENT);
				pos+=snprintf(pos,size-(pos-print_buf),"%f\t",ratio);
			}
			else
			{
				value=get_stat_unit_val( _handle, field_id[i+j], 0,FS_CALC_CURRENT, 0);
				pos+=snprintf(pos,sizeof(print_buf)-(pos-print_buf),"%10lld\t",value);
			}
		}
		pos+=snprintf(pos,sizeof(print_buf)-(pos-print_buf),"\nspeed/s\t");
		for(j=0;j<FIELD_PER_LINE&&i+j<field_cnt;j++)
		{
			if(p->is_ratio==1)
			{
				ratio=get_stat_ratio(_handle,p->numerator_id,p->denominator_id,p->output_scaling, 0,FS_CALC_SPEED);
				pos+=snprintf(pos,size-(pos-print_buf),"%10.2e\t",ratio);
			}
			else
			{
				value=get_stat_unit_val( _handle, field_id[i+j], 0,FS_CALC_SPEED, 1);
				pos+=snprintf(pos,size-(pos-print_buf),"%10lld\t",value*1000/interval_ms);
			}
		}
		i+=j;
		pos+=snprintf(pos,sizeof(print_buf)-(pos-print_buf),"\n");
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
		p_column=&(_handle->display[_handle->cloumn_id[i]]);
		if(p_column->is_invisible==1)
		{
			continue;
		}
		pos+=snprintf(pos,size-(pos-print_buf),"\t%10s",p_column->name);
	}
	pos+=snprintf(pos,sizeof(print_buf)-(pos-print_buf),"\n");
	for(i=0;i<_handle->display_cnt;i++)
	{
		p_line=&(_handle->display[i]);
		if(p_line->style!=FS_STYLE_LINE)
		{
			continue;
		}
		pos+=snprintf(pos,size-(pos-print_buf),"%-20s\t",p_line->name);
		for(j=0;j<_handle->column_cnt;j++)
		{
			p_column=&(_handle->display[_handle->cloumn_id[j]]);
			if(p_column->is_invisible==1)
			{
				//for flush last_output_value
				get_stat_unit_val( _handle, i, _handle->cloumn_id[j],p_column->calc_type, 0);
				continue;
			}
			if(p_column->is_ratio==1)
			{
				ratio=get_stat_ratio(_handle,p_column->numerator_id,p_column->denominator_id,p_column->output_scaling, i,p_column->calc_type);
				pos+=snprintf(pos,size-(pos-print_buf),"%10.2e\t",ratio);
			}
			else
			{
				value=get_stat_unit_val(_handle,i, _handle->cloumn_id[j], p_column->calc_type, 0);
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
		pos+=snprintf(pos,sizeof(print_buf)-(pos-print_buf),"\n");

	}
	if(pos-print_buf>0)
	{
		if(*(pos-1)=='\n')
		{
			pos--;
		}
		pos+=snprintf(pos,sizeof(print_buf)-(pos-print_buf),"\n");
	}
	return pos-print_buf;
}
void FS_passive_output(screen_stat_handle_t handle)
{
	struct FS_space_t* _handle=(struct FS_space_t*)handle;
	struct timespec now;
	time_t current=0;
	char print_buf[1024*64]={0};
	char*pos=NULL;
	time(&current);
	if(_handle->screen_print_trigger==0)
	{
		return;
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

	pos+=output_style_status(_handle, interval_ms,pos, sizeof(print_buf)-(pos-print_buf));
	pos+=output_style_field(_handle, interval_ms,pos, sizeof(print_buf)-(pos-print_buf));
	pos+=output_style_table(_handle, interval_ms,pos, sizeof(print_buf)-(pos-print_buf));

	if(_handle->fp==NULL)
	{
		printf("Field Stat: open %s failed.\n",_handle->output_file);
		return;
	}
	if(_handle->print_mode==1)
	{
		fseek(_handle->fp,0,SEEK_SET);
	}
	fwrite(print_buf,pos-print_buf,1,_handle->fp);
	fflush(_handle->fp);
	memcpy(&(_handle->last_display_time),&now,sizeof(now));
}
void *thread_screen_print(void *arg)
{
	struct FS_space_t* handle=(struct FS_space_t*)arg;
	while(handle->create_thread)
	{
		FS_passive_output(handle);
		sleep(handle->stat_cycle);
	}
	return NULL;
}

screen_stat_handle_t init_screen_stat(FILE* output_fp,int stat_cycle,int screen_print_trigger)
{
	struct FS_space_t* handle=(struct FS_space_t* )FS_create_handle();
	handle->fp=output_fp;
	FS_set_para(handle, STAT_CYCLE,&stat_cycle, (int)sizeof(stat_cycle));
	FS_set_para(handle, PRINT_TRIGGER,&screen_print_trigger, (int)sizeof(screen_print_trigger));
	FS_set_para(handle, CREATE_THREAD,&screen_print_trigger, (int)sizeof(screen_print_trigger));

	FS_start(handle);
	return handle;
}

int stat_field_register(screen_stat_handle_t handle,const char* field_name)
{
	int ret=0;
	ret=FS_register(handle,FS_STYLE_FIELD,FS_CALC_SPEED, field_name);
	return ret;
}

int stat_field_operation(screen_stat_handle_t handle,int field_id,int operation,long long value)
{
	int ret=0;
	ret=FS_operate( handle, field_id, 0,(enum field_op)operation, value);
	return ret;
}


