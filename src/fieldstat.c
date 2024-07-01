
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "cjson/cJSON.h"
#include "uthash.h"
#include "serializer.h"
#include "fieldstat.h"
#include "metrics/metric.h"
#include "cell_manager.h"
#include "heavy_keeper.h"
#include "tag_map.h"

#define DEFAULT_N_METRIC 64
#define DEFAULT_N_CUBE 128


struct exdata {
    struct metric **metrics;
    size_t valid_metric_arr_len;
    size_t max_n_metric;
    struct fieldstat_tag_list tags;

    const struct metric_name_id_map *metric_reference; // used in merge, point to fieldstat->metric_name_id_map
};

union cell_manager {
    struct heavy_keeper *topk;
    struct tag_map *comprehensive;
};

struct fs_cube {
    enum sampling_mode sampling_mode;
    union cell_manager cells;
    size_t max_n_cell;

    // the key of cube is the combination of shared tags
    struct fieldstat_tag *shared_tags;
    size_t n_shared_tags;
    struct tag_hash_key *key_tag;

    int primary_metric_id;
};

struct metric_name_id_item {
    char *name;
    int id;
    UT_hash_handle hh;
};

struct metric_name_id_map {
    struct metric_name_id_item *map;
};

struct exdata_new_args {
    const struct fieldstat_tag *tags;
    size_t n_tags;
    // struct metric_name_id_map *metric_reference;
};

struct fieldstat {
    struct fs_cube **cube;
    unsigned long *cube_version; // increase from 0 every time the cube is deleted
    unsigned long cell_version; // increase from 0 every time fieldstat_reset is called
    size_t valid_cube_arr_length;
    size_t max_n_cube;

    struct metric **metric_masters;
    size_t n_metric_master;
    size_t max_n_metric_master;
    struct metric_name_id_map *metric_name_id_map;

    struct cube_manager *shared_tag_cube_manager;
};


/* -------------------------------------------------------------------------- */
/*                              metric and exdata                             */
/* -------------------------------------------------------------------------- */
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

int name_id_map_get_id_by_name(const struct metric_name_id_map *map, const char *metric_name)
{
    struct metric_name_id_item *entry = NULL;
    HASH_FIND_STR(map->map, metric_name, entry);
    if (entry == NULL) {
        return -1;
    }
    return entry->id;
}

void name_id_map_add(struct metric_name_id_map *map, const char *name, int id) {
    struct metric_name_id_item *entry = malloc(sizeof(struct metric_name_id_item));
    entry->id = id;
    entry->name = strdup(name);
    HASH_ADD_KEYPTR(hh, map->map, entry->name, strlen(entry->name), entry);
}

void name_id_map_free(struct metric_name_id_map *map)
{
    struct metric_name_id_item *entry, *tmp;
    HASH_ITER(hh, map->map, entry, tmp) {
        HASH_DEL(map->map, entry);
        free(entry->name);
        free(entry);
    }

    free(map);
}

struct metric_name_id_map *name_id_map_copy(struct metric_name_id_map *map)
{
    struct metric_name_id_map *map_dup = malloc(sizeof(struct metric_name_id_map));
    map_dup->map = NULL;
    struct metric_name_id_item *entry, *tmp;
    HASH_ITER(hh, map->map, entry, tmp) {
        name_id_map_add(map_dup, entry->name, entry->id);
    }
    return map_dup;
}

void add_metric_to_exdata(struct exdata *exdata, struct metric *metric, int metric_id)
{
    if (metric_id >= exdata->max_n_metric) {
        exdata->max_n_metric *= 2;
        exdata->metrics = realloc(exdata->metrics, sizeof(struct metric *) * exdata->max_n_metric);
        memset(exdata->metrics + exdata->valid_metric_arr_len, 0, sizeof(struct metric *) * (exdata->max_n_metric - exdata->valid_metric_arr_len));
    }

    exdata->metrics[metric_id] = metric;
    if (metric_id >= exdata->valid_metric_arr_len) {
        exdata->valid_metric_arr_len = metric_id + 1;
    }
}

struct metric *find_metric_in_exdata(const struct exdata *exdata, int metric_id)
{
    if (metric_id >= exdata->valid_metric_arr_len) {
        return NULL;
    }
    return exdata->metrics[metric_id];
}

struct metric *construct_or_find_metric_to_exdata(struct fieldstat *instance, struct exdata *exdata, int metric_id)
{
    struct metric *metric = find_metric_in_exdata(exdata, metric_id);
    if (metric != NULL) {
        return metric;
    }
   
    metric = metric_fork(instance->metric_masters[metric_id]);
    add_metric_to_exdata(exdata, metric, metric_id);
    return metric;
}

struct exdata *exdata_new(const struct exdata_new_args *args) {
    struct exdata *pthis = malloc(sizeof(struct exdata));
    pthis->metrics = calloc(DEFAULT_N_METRIC, sizeof(struct metric *));
    pthis->valid_metric_arr_len = 0;
    pthis->max_n_metric = DEFAULT_N_METRIC;

    pthis->tags.n_tag = args->n_tags;
    pthis->tags.tag = malloc(sizeof(struct fieldstat_tag) * args->n_tags);
    tag_array_copy(pthis->tags.tag, args->tags, args->n_tags);

    pthis->metric_reference = NULL;
    return pthis;
}

void exdata_free(struct exdata *pthis) {
    for (size_t i = 0; i < pthis->valid_metric_arr_len; i++) {
        metric_free(pthis->metrics[i]);
    }
    free(pthis->metrics);
    for (size_t i = 0; i < pthis->tags.n_tag; i++) {
        free((char *)pthis->tags.tag[i].key);
        if (pthis->tags.tag[i].type == TAG_CSTRING) {
            free((char *)pthis->tags.tag[i].value_str);
        }
    }
    free(pthis->tags.tag);
    free(pthis);
}

