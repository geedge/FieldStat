
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "cjson/cJSON.h"
#include "uthash.h"

#include "fieldstat.h"
#include "metrics/metric.h"
#include "cube.h"
#include "metric_manifest.h"


struct fieldstat {
    struct metric_manifest_manager *manifest_manager;

    struct cube_manager *cube_manager;
};

union metric_parameter *construct_parameters(enum metric_type type, ...)
{
    union metric_parameter *paras = (union metric_parameter *)malloc(sizeof(union metric_parameter));
    va_list ap;
    va_start(ap, type);
    switch (type) {
        case METRIC_TYPE_COUNTER:
            break;
        case METRIC_TYPE_HLL:
            paras->hll.precision = (char)va_arg(ap, int);
            break;
        case METRIC_TYPE_HISTOGRAM:
            paras->hdr.lowest_trackable_value = va_arg(ap, long long);
            paras->hdr.highest_trackable_value = va_arg(ap, long long);
            paras->hdr.significant_figures = va_arg(ap, int);
            break;
        default:
            assert(0);
    }
    va_end(ap);
    return paras;
}

void tag_array_copy(struct fieldstat_tag *tags_dst, const struct fieldstat_tag *tags_src, size_t n_tag)
{
    for (size_t i = 0; i < n_tag; i++) {
        tags_dst[i].key = strdup(tags_src[i].key);
        tags_dst[i].type = tags_src[i].type;
        switch (tags_src[i].type)
        {
        case TAG_INTEGER:
            tags_dst[i].value_longlong = tags_src[i].value_longlong;
            break;
        case TAG_CSTRING:
            tags_dst[i].value_str = strdup(tags_src[i].value_str);
            break;
        case TAG_DOUBLE:
            tags_dst[i].value_double = tags_src[i].value_double;
            break;
        default:
            break;
        }
    }
}

