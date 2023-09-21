
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "cjson/cJSON.h"
#include "uthash.h"
#include "serializer.h"
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
    struct tag_hash_key *key_tag;

    struct metric **metrics;
    size_t n_metric;
    size_t max_n_metric;

    struct metric_name_id_map *metric_name_id_map;
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

    struct cube_manager *shared_tag_cube_manager;
};


struct fieldstat *fieldstat_new()
{
    struct fieldstat *instance = calloc(1, sizeof(struct fieldstat));

    instance->max_n_cube = 100;
    instance->cube = calloc(instance->max_n_cube, sizeof(struct fs_cube *));
    instance->cube_version = calloc(instance->max_n_cube, sizeof(unsigned long));

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
        return 0;
    }
    return instance->cell_version;
}

int fieldstat_unregister_cube(struct fieldstat *instance, int cube_id)
{
    if (instance == NULL) {
        return -1;
    }
    if (cube_id < 0 || cube_id >= instance->valid_cube_arr_length) {
        return -1;
    }
    if (instance->cube[cube_id] == NULL) {
        return -1;
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
    int cube_id = instance->valid_cube_arr_length;
    instance->cube[cube_id] = cube;
    instance->valid_cube_arr_length++;
    
    cube_manager_add(instance->shared_tag_cube_manager, cube->key_tag, cube_id);

    return cube_id;
}

struct fs_cube *fieldstat_cube_info_init(const struct fieldstat_tag *shared_tags, size_t n_tag, enum sampling_mode mode, size_t max_n_cell)
{
    struct fs_cube *cube = calloc(1, sizeof(struct fs_cube));
    cube->sampling_mode = mode;
    cube->max_n_cell = max_n_cell;
    if (n_tag == 0) {
        cube->shared_tags = NULL;
    } else {
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
    }

    cube->n_shared_tags = n_tag;

    struct tag_hash_key *shared_tag_key = tag_hash_key_construct_with_fieldstat_tag(shared_tags, n_tag);
    cube->key_tag = shared_tag_key;

    cube->max_n_metric = 128;
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
        return -1;
    }

    if (n_tag == 0 || shared_tags == NULL) {
        shared_tags = NULL;
        n_tag = 0;
    }
    if (mode == SAMPLING_MODE_TOPK && max_n_cell == 0) {
        return -1;
    }
    if (max_n_cell == 0) {
        max_n_cell = INT32_MAX;
    }
    struct tag_hash_key *shared_tag_key = tag_hash_key_construct_with_fieldstat_tag(shared_tags, n_tag);
    int ret = cube_manager_find(instance->shared_tag_cube_manager, shared_tag_key);
    tag_hash_key_free(shared_tag_key);
    if (ret != -1) {
        return -2;
    }

    struct fs_cube *cube = fieldstat_cube_new(shared_tags, n_tag, mode, max_n_cell);

    int cube_id = fieldstat_append_cube_to_instance(instance, cube);

    return cube_id;
}

int fieldstat_cube_add(struct fieldstat *instance, int cube_id, const struct fieldstat_tag *tags, size_t n_tag, long long increment)
{
    if (instance == NULL) {
        return -2;
    }
    if (cube_id < 0 || cube_id >= instance->valid_cube_arr_length) {
        return -2;
    }
    
    struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        return -2;
    }
    if (cube->sampling_mode == SAMPLING_MODE_TOPK && increment <= 0) {
        return -3;
    }

    if (n_tag == 0 || tags == NULL) {
        tags = NULL;
        n_tag = 0;
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

int fieldstat_cube_remove(struct fieldstat *instance, int cube_id, const struct fieldstat_tag *tags, size_t n_tag)
{
    if (instance == NULL) {
        return -2;
    }
    if (cube_id < 0 || cube_id >= instance->valid_cube_arr_length) {
        return -2;
    }
    struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        return -2;
    }
    if (cube->sampling_mode == SAMPLING_MODE_TOPK) {
        return -3;
    }
    if (n_tag == 0 || tags == NULL) {
        tags = NULL;
        n_tag = 0;
    }

    struct tag_hash_key *tag_key = tag_hash_key_construct_with_fieldstat_tag(tags, n_tag);
    int id = cell_manager_delete_cell(cube->cell_manager, tag_key);
    if (id == -1) {
        tag_hash_key_free(tag_key);
        return -1;
    }
    for (size_t i = 0; i < cube->n_metric; i++) {
        metric_delete_cell(cube->metrics[i], id);
    }
    tag_hash_key_free(tag_key);

    instance->cell_version++;
    return id;
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
    tag_hash_key_free(cube->key_tag);

    for (size_t i = 0; i < cube->n_metric; i++) {
        metric_free(cube->metrics[i]);
    }
    free(cube->metrics);

    name_id_map_free(cube->metric_name_id_map);

    free(cube);
    instance->cube[cube_id] = NULL;
}

