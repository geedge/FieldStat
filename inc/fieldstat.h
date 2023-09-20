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


struct fieldstat_tag{
	const char *key;
	int value_type;  // 0 int, 1 double, 2 str
	union{
		long long value_int;
		double value_double;
		const char *value_str;
	};
};

struct fieldstat_instance;
struct fieldstat_dynamic_instance;

/**
 * Create fieldstat instance.
 * The operation should be executed in single-threaded fashion.
 * @param name The instance name.
 * @return Instance ptr. NULL failed, Not NULL successd.
 */
struct fieldstat_instance * fieldstat_instance_new(const char *name);
/**
 * Create prometheus endpoint. 
 * The operation should be executed in single-threaded fashion.
 * @param listen_port The endpoint listen port. i.e., 80,8080
 * @param url_path The endpoint url path. url path is "/metrics" when url path is NULL.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_global_enable_prometheus_endpoint(unsigned short listen_port, const char *url_path);
/**
 * Enable fieldstat instance output prometheus
 * The operation should be executed in single-threaded fashion.
 * @param instance The fieldstat instance.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_enable_prometheus_output(struct fieldstat_instance *instance);
/**
 * Enable output metric to statsd server.
 * The operation should be executed in single-threaded fashion.
 * @param instance The fieldstat instance.
 * @param ip  Statsd server ip.
 * @param port Statsd server port. i.e., 80,8080
 * @return -1 is failed. 0 is success.
 */
int fieldstat_set_statsd_server(struct fieldstat_instance *instance, const char *ip, unsigned short port);
/**
 * Enable output metric to line protocol server.
 * The operation should be executed in single-threaded fashion.
 * @param instance The fieldstat instance.
 * @param ip  Line protocol server ip.
 * @param port Line protocol server port. i.e., 80,8080
 * @return -1 is failed. 0 is success.
 */
int fieldstat_set_line_protocol_server(struct fieldstat_instance *instance, const char *ip, unsigned short port);
/**
 * Enable output metric to file.
 * The operation should be executed in single-threaded fashion.
 * @param instance The fieldstat instance.
 * @param filename The output target filename.
 * @param format The output format. value in {"json","default"}
 * @return -1 is failed. 0 is success.
 */
int fieldstat_set_local_output(struct fieldstat_instance *instance, const char *filename, const char *format);
/**
 * Disable the background thread.
 * The operation should be executed in single-threaded fashion.
 * @param instance The fieldstat instance.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_disable_background_thread(struct fieldstat_instance *instance);
/**
 * Set the output interval in milliseconds
 * The operation should be executed in single-threaded fashion.
 * @param seconds  In milliseconds.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_set_output_interval(struct fieldstat_instance *instance, int milliseconds);
/**
 * Register counter and gauge type metrics
 * @param instance The fieldstat instance.
 * @param type counter, gauge.
 * @param field_name Metric field name.
 * @param tags The tag array.
 * @param n_tag Size of tags[]
 * @return metric id. -1 is failed. >=0 is success.
 */
int fieldstat_register(struct fieldstat_instance *instance, enum field_type type, const char *field_name, const struct fieldstat_tag tags[], size_t n_tag);
/**
 * Register table. Max table num is 64. Max table columns is 64.
 * @param instance The fieldstat instance.
 * @param table_name  The table name.
 * @param column_name column name array.
 * @param column_type column type array. Type in [counter, gauge]
 * @param n_column size of column_type[] and column_name[]
 * @return table id.  -1 is failed. >=0 is success.
 */
int fieldstat_register_table(struct fieldstat_instance *instance, const char *table_name, const char *column_name[], enum field_type column_type[], size_t n_column);
/**
 * Register table row
 * @param instance The fieldstat instance.
 * @param table_id The table id by fieldstat_register_table create.
 * @param row_name The table row name.
 * @param tags The tag array.
 * @param n_tag Size of tags[]
 * @param output_metric_ids Output metric ids
 * @return -1 is failed. 0 is success.
 */
int fieldstat_register_table_row(struct fieldstat_instance *instance, int table_id, const char *row_name, const struct fieldstat_tag tags[],size_t n_tag, int output_metric_ids[]);
/**
 * Increment fieldstat metric value by metric_id.
 * @param instance The fieldstat instance.
 * @param metric_id The metric id.
 * @param increment The value to increment.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_value_incrby(struct fieldstat_instance *instance, int metric_id, long long increment);
/**
 * Set fieldstat metric value by metric id.
 * @param instance The fieldstat instance.
 * @param metric_id The metric id.
 * @param value The value to set.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_value_set(struct fieldstat_instance *instance, int metric_id, long long value);
/**
 * Decrement fieldstat metric value.
 * @param instance The fieldstat instance.
 * @param metric_id The metric id.
 * @param decrement The value to decrement.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_value_decrby(struct fieldstat_instance *instance, int metric_id, long long decrment);
/**
 * Make the fieldstat instance affect.
 * @param instance The fieldstat instance.
 */