bool is_tag_array_same(const struct fieldstat_tag *tags1, const struct fieldstat_tag *tags2, size_t n_tag)
{
    for (size_t i = 0; i < n_tag; i++) {
        if (tags1[i].type != tags2[i].type) {
            return false;
        }
        if (strcmp(tags1[i].key, tags2[i].key) != 0) {
            return false;
        }
        switch (tags1[i].type)
        {
        case TAG_INTEGER:
            if (tags1[i].value_longlong != tags2[i].value_longlong) {
                return false;
            }
            break;
        case TAG_CSTRING:
            if (strcmp(tags1[i].value_str, tags2[i].value_str) != 0) {
                return false;
            }
            break;
        case TAG_DOUBLE:
            if (tags1[i].value_double != tags2[i].value_double) {
                return false;
            }
            break;
        default:
            break;
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/*                                  fieldstat                                 */
/* -------------------------------------------------------------------------- */

struct fieldstat *fieldstat_new()
{
    struct fieldstat *instance = calloc(1, sizeof(struct fieldstat));

    instance->manifest_manager = metric_manifest_manager_new();

    instance->cube_manager = cube_manager_new();

    return instance;
}

void fieldstat_free(struct fieldstat *instance)
{
    if (instance == NULL) {
        return;
    }

    cube_manager_free(instance->cube_manager);

    metric_manifest_manager_free(instance->manifest_manager);

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

void fieldstat_free_tag_array(struct fieldstat_tag *tags, size_t n_tags)
{
    for (size_t i = 0; i < n_tags; i++) {
        struct fieldstat_tag *tag = &tags[i];
        free((char *)tag->key);
        if (tag->type == TAG_CSTRING) {
            free((char *)tag->value_str);
        }
    }
    free(tags);
}

int fieldstat_create_cube(struct fieldstat *instance, const struct fieldstat_tag *cube_identifier, size_t n_tag, enum sampling_mode mode, size_t max_n_cell)
{
    if (instance == NULL) {
        return  FS_ERR_NULL_HANDLER;
    }

    if (n_tag == 0 || cube_identifier == NULL) {
        cube_identifier = NULL;
        n_tag = 0;
    }
    if (mode == SAMPLING_MODE_TOPK && max_n_cell == 0) {
        return FS_ERR_INVALID_PARAM;
    }
    if (max_n_cell == 0) {
        max_n_cell = INT32_MAX;
    }
    
    struct cube *cube = cube_new(cube_identifier, n_tag, mode, max_n_cell);
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
    const struct metric_manifest *manifest = metric_manifest_manager_get_by_id(instance->manifest_manager, metric_id);
    if (manifest == NULL) {
        return FS_ERR_INVALID_METRIC_ID;
    }
    if (manifest->type != METRIC_TYPE_COUNTER) {
        return FS_ERR_INVALID_PARAM;
    }

    cube_set_primary_metric(cube, metric_id);

    return FS_OK;
}

/* -------------------------------------------------------------------------- */
/*                               metric register                              */
/* -------------------------------------------------------------------------- */
int fieldstat_register_counter(struct fieldstat *instance, const char *metric_name)
{
    struct metric_manifest *metric = malloc(sizeof(struct metric_manifest));
    metric->name = strdup(metric_name);
    metric->parameters = construct_parameters(METRIC_TYPE_COUNTER);
    metric->type = METRIC_TYPE_COUNTER;

    int id = metric_manifest_manager_add(instance->manifest_manager, metric);
    if (id < 0) {
        free(metric->name);
        free(metric->parameters);
        free(metric);
        return FS_ERR_INVALID_KEY;
    }

    metric->id = id;
    return id;
}

int fieldstat_register_hll(struct fieldstat *instance, const char *metric_name, unsigned char precision)
{
    if (precision < 4 || precision > 18) {
        return FS_ERR_INVALID_PARAM;
    }

    struct metric_manifest *metric = malloc(sizeof(struct metric_manifest));
    metric->name = strdup(metric_name);
    metric->parameters = construct_parameters(METRIC_TYPE_HLL, precision);
    metric->type = METRIC_TYPE_HLL;

    int id = metric_manifest_manager_add(instance->manifest_manager, metric);
    if (id < 0) {
        free(metric->name);
        free(metric->parameters);
        free(metric);
        return FS_ERR_INVALID_KEY;
    }

    metric->id = id;
    return id;
}

int fieldstat_register_hist(struct fieldstat *instance, const char *metric_name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures)
{
    // refer to hdr_histogram.h for the rules of parameters. Just copy them here
    if (lowest_trackable_value < 1) {
        return FS_ERR_INVALID_PARAM;
    }
    if (significant_figures < 1 || significant_figures > 5) {
        return FS_ERR_INVALID_PARAM;
    }
    if (lowest_trackable_value * 2 > highest_trackable_value)
	{
		return FS_ERR_INVALID_PARAM;
	}


    struct metric_manifest *metric = malloc(sizeof(struct metric_manifest));
    metric->name = strdup(metric_name);
    metric->parameters = construct_parameters(METRIC_TYPE_HISTOGRAM, lowest_trackable_value, highest_trackable_value, significant_figures);
    metric->type = METRIC_TYPE_HISTOGRAM;

    int id = metric_manifest_manager_add(instance->manifest_manager, metric);
    if (id < 0) {
        free(metric->name);
        free(metric->parameters);
        free(metric);
        return FS_ERR_INVALID_KEY;
    }
    metric->id = id;

    return id;
}

/* -------------------------------------------------------------------------- */
/*                               metric operation                              */
/* -------------------------------------------------------------------------- */

// cppcheck-suppress [constParameterPointer, unmatchedSuppression]
int fieldstat_counter_incrby(struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag *tags, size_t n_tag, long long increment)
{
    struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    const struct metric_manifest *manifest = metric_manifest_manager_get_by_id(instance->manifest_manager, metric_id);
    if (manifest == NULL || manifest->type != METRIC_TYPE_COUNTER) {
        return FS_ERR_INVALID_METRIC_ID;
    }
    
    return cube_counter_incrby(cube, manifest, tags, n_tag, increment);
}

// cppcheck-suppress [constParameterPointer, unmatchedSuppression]
int fieldstat_counter_set(struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag *tags, size_t n_tag, long long value)
{
    struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    const struct metric_manifest *manifest = metric_manifest_manager_get_by_id(instance->manifest_manager, metric_id);
    if (manifest == NULL || manifest->type != METRIC_TYPE_COUNTER) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    return cube_counter_set(cube, manifest, tags, n_tag, value);
}

// cppcheck-suppress [constParameterPointer, unmatchedSuppression]
int fieldstat_hll_add(struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag *tags, size_t n_tag, const char *key, size_t key_len)
{
    struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    const struct metric_manifest *manifest = metric_manifest_manager_get_by_id(instance->manifest_manager, metric_id);
    if (manifest == NULL || manifest->type != METRIC_TYPE_HLL) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    return cube_hll_add(cube, manifest, tags, n_tag, key, key_len);
}

// cppcheck-suppress [constParameterPointer, unmatchedSuppression]
int fieldstat_hist_record(struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag *tags, size_t n_tag, long long value)
{
    struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    const struct metric_manifest *manifest = metric_manifest_manager_get_by_id(instance->manifest_manager, metric_id);
    if (manifest == NULL || manifest->type != METRIC_TYPE_HISTOGRAM) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    return cube_histogram_record(cube, manifest, tags, n_tag, value);
}

int fieldstat_merge(struct fieldstat *instance, const struct fieldstat *src)
{
    if (instance == NULL || src == NULL) {
        return FS_ERR_NULL_HANDLER;
    }

    size_t n_metric_src = 0;
    const struct metric_manifest **list_src = metric_manifest_manager_list(src->manifest_manager, &n_metric_src);
    size_t n_metric_dst = 0;
    const struct metric_manifest **list_dst = metric_manifest_manager_list(instance->manifest_manager, &n_metric_dst);
    int len_min = n_metric_src < n_metric_dst ? n_metric_src : n_metric_dst;
    for (int i = 0; i < len_min; i++) {
        if (list_src[i]->type != list_dst[i]->type) {
            return FS_ERR_INVALID_PARAM;
        }
        if (strcmp(list_src[i]->name, list_dst[i]->name) != 0) {
            return FS_ERR_INVALID_PARAM;
        }
    }
    for (int i = n_metric_dst; i < n_metric_src; i++) {
        metric_manifest_manager_add(instance->manifest_manager, metric_manifest_copy(list_src[i]));
    }


    cube_manager_merge(instance->cube_manager, src->cube_manager);

    return FS_OK;
}

struct fieldstat *fieldstat_fork(const struct fieldstat *instance)
{
    if (instance == NULL) {
        return NULL;
    }
    struct fieldstat *new_instance = calloc(1, sizeof(struct fieldstat));
    new_instance->cube_manager = cube_manager_fork(instance->cube_manager);

    new_instance->manifest_manager = metric_manifest_manager_copy(instance->manifest_manager);

    return new_instance;
}

int fieldstat_calibrate(const struct fieldstat *master, struct fieldstat *replica)
{
    cube_manager_calibrate(replica->cube_manager, master->cube_manager);
    metric_manifest_manager_free(replica->manifest_manager);
    replica->manifest_manager = metric_manifest_manager_copy(master->manifest_manager);

    return FS_OK;
}

/* -------------------------------------------------------------------------- */
/*                                    query                                   */
/* -------------------------------------------------------------------------- */
void fieldstat_get_cubes(const struct fieldstat *instance, int **cube_ids, int *n_cube)
{
    cube_manager_list(instance->cube_manager, cube_ids, n_cube);
}

void fieldstat_get_metrics(const struct fieldstat *instance, int **metric_id_out, size_t *n_metric)
{
    const struct metric_manifest **list = metric_manifest_manager_list(instance->manifest_manager, n_metric);
    if (*n_metric == 0) {
        *metric_id_out = NULL;
        return;
    }

    int *tmp_ids = (int *)malloc(sizeof(int) * (*n_metric));
    *metric_id_out = tmp_ids;

    for (int i = 0; i < *n_metric; i++) {
        tmp_ids[i] = list[i]->id;
    }
}

struct fieldstat_tag_list *fieldstat_cube_get_tags(const struct fieldstat *instance, int cube_id)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return NULL;
    }
    
    return cube_get_identifier(cube);
}

int fieldstat_counter_get(const struct fieldstat *instance, int cube_id, const struct fieldstat_tag_list *tags, int metric_id, long long *value)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    
    return cube_counter_get(cube, metric_id, tags, value);
}

int fieldstat_hll_get(const struct fieldstat *instance, int cube_id, const struct fieldstat_tag_list *tags, int metric_id, double *value)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    
    int ret = cube_hll_get(cube, metric_id, tags, value);

    return ret;
}