struct fs_cube *fieldstat_cube_fork(const struct fs_cube *cube) {
    return fieldstat_cube_new(cube->shared_tags, cube->n_shared_tags, cube->sampling_mode, cube->max_n_cell);
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
    name_id_map_add(&cube->metric_name_id_map, metric_get_name(metric), cube->n_metric);
    cube->metrics[cube->n_metric] = metric;
    cube->n_metric++;

    return cube->n_metric - 1;
}

int fieldstat_register_counter(struct fieldstat *instance, int cube_id, const char *field_name, enum counter_mode mode)
{
    if (instance == NULL) {
        return -1;
    }
    if (cube_id < 0 || cube_id >= instance->valid_cube_arr_length) {
        return -1;
    }
    struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        return -1;
    }

    if (name_id_map_get_id_by_name(cube->metric_name_id_map, field_name) != -1) {
        return -3;
    }

    struct metric *metric = metric_counter_new(field_name, mode);

    return append_metric_to_cube(cube, metric);
}

int fieldstat_register_hll(struct fieldstat *instance, int cube_id, const char *field_name, unsigned char precision)
{
    if (instance == NULL) {
        return -1;
    }
    if (cube_id < 0 || cube_id >= instance->valid_cube_arr_length) {
        return -1;
    }
    struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        return -1;
    }
    if (precision < 4 || precision > 18) {
        return -2;
    }

    if (name_id_map_get_id_by_name(cube->metric_name_id_map, field_name) != -1) {
        return -3;
    }

    struct metric *metric = metric_hll_new(field_name, precision);

    return append_metric_to_cube(cube, metric);
}

int fieldstat_register_hist(struct fieldstat *instance, int cube_id, const char *field_name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures)
{
    if (instance == NULL) {
        return -1;
    }
    if (cube_id < 0 || cube_id >= instance->valid_cube_arr_length) {
        return -1;
    }
    struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        return -1;
    }

    // refer to hdr_histogram.h for the rules of parameters. Just copy them here
    if (lowest_trackable_value < 1) {
        return -2;
    }
    if (significant_figures < 1 || significant_figures > 5) {
        return -2;
    }
    if (lowest_trackable_value * 2 > highest_trackable_value)
	{
		return -2;
	}

    if (name_id_map_get_id_by_name(cube->metric_name_id_map, field_name) != -1) {
        return -3;
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
        return NULL;
    }
    struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        return NULL;
    }
    return cube->metrics[metric_id];
}