void fieldstat_instance_start(struct fieldstat_instance *instance);
/**
 * Passive output
 * @param instance The fieldstat instance.
 */
void fieldstat_passive_output(struct fieldstat_instance *instance);
/**
 * Register histogram metric
 * @param instance fieldstat instance
 * @param field_name  field name
 * @param tags        The tag array
 * @param n_tag       Size of tags
 * @param upper_inclusive_bounds The upper inclusive bound of histogram
 * @param lowest_trackable_value The smallest possible value to be put into the
 * histogram.
 * @param highest_trackable_value The largest possible value to be put into the
 * histogram.
 * @param significant_figures The level of precision for this histogram, i.e. the number
 * of figures in a decimal number that will be maintained.  E.g. a value of 3 will mean
 * the results from the histogram will be accurate up to the first three digits.  Must
 * be a value between 1 and 5 (inclusive).
 * @param output_window The of output data. 1: output the data in the time interval window, 0: output data all the time.
 * @return metric id: -1 is failed,  >= 0 is success
 */
int fieldstat_register_histogram(struct fieldstat_instance *instance, const char *field_name, const struct fieldstat_tag tags[], size_t n_tag, 
		const char *upper_inclusive_bounds, const long long lowest_trackable_value, long long highest_trackable_value, int significant_figures, int output_window);

/**
 * Register summary metric
 * @param instance fieldstat instance
 * @param field_name  field name
 * @param tags		  The tag array
 * @param n_tag       Size of tags
 * @param quantiles   the quantiles of summary output
 * @param lowest_trackable_value The smallest possible value to be put into the
 * histogram.
 * @param highest_trackable_value The largest possible value to be put into the
 * histogram.
 * @param significant_figures The level of precision for this histogram, i.e. the number
 * of figures in a decimal number that will be maintained.  E.g. a value of 3 will mean
 * the results from the histogram will be accurate up to the first three digits.  Must
 * be a value between 1 and 5 (inclusive).
 * @param output_window The of output data. 1: output the data in the time interval window, 0: output data all the time.
 * @return metric id: -1 is failed,  >= 0 is success
 */
int fieldstat_register_summary(struct fieldstat_instance *instance, const char *field_name, const struct fieldstat_tag tags[], size_t n_tag, 
		const char *quantiles, const long long lowest_trackable_value, long long highest_trackable_value, int significant_figures, int output_window);

/**
 * Register dynamic fieldstat instance.
 * @param instance fieldstat instance
 * @param name The instance name.
 * @param n_thread The count of thread.
 * @return  ptr dynamic_instance. NULL is failad. Not NULL is successd.
 */
struct fieldstat_dynamic_instance * fieldstat_dynamic_instance_new(const char *name, int n_thread);

/**
 * Enable output metric to line protocol server.
 * The operation should be executed in single-threaded fashion.
 * @param instance The fieldstat instance.
 * @param ip  Line protocol server ip.
 * @param port Line protocol server port. i.e., 80,8080
 * @return -1 is failed. 0 is success.
 */
int fieldstat_dynamic_set_line_protocol_server(struct fieldstat_dynamic_instance *instance, const char *ip, unsigned short port);
/**
 * Disable the dynamic metric output background thread.
 * The operation should be executed in single-threaded fashion.
 * @param instance The fieldstat instance.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_dynamic_disable_background_thread(struct fieldstat_dynamic_instance *instance);
/**
 * Set the dynamic metric output interval in milliseconds
 * The operation should be executed in single-threaded fashion.
 * @param seconds  In milliseconds.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_dynamic_set_output_interval(struct fieldstat_dynamic_instance *instance, int milliseconds);
/**
 * Dynamic metrics passive output
 * @param instance The fieldstat instance.
 */
void fieldstat_dynamic_passive_output(struct fieldstat_dynamic_instance *instance);
/**
 * Make the fieldstat dynamic instance affect.
 * @param instance The fieldstat instance.
 */