long long fieldstat_hist_value_at_percentile(const struct fieldstat *instance, int cube_id, const struct fieldstat_tag_list *tags, int metric_id, double percentile)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    long long value;
    int ret = cube_histogram_value_at_percentile(cube, metric_id, tags, percentile, &value);
    if (ret < 0) {
        return ret;
    }

    return value;
}

long long fieldstat_hist_count_le_value(const struct fieldstat *instance, int cube_id, const struct fieldstat_tag_list *tags, int metric_id, long long value)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    long long count;
    int ret = cube_histogram_count_le_value(cube, metric_id, tags, value, &count);
    if (ret < 0) {
        return ret;
    }
    return count;
}

void fieldstat_get_serialized_blob(const struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag_list *tags, char **blob, size_t *blob_size)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        *blob = NULL;
        *blob_size = 0;
        return;
    }

    cube_get_serialization(cube, metric_id, tags, blob, blob_size);
}

void fieldstat_tag_list_arr_free(struct fieldstat_tag_list *tag_list, size_t n_cell)
{
    if (tag_list == NULL) {
        return;
    }
    for (int i = 0; i < n_cell; i++) {
        fieldstat_free_tag_array(tag_list[i].tag, tag_list[i].n_tag);
    }
    free(tag_list);
}

const char *fieldstat_get_metric_name(const struct fieldstat *instance, int metric_id)
{
    const struct metric_manifest *metric = metric_manifest_manager_get_by_id(instance->manifest_manager, metric_id);
    if (metric == NULL) {
        return NULL;
    }

    return metric->name;
}

enum metric_type fieldstat_get_metric_type(const struct fieldstat *instance, int metric_id)
{
    const struct metric_manifest *metric = metric_manifest_manager_get_by_id(instance->manifest_manager, metric_id);
    if (metric == NULL) {
        return (enum metric_type)(-1);
    }

    return metric->type;
}

void fieldstat_cube_get_cells(const struct fieldstat *instance, int cube_id, struct fieldstat_tag_list **tag_list, size_t *n_cell)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return;
    }
    
    cube_get_cells(cube, tag_list, n_cell);
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

int fieldstat_find_cube(const struct fieldstat *instance, const struct fieldstat_tag *shared_tags, size_t n_shared_tags)
{
    if (instance == NULL) {
        return FS_ERR_NULL_HANDLER;
    }

    int cube_id = cube_manager_find(instance->cube_manager, shared_tags, n_shared_tags);
    if (cube_id == -1) {
        return FS_ERR_INVALID_KEY;
    }

    return cube_id;
}

void fieldstat_get_metric_in_cell(const struct fieldstat *instance, int cube_id, const struct fieldstat_tag_list *tags, int **metric_id_out, size_t *n_metric_out)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    return cube_get_cells_used_by_metric(cube, tags, metric_id_out, n_metric_out);
}