#define FIELDSTAT_GENERAL_CHECK(instance, cube_id, metric_id, cell_id, metric_type) \
do { \
    if ((instance) == NULL) {   \
        return -1;  \
    }   \
    if ((cube_id) < 0 || (cube_id) >= (instance)->valid_cube_arr_length) {    \
        return -1;  \
    }   \
    const struct fs_cube *cube = (instance)->cube[(cube_id)];   \
    if (cube == NULL) { \
        return -1;  \
    }   \
    if ((metric_id) < 0 || (metric_id) >= cube->n_metric) { \
        return -2;  \
    }   \
    const struct metric *metric = cube->metrics[(metric_id)]; \
    if (metric == NULL || metric_get_type(metric) != (metric_type)) { \
        return -2;  \
    }   \
    if ((cell_id) < 0 || cell_manager_get_tag_by_cell_id(cube->cell_manager, cell_id) == NULL) { \
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

/*
    "ver": <EXPORT_VER>
    "cube":[<blob>]*

    *the blob is :
    bin: shared_tags| uint32: sampling_mode | uint32: max_n_cell | unsigned char[]: <metric blob> [<pad> 0-4] |  uint32: <length of cell_manager_blob> | <cell manager blob> [<pad> 0-4] ]

*/

void fieldstat_cube_serialize(const struct fs_cube *cube, char **blob_out, size_t *blob_size_out)
{
    struct fs_reader *reader = fs_reader_new();

    char *shared_tag_blob = NULL;
    size_t shared_tag_blob_size = 0;
    fieldtag_list_serialize(cube->shared_tags, cube->n_shared_tags, &shared_tag_blob, &shared_tag_blob_size);
    fs_reader_read_bin(reader, shared_tag_blob, shared_tag_blob_size);
    free(shared_tag_blob);

    fs_reader_read_uint(reader, cube->sampling_mode);
    fs_reader_read_uint(reader, cube->max_n_cell);

    fs_reader_start_bin_array(reader, cube->n_metric);
    for (int j = 0; j < cube->n_metric; j++) {
        char *metric_blob = NULL;
        size_t metric_blob_size = 0;
        metric_serialize(cube->metrics[j], &metric_blob, &metric_blob_size);
        fs_reader_read_bin(reader, metric_blob, metric_blob_size);
        free(metric_blob);
    }

    char *cm_blob = NULL;
    size_t cm_blob_size = 0;
    cell_manager_serialize(cube->cell_manager, &cm_blob, &cm_blob_size);
    fs_reader_read_bin(reader, cm_blob, cm_blob_size);
    free(cm_blob);

    fs_reader_finalize(reader, blob_out, blob_size_out);
}

struct fs_cube *fieldstat_cube_deserialize(const char *blob, size_t blob_size) {
    struct fs_writer *writer = fs_writer_new(blob, blob_size);

    struct fieldstat_tag *shared_tags;
    size_t n_shared_tags;
    char *shared_tags_blob = NULL;
    size_t shared_tags_blob_size = 0;
    fs_writer_write_bin(writer, &shared_tags_blob, &shared_tags_blob_size);
    fieldtag_list_deserialize(shared_tags_blob, shared_tags_blob_size, &shared_tags, &n_shared_tags);
    free(shared_tags_blob);

    enum sampling_mode sampling_mode;
    fs_writer_write_uint(writer, &sampling_mode);
    size_t max_n_cell;
    fs_writer_write_uint(writer, (uint32_t *)&max_n_cell);
    struct fs_cube *fs_cube = fieldstat_cube_info_init(shared_tags, n_shared_tags, sampling_mode, max_n_cell);
    fieldtag_list_free(shared_tags, n_shared_tags);

    size_t n_metrics;
    char **metric_blob_arr = NULL;
    size_t *metric_blob_size_arr = NULL;
    fs_writer_write_bin_array(writer, &metric_blob_arr, &n_metrics, &metric_blob_size_arr);
    for (size_t j = 0; j < n_metrics; j++) {
        struct metric *metric = metric_deserialize(metric_blob_arr[j], metric_blob_size_arr[j]);
        append_metric_to_cube(fs_cube, metric);
    }
    for (size_t j = 0; j < n_metrics; j++) {
        free(metric_blob_arr[j]);
    }
    free(metric_blob_arr);
    free(metric_blob_size_arr);

    char *cm_blob = NULL;
    size_t cm_blob_size = 0;
    fs_writer_write_bin(writer, &cm_blob, &cm_blob_size);
    struct cell_manager *cm = cell_manager_deserialize(cm_blob, cm_blob_size);
    free(cm_blob);
    fs_cube->cell_manager = cm;

    fs_writer_free(writer);
    return fs_cube;
}

int fieldstat_serialize(const struct fieldstat *instance, char **blob_out, size_t *blob_size_out)
{
    if (instance == NULL) {
        return -1;
    }
    struct fs_reader *reader = fs_reader_new();

    fs_reader_read_uint(reader, EXPORT_VER);

    int n_cube = 0;
    for (int i = 0; i < instance->valid_cube_arr_length; i++) {
        if (instance->cube[i] != NULL) {
            n_cube++;
        }
    }
    fs_reader_start_bin_array(reader, n_cube);
    for (int i = 0; i < instance->valid_cube_arr_length; i++) {
        const struct fs_cube *cube = instance->cube[i];
        if (cube == NULL) {
            continue;
        }
        char *cube_blob = NULL;
        size_t cube_blob_size = 0;
        fieldstat_cube_serialize(cube, &cube_blob, &cube_blob_size);
        fs_reader_read_bin(reader, cube_blob, cube_blob_size);
        free(cube_blob);
    }

    fs_reader_finalize(reader, blob_out, blob_size_out);

    return 0;
}

struct fieldstat *fieldstat_deserialize(const char *blob, size_t blob_size)
{
    struct fs_writer *writer = fs_writer_new(blob, blob_size);
    uint32_t ver;
    fs_writer_write_uint(writer, &ver);
    if (ver != EXPORT_VER) {
        assert(0);
        fs_writer_free(writer);
        return NULL;
    }

    struct fieldstat *instance = fieldstat_new();
    char **cube_blob_arr = NULL;
    size_t *cube_blob_size_arr = NULL;
    size_t n_cube;
    fs_writer_write_bin_array(writer, &cube_blob_arr, &n_cube, &cube_blob_size_arr);
    for (int i = 0; i < n_cube; i++) {
        struct fs_cube *fs_cube = fieldstat_cube_deserialize(cube_blob_arr[i], cube_blob_size_arr[i]);
        fieldstat_append_cube_to_instance(instance, fs_cube);
    }
    for (int i = 0; i < n_cube; i++) {
        free(cube_blob_arr[i]);
    }
    free(cube_blob_arr);
    free(cube_blob_size_arr);

    fs_writer_free(writer);
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
            continue;
        }

        int cell_id_final = cell_manager_add_cell(cell_manager_dest, tag_src);
        if (cell_id_final == -1) { // dest is full
            break;
        }
        for (int metric_id_src = 0; metric_id_src < src->n_metric; metric_id_src++) {
            int metric_id_dest = metric_id_src_dest_map[metric_id_src];
            (void)metric_merge_or_copy_cell(dest->metrics[metric_id_dest], src->metrics[metric_id_src], cell_id_final, cell_id_src);
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

    int metric_id_src_dest_map[src->n_metric];
    for (int metric_id_src = 0; metric_id_src < src->n_metric; metric_id_src++) {
        const char *name_src = metric_get_name(src->metrics[metric_id_src]);
        int metric_id_dest = name_id_map_get_id_by_name(name_id_map_dest, name_src);
        if (metric_id_dest == -1) { // dest does not have this metric
            struct metric *metric_dup = metric_fork(src->metrics[metric_id_src]);
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
        return -1;
    }
    size_t n_cube_src = src->valid_cube_arr_length;
    // tag_cube_id_map use cube shared tag as key, find cubes. Is is not a cell manager.
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
            struct fs_cube *copied_cube = fieldstat_cube_dup(cube_src);
            fieldstat_append_cube_to_instance(instance, copied_cube);
        } else {
            if (instance->cube[cube_id_tmp]->sampling_mode != cube_src->sampling_mode) {
                ret = -1;
                continue;
            }
            fieldstat_cube_merge(instance->cube[cube_id_tmp], cube_src);
        }
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
        return -1;
    }
    const struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        return -1;
    }
    return cube->n_metric - 1;
}

