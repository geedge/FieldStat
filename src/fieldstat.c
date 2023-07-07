
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "cjson/cJSON.h"
#include "mpack/mpack.h"
#include "fieldstat.h"
#include "fs_cell.h"

#include "uthash.h"


struct cell_id_map {
    int cell_id;
    struct fs_cell *cell;
    UT_hash_handle hh;
};

struct fs_cube {
    enum sampling_mode sampling_mode;
    struct cell_manager map;
    size_t max_n_cell;
    int current_cell_id;
    struct cell_id_map *cell_id_map;
    size_t n_cell;

    struct fieldstat_tag *shared_tags;
    size_t n_shared_tags;

};

struct fieldstat {
    struct fs_cube *cube;
    size_t n_cube;

    size_t max_n_metric;
    struct metric *metrics;
    size_t n_metric;
};



int name_id_map_find_a_field_name(struct fieldstat *instance, const char *field_name)
{
// todo: 现在复原是以cell为粒度。所以找到cell id 的过程顺起来了，就是由cube分配一个cell id，struct name_id_map删除

    return 0;
}

struct fieldstat *fieldstat_new()
{
    struct fieldstat *instance = malloc(sizeof(struct fieldstat));
    memset(instance, 0, sizeof(struct fieldstat));

    return instance;
}

void fieldstat_free(struct fieldstat *instance)
{
    // todo
}

void fieldstat_reset(struct fieldstat *instance)
{
    // todo
}

static int append_metric_to_instance(struct fieldstat *instance, struct metric *metric)
{
    if (instance->n_metric >= instance->max_n_metric) {
        instance->max_n_metric *= 2;
        instance->metrics = realloc(instance->metrics, sizeof(struct metric *) * instance->max_n_metric);
    }
    instance->metrics[instance->n_metric] = metric;
    instance->n_metric++;
    return instance->n_metric - 1;
}

/* -------------------------------------------------------------------------- */
/*                               field register                              */
/* -------------------------------------------------------------------------- */

int fieldstat_register_counter(struct fieldstat *instance, const char *field_name, bool is_gauge)
{
    struct metric *metrics = metrics_new_counter(field_name, is_gauge);
    return append_metric_to_instance(instance, metrics);
}

/* -------------------------------------------------------------------------- */
/*                               field operation                              */
/* -------------------------------------------------------------------------- */

int fieldstat_counter_incrby(struct fieldstat *instance, int metric_id, int cell_id, long long increment)
{
    struct metric *metrics_info = instance->metrics[metric_id];
    if (metrics_info == NULL || metrics_get_type(metrics_info) != METRICS_TYPE_COUNTER) {
        printf("ERR: metrics is not registered yet\n");
        return -1;
    }

    struct metric * metrics = cell_find_or_create_metric(instance->cell[cell_id], metrics_info);
    return metrics_counter_incrby(metrics, increment);
}

/* -------------------------------------------------------------------------- */
/*                                serialization                               */
/* -------------------------------------------------------------------------- */

/*
{
    "ver": <EXPORT_VER>
    "metrics"= {
        "name":
        "type":
        “para":<blob>
    }
    "cube"= {
        "cells":[
            "metrics":<blob of each metric>
        ]
        "tag": {
            "type":<topk/comp>
            "blob":<serialize counter or topk>
        }
    }
}
*/

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

    mpack_node_t field_arr = mpack_node_map_cstr(root, "metrics");
    for (int i = 0 ; i < mpack_node_array_length(field_arr); i++) {
        mpack_node_t field_blob_bin = mpack_node_array_at(field_arr, i);
        struct fs_cube *tmp_field = field_deserialize(mpack_node_bin_data(field_blob_bin), mpack_node_bin_size(field_blob_bin));
        append_metric_to_instance(instance, tmp_field);
    }
    instance->n_field = mpack_node_array_length(field_arr);

    mpack_tree_destroy(&tree);
 
    return instance;
}

int fieldstat_merge(struct fieldstat *instance, struct fieldstat *src)
{
    for (int i = 0; i < src->n_field; i++) {
        struct fs_cube *tmp_field = src->field_array[i];
        int field_id_tmp = name_id_map_find_a_field_name(instance, field_get_name(tmp_field));

        if (field_id_tmp == -1) {
            struct fs_cube *copied_field = field_dup(tmp_field);
            int alloced_id = append_metric_to_instance(instance, copied_field);
            name_id_map_add_a_field_name(instance, field_get_name(copied_field), alloced_id);
        } else {
            field_merge(instance->field_array[field_id_tmp], tmp_field);
        }
    }

    fieldstat_free(src);
    return 0;
}

int fieldstat_serialize(const struct fieldstat *instance, char **blob_out, size_t *blob_size_out)
{
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob_out, blob_size_out);

    mpack_start_map(&writer, 2); // version and a single array of fields

    mpack_write_cstr(&writer, "ver");
    mpack_write_i32(&writer, EXPORT_VER);

    mpack_write_cstr(&writer, "fields");
    mpack_start_array(&writer, instance->n_field);
    for (int i = 0; i < instance->n_field; i++) {
        char *field_blob = NULL;
        size_t field_blob_size = 0;
        field_serialize(instance->field_array[i], &field_blob, &field_blob_size);
        mpack_write_bin(&writer, field_blob, field_blob_size);
        free(field_blob);
    }
    mpack_finish_array(&writer);

    mpack_finish_map(&writer);
    mpack_writer_destroy(&writer);

    return 0;
}


/* -------------------------------------------------------------------------- */
/*                                    query                                   */
/* -------------------------------------------------------------------------- */
int fieldstat_get_max_field_id(const struct fieldstat *instance)
{
    return instance->n_field - 1;
}

const char *fieldstat_get_field_name(const struct fieldstat *instance, int field_id)
{
    struct fs_cube *field = instance->field_array[field_id];
    if (field == NULL) {
        printf("ERR: field is not registered yet\n");
        return NULL;
    }
    return field_get_name(field);
}

enum metric_type fieldstat_get_field_type(const struct fieldstat *instance, int field_id)
{
    struct fs_cube *field = instance->field_array[field_id];
    if (field == NULL) {
        printf("ERR: field is not registered yet\n");
        return (enum metric_type)0xFFFFFFFF;
    }
    return field_get_type(instance->field_array[field_id]);
}

static bool fieldstat_check_field(struct fs_cube *field, enum metric_type type)
{
    if (field == NULL) {
        printf("ERR: field is not registered yet\n");
        return false;
    }
    if (field_get_type(field) != type) {
        printf("ERR: field type is not matched\n");
        return false;
    }
    return true;
}

void fieldstat_get_tag_lists(const struct fieldstat *instance, int field_id, struct fieldstat_tag_group **lists, size_t *n_list)
{
    struct fs_cube *field = instance->field_array[field_id];
    if (field == NULL) {
        printf("ERR: field is not registered yet, field id : %d\n", field_id);
        *lists = NULL;
        *n_list = 0;
        return;
    }

    field_get_tags(field, lists, n_list);
}

long long fieldstat_counter_get(const struct fieldstat *instance, int field_id, const struct fieldstat_tag_group *tag_list)
{
    struct fs_cube *field = instance->field_array[field_id];
    if (!fieldstat_check_field(field, METRIC_TYPE_COUNTER)) {
        return -1;
    }

    struct cell *cell = field_find_cell(field, tag_list->tag, tag_list->n_tag);
    if (cell == NULL) {
        return 0;
    }

    return cell_counter_get(cell);
}

long long fieldstat_histogram_value_at_percentile(const struct fieldstat *instance, int field_id, const struct fieldstat_tag_group *tag_list, double percentile)
{
    struct fs_cube *field = instance->field_array[field_id];
    if (!fieldstat_check_field(field, METRIC_TYPE_HISTOGRAM)) {
        return -1;
    }

    struct cell *cell = field_find_cell(field, tag_list->tag, tag_list->n_tag);
    if (cell == NULL) {
        return 0;
    }

    return cell_histogram_value_at_percentile(cell, percentile);
}

long long fieldstat_histogram_count_le_value(struct fieldstat *instance, int field_id, const struct fieldstat_tag_group *tag_list, long long value)
{
    struct fs_cube *field = instance->field_array[field_id];
    if (!fieldstat_check_field(field, METRIC_TYPE_HISTOGRAM)) {
        return -1;
    }

    struct cell *cell = field_find_cell(field, tag_list->tag, tag_list->n_tag);
    if (cell == NULL) {
        return 0;
    }

    return cell_histogram_count_le_value(cell, value);
}

double fieldstat_hll_get(const struct fieldstat *instance, int field_id, const struct fieldstat_tag_group *tag_list)
{
    struct fs_cube *field = instance->field_array[field_id];
    if (!fieldstat_check_field(field, METRIC_TYPE_HLL)) {
        return -1;
    }

    struct cell *cell = field_find_cell(field, tag_list->tag, tag_list->n_tag);
    if (cell == NULL) {
        return 0;
    }

    return cell_hll_get(cell);
}

void fieldstat_get_serialized_blob(const struct fieldstat *instance, int field_id, const struct fieldstat_tag_group *tag_list, char **blob, size_t *blob_size)
{
    struct fs_cube *field = instance->field_array[field_id];
    if (field == NULL) {
        printf("ERR: field is not registered yet\n");
        return;
    }
    enum metric_type type = field_get_type(field);
    if (!(type == METRIC_TYPE_HLL || type == METRIC_TYPE_HISTOGRAM)) {
        printf("ERR: Only metric of hll and histogram support get serialized blob\n");
        return;
    }

    struct cell *cell = field_find_cell(field, tag_list->tag, tag_list->n_tag);
    if (cell == NULL) {
        *blob = NULL;
        *blob_size = 0;
        return;
    }

    cell_get_plain_blob(cell, blob, blob_size);
}
