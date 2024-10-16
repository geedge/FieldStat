#pragma once
#include <stdio.h>
#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <stdbool.h>
#include <uuid/uuid.h>

#define FS_OK 0
#define FS_ERR_TOO_MANY_CELLS -1
#define FS_ERR_NULL_HANDLER -2
#define FS_ERR_INVALID_CUBE_ID -3
#define FS_ERR_INVALID_METRIC_ID -4
#define FS_ERR_INVALID_DIMENSION -5
#define FS_ERR_INVALID_PARAM -6
#define FS_ERR_INVALID_METRIC_NAME -7
#define FS_ERR_INVALID_METRIC_TYPE -8
#define FS_ERR_MAX_N_CELL_LESS_THAN_ZERO -9
#define FS_ERR_DIMENSION_ALREADY_EXISTS -10
#define FS_ERR_METRIC_NAME_ALREADY_EXISTS -11
#define FS_ERR_CUBE_SAMPLING_NOT_INITIALIZED -12
#define FS_ERR_OPERATION_NOT_SUPPORTED_FOR_PRIMARY_METRIC -13
#define FS_ERR_DIFFERENT_CONFIGURATION_FOR_SAME_CUBE -14

enum metric_type
{
	METRIC_TYPE_COUNTER,
	METRIC_TYPE_HLL,
	METRIC_TYPE_HISTOGRAM,
};

enum field_type
{
	FIELD_VALUE_INTEGER,
	FIELD_VALUE_DOUBLE,
	FIELD_VALUE_CSTRING,
	FIELD_VALUE_UUID,
};

enum sampling_mode {
    SAMPLING_MODE_COMPREHENSIVE,
    SAMPLING_MODE_TOPK,
    SAMPLING_MODE_TOP_CARDINALITY,
};

struct field {
	const char *key;
    enum field_type type;
	union{
		long long value_longlong;
		double value_double;
		const char *value_str;
        uuid_t value_uuid;
	};
};

struct histogram; 

struct fieldstat;
struct fieldstat *fieldstat_new();
void fieldstat_free(struct fieldstat *instance);
// copy only registered cubes and metrics, not including cells. Used to new a instance of the same cube and metric manifests,but no cells.
struct fieldstat *fieldstat_fork(const struct fieldstat *instance);

/*
 * let the configuration of target be the same as master, no matter what the configuration of target is.
 * the configurations will be kept as much as possible, like cells in the same cube will be kept, but the cells in different cubes will be deleted.
*/
void fieldstat_calibrate(const struct fieldstat *master, struct fieldstat *replica);
/*
    * @brief add an cube to this instance. Cube represents an template with a user-defined set of cells and metrics. `fieldstat_cube_set_sampling` must be called before adding any cells.
    * @param cube_dimensions: This is the key of the cube. Can be NULL. Must be unique. cube_dimensions are ordered, which means that {"KEY": "123", "KEY2": "456"} and {"KEY2": "456", "KEY": "123"} are map to different cube.
    * @param n_dimension: number of field in dimension.
    * @return cube id, if success; otherwise, return error codes.
*/
int fieldstat_cube_create(struct fieldstat *instance, const struct field *cube_dimensions, size_t n_dimension);
/*
    * @brief Set the sampling mode of the cube of cube_id. Refer to readme for more details.
    * @param mode: sampling mode. Refer to enum sampling_mode.
    * @param max_n_cell: max number of samplings(cells) in each cube. When mode is TOPK, max_n_cell > 0, while in COMPREHENSIVE mode, max_n_cell can be 0, meaning that there is no limit.
    * @return error codes.
* */
int fieldstat_cube_set_sampling(struct fieldstat *instance, int cube_id, enum sampling_mode mode, int max_n_cell, int primary_metric_id);

/*
    * @brief Delete the cube of cube_id. All the cells and metrics are deleted. The cube_id may be reused by other new cubes.
    * @return error codes.
*/
int fieldstat_cube_destroy(struct fieldstat *instance, int cube_id);

/*
    * @brief add a metric to the cube of cube_id. One metric may be associated with different cells.
    * @param metric_name: name of the metric. Cannot be NULL. Must be unique.
    * @return metric id>=0 if success. If failed return error codes.
*/
int fieldstat_register_counter(struct fieldstat *instance, int cube_id, const char *metric_name);

/*
    * @brief refer to fieldstat_register_counter.
    * @param precision: the bigger, the larger memory consumption, while accuracy improved. Must be in [4, 18].
    * @return metric id if success. If failed return error codes.
*/
int fieldstat_register_hll(struct fieldstat *instance, int cube_id, const char *metric_name, unsigned char precision);