void fieldstat_get_cells(const struct fieldstat *instance, int cube_id, int metric_id,
    int **cell_ids, struct fieldstat_tag_list **tag_list, size_t *n_cell)
{
    if (cube_id >= instance->valid_cube_arr_length || cube_id < 0) {
        return;
    }
    const struct metric *metric = fieldstat_find_metric(instance, cube_id, metric_id);
    if (metric == NULL) {
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
            continue;
        }
        tag_hash_key_convert_to_fieldstat_tag(tag_key, &(tag_list_ret[i].tag), &(tag_list_ret[i].n_tag));
    }
}

struct fieldstat_tag_list *fieldstat_get_shared_tags(const struct fieldstat *instance, int cube_id)
{
    if (cube_id >= instance->valid_cube_arr_length || cube_id < 0) {
        return NULL;
    }
    struct fs_cube *cube = instance->cube[cube_id];
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
                break;
        }
    }
    tag_list->n_tag = cube->n_shared_tags;

    return tag_list;
}

int fieldstat_counter_get(const struct fieldstat *instance, int cube_id, int metric_id, int cell_id, long long *value)
{
    const struct metric *metric = fieldstat_find_metric(instance, cube_id, metric_id);
    if (metric == NULL || metric_get_type(metric) != METRIC_TYPE_COUNTER) {
        return -1;
    }

    return metric_counter_get(metric, cell_id, value);
}

