
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "cjson/cJSON.h"
#include "uthash.h"

#include "fieldstat.h"
#include "metrics/metric.h"
#include "my_ut_hash.h"
#include "cube.h"
#include "metric_manifest.h"

#define DEFAULT_N_METRIC 32



struct fieldstat {
    struct metric_manifest **manifests; // TODO: 把那个哈希表再加回去
    size_t manifest_size;
    size_t manifest_cnt;

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

struct metric_manifest *metric_manifest_copy(const struct metric_manifest *src)
{
    struct metric_manifest *dest = (struct metric_manifest *)malloc(sizeof(struct metric_manifest));
    dest->name = strdup(src->name);
    if (src->paras == NULL) {
        dest->paras = NULL;
    } else {
        dest->paras = (union metric_parameter *)malloc(sizeof(union metric_parameter));
        memcpy(dest->paras, src->paras, sizeof(union metric_parameter));
    }

    dest->type = src->type;
    dest->id = src->id;

    return dest;
}

void metric_manifest_free(struct metric_manifest *manifest) {
    free(manifest->name);
    free(manifest->paras);
    free(manifest);
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

bool is_metric_name_duplicate(const struct fieldstat *instance, const char *name)
{
    for (size_t i = 0; i < instance->manifest_cnt; i++) {
        if (strcmp(instance->manifests[i]->name, name) == 0) {
            return true;
        }
    }
    return false;
}
/* -------------------------------------------------------------------------- */
/*                                  fieldstat                                 */
/* -------------------------------------------------------------------------- */

struct fieldstat *fieldstat_new()
{
    struct fieldstat *instance = calloc(1, sizeof(struct fieldstat));

    instance->manifest_size = DEFAULT_N_METRIC;
    instance->manifests = calloc(instance->manifest_size, sizeof(struct metric_manifest *));
    instance->manifest_cnt = 0;

    instance->cube_manager = cube_manager_new();

    return instance;
}

void fieldstat_free(struct fieldstat *instance)
{
    if (instance == NULL) {
        return;
    }

    cube_manager_free(instance->cube_manager);

    for (size_t i = 0; i < instance->manifest_cnt; i++) {
        metric_manifest_free(instance->manifests[i]);
    }
    free(instance->manifests);

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

int fieldstat_create_cube(struct fieldstat *instance, const struct fieldstat_tag *shared_tags, size_t n_tag, enum sampling_mode mode, size_t max_n_cell)
{
    if (instance == NULL) {
        return  FS_ERR_NULL_HANDLER;
    }

    if (n_tag == 0 || shared_tags == NULL) {
        shared_tags = NULL;
        n_tag = 0;
    }
    if (mode == SAMPLING_MODE_TOPK && max_n_cell == 0) {
        return FS_ERR_INVALID_PARAM;
    }
    if (max_n_cell == 0) {
        max_n_cell = INT32_MAX;
    }
    
    struct cube *cube = cube_new(shared_tags, n_tag, mode, max_n_cell);
    int ret = cube_manager_add(instance->cube_manager, cube);
    if (ret < 0) {
        cube_free(cube);
        return FS_ERR_INVALID_KEY;
    }

    return ret; //ret is the cube_id
}

int fieldstat_cube_set_primary_metric(struct fieldstat *instance, int cube_id, int metric_id)
{
    if (instance == NULL) {
        return FS_ERR_NULL_HANDLER;
    }
    
    struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    if (metric_id < 0 || metric_id >= instance->manifest_cnt) {
        return FS_ERR_INVALID_METRIC_ID;
    }
    if (instance->manifests[metric_id] == NULL) {
        return FS_ERR_INVALID_METRIC_ID;
    }
    if (instance->manifests[metric_id]->type != METRIC_TYPE_COUNTER) {
        return FS_ERR_INVALID_PARAM;
    }

    cube_set_primary_metric(cube, metric_id);

    return FS_OK;
}

/* -------------------------------------------------------------------------- */
/*                               metric register                              */
/* -------------------------------------------------------------------------- */

void add_manifest_to_instance(struct fieldstat *instance, const struct metric_manifest *manifest, int metric_id)
{
    if (metric_id >= instance->manifest_size) {
        instance->manifests = realloc(instance->manifests, sizeof(struct metric_manifest *) * instance->manifest_size * 2);
        memset(instance->manifests + instance->manifest_size, 0, sizeof(struct metric_manifest *) * (instance->manifest_size));
        instance->manifest_size *= 2;
    }

    instance->manifests[metric_id] = (struct metric_manifest *)manifest;
    if (metric_id >= instance->manifest_cnt) {
        instance->manifest_cnt = metric_id + 1;
    }
}

static int append_manifest_to_instance(struct fieldstat *instance, const struct metric_manifest *metric)
{
    int metric_id = instance->manifest_cnt;
    add_manifest_to_instance(instance, metric, metric_id);

    return metric_id;
}

int check_before_register_metric(const struct fieldstat *instance, const char *metric_name)
{
    if (instance == NULL) {
        return FS_ERR_NULL_HANDLER;
    }

    if (is_metric_name_duplicate(instance, metric_name)) {
        return FS_ERR_INVALID_KEY;
    }

    return FS_OK;
}

int fieldstat_register_counter(struct fieldstat *instance, const char *metric_name)
{
    int ret = check_before_register_metric(instance, metric_name);
    if (ret != FS_OK) {
        return ret;
    }

    struct metric_manifest *metric = malloc(sizeof(struct metric_manifest));
    metric->name = strdup(metric_name);
    metric->paras = construct_parameters(METRIC_TYPE_COUNTER);
    metric->type = METRIC_TYPE_COUNTER;

    int id = append_manifest_to_instance(instance, metric);
    metric->id = id;

    return id;
}

int fieldstat_register_hll(struct fieldstat *instance, const char *metric_name, unsigned char precision)
{
    int ret = check_before_register_metric(instance, metric_name);
    if (ret != FS_OK) {
        return ret;
    }
    if (precision < 4 || precision > 18) {
        return FS_ERR_INVALID_PARAM;
    }

    struct metric_manifest *metric = malloc(sizeof(struct metric_manifest));
    metric->name = strdup(metric_name);
    metric->paras = construct_parameters(METRIC_TYPE_HLL, precision);
    metric->type = METRIC_TYPE_HLL;

    int id = append_manifest_to_instance(instance, metric);
    metric->id = id;

    return id;
}

int fieldstat_register_hist(struct fieldstat *instance, const char *metric_name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures)
{
    int ret = check_before_register_metric(instance, metric_name);
    if (ret != FS_OK) {
        return ret;
    }
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
    metric->paras = construct_parameters(METRIC_TYPE_HISTOGRAM, lowest_trackable_value, highest_trackable_value, significant_figures);
    metric->type = METRIC_TYPE_HISTOGRAM;

    int id = append_manifest_to_instance(instance, metric);
    metric->id = id;

    return id;
}

/* -------------------------------------------------------------------------- */
/*                               metric operation                              */
/* -------------------------------------------------------------------------- */
int check_before_add(const struct fieldstat *instance, int cube_id, int metric_id, enum metric_type type)
{
    if (instance == NULL) {
        return FS_ERR_NULL_HANDLER;
    }

    if (cube_manager_get_cube_by_id(instance->cube_manager, cube_id) == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }

    if (metric_id < 0 || metric_id >= instance->manifest_cnt) {
        return FS_ERR_INVALID_METRIC_ID;
    }
    const struct metric_manifest *metric = instance->manifests[metric_id];
    if (metric == NULL || metric->type != type) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    return FS_OK;
}

int fieldstat_counter_incrby(struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag *tags, size_t n_tag, long long increment)
{
    int ret = check_before_add(instance, cube_id, metric_id, METRIC_TYPE_COUNTER);
    if (ret != FS_OK) {
        return ret;
    }

    struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    const struct metric_manifest *manifest = instance->manifests[metric_id];
    
    return cube_counter_incrby(cube, manifest, tags, n_tag, increment);

}

int fieldstat_counter_set(struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag *tags, size_t n_tag, long long value)
{
    int ret = check_before_add(instance, cube_id, metric_id, METRIC_TYPE_COUNTER);
    if (ret != FS_OK) {
        return ret;
    }
    struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    const struct metric_manifest *manifest = instance->manifests[metric_id];

    return cube_counter_set(cube, manifest, tags, n_tag, value);
}

int fieldstat_hll_add(struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag *tags, size_t n_tag, const char *key, size_t key_len)
{
    int ret = check_before_add(instance, cube_id, metric_id, METRIC_TYPE_HLL);
    if (ret != FS_OK) {
        return ret;
    }
    
    struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    const struct metric_manifest *manifest = instance->manifests[metric_id];

    return cube_hll_add(cube, manifest, tags, n_tag, key, key_len);
}

int fieldstat_hist_record(struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag *tags, size_t n_tag, long long value)
{
    int ret = check_before_add(instance, cube_id, metric_id, METRIC_TYPE_HISTOGRAM);
    if (ret != FS_OK) {
        return ret;
    }

    struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    const struct metric_manifest *manifest = instance->manifests[metric_id];

    return cube_histogram_record(cube, manifest, tags, n_tag, value);
}

/* -------------------------------------------------------------------------- */
/*                                    merge                                   */
/* -------------------------------------------------------------------------- */



int fieldstat_merge(struct fieldstat *instance, struct fieldstat *src)
{
    if (instance == NULL || src == NULL) {
        return FS_ERR_NULL_HANDLER;
    }

    int metric_len_src = src->manifest_cnt;
    for (int i = 0; i < metric_len_src; i++) {
        const struct metric_manifest *metric_src = src->manifests[i];
        const struct metric_manifest *metric_dst = instance->manifests[i];
        if (metric_src == NULL || metric_dst == NULL) {
            break;
        }
        if (metric_src->type != metric_dst->type ||
                strcmp(metric_src->name, metric_dst->name) != 0
            ) {
            assert(0);
            return FS_ERR_INVALID_PARAM;
        }
    }
    for (int i = 0; i < metric_len_src; i++) {
        if (instance->manifests[i] != NULL || src->manifests[i] == NULL) {
            continue;
        }
        const struct metric_manifest *metric_src = src->manifests[i];
        append_manifest_to_instance(instance, metric_manifest_copy(metric_src));
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

    new_instance->manifests = calloc(instance->manifest_size, sizeof(struct metric_manifest *));
    new_instance->manifest_size = instance->manifest_size;
    new_instance->manifest_cnt = instance->manifest_cnt;
    for (size_t i = 0; i < instance->manifest_cnt; i++) {
        new_instance->manifests[i] = metric_manifest_copy(instance->manifests[i]);
    }

    return new_instance;
}

void calibrate_metrics_in_instance(const struct fieldstat *master, struct fieldstat *replica)
{
    if (replica->manifest_size < master->manifest_size) {
        replica->manifests = (struct metric_manifest **)realloc(replica->manifests, sizeof(struct metric_manifest *) * master->manifest_size);
        memset(replica->manifests + replica->manifest_size, 0, sizeof(struct metric_manifest *) * (master->manifest_size - replica->manifest_size));
        replica->manifest_size = master->manifest_size;
    }

    size_t longer_arr_len = master->manifest_cnt > replica->manifest_cnt ? master->manifest_cnt : replica->manifest_cnt;
    for (size_t i = 0; i < longer_arr_len; i++) {
        const struct metric_manifest *metric_master = i >= master->manifest_cnt ? NULL : master->manifests[i];
        struct metric_manifest *metric_target = i >= replica->manifest_cnt ? NULL : replica->manifests[i];
        if (metric_master == NULL && metric_target == NULL) {
            continue;
        }
        if (metric_master == NULL && metric_target != NULL) {
            metric_manifest_free(metric_target);
            replica->manifests[i] = NULL;
            continue;
        }
        if (metric_master != NULL && metric_target == NULL) {
            const struct metric_manifest *metric_dup = metric_manifest_copy(metric_master);
            add_manifest_to_instance(replica, metric_dup, i);
            continue;
        }

        if (metric_master->type != metric_target->type || strcmp(metric_master->name, metric_target->name) != 0) {
            metric_manifest_free(metric_target);
            const struct metric_manifest *metric_dup = metric_manifest_copy(metric_master);
            add_manifest_to_instance(replica, metric_dup, i);
            continue;
        }

        // metric same, no need to do anything
    }

    replica->manifest_cnt = master->manifest_cnt;
}

int fieldstat_calibrate(const struct fieldstat *master, struct fieldstat *replica)
{
    cube_manager_calibrate(replica->cube_manager, master->cube_manager);
    calibrate_metrics_in_instance(master, replica);

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
    int *tmp_ids = (int *)malloc(sizeof(int) * instance->manifest_cnt);
    *metric_id_out = tmp_ids;
    int cnt = 0;
    for (int i = 0; i < instance->manifest_cnt; i++) {
        if (instance->manifests[i] != NULL) {
            tmp_ids[cnt] = i;
            cnt ++;
        }
    }
    if (cnt == 0) {
        free(tmp_ids);
        *metric_id_out = NULL;
    }
    *n_metric = cnt;
}

struct fieldstat_tag_list *fieldstat_get_shared_tags(const struct fieldstat *instance, int cube_id)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return NULL;
    }
    
    return cube_get_identifier(cube);
}

int fieldstat_counter_get(const struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag_list *tags, long long *value)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    
    return cube_counter_get(cube, metric_id, tags, value);
}

int fieldstat_hll_get(const struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag_list *tags, double *value)
{
    const struct cube *cube = cube_manager_get_cube_by_id(instance->cube_manager, cube_id);
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    
    int ret = cube_hll_get(cube, metric_id, tags, value);

    return ret;
}

long long fieldstat_hist_value_at_percentile(const struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag_list *tags, double percentile)
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

long long fieldstat_hist_count_le_value(const struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag_list *tags, long long value)
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
    if (metric_id < 0 || metric_id >= instance->manifest_cnt) {
        return NULL;
    }
    const struct metric_manifest *metric = instance->manifests[metric_id];
    if (metric == NULL) {
        return NULL;
    }

    return metric->name;
}

enum metric_type fieldstat_get_metric_type(const struct fieldstat *instance, int metric_id)
{
    if (instance == NULL || metric_id < 0 || metric_id >= instance->manifest_cnt) {
        return (enum metric_type)(-1);
    }
    const struct metric_manifest *metric = instance->manifests[metric_id];
    if (metric == NULL) {
        return (enum metric_type)(-1);
    }

    return metric->type;
}

void fieldstat_get_cells_used_by_cube(const struct fieldstat *instance, int cube_id, struct fieldstat_tag_list **tag_list, size_t *n_cell)
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