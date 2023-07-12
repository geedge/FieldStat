
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "cjson/cJSON.h"
#include "uthash.h"
#include "mpack/mpack.h"
#include "fieldstat.h"
#include "metrics/metric.h"
#include "tags/dimension_cube_map.h"

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
};

struct fieldstat {
    struct fs_cube **cube;
    size_t n_cube;
    size_t max_n_cube;
};

struct metric_name_id_map {
    char *name;
    int id;
    UT_hash_handle hh;
};

struct fieldstat *fieldstat_new()
{
    struct fieldstat *instance = malloc(sizeof(struct fieldstat));
    memset(instance, 0, sizeof(struct fieldstat));

    instance->max_n_cube = 5;
    instance->cube = malloc(sizeof(struct fs_cube *) * instance->max_n_cube);
    memset(instance->cube, 0, sizeof(struct fs_cube *) * instance->max_n_cube);

    return instance;
}

void fieldstat_cube_free(struct fieldstat *instance, int cube_id);
void fieldstat_free(struct fieldstat *instance)
{
    for (size_t i = 0; i < instance->n_cube; i++) {
        fieldstat_cube_free(instance, i);
    }
    free(instance->cube);

    free(instance);
}

void fieldstat_reset(struct fieldstat *instance)
{
    // todo
}

/* -------------------------------------------------------------------------- */
/*                                    cube                                    */
/* -------------------------------------------------------------------------- */

void fieldstat_clear_one_tag(const struct fieldstat_tag *tag)
{
    free((char *)tag->key);
    if (tag->type == TAG_CSTRING) {
        free((char *)tag->value_str);
    }
}

static void fieldtag_list_copy(const struct fieldstat_tag *tag, size_t n_tag, struct fieldstat_tag *dest)
{
    for (size_t i = 0; i < n_tag; i++) {
        dest[i].key = strdup(tag[i].key);
        dest[i].type = tag[i].type;
        switch(tag->type)
        {
            case TAG_INTEGER:
                dest[i].value_int = tag[i].value_int;
                break;
            case TAG_DOUBLE:
                dest[i].value_double = tag[i].value_double;
                break;
            case TAG_CSTRING:
                dest[i].value_str = strdup(tag[i].value_str);
                break;
            default:
                assert(0);
                break;
        }
    }
}

int fieldstat_append_cube_to_instance(struct fieldstat *instance, struct fs_cube *cube)
{
    if (instance->n_cube >= instance->max_n_cube) {
        instance->max_n_cube *= 2;
        instance->cube = realloc(instance->cube, sizeof(struct fs_cube *) * instance->max_n_cube);
    }
    instance->cube[instance->n_cube] = cube;
    instance->n_cube++;
    return instance->n_cube - 1;
}

void fieldstat_cube_info_init(struct fs_cube *cube, const struct fieldstat_tag *shared_tags, size_t n_tag, enum sampling_mode mode, size_t max_n_cell)
{
    memset(cube, 0, sizeof(struct fs_cube));
    cube->sampling_mode = mode;
    cube->max_n_cell = max_n_cell;
    cube->next_cell_id = 0;
    cube->shared_tags = malloc(sizeof(struct fieldstat_tag) * n_tag);
    fieldtag_list_copy(shared_tags, n_tag, cube->shared_tags);
    cube->n_shared_tags = n_tag;

    cube->max_n_metric = 16;
    cube->metrics = malloc(sizeof(struct metric *) * cube->max_n_metric);
    cube->n_metric = 0;
}

struct fs_cube *fieldstat_cube_new(const struct fieldstat_tag *shared_tags, size_t n_tag, enum sampling_mode mode, size_t max_n_cell)
{
    struct fs_cube *cube = malloc(sizeof(struct fs_cube));
    fieldstat_cube_info_init(cube, shared_tags, n_tag, mode, max_n_cell);

    cube->cell_manager = cell_manager_new(mode, max_n_cell);

    return cube;
}

int fieldstat_register_cube(struct fieldstat *instance, const struct fieldstat_tag *shared_tags, size_t n_tag, enum sampling_mode mode, size_t max_n_cell)
{
    if (n_tag == 0) {
        printf("ERR: shared tags must not be empty\n");
        return -1;
    }
    struct fs_cube *cube = fieldstat_cube_new(shared_tags, n_tag, mode, max_n_cell);

    int cube_id = fieldstat_append_cube_to_instance(instance, cube);
    return cube_id;
}

int fieldstat_cube_add(struct fieldstat *instance, int cube_id, const struct fieldstat_tag *tags, size_t n_tag, unsigned int count)
{
    struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        printf("ERR: cube is not registered yet\n");
        return -1;
    }

    int ret = cell_manager_add_cell(cube->cell_manager, tags, n_tag, (unsigned int)count);

    return ret;
}

void fieldstat_cube_free(struct fieldstat *instance, int cube_id)
{
    struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        printf("ERR: cube is not registered yet\n");
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

int fieldstat_register_counter(struct fieldstat *instance, int cube_id, const char *field_name, bool is_gauge)
{
    struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        printf("ERR: cube is not registered yet\n");
        return -1;
    }
    struct metric *metric = metric_counter_new(field_name, is_gauge, cube->sampling_mode == SAMPLING_MODE_TOPK ? true : false);

    return append_metric_to_cube(cube, metric);
}

/* -------------------------------------------------------------------------- */
/*                               metric operation                              */
/* -------------------------------------------------------------------------- */
struct metric *fieldstat_find_metric(const struct fieldstat *instance, int cube_id, int metric_id)
{
    struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        printf("ERR: cube is not registered yet\n");
        return NULL;
    }
    return cube->metrics[metric_id];
}

int fieldstat_counter_incrby(struct fieldstat *instance, int metric_id, int cube_id, int cell_id, long long increment)
{
    struct metric *metric = fieldstat_find_metric(instance, cube_id, metric_id);
    if (metric == NULL || metric_get_type(metric) != METRIC_TYPE_COUNTER) {
        printf("ERR: metrics is not registered yet\n");
        return -1;
    }

    return metric_counter_incrby(metric, cell_id, increment);
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
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob_out, blob_size_out);
    mpack_build_map(&writer);

    mpack_write_cstr(&writer, "ver");
    mpack_write_i32(&writer, EXPORT_VER);

    mpack_write_cstr(&writer, "cube");
    mpack_build_array(&writer);
    for (int i = 0; i < instance->n_cube; i++) {
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
        struct fs_cube *fs_cube = malloc(sizeof(struct fs_cube));
        fieldstat_cube_info_init(fs_cube, shared_tags, n_shared_tags, sampling_mode, max_n_cell);
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
    struct fs_cube *cube_dup = fieldstat_cube_new(cube->shared_tags, cube->n_shared_tags, cube->sampling_mode, cube->max_n_cell);
    
    for (int i = 0; i < cube->n_metric; i ++) {
        struct metric *metric_dup = metric_copy_structure(cube->metrics[i]);
        metric_copy_cell(metric_dup, cube->metrics[i], i, i);
        append_metric_to_cube(cube_dup, metric_dup);
    }
    return cube_dup;
}

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

void cube_push_all_cells_to_another_cell_manager(const struct fs_cube *src, struct cell_manager *cell_manager_aim)
{
    int *cell_ids_src;
    size_t cell_id_count_src;
    struct fieldstat_tag *tags_src;
    size_t tag_count_src;
    struct cell_manager *cell_manager_src = src->cell_manager;
    for (int i = 0; i < src->n_metric; i ++) {
        metric_get_cell_ids(src->metrics[i], &cell_ids_src, &cell_id_count_src);
        if (cell_id_count_src == 0) {
            continue;
        }
        for (int j = 0; j < cell_id_count_src; j ++) {
            cell_manager_get_tag_by_cube_id(cell_manager_src, cell_ids_src[j], &tags_src, &tag_count_src);
            int count = cell_manager_get_count_by_tag(cell_manager_src, tags_src, tag_count_src);
            (void)cell_manager_add_cell(cell_manager_aim, tags_src, tag_count_src, count);

            fieldtag_list_free(tags_src, tag_count_src);
        }
        free(cell_ids_src);
    }
}

int fieldstat_cube_merge_comprehensive(struct fs_cube *dest, const struct fs_cube *src)
{
    struct cell_manager *cell_manager_src = src->cell_manager;
    struct cell_manager *cell_manager_dest = dest->cell_manager;
    int *cell_ids_src;
    size_t cell_id_count_src;
    struct fieldstat_tag *tags_src;
    size_t tag_count_src;
    struct metric_name_id_map *name_id_map_dest = NULL;
    name_id_map_init(&name_id_map_dest, dest);

    for (int metric_id_src = 0; metric_id_src < src->n_metric; metric_id_src ++) {
        metric_get_cell_ids(src->metrics[metric_id_src], &cell_ids_src, &cell_id_count_src);
        if (cell_id_count_src == 0) { // no cell in this metric
            continue;
        }

        const char *name_src = metric_get_name(src->metrics[metric_id_src]);
        int metric_id_dest = name_id_map_get_id_by_name(name_id_map_dest, name_src);
        if (metric_id_dest == -1) { // metric not exist in dest, new an empty metric
            struct metric *metric_dup = metric_copy_structure(src->metrics[metric_id_src]);
            metric_id_dest = append_metric_to_cube(dest, metric_dup);
        }
        for (int j = 0; j < cell_id_count_src; j ++) {
            int cell_id_tmp_src = cell_ids_src[j];
            cell_manager_get_tag_by_cube_id(cell_manager_src, cell_ids_src[j], &tags_src, &tag_count_src);

            int cell_id_tmp_dest = cell_manager_get_cube_id_by_tag(cell_manager_dest, tags_src, tag_count_src);
            if (cell_id_tmp_dest != -1) {
                metric_merge_cell(dest->metrics[metric_id_dest], src->metrics[metric_id_src], cell_id_tmp_dest, cell_id_tmp_src);
                fieldtag_list_free(tags_src, tag_count_src);
                continue;
            }
            cell_id_tmp_dest = cell_manager_add_cell(cell_manager_dest, tags_src, tag_count_src, 0);
            if (cell_id_tmp_dest == -1) { // the cube has already full
                fieldtag_list_free(tags_src, tag_count_src);
                break;
            }
            metric_copy_cell(dest->metrics[metric_id_dest], src->metrics[metric_id_src], cell_id_tmp_dest, cell_id_tmp_src);
            fieldtag_list_free(tags_src, tag_count_src);
        }
        free(cell_ids_src);
    }

    name_id_map_free(name_id_map_dest);
    return 0;
}

int fieldstat_cube_merge_topk(struct fs_cube *dest, const struct fs_cube *src)
{
    struct cell_manager *cell_manager_final = cell_manager_new(SAMPLING_MODE_TOPK, dest->max_n_cell);
    struct cell_manager *cell_manager_src = src->cell_manager;
    struct cell_manager *cell_manager_dest = dest->cell_manager;
    struct metric_name_id_map *name_id_map_src = NULL;
    struct metric_name_id_map *name_id_map_dest = NULL;
    int *cell_ids_dest;
    size_t cell_id_count_dest;
    int *cell_ids_src;
    size_t cell_id_count_src;
    struct fieldstat_tag *tags_dest;
    struct fieldstat_tag *tags_src;
    size_t tag_count_dest;
    size_t tag_count_src;
    name_id_map_init(&name_id_map_src, src);
    name_id_map_init(&name_id_map_dest, dest);

    // merge cell manager
    cube_push_all_cells_to_another_cell_manager(src, cell_manager_final);
    cube_push_all_cells_to_another_cell_manager(dest, cell_manager_final);


    // dest cube manager   |   merged cube manager | src cube manager    |  operation
    //  -----              |   -----               | -----               |  -----
    // exist               |           exist       |       exist         |  merge cell_src and cell_dest and change id of cell_dest to cell_final_id
    // exist               |           exist       |       non           |  change id of cell_dest to cell_final_id
    // exist               |           non         |       exist         |  delete cell_dest
    // exist               |           non         |       non           |  delete cell_dest
    for (int metric_id_dest = 0; metric_id_dest < dest->n_metric; metric_id_dest ++) {
        const char *name_dest = metric_get_name(dest->metrics[metric_id_dest]);
        int metric_id_src = name_id_map_get_id_by_name(name_id_map_src, name_dest);
        if (metric_id_src == -1) { // metric not exist in src
            continue;
        }
        metric_get_cell_ids(dest->metrics[metric_id_dest], &cell_ids_dest, &cell_id_count_dest);
        if (cell_id_count_dest == 0) { // no cell in metric
            continue;
        }
        for (int j = 0; j < cell_id_count_dest; j ++) {
            int cell_id_tmp_dest = cell_ids_dest[j];

            if (cell_manager_get_tag_by_cube_id(cell_manager_dest, cell_id_tmp_dest, &tags_dest, &tag_count_dest) == -1) {
                printf("error: cell_id %d not exist in dest\n", cell_id_tmp_dest);
                continue;
            }
            int cell_id_tmp_final = cell_manager_get_cube_id_by_tag(cell_manager_final, tags_dest, tag_count_dest);
            int cell_id_tmp_src = cell_manager_get_cube_id_by_tag(cell_manager_src, tags_dest, tag_count_dest);
            if (cell_id_tmp_final != -1 && cell_id_tmp_src != -1) { // tags exist in dest, final and source
                metric_merge_cell(dest->metrics[metric_id_dest], src->metrics[metric_id_src], cell_id_tmp_dest, cell_id_tmp_src);
                metric_change_cell_id(dest->metrics[metric_id_dest], cell_id_tmp_dest, cell_id_tmp_final);
            } else if (cell_id_tmp_final != -1 && cell_id_tmp_src == -1) { // tags exist in dest and final, but not source
                metric_change_cell_id(dest->metrics[metric_id_dest], cell_id_tmp_dest, cell_id_tmp_final);
            } else { // tag exist in dest, but not final(source exist or not exist)
                metric_delete_cell(dest->metrics[metric_id_dest], cell_id_tmp_dest);
            }
            fieldtag_list_free(tags_dest, tag_count_dest);
        }
        free(cell_ids_dest);
    }

    // dest cube manager   |   merged cube manager  | src cube manager    |     operation
    //  -----               |   -----               | -----               |  -----
    // non                     exist                  exist                     copy cell_src to cell_dest, set its id to cell_final_id
// other unprocessed cases:
    // non                            exist                non                       --   (impossible case)
    // non                            non                  exist                   no operation
    // non                            non                  non                     no operation
    for (int metric_id_src = 0; metric_id_src < src->n_metric; metric_id_src ++) {
        metric_get_cell_ids(src->metrics[metric_id_src], &cell_ids_src, &cell_id_count_src);
        if (cell_ids_src == NULL) { // no cell in src metric
            continue;
        }
        const char *name_src = metric_get_name(src->metrics[metric_id_src]);
        int metric_id_dest = name_id_map_get_id_by_name(name_id_map_dest, name_src);
        if (metric_id_dest == -1) { // metric not exist in dest, new an empty metric
            struct metric *metric_dup = metric_copy_structure(src->metrics[metric_id_src]);
            metric_id_dest = append_metric_to_cube(dest, metric_dup);
        }

        for (int j = 0; j < cell_id_count_src; j ++) {
            int cell_id_tmp_src = cell_ids_src[j];
            if (cell_manager_get_tag_by_cube_id(cell_manager_src, cell_id_tmp_src, &tags_src, &tag_count_src) == -1) {
                printf("error: cell_id %d not exist in src\n", cell_id_tmp_src);
                continue;
            }
            int cell_id_tmp_final = cell_manager_get_cube_id_by_tag(cell_manager_final, tags_src, tag_count_src);
            if (cell_id_tmp_final == -1) { // tags not exist in final, which have been processed in previous loop
                fieldtag_list_free(tags_src, tag_count_src);
                continue;
            }
            int cell_id_tmp_dest = cell_manager_get_cube_id_by_tag(cell_manager_dest, tags_src, tag_count_src);
            if (cell_id_tmp_dest != -1) { // tags exist in dest, which have been processed in previous loop
                fieldtag_list_free(tags_src, tag_count_src);
                continue;
            }
            metric_copy_cell(dest->metrics[metric_id_dest], src->metrics[metric_id_src], cell_id_tmp_final, cell_id_tmp_src);

            fieldtag_list_free(tags_src, tag_count_src);
        }
        free(cell_ids_src);
    }

    cell_manager_free(cell_manager_dest);
    dest->cell_manager = cell_manager_final;

    name_id_map_free(name_id_map_dest);
    name_id_map_free(name_id_map_src);

    return 0;
}

int fieldstat_cube_merge(struct fs_cube *dest, const struct fs_cube *src)
{
    if (dest->sampling_mode == SAMPLING_MODE_COMPREHENSIVE) {
        return fieldstat_cube_merge_comprehensive(dest, src);
    }
    return fieldstat_cube_merge_topk(dest, src);
}

int fieldstat_merge(struct fieldstat *instance, struct fieldstat *src)
{
    // tag_id_map use cube shared tag as key, find cubes. Is is not a cell manager.
    struct cell_manager *tag_id_map = cell_manager_new(SAMPLING_MODE_COMPREHENSIVE, instance->n_cube + src->n_cube);
    for (int i = 0; i < instance->n_cube; i++) {
        cell_manager_add_cell(tag_id_map, instance->cube[i]->shared_tags, instance->cube[i]->n_shared_tags, 0);
    }

    for (int i = 0; i < src->n_cube; i++) {
        struct fs_cube *cube_src = src->cube[i];
        int cude_id_tmp = cell_manager_get_cube_id_by_tag(tag_id_map, cube_src->shared_tags, cube_src->n_shared_tags);
        if (cude_id_tmp == -1) {
            struct fs_cube *copied_cube = fieldstat_cube_dup(cube_src);
            (void)fieldstat_append_cube_to_instance(instance, copied_cube);
        } else {
            if (instance->cube[cude_id_tmp]->sampling_mode != cube_src->sampling_mode) {
                printf("ERR: cube sampling mode not match\n");
                return -1;
            }
            fieldstat_cube_merge(instance->cube[cude_id_tmp], cube_src);
        }
    }

    cell_manager_free(tag_id_map);
    return 0;
}

/* -------------------------------------------------------------------------- */
/*                                    query                                   */
/* -------------------------------------------------------------------------- */
int fieldstat_get_max_cube_id(const struct fieldstat *instance)
{
    return instance->n_cube - 1;
}

int fieldstat_get_max_metric_id(const struct fieldstat *instance, int cube_id)
{
    struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        printf("ERR: cube is not registered yet\n");
        return -1;
    }
    return cube->n_metric - 1;
}

void fieldstat_get_cells(const struct fieldstat *instance, int cube_id, int metric_id,
    int **cell_ids, struct fieldstat_tag_list **tag_list, size_t *n_cell)
{
    struct metric *metric = fieldstat_find_metric(instance, cube_id, metric_id);
    if (metric == NULL) {
        printf("ERR: metrics is not registered yet\n");
        return;
    }

    metric_get_cell_ids(metric, cell_ids, n_cell);
    if (*n_cell == 0) {
        return;
    }

    *tag_list = (struct fieldstat_tag_list *)malloc(sizeof(struct fieldstat_tag_list) * (*n_cell));
    for (int i = 0; i < *n_cell; i++) {
        cell_manager_get_tag_by_cube_id(instance->cube[cube_id]->cell_manager, (*cell_ids)[i], &((*tag_list)[i].tag), &((*tag_list)[i].n_tag));
    }
}

long long fieldstat_counter_get(const struct fieldstat *instance, int cube_id, int metric_id, int cell_id)
{
    printf("fieldstat_counter_get enter\n");
    struct metric *metric = fieldstat_find_metric(instance, cube_id, metric_id);
    if (metric == NULL || metric_get_type(metric) != METRIC_TYPE_COUNTER) {
        printf("ERR: metrics is not registered yet\n");
        return -1;
    }

    return metric_counter_get(metric, cell_id);
}

void fieldstat_tag_list_arr_free(struct fieldstat_tag_list *tag_list, size_t n_cell)
{
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
    struct metric *metric = fieldstat_find_metric(instance, cube_id, metric_id);
    if (metric == NULL) {
        printf("ERR: metrics is not registered yet\n");
        return NULL;
    }

    return metric_get_name(metric);
}

enum metric_type fieldstat_get_metric_type(const struct fieldstat *instance, int cube_id, int metric_id)
{
    struct metric *metric = fieldstat_find_metric(instance, cube_id, metric_id);
    if (metric == NULL) {
        printf("ERR: metrics is not registered yet\n");
        return -1;
    }

    return metric_get_type(metric);
}
