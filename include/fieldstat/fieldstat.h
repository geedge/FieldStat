#pragma once
#include <stdio.h>
#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <stdbool.h>

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
	};
};

struct fieldstat;
struct fieldstat *fieldstat_new();
void fieldstat_free(struct fieldstat *instance);
// copy only registered cubes and metrics, not including cells. Used to new a instance of the same schema.
struct fieldstat *fieldstat_fork(const struct fieldstat *instance);

/*
 * let the configuration of target be the same as master, no matter what the configuration of target is.
 * the configurations will be kept as much as possible, like cells in the same cube will be kept, but the cells in different cubes will be deleted.
 * @ return FS_OK or FS_ERR_NULL_HANDLER 
*/
void fieldstat_calibrate(const struct fieldstat *master, struct fieldstat *replica);
/*
    * @brief add an cube to this instance. Cube represents an template with a user-defined set of cells and metrics.
    * @param cube_dimensions: This is the key of the cube. Can be NULL. Must be unique. cube_dimensions are ordered, which means that {"KEY": "123", "KEY2": "456"} and {"KEY2": "456", "KEY": "123"} are map to different cube.
    * @param n_dimension: number of field in dimension.
    * @param mode: sampling mode. Refer to enum sampling_mode.
    * @param max_n_cell: max number of samplings(cells) in each cube. When mode is TOPK, max_n_cell > 0, while in COMPREHENSIVE mode, max_n_cell can be 0, meaning that there is no limit.
    * @return cube id, if success; otherwise, return FS_ERR_NULL_HANDLER, or FS_ERR_INVALID_PARAM when (max_n_cell == 0 && mode == TOPK). return FS_ERR_INVALID_KEY when the cube_dimensions is not unique.
*/
int fieldstat_cube_create(struct fieldstat *instance, const struct field *cube_dimensions, size_t n_dimension);
int fieldstat_cube_set_sampling(struct fieldstat *instance, int cube_id, enum sampling_mode mode, int max_n_cell, int primary_metric_id);

/*
    * @brief Delete the cube of cube_id. All the cells and metrics are deleted. The cube_id may be reused by other new cubes. Increase the corresponding cube_version by 1.
    * @return FS_OK, FS_ERR_NULL_HANDLER or FS_ERR_INVALID_CUBE_ID
*/
int fieldstat_cube_destroy(struct fieldstat *instance, int cube_id);

/*
    * @brief add a metric to the cube of cube_id. One metric may be associated with different cells.
    * @param metric_name: name of the metric. Cannot be NULL. Must be unique.
    * @return metric id>=0 if success. If failed, return FS_ERR_NULL_HANDLER, FS_ERR_INVALID_KEY(when metric_name is not unique in this cube)
*/
int fieldstat_register_counter(struct fieldstat *instance, int cube_id, const char *metric_name);

/*
    * @brief refer to fieldstat_register_counter.
    * @param precision: the bigger, the larger memory consumption, while accuracy improved. Must be in [4, 18].
    * @return metric id if success. If failed, return FS_ERR_NULL_HANDLER, FS_ERR_INVALID_KEY(when metric_name is not unique in this cube), or FS_ERR_INVALID_PARAM(if precision not in range)
*/
int fieldstat_register_hll(struct fieldstat *instance, int cube_id, const char *metric_name, unsigned char precision);

/*
    * @brief refer to fieldstat_register_counter.
    * @param lowest_trackable_value: the lowest value that can be tracked (distinguishable from 0) by the histogram. Must be >= 1.
    * @param highest_trackable_value: the highest value to be tracked by the histogram. Must be >= 2 * lowest_trackable_value.
    * @param significant_figures: the precision of the histogram. Must be in [1, 5].
        * @return metric id if success. If failed, return FS_ERR_NULL_HANDLER, FS_ERR_INVALID_KEY(when metric_name is not unique in this cube), or FS_ERR_INVALID_PARAM(if any of the 3 params are out of range)
*/
int fieldstat_register_histogram(struct fieldstat *instance, int cube_id, const char *metric_name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures);

/*
    * @brief let the value of counter metric of cell_id increase by `increment`.
    * @param cube_id: cube id, previously returned by fieldstat_cube_create.
    * @param metric_id: metric id, previously returned by fieldstat_register_counter.
    * @param increment: increment of the counter metric. Can be negative.
    * @return FS_OK if success. FS_ERR_NULL_HANDLER, FS_ERR_INVALID_CUBE_ID, FS_ERR_INVALID_METRIC_ID if fail.
    *         FS_ERR_INVALID_PARAM when cube is topk, metric is primary metric, and increment is negative.
    *         FS_ERR_TOO_MANY_CELLS when the cube is full.
    *           In comprehensive mode, a full cube cannot be added any more cells.
    *           In topk mode, every increment matters, so even the cube is full, the cell can also replace the least frequent cell.
*/
int fieldstat_counter_incrby(struct fieldstat *instance, int cube_id, int metric_id, const struct field *cell_dimensions, size_t n_dimensions, long long increment);

/*
    * @brief let the value of counter metric equal to value. Other annotations refer to fieldstat_counter_incrby.
    * @return Refer to fieldstat_counter_incrby. What's more, be cautious to call this function if the metric is a primary metric of a topk cube,
    *   in such case, FS_ERR_INVALID_PARAM will be the output when the value is set to a smaller one(increment is negative).
*/
int fieldstat_counter_set(struct fieldstat *instance, int cube_id, int metric_id, const struct field *cell_dimensions, size_t n_dimensions, long long value);

/*
    * @brief add a key to the hll metric of cell_id. HLL approximates the number of distinct elements in a set of `key`s.
    * @param key: key of the hll metric. Cannot be NULL.
    * @param key_len: strlen(key).
    * @return FS_OK if success. FS_ERR_NULL_HANDLER, FS_ERR_INVALID_CUBE_ID, FS_ERR_INVALID_METRIC_ID if fail. 
    *         Since topk only support counter, FS_ERR_INVALID_PARAM is returned when the cube is topk.
*/
int fieldstat_hll_add(struct fieldstat *instance, int cube_id, int metric_id, const struct field *cell_dimensions, size_t n_dimensions, const char *key, size_t key_len);
int fieldstat_hll_add_field(struct fieldstat *instance, int cube_id, int metric_id, const struct field *cell_dimensions, size_t n_dimensions, const struct field *item, size_t item_len);


/* 
    * @brief Add a value to the histogram metric of cell_id. Histogram will record the distribution of the values.
             The value bigger than highest_trackable_value will be set to highest_trackable_value. The value less than lowest_trackable_value will be tried to record, and, if succeed, remains in the record as -inf(most of the time) or 0(if value == 0)
    * @param value: value of the histogram metric.
    * @return FS_OK if success. FS_ERR_NULL_HANDLER, FS_ERR_INVALID_CUBE_ID, FS_ERR_INVALID_METRIC_ID if fail.
    *         FS_ERR_INVALID_PARAM when value is less than 0, or the cube is topk.
*/
int fieldstat_histogram_record(struct fieldstat *instance, int cube_id, int metric_id, const struct field *cell_dimensions, size_t n_dimensions, long long value);

/*
    * @brief Delete all the cells, also the content of every metrics. The cube and metrics are not deleted. Increase cell_version by 1.
             Note that the cell record won't be deleted at once, they just seem to be deleted. The cell record will be deleted when they are not used since the last reset and until the next reset.
*/
void fieldstat_reset(struct fieldstat *instance);

/*
 @brief Merge the instance. The registered cubes and metrics are merged even if there are no cells added.
 @return 0 if success. return FS_ERR_INVALID_PARAM when the registered cubes or metrics between dest and src of the same keys has different types or configurations(like different primary metric).
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

void fieldstat_cell_get_metrics(const struct fieldstat *instance, int cube_id, const struct field_list *cell_dimensions, int **metric_id_out, size_t *n_metric_out);

// query the name of the metric, return NULL if metric_id is invalid.
const char *fieldstat_metric_get_name(const struct fieldstat *instance, int cube_id, int metric_id);

// query the type of the metric. return (enum metric_type)-1 if metric_id is invalid.
enum metric_type fieldstat_metric_get_type(const struct fieldstat *instance, int cube_id, int metric_id);

/*
    get the cell_dimensions added to cube when calling fieldstat_counter_incrby, fieldstat_counter_set, fieldstat_hll_add, fieldstat_histogram_record.
*/
void fieldstat_cube_get_cells(const struct fieldstat *instance, int cube_id, struct field_list **cell_dimensions, size_t *n_cell);

/*
    get the field of fieldstat_cube_create. User free them by calling fieldstat_field_list_arr_free(struct field_list *, 1)
    return NULL when ID is invalid.
*/
struct field_list *fieldstat_cube_get_dimension(const struct fieldstat *instance, int cube_id);

int fieldstat_cube_get_metric_id_by_name(const struct fieldstat *instance, int cube_id, const char *metric_name);

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