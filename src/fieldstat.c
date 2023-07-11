
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

// todo: 用来merge的时候用，暂时先放着
// todo: 应该是cube里的概念，总之再说
struct metric_name_id_map {
    char *name;
    int id;
    UT_hash_handle hh;
};

struct fs_cube {
    enum sampling_mode sampling_mode;
    struct cell_manager *cell_manager;
    size_t max_n_cell;
    int next_cell_id;

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

    struct metric_name_id_map *metric_name_id_map;
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

    struct metric_name_id_map *node, *tmp;
    HASH_ITER(hh, instance->metric_name_id_map, node, tmp) {
        HASH_DEL(instance->metric_name_id_map, node);
        free(node->name);
        free(node);
    }

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

int fieldstat_register_cube(struct fieldstat *instance, struct fieldstat_tag *shared_tags, size_t n_tag, enum sampling_mode mode, size_t max_n_cell)
{
    struct fs_cube *cube = malloc(sizeof(struct fs_cube));
    memset(cube, 0, sizeof(struct fs_cube));
    cube->sampling_mode = mode;
    cube->max_n_cell = max_n_cell;
    cube->next_cell_id = 0;
    cube->shared_tags = malloc(sizeof(struct fieldstat_tag) * n_tag);
    fieldtag_list_copy(shared_tags, n_tag, cube->shared_tags);
    cube->n_shared_tags = n_tag;

    cube->max_n_metric = 8;
    cube->metrics = malloc(sizeof(struct metric *) * cube->max_n_metric);
    cube->n_metric = 0;

    cube->cell_manager = cell_manager_new(mode, max_n_cell);

    if (instance->n_cube >= instance->max_n_cube) {
        instance->max_n_cube *= 2;
        instance->cube = realloc(instance->cube, sizeof(struct fs_cube *) * instance->max_n_cube);
    }
    instance->cube[instance->n_cube] = cube;
    instance->n_cube++;
    return instance->n_cube - 1;
}

int fieldstat_cube_add(struct fieldstat *instance, int cube_id, const struct fieldstat_tag *tags, size_t n_tag, unsigned int count)
{
    struct fs_cube *cube = instance->cube[cube_id];
    if (cube == NULL) {
        printf("ERR: cube is not registered yet\n");
        return -1;
    }

    int ret = cell_manager_add_cube(cube->cell_manager, tags, n_tag, (unsigned int)count, cube->next_cell_id);
    if (ret < 0) {
        return -1;
    }
    cube->next_cell_id++;
    return cube->next_cell_id - 1;
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

// /*
// {
//     "ver": <EXPORT_VER>
//     "metrics"= {
//         "name":
//         "type":
//         “para":<blob>
//     }
//     "cube"= {
//         "cells":[
//             "metrics":<blob of each metric>
//         ]
//         "tag": {
//             "type":<topk/comp>
//             "blob":<serialize counter or topk>
//         }
//     }
// }
// */

// struct fieldstat *fieldstat_deserialize(const char *blob, size_t blob_size)
// {
//     mpack_tree_t tree;
//     mpack_tree_init_data(&tree, blob, blob_size);
//     mpack_tree_parse(&tree);
//     mpack_node_t root = mpack_tree_root(&tree);

//     int ver = mpack_node_i32(mpack_node_map_cstr(root, "ver"));
//     if (ver != EXPORT_VER) {
//         printf("ERR: fieldstat_deserialize: invalid version\n");
//         mpack_tree_destroy(&tree);
//         return NULL;
//     }

//     struct fieldstat *instance = fieldstat_new();

//     mpack_node_t field_arr = mpack_node_map_cstr(root, "metrics");
//     for (int i = 0 ; i < mpack_node_array_length(field_arr); i++) {
//         mpack_node_t field_blob_bin = mpack_node_array_at(field_arr, i);
//         struct fs_cube *tmp_field = field_deserialize(mpack_node_bin_data(field_blob_bin), mpack_node_bin_size(field_blob_bin));
//         append_metric_to_instance(instance, tmp_field);
//     }
//     instance->n_field = mpack_node_array_length(field_arr);

//     mpack_tree_destroy(&tree);
 
//     return instance;
// }

// int fieldstat_merge(struct fieldstat *instance, struct fieldstat *src)
// {
//     for (int i = 0; i < src->n_field; i++) {
//         struct fs_cube *tmp_field = src->field_array[i];
//         int field_id_tmp = name_id_map_find_a_field_name(instance, field_get_name(tmp_field));

//         if (field_id_tmp == -1) {
//             struct fs_cube *copied_field = field_dup(tmp_field);
//             int alloced_id = append_metric_to_instance(instance, copied_field);
//             name_id_map_add_a_field_name(instance, field_get_name(copied_field), alloced_id);
//         } else {
//             field_merge(instance->field_array[field_id_tmp], tmp_field);
//         }
//     }

//     fieldstat_free(src);
//     return 0;
// }

// int fieldstat_serialize(const struct fieldstat *instance, char **blob_out, size_t *blob_size_out)
// {
//     mpack_writer_t writer;
//     mpack_writer_init_growable(&writer, blob_out, blob_size_out);

//     mpack_start_map(&writer, 2); // version and a single array of fields

//     mpack_write_cstr(&writer, "ver");
//     mpack_write_i32(&writer, EXPORT_VER);

//     mpack_write_cstr(&writer, "fields");
//     mpack_start_array(&writer, instance->n_field);
//     for (int i = 0; i < instance->n_field; i++) {
//         char *field_blob = NULL;
//         size_t field_blob_size = 0;
//         field_serialize(instance->field_array[i], &field_blob, &field_blob_size);
//         mpack_write_bin(&writer, field_blob, field_blob_size);
//         free(field_blob);
//     }
//     mpack_finish_array(&writer);

//     mpack_finish_map(&writer);
//     mpack_writer_destroy(&writer);

//     return 0;
// }


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
