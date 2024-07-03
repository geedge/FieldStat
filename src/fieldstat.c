
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
#define DEFAULT_N_CUBE 64


struct cube_manager_item {
    char *key;
    size_t key_len;
    int cell_id;
    UT_hash_handle hh;
};

struct cube_manager {
    struct cube_manager_item *head;
};


struct fieldstat {
    struct cube **cube;
    unsigned long *cube_version; // increase from 0 every time the cube is deleted
    unsigned long cell_version; // increase from 0 every time fieldstat_reset is called
    size_t max_n_cube;

    struct metric_manifest **metric_masters;
    size_t max_n_metric_master;

    struct cube_manager *shared_tag_cube_manager;
};

void cube_manager_free(struct cube_manager *pthis)
{
    struct cube_manager_item *node = NULL;
    struct cube_manager_item *tmp = NULL;
    struct cube_manager_item *head = pthis->head;
    HASH_ITER(hh, head, node, tmp) {
        HASH_DEL(head, node);
        free(node->key);
        free(node);
    }
    free(pthis);
}

struct cube_manager *cube_manager_new()
{
    struct cube_manager *pthis = (struct cube_manager *)malloc(sizeof(struct cube_manager));
    pthis->head = NULL;
    return pthis;
}

static char *key_dup(const char *key, size_t key_len)
{
    char *new_key = (char *)malloc(key_len);
    memcpy(new_key, key, key_len);
    return new_key;
}

void cube_manager_add(struct cube_manager *pthis, const char *key, size_t key_len, int id)
{
    struct cube_manager_item *node = (struct cube_manager_item *)malloc(sizeof(struct cube_manager_item));
    node->key = key_dup(key, key_len);
    node->key_len = key_len;
    node->cell_id = id;
    HASH_ADD_KEYPTR(hh, pthis->head, node->key, key_len, node);
}

void cube_manager_delete(struct cube_manager *pthis, const char *key, size_t key_len)
{
    struct cube_manager_item *node = NULL;
    HASH_FIND(hh, pthis->head, key, key_len, node);
    if (node != NULL) {
        HASH_DEL(pthis->head, node);
        free(node->key);
        free(node);
    }
}

int cube_manager_find(const struct cube_manager *pthis, const char *key, size_t key_len)
{
    struct cube_manager_item *node = NULL;
    HASH_FIND(hh, pthis->head, key, key_len, node);
    if (node == NULL) {
        return -1;
    } else {
        return node->cell_id;
    }
}

