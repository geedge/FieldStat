
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "cjson/cJSON.h"
#include "uthash.h"
#include "hdr/hdr_histogram.h"

#include "fieldstat.h"
#include "metrics/metric.h"
#include "cube.h"
#include "metric_manifest.h"

struct fieldstat {
    struct cube_manager *cube_manager;
};

struct histogram {
    struct hdr_histogram *histogram;
};


/* -------------------------------------------------------------------------- */
/*                                  fieldstat                                 */
/* -------------------------------------------------------------------------- */

struct fieldstat *fieldstat_new()
{
    struct fieldstat *instance = calloc(1, sizeof(struct fieldstat));

    instance->cube_manager = cube_manager_new();

    return instance;
}

void fieldstat_free(struct fieldstat *instance)
{
    if (instance == NULL) {
        return;
    }

    cube_manager_free(instance->cube_manager);

    free(instance);
}

void fieldstat_reset(struct fieldstat *instance)
{
    if (instance == NULL) {
        return;
    }
    cube_manager_reset(instance->cube_manager);
}

int fieldstat_cube_destroy(struct fieldstat *instance, int cube_id)
{
    struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }

    cube_manager_delete(instance->cube_manager, cube);

    return FS_OK;
}

/* -------------------------------------------------------------------------- */
/*                                    cube                                    */
/* -------------------------------------------------------------------------- */

// cppcheck-suppress [constParameterPointer, unmatchedSuppression]
int fieldstat_cube_set_sampling(struct fieldstat *instance, int cube_id, enum sampling_mode mode, int max_n_cell, int primary_metric_id) {
    if (max_n_cell <= 0) {
        if (mode != SAMPLING_MODE_COMPREHENSIVE) {
            return FS_ERR_MAX_N_CELL_LESS_THAN_ZERO;
        } else {
            max_n_cell = INT32_MAX;
        }
    }

    struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    
    return cube_set_sampling(cube, mode, max_n_cell, primary_metric_id);
}

int fieldstat_cube_create(struct fieldstat *instance, const struct field *cube_dimensions, size_t n_dimension)
{
    if (instance == NULL) {
        return  FS_ERR_NULL_HANDLER;
    }

    if (n_dimension == 0 || cube_dimensions == NULL) {
        cube_dimensions = NULL;
        n_dimension = 0;
    }

    struct cube *cube = cube_new(cube_dimensions, n_dimension);
    int ret = cube_manager_add(instance->cube_manager, cube);
    if (ret < 0) {
        cube_free(cube);
        return FS_ERR_DIMENSION_ALREADY_EXISTS;
    }

    return ret; //ret is the cube_id
}

/* -------------------------------------------------------------------------- */
/*                               metric register                              */
/* -------------------------------------------------------------------------- */
// cppcheck-suppress [constParameterPointer, unmatchedSuppression]
int fieldstat_register_counter(struct fieldstat *instance, int cube_id, const char *metric_name)
{
    struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    return cube_register_counter(cube, metric_name);
}

// cppcheck-suppress [constParameterPointer, unmatchedSuppression]
int fieldstat_register_hll(struct fieldstat *instance, int cube_id, const char *metric_name, unsigned char precision)
{
    struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    return cube_register_hll(cube, metric_name, precision);
}

// cppcheck-suppress [constParameterPointer, unmatchedSuppression]
int fieldstat_register_histogram(struct fieldstat *instance, int cube_id, const char *metric_name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures)
{
    struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    return cube_register_hist(cube, metric_name, lowest_trackable_value, highest_trackable_value, significant_figures);
}

/* -------------------------------------------------------------------------- */
/*                               metric operation                              */
/* -------------------------------------------------------------------------- */

// cppcheck-suppress [constParameterPointer, unmatchedSuppression]
int fieldstat_counter_incrby(struct fieldstat *instance, int cube_id, int metric_id, const struct field *cell_dimensions, size_t n_dimensions, long long increment)
{
    struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }

    return cube_counter_incrby(cube, metric_id, cell_dimensions, n_dimensions, increment);
}

// cppcheck-suppress [constParameterPointer, unmatchedSuppression]
int fieldstat_counter_incrby_batch(struct fieldstat *instance, int cube_id, const struct field *cell_dimensions, size_t n_dimensions, const int metric_ids[],  const long long increments[], size_t n_metrics)
{
    struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }

    return cube_counter_incrby_batch(cube, metric_ids, cell_dimensions, n_dimensions, increments, n_metrics);
}

// cppcheck-suppress [constParameterPointer, unmatchedSuppression]
int fieldstat_counter_set(struct fieldstat *instance, int cube_id, int metric_id, const struct field *cell_dimensions, size_t n_dimensions, long long value)
{
    struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }

    return cube_counter_set(cube, metric_id, cell_dimensions, n_dimensions, value);
}

// cppcheck-suppress [constParameterPointer, unmatchedSuppression]
int fieldstat_hll_add(struct fieldstat *instance, int cube_id, int metric_id, const struct field *cell_dimensions, size_t n_dimensions, const char *key, size_t key_len)
{
    struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }

    return cube_hll_add(cube, metric_id, cell_dimensions, n_dimensions, key, key_len);
}

// cppcheck-suppress [constParameterPointer, unmatchedSuppression]
int fieldstat_hll_add_fields(struct fieldstat *instance, int cube_id, int metric_id, const struct field *cell_dimensions, size_t n_dimensions, const struct field **items, size_t n_items)
{
    struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }

    return cube_hll_add_field(cube, metric_id, cell_dimensions, n_dimensions, items, n_items);
}

// cppcheck-suppress [constParameterPointer, unmatchedSuppression]
int fieldstat_histogram_record(struct fieldstat *instance, int cube_id, int metric_id, const struct field *cell_dimensions, size_t n_dimensions, long long value)
{
    struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }

    return cube_histogram_record(cube, metric_id, cell_dimensions, n_dimensions, value);
}

int fieldstat_merge(struct fieldstat *instance, const struct fieldstat *src)
{
    if (instance == NULL || src == NULL) {
        return FS_ERR_NULL_HANDLER;
    }

    return cube_manager_merge(instance->cube_manager, src->cube_manager);
}

struct fieldstat *fieldstat_fork(const struct fieldstat *instance)
{
    if (instance == NULL) {
        return NULL;
    }
    struct fieldstat *new_instance = calloc(1, sizeof(struct fieldstat));
    new_instance->cube_manager = cube_manager_fork(instance->cube_manager);

    return new_instance;
}

void fieldstat_calibrate(const struct fieldstat *master, struct fieldstat *replica)
{
    cube_manager_calibrate(replica->cube_manager, master->cube_manager);
}

/* -------------------------------------------------------------------------- */
/*                      histogram fast batch transaction                      */
/* -------------------------------------------------------------------------- */
struct histogram *histogram_new(long long lowest_trackable_value, long long highest_trackable_value, int significant_figures) {
    struct hdr_histogram* inner = NULL;
    int ret = hdr_init(lowest_trackable_value, highest_trackable_value, significant_figures, &inner);
    if (ret != 0) {
        return NULL;
    }

    struct histogram *histogram = calloc(1, sizeof(struct histogram));
    histogram->histogram = inner;

    return histogram;
}

void histogram_free(struct histogram *histogram)
{
    if (histogram == NULL) {
        return;
    }
    hdr_close(histogram->histogram);
    free(histogram);
}

void histogram_reset(struct histogram *histogram) {
    hdr_reset(histogram->histogram);
}

// cppcheck-suppress [constParameterPointer, unmatchedSuppression]
int fieldstat_histogram_merge(struct fieldstat *instance, int cube_id, int metric_id, const struct field *cell_dimensions, size_t n_dimensions, const struct histogram *src)
{
    struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }

    return cube_histogram_merge(cube, metric_id, cell_dimensions, n_dimensions, src->histogram);
}

int histogram_record(struct histogram *histogram, long long value) {
    bool ret = hdr_record_value(histogram->histogram, value);
    if (!ret) {
        return FS_ERR_INVALID_PARAM;
    }
    return FS_OK;
}

/* -------------------------------------------------------------------------- */
/*                                    query                                   */
/* -------------------------------------------------------------------------- */
void fieldstat_get_cubes(const struct fieldstat *instance, int **cube_ids, int *n_cube)
{
    cube_manager_list(instance->cube_manager, cube_ids, n_cube);
}

struct field_list *fieldstat_cube_get_dimension(const struct fieldstat *instance, int cube_id)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return NULL;
    }
    
    return cube_get_identifier(cube);
}

int fieldstat_counter_get(const struct fieldstat *instance, int cube_id, const struct field_list *cell_dimensions, int metric_id, long long *value)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    
    return cube_counter_get(cube, metric_id, cell_dimensions, value);
}

int fieldstat_hll_get(const struct fieldstat *instance, int cube_id, const struct field_list *cell_dimensions, int metric_id, double *value)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    
    int ret = cube_hll_get(cube, metric_id, cell_dimensions, value);

    return ret;
}

long long fieldstat_histogram_value_at_percentile(const struct fieldstat *instance, int cube_id, const struct field_list *cell_dimensions, int metric_id, double percentile)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    long long value;
    int ret = cube_histogram_value_at_percentile(cube, metric_id, cell_dimensions, percentile, &value);
    if (ret < 0) {
        return ret;
    }

    return value;
}

long long fieldstat_histogram_count_le_value(const struct fieldstat *instance, int cube_id, const struct field_list *cell_dimensions, int metric_id, long long value)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    long long count;
    int ret = cube_histogram_count_le_value(cube, metric_id, cell_dimensions, value, &count);
    if (ret < 0) {
        return ret;
    }
    return count;
}

void fieldstat_metric_get_serialization_as_base64(const struct fieldstat *instance, int cube_id, int metric_id, const struct field_list *cell_dimensions, char **blob, size_t *blob_size)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        *blob = NULL;
        *blob_size = 0;
        return;
    }

    cube_get_serialization_as_base64(cube, metric_id, cell_dimensions, blob, blob_size);
}

void fieldstat_field_list_arr_free(struct field_list *field_lists, size_t n_field_list)
{
    if (field_lists == NULL) {
        return;
    }
    for (int i = 0; i < n_field_list; i++) {
        for (size_t j = 0; j < field_lists[i].n_field; j++) {
            struct field *field = &(field_lists[i].field[j]);
            free((char *)field->key);
            if (field->type == FIELD_VALUE_CSTRING) {
                free((char *)field->value_str);
            }
        }
        free(field_lists[i].field);
    }
    free(field_lists);
}

//uninitialized

const char *fieldstat_metric_get_name(const struct fieldstat *instance, int cube_id, int metric_id)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return NULL;
    }
    const struct metric_manifest *manifest = cube_get_metric_manifest_by_id(cube, metric_id);
    if (manifest == NULL) {
        return NULL;
    }
    return manifest->name;
}

enum metric_type fieldstat_metric_get_type(const struct fieldstat *instance, int cube_id, int metric_id)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return (enum metric_type)(-1);
    }
    
    const struct metric_manifest *manifest = cube_get_metric_manifest_by_id(cube, metric_id);
    if (manifest == NULL) {
        return (enum metric_type)(-1);
    }

    return manifest->type;
}

void fieldstat_cube_get_cells(const struct fieldstat *instance, int cube_id, struct field_list **cell_dimensions, size_t *n_cell)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return;
    }
    
    cube_get_cells(cube, cell_dimensions, n_cell);
}

int fieldstat_cube_get_cell_number(const struct fieldstat *instance, int cube_id)
{
    if (instance == NULL) {
        return FS_ERR_NULL_HANDLER;
    }

    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }

    return cube_get_cell_count(cube);
}

int fieldstat_find_cube(const struct fieldstat *instance, const struct field *cube_dimensions, size_t n_dimensions)
{
    if (instance == NULL) {
        return FS_ERR_NULL_HANDLER;
    }

    int cube_id = cube_manager_find(instance->cube_manager, cube_dimensions, n_dimensions);
    if (cube_id == -1) {
        return FS_ERR_INVALID_DIMENSION;
    }

    return cube_id;
}

void fieldstat_cell_get_metrics(const struct fieldstat *instance, int cube_id, const struct field_list *cell_dimensions, int **metric_id_out, size_t *n_metric_out)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    return cube_get_metrics_in_cell(cube, cell_dimensions, metric_id_out, n_metric_out);
}

int fieldstat_cube_get_metric_id_by_name(const struct fieldstat *instance, int cube_id, const char *metric_name) 
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }

    const struct metric_manifest *manifest = cube_get_metric_manifest_by_name(cube, metric_name);
    if (manifest == NULL) {
        return FS_ERR_INVALID_METRIC_NAME;
    }

    return manifest->id;
}