double fieldstat_hll_get(const struct fieldstat *instance, int cube_id, int metric_id, int cell_id)
{
    const struct metric *metric = fieldstat_find_metric(instance, cube_id, metric_id);
    if (metric == NULL || metric_get_type(metric) != METRIC_TYPE_HLL) {
        return -1;
    }

    return metric_hll_get(metric, cell_id);
}

long long fieldstat_hist_value_at_percentile(const struct fieldstat *instance, int cube_id, int metric_id, int cell_id, double percentile)
{
    const struct metric *metric = fieldstat_find_metric(instance, cube_id, metric_id);
    if (metric == NULL || metric_get_type(metric) != METRIC_TYPE_HISTOGRAM) {
        return -1;
    }

    return metric_histogram_value_at_percentile(metric, cell_id, percentile);
}

long long fieldstat_hist_count_le_value(const struct fieldstat *instance, int cube_id, int metric_id, int cell_id, long long value)
{
    const struct metric *metric = fieldstat_find_metric(instance, cube_id, metric_id);
    if (metric == NULL || metric_get_type(metric) != METRIC_TYPE_HISTOGRAM) {
        return -1;
    }

    return metric_histogram_count_le_value(metric, cell_id, value);
}

void fieldstat_get_serialized_blob(const struct fieldstat *instance, int cube_id, int metric_id, int cell_id, char **blob, size_t *blob_size)
{
    const struct metric *metric = fieldstat_find_metric(instance, cube_id, metric_id);
    if (metric == NULL) {
        return;
    }
    enum metric_type type = metric_get_type(metric);
    if (!(type == METRIC_TYPE_HLL || type == METRIC_TYPE_HISTOGRAM)) {
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
        return NULL;
    }

    return metric_get_name(metric);
}

enum metric_type fieldstat_get_metric_type(const struct fieldstat *instance, int cube_id, int metric_id)
{
    const struct metric *metric = fieldstat_find_metric(instance, cube_id, metric_id);
    if (metric == NULL) {
        return (enum metric_type)-1;
    }

    return metric_get_type(metric);
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
        struct fs_cube *cube = instance->cube[i];
        if (cube == NULL) {
            continue;
        }
        new_instance->cube[i] = fieldstat_cube_new(cube->shared_tags, cube->n_shared_tags, cube->sampling_mode, cube->max_n_cell);
        cube_manager_add(new_instance->shared_tag_cube_manager, cube->key_tag, i);
        // copy registered metrics
        for (size_t j = 0; j < cube->n_metric; j++) {
            const struct metric *metric = cube->metrics[j];
            struct metric *new_metric = metric_fork(metric);
            append_metric_to_cube(new_instance->cube[i], new_metric);
        }
    }

    new_instance->cube_version = calloc(new_instance->max_n_cube, sizeof(unsigned long));
    return new_instance;
}

void fieldstat_get_cells_from_cube(const struct fieldstat *instance, int cube_id, int **cell_ids, struct fieldstat_tag_list **tag_list, size_t *n_cell)
{
    if (cube_id < 0 || cube_id >= instance->valid_cube_arr_length) {
        return;
    }
    const struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
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

int fieldstat_get_max_cell_id(const struct fieldstat *instance, int cube_id)
{
    const struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        return -1;
    }

    int max_id;
    (void)cell_manager_dump(cube->cell_manager, &max_id);
    return max_id;
}

int fieldstat_find_cube(const struct fieldstat *instance, const struct fieldstat_tag *shared_tags, size_t n_shared_tags)
{
    const struct cube_manager *tag_cube_id_map = instance->shared_tag_cube_manager;
    struct tag_hash_key *shared_tag_key = tag_hash_key_construct_with_fieldstat_tag(shared_tags, n_shared_tags);
    int cube_id = cube_manager_find(tag_cube_id_map, shared_tag_key);
    tag_hash_key_free(shared_tag_key);
    return cube_id;
}

void calibrate_metrics(const struct fs_cube *master, struct fs_cube *target)
{
    // metric won't be deleted or modified, so master always has more metrics than target, and metric under the same id must be the same. Just take care of the rest.
    for (int i = target->n_metric; i < master->n_metric; i++) { // i starts from target->n_metric, because target->n_metric is the first metric that is not in target.
        const struct metric *metric_master = master->metrics[i];
        struct metric *metric_dup = metric_fork(metric_master);
        append_metric_to_cube(target, metric_dup);
    }
}

void fieldstat_calibrate(const struct fieldstat *master, struct fieldstat *target)
{
    if (target->max_n_cube < master->max_n_cube) {
        target->cube = (struct fs_cube **)realloc(target->cube, sizeof(struct fs_cube *) * master->max_n_cube);
        target->cube_version = (unsigned long *)realloc(target->cube_version, sizeof(unsigned long) * master->max_n_cube);
        target->max_n_cube = master->max_n_cube;
    }

    size_t longer_arr_len = master->valid_cube_arr_length > target->valid_cube_arr_length ? master->valid_cube_arr_length : target->valid_cube_arr_length;
    for (size_t i = 0; i < longer_arr_len; i++) {
        const struct fs_cube *cube_master = i >= master->valid_cube_arr_length ? NULL : master->cube[i];
        struct fs_cube *cube_target = i >= target->valid_cube_arr_length ? NULL : target->cube[i];
        if (cube_master == NULL && cube_target == NULL) {
            continue;
        }
        if (cube_master == NULL && cube_target != NULL) {
            fieldstat_cube_free(target, i);
            continue;
        }
        if (cube_master != NULL && cube_target == NULL) {
            struct fs_cube *cube_dup = fieldstat_cube_fork(cube_master);
            target->cube[i] = cube_dup;
            continue;
        }

        if (master->cube_version[i] == target->cube_version[i] && tag_hash_key_cmp(cube_master->key_tag, cube_target->key_tag) == 0) {
            calibrate_metrics(cube_master, cube_target);
            continue;
        }
        
        struct fs_cube *cube_dup = fieldstat_cube_fork(cube_target);
        fieldstat_cube_free(target, i);
        target->cube[i] = cube_dup;
    }

    memcpy(target->cube_version, master->cube_version, sizeof(unsigned long) * master->max_n_cube);
    target->valid_cube_arr_length = master->valid_cube_arr_length;
}