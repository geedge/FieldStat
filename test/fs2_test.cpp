#include "field_stat2.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#define	TEST_STATUS_NUM	4
#define TEST_FIELD_NUM	9
#define TEST_LINE_NUM	7
#define TEST_COLUMN_NUM	8
#define TEST_HISTOGRAM_NUM	4

#define	TEST_RUNTIME_REG_NUM	32
#define	TEST_RUNTIME_REG_LINE_NUM 6
int main(int argc,char* argv[])
{
	screen_stat_handle_t handle=NULL;
	const char* stat_path="./fs2_test.status";
	const char* app_name="fs2_test";
	char buff[128];
	int value=0,i=0,j=0,runtime_reg_num=0,runtime_reg_line_num=0,ret=0;
	int loops=10;
	handle=FS_create_handle();
	
	FS_set_para(handle, APP_NAME, app_name, strlen(app_name)+1);
	value=0;
	FS_set_para(handle, FLUSH_BY_DATE, &value, sizeof(value));
	FS_set_para(handle, OUTPUT_DEVICE, stat_path, strlen(stat_path)+1);
	value=1;
	FS_set_para(handle, PRINT_MODE, &value, sizeof(value));
	value=1;
	FS_set_para(handle, CREATE_THREAD, &value, sizeof(value));
    FS_set_para(handle, METRIS_FORMAT, &value, sizeof(value));
    value=2;
	FS_set_para(handle, STAT_CYCLE, &value, sizeof(value));
	value=4096;
	FS_set_para(handle, MAX_STAT_FIELD_NUM, &value, sizeof(value));

	FS_set_para(handle, STATS_SERVER_IP, "127.0.0.1", strlen("127.0.0.1"));
	value=8100;
	FS_set_para(handle, STATS_SERVER_PORT, &value, sizeof(value));

	//value=FS_OUTPUT_INFLUX_LINE;
	value=FS_OUTPUT_STATSD;
	FS_set_para(handle, STATS_FORMAT, &value, sizeof(value));

	
	const char* histogram_format="0.1,0.5,0.8,0.9,0.95,0.99";
	FS_set_para(handle, HISTOGRAM_GLOBAL_BINS, histogram_format, strlen(histogram_format)+1);
	
	int status_ids[TEST_STATUS_NUM], field_ids[TEST_FIELD_NUM], line_ids[TEST_LINE_NUM+TEST_RUNTIME_REG_LINE_NUM], column_ids[TEST_COLUMN_NUM];
	int histogram_ids[TEST_HISTOGRAM_NUM];
	int runtime_status_ids[TEST_RUNTIME_REG_NUM];
	for(i=0;i<TEST_STATUS_NUM;i++)
	{
		snprintf(buff,sizeof(buff),"status_%02d",i);
		status_ids[i]=FS_register(handle, FS_STYLE_STATUS, FS_CALC_CURRENT,buff);
	}
	for(i=0;i<TEST_FIELD_NUM;i++)
	{
		snprintf(buff,sizeof(buff),"field_%02d",i);
		field_ids[i]=FS_register(handle, FS_STYLE_FIELD, FS_CALC_CURRENT,buff);
	}
	for(i=0;i<TEST_LINE_NUM;i++)
	{
		snprintf(buff,sizeof(buff),"line_%d",i);
		line_ids[i]=FS_register(handle, FS_STYLE_LINE, FS_CALC_CURRENT,buff);
	}
	for(i=0;i<TEST_COLUMN_NUM;i++)
	{
		snprintf(buff,sizeof(buff),"column_%d",i);
		column_ids[i]=FS_register(handle, FS_STYLE_COLUMN, FS_CALC_SPEED,buff);
	}
	for(i=0;i<TEST_HISTOGRAM_NUM;i++)
	{
		snprintf(buff,sizeof(buff),"rpc_%d",i);
		if(i==1)
		{
			histogram_ids[i]=FS_register_histogram(handle, FS_CALC_SPEED, buff
													,1,100000,3);
		}
		else
		{
			histogram_ids[i]=FS_register_histogram(handle, FS_CALC_CURRENT, buff
										,1,100000,3);
		}
	}
	value=column_ids[2];
	FS_set_para(handle, ID_INVISBLE, &value, sizeof(value));
	snprintf(buff,sizeof(buff),"c0/c1");
	FS_register_ratio(handle,
				column_ids[0], 
				column_ids[1], 
				1,
				FS_STYLE_COLUMN,
				FS_CALC_SPEED,
				buff);
	value=line_ids[1];
	FS_set_para(handle, NOT_SEND_METRIC_TO_SERVER, &value, sizeof(value));
	
	FS_start(handle);
	loops=100;
	while (loops>0)
	{
		loops--;
		for(i=0;i<TEST_STATUS_NUM;i++)
		{
			FS_operate(handle, status_ids[i], 0, FS_OP_SET, i*10);
		}
		for(i=0;i<TEST_FIELD_NUM;i++)
		{
			FS_operate(handle, field_ids[i], 0, FS_OP_ADD, i*100);
		}
		for(i=0;i<TEST_LINE_NUM+runtime_reg_line_num;i++)
		{
			for(j=0;j<TEST_COLUMN_NUM;j++)
			{
				FS_operate(handle, line_ids[i], column_ids[j], FS_OP_ADD, (j+1)*30);
			}
		}
		for(i=0;i<runtime_reg_num;i++)
		{
			FS_operate(handle, runtime_status_ids[i], 0, FS_OP_ADD, i*1000);
		}
		if(runtime_reg_num<TEST_RUNTIME_REG_NUM)
		{
			snprintf(buff,sizeof(buff),"rt_reg_%02d",runtime_reg_num);
			ret=FS_register(handle,FS_STYLE_STATUS, FS_CALC_SPEED, buff);
			assert(ret>=0);
			runtime_status_ids[runtime_reg_num]=ret;
			runtime_reg_num++;
			ret=FS_register(handle,FS_STYLE_COLUMN, FS_CALC_SPEED, buff);
			assert(ret==-1);//always failed
		}
		if(runtime_reg_line_num<TEST_RUNTIME_REG_LINE_NUM)
		{
			snprintf(buff,sizeof(buff),"line_rt_%02d",runtime_reg_line_num);
			ret=FS_register(handle,FS_STYLE_LINE, FS_CALC_SPEED, buff);
			assert(ret>=0);
			line_ids[TEST_LINE_NUM+runtime_reg_line_num]=ret;
			runtime_reg_line_num++;
		}
		long long preset[]={1,20,30,200,400,1000,2000,4000};
		for(i=0;i<TEST_HISTOGRAM_NUM;i++)
		{
			for(j=0;(size_t)j<sizeof(preset)/sizeof(long long);j++)
			{
				FS_operate(handle, histogram_ids[i], 0, FS_OP_SET,preset[j]);
			}
		}
		sleep(1);
	}
	FS_stop(&handle);
}