void fieldstat_dynamic_instance_start(struct fieldstat_dynamic_instance *instance);
/**
 * Register table. Max table num is 64. Max table columns is 64.
 * @param instance The fieldstat dynamic instance.
 * @param table_name  The table name.
 * @param column_name column name array.
 * @param column_type column type array. Type in [counter, gauge]
 * @param n_column size of column_type[] and column_name[]
 * @param out_column_ids output metric id in table row.
 * @return table id.  -1 is failed. >=0 is success.
 */
int fieldstat_register_dynamic_table(struct fieldstat_dynamic_instance *instance, const char *table_name, const char *column_name[], enum field_type column_type[], size_t n_column, unsigned int out_column_ids[]);
/**
 * fieldstat dynamic instance metric value increment operate. metirc type in[gauge, counter]
 * @param instance The fieldstat dynamic instance.
 * @param type The metric type.
 * @param field_name The metric field name.
 * @param value The increment value.
 * @param tags The tag array.
 * @param n_tag Size of tags[]
 * @param thread_id The thread id of the call.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_dynamic_metric_value_incrby(struct fieldstat_dynamic_instance *instance, enum field_type type, const char *field_name, long long value, const struct fieldstat_tag tags[], size_t n_tags, int thread_id);
/**
 * fieldstat dynamic instance metric value set operate. metirc type in[gauge, counter]
 * @param instance The fieldstat dynamic instance.
 * @param type The metric type.
 * @param field_name The metric field name.
 * @param value The set value.
 * @param tags The tag array.
 * @param n_tag Size of tags[]
 * @param thread_id The thread id of the call.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_dynamic_metric_value_set(struct fieldstat_dynamic_instance *instance, enum field_type type, const char *field_name, long long value, const struct fieldstat_tag tags[], size_t n_tags, int thread_id);
/**
 * fieldstat dynamic instance metric value decrment operate. metirc type in[gauge, counter]
 * @param instance The fieldstat dynamic instance.
 * @param type The metric type.
 * @param field_name The metric field name.
 * @param value The decrment value.
 * @param tags The tag array.
 * @param n_tag Size of tags[]
 * @param thread_id The thread id of the call.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_dynamic_metric_value_decrby(struct fieldstat_dynamic_instance *instance, enum field_type type, const char *field_name, long long value, const struct fieldstat_tag tags[], size_t n_tags, int thread_id);
/**
 * fieldstat dynamic instance table metric value incrment operate.
 * @param instance The fieldstat dynamic instance.
 * @param table_id The table id.
 * @param column_id The column id.
 * @param row_name The row name of table.
 * @param value The incrment value.
 * @param tags The tag array.
 * @param n_tag Size of tags[]
 * @param thread_id The thread id of the call.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_dynamic_table_metric_value_incrby(struct fieldstat_dynamic_instance *instance, int table_id, unsigned int column_id, const char *row_name, long long value, const struct fieldstat_tag tags[], size_t n_tags, int thread_id);
/**
 * fieldstat dynamic instance table metric value set operate.
 * @param instance The fieldstat dynamic instance.
 * @param table_id The table id.
 * @param column_id The column id.
 * @param row_name The row name of table.
 * @param value The set value.
 * @param tags The tag array.
 * @param n_tag Size of tags[]
 * @param thread_id The thread id of the call.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_dynamic_table_metric_value_set(struct fieldstat_dynamic_instance *instance, int table_id, unsigned int column_id, const char *row_name, long long value, const struct fieldstat_tag tags[], size_t n_tags, int thread_id);
/**
 * fieldstat dynamic instance table metric value decrment operate.
 * @param instance The fieldstat dynamic instance.
 * @param table_id The table id.
 * @param column_id The column id.
 * @param row_name The row name of table.
 * @param value The decrment value.
 * @param tags The tag array.
 * @param n_tag Size of tags[]
 * @param thread_id The thread id of the call.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_dynamic_table_metric_value_decrby(struct fieldstat_dynamic_instance *instance, int table_id, unsigned int column_id, const char *row_name, long long value, const struct fieldstat_tag tags[], size_t n_tags, int thread_id);
/**
 * fieldstat instance free.
 * @param instance The fieldstat instance need to free.
 */
void fieldstat_instance_free(struct fieldstat_instance *instance);
/**
 * fieldstat dynamic instance free.
 * @param instance The fieldstat dynamic instance need to free.
 */
