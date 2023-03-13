#pragma once
#include <stdio.h>
#ifdef __cplusplus
extern "C"
{
#endif

#define TABLE_COLUMN_MAX_SIZE 64

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
	int id[TABLE_COLUMN_MAX_SIZE];  // define marco
};

/**
 * Create fieldstat instance.
 * @param name  The instance name.
 * @return Instance ptr. NULL is failed, Not NULL is successd.
 */
struct fieldstat_instance * fieldstat_instance_create(const char *name);
/**
 * Create prometheus endpoint.
 * @param listen_port The endpoint listen port.
 * @param url The endpoint url. url is "/metrics" when url is NULL.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_global_enable_prometheus_endpoint(unsigned short listen_port, const char *url);
/**
 * Enable fieldstat instance output prometheus
 * @param instance The Fieldstat instance.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_set_prometheus_output(struct fieldstat_instance *instance);
/**
 * Enable output metric to statsd server.
 * @param instance The Fieldstat instance.
 * @param ip  Statsd server ip.
 * @param port Statsd server port.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_set_statsd_server(struct fieldstat_instance *instance, const char *ip, unsigned short port);
/**
 * Enable output metric to line protocol server.
 * @param instance The Fieldstat instance.
 * @param ip  Line protocol server ip.
 * @param port Line protocol server port.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_set_line_protocol_server(struct fieldstat_instance *instance, const char *ip, unsigned short port);
/**
 * Enable output metric to file.
 * @param instance The Fieldstat instance.
 * @param filename The output target filename.
 * @param format The output format. value in {"json","default"}
 * @return -1 is failed. 0 is success.
 */
int fieldstat_set_local_output(struct fieldstat_instance *instance, const char *filename, const char *format);
/**
 * Disable the background thread.
 * @param instance The Fieldstat instance.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_background_thread_disable(struct fieldstat_instance *instance);
/**
 * Set the output interval in seconds
 * @param seconds  In seconds.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_set_output_interval(struct fieldstat_instance *instance, int seconds);
/**
 * Register counter and gauge type metrics
 * @param instance The fieldstat instance.
 * @param type Counter, gauge.
 * @param field_name Metric field name.
 * @param tag_key Tag key array
 * @param tag_value Tag key array
 * @param n_tag size of tag_key[] and tag_value[]
 * @return metric id. -1 is failed. >=0 is success.
 */
int fieldstat_register(struct fieldstat_instance *instance, enum field_type type, const char *field_name, const char *tag_key[], const char *tag_value[], size_t n_tag);
/**
 * Register table. Max table num is 64. Max table columns is 64.
 * @param instance The fieldstat instance.
 * @param name  The table name.
 * @param column_name column name array.
 * @param column_type column type array. Type in [counter, gauge]
 * @param n_column size of column_type[] and column_name[]
 * @return metric id.  -1 is failed. >=0 is success.
 */
int fieldstat_register_table(struct fieldstat_instance *instance, const char *name, const char *column_name[], enum field_type column_type[], size_t n_column);
/**
 * Register table metric
 * @param instance The fieldstat instance.
 * @param table_id The table id by fieldstat_register_table create.
 * @param line_name The table line name.
 * @param tag_key Tag key array
 * @param tag_value Tag key array
 * @param n_tag size of tag_key[] and tag_value[]
 * @return metric id. -1 is failed. >=0 is success.
 */
struct metric_id_list fieldstat_register_table_metrics(struct fieldstat_instance * instance, int table_id, const char *line_name, const char *tag_key[], const char *tag_value[],size_t n_tag);
/**
 * increment filed value.
 * @param instance The fieldstat instance.
 * @param field_id The field id.
 * @param increment the value to increment
 * @return -1 is failed. 0 is success.
 */
int fieldstat_value_incrby(struct fieldstat_instance *instance, int field_id, long long increment);
/**
 * Set filed value.
 * @param instance The fieldstat instance.
 * @param field_id The field id.
 * @param increment the value to set
 * @return -1 is failed. 0 is success.
 */
int fieldstat_value_set(struct fieldstat_instance *instance, int field_id, long long retain);
/**
 * Decrment filed value.
 * @param instance The fieldstat instance.
 * @param field_id The field id.
 * @param increment the value to decrment
 * @return -1 is failed. 0 is success.
 */
int fieldstat_value_decrby(struct fieldstat_instance *instance, int field_id, long long decrment);
/**
 * Make the fieldstat instance affect.
 * @param instance The fieldstat instance.
 */
void fieldstat_instance_start(struct fieldstat_instance *instance);
/**
 * passive output
 * @param instance The fieldstat instance.
 */
void fieldstat_passive_output(struct fieldstat_instance *instance);

/**
 * Register histogram metric
 * @param instance fieldstat instance
 * @param field_name  field name
 * @param tag_key     the array of tag key
 * @param tag_value   the array of tag value
 * @param n_tag       the number of tags
 * @param quantiles   the quantiles of summary output
 * @return metric id: -1 is failed,  >= 0 is success
 * the output.
 */
int fieldstat_register_histogram(struct fieldstat_instance *instance, const char *field_name, const char *tag_key[], const char *tag_value[], size_t n_tag, 
		const char * bins,const long long lowest_trackable_value,long long highest_trackable_value,int significant_figures);

/**
 * Register summary metric
 * @param instance fieldstat instance
 * @param field_name  field name
 * @param tag_key     the array of tag key
 * @param tag_value   the array of tag value
 * @param n_tag       the number of tags
 * @param quantiles   the quantiles of summary output
 * @return metric id: -1 is failed,  >= 0 is success
 * the output.
 */
int fieldstat_register_summary(struct fieldstat_instance *instance, const char *field_name, const char *tag_key[], const char *tag_value[], size_t n_tag, 
		const char * bins,const long long lowest_trackable_value,long long highest_trackable_value,int significant_figures);

#ifdef __cplusplus
}
#endif