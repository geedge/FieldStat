
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "cjson/cJSON.h"
#include "uthash.h"
#include "mpack/mpack.h"
#include "fieldstat.h"
#include "metrics/metric.h"
#include "tags/cell_manager.h"

struct fs_cube {
    enum sampling_mode sampling_mode;
    struct cell_manager *cell_manager;
    size_t max_n_cell;
    int next_cell_id;

    // the key of cube is the combination of shared tags
    struct fieldstat_tag *shared_tags;
    size_t n_shared_tags;

    struct metric **metrics;
    size_t n_metric;
    size_t max_n_metric;

    struct metric_name_id_map *metric_name_id_map; // used only in merge to store metric id in history
};

struct metric_name_id_map {
    char *name;
    int id;
    UT_hash_handle hh;
};

struct fieldstat {
    struct fs_cube **cube;
    unsigned long *cube_version; // increase from 0 every time the cube is deleted
    unsigned long cell_version; // increase from 0 every time fieldstat_reset is called
    size_t valid_cube_arr_length;
    size_t max_n_cube;

    struct cube_manager *shared_tag_cube_manager; // used only in merge to store shared tags in history
};


struct fieldstat *fieldstat_new()
{
    struct fieldstat *instance = calloc(1, sizeof(struct fieldstat));

    instance->max_n_cube = 100;
    instance->cube = calloc(instance->max_n_cube, sizeof(struct fs_cube *));
    instance->cube_version = calloc(instance->max_n_cube, sizeof(unsigned long));

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
    if (instance->shared_tag_cube_manager != NULL){
        cube_manager_free(instance->shared_tag_cube_manager);
    }

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
        for (size_t j = 0; j < cube->n_metric; j++) {
            metric_reset(cube->metrics[j]);
        }

        cell_manager_reset(cube->cell_manager);
    }
    instance->cell_version++;
}

unsigned long fieldstat_get_cell_version(const struct fieldstat *instance)
{
    if (instance == NULL) {
        printf("ERR: instance is NULL\n");
        return 0;
    }
    return instance->cell_version;
}

int fieldstat_unregister_cube(struct fieldstat *instance, int cube_id)
{
    if (instance == NULL) {
        printf("ERR: instance is NULL\n");
        return -1;
    }
    if (cube_id < 0 || cube_id >= instance->valid_cube_arr_length) {
        //printf("ERR: cube_id is out of range, cube_id: %d\n", cube_id);
        return -1;
    }
    if (instance->shared_tag_cube_manager != NULL) {
        printf("WARNING: This fieldstat instance is used as a merge destination. Please make sure merging has finished.\n");
        cube_manager_free(instance->shared_tag_cube_manager);
        instance->shared_tag_cube_manager = NULL;
    }
    if (instance->cube[cube_id] == NULL) {
        printf("ERR: cube_id is not registered\n");
        return -1;
    }
    fieldstat_cube_free(instance, cube_id);
    instance->cube[cube_id] = NULL;
    instance->cube_version[cube_id]++;
    return 0;
}

long long fieldstat_get_cube_version(const struct fieldstat *instance, int cube_id)
{
    if (instance == NULL) {
        return -1;
    }
    if (cube_id < 0) {
        return -2;
    }
    if (cube_id >= instance->valid_cube_arr_length) {
        return -3;
    }
    if (instance->cube[cube_id] == NULL && instance->cube_version[cube_id] == 0) {
        return -3;
    }

    return instance->cube_version[cube_id];
}

/* -------------------------------------------------------------------------- */
/*                                    cube                                    */
/* -------------------------------------------------------------------------- */

int name_id_map_get_id_by_name(struct metric_name_id_map *map, const char *field_name)
{
    struct metric_name_id_map *entry = NULL;
    HASH_FIND_STR(map, field_name, entry);
    if (entry == NULL) {
        return -1;
    }
    return entry->id;
}

void name_id_map_add(struct metric_name_id_map **map, const char *name, int id)
{
    struct metric_name_id_map *entry = malloc(sizeof(struct metric_name_id_map));
    entry->id = id;
    entry->name = strdup(name);
    HASH_ADD_KEYPTR(hh, *map, entry->name, strlen(entry->name), entry);
}

void name_id_map_free(struct metric_name_id_map *map)
{
    struct metric_name_id_map *entry, *tmp;
    HASH_ITER(hh, map, entry, tmp) {
        HASH_DEL(map, entry);
        free(entry->name);
        free(entry);
    }
}

void name_id_map_init(struct metric_name_id_map **map, const struct fs_cube *cube)
{
    *map = NULL;
    const char *name;
    for (int i = 0; i < cube->n_metric; i++) {
        name = metric_get_name(cube->metrics[i]);
        name_id_map_add(map, name, i);
    }
}

void fieldstat_clear_one_tag(const struct fieldstat_tag *tag)
{
    free((char *)tag->key);
    if (tag->type == TAG_CSTRING && tag->value_str != NULL) {
        free((char *)tag->value_str);
    }
}