struct exdata *exdata_copy(const struct exdata *src) {
    struct exdata *pthis = malloc(sizeof(struct exdata));
    pthis->metrics = calloc(src->max_n_metric, sizeof(struct metric *));
    pthis->valid_metric_arr_len = src->valid_metric_arr_len;
    pthis->max_n_metric = src->max_n_metric;
    for (size_t i = 0; i < src->valid_metric_arr_len; i++) {
        if (src->metrics[i] == NULL) {
            continue;
        }

        pthis->metrics[i] = metric_copy(src->metrics[i]);
    }
    
    pthis->tags.n_tag = src->tags.n_tag;
    pthis->tags.tag = malloc(sizeof(struct fieldstat_tag) * src->tags.n_tag);
    tag_array_copy(pthis->tags.tag, src->tags.tag, src->tags.n_tag);

    pthis->metric_reference = NULL;

    return pthis;
}

void exdata_reset(struct exdata *pthis) {
    for (size_t i = 0; i < pthis->valid_metric_arr_len; i++) {
        if (pthis->metrics[i] == NULL) {
            continue;
        }
        metric_reset(pthis->metrics[i]);
    }
}

void exdata_merge(struct exdata *dest, const struct exdata *src) {
    for (size_t i = 0; i < src->valid_metric_arr_len; i++) {
        const struct metric *metric_src = src->metrics[i];
        if (metric_src == NULL) {
            continue;
        }
        struct metric *metric_dst = find_metric_in_exdata(dest, i);
        assert(strcmp(metric_get_name(metric_src), metric_get_name(metric_dst)) == 0);

        if (metric_dst == NULL) {
            metric_dst = metric_copy(metric_src);
            add_metric_to_exdata(dest, metric_dst, i);
        } else {
            metric_merge(metric_dst, metric_src);
        }
    }
}

void *exdata_new_i(void *arg) {
    return exdata_new((struct exdata_new_args *)arg);
}

void exdata_free_i(void *exdata) {
    exdata_free((struct exdata *)exdata);
}

void exdata_reset_i(void *exdata) {
    exdata_reset((struct exdata *)exdata);
}

void exdata_merge_i(void *dest, void *src) {
    exdata_merge((struct exdata *)dest, (struct exdata *)src);
}

void *exdata_copy_i(void *exdata) {
    return exdata_copy((struct exdata *)exdata);
}


struct fieldstat *fieldstat_new()
{
    struct fieldstat *instance = calloc(1, sizeof(struct fieldstat));

    instance->max_n_cube = DEFAULT_N_CUBE;
    instance->cube = calloc(instance->max_n_cube, sizeof(struct fs_cube *));
    instance->cube_version = calloc(instance->max_n_cube, sizeof(unsigned long));

    instance->max_n_metric_master = DEFAULT_N_METRIC;
    instance->metric_masters = calloc(instance->max_n_metric_master, sizeof(struct metric *));
    instance->metric_name_id_map = calloc(1, sizeof(struct metric_name_id_map));

    instance->shared_tag_cube_manager = cube_manager_new();

    return instance;
}

void fieldstat_cube_free(struct fieldstat *instance, int cube_id);
void fieldstat_free(struct fieldstat *instance)
{
    if (instance == NULL) {
        return;
    }
    for (size_t i = 0; i < instance->valid_cube_arr_length; i++) {
        fieldstat_cube_free(instance, i);
    }
    free(instance->cube);
    free(instance->cube_version);
    cube_manager_free(instance->shared_tag_cube_manager);

    for (size_t i = 0; i < instance->n_metric_master; i++) {
        metric_free(instance->metric_masters[i]);
    }
    free(instance->metric_masters);
    name_id_map_free(instance->metric_name_id_map);

    free(instance);
}

void fieldstat_reset(struct fieldstat *instance)
{
    if (instance == NULL) {
        return;
    }
    for (size_t i = 0; i < instance->valid_cube_arr_length; i++) {
        struct fs_cube *cube = instance->cube[i];
        if (cube == NULL) {
            continue;
        }

        if (cube->sampling_mode == SAMPLING_MODE_TOPK) {
            heavy_keeper_reset(cube->cells.topk);
        } else {
            tag_map_reset(cube->cells.comprehensive);
        }
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

int fieldstat_destroy_cube(struct fieldstat *instance, int cube_id)
{
    if (instance == NULL) {
        return FS_ERR_NULL_HANDLER;
    }
    if (cube_id < 0 || cube_id >= instance->valid_cube_arr_length) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    if (instance->cube[cube_id] == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }

    const struct fs_cube *cube = instance->cube[cube_id];
    cube_manager_delete(instance->shared_tag_cube_manager, cube->key_tag);

    fieldstat_cube_free(instance, cube_id);

    instance->cube[cube_id] = NULL;
    instance->cube_version[cube_id]++;
    return 0;
}

long long fieldstat_get_cube_version(const struct fieldstat *instance, int cube_id)
{
    if (instance == NULL) {
        return FS_ERR_NULL_HANDLER;
    }
    if (cube_id < 0 || cube_id >= instance->valid_cube_arr_length) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    if (instance->cube[cube_id] == NULL && instance->cube_version[cube_id] == 0) {
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

void add_cube_to_position(struct fieldstat *instance, struct fs_cube *cube, int cube_id)
{
    if (cube_id >= instance->max_n_cube) {
        instance->max_n_cube *= 2;
        struct fs_cube **old_cube_arr = instance->cube;
        instance->cube = calloc(instance->max_n_cube, sizeof(struct fs_cube *));
        memcpy(instance->cube, old_cube_arr, sizeof(struct fs_cube *) * instance->valid_cube_arr_length);
        free(old_cube_arr);

        unsigned long *old_ver_arr = instance->cube_version;
        instance->cube_version = calloc(instance->max_n_cube, sizeof(unsigned long));
        memcpy(instance->cube_version, old_ver_arr, sizeof(unsigned long) * instance->valid_cube_arr_length);
        free(old_ver_arr);
    }
    instance->cube[cube_id] = cube;
    if (cube_id >= instance->valid_cube_arr_length) {
        instance->valid_cube_arr_length = cube_id + 1;
    }
}

int fieldstat_append_cube_to_instance(struct fieldstat *instance, struct fs_cube *cube)
{
    for (int i = 0; i < instance->valid_cube_arr_length; i++) {
        if (instance->cube[i] == NULL) {
            instance->cube[i] = cube;
            cube_manager_add(instance->shared_tag_cube_manager, cube->key_tag, i);
            return i;
        }
    }
    int cube_id = instance->valid_cube_arr_length;
    add_cube_to_position(instance, cube, cube_id);
    cube_manager_add(instance->shared_tag_cube_manager, cube->key_tag, cube_id);

    return cube_id;
}

struct fs_cube *fieldstat_cube_info_init(const struct fieldstat_tag *shared_tags, size_t n_tag, enum sampling_mode mode, size_t max_n_cell)
{
    struct fs_cube *cube = calloc(1, sizeof(struct fs_cube));
    cube->sampling_mode = mode;

    if (n_tag == 0) {
        cube->shared_tags = NULL;
    } else {
        cube->shared_tags = malloc(sizeof(struct fieldstat_tag) * n_tag);
        tag_array_copy(cube->shared_tags, shared_tags, n_tag);
    }

    cube->n_shared_tags = n_tag;

    struct tag_hash_key *shared_tag_key = malloc(sizeof(struct tag_hash_key));
    tag_hash_key_init_with_fieldstat_tag(shared_tag_key, shared_tags, n_tag, true);
    cube->key_tag = shared_tag_key;

    cube->max_n_cell = max_n_cell;

    return cube;
}

struct fs_cube *fieldstat_cube_new(const struct fieldstat_tag *shared_tags, size_t n_tag, enum sampling_mode mode, size_t max_n_cell)
{
    struct fs_cube *cube = fieldstat_cube_info_init(shared_tags, n_tag, mode, max_n_cell);

    switch (mode)
    {
        case SAMPLING_MODE_TOPK:
            cube->cells.topk = heavy_keeper_new(max_n_cell);
            heavy_keeper_set_exdata_schema(cube->cells.topk, exdata_new_i, exdata_free_i, exdata_merge_i, exdata_reset_i, exdata_copy_i);
            break;
        case SAMPLING_MODE_COMPREHENSIVE:
            cube->cells.comprehensive = tag_map_new(max_n_cell);
            tag_map_set_exdata_schema(cube->cells.comprehensive, exdata_new_i, exdata_free_i, exdata_merge_i, exdata_reset_i, exdata_copy_i);
            break;
        default:
            assert(0);
            break;
    }

    return cube;
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
    struct tag_hash_key shared_tag_key;
    tag_hash_key_init_with_fieldstat_tag(&shared_tag_key, shared_tags, n_tag, false);
    int ret = cube_manager_find(instance->shared_tag_cube_manager, &shared_tag_key);
    if (ret != -1) {
        return FS_ERR_INVALID_KEY;
    }

    struct fs_cube *cube = fieldstat_cube_new(shared_tags, n_tag, mode, max_n_cell);

    int cube_id = fieldstat_append_cube_to_instance(instance, cube);

    return cube_id;
}

void fieldstat_cube_free_contents(struct fieldstat *instance, int cube_id)
{
    struct fs_cube *cube = instance->cube[cube_id];

    switch (cube->sampling_mode)
    {
    case SAMPLING_MODE_TOPK:
        heavy_keeper_free(cube->cells.topk);
        break;
    case SAMPLING_MODE_COMPREHENSIVE:
        tag_map_free(cube->cells.comprehensive);
        break;
    default:
        assert(0);
        break;
    }

    fieldstat_free_tag_array(cube->shared_tags, cube->n_shared_tags);
    tag_hash_key_free(cube->key_tag);

    free(cube);
    instance->cube[cube_id] = NULL;

    if (cube_id == instance->valid_cube_arr_length - 1) {
        instance->valid_cube_arr_length--;
    }
}

void fieldstat_cube_free(struct fieldstat *instance, int cube_id)
{
    if (instance->cube[cube_id] == NULL) {
        return;
    }
    
    cube_manager_delete(instance->shared_tag_cube_manager, instance->cube[cube_id]->key_tag);

    fieldstat_cube_free_contents(instance, cube_id);
}

int fieldstat_cube_set_primary_metric(struct fieldstat *instance, int cube_id, int metric_id)
{
    if (instance == NULL) {
        return FS_ERR_NULL_HANDLER;
    }
    if (cube_id < 0 || cube_id >= instance->valid_cube_arr_length) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    if (cube->sampling_mode != SAMPLING_MODE_TOPK) {
        return FS_ERR_INVALID_PARAM;
    }
    if (metric_id < 0 || metric_id >= instance->n_metric_master) {
        return FS_ERR_INVALID_METRIC_ID;
    }
    if (instance->metric_masters[metric_id] == NULL) {
        return FS_ERR_INVALID_METRIC_ID;
    }
    if (metric_get_type(instance->metric_masters[metric_id]) != METRIC_TYPE_COUNTER) {
        return FS_ERR_INVALID_PARAM;
    }

    cube->primary_metric_id = metric_id;

    return FS_OK;
}

/* -------------------------------------------------------------------------- */
/*                               metric register                              */
/* -------------------------------------------------------------------------- */

void add_metric_to_instance(struct fieldstat *instance, const struct metric *metric, int metric_id)
{
    if (metric_id >= instance->max_n_metric_master) {
        instance->max_n_metric_master *= 2;
        instance->metric_masters = realloc(instance->metric_masters, sizeof(struct metric *) * instance->max_n_metric_master);
        memset(instance->metric_masters + instance->n_metric_master, 0, sizeof(struct metric *) * (instance->max_n_metric_master - instance->n_metric_master));
    }

    instance->metric_masters[metric_id] = (struct metric *)metric;
    if (metric_id >= instance->n_metric_master) {
        instance->n_metric_master = metric_id + 1;
    }
    name_id_map_add(instance->metric_name_id_map, metric_get_name(metric), metric_id);
}

static int append_metric_to_instance(struct fieldstat *instance, const struct metric *metric)
{
    int metric_id = instance->n_metric_master;
    add_metric_to_instance(instance, metric, metric_id);

    return metric_id;
}

int check_before_register_metric(const struct fieldstat *instance, const char *metric_name)
{
    if (instance == NULL) {
        return FS_ERR_NULL_HANDLER;
    }

    if (name_id_map_get_id_by_name(instance->metric_name_id_map, metric_name) != -1) {
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

    const struct metric *metric = metric_counter_new(metric_name);

    return append_metric_to_instance(instance, metric);
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

    const struct metric *metric = metric_hll_new(metric_name, precision);

    return append_metric_to_instance(instance, metric);
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

    const struct metric *metric = metric_histogram_new(metric_name, lowest_trackable_value, highest_trackable_value, significant_figures);

    return append_metric_to_instance(instance, metric);
}

/* -------------------------------------------------------------------------- */
/*                               metric operation                              */
/* -------------------------------------------------------------------------- */
int check_before_add(const struct fieldstat *instance, int cube_id, int metric_id, enum metric_type type)
{
    if (instance == NULL) {
        return FS_ERR_NULL_HANDLER;
    }

    if (cube_id < 0 || cube_id >= instance->valid_cube_arr_length) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    if (instance->cube[cube_id] == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }

    if (metric_id < 0 || metric_id >= instance->n_metric_master) {
        return FS_ERR_INVALID_METRIC_ID;
    }
    const struct metric *metric = instance->metric_masters[metric_id];
    if (metric == NULL || metric_get_type(metric) != type) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    return FS_OK;
}

struct exdata *find_or_add_exdata_comprehensive(struct tag_map *comprehensive, const struct tag_hash_key *tag_key, struct exdata_new_args *args)
{
    struct exdata *cell_data = tag_map_get0_exdata(comprehensive, tag_key);
    if (cell_data == NULL) {
        int tmp_ret = tag_map_add(comprehensive, tag_key, args);
        if (tmp_ret != 1) {
            return NULL;
        }
        cell_data = tag_map_get0_exdata(comprehensive, tag_key);
    }
    return cell_data;
}

int fieldstat_counter_incrby(struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag *tags, size_t n_tag, long long increment)
{
    int ret = check_before_add(instance, cube_id, metric_id, METRIC_TYPE_COUNTER);
    if (ret != FS_OK) {
        return ret;
    }

    const struct fs_cube *cube = instance->cube[cube_id];
    struct tag_hash_key tag_key;
    tag_hash_key_init_with_fieldstat_tag(&tag_key, tags, n_tag, false);

    struct exdata_new_args args;
    args.tags = tags;
    args.n_tags = n_tag;

    struct exdata *cell_data;
    switch (cube->sampling_mode)
    {
    case SAMPLING_MODE_TOPK:
        if (cube->primary_metric_id != metric_id) {
            cell_data = heavy_keeper_get0_exdata(cube->cells.topk, &tag_key);

            if (cell_data == NULL) {
                int tmp_ret = heavy_keeper_add(cube->cells.topk, &tag_key, 0, &args);
                if (tmp_ret != 1) {
                    return FS_ERR_TOO_MANY_CELLS;
                }
                cell_data = heavy_keeper_get0_exdata(cube->cells.topk, &tag_key);
            }
        } else {
            if (increment < 0) {
                return FS_ERR_INVALID_PARAM;
            } else if (increment == 0) {
                return FS_OK;
            }

            // heavy_keeper_add should be called anyway, to let the topk record update.
            int tmp_ret = heavy_keeper_add(cube->cells.topk, &tag_key, increment, &args);
            if (tmp_ret != 1) {
                return FS_ERR_TOO_MANY_CELLS;
            }
            cell_data = heavy_keeper_get0_exdata(cube->cells.topk, &tag_key);
        }
        break;
    case SAMPLING_MODE_COMPREHENSIVE:
        cell_data = find_or_add_exdata_comprehensive(cube->cells.comprehensive, &tag_key, &args);
        if (cell_data == NULL) {
            return FS_ERR_TOO_MANY_CELLS;
        }
        break;
    default:
        assert(0);
        break;
    }

    struct metric *metric = construct_or_find_metric_to_exdata(instance, cell_data, metric_id);

    metric_counter_incrby(metric, increment);
    return FS_OK;
}


int fieldstat_counter_set(struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag *tags, size_t n_tag, long long value)
{
    int ret = check_before_add(instance, cube_id, metric_id, METRIC_TYPE_COUNTER);
    if (ret != FS_OK) {
        return ret;
    }
    const struct fs_cube *cube = instance->cube[cube_id];
    struct tag_hash_key tag_key;
    tag_hash_key_init_with_fieldstat_tag(&tag_key, tags, n_tag, false);

    struct exdata_new_args args;
    args.tags = tags;
    args.n_tags = n_tag;

    struct exdata *cell_data;
    switch (cube->sampling_mode)
    {
    case SAMPLING_MODE_TOPK: {
        if (cube->primary_metric_id != metric_id) {
            cell_data = heavy_keeper_get0_exdata(cube->cells.topk, &tag_key);
            if (cell_data == NULL) {
                int tmp_ret = heavy_keeper_add(cube->cells.topk, &tag_key, 0, &args);
                if (tmp_ret != 1) {
                    return FS_ERR_TOO_MANY_CELLS;
                }
                cell_data = heavy_keeper_get0_exdata(cube->cells.topk, &tag_key);
            }
        } else {
            long long current_count = 0;
            cell_data = heavy_keeper_get0_exdata(cube->cells.topk, &tag_key);
            if (cell_data != NULL) {
                const struct metric *tmp_metric = find_metric_in_exdata(cell_data, metric_id);
                if (tmp_metric != NULL) {
                    current_count = metric_counter_get(tmp_metric);
                }
            }
            long long increment = value - current_count;
            if (increment < 0) {
                return FS_ERR_INVALID_PARAM;
            } else if (increment == 0) {
                return FS_OK;
            }
            
            int tmp_ret = heavy_keeper_add(cube->cells.topk, &tag_key, increment, &args);
            if (tmp_ret != 1) {
                return FS_ERR_TOO_MANY_CELLS;
            }
            cell_data = heavy_keeper_get0_exdata(cube->cells.topk, &tag_key);
        }
        break;}
    case SAMPLING_MODE_COMPREHENSIVE: {
        cell_data = find_or_add_exdata_comprehensive(cube->cells.comprehensive, &tag_key, &args);
        break;}
    default:
        assert(0);
        break;
    }

    struct metric *metric = construct_or_find_metric_to_exdata(instance, cell_data, metric_id);
    metric_counter_set(metric, value);
    return FS_OK;
}

int fieldstat_hll_add(struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag *tags, size_t n_tag, const char *key, size_t key_len)
{
    int ret = check_before_add(instance, cube_id, metric_id, METRIC_TYPE_HLL);
    if (ret != FS_OK) {
        return ret;
    }
    if (instance->cube[cube_id]->sampling_mode == SAMPLING_MODE_TOPK) {
        return FS_ERR_INVALID_PARAM;
    }
    
    struct tag_hash_key tag_key;
    tag_hash_key_init_with_fieldstat_tag(&tag_key, tags, n_tag, false);

    struct exdata_new_args args;
    args.tags = tags;
    args.n_tags = n_tag;

    struct exdata *cell_data = find_or_add_exdata_comprehensive(instance->cube[cube_id]->cells.comprehensive, &tag_key, &args);
    struct metric *metric = construct_or_find_metric_to_exdata(instance, cell_data, metric_id);

    metric_hll_add(metric, key, key_len);
    return 0;
}

int fieldstat_hist_record(struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag *tags, size_t n_tag, long long value)
{
    int ret = check_before_add(instance, cube_id, metric_id, METRIC_TYPE_HISTOGRAM);
    if (ret != FS_OK) {
        return ret;
    }
    if (instance->cube[cube_id]->sampling_mode == SAMPLING_MODE_TOPK) {
        return FS_ERR_INVALID_PARAM;
    }

    struct tag_hash_key tag_key;
    tag_hash_key_init_with_fieldstat_tag(&tag_key, tags, n_tag, false);

    struct exdata_new_args args;
    args.tags = tags;
    args.n_tags = n_tag;

    struct exdata *cell_data = find_or_add_exdata_comprehensive(instance->cube[cube_id]->cells.comprehensive, &tag_key, &args);

    // // metric_histogram_record may fail, unlike the other add functions.
    struct metric *metric = find_metric_in_exdata(cell_data, metric_id);
    if (metric != NULL) {
        ret = metric_histogram_record(metric, value);
        if (ret < 0) {
            return FS_ERR_INVALID_PARAM;
        }
    } else {
        metric = metric_fork(instance->metric_masters[metric_id]);
        ret = metric_histogram_record(metric, value);
        if (ret < 0) {
            metric_free(metric);
            return FS_ERR_INVALID_PARAM;
        }
        add_metric_to_exdata(cell_data, metric, metric_id);
    }

    return FS_OK;
}

/* -------------------------------------------------------------------------- */
/*                                    merge                                   */
/* -------------------------------------------------------------------------- */

struct fs_cube *fieldstat_cube_copy(const struct fs_cube *cube)
{
    struct fs_cube *cube_dup = fieldstat_cube_info_init(cube->shared_tags, cube->n_shared_tags, cube->sampling_mode, cube->max_n_cell);
    cube_dup->primary_metric_id = cube->primary_metric_id;

    switch (cube->sampling_mode)
    {
    case SAMPLING_MODE_TOPK:
        cube_dup->cells.topk = heavy_keeper_copy(cube->cells.topk);
        break;
    case SAMPLING_MODE_COMPREHENSIVE:
        cube_dup->cells.comprehensive = tag_map_copy(cube->cells.comprehensive);
        break;
    default:
        assert(0);
        break;
    }

    return cube_dup;
}

void fieldstat_cube_merge(struct fs_cube *dest, const struct fs_cube *src)
{
    assert(dest->sampling_mode == src->sampling_mode);

    switch (dest->sampling_mode)
    {
    case SAMPLING_MODE_TOPK:
        heavy_keeper_merge(dest->cells.topk, src->cells.topk);
        break;
    case SAMPLING_MODE_COMPREHENSIVE:
        tag_map_merge(dest->cells.comprehensive, src->cells.comprehensive);
        break;
    default:
        assert(0);
        break;
    }
}

int fieldstat_merge(struct fieldstat *instance, struct fieldstat *src)
{
    if (instance == NULL || src == NULL) {
        return FS_ERR_NULL_HANDLER;
    }

    int metric_len_src = src->n_metric_master;
    int metric_len_dst = instance->n_metric_master;
    int metric_len_common = metric_len_src > metric_len_dst ? metric_len_dst : metric_len_src;
    for (int i = 0; i < metric_len_common; i++) {
        const struct metric *metric_src = src->metric_masters[i];
        const struct metric *metric_dst = instance->metric_masters[i];
        if (metric_get_type(metric_src) != metric_get_type(metric_dst) || 
                strcmp(metric_get_name(metric_src), metric_get_name(metric_dst)) != 0
            ) {
            assert(0);
            return FS_ERR_INVALID_PARAM;
        }
    }
    for (int i = metric_len_common; i < metric_len_src; i++) {
        const struct metric *metric_src = src->metric_masters[i];
        int id_tmp = append_metric_to_instance(instance, metric_fork(metric_src));
        name_id_map_add(instance->metric_name_id_map, metric_get_name(metric_src), id_tmp);
    }

    size_t n_cube_src = src->valid_cube_arr_length;
    const struct cube_manager *tag_cube_id_map = instance->shared_tag_cube_manager;
    int ret = 0;
    for (int i = 0; i < n_cube_src; i++) {
        const struct fs_cube *cube_src = src->cube[i];
        if (cube_src == NULL) {
            continue;
        }

        const struct tag_hash_key *shared_tag_key_src = cube_src->key_tag;
        int cube_id_tmp = cube_manager_find(tag_cube_id_map, shared_tag_key_src);
        if (cube_id_tmp == -1) {
            struct fs_cube *copied_cube = fieldstat_cube_copy(cube_src);
            fieldstat_append_cube_to_instance(instance, copied_cube);
        } else {
            struct fs_cube *cube_dst = instance->cube[cube_id_tmp];
            if (cube_dst->sampling_mode != cube_src->sampling_mode) {
                ret = FS_ERR_INVALID_PARAM;
                continue;
            }
            if (cube_dst->sampling_mode == SAMPLING_MODE_TOPK && cube_dst->primary_metric_id != cube_src->primary_metric_id) {
                ret = FS_ERR_INVALID_PARAM;
                continue;
            }
            fieldstat_cube_merge(cube_dst, cube_src);
        }
    }

    return ret;
}

// only copy the cube configurations, leave the cells empty
struct fs_cube *fieldstat_cube_fork(const struct fs_cube *cube) {
    struct fs_cube *ret = fieldstat_cube_new(cube->shared_tags, cube->n_shared_tags, cube->sampling_mode, cube->max_n_cell);
    ret->primary_metric_id = cube->primary_metric_id;

    return ret;
}

struct fieldstat *fieldstat_fork(const struct fieldstat *instance)
{
    if (instance == NULL) {
        return NULL;
    }
    struct fieldstat *new_instance = calloc(1, sizeof(struct fieldstat));
    new_instance->shared_tag_cube_manager = cube_manager_new();

    new_instance->valid_cube_arr_length = instance->valid_cube_arr_length;
    new_instance->max_n_cube = instance->max_n_cube;
    new_instance->cube = calloc(new_instance->max_n_cube, sizeof(struct fs_cube *));
    for (size_t i = 0; i < new_instance->valid_cube_arr_length; i++) {
        const struct fs_cube *cube = instance->cube[i];
        if (cube == NULL) {
            continue;
        }
        new_instance->cube[i] = fieldstat_cube_fork(cube);
        cube_manager_add(new_instance->shared_tag_cube_manager, cube->key_tag, i);
    }
    new_instance->cube_version = calloc(new_instance->max_n_cube, sizeof(unsigned long));
    memcpy(new_instance->cube_version, instance->cube_version, sizeof(unsigned long) * new_instance->max_n_cube);

    new_instance->metric_masters = calloc(instance->max_n_metric_master, sizeof(struct metric *));
    new_instance->max_n_metric_master = instance->max_n_metric_master;
    new_instance->n_metric_master = instance->n_metric_master;
    for (size_t i = 0; i < instance->n_metric_master; i++) {
        new_instance->metric_masters[i] = metric_fork(instance->metric_masters[i]);
    }
    new_instance->metric_name_id_map = name_id_map_copy(instance->metric_name_id_map);

    return new_instance;
}

void calibrate_metrics_in_instance(const struct fieldstat *master, struct fieldstat *replica)
{
    if (replica->max_n_metric_master < master->max_n_metric_master) {
        replica->metric_masters = (struct metric **)realloc(replica->metric_masters, sizeof(struct metric *) * master->max_n_metric_master);
        memset(replica->metric_masters + replica->max_n_metric_master, 0, sizeof(struct metric *) * (master->max_n_metric_master - replica->max_n_metric_master));
        replica->max_n_metric_master = master->max_n_metric_master;
    }

    size_t longer_arr_len = master->n_metric_master > replica->n_metric_master ? master->n_metric_master : replica->n_metric_master;
    for (size_t i = 0; i < longer_arr_len; i++) {
        const struct metric *metric_master = i >= master->n_metric_master ? NULL : master->metric_masters[i];
        struct metric *metric_target = i >= replica->n_metric_master ? NULL : replica->metric_masters[i];
        if (metric_master == NULL && metric_target == NULL) {
            continue;
        }
        if (metric_master == NULL && metric_target != NULL) {
            metric_free(metric_target);
            replica->metric_masters[i] = NULL;
            continue;
        }
        if (metric_master != NULL && metric_target == NULL) {
            const struct metric *metric_dup = metric_fork(metric_master);
            add_metric_to_instance(replica, metric_dup, i);
            continue;
        }
        if (metric_get_type(metric_master) != metric_get_type(metric_target) || 
                strcmp(metric_get_name(metric_master), metric_get_name(metric_target)) != 0
            ) {
            metric_free(metric_target);
            const struct metric *metric_dup = metric_fork(metric_master);
            add_metric_to_instance(replica, metric_dup, i);
            continue;
        }

        // metric same, no need to do anything
    }

    replica->n_metric_master = master->n_metric_master;
}

int fieldstat_calibrate(const struct fieldstat *master, struct fieldstat *replica)
{
    if (master == NULL || replica == NULL) {
        return FS_ERR_NULL_HANDLER;
    }

    if (replica->max_n_cube < master->max_n_cube) {
        replica->cube = (struct fs_cube **)realloc(replica->cube, sizeof(struct fs_cube *) * master->max_n_cube);
        memset(replica->cube + replica->max_n_cube, 0, sizeof(struct fs_cube *) * (master->max_n_cube - replica->max_n_cube));
        replica->cube_version = (unsigned long *)realloc(replica->cube_version, sizeof(unsigned long) * master->max_n_cube);
        memset(replica->cube_version + replica->max_n_cube, 0, sizeof(unsigned long) * (master->max_n_cube - replica->max_n_cube));
        replica->max_n_cube = master->max_n_cube;
    }

    size_t len_master = master->valid_cube_arr_length;
    size_t len_replica = replica->valid_cube_arr_length;
    size_t longer_arr_len = len_master > len_replica ? len_master : len_replica;
    for (size_t i = 0; i < longer_arr_len; i++) {
        const struct fs_cube *cube_master = i >= len_master ? NULL : master->cube[i];
        const struct fs_cube *cube_target = i >= len_replica ? NULL : replica->cube[i];

        if (cube_master == NULL && cube_target == NULL) {
            continue;
        }
        if (cube_master == NULL && cube_target != NULL) {
            fieldstat_cube_free_contents(replica, i);
            continue;
        }
        if (cube_master != NULL && cube_target == NULL) {
            struct fs_cube *cube_dup = fieldstat_cube_fork(cube_master);
            add_cube_to_position(replica, cube_dup, i);
            continue;
        }

        if (master->cube_version[i] == replica->cube_version[i] && tag_hash_key_cmp(cube_master->key_tag, cube_target->key_tag) == 0) {
            continue;
        }

        fieldstat_cube_free_contents(replica, i);
        struct fs_cube *cube_dup = fieldstat_cube_fork(cube_master);
        add_cube_to_position(replica, cube_dup, i);
    }

    memcpy(replica->cube_version, master->cube_version, sizeof(unsigned long) * master->max_n_cube);
    replica->valid_cube_arr_length = master->valid_cube_arr_length;

    cube_manager_calibrate(replica->shared_tag_cube_manager, master->shared_tag_cube_manager);
    calibrate_metrics_in_instance(master, replica);

    return FS_OK;
}

/* -------------------------------------------------------------------------- */
/*                                    query                                   */
/* -------------------------------------------------------------------------- */
void fieldstat_get_cubes(const struct fieldstat *instance, int **cube_ids, int *n_cube)
{
    if (instance == NULL || instance->valid_cube_arr_length == 0) {
        *cube_ids = NULL;
        *n_cube = 0;
        return;
    }
    int all_available_cube_count = 0;

    int *tmp_ids = (int *)malloc(sizeof(int) * instance->valid_cube_arr_length);
    for (int i = 0; i < instance->valid_cube_arr_length; i++) {
        const struct fs_cube *tmp_cube = instance->cube[i];
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
    if (instance == NULL || instance->n_metric_master == 0) {
        *metric_id_out = NULL;
        *n_metric = 0;
        return;
    }

    int *tmp_ids = (int *)malloc(sizeof(int) * instance->n_metric_master);
    *metric_id_out = tmp_ids;
    *n_metric = instance->n_metric_master;
    for (int i = 0; i < instance->n_metric_master; i++) {
        tmp_ids[i] = i;
    }

    return;
}

struct fieldstat_tag_list *fieldstat_get_shared_tags(const struct fieldstat *instance, int cube_id)
{
    if (instance == NULL || cube_id >= instance->valid_cube_arr_length || cube_id < 0) {
        return NULL;
    }
    const struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        return NULL;
    }
   
    struct fieldstat_tag_list *tag_list = (struct fieldstat_tag_list *)malloc(sizeof(struct fieldstat_tag_list));

    if (cube->n_shared_tags == 0) {
        tag_list->tag = NULL;
        tag_list->n_tag = 0;
        return tag_list;
    }

    tag_list->tag = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * cube->n_shared_tags);
    tag_list->n_tag = cube->n_shared_tags;
    tag_array_copy(tag_list->tag, cube->shared_tags, cube->n_shared_tags);

    return tag_list;
}

const struct exdata *get_exdata_by_tag_list(const struct fs_cube *cube, const struct fieldstat_tag_list *tags)
{
    struct tag_hash_key tag_key;
    tag_hash_key_init_with_fieldstat_tag(&tag_key, tags->tag, tags->n_tag, false);

    switch (cube->sampling_mode)
    {
    case SAMPLING_MODE_TOPK:
        return heavy_keeper_get0_exdata(cube->cells.topk, &tag_key);
    case SAMPLING_MODE_COMPREHENSIVE:
        return tag_map_get0_exdata(cube->cells.comprehensive, &tag_key);
    default:
        assert(0);
        return NULL;
    }
}

const struct metric *get_metric_by_tag_list(const struct fieldstat *instance, int cube_id, const struct fieldstat_tag_list *tags, int metric_id,int *ret)
{
    if (cube_id < 0 || cube_id >= instance->valid_cube_arr_length) {
        *ret = FS_ERR_INVALID_CUBE_ID;
        return NULL;
    }
    const struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        *ret = FS_ERR_INVALID_CUBE_ID;
        return NULL;
    }

    const struct exdata *data = get_exdata_by_tag_list(cube, tags);
    
    if (data == NULL) {
        *ret = FS_ERR_INVALID_TAG;
        return NULL;
    }

    if (metric_id < 0 || metric_id >= data->valid_metric_arr_len) {
        *ret = FS_ERR_INVALID_METRIC_ID;
        return NULL;
    }
    *ret = FS_OK;

    return data->metrics[metric_id];
}

int fieldstat_counter_get(const struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag_list *tags, long long *value)
{
    int ret;
    const struct metric *metric = get_metric_by_tag_list(instance, cube_id, tags, metric_id, &ret);
    if (ret!=FS_OK) {
        return ret;
    }
    if (metric == NULL || metric_get_type(metric) != METRIC_TYPE_COUNTER) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    *value = metric_counter_get(metric);

    return FS_OK;
}

int fieldstat_hll_get(const struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag_list *tags, double *value)
{
    int ret;
    const struct metric *metric = get_metric_by_tag_list(instance, cube_id, tags, metric_id, &ret);
    if (ret!=FS_OK) {
        return ret;
    }
    if (metric == NULL || metric_get_type(metric) != METRIC_TYPE_HLL) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    *value = metric_hll_get(metric);

    return FS_OK;
}

long long fieldstat_hist_value_at_percentile(const struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag_list *tags, double percentile)
{
    int ret;
    const struct metric *metric = get_metric_by_tag_list(instance, cube_id, tags, metric_id, &ret);
    if (ret!=FS_OK) {
        return ret;
    }
    if (metric == NULL || metric_get_type(metric) != METRIC_TYPE_HISTOGRAM) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    return metric_histogram_value_at_percentile(metric, percentile);
}

long long fieldstat_hist_count_le_value(const struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag_list *tags, long long value)
{
    int ret;
    const struct metric *metric = get_metric_by_tag_list(instance, cube_id, tags, metric_id, &ret);
    if (ret!=FS_OK) {
        return ret;
    }
    if (metric == NULL || metric_get_type(metric) != METRIC_TYPE_HISTOGRAM) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    return metric_histogram_count_le_value(metric, value);
}

// metric_get_plain_blob
void fieldstat_get_serialized_blob(const struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag_list *tags, char **blob, size_t *blob_size)
{
    int ret;
    const struct metric *metric = get_metric_by_tag_list(instance, cube_id, tags, metric_id, &ret);
    if (metric == NULL) {
        *blob = NULL;
        *blob_size = 0;
        return;
    }

    metric_get_plain_blob(metric, blob, blob_size);
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
    if (metric_id < 0 || metric_id >= instance->n_metric_master) {
        return NULL;
    }
    const struct metric *metric = instance->metric_masters[metric_id];
    if (metric == NULL) {
        return NULL;
    }

    return metric_get_name(metric);
}

enum metric_type fieldstat_get_metric_type(const struct fieldstat *instance, int metric_id)
{
    if (instance == NULL || metric_id < 0 || metric_id >= instance->n_metric_master) {
        return (enum metric_type)(-1);
    }
    const struct metric *metric = instance->metric_masters[metric_id];
    if (metric == NULL) {
        return (enum metric_type)(-1);
    }

    return metric_get_type(metric);
}

void fieldstat_get_cells_used_by_cube(const struct fieldstat *instance, int cube_id, struct fieldstat_tag_list **tag_list, size_t *n_cell)
{
    if (instance == NULL || cube_id < 0 || cube_id >= instance->valid_cube_arr_length) {
        return;
    }
    const struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        return;
    }
    
    struct exdata **cell_datas = NULL;
    size_t n_cell_tmp = 0;
    switch (cube->sampling_mode) {
        case SAMPLING_MODE_COMPREHENSIVE:
            tag_map_list(cube->cells.comprehensive, (void ***)&cell_datas, &n_cell_tmp);
            break;
        case SAMPLING_MODE_TOPK:
            heavy_keeper_list(cube->cells.topk, (void ***)&cell_datas, &n_cell_tmp);
            break;
        default:
            assert(0);
    }

    if (n_cell_tmp == 0) {
        *tag_list = NULL;
        *n_cell = 0;
        return;
    }

    struct fieldstat_tag_list *tag_list_ret = (struct fieldstat_tag_list *)malloc(sizeof(struct fieldstat_tag_list) * n_cell_tmp);
    *tag_list = tag_list_ret;
    *n_cell = n_cell_tmp;

    for (int i = 0; i < n_cell_tmp; i++) {
        struct exdata *cell_data = cell_datas[i];
        struct fieldstat_tag_list *tag_list_tmp = &tag_list_ret[i];
        tag_list_tmp->n_tag = cell_data->tags.n_tag;
        if (tag_list_tmp->n_tag == 0) {
            tag_list_tmp->tag = NULL;
            continue;
        }
        tag_list_tmp->tag = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * tag_list_tmp->n_tag);
        tag_array_copy(tag_list_tmp->tag, cell_data->tags.tag, tag_list_tmp->n_tag);
    }

    free(cell_datas);
}

int fieldstat_get_used_sampling(const struct fieldstat *instance, int cube_id)
{
    if (instance == NULL) {
        return FS_ERR_NULL_HANDLER;
    }
    if (cube_id < 0 || cube_id >= instance->valid_cube_arr_length) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    const struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }

    switch (cube->sampling_mode) {
        case SAMPLING_MODE_COMPREHENSIVE:
            return tag_map_get_count(cube->cells.comprehensive);
        case SAMPLING_MODE_TOPK:
            return heavy_keeper_get_count(cube->cells.topk);
        default:
            return FS_ERR_INVALID_PARAM;
    }
}

int fieldstat_find_cube(const struct fieldstat *instance, const struct fieldstat_tag *shared_tags, size_t n_shared_tags)
{
    if (instance == NULL) {
        return FS_ERR_NULL_HANDLER;
    }
    const struct cube_manager *tag_cube_id_map = instance->shared_tag_cube_manager;
    struct tag_hash_key shared_tag_key;
    tag_hash_key_init_with_fieldstat_tag(&shared_tag_key, shared_tags, n_shared_tags, false);
    int cube_id = cube_manager_find(tag_cube_id_map, &shared_tag_key);
    if (cube_id == -1) {
        return FS_ERR_INVALID_KEY;
    }

    return cube_id;
}

int fieldstat_get_cube_mode(const struct fieldstat *instance, int cube_id, enum sampling_mode *mode, int *primary_metric_id)
{
    if (instance == NULL) {
        return FS_ERR_NULL_HANDLER;
    }
    if (cube_id < 0 || cube_id >= instance->valid_cube_arr_length) {
        return FS_ERR_INVALID_CUBE_ID;
    }
    const struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        return FS_ERR_INVALID_CUBE_ID;
    }

    *mode = cube->sampling_mode;
    if (cube->sampling_mode == SAMPLING_MODE_TOPK) {
        *primary_metric_id = cube->primary_metric_id;
    } else {
        *primary_metric_id = -1;
    }

    return FS_OK;
}

void fieldstat_get_metric_in_cell(const struct fieldstat *instance, int cube_id, const struct fieldstat_tag_list *tags, int **metric_id_out, size_t *n_metric_out)
{
    const struct exdata *cell_data = get_exdata_by_tag_list(instance->cube[cube_id], tags);
    if (cell_data == NULL) {
        *metric_id_out = NULL;
        *n_metric_out = 0;
        return;
    }

    *metric_id_out = (int *)malloc(sizeof(int) * cell_data->valid_metric_arr_len);
    int n_metric = 0;
    for (int i = 0; i < cell_data->valid_metric_arr_len; i++) {
        if (cell_data->metrics[i] != NULL) {
            (*metric_id_out)[n_metric] = i;
            n_metric++;
        }
    }
    *n_metric_out = n_metric;
}