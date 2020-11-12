#ifndef	__FIELD_STAT2_INTERNAL_H__
#define	__FIELD_STAT2_INTERNAL_H__

#include <pthread.h>
#include "hdr_histogram.h"

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

#define	INIT_STAT_FIELD_NUM		1024
#define	MAX_STAT_COLUMN_NUM		64
#define MAX_PATH_LEN			256
#define	UDP_PAYLOAD_SIZE		1460

#define	STATUS_PER_LINE				6
#define	FIELD_PER_LINE				8



#define	HISOTGRAM_EXTRA_INF			0
#define	HISTOGRAM_EXTRA_SUM			1
#define	HISTOGRAM_EXTRA_MAXVAL		2
#define	HISTOGRAM_EXTRA_SIZE		3


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


struct FS_space_t
{
	int stat_cycle;
	int screen_print_trigger;
	int print_mode;	//1:Rewrite ,2: Append
	int create_thread;
	int running;
	int output_prometheus;
	
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

int PROM_register(screen_stat_handle_t handle);
long long get_stat_unit_val(display_manifest_t* p, int column_seq,enum field_calc_algo calc_type,int is_refer);
#endif