int fieldstat_append_cube_to_instance(struct fieldstat *instance, struct fs_cube *cube)
{
    for (int i = 0; i < instance->valid_cube_arr_length; i++) {
        if (instance->cube[i] == NULL) {
            instance->cube[i] = cube;
            return i;
        }
    }
    if (instance->valid_cube_arr_length >= instance->max_n_cube) {
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
    instance->cube[instance->valid_cube_arr_length] = cube;
    instance->valid_cube_arr_length++;
    return instance->valid_cube_arr_length - 1;
}

struct fs_cube *fieldstat_cube_info_init(const struct fieldstat_tag *shared_tags, size_t n_tag, enum sampling_mode mode, size_t max_n_cell)
{
    struct fs_cube *cube = calloc(1, sizeof(struct fs_cube));
    cube->sampling_mode = mode;
    cube->max_n_cell = max_n_cell;
    cube->shared_tags = malloc(sizeof(struct fieldstat_tag) * n_tag);
    for (int i = 0; i < n_tag; i++) {
        struct fieldstat_tag *dest_tag = &cube->shared_tags[i];
        dest_tag->key = strdup(shared_tags[i].key);
        dest_tag->type = shared_tags[i].type;
        switch (dest_tag->type)
        {
        case TAG_INTEGER:
            dest_tag->value_longlong = shared_tags[i].value_longlong;
            break;
        case TAG_CSTRING:
            dest_tag->value_str = strdup(shared_tags[i].value_str);
            break;
        case TAG_DOUBLE:
            dest_tag->value_double = shared_tags[i].value_double;
            break;
        
        default:
            break;
        }
    }

    cube->n_shared_tags = n_tag;

    cube->max_n_metric = 16;
    cube->metrics = malloc(sizeof(struct metric *) * cube->max_n_metric);

    return cube;
}

struct fs_cube *fieldstat_cube_new(const struct fieldstat_tag *shared_tags, size_t n_tag, enum sampling_mode mode, size_t max_n_cell)
{
    struct fs_cube *cube = fieldstat_cube_info_init(shared_tags, n_tag, mode, max_n_cell);

    cube->cell_manager = cell_manager_new(mode, max_n_cell);

    return cube;
}

int fieldstat_register_cube(struct fieldstat *instance, const struct fieldstat_tag *shared_tags, size_t n_tag, enum sampling_mode mode, size_t max_n_cell)
{
    if (instance == NULL) {
        printf("ERR: fieldstat instance is NULL\n");
        return -1;
    }
    if (n_tag == 0 || shared_tags == NULL) {
        printf("ERR: shared tags must not be empty\n");
        return -1;
    }
    struct fs_cube *cube = fieldstat_cube_new(shared_tags, n_tag, mode, max_n_cell);

    int cube_id = fieldstat_append_cube_to_instance(instance, cube);
    return cube_id;
}

int fieldstat_cube_add(struct fieldstat *instance, int cube_id, const struct fieldstat_tag *tags, size_t n_tag, long long increment)
{
    if (instance == NULL) {
        printf("ERR: fieldstat instance is NULL\n");
        return -1;
    }
    if (cube_id < 0 || cube_id >= instance->valid_cube_arr_length) {
        //printf("ERR: cube_id is invalid, %d\n", cube_id);
        return -1;
    }
    
    struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        printf("ERR: fieldstat_cube_add cube is not registered yet\n");
        return -1;
    }
    if (cube->sampling_mode == SAMPLING_MODE_TOPK && increment <= 0) {
        printf("ERR: increment cannot less than zero\n");
        return -1;
    }

    int ret = -1;
    struct tag_hash_key *tag_key = tag_hash_key_construct_with_fieldstat_tag(tags, n_tag);
    if (cube->sampling_mode == SAMPLING_MODE_COMPREHENSIVE) {
        ret = cell_manager_add_cell(cube->cell_manager, tag_key);
    } else {
        int popped_cell_id = -1;
        int existing_cell_id = -1;
        ret = cell_manager_add_cell_topk(cube->cell_manager, tag_key, increment, &popped_cell_id, &existing_cell_id);
        if (popped_cell_id != -1) {
            for (size_t i = 0; i < cube->n_metric; i++) {
                metric_delete_cell(cube->metrics[i], popped_cell_id);
            }
        }
    }

    tag_hash_key_free(tag_key);

    return ret;
}

void fieldstat_cube_free(struct fieldstat *instance, int cube_id)
{
    struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        return;
    }
    cell_manager_free(cube->cell_manager);
    for (size_t i = 0; i < cube->n_shared_tags; i++) {
        fieldstat_clear_one_tag(&cube->shared_tags[i]);
    }
    free(cube->shared_tags);

    for (size_t i = 0; i < cube->n_metric; i++) {
        metric_free(cube->metrics[i]);
    }
    free(cube->metrics);

    if (cube->metric_name_id_map != NULL) {
        name_id_map_free(cube->metric_name_id_map);
    }

    free(cube);
    instance->cube[cube_id] = NULL;
}


/* -------------------------------------------------------------------------- */
/*                               metric register                              */
/* -------------------------------------------------------------------------- */

static int append_metric_to_cube(struct fs_cube *cube, struct metric *metric)
{
    if (cube->n_metric >= cube->max_n_metric) {
        cube->max_n_metric *= 2;
        cube->metrics = realloc(cube->metrics, sizeof(struct metric *) * cube->max_n_metric);
    }
    cube->metrics[cube->n_metric] = metric;
    cube->n_metric++;
    return cube->n_metric - 1;
}

