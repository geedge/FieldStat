#ifndef	__FIELD_STAT2_INTERNAL_H__
#define	__FIELD_STAT2_INTERNAL_H__

#include <pthread.h>
#include "hdr_histogram.h"
#include "threadsafe_counter.h"

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



#define N_TAG_MAX				32
#define	NUM_INIT_METRICS		1024

enum field_calc_algo
{
	FS_CALC_CURRENT=0,
	FS_CALC_SPEED
};

struct stat_unit_t
{
	struct threadsafe_counter changing;
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


struct metric_t
{
	char *field_name;
	enum field_type field_type;
	size_t n_tag;
	char *tag_key[N_TAG_MAX];
	char *tag_value[N_TAG_MAX];
	int output_scaling;

	int is_invisible;
	int is_ratio;
	int not_send_to_server;
	int numerator_id;
	int denominator_id;
	
	union
	{
		struct stat_unit_t counter;
		struct stat_unit_t gauge;
		struct stat_unit_t single;//for status and field
		struct stat_unit_t* line; //for line
		struct histogram_t histogram;
		int column_seq;	//for column
	};
};





#endif