/*
    * @brief refer to fieldstat_register_counter.
    * @param lowest_trackable_value: the lowest value that can be tracked (distinguishable from 0) by the histogram. Must be >= 1.
    * @param highest_trackable_value: the highest value to be tracked by the histogram. Must be >= 2 * lowest_trackable_value.
    * @param significant_figures: the precision of the histogram. Must be in [1, 5].
    * @return metric id if success. If failed return error codes.
*/
int fieldstat_register_histogram(struct fieldstat *instance, int cube_id, const char *metric_name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures);

/*
    * @brief let the value of counter metric of cell_id increase by `increment`.
    * @param cube_id: cube id, previously returned by fieldstat_cube_create.
    * @param metric_id: metric id, previously returned by fieldstat_register_counter.
    * @param increment: increment of the counter metric. Can be negative. But if the metric is primary metric of a topk cube, increment must be positive.
    * @return Error codes.
*/
int fieldstat_counter_incrby(struct fieldstat *instance, int cube_id, int metric_id, const struct field *cell_dimensions, size_t n_dimensions, long long increment);

/* 
    * @brief refer to fieldstat_counter_incrby. The difference is that the increments of multiple metrics are added in batch. It is faster than calling fieldstat_counter_incrby multiple times.
    * @param metric_ids: metric ids, previously returned by fieldstat_register_counter, must be counters.
    * @param increments: increments of the counter metrics. The length of increments must be equal to n_metrics. Position i of increments is the increment of metric_ids[i].
    * @param n_metrics: the length of metric_ids and increments.
    * @return Error codes. Or just assert fail if the parameters are invalid.
*/
int fieldstat_counter_incrby_batch(struct fieldstat *instance, int cube_id, const struct field *cell_dimensions, size_t n_dimensions, const int metric_ids[],  const long long increments[], size_t n_metrics);

/*
    * @brief let the value of counter metric equal to value. Must be called on non-primary metric. Other annotations refer to fieldstat_counter_incrby.
*/
int fieldstat_counter_set(struct fieldstat *instance, int cube_id, int metric_id, const struct field *cell_dimensions, size_t n_dimensions, long long value);

/*
    * @brief add a key to the hll metric of cell_id. HLL approximates the number of distinct elements in a set of `key`s.
    * @param key: key of the hll metric. Cannot be NULL.
    * @param key_len: strlen(key).
    * @return Error codes.
*/
int fieldstat_hll_add(struct fieldstat *instance, int cube_id, int metric_id, const struct field *cell_dimensions, size_t n_dimensions, const char *key, size_t key_len);

/*
    * @brief refer to fieldstat_hll_add. The key is a field.
    * @param item: the field of the key.
    * @param item_len: the length of the field.
    * @return Error codes.
*/
int fieldstat_hll_add_fields(struct fieldstat *instance, int cube_id, int metric_id, const struct field *cell_dimensions, size_t n_dimensions, const struct field **items, size_t n_items);


/* 
    * @brief Add a value to the histogram metric of cell_id. Histogram will record the distribution of the values.
             The value bigger than highest_trackable_value will be set to highest_trackable_value. The value less than lowest_trackable_value will be tried to record, and, if succeed, remains in the record as -inf(most of the time) or 0(if value == 0)
    * @param value: value of the histogram metric.
    * @return Error codes.
*/
int fieldstat_histogram_record(struct fieldstat *instance, int cube_id, int metric_id, const struct field *cell_dimensions, size_t n_dimensions, long long value);

/*
struct histogram provides a way to update the histogram metric in batch created and updated by user.

  Example:
    struct histogram *histogram = fieldstat_histogram_fork(instance, cube_id, metric_id);
    for (int i = 0; i < many_packet_length; i++) {
        histogram_record(histogram, packet_length[i]);
    }

    fieldstat_histogram_merge(instance, cube_id, metric_id, cell_dimensions, n_dimensions, histogram);
    histogram_reset(histogram);
    
    ...
    // after many times of histogram_record
    histogram_free(histogram);
*/
struct histogram *fieldstat_histogram_fork(const struct fieldstat *instance, int cube_id, int metric_id);
void histogram_free(struct histogram *histogram);
void histogram_reset(struct histogram *histogram);
int fieldstat_histogram_merge(struct fieldstat *instance, int cube_id, int metric_id, const struct field *cell_dimensions, size_t n_dimensions, const struct histogram *src);
int histogram_record(struct histogram *histogram, long long value);

/*
    * @brief Delete all the cells, also the content of every metrics. The cube and metrics are not deleted.
             Note that the cell record won't be deleted at once, they just seem to be deleted. The cell record will be deleted when they are not used since the last reset and until the next reset.
*/
void fieldstat_reset(struct fieldstat *instance);

/*
 @brief Merge the instance. The registered cubes and metrics are merged even if there are no cells added. 
        Will return error if the configuration of the same cube is different, including the sampling mode configurations, the primary metric configurations, and the registered metrics.
 @return Error code.
*/
int fieldstat_merge(struct fieldstat *instance, const struct fieldstat *src);