void cube_manager_calibrate(struct cube_manager *pthis, const struct cube_manager *master)
{
    struct cube_manager_item *node = NULL;
    struct cube_manager_item *tmp = NULL;

    // exist in self but not in master
    HASH_ITER(hh, pthis->head, node, tmp) {
        int cube_id = cube_manager_find(master, node->key, node->key_len);
        if (cube_id == -1) {
            HASH_DEL(pthis->head, node);
            free(node->key);
            free(node);
        } else {
            node->cell_id = cube_id;
        }
    }

    // exist in master but not in self
    HASH_ITER(hh, master->head, node, tmp) {
        int cube_id = cube_manager_find(pthis, node->key, node->key_len);
        if (cube_id == -1) {
            cube_manager_add(pthis, node->key, node->key_len, node->cell_id);
        }
    }
}

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
    for (size_t i = 0; i < instance->max_n_metric_master; i++) {
        if (instance->metric_masters[i] == NULL) {
            continue;
        }
        if (strcmp(instance->metric_masters[i]->name, name) == 0) {
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

    instance->max_n_cube = DEFAULT_N_CUBE;
    instance->cube = calloc(instance->max_n_cube, sizeof(struct cube *));
    instance->cube_version = calloc(instance->max_n_cube, sizeof(unsigned long));

    instance->max_n_metric_master = DEFAULT_N_METRIC;
    instance->metric_masters = calloc(instance->max_n_metric_master, sizeof(struct metric_manifest *));

    instance->shared_tag_cube_manager = cube_manager_new();

    return instance;
}

void fieldstat_cube_free(struct fieldstat *instance, int cube_id);
void fieldstat_free(struct fieldstat *instance)
{
    if (instance == NULL) {
        return;
    }
    for (size_t i = 0; i < instance->max_n_cube; i++) {
        fieldstat_cube_free(instance, i);
    }
    free(instance->cube);
    free(instance->cube_version);
    cube_manager_free(instance->shared_tag_cube_manager);

    for (size_t i = 0; i < instance->max_n_metric_master; i++) {
        if (instance->metric_masters[i] != NULL) {
            metric_manifest_free(instance->metric_masters[i]);
        }
    }
    free(instance->metric_masters);

    free(instance);
}

void fieldstat_reset(struct fieldstat *instance)
{
    if (instance == NULL) {
        return;
    }
    for (size_t i = 0; i < instance->max_n_cube; i++) {
        struct cube *cube = instance->cube[i];
        if (cube == NULL) {
            continue;
        }

        cube_reset(cube);
    }
    instance->cell_version++;
}

unsigned long fieldstat_get_version(const struct fieldstat *instance)
{
    if (instance == NULL) {
        return 0;
    }
    return instance->cell_version;
}

void get_cube_key(const struct cube *cube, char **key, size_t *key_len)
{
    struct fieldstat_tag_list *shared_tag = cube_get_identifier(cube);
    build_dynamic_cell_key(shared_tag->tag, shared_tag->n_tag, key, key_len);
    fieldstat_tag_list_arr_free(shared_tag, 1);
}

int fieldstat_destroy_cube(struct fieldstat *instance, int cube_id)
{
    if (instance == NULL) {
        return FS_ERR_NULL_HANDLER;
    }
    if (cube_id < 0 || cube_id >= instance->max_n_cube) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    if (instance->cube[cube_id] == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }

    const struct cube *cube = instance->cube[cube_id];
    char *shared_tag_key = NULL;
    size_t shared_tag_key_len = 0;
    get_cube_key(cube, &shared_tag_key, &shared_tag_key_len);
    cube_manager_delete(instance->shared_tag_cube_manager, shared_tag_key, shared_tag_key_len);

    fieldstat_cube_free(instance, cube_id);

    instance->cube[cube_id] = NULL;
    instance->cube_version[cube_id]++;

    free(shared_tag_key);
    return 0;
}

long long fieldstat_get_cube_version(const struct fieldstat *instance, int cube_id)
{
    if (instance == NULL) {
        return FS_ERR_NULL_HANDLER;
    }
    if (cube_id < 0 || cube_id >= instance->max_n_cube) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    if (instance->cube[cube_id] == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }

    return instance->cube_version[cube_id];
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

void add_cube_to_position(struct fieldstat *instance, struct cube *cube, int cube_id)
{
    if (cube_id >= instance->max_n_cube) {
        struct cube **old_cube_arr = instance->cube;
        instance->cube = calloc(instance->max_n_cube * 2, sizeof(struct cube *));
        memcpy(instance->cube, old_cube_arr, sizeof(struct cube *) * instance->max_n_cube);
        free(old_cube_arr);

        unsigned long *old_ver_arr = instance->cube_version;
        instance->cube_version = calloc(instance->max_n_cube * 2, sizeof(unsigned long));
        memcpy(instance->cube_version, old_ver_arr, sizeof(unsigned long) * instance->max_n_cube);
        free(old_ver_arr);

        instance->max_n_cube *= 2;
    }
    instance->cube[cube_id] = cube;
}

int fieldstat_append_cube_to_instance(struct fieldstat *instance, struct cube *cube)
{
    for (int i = 0; i < instance->max_n_cube; i++) {
        if (instance->cube[i] == NULL) {
            instance->cube[i] = cube;
            return i;
        }
    }

    int cube_id = instance->max_n_cube;
    add_cube_to_position(instance, cube, cube_id);

    return cube_id;
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
    
    char *shared_tag_key = NULL;
    size_t shared_tag_key_len = 0;
    build_dynamic_cell_key(shared_tags, n_tag, &shared_tag_key, &shared_tag_key_len);
    int ret = cube_manager_find(instance->shared_tag_cube_manager, shared_tag_key, shared_tag_key_len);
    if (ret != -1) {
        free(shared_tag_key);
        return FS_ERR_INVALID_KEY;
    }

    struct cube *cube = cube_new(shared_tags, n_tag, mode, max_n_cell);

    int cube_id = fieldstat_append_cube_to_instance(instance, cube);
    cube_manager_add(instance->shared_tag_cube_manager, shared_tag_key, shared_tag_key_len, cube_id);
    free(shared_tag_key);

    return cube_id;
}

void fieldstat_cube_free(struct fieldstat *instance, int cube_id)
{
    struct cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        return;
    }
    
    char *shared_tag_key = NULL;
    size_t shared_tag_key_len = 0;
    get_cube_key(cube, &shared_tag_key, &shared_tag_key_len);
    cube_manager_delete(instance->shared_tag_cube_manager, shared_tag_key, shared_tag_key_len);
    free(shared_tag_key);

    cube_free(cube);
    instance->cube[cube_id] = NULL;
}

int fieldstat_cube_set_primary_metric(struct fieldstat *instance, int cube_id, int metric_id)
{
    if (instance == NULL) {
        return FS_ERR_NULL_HANDLER;
    }
    if (cube_id < 0 || cube_id >= instance->max_n_cube) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    struct cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    if (metric_id < 0 || metric_id >= instance->max_n_metric_master) {
        return FS_ERR_INVALID_METRIC_ID;
    }
    if (instance->metric_masters[metric_id] == NULL) {
        return FS_ERR_INVALID_METRIC_ID;
    }
    if (instance->metric_masters[metric_id]->type != METRIC_TYPE_COUNTER) {
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
    if (metric_id >= instance->max_n_metric_master) {
        instance->metric_masters = realloc(instance->metric_masters, sizeof(struct metric_manifest *) * instance->max_n_metric_master * 2);
        memset(instance->metric_masters + instance->max_n_metric_master, 0, sizeof(struct metric_manifest *) * (instance->max_n_metric_master));
        instance->max_n_metric_master *= 2;
    }

    instance->metric_masters[metric_id] = (struct metric_manifest *)manifest;
}

static int append_manifest_to_instance(struct fieldstat *instance, const struct metric_manifest *metric)
{
    int metric_id = 0;
    for ( ;metric_id < instance->max_n_metric_master; metric_id++) {
        if (instance->metric_masters[metric_id] == NULL) {
            break;
        }
    }

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

    if (cube_id < 0 || cube_id >= instance->max_n_cube) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    if (instance->cube[cube_id] == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }

    if (metric_id < 0 || metric_id >= instance->max_n_metric_master) {
        return FS_ERR_INVALID_METRIC_ID;
    }
    const struct metric_manifest *metric = instance->metric_masters[metric_id];
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

    struct cube *cube = instance->cube[cube_id];
    const struct metric_manifest *manifest = instance->metric_masters[metric_id];
    
    return cube_counter_incrby(cube, manifest, tags, n_tag, increment);

}

int fieldstat_counter_set(struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag *tags, size_t n_tag, long long value)
{
    int ret = check_before_add(instance, cube_id, metric_id, METRIC_TYPE_COUNTER);
    if (ret != FS_OK) {
        return ret;
    }
    struct cube *cube = instance->cube[cube_id];
    const struct metric_manifest *manifest = instance->metric_masters[metric_id];

    return cube_counter_set(cube, manifest, tags, n_tag, value);
}

int fieldstat_hll_add(struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag *tags, size_t n_tag, const char *key, size_t key_len)
{
    int ret = check_before_add(instance, cube_id, metric_id, METRIC_TYPE_HLL);
    if (ret != FS_OK) {
        return ret;
    }
    
    struct cube *cube = instance->cube[cube_id];
    const struct metric_manifest *manifest = instance->metric_masters[metric_id];

    return cube_hll_add(cube, manifest, tags, n_tag, key, key_len);
}

int fieldstat_hist_record(struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag *tags, size_t n_tag, long long value)
{
    int ret = check_before_add(instance, cube_id, metric_id, METRIC_TYPE_HISTOGRAM);
    if (ret != FS_OK) {
        return ret;
    }

    struct cube *cube = instance->cube[cube_id];
    const struct metric_manifest *manifest = instance->metric_masters[metric_id];

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

    int metric_len_src = src->max_n_metric_master;
    for (int i = 0; i < metric_len_src; i++) {
        const struct metric_manifest *metric_src = src->metric_masters[i];
        const struct metric_manifest *metric_dst = instance->metric_masters[i];
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
        if (instance->metric_masters[i] != NULL || src->metric_masters[i] == NULL) {
            continue;
        }
        const struct metric_manifest *metric_src = src->metric_masters[i];
        append_manifest_to_instance(instance, metric_manifest_copy(metric_src));
    }

    struct cube_manager *tag_cube_id_map = instance->shared_tag_cube_manager;
    int ret = 0;
    for (int i = 0; i < src->max_n_cube; i++) {
        const struct cube *cube_src = src->cube[i];
        if (cube_src == NULL) {
            continue;
        }

        struct fieldstat_tag_list *shared_tag_list = cube_get_identifier(cube_src);
        char *shared_tag_key_src = NULL;
        size_t shared_tag_key_len = 0;
        build_dynamic_cell_key(shared_tag_list->tag, shared_tag_list->n_tag, &shared_tag_key_src, &shared_tag_key_len);
        int cube_id_dest = cube_manager_find(tag_cube_id_map, shared_tag_key_src, shared_tag_key_len);
        if (cube_id_dest == -1) {
            struct cube *copied_cube = cube_copy(cube_src);
            cube_id_dest = fieldstat_append_cube_to_instance(instance, copied_cube);
            cube_manager_add(tag_cube_id_map, shared_tag_key_src, shared_tag_key_len, cube_id_dest);
        } else {
            struct cube *cube_dst = instance->cube[cube_id_dest];
            cube_merge(cube_dst, cube_src);
        }
        fieldstat_tag_list_arr_free(shared_tag_list, 1);
        free(shared_tag_key_src);
    }

    return ret;
}

struct fieldstat *fieldstat_fork(const struct fieldstat *instance)
{
    if (instance == NULL) {
        return NULL;
    }
    struct fieldstat *new_instance = calloc(1, sizeof(struct fieldstat));
    new_instance->shared_tag_cube_manager = cube_manager_new();

    new_instance->max_n_cube = instance->max_n_cube;
    new_instance->cube = calloc(new_instance->max_n_cube, sizeof(struct cube *));
    for (size_t i = 0; i < new_instance->max_n_cube; i++) {
        const struct cube *cube = instance->cube[i];
        if (cube == NULL) {
            continue;
        }
        new_instance->cube[i] = cube_fork(cube);
        
        char *shared_tag_key = NULL;
        size_t shared_tag_key_len = 0;
        get_cube_key(cube, &shared_tag_key, &shared_tag_key_len);
        cube_manager_add(new_instance->shared_tag_cube_manager, shared_tag_key, shared_tag_key_len, i);
        free(shared_tag_key);
    }
    new_instance->cube_version = calloc(new_instance->max_n_cube, sizeof(unsigned long));
    memcpy(new_instance->cube_version, instance->cube_version, sizeof(unsigned long) * new_instance->max_n_cube);

    new_instance->metric_masters = calloc(instance->max_n_metric_master, sizeof(struct metric_manifest *));
    new_instance->max_n_metric_master = instance->max_n_metric_master;
    for (size_t i = 0; i < instance->max_n_metric_master; i++) {
        if (instance->metric_masters[i] != NULL) {
            new_instance->metric_masters[i] = metric_manifest_copy(instance->metric_masters[i]);
        }
    }

    return new_instance;
}

void calibrate_metrics_in_instance(const struct fieldstat *master, struct fieldstat *replica)
{
    if (replica->max_n_metric_master < master->max_n_metric_master) {
        replica->metric_masters = (struct metric_manifest **)realloc(replica->metric_masters, sizeof(struct metric_manifest *) * master->max_n_metric_master);
        memset(replica->metric_masters + replica->max_n_metric_master, 0, sizeof(struct metric_manifest *) * (master->max_n_metric_master - replica->max_n_metric_master));
        replica->max_n_metric_master = master->max_n_metric_master;
    }

    size_t longer_arr_len = master->max_n_metric_master > replica->max_n_metric_master ? master->max_n_metric_master : replica->max_n_metric_master;
    for (size_t i = 0; i < longer_arr_len; i++) {
        const struct metric_manifest *metric_master = i >= master->max_n_metric_master ? NULL : master->metric_masters[i];
        struct metric_manifest *metric_target = i >= replica->max_n_metric_master ? NULL : replica->metric_masters[i];
        if (metric_master == NULL && metric_target == NULL) {
            continue;
        }
        if (metric_master == NULL && metric_target != NULL) {
            metric_manifest_free(metric_target);
            replica->metric_masters[i] = NULL;
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
}

int fieldstat_calibrate(const struct fieldstat *master, struct fieldstat *replica)
{
    if (master == NULL || replica == NULL) {
        return FS_ERR_NULL_HANDLER;
    }

    if (replica->max_n_cube < master->max_n_cube) {
        replica->cube = (struct cube **)realloc(replica->cube, sizeof(struct cube *) * master->max_n_cube);
        memset(replica->cube + replica->max_n_cube, 0, sizeof(struct cube *) * (master->max_n_cube - replica->max_n_cube));
        replica->cube_version = (unsigned long *)realloc(replica->cube_version, sizeof(unsigned long) * master->max_n_cube);
        memset(replica->cube_version + replica->max_n_cube, 0, sizeof(unsigned long) * (master->max_n_cube - replica->max_n_cube));
        replica->max_n_cube = master->max_n_cube;
    }

    size_t len_master = master->max_n_cube;
    size_t len_replica = replica->max_n_cube;
    size_t longer_arr_len = len_master > len_replica ? len_master : len_replica;
    for (size_t i = 0; i < longer_arr_len; i++) {
        const struct cube *cube_master = i >= len_master ? NULL : master->cube[i];
        struct cube *cube_target = i >= len_replica ? NULL : replica->cube[i];

        if (cube_master == NULL && cube_target == NULL) {
            continue;
        }
        if (cube_master == NULL && cube_target != NULL) {
            cube_free(cube_target);
            replica->cube[i] = NULL;
            continue;
        }
        if (cube_master != NULL && cube_target == NULL) {
            struct cube *cube_dup = cube_fork(cube_master);
            add_cube_to_position(replica, cube_dup, i);
            continue;
        }

        struct fieldstat_tag_list *cube_master_identifier = cube_get_identifier(cube_master);
        struct fieldstat_tag_list *cube_target_identifier = cube_get_identifier(cube_target);
        bool is_same = cube_master_identifier->n_tag == cube_target_identifier->n_tag && is_tag_array_same(cube_master_identifier->tag, cube_target_identifier->tag, cube_master_identifier->n_tag);
        fieldstat_tag_list_arr_free(cube_master_identifier, 1);
        fieldstat_tag_list_arr_free(cube_target_identifier, 1);
        if (master->cube_version[i] == replica->cube_version[i] && is_same) {
            continue;
        }

        cube_free(cube_target);
        struct cube *cube_dup = cube_fork(cube_master);
        add_cube_to_position(replica, cube_dup, i);
    }

    memcpy(replica->cube_version, master->cube_version, sizeof(unsigned long) * master->max_n_cube);

    cube_manager_calibrate(replica->shared_tag_cube_manager, master->shared_tag_cube_manager);
    calibrate_metrics_in_instance(master, replica);

    return FS_OK;
}

/* -------------------------------------------------------------------------- */
/*                                    query                                   */
/* -------------------------------------------------------------------------- */
void fieldstat_get_cubes(const struct fieldstat *instance, int **cube_ids, int *n_cube)
{
    int all_available_cube_count = 0;

    int *tmp_ids = (int *)malloc(sizeof(int) * instance->max_n_cube);
    for (int i = 0; i < instance->max_n_cube; i++) {
        const struct cube *tmp_cube = instance->cube[i];
        if (tmp_cube == NULL) {
            continue;
        }
        tmp_ids[all_available_cube_count] = i;
        all_available_cube_count ++;
    }
    if (all_available_cube_count == 0) {
        free(tmp_ids);
        *cube_ids = NULL;
        *n_cube = 0;
        return;
    }

    *cube_ids = (int *)malloc(sizeof(int) * all_available_cube_count);
    memcpy(*cube_ids, tmp_ids, sizeof(int) * all_available_cube_count);
    *n_cube = all_available_cube_count;

    free(tmp_ids);
}

void fieldstat_get_metrics(const struct fieldstat *instance, int **metric_id_out, size_t *n_metric)
{
    int *tmp_ids = (int *)malloc(sizeof(int) * instance->max_n_metric_master);
    *metric_id_out = tmp_ids;
    int cnt = 0;
    for (int i = 0; i < instance->max_n_metric_master; i++) {
        if (instance->metric_masters[i] != NULL) {
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
    if (instance == NULL || cube_id >= instance->max_n_cube || cube_id < 0) {
        return NULL;
    }
    const struct cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        return NULL;
    }
    
    return cube_get_identifier(cube);
}

int fieldstat_counter_get(const struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag_list *tags, long long *value)
{
    if (cube_id < 0 || cube_id >= instance->max_n_cube) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    const struct cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    
    return cube_counter_get(cube, metric_id, tags, value);
}

int fieldstat_hll_get(const struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag_list *tags, double *value)
{
    if (cube_id < 0 || cube_id >= instance->max_n_cube) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    const struct cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    
    int ret = cube_hll_get(cube, metric_id, tags, value);

    return ret;
}

long long fieldstat_hist_value_at_percentile(const struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag_list *tags, double percentile)
{
    if (cube_id < 0 || cube_id >= instance->max_n_cube) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    const struct cube *cube = instance->cube[cube_id];
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
    if (cube_id < 0 || cube_id >= instance->max_n_cube) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    const struct cube *cube = instance->cube[cube_id];
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
    *blob = NULL;
    *blob_size = 0;
    if (cube_id < 0 || cube_id >= instance->max_n_cube) {
        return;
    }
    const struct cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
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
    if (metric_id < 0 || metric_id >= instance->max_n_metric_master) {
        return NULL;
    }
    const struct metric_manifest *metric = instance->metric_masters[metric_id];
    if (metric == NULL) {
        return NULL;
    }

    return metric->name;
}

enum metric_type fieldstat_get_metric_type(const struct fieldstat *instance, int metric_id)
{
    if (instance == NULL || metric_id < 0 || metric_id >= instance->max_n_metric_master) {
        return (enum metric_type)(-1);
    }
    const struct metric_manifest *metric = instance->metric_masters[metric_id];
    if (metric == NULL) {
        return (enum metric_type)(-1);
    }

    return metric->type;
}

void fieldstat_get_cells_used_by_cube(const struct fieldstat *instance, int cube_id, struct fieldstat_tag_list **tag_list, size_t *n_cell)
{
    if (instance == NULL || cube_id < 0 || cube_id >= instance->max_n_cube) {
        return;
    }
    const struct cube *cube = instance->cube[cube_id];
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
    if (cube_id < 0 || cube_id >= instance->max_n_cube) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    const struct cube *cube = instance->cube[cube_id];
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
    const struct cube_manager *tag_cube_id_map = instance->shared_tag_cube_manager;
    char *shared_tag_key = NULL;
    size_t shared_tag_key_len = 0;
    build_dynamic_cell_key(shared_tags, n_shared_tags, &shared_tag_key, &shared_tag_key_len);
    int cube_id = cube_manager_find(tag_cube_id_map, shared_tag_key, shared_tag_key_len);
    free(shared_tag_key);
    if (cube_id == -1) {
        return FS_ERR_INVALID_KEY;
    }

    return cube_id;
}

void fieldstat_get_metric_in_cell(const struct fieldstat *instance, int cube_id, const struct fieldstat_tag_list *tags, int **metric_id_out, size_t *n_metric_out)
{
    return cube_get_cells_used_by_metric(instance->cube[cube_id], tags, metric_id_out, n_metric_out);
}