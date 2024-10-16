#pragma once
#include <stddef.h>
#include "fieldstat.h" // for fieldstat_tag

#ifdef __cplusplus
extern "C"
{
#endif

struct fieldstat_easy;

/* 
 * new a fieldstat_easy instance.
    * @param max_thread_num: max thread number of this instance.
    * @param name: name of this instance. Will appear in output json. Can be NULL.
    * @param dimensions: dimensions of this instance. Will appear in output json. Can be NULL.
*/
struct fieldstat_easy *fieldstat_easy_new(int max_thread_num, const char *name, const struct field *global_dimensions, size_t n_dimension);
/*
    * free a fieldstat_easy instance.
*/
void fieldstat_easy_free(struct fieldstat_easy *fse);
/*
    * enable auto output. both data of accumulated and delta will be output.
    * @param output_path: output file path. Should be identical to the one in python config.
    * @param interval_second: output interval in second.
    * @return: 0 if success, -1 if failed to open file. -2 if the output is already enabled.
*/
int fieldstat_easy_enable_auto_output(struct fieldstat_easy *pthis, const char *output_path, int interval_second);

/*
    * @brief add a metric to the cube of cube_id. One metric may be associated with different cells.
    * @param metric_name: name of the metric. Cannot be NULL. Must be unique.
    * @return metric id>=0 if success. If failed, return FS_ERR_NULL_HANDLER, FS_ERR_INVALID_KEY(when metric_name is not unique in this cube).
    * For the error code, see fieldstat.h
*/
int fieldstat_easy_register_counter(struct fieldstat_easy *fse, const char *name);
/*
    * @brief refer to fieldstat_easy_register_counter.
    * @param lowest_trackable_value: the lowest value that can be tracked (distinguishable from 0) by the histogram. Must be >= 1.
    * @param highest_trackable_value: the highest value to be tracked by the histogram. Must be >= 2 * lowest_trackable_value.
    * @param significant_figures: the precision of the histogram. Must be in [1, 5].
    * @return metric id if success. If failed, return FS_ERR_NULL_HANDLER, FS_ERR_INVALID_KEY(when metric_name is not unique in this cube), or FS_ERR_INVALID_PARAM(if any of the 3 params are out of range)
    * For the error code, see fieldstat.h
*/
int fieldstat_easy_register_histogram(struct fieldstat_easy *fse, const char *name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures);
/*
    * Output the accumulated data to a json string.
    * @param buff: output buffer. User should free it after use.
*/
void fieldstat_easy_output(struct fieldstat_easy *fse, char **buff, size_t *buff_len);
void fieldstat_easy_output_array(struct fieldstat_easy *fse, char ***json_objects, size_t *n_object);
/*
    Call fieldstat_easy_output_array and then reset the instance. 
    Will fail and return -1 if fieldstat_easy_enable_auto_output is called, since the behavior is hard to define. Return 0 if success.
    The thread will be blocked until output is finished.
*/
int fieldstat_easy_output_array_and_reset(struct fieldstat_easy *fse, char ***json_objects, size_t *n_object);
/*
    * @brief let the value of counter metric of dimensions increase by `increment`.
    * @param thread_id: thread id. Must be in [0, max_thread_num).
    * @param metric_id: metric id, previously returned by fieldstat_easy_register_counter.
    * @param increment: increment of the counter metric. Can be negative.
    * return -1 also when the thread_id is out of range.FS_ERR_INVALID_METRIC_ID metric_id is not registered.
*/

int fieldstat_easy_counter_incrby(struct fieldstat_easy *fse, int thread_id, int metric_id, const struct field *dimensions, size_t n_dimensions, long long increment);
int fieldstat_easy_counter_incrby_batch(struct fieldstat_easy *fse, int thread_id, int metric_ids[], const struct field *dimensions, size_t n_dimensions, const long long *increments, size_t n_metrics);

/*
    * @brief let the value of counter metric of dimensions equal to `value`.
    *    for other notes, see fieldstat_easy_counter_incrby.
    * The value will be output by summing each ones in different threads, exactly the same as values set by fieldstat_easy_counter_incrby.
*/
int fieldstat_easy_counter_set(struct fieldstat_easy *fse, int thread_id, int metric_id, const struct field *dimensions, size_t n_dimensions, long long value);
/* 
    * @brief Add a value to the histogram metric of cell_id. Histogram will record the distribution of the values.
             The value bigger than highest_trackable_value will be set to highest_trackable_value. The value less than lowest_trackable_value will be tried to record, and, if succeed, remains in the record as -inf(most of the time) or 0(if value == 0)
    * @param value: value of the histogram metric.
    * @return FS_OK if success. FS_ERR_NULL_HANDLER, FS_ERR_INVALID_CUBE_ID, FS_ERR_INVALID_METRIC_ID if fail.
    * return -1 also when the thread_id is out of range.
*/
int fieldstat_easy_histogram_record(struct fieldstat_easy *fse, int thread_id, int metric_id, const struct field *dimensions, size_t n_dimensions, long long value);

#ifdef __cplusplus
}
#endif