/* -------------------------------------------------------------------------- */
/*                                    query                                   */
/* -------------------------------------------------------------------------- */

struct field_list
{
    struct field *field;
    size_t n_field;
};

/* 
    * @brief Get all the registered cubes.
    * @param cube_ids: the cube ids. The caller should free it. Use it like: int *cube_ids; fieldstat_get_cubes(instance, &cube_ids, &n_cube); for (int i = 0; i < n_cube; i++) { printf("%d\n", cube_ids[i]); } free(cube_ids);
    * @param n_cube: Length of cube_ids.
*/
void fieldstat_get_cubes(const struct fieldstat *instance, int **cube_ids, int *n_cube);

/*
    * @brief Get all the metrics in a cell. The metrics are added to cell through fieldstat_counter_incrby, fieldstat_counter_set, fieldstat_hll_add, fieldstat_histogram_record.
    * @param cube_id: cube id, previously returned by fieldstat_get_cubes.
    * @param cell_dimensions: The dimension specifying the cell. Can be queried by `fieldstat_cube_get_cells`
    * @param metric_id_out: the metric ids. The caller should free it.
*/
void fieldstat_cell_get_metrics(const struct fieldstat *instance, int cube_id, const struct field_list *cell_dimensions, int **metric_id_out, size_t *n_metric_out);

// query the name of the metric, return NULL if metric_id is invalid.
const char *fieldstat_metric_get_name(const struct fieldstat *instance, int cube_id, int metric_id);

// query the metric id by name. return error codes if metric_name is invalid.
int fieldstat_cube_get_metric_id_by_name(const struct fieldstat *instance, int cube_id, const char *metric_name);

// query the type of the metric. return (enum metric_type)-1 if metric_id is invalid.
enum metric_type fieldstat_metric_get_type(const struct fieldstat *instance, int cube_id, int metric_id);

/*
    get the cell_dimensions added to cube when calling fieldstat_counter_incrby, fieldstat_counter_set, fieldstat_hll_add, fieldstat_histogram_record.
*/
void fieldstat_cube_get_cells(const struct fieldstat *instance, int cube_id, struct field_list **cell_dimensions, size_t *n_cell);

/*
    get the dimension given in fieldstat_cube_create. User free them by calling fieldstat_field_list_arr_free(struct field_list *, 1)
    return NULL when ID is invalid.
*/
struct field_list *fieldstat_cube_get_dimension(const struct fieldstat *instance, int cube_id);

/*
    return a cube id corresponding to `cube_dimensions`. FS_ERR_INVALID_KEY is returned if the cube is not found.
*/
int fieldstat_find_cube(const struct fieldstat *instance, const struct field *cube_dimensions, size_t n_dimensions);

/* 
    get the cell numbers in a cube. Return FS_ERR_INVALID_CUBE_ID if cube_id is invalid.
*/
int fieldstat_cube_get_cell_number(const struct fieldstat *instance, int cube_id);

/*
    * @brief Get the value of a metric of a cell.
    * @param cube_id: cube id, previously returned by fieldstat_get_cubes.
    * @param metric_id: metric id, previously returned by fieldstat_get_max_metric_id.
    * @param cell_dimensions: previously returned by fieldstat_get_cells_used_by_metric.
    * @param value_out: the value of the metric. If the cell is not found, *value_out is set to 0.
    * @return FS_OK if success. FS_ERR_NULL_HANDLER, FS_ERR_INVALID_CUBE_ID, FS_ERR_INVALID_METRIC_ID if fail.
*/
int fieldstat_counter_get(const struct fieldstat *instance, int cube_id, const struct field_list *cell_dimensions, int metric_id, long long *value);

/*
    @brief Get an approximate count of the number of distinct elements in the cell. Other information refer to fieldstat_counter_get.
    @return >= 0 if success. FS_ERR_INVALID_PARAM if precision is invalid.
*/
int fieldstat_hll_get(const struct fieldstat *instance, int cube_id, const struct field_list *cell_dimensions, int metric_id, double *value);
long long fieldstat_histogram_value_at_percentile(const struct fieldstat *instance, int cube_id, const struct field_list *cell_dimensions, int metric_id, double percentile);
long long fieldstat_histogram_count_le_value(const struct fieldstat *instance, int cube_id, const struct field_list *cell_dimensions, int metric_id, long long value);

// get the base 64 encoded string of the serialized blob of a cell
void fieldstat_metric_get_serialization_as_base64(const struct fieldstat *instance, int cube_id, int metric_id, const struct field_list *cell_dimensions, char **blob, size_t *blob_size);

void fieldstat_field_list_arr_free(struct field_list *field_lists, size_t n_field_list);


#ifdef __cplusplus
}
#endif