int fieldstat_register_counter(struct fieldstat *instance, int cube_id, const char *field_name, enum counter_mode mode)
{
    if (instance == NULL) {
        printf("ERR: fieldstat instance is NULL\n");
        return -1;
    }
    if (cube_id < 0 || cube_id >= instance->valid_cube_arr_length) {
        printf("ERR: fieldstat_register_hist cube_id is invalid, %d\n", cube_id);
        return -1;
    }
    struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        printf("ERR: fieldstat_register_counter cube is not registered yet\n");
        return -1;
    }
    struct metric *metric = metric_counter_new(field_name, mode);

    return append_metric_to_cube(cube, metric);
}

int fieldstat_register_hll(struct fieldstat *instance, int cube_id, const char *field_name, unsigned char precision)
{
    if (instance == NULL) {
        printf("ERR: fieldstat instance is NULL\n");
        return -1;
    }
    if (cube_id < 0 || cube_id >= instance->valid_cube_arr_length) {
        printf("ERR: fieldstat_register_hist cube_id is invalid, %d\n", cube_id);
        return -1;
    }
    struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        printf("ERR: fieldstat_register_hll cube is not registered yet\n");
        return -1;
    }
    if (precision < 4 || precision > 18) {
        printf("ERR: precision must be between 4 and 18\n");
        return -1;
    }
    struct metric *metric = metric_hll_new(field_name, precision);

    return append_metric_to_cube(cube, metric);
}

int fieldstat_register_hist(struct fieldstat *instance, int cube_id, const char *field_name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures)
{
    if (instance == NULL) {
        printf("ERR: fieldstat instance is NULL\n");
        return -1;
    }
    if (cube_id < 0 || cube_id >= instance->valid_cube_arr_length) {
        printf("ERR: fieldstat_register_hist cube_id is invalid, %d\n", cube_id);
        return -1;
    }
    struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        printf("ERR: fieldstat_register_histogram cube is not registered yet\n");
        return -1;
    }
    if (lowest_trackable_value < 1) {
        printf("ERR: lowest_trackable_value not correct.\n");
        return -1;
    }
    if (significant_figures < 1 || significant_figures > 5) {
        printf("ERR: significant_figures must be between 1 and 5\n");
        return -1;
    }
    if (lowest_trackable_value * 2 > highest_trackable_value)
	{
        printf("ERR: highest_trackable_value must be at least twice as large as lowest_trackable_value\n");
		return -1;
	}

    struct metric *metric = metric_histogram_new(field_name, lowest_trackable_value, highest_trackable_value, significant_figures);

    return append_metric_to_cube(cube, metric);
}

/* -------------------------------------------------------------------------- */
/*                               metric operation                              */
/* -------------------------------------------------------------------------- */
static struct metric *fieldstat_find_metric(const struct fieldstat *instance, int cube_id, int metric_id)
{
    if (cube_id < 0 || cube_id >= instance->valid_cube_arr_length) {
        printf("ERR: fieldstat_find_metric cube_id is not correct, input cube id: %d\n", cube_id);
        return NULL;
    }
    struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        printf("ERR: fieldstat_find_metric cube is not registered yet, input cube id: %d\n", cube_id);
        return NULL;
    }
    return cube->metrics[metric_id];
}

#define FIELDSTAT_GENERAL_CHECK(instance, cube_id, metric_id, cell_id, metric_type) \
do { \
    if ((instance) == NULL) {   \
        printf("ERR: [%s] fieldstat instance is NULL\n", __FUNCTION__); \
        return -1;  \
    }   \
    if ((cube_id) < 0 || (cube_id) >= (instance)->valid_cube_arr_length) {    \
        printf("ERR: [%s] cube_id is not correct, input cube id: %d\n", __FUNCTION__, (cube_id)); \
        return -1;  \
    }   \
    const struct fs_cube *cube = (instance)->cube[(cube_id)];   \
    if (cube == NULL) { \
        printf("ERR: [%s] cube is not registered yet, input cube id: %d\n", __FUNCTION__, (cube_id)); \
        return -1;  \
    }   \
    if ((metric_id) < 0 || (metric_id) >= cube->n_metric) { \
        printf("ERR: [%s] metric_id is not correct, input metric id: %d\n", __FUNCTION__, (metric_id));   \
        return -2;  \
    }   \
    const struct metric *metric = cube->metrics[(metric_id)]; \
    if (metric == NULL || metric_get_type(metric) != (metric_type)) { \
        printf("ERR: [%s] metric is not registered yet, input metric id: %d\n", __FUNCTION__, (metric_id));   \
        return -2;  \
    }   \
    if ((cell_id) < 0 || cell_manager_get_tag_by_cell_id(cube->cell_manager, cell_id) == NULL) { \
        printf("ERR: [%s] cell_id is not correct, input cell id: %d\n", __FUNCTION__, (cell_id)); \
        return -3;  \
    }   \
} while (0)