void fieldstat_dynamic_instance_free(struct fieldstat_dynamic_instance *instance);
/**
 * fieldstat dynamic instance table metric value get operate.
 * @param instance The fieldstat dynamic instance.
 * @param table_id The table id.
 * @param column_id The column id.
 * @param row_name The row name of table.
 * @param value The set value.
 * @param tags The tag array.
 * @param n_tag Size of tags[]
 * @param thread_id The thread id of the call.
 * @return  long long value.
 */
long long fieldstat_dynamic_table_metric_value_get(struct fieldstat_dynamic_instance *instance, int table_id, unsigned int column_id, const char *row_name, const struct fieldstat_tag tags[], size_t n_tags, int thread_id);
/**
 * fieldstat dynamic instance metric value get operate. metirc type in[gauge, counter]
 * @param instance The fieldstat dynamic instance.
 * @param field_name The metric field name.
 * @param tags The tag array.
 * @param n_tag Size of tags[]
 * @param thread_id The thread id of the call.
 * @return long long value.
 */
long long fieldstat_dynamic_metric_value_get(struct fieldstat_dynamic_instance *instance, const char *field_name, const struct fieldstat_tag tags[], size_t n_tags, int thread_id);
/**
 * fieldstat set metric ratio parameter. 
 * @param instance The fieldstat instance.
 * @param metric_id  The metric id need to set ratio.
 * @param numerator_metric_id The metric id as numerator.
 * @param denominator_metric_id The metric id as denominator.
 * @return  -1 is failed. 0 is success.
 */
int fieldstat_set_metric_ratio_para(struct fieldstat_instance *instance, int metric_id, int numerator_metric_id, int denominator_metric_id, int output_scaling);

/**
 * fieldstat set metric invisible parameter. 
 * @param instance The fieldstat instance.
 * @param metric_id  The metric id need to set invisible.
 * @return  -1 is failed. 0 is success.
 */
int fieldstat_set_metric_invisible_para(struct fieldstat_instance *instance, int metric_id);


/**
 * fieldstat dynamic instance table row metric values incrment operate.
 * @param instance The fieldstat dynamic instance.
 * @param table_id The table id.
 * @param row_name The row name of table.
 * @param values The value array.
 * @param n_values Size of value array.
 * @param tags The tag array.
 * @param n_tags Size of tags[]
 * @param thread_id The thread id of the call.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_dynamic_table_row_metric_values_incrby(
    struct fieldstat_dynamic_instance *instance,
    int table_id,
    const char *row_name,
    long long values[],
    size_t n_values,
    const struct fieldstat_tag tags[],
    size_t n_tags,
    int thread_id);

/**
 * fieldstat dynamic instance table row metric values decrment operate.
 * @param instance The fieldstat dynamic instance.
 * @param table_id The table id.
 * @param row_name The row name of table.
 * @param values The value array.
 * @param n_values Size of value array.
 * @param tags The tag array.
 * @param n_tags Size of tags[]
 * @param thread_id The thread id of the call.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_dynamic_table_row_metric_values_decrby(
    struct fieldstat_dynamic_instance *instance,
    int table_id,
    const char *row_name,
    long long values[],
    size_t n_values,
    const struct fieldstat_tag tags[],
    size_t n_tags,
    int thread_id);

/**
 * fieldstat dynamic instance table row metric values set operate.
 * @param instance The fieldstat dynamic instance.
 * @param table_id The table id.
 * @param row_name The row name of table.
 * @param values The value array.
 * @param n_values Size of value array.
 * @param tags The tag array.
 * @param n_tags Size of tags[]
 * @param thread_id The thread id of the call.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_dynamic_table_row_metric_values_set(
    struct fieldstat_dynamic_instance *instance,
    int table_id,
    const char *row_name,
    long long values[],
    size_t n_values,
    const struct fieldstat_tag tags[],
    size_t n_tags,
    int thread_id);


/**
 * fieldstat dynamic read hash table item numbers of per thread.
 * @param instance The fieldstat dynamic instance.
 * @param thread_id The thread id of the call.
 * @return The number of items in the hash table of per thread.
 */
unsigned int fieldstat_dynamic_read_htable_item_cnt(
    struct fieldstat_dynamic_instance *instance,
    int thread_id);

/**
 * fieldstat dynamic read metrics numbers in per thread hash table.
 * @param instance The fieldstat dynamic instance.
 * @param thread_id The thread id of the call.
 * @return The number of metrics in the hash table of per thread.
 */
unsigned int fieldstat_dynamic_read_metrics_cnt(
	struct fieldstat_dynamic_instance *instance,
	int thread_id);


#ifdef __cplusplus
}
#endif
