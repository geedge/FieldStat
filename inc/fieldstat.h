#pragma once
#include <stdio.h>
#ifdef __cplusplus
extern "C"
{
#endif
enum field_type
{
	FIELD_TYPE_COUNTER,
	FIELD_TYPE_GAUGE,
	FILED_TYPE_HISTOGRAM,
	FIELD_TYPE_SUMMARY
};

struct fieldstat_instance;
struct metric_id_list
{
	int count;
	int id[64];  // define marco
};

/**
 * @param name  The instance name.
 * @return      Instance id. NULL is failed, Not NULL is successd.
 * the output.
 */
struct fieldstat_instance * fieldstat_instance_create(const char *name);
int fieldstat_global_enable_prometheus_endpoint(unsigned short listen_port, const char *url);
int fieldstat_set_prometheus_output(struct fieldstat_instance *instance);
int fieldstat_set_statsd_server(struct fieldstat_instance *instance, const char *ip, unsigned short port);
int fieldstat_set_line_protocol_server(struct fieldstat_instance *instance, const char *ip, unsigned short port);
int fieldstat_set_local_output(struct fieldstat_instance *instance, const char *filename, const char *format);//format could be "json" or "default"
int fieldstat_backgroud_thead_disable(struct fieldstat_instance *instance);
int fieldstat_set_output_interval(struct fieldstat_instance *instance, int seconds);//default is 2 seconds


/*
 * @param field_name the name of the field
 * @type counter, gauge summary and histogram
 * @n_tag size of tag_key[] and tag_value[]
 * 
 * return field_id to be used in fieldstat_value_xxx(),
 * 	 -1 if an error occured.
 * example field_name=packets, tag=[{"policy_id", "101"},{"profile_id", "a"}]
 * ouput: accumulate value or delta value
 * counter or histogram
 * */
int fieldstat_register(struct fieldstat_instance *instance, enum field_type type, const char *field_name, const char *tag_key[], const char *tag_value[], size_t n_tag);
int fieldstat_register_table(struct fieldstat_instance *instance, enum field_type table_type[], const char *field_list[], size_t n_field);
//int fieldstat_register_table_metrics(struct fieldstat_instance * instance, const char *field_name, const char *tag_key[], const char *tag_value[]);
struct metric_id_list fieldstat_register_table_metrics(struct fieldstat_instance * instance, int table_id, const char *field_name, const char *tag_key[], const char *tag_value[],size_t n_tag);


int fieldstat_value_incrby(struct fieldstat_instance *instance, int field_id, long long increment);
int fieldstat_value_set(struct fieldstat_instance *instance, int field_id, long long retain);
int fieldstat_value_decrby(struct fieldstat_instance *instance, int field_id, long long decrment);

void fieldstat_instance_start(struct fieldstat_instance *instance);
void fieldstat_passive_output(struct fieldstat_instance *instance);

/**
 * @param instance fieldstat instance
 * @param type field type
 * @param field_name  field name
 * @param tag_key     the array of tag key
 * @param tag_value   the array of tag value
 * @param n_tag       the number of tags
 * @param quantiles   the quantiles of summary output
 * @return metric id: -1 is failed,  > 0 is success
 * the output.
 */
int fieldstat_register_histogram(struct fieldstat_instance *instance, enum field_type type, const char *field_name, const char *tag_key[], const char *tag_value[], size_t n_tag, 
		const char * bins,const long long lowest_trackable_value,long long highest_trackable_value,int significant_figures);

#ifdef __cplusplus
}
#endif