int fieldstat_counter_incrby(struct fieldstat *instance, int cube_id, int metric_id, int cell_id, long long increment)
{
    FIELDSTAT_GENERAL_CHECK(instance, cube_id, metric_id, cell_id, METRIC_TYPE_COUNTER);
    struct metric *metric = instance->cube[cube_id]->metrics[metric_id];

    metric_counter_incrby(metric, cell_id, increment);
    return 0;
}

int fieldstat_counter_set(struct fieldstat *instance, int cube_id, int metric_id, int cell_id, long long value)
{
    FIELDSTAT_GENERAL_CHECK(instance, cube_id, metric_id, cell_id, METRIC_TYPE_COUNTER);
    struct metric *metric = instance->cube[cube_id]->metrics[metric_id];

    int ret = metric_counter_set(metric, cell_id, value);
    if (ret < 0) {
        printf("ERR: fieldstat_counter_set failed");
        return -4;
    }
    return 0;
}

int fieldstat_hll_add(struct fieldstat *instance, int cube_id, int metric_id, int cell_id, const char *key, size_t key_len)
{
    FIELDSTAT_GENERAL_CHECK(instance, cube_id, metric_id, cell_id, METRIC_TYPE_HLL);
    struct metric *metric = instance->cube[cube_id]->metrics[metric_id];

    metric_hll_add(metric, cell_id, key, key_len);
    return 0;
}

int fieldstat_hist_record(struct fieldstat *instance, int cube_id, int metric_id, int cell_id, long long value)
{
    FIELDSTAT_GENERAL_CHECK(instance, cube_id, metric_id, cell_id, METRIC_TYPE_HISTOGRAM);
    struct metric *metric = instance->cube[cube_id]->metrics[metric_id];

    int ret = metric_histogram_record(metric, cell_id, value);
    if (ret < 0) {
        printf("ERR: fieldstat_histogram_record failed");
        return -4;
    }
    return 0;
}


// /* -------------------------------------------------------------------------- */
// /*                                serialization                               */
// /* -------------------------------------------------------------------------- */
/*
{
    "ver": <EXPORT_VER>

    "cube"= {
        [{
        "shared_tags":[<tag_list_serialized_blob>]
        "sampling_mode":0 or 1 
        "max_n_cell":<max number of cells>
        "metrics":[
            [<blob>]
        ]
        "cm":<cell manager serialized blob>
        }]
    }
}
*/

int fieldstat_serialize(const struct fieldstat *instance, char **blob_out, size_t *blob_size_out)
{
    if (instance == NULL) {
        printf("ERR: fieldstat_serialize input is NULL\n");
        return -1;
    }
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob_out, blob_size_out);
    mpack_build_map(&writer);

    mpack_write_cstr(&writer, "ver");
    mpack_write_i32(&writer, EXPORT_VER);

    mpack_write_cstr(&writer, "cube");
    mpack_build_array(&writer);
    for (int i = 0; i < instance->valid_cube_arr_length; i++) {
        struct fs_cube *cube = instance->cube[i];
        if (cube == NULL) {
            continue;
        }
        mpack_build_map(&writer);
        mpack_write_cstr(&writer, "shared_tags");
        char *shared_tag_blob = NULL;
        size_t shared_tag_blob_size = 0;
        fieldtag_list_serialize(cube->shared_tags, cube->n_shared_tags, &shared_tag_blob, &shared_tag_blob_size);
        mpack_write_bin(&writer, shared_tag_blob, shared_tag_blob_size);
        free(shared_tag_blob);
        mpack_write_cstr(&writer, "sampling_mode");
        mpack_write_i8(&writer, cube->sampling_mode);
        mpack_write_cstr(&writer, "max_n_cell");
        mpack_write_i64(&writer, cube->max_n_cell);

        mpack_write_cstr(&writer, "metrics");
        mpack_start_array(&writer, cube->n_metric);
        for (int j = 0; j < cube->n_metric; j++) {
            char *metric_blob = NULL;
            size_t metric_blob_size = 0;
            metric_serialize(cube->metrics[j], &metric_blob, &metric_blob_size);
            mpack_write_bin(&writer, metric_blob, metric_blob_size);
            free(metric_blob);
        }
        mpack_finish_array(&writer);

        mpack_write_cstr(&writer, "cm");
        char *cm_blob = NULL;
        size_t cm_blob_size = 0;
        cell_manager_serialize(cube->cell_manager, &cm_blob, &cm_blob_size);
        mpack_write_bin(&writer, cm_blob, cm_blob_size);
        free(cm_blob);

        mpack_complete_map(&writer);
    }
    mpack_complete_array(&writer);

    mpack_complete_map(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        printf("ERR: fieldstat_serialize: mpack_writer_destroy failed\n");
        return -1;
    }

    return 0;
}

struct fieldstat *fieldstat_deserialize(const char *blob, size_t blob_size)
{
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, blob_size);
    mpack_tree_parse(&tree);
    mpack_node_t root = mpack_tree_root(&tree);
    int ver = mpack_node_i32(mpack_node_map_cstr(root, "ver"));
    if (ver != EXPORT_VER) {
        printf("ERR: fieldstat_deserialize: invalid version\n");
        mpack_tree_destroy(&tree);
        return NULL;
    }

    struct fieldstat *instance = fieldstat_new();
    mpack_node_t cube_node = mpack_node_map_cstr(root, "cube");
    int n_cube = mpack_node_array_length(cube_node);
    for (int i = 0; i < n_cube; i++) {
        mpack_node_t cube = mpack_node_array_at(cube_node, i);

        mpack_node_t shared_tags_node = mpack_node_map_cstr(cube, "shared_tags");
        struct fieldstat_tag *shared_tags = NULL;
        size_t n_shared_tags = 0;
        fieldtag_list_deserialize(mpack_node_bin_data(shared_tags_node), mpack_node_bin_size(shared_tags_node), &shared_tags, &n_shared_tags);
        int sampling_mode = mpack_node_i8(mpack_node_map_cstr(cube, "sampling_mode"));
        int max_n_cell = mpack_node_i64(mpack_node_map_cstr(cube, "max_n_cell"));
        struct fs_cube *fs_cube = fieldstat_cube_info_init(shared_tags, n_shared_tags, sampling_mode, max_n_cell);
        fieldtag_list_free(shared_tags, n_shared_tags);

        mpack_node_t metrics_node = mpack_node_map_cstr(cube, "metrics");
        int n_metrics = mpack_node_array_length(metrics_node);
        for (int j = 0; j < n_metrics; j++) {
            mpack_node_t metric_node = mpack_node_array_at(metrics_node, j);
            struct metric *metric = metric_deserialize(mpack_node_bin_data(metric_node), mpack_node_bin_size(metric_node));
            append_metric_to_cube(fs_cube, metric);
        }

        mpack_node_t cm_node = mpack_node_map_cstr(cube, "cm");
        struct cell_manager *cm = cell_manager_deserialize(mpack_node_bin_data(cm_node), mpack_node_bin_size(cm_node));
        fs_cube->cell_manager = cm;

        fieldstat_append_cube_to_instance(instance, fs_cube);
    }

    mpack_tree_destroy(&tree);
    return instance;
}

/* -------------------------------------------------------------------------- */
/*                                    merge                                   */
/* -------------------------------------------------------------------------- */

struct fs_cube *fieldstat_cube_dup(const struct fs_cube *cube)
{
    struct fs_cube *cube_dup = fieldstat_cube_info_init(cube->shared_tags, cube->n_shared_tags, cube->sampling_mode, cube->max_n_cell);
    const struct cell_manager *cm_src = cube->cell_manager;

    struct cell_manager *cm_dup = cell_manager_copy(cm_src);
    cube_dup->cell_manager = cm_dup;

    for (int i = 0; i < cube->n_metric; i ++) {
        struct metric *metric_dup = metric_copy(cube->metrics[i]);
        append_metric_to_cube(cube_dup, metric_dup);
    }
    return cube_dup;
}

void fieldstat_cube_merge_comprehensive(struct fs_cube *dest, const struct fs_cube *src, const int *metric_id_src_dest_map)
{
    const struct cell_manager *cell_manager_src = src->cell_manager;
    struct cell_manager *cell_manager_dest = dest->cell_manager;

    int arr_len_src = 0;
    const struct tag_hash_key **tag_arr_src = cell_manager_dump(cell_manager_src, &arr_len_src);
    for (int cell_id_src = 0; cell_id_src < arr_len_src; cell_id_src++) {
        const struct tag_hash_key *tag_src = tag_arr_src[cell_id_src];
        if (tag_src == NULL) {
            // the tag_src is continuous, so there may not be NULL in the middle
            printf("ERR: tag_src is NULL, id: %d\n", cell_id_src);
            continue;
        }

        int cell_id_final = cell_manager_add_cell(cell_manager_dest, tag_src);
        if (cell_id_final == -1) { // dest is full
            break;
        }
        for (int metric_id_src = 0; metric_id_src < src->n_metric; metric_id_src++) {
            int metric_id_dest = metric_id_src_dest_map[metric_id_src];
            int tmp_ret = metric_merge_or_copy_cell(dest->metrics[metric_id_dest], src->metrics[metric_id_src], cell_id_final, cell_id_src);
            if (tmp_ret == -1) {
                printf("ERR: metric_merge_or_copy_cell failed\n");
            }
        }
    }
}

void fieldstat_cube_merge_topk(struct fs_cube *dest, const struct fs_cube *src, const int *metric_id_src_dest_map)
{
    const struct cell_manager *cell_manager_src = src->cell_manager;
    struct cell_manager *cell_manager_dest = dest->cell_manager;
    int *cell_id_added = NULL;
    int *cell_id_old = NULL;
    int n_cell_id_added = 0;
    int *cell_id_popped = NULL;
    int n_cell_id_popped = 0;

    cell_manager_merge_topk(cell_manager_dest, cell_manager_src, &cell_id_popped, &n_cell_id_popped, &cell_id_old, &cell_id_added, &n_cell_id_added);

    for (int i = 0; i < n_cell_id_added; i++) {
        int tmp_id_dest = cell_id_added[i];
        int tmp_id_src = cell_id_old[i];

        for (int j = 0; j < src->n_metric; j++) {
            int metric_id_dest = metric_id_src_dest_map[j];
            metric_merge_or_copy_cell(dest->metrics[metric_id_dest], src->metrics[j], tmp_id_dest, tmp_id_src);
        }
    }
    // chances are that: a cell exists in both dest and src, but it is not in final. In this case, these cells are both in cell_id_added and cell_id_popped.
    // Since all cells are counter, which is easy to merge, we just delete these cells after merging them.
    for (int i = 0; i < n_cell_id_popped;i++) {
        int id = cell_id_popped[i];

        for (int j = 0; j < dest->n_metric; j++) {
            metric_delete_cell(dest->metrics[j], id);
        }
    }
    if (cell_id_popped != NULL) {
        free(cell_id_popped);
    }
    if (cell_id_added != NULL) {
        free(cell_id_added);
    }
    if (cell_id_old != NULL) {
        free(cell_id_old);
    }
}

void fieldstat_cube_merge(struct fs_cube *dest, const struct fs_cube *src)
{
    struct metric_name_id_map *name_id_map_dest = dest->metric_name_id_map;
    if (name_id_map_dest == NULL) {
        name_id_map_init(&name_id_map_dest, dest);
        dest->metric_name_id_map = name_id_map_dest;
    }

    int metric_id_src_dest_map[src->n_metric];
    for (int metric_id_src = 0; metric_id_src < src->n_metric; metric_id_src++) {
        const char *name_src = metric_get_name(src->metrics[metric_id_src]);
        int metric_id_dest = name_id_map_get_id_by_name(name_id_map_dest, name_src);
        if (metric_id_dest == -1) { // dest does not have this metric
            struct metric *metric_dup = metric_copy_structure(src->metrics[metric_id_src]);
            metric_id_dest = append_metric_to_cube(dest, metric_dup);
            name_id_map_add(&dest->metric_name_id_map, name_src, metric_id_dest);
        }
        metric_id_src_dest_map[metric_id_src] = metric_id_dest;
    }

    if (dest->sampling_mode == SAMPLING_MODE_COMPREHENSIVE) {
        return fieldstat_cube_merge_comprehensive(dest, src, metric_id_src_dest_map);
    }
    fieldstat_cube_merge_topk(dest, src, metric_id_src_dest_map);
}

int fieldstat_merge(struct fieldstat *instance, struct fieldstat *src)
{
    if (instance == NULL || src == NULL) {
        printf("ERR: fieldstat_merge: instance or src is NULL\n");
        return -1;
    }
    size_t n_cube_dest = instance->valid_cube_arr_length;
    size_t n_cube_src = src->valid_cube_arr_length;
    // tag_cube_id_map use cube shared tag as key, find cubes. Is is not a cell manager.
    struct cube_manager *tag_cube_id_map = instance->shared_tag_cube_manager;
    if (tag_cube_id_map == NULL) {
        tag_cube_id_map = cube_manager_new();
        instance->shared_tag_cube_manager = tag_cube_id_map;
        for (int i = 0; i < n_cube_dest; i++) {
            const struct fs_cube *cube_dest = instance->cube[i];
            if (cube_dest == NULL) {
                continue;
            }
            struct tag_hash_key *shared_tag_key = tag_hash_key_construct_with_fieldstat_tag(cube_dest->shared_tags, instance->cube[i]->n_shared_tags);
            cube_manager_add(tag_cube_id_map, shared_tag_key, i);
            tag_hash_key_free(shared_tag_key);
        }
    }

    int ret = 0;
    for (int i = 0; i < n_cube_src; i++) {
        const struct fs_cube *cube_src = src->cube[i];
        if (cube_src == NULL) {
            continue;
        }

        struct tag_hash_key *shared_tag_key_src = tag_hash_key_construct_with_fieldstat_tag(cube_src->shared_tags, cube_src->n_shared_tags);
        int cube_id_tmp = cube_manager_find(tag_cube_id_map, shared_tag_key_src);
        if (cube_id_tmp == -1) {
            struct fs_cube *copied_cube = fieldstat_cube_dup(cube_src);
            int cube_id = fieldstat_append_cube_to_instance(instance, copied_cube);
            cube_manager_add(tag_cube_id_map, shared_tag_key_src, cube_id);
        } else {
            if (instance->cube[cube_id_tmp]->sampling_mode != cube_src->sampling_mode) {
                printf("ERR: cube sampling mode not match\n");
                ret = -1;
                tag_hash_key_free(shared_tag_key_src);
                continue;
            }
            fieldstat_cube_merge(instance->cube[cube_id_tmp], cube_src);
        }
        tag_hash_key_free(shared_tag_key_src);
    }

    return ret;
}

/* -------------------------------------------------------------------------- */
/*                                    query                                   */
/* -------------------------------------------------------------------------- */
void fieldstat_get_cubes(const struct fieldstat *instance, int **cube_ids, int *n_cube)
{
    int all_available_cube_count = 0;
    if (instance->valid_cube_arr_length == 0) {
        *cube_ids = NULL;
        *n_cube = 0;
        return;
    }

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

int fieldstat_get_max_metric_id(const struct fieldstat *instance, int cube_id)
{
    if (cube_id >= instance->valid_cube_arr_length || cube_id < 0) {
        printf("ERR: fieldstat_get_max_metric_id cube_id is invalid\n");
        return -1;
    }
    const struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        printf("ERR: fieldstat_get_max_metric_id cube is not registered yet\n");
        return -1;
    }
    return cube->n_metric - 1;
}

void fieldstat_get_cells(const struct fieldstat *instance, int cube_id, int metric_id,
    int **cell_ids, struct fieldstat_tag_list **tag_list, size_t *n_cell)
{
    if (cube_id >= instance->valid_cube_arr_length || cube_id < 0) {
        printf("ERR: fieldstat_get_cells cube_id is invalid\n");
        return;
    }
    const struct metric *metric = fieldstat_find_metric(instance, cube_id, metric_id);
    if (metric == NULL) {
        printf("ERR: metric or cube is not registered yet\n");
        return;
    }

    size_t n_cell_ret = 0;
    metric_get_cell_ids(metric, cell_ids, &n_cell_ret);
    *n_cell = n_cell_ret;
    if (n_cell_ret == 0) {
        *tag_list = NULL;
        return;
    }

    struct fieldstat_tag_list *tag_list_ret = (struct fieldstat_tag_list *)malloc(sizeof(struct fieldstat_tag_list) * n_cell_ret);
    *tag_list = tag_list_ret;
    for (int i = 0; i < n_cell_ret; i++) {
        const struct tag_hash_key *tag_key = cell_manager_get_tag_by_cell_id(instance->cube[cube_id]->cell_manager, (*cell_ids)[i]);
        if (tag_key == NULL) {
            printf("ERR: fieldstat_get_cells cell_manager_get_tag_by_cell_id failed, cube id: %d, metric id: %d,  cell id: %d\n", cube_id, metric_id, (*cell_ids)[i]);
            continue;
        }
        tag_hash_key_convert_to_fieldstat_tag(tag_key, &(tag_list_ret[i].tag), &(tag_list_ret[i].n_tag));
    }
}

struct fieldstat_tag_list *fieldstat_get_shared_tags(const struct fieldstat *instance, int cube_id)
{
    if (cube_id >= instance->valid_cube_arr_length || cube_id < 0) {
        printf("ERR: fieldstat_get_shared_tags cube_id is invalid\n");
        return NULL;
    }
    struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        printf("ERR: fieldstat_get_shared_tags cube is not registered yet\n");
        return NULL;
    }

    struct fieldstat_tag_list *tag_list = (struct fieldstat_tag_list *)malloc(sizeof(struct fieldstat_tag_list));
    tag_list->tag = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * cube->n_shared_tags);
    for (int i = 0; i < cube->n_shared_tags; i++) {
        struct fieldstat_tag *tag_dest = &(tag_list->tag[i]);
        struct fieldstat_tag *tag_src = &(cube->shared_tags[i]);
        tag_dest->key = strdup(tag_src->key);
        tag_dest->type = tag_src->type;
        switch (tag_src->type) {
            case TAG_INTEGER:
                tag_dest->value_longlong = tag_src->value_longlong;
                break;
            case TAG_CSTRING:
                tag_dest->value_str = strdup(tag_src->value_str);
                break;
            case TAG_DOUBLE:
                tag_dest->value_double = tag_src->value_double;
                break;
            default:
                printf("ERR: tag type not supported\n");
                break;
        }
    }
    tag_list->n_tag = cube->n_shared_tags;

    return tag_list;
}

long long fieldstat_counter_get(const struct fieldstat *instance, int cube_id, int metric_id, int cell_id)
{
    const struct metric *metric = fieldstat_find_metric(instance, cube_id, metric_id);
    if (metric == NULL || metric_get_type(metric) != METRIC_TYPE_COUNTER) {
        printf("ERR: fieldstat_counter_get metrics is not registered yet\n");
        return -1;
    }

    return metric_counter_get(metric, cell_id);
}

double fieldstat_hll_get(const struct fieldstat *instance, int cube_id, int metric_id, int cell_id)
{
    const struct metric *metric = fieldstat_find_metric(instance, cube_id, metric_id);
    if (metric == NULL || metric_get_type(metric) != METRIC_TYPE_HLL) {
        printf("ERR: fieldstat_hll_get metrics is not registered yet\n");
        return -1;
    }

    return metric_hll_get(metric, cell_id);
}

long long fieldstat_hist_value_at_percentile(const struct fieldstat *instance, int cube_id, int metric_id, int cell_id, double percentile)
{
    const struct metric *metric = fieldstat_find_metric(instance, cube_id, metric_id);
    if (metric == NULL || metric_get_type(metric) != METRIC_TYPE_HISTOGRAM) {
        printf("ERR: fieldstat_histogram_value_at_percentile metrics is not registered yet\n");
        return -1;
    }

    return metric_histogram_value_at_percentile(metric, cell_id, percentile);
}

long long fieldstat_hist_count_le_value(const struct fieldstat *instance, int cube_id, int metric_id, int cell_id, long long value)
{
    const struct metric *metric = fieldstat_find_metric(instance, cube_id, metric_id);
    if (metric == NULL || metric_get_type(metric) != METRIC_TYPE_HISTOGRAM) {
        printf("ERR: fieldstat_histogram_value_le_value metrics is not registered yet\n");
        return -1;
    }

    return metric_histogram_count_le_value(metric, cell_id, value);
}

