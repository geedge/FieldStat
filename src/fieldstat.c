
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "cjson/cJSON.h"
#include "mpack/mpack.h"
#include "fieldstat.h"
#include "field.h"
#include "fs_cell.h"

#include "uthash.h"

// ut hash list with field name as key
struct field_name_id_map {
    char *field_name;
    int field_id;
    UT_hash_handle hh;
};

struct fieldstat {
    struct fs_field **field_array; // all the metrics are dynamic(differ from field stat 3.0)
    size_t n_field;
    size_t max_field;

    struct field_name_id_map *field_name_id_map; // for fast field id lookup, update only when deserializing.
};


int name_id_map_find_a_field_name(struct fieldstat *instance, const char *field_name)
{
    if (instance->field_name_id_map == NULL) {
        return -1;
    }
    // find the name in hash
    struct field_name_id_map *entry = NULL;
    HASH_FIND_STR(instance->field_name_id_map, field_name, entry);
    if (entry == NULL) {
        return -1;
    }
    return entry->field_id;
}

void name_id_map_add_a_field_name(struct fieldstat *instance, const char *field_name, int field_id)
{
    struct field_name_id_map *entry = malloc(sizeof(struct field_name_id_map));
    entry->field_id = field_id;
    entry->field_name = strdup(field_name);
    HASH_ADD_KEYPTR(hh, instance->field_name_id_map, entry->field_name, strlen(entry->field_name), entry);
}

void name_id_map_free(struct fieldstat *instance)
{
    struct field_name_id_map *map = instance->field_name_id_map;
    struct field_name_id_map *entry, *tmp;
    HASH_ITER(hh, map, entry, tmp) {
        HASH_DEL(map, entry);
        free(entry->field_name);
        free(entry);
    }
}

struct fieldstat *fieldstat_new()
{
    struct fieldstat *instance = malloc(sizeof(struct fieldstat));
    memset(instance, 0, sizeof(struct fieldstat));

    size_t init_array_len = 256;
    instance->max_field = init_array_len;
    instance->field_array = malloc(sizeof(struct fs_field *) * init_array_len);

    return instance;
}

void fieldstat_free(struct fieldstat *instance)
{
    for (int i = 0; i < instance->n_field; i++) { // FIXME: 当前添加都是添加到最后，所以可以直接这样循环，会不会有unregister 的需求？
        field_free(instance->field_array[i]);
    }
    name_id_map_free(instance);
    free(instance->field_array);
    free(instance);
}

void fieldstat_refresh(struct fieldstat *instance)
{
    for (int i = 0; i < instance->n_field; i++) {
        field_refresh(instance->field_array[i]);
    }
}

void fieldstat_reset(struct fieldstat *instance)
{
    for (int i = 0; i < instance->n_field; i++) {
        field_reset(instance->field_array[i]);
    }
}

static int append_field_to_instance(struct fieldstat *instance, struct fs_field *field)
{
    if (instance->n_field >= instance->max_field) {
        instance->max_field *= 2;
        instance->field_array = realloc(instance->field_array, sizeof(struct fs_field *) * instance->max_field);
    }
    instance->field_array[instance->n_field] = (struct fs_field *)field;
    field_set_id(field, instance->n_field);
    instance->n_field++;
    return instance->n_field - 1;
}

/* -------------------------------------------------------------------------- */
/*                               field register                              */
/* -------------------------------------------------------------------------- */
int fieldstat_register_topk(struct fieldstat *instance, const char *field_name, int K,
    const char *metric_name[], size_t n_metric, int primary_metric)
{
    if (primary_metric < 0 || primary_metric >= n_metric) {
        printf("fieldstat_register_topk: invalid primary_metric.\n");
        return -1;
    }
    if (n_metric == 0) {
        printf("fieldstat_register_topk: n_metric must be at lease one(primary metric).\n");
        return -1;
    }
    struct fs_field *field = field_new_topk(field_name, K, metric_name, n_metric, primary_metric);

    return append_field_to_instance(instance, field);
}

int fieldstat_register_counter(struct fieldstat *instance, const char *field_name)
{
    struct fs_field *field = field_new_counter(field_name);

    return append_field_to_instance(instance, field);
}

int fieldstat_register_histogram(struct fieldstat *instance, const char *field_name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures)
{
    if (lowest_trackable_value < 1 || significant_figures < 1 || 5 < significant_figures)
	{
        printf("ERR: fieldstat_register_histogram: invalid params.\n");
		return -1;
	}

	if (lowest_trackable_value * 2 > highest_trackable_value)
	{
        printf("ERR: fieldstat_register_histogram: invalid params.\n");
		return -1;
	}

    struct fs_field *field = field_new_histogram(field_name, lowest_trackable_value, highest_trackable_value, significant_figures);

    return append_field_to_instance(instance, field);
}

int fieldstat_register_hll(struct fieldstat *instance, const char *field_name, unsigned char precision)
{
    // Ensure the precision is somewhat sane
    if (precision < 4 || precision > 18) {
        printf("ERR: fieldstat_register_histogram: invalid params.\n");
        return -1;
    }

    struct fs_field *field = field_new_hll(field_name, precision);

    return append_field_to_instance(instance, field);
}

/* -------------------------------------------------------------------------- */
/*                               field operation                              */
/* -------------------------------------------------------------------------- */
int fieldstat_topk_add(struct fieldstat *instance, int field_id, const char *key, size_t key_len, unsigned int count[], const struct fieldstat_tag tags[], size_t n_tag)
{
    struct fs_field *field = (struct fs_field *)instance->field_array[field_id];
    if (field == NULL || field_get_type(field) != FIELD_TYPE_TOPK) {
        printf("ERR: field is not registered yet\n");
        return -1;
    }
    struct cell *cell = field_find_or_create_cell(field, tags, n_tag);
    cell_topk_add(cell, key, key_len, count);

    return 0;
}

int fieldstat_counter_incrby(struct fieldstat *instance, int field_id, long long increment, const struct fieldstat_tag tags[], size_t n_tag)
{
    struct fs_field *field = (struct fs_field *)instance->field_array[field_id];
    if (field == NULL || field_get_type(field) != FIELD_TYPE_COUNTER) {
        printf("ERR: field is not registered yet\n");
        return -1;
    }
    struct cell *cell = field_find_or_create_cell(field, tags, n_tag);
    cell_counter_incrby(cell, increment);
    return 0;
}

int fieldstat_histogram_record(struct fieldstat *instance, int field_id, long long value, const struct fieldstat_tag tags[], size_t n_tag)
{
    struct fs_field *field = (struct fs_field *)instance->field_array[field_id];
    if (field == NULL || field_get_type(field) != FIELD_TYPE_HISTOGRAM) {
        printf("ERR: field is not registered yet\n");
        return -1;
    }
    struct cell *cell = field_find_or_create_cell(field, tags, n_tag);
    return cell_histogram_record(cell, value);
}

int fieldstat_hll_add(struct fieldstat *instance, int field_id, const char *key, size_t key_len, const struct fieldstat_tag tags[], size_t n_tag)
{
    struct fs_field *field = (struct fs_field *)instance->field_array[field_id];
    if (field == NULL || field_get_type(field) != FIELD_TYPE_HLL) {
        printf("ERR: field is not registered yet\n");
        return -1;
    }
    struct cell *cell = field_find_or_create_cell(field, tags, n_tag);
    return cell_hll_add(cell, key, key_len);
}
/* -------------------------------------------------------------------------- */
/*                                serialization                               */
/* -------------------------------------------------------------------------- */

/*
{
    "ver": <EXPORT_VER>
    "fields"= [<binary of fields, refer to field.c annotation for its detail>]
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

    mpack_node_t field_arr = mpack_node_map_cstr(root, "fields");
    for (int i = 0 ; i < mpack_node_array_length(field_arr); i++) {
        mpack_node_t field_blob_bin = mpack_node_array_at(field_arr, i);
        struct fs_field *tmp_field = field_deserialize(mpack_node_bin_data(field_blob_bin), mpack_node_bin_size(field_blob_bin));
        append_field_to_instance(instance, tmp_field);
    }
    instance->n_field = mpack_node_array_length(field_arr);

    mpack_tree_destroy(&tree);
 
    return instance;
}

int fieldstat_merge_with_blob(struct fieldstat *instance, const char *blob, size_t blob_size)
{
    struct fieldstat *src = fieldstat_deserialize(blob, blob_size);
    if (src == NULL) {
        return -1;
    }

    for (int i = 0; i < src->n_field; i++) {
        struct fs_field *tmp_field = src->field_array[i];
        int field_id_tmp = name_id_map_find_a_field_name(instance, field_get_name(tmp_field));

        if (field_id_tmp == -1) {
            struct fs_field *copied_field = field_dup(tmp_field);
            int alloced_id = append_field_to_instance(instance, copied_field);
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
    struct fs_field *field = instance->field_array[field_id];
    if (field == NULL) {
        printf("ERR: field is not registered yet\n");
        return NULL;
    }
    return field_get_name(field);
}

enum field_type fieldstat_get_field_type(const struct fieldstat *instance, int field_id)
{
    struct fs_field *field = instance->field_array[field_id];
    if (field == NULL) {
        printf("ERR: field is not registered yet\n");
        return (enum field_type)0xFFFFFFFF;
    }
    return field_get_type(instance->field_array[field_id]);
}

static bool fieldstat_check_field(struct fs_field *field, enum field_type type)
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

void fieldstat_get_tag_lists(const struct fieldstat *instance, int field_id, struct fieldstat_tag_list **lists, size_t *n_list)
{
    struct fs_field *field = instance->field_array[field_id];
    if (field == NULL) {
        printf("ERR: field is not registered yet, field id : %d\n", field_id);
        *lists = NULL;
        *n_list = 0;
        return;
    }

    field_get_tags(field, lists, n_list);
}

int fieldstat_topk_query(const struct fieldstat *instance, int id, const struct fieldstat_tag_list *tag_list,
    char ***keys_out, size_t *n_key_out,
    char ***metric_name_out, size_t *n_metric_out, size_t *primary_metric_id_out,
    unsigned int ***metric_value_out)
{
    struct fs_field *field = instance->field_array[id];
    if (!fieldstat_check_field(field, FIELD_TYPE_TOPK)) {
        *n_key_out = 0;
        *keys_out = NULL;
        *n_metric_out = 0;
        *metric_name_out = NULL;
        *metric_value_out = NULL;
        return -1;
    }

    struct cell *cell = field_find_cell(field, tag_list->tag, tag_list->n_tag);
    if (cell == NULL) {
        *n_key_out = 0;
        *keys_out = NULL;
        *n_metric_out = 0;
        *metric_name_out = NULL;
        *metric_value_out = NULL;

        return 0;
    }

    cell_topk_query(cell, keys_out, n_key_out,
        metric_name_out, n_metric_out, primary_metric_id_out,
        metric_value_out);
    return 0;
}

long long fieldstat_counter_get(const struct fieldstat *instance, int field_id, const struct fieldstat_tag_list *tag_list)
{
    struct fs_field *field = instance->field_array[field_id];
    if (!fieldstat_check_field(field, FIELD_TYPE_COUNTER)) {
        return -1;
    }

    struct cell *cell = field_find_cell(field, tag_list->tag, tag_list->n_tag);
    if (cell == NULL) {
        return 0;
    }

    return cell_counter_get(cell);
}

long long fieldstat_histogram_value_at_percentile(const struct fieldstat *instance, int field_id, const struct fieldstat_tag_list *tag_list, double percentile)
{
    struct fs_field *field = instance->field_array[field_id];
    if (!fieldstat_check_field(field, FIELD_TYPE_HISTOGRAM)) {
        return -1;
    }

    struct cell *cell = field_find_cell(field, tag_list->tag, tag_list->n_tag);
    if (cell == NULL) {
        return 0;
    }

    return cell_histogram_value_at_percentile(cell, percentile);
}

long long fieldstat_histogram_count_le_value(struct fieldstat *instance, int field_id, const struct fieldstat_tag_list *tag_list, long long value)
{
    struct fs_field *field = instance->field_array[field_id];
    if (!fieldstat_check_field(field, FIELD_TYPE_HISTOGRAM)) {
        return -1;
    }

    struct cell *cell = field_find_cell(field, tag_list->tag, tag_list->n_tag);
    if (cell == NULL) {
        return 0;
    }

    return cell_histogram_count_le_value(cell, value);
}

double fieldstat_hll_get(const struct fieldstat *instance, int field_id, const struct fieldstat_tag_list *tag_list)
{
    struct fs_field *field = instance->field_array[field_id];
    if (!fieldstat_check_field(field, FIELD_TYPE_HLL)) {
        return -1;
    }

    struct cell *cell = field_find_cell(field, tag_list->tag, tag_list->n_tag);
    if (cell == NULL) {
        return 0;
    }

    return cell_hll_get(cell);
}

void fieldstat_get_serialized_blob(const struct fieldstat *instance, int field_id, const struct fieldstat_tag_list *tag_list, char **blob, size_t *blob_size)
{
    struct fs_field *field = instance->field_array[field_id];
    if (field == NULL) {
        printf("ERR: field is not registered yet\n");
        return;
    }
    enum field_type type = field_get_type(field);
    if (!(type == FIELD_TYPE_HLL || type == FIELD_TYPE_HISTOGRAM)) {
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