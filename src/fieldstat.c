
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "cjson/cJSON.h"
#include "uthash.h"

#include "fieldstat.h"
#include "metrics/metric.h"
#include "cube.h"

struct fieldstat {
    struct cube_manager *cube_manager;
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

int fieldstat_destroy_cube(struct fieldstat *instance, int cube_id)
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

void fieldstat_free_tag_array(struct field *fields, size_t n_tags)
{
    for (size_t i = 0; i < n_tags; i++) {
        struct field *field = &fields[i];
        free((char *)field->key);
        if (field->type == FIELD_VALUE_CSTRING) {
            free((char *)field->value_str);
        }
    }
    free(fields);
}

int fieldstat_create_cube(struct fieldstat *instance, const struct field *cube_dimensions, size_t n_dimension, enum sampling_mode mode, size_t max_n_cell)
{
    if (instance == NULL) {
        return  FS_ERR_NULL_HANDLER;
    }

    if (n_dimension == 0 || cube_dimensions == NULL) {
        cube_dimensions = NULL;
        n_dimension = 0;
    }
    if (mode == SAMPLING_MODE_TOPK && max_n_cell == 0) {
        return FS_ERR_INVALID_PARAM;
    }
    if (max_n_cell == 0) {
        max_n_cell = INT32_MAX;
    }
    
    struct cube *cube = cube_new(cube_dimensions, n_dimension, mode, max_n_cell);
    int ret = cube_manager_add(instance->cube_manager, cube);
    if (ret < 0) {
        cube_free(cube);
        return FS_ERR_INVALID_KEY;
    }

    return ret; //ret is the cube_id
}

// cppcheck-suppress [constParameterPointer, unmatchedSuppression]
int fieldstat_cube_set_primary_metric(struct fieldstat *instance, int cube_id, int metric_id)
{
    if (instance == NULL) {
        return FS_ERR_NULL_HANDLER;
    }
    
    struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    
    return cube_set_primary_metric(cube, metric_id);
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
int fieldstat_hll_add_field(struct fieldstat *instance, int cube_id, int metric_id, const struct field *cell_dimensions, size_t n_dimensions, const struct field *item, size_t item_len)
{
    struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }

    return cube_hll_add_field(cube, metric_id, cell_dimensions, n_dimensions, item, item_len);
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

int fieldstat_calibrate(const struct fieldstat *master, struct fieldstat *replica)
{
    cube_manager_calibrate(replica->cube_manager, master->cube_manager);

    return FS_OK;
}

/* -------------------------------------------------------------------------- */
/*                                    query                                   */
/* -------------------------------------------------------------------------- */
void fieldstat_get_cubes(const struct fieldstat *instance, int **cube_ids, int *n_cube)
{
    cube_manager_list(instance->cube_manager, cube_ids, n_cube);
}

struct field_list *fieldstat_cube_get_tags(const struct fieldstat *instance, int cube_id)
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

void fieldstat_tag_list_arr_free(struct field_list *tag_list, size_t n_cell)
{
    if (tag_list == NULL) {
        return;
    }
    for (int i = 0; i < n_cell; i++) {
        fieldstat_free_tag_array(tag_list[i].field, tag_list[i].n_field);
    }
    free(tag_list);
}

const char *fieldstat_get_metric_name(const struct fieldstat *instance, int cube_id, int metric_id)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        printf("cube is NULL\n");
        return NULL;
    }
    return cube_get_metric_name(cube, metric_id);
}

enum metric_type fieldstat_get_metric_type(const struct fieldstat *instance, int cube_id, int metric_id)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return (enum metric_type)(-1);
    }
    return cube_get_metric_type(cube, metric_id);
}

void fieldstat_cube_get_cells(const struct fieldstat *instance, int cube_id, struct field_list **cell_dimensions, size_t *n_cell)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return;
    }
    
    cube_get_cells(cube, cell_dimensions, n_cell);
}

int fieldstat_get_used_sampling(const struct fieldstat *instance, int cube_id)
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
        return FS_ERR_INVALID_KEY;
    }

    return cube_id;
}

void fieldstat_get_metric_in_cell(const struct fieldstat *instance, int cube_id, const struct field_list *cell_dimensions, int **metric_id_out, size_t *n_metric_out)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    return cube_get_metrics_in_cell(cube, cell_dimensions, metric_id_out, n_metric_out);
}

void fieldstat_cube_get_metrics(const struct fieldstat *instance, int cube_id, int **metric_id_out, size_t *n_metric) {
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    cube_get_metrics(cube, metric_id_out, n_metric);
}