void fieldstat_get_serialized_blob(const struct fieldstat *instance, int cube_id, int metric_id, int cell_id, char **blob, size_t *blob_size)
{
    const struct metric *metric = fieldstat_find_metric(instance, cube_id, metric_id);
    if (metric == NULL) {
        printf("ERR: fieldstat_get_serialized_blob metrics is not registered yet\n");
        return;
    }
    enum metric_type type = metric_get_type(metric);
    if (!(type == METRIC_TYPE_HLL || type == METRIC_TYPE_HISTOGRAM)) {
        printf("ERR: Only metric of hll and histogram support get serialized blob\n");
        return;
    }

    metric_get_plain_blob(metric, cell_id, blob, blob_size);
}

void fieldstat_tag_list_arr_free(struct fieldstat_tag_list *tag_list, size_t n_cell)
{
    if (tag_list == NULL) {
        return;
    }
    for (int i = 0; i < n_cell; i++) {
        for (int j = 0; j < tag_list[i].n_tag; j++) {
            fieldstat_clear_one_tag(&(tag_list[i].tag[j]));
        }
        free(tag_list[i].tag);
    }
    free(tag_list);
}

const char *fieldstat_get_metric_name(const struct fieldstat *instance, int cube_id, int metric_id)
{
    const struct metric *metric = fieldstat_find_metric(instance, cube_id, metric_id);
    if (metric == NULL) {
        printf("ERR: metrics is not registered yet\n");
        return NULL;
    }

    return metric_get_name(metric);
}

enum metric_type fieldstat_get_metric_type(const struct fieldstat *instance, int cube_id, int metric_id)
{
    const struct metric *metric = fieldstat_find_metric(instance, cube_id, metric_id);
    if (metric == NULL) {
        printf("ERR: metrics is not registered yet\n");
        return (enum metric_type)-1;
    }

    return metric_get_type(metric);
}

struct fieldstat *fieldstat_dup(const struct fieldstat *instance)
{
    if (instance == NULL) {
        return NULL;
    }
    struct fieldstat *new_instance = calloc(1, sizeof(struct fieldstat));

    new_instance->valid_cube_arr_length = instance->valid_cube_arr_length;
    new_instance->max_n_cube = instance->max_n_cube;
    new_instance->cube = calloc(new_instance->max_n_cube, sizeof(struct fs_cube *));
    for (size_t i = 0; i < new_instance->valid_cube_arr_length; i++) {
        struct fs_cube *cube = instance->cube[i];
        if (cube == NULL) {
            continue;
        }
        new_instance->cube[i] = fieldstat_cube_new(cube->shared_tags, cube->n_shared_tags, cube->sampling_mode, cube->max_n_cell);
        // copy registered metrics
        for (size_t j = 0; j < cube->n_metric; j++) {
            const struct metric *metric = cube->metrics[j];
            struct metric *new_metric = metric_copy_structure(metric);
            append_metric_to_cube(new_instance->cube[i], new_metric);
        }
    }

    new_instance->cube_version = calloc(new_instance->max_n_cube, sizeof(unsigned long));
    return new_instance;
}

void fieldstat_cube_read_cell(const struct fieldstat *instance, int cube_id, int **cell_ids, struct fieldstat_tag_list **tag_list, size_t *n_cell)
{
    if (cube_id < 0 || cube_id >= instance->valid_cube_arr_length) {
        printf("ERR: fieldstat_cube_read_cell cube id is invalid, cube id: %d\n", cube_id);
        return;
    }
    const struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        printf("ERR: fieldstat_cube_read_cell cube is not registered yet, cube id: %d\n", cube_id);
        return;
    }
    
    int arr_len = 0;
    const struct tag_hash_key **tags_discontinuous = cell_manager_dump(cube->cell_manager, &arr_len);
    if (arr_len == 0) {
        *cell_ids = NULL;
        *tag_list = NULL;
        *n_cell = 0;
        return;
    }

    struct fieldstat_tag_list *tag_list_ret = (struct fieldstat_tag_list *)malloc(sizeof(struct fieldstat_tag_list) * arr_len);
    int *cell_ids_ret = (int *)malloc(sizeof(int) * arr_len);
    int n_cell_ret = 0;

    for (int i = 0; i < arr_len; i++) {
        const struct tag_hash_key *tag_key = tags_discontinuous[i];
        if (tag_key == NULL) {
            continue;
        }
        tag_hash_key_convert_to_fieldstat_tag(tag_key, &(tag_list_ret[n_cell_ret].tag), &(tag_list_ret[n_cell_ret].n_tag));
        cell_ids_ret[n_cell_ret] = i;
        n_cell_ret++;
    }
    if (n_cell_ret == 0) {
        free(cell_ids_ret);
        free(tag_list_ret);
        *cell_ids = NULL;
        *tag_list = NULL;
        *n_cell = 0;
        return;
    }

    *cell_ids = cell_ids_ret;
    *tag_list = tag_list_ret;
    *n_cell = n_cell_ret;
}

int fieldstat_get_max_cell_num(const struct fieldstat *instance, int cube_id)
{
    const struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        printf("ERR: fieldstat_get_max_cell_num cube is not registered yet, cube id: %d\n", cube_id);
        return -1;
    }

    return cube->max_n_cell;
}