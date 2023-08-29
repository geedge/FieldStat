#pragma once
#include <stdio.h>
#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <stdbool.h>

#define EXPORT_VER 1 // increase by 1 when the serialization format is changed.

enum metric_type
{
	METRIC_TYPE_COUNTER,
	METRIC_TYPE_HLL,
	METRIC_TYPE_HISTOGRAM,
};
//todo:
// poc
// tracing
// 性能
// 配置
// stream 
// 怎么和C 对接
// 类型系统？


enum fs_tag_type
{
	TAG_INTEGER,
	TAG_DOUBLE,
	TAG_CSTRING,
};

enum sampling_mode {
    SAMPLING_MODE_COMPREHENSIVE,
    SAMPLING_MODE_TOPK,
};

enum counter_mode {
    COUNTER_MERGE_BY_SUM,
    COUNTER_MERGE_BY_MAX,
    COUNTER_MERGE_BY_MIN,
};

struct fieldstat_tag {
	const char *key;
    enum fs_tag_type type;
	union{
		long long value_longlong;
		double value_double;
		const char *value_str;
	};
};


struct fieldstat;
struct fieldstat *fieldstat_new();
void fieldstat_free(struct fieldstat *instance);
// copy only registered cubes and metrics, not including cells.
struct fieldstat *fieldstat_dup(const struct fieldstat *instance);
/*
    * @brief add an cube to this instance. Cube represents an template with a user-defined set of cells and metrics.
    * @param shared_tags: tags that are shared by all cubes in this instance. This is the key of the cube. Cannot be NULL. Must be unique. shared_tags are ordered.
    * @param n_tag: number of shared tags.
    * @param mode: sampling mode. Refer to enum sampling_mode.
    * @param max_n_cell: max number of cells in each cube. When mode is TOPK, max_n_cell > 0, while in COMPREHENSIVE mode, max_n_cell can be 0, meaning that there is no limit.
    * @return cube id, if success; otherwise, return -1. Fail only when `instance` == NULL. the uniqueness of shared_tags is not checked, user should guarantee that shared_tags are unique.
*/
int fieldstat_register_cube(struct fieldstat *instance, const struct fieldstat_tag *shared_tags, size_t n_tag, enum sampling_mode mode, size_t max_n_cell);
/*
    * @brief Delete the cube of cube_id. All the cells and metrics are deleted. The cube_id may be reused by other new cubes. Increase the corresponding cube_version by 1.
    * @return 0 if success. -1 if cube_id is invalid.
*/
int fieldstat_unregister_cube(struct fieldstat *instance, int cube_id);

/*
    * @brief get the cube_version of the cube of cube_id.
    * @return cube_version if success. -1 if instance is NULL. -2 if cube_id is -1. -3 if the cube_id is invalid(never registered for even once)
*/
long long fieldstat_get_cube_version(const struct fieldstat *instance, int cube_id);
/*
    * @brief add a metric to the cube of cube_id. One metric may have multiple sub-metric that are associated with different cells.
    * @param cube_id: cube id, previously returned by fieldstat_register_cube.
    * @param field_name: name of the metric. Cannot be NULL. Must be unique in this cube.
    * @param counter_mode: merge method of the metric. Refer to enum counter_mode.
    * @return metric id, if success; otherwise, return -1. Fail when cube is not registered, or the parameter is invalid.
*/
int fieldstat_register_counter(struct fieldstat *instance, int cube_id, const char *field_name, enum counter_mode mode);

/*
    * @brief add a metric to the cube of cube_id. One metric may have multiple sub-metric that are associated with different cells. other parameters are the same as fieldstat_register_counter.
    * @param precision: the bigger, the larger memory consumption, while accuracy improved. Must be in [4, 18].
    * @return metric id, if success; otherwise, return -1. Fail when cube is not registered, or the parameter is invalid.
*/
int fieldstat_register_hll(struct fieldstat *instance, int cube_id, const char *field_name, unsigned char precision);

/*
    * @brief add a metric to the cube of cube_id. One metric may have multiple sub-metric that are associated with different cells. other parameters are the same as fieldstat_register_counter.
    * @param lowest_trackable_value: the lowest value that can be tracked (distinguishable from 0) by the histogram. Must be >= 1.
    * @param highest_trackable_value: the highest value to be tracked by the histogram. Must be >= 2 * lowest_trackable_value.
    * @param significant_figures: the precision of the histogram. Must be in [1, 5].
    * @return metric id, if success; otherwise, return -1. Fail when cube is not registered, or the parameter is invalid.
*/
int fieldstat_register_hist(struct fieldstat *instance, int cube_id, const char *field_name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures);

/*
    * @brief add a cell to the cube of cube_id. One cell represents a set of tags. In topk sampling mode, this function will update the cell ranking every time it is called.
    * @param cube_id: cube id, previously returned by fieldstat_register_cube.
    * @param tags: tags of the cell. Can be NULL, and all NULL tags are mapped to a single cell. Tags are ordered, which means that {"TAG_KEY": "123", "TAG_KEY2": "456"} and {"TAG_KEY2": "456", "TAG_KEY": "123"} are map to different cell.
    * @param n_tag: number of tags.
    * @param increment: the primary metric increment of this cell. For example, if the primary metric is "count", then increment is the count of this cell.
    *                   in comprehensive sampling mode, this parameter is ignored.
    * @return cell id >= 0, if success; otherwise, return -2 when cube is not registered, instance are NULL, or (increment < 0 and mode == topk).  \
    *         return -1 when: mode is (SAMPLING_MODE_COMPREHENSIVE) and cell number >= (max_n_cell) ; or SAMPLING_MODE_TOPK and cell already exists.
    *         in topk sampling mode, the same tags may map to different cell ids.
*/
int fieldstat_cube_add(struct fieldstat *instance, int cube_id, const struct fieldstat_tag *tags, size_t n_tag, long long increment);

/*
    * @brief Delete the cell added by fieldstat_cube_add. Increase the cell_version by 1. Also, delete all the metric records of this cell. The cell id will not be reused. Does not support topk sampling mode.
    * @param refer to fieldstat_cube_add.
    * @return the deleted cell id >= 0, if success; otherwise, return -2 when cube is not registered, instance is NULL.  \
    *         return -1 when cell is not found. return -3 when cube is in topk sampling mode.
*/
int fieldstat_cube_remove(struct fieldstat *instance, int cube_id, const struct fieldstat_tag *tags, size_t n_tag);

/*
    * @brief let the value of counter metric of cell_id increase by increment.
    * @param cube_id: cube id, previously returned by fieldstat_register_cube.
    * @param metric_id: metric id, previously returned by fieldstat_register_counter.
    * @param cell_id: cell id, previously returned by fieldstat_cube_add.
    * @param increment: increment of the counter metric. Can be negative.
    * @return 0 if success. -1 if cube_id is invalid. -2 if metric_id is invalid, or the metric is not a METRIC_TYPE_COUNTER. -3 if cell_id is invalid.
*/
int fieldstat_counter_incrby(struct fieldstat *instance, int cube_id, int metric_id, int cell_id, long long increment);

/*
    * @brief let the value of counter metric equal to value. Other annotations refer to fieldstat_counter_incrby.
    * @return 0 if success. -1 if cube_id is invalid. -2 if metric_id is invalid, or the metric is not a METRIC_TYPE_COUNTER. -3 if cell_id is invalid.
*/
int fieldstat_counter_set(struct fieldstat *instance, int cube_id, int metric_id, int cell_id, long long value);

/*
    * @brief add a key to the hll metric of cell_id. HLL approximates the number of distinct elements in a set of `key`s.
    * @param key: key of the hll metric. Cannot be NULL.
    * @param key_len: strlen(key).
    * @return 0 if success. -1 if cube_id is invalid. -2 if metric_id is invalid, or the metric is not a METRIC_TYPE_HLL. -3 if cell_id is invalid.
*/
int fieldstat_hll_add(struct fieldstat *instance, int cube_id, int metric_id, int cell_id, const char *key, size_t key_len);

/* 
    * @brief Add a value to the histogram metric of cell_id. Histogram will record the distribution of the values.
    * @param value: value of the histogram metric.
    * @return 0 if success. -1 if cube_id is invalid. -2 if metric_id is invalid, or the metric is not a METRIC_TYPE_HISTOGRAM. -3 if cell_id is invalid.
*/
int fieldstat_hist_record(struct fieldstat *instance, int cube_id, int metric_id, int cell_id, long long value);

/*
    * @brief Delete all the cells, also the content of every metrics. The cube and metrics are not deleted. Increase cell_version by 1.
*/
void fieldstat_reset(struct fieldstat *instance);
unsigned long fieldstat_get_cell_version(const struct fieldstat *instance);

/*
 @brief Merge the instance. The registered cubes and metrics are merged even if there are no cells added.
 @return 0 if success. -1 if failed. Failed when the registered cubes or metrics between dest and src of the same keys has different types or configurations.
*/
int fieldstat_merge(struct fieldstat *instance, struct fieldstat *src);
struct fieldstat *fieldstat_deserialize(const char *blob, size_t blob_size);
int fieldstat_serialize(const struct fieldstat *instance, char **blob_out, size_t *blob_size_out);

/* -------------------------------------------------------------------------- */
/*                                    query                                   */
/* -------------------------------------------------------------------------- */

struct fieldstat_tag_list
{
    struct fieldstat_tag *tag;
    size_t n_tag;
};

/* 
    * @brief Get all the registered cubes.
    * @param cube_ids: the cube ids. The caller should free it. Use it like: int *cube_ids; fieldstat_get_cubes(instance, &cube_ids, &n_cube); for (int i = 0; i < n_cube; i++) { printf("%d\n", cube_ids[i]); } free(cube_ids);
    * @param n_cube: Length of cube_ids.
*/
void fieldstat_get_cubes(const struct fieldstat *instance, int **cube_ids, int *n_cube);

/*
    * @brief Get all the registered metrics of a cube.
    * @param cube_id: cube id, previously returned by fieldstat_get_cubes.
    * @param metric_ids: the metric ids. The caller should free it. Use it like: int max_id = fieldstat_get_max_metric_id(instance, cube_id); for (int metric_id = 0; metric_id <= max_id; metric_id++) {// do something }
    * @param n_metric: Length of metric_ids.
    * @return -1 is returned if cube_id is invalid, or no metrics are registered.
*/
int fieldstat_get_max_metric_id(const struct fieldstat *instance, int cube_id);

// query the name of the metric, return NULL if cube_id or metric_id is invalid.
const char *fieldstat_get_metric_name(const struct fieldstat *instance, int cube_id, int metric_id);

// query the type of the metric. return -1 if cube_id or metric_id is invalid.
enum metric_type fieldstat_get_metric_type(const struct fieldstat *instance, int cube_id, int metric_id);

/*
    * @brief Get all the cells in a cube and their tags, added by fieldstat_cube_add.
    * @param cell_ids cell_ids[i] is the cell id of i-th cell. User free them.
    * @param tag_list tag_list[i] is the tag list of i-th cell. User free them by calling fieldstat_tag_list_arr_free.
    * @param n_cell: Length of cell_ids and tag_list.
*/
void fieldstat_cube_read_cell(const struct fieldstat *instance, int cube_id,
    int **cell_ids, struct fieldstat_tag_list **tag_list, size_t *n_cell);

/*
    * @brief Get all the cells in a cube and their tags in a specific metric. Different metrics may have different cells.
    * @param metric_id: metric id, previously returned by fieldstat_get_max_metric_id.
    * @param cell_ids cell_ids[i] is the cell id of i-th cell. User free them.
    * @param tag_list tag_list[i] is the tag list of i-th cell. User free them by calling fieldstat_tag_list_arr_free.
    * @param n_cell: Length of cell_ids and tag_list.
*/
void fieldstat_get_cells(const struct fieldstat *instance, int cube_id, int metric_id,
    int **cell_ids, struct fieldstat_tag_list **tag_list, size_t *n_cell);
    

/*
    get the shared tag of fieldstat_register_cube. User free them by calling fieldstat_tag_list_arr_free(struct fieldstat_tag_list *, 1)
*/
struct fieldstat_tag_list *fieldstat_get_shared_tags(const struct fieldstat *instance, int cube_id);

/* 
    get the parameter max_n_cell of fieldstat_register_cube
*/
int fieldstat_get_max_cell_num(const struct fieldstat *instance, int cube_id);

/*
    * @brief Get the value of a metric of a cell.
    * @param cube_id: cube id, previously returned by fieldstat_get_cubes.
    * @param metric_id: metric id, previously returned by fieldstat_get_max_metric_id.
    * @param cell_id: cell id, previously returned by fieldstat_get_cells.
    * @return -1 if cube_id or metric_id or cell_id is invalid.
*/
long long fieldstat_counter_get(const struct fieldstat *instance, int cube_id, int metric_id, int cell_id);
double fieldstat_hll_get(const struct fieldstat *instance, int cube_id, int metric_id, int cell_id);
long long fieldstat_hist_value_at_percentile(const struct fieldstat *instance, int cube_id, int metric_id, int cell_id, double percentile);
long long fieldstat_hist_count_le_value(const struct fieldstat *instance, int cube_id, int metric_id, int cell_id, long long value);

// get the base 64 encoded string of the serialized blob of a cell
void fieldstat_get_serialized_blob(const struct fieldstat *instance, int cube_id, int metric_id, int cell_id, char **blob, size_t *blob_size);

void fieldstat_tag_list_arr_free(struct fieldstat_tag_list *tag_list, size_t n_cell);

#ifdef __cplusplus
}
#endif