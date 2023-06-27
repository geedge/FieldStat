
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

// TODO: name 等输出相关量的序列化放到exporter里


struct fieldstat {
    struct fs_field **field_array; // all the metrics are dynamic(differ from field stat 3.0)
    size_t n_field;
    size_t max_field;
};

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
    printf("fieldstat_free, nfield: %zu\n", instance->n_field);
    for (int i = 0; i < instance->n_field; i++) { // FIXME: 当前添加都是添加到最后，所以可以直接这样循环，会不会有unregister 的需求？
        field_free(instance->field_array[i]);
    }
    free(instance->field_array);
    free(instance);
}

void fieldstat_refresh(struct fieldstat *instance)
{
    for (int i = 0; i < instance->n_field; i++) {
        field_refresh(instance->field_array[i]);
    }
}

static int append_field_to_instance(struct fieldstat *instance, struct fs_field *field)
{
    if (instance->n_field >= instance->max_field) {
        instance->max_field *= 2;
        instance->field_array = realloc(instance->field_array, sizeof(struct fs_field *) * instance->max_field);
    }
    instance->field_array[instance->n_field++] = (struct fs_field *)field;
    field_set_id(field, instance->n_field - 1);
    return instance->n_field - 1;
}

/* -------------------------------------------------------------------------- */
/*                               field register                              */
/* -------------------------------------------------------------------------- */
int fieldstat_register_topk(struct fieldstat *instance, const char *field_name, int K)
{
    struct fs_field *field = field_new_topk(field_name, K);

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
        printf("fieldstat_register_histogram: invalid params.\n");
		return -1;
	}

	if (lowest_trackable_value * 2 > highest_trackable_value)
	{
        printf("fieldstat_register_histogram: invalid params.\n");
		return -1;
	}

    struct fs_field *field = field_new_histogram(field_name, lowest_trackable_value, highest_trackable_value, significant_figures);

    return append_field_to_instance(instance, field);
}

int fieldstat_register_cardinality(struct fieldstat *instance, const char *field_name, unsigned char precision)
{
    // Ensure the precision is somewhat sane
    if (precision < 4 || precision > 18) {
        printf("fieldstat_register_histogram: invalid params.\n");
        return -1;
    }

    struct fs_field *field = field_new_cardinality(field_name, precision);

    return append_field_to_instance(instance, field);
}

/* -------------------------------------------------------------------------- */
/*                               field operation                              */
/* -------------------------------------------------------------------------- */
int fieldstat_topk_add(struct fieldstat *instance, int field_id, const char *key, size_t key_len, const struct fieldstat_tag tags[], size_t n_tag)
{
    struct fs_field *field = (struct fs_field *)instance->field_array[field_id];
    if (field == NULL || field_get_type(field) != FIELD_TYPE_TOPK) {
        printf("ERR: field is not registered yet\n");
        return -1;
    }
    struct cell *cell = field_find_or_create_cell(field, tags, n_tag);
    cell_topk_add(cell, key, key_len, 1);
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

int fieldstat_cardinality_add(struct fieldstat *instance, int field_id, const char *key, size_t key_len, const struct fieldstat_tag tags[], size_t n_tag)
{
    struct fs_field *field = (struct fs_field *)instance->field_array[field_id];
    if (field == NULL || field_get_type(field) != FIELD_TYPE_CARDINALITY) {
        printf("ERR: field is not registered yet\n");
        return -1;
    }
    struct cell *cell = field_find_or_create_cell(field, tags, n_tag);
    return cell_cardinality_add(cell, key, key_len);
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
        printf("fieldstat_deserialize: invalid version\n");
        mpack_tree_destroy(&tree);
        return NULL;
    }

    struct fieldstat *instance = fieldstat_new();

    mpack_node_t field_arr = mpack_node_map_cstr(root, "fields");
    for (int i = 0 ; i < mpack_node_array_length(field_arr); i++) {
        mpack_node_t field_blob_bin = mpack_node_array_at(field_arr, i);
        struct fs_field *tmp_field = field_deserialize(mpack_node_bin_data(field_blob_bin), mpack_node_bin_size(field_blob_bin));
        int field_id_tmp = field_get_id(tmp_field);
        // TODO: 应该是用名字来做key，而不是id
        instance->field_array[field_id_tmp] = tmp_field;
        printf("fieldstat_deserialize field (id:%d) add to instance, field pointer: %p \n", field_id_tmp, tmp_field);
    }
    instance->n_field = mpack_node_array_length(field_arr);

    mpack_tree_destroy(&tree);
 
    return instance;
}

int fieldstat_merge_with_blob(struct fieldstat *instance, const char *blob, size_t blob_size)
{
    struct fieldstat *src = fieldstat_deserialize(blob, blob_size);
    if (src == NULL) {
        fprintf(stderr, "fieldstat_deserialize failed\n");
        return -1;
    }
    if (instance->n_field != src->n_field) {
        fprintf(stderr, "instance->n_field != src->n_field\n");
        return -1;
    }

    for (int i = 0; i < src->n_field; i++) {
        struct fs_field *tmp_field = src->field_array[i];
        int field_id_tmp = field_get_id(tmp_field);
        // TODO: 不用id，用name
       
        if (instance->field_array[field_id_tmp] == NULL) {
            instance->field_array[field_id_tmp] = field_dup(tmp_field);
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

void fieldstat_topk_query(const struct fieldstat *instance, int id, const struct fieldstat_tag_list *tag_list, char ***keys_out, long long **count_out, size_t *n_key_out)
{
    struct fs_field *field = instance->field_array[id];
    if (field == NULL || field_get_type(field) != FIELD_TYPE_TOPK) {
        printf("ERR: field is not registered yet\n");
        return;
    }
    struct cell *cell = field_find_cell(field, tag_list->tag, tag_list->n_tag);
    if (cell == NULL) {
        *n_key_out = 0;
        *keys_out = NULL;
        *count_out = NULL;

        return;
    }

    cell_topk_query(cell, keys_out, count_out, n_key_out);
}

void tag_list_clear(struct fieldstat_tag_list *tag_list)
{
    for (int i = 0; i < tag_list->n_tag; i++) {
        struct fieldstat_tag *tag = &tag_list->tag[i];
        free((char *)tag->key);
        if (tag->type == TAG_CSTRING && tag->value_str) {
            free((char *)tag->value_str);
        }
    }
    free(tag_list->tag);
}

// TODO: 这个改成直接生成数组
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

long long fieldstat_counter_get(const struct fieldstat *instance, int field_id, const struct fieldstat_tag_list *tag_list)
{
    struct fs_field *field = instance->field_array[field_id];
    if (field == NULL || field_get_type(field) != FIELD_TYPE_COUNTER) {
        printf("ERR: field is not registered yet\n");
        return -1;
    }
    struct cell *cell = field_find_cell(field, tag_list->tag, tag_list->n_tag);
    if (cell == NULL) {
        return 0;
    }

    return cell_counter_get(cell);
}

long long fieldstat_histogram_value_at_percentile(struct fieldstat *instance, int field_id, const struct fieldstat_tag_list *tag_list, double percentile)
{
    struct fs_field *field = instance->field_array[field_id];
    if (field == NULL || field_get_type(field) != FIELD_TYPE_HISTOGRAM) {
        printf("ERR: field is not registered yet\n");
        return -1;
    }
    struct cell *cell = field_find_cell(field, tag_list->tag, tag_list->n_tag);
    if (cell == NULL) {
        return 0;
    }

    return cell_histogram_value_at_percentile(cell, percentile);
}

//  avg=fieldstat_histogram_average(instance, field_id[i], tag_lists[j]); 
// TODO: histogram 两种query

double fieldstat_cardinality_get(const struct fieldstat *instance, int field_id, const struct fieldstat_tag_list *tag_list)
{
    struct fs_field *field = instance->field_array[field_id];
    if (field == NULL || field_get_type(field) != FIELD_TYPE_CARDINALITY) {
        printf("ERR: field is not registered yet\n");
        return -1;
    }
    struct cell *cell = field_find_cell(field, tag_list->tag, tag_list->n_tag);
    if (cell == NULL) {
        return 0;
    }

    return cell_cardinality_get(cell);
}

void fieldstat_get_serialized_blob(const struct fieldstat *instance, int field_id, const struct fieldstat_tag_list *tag_list, char **blob, size_t *blob_size)
{
    struct fs_field *field = instance->field_array[field_id];
    if (field == NULL) {
        printf("ERR: field is not registered yet\n");
        return;
    }
    enum field_type type = field_get_type(field);
    if (!(type == FIELD_TYPE_CARDINALITY || type == FIELD_TYPE_HISTOGRAM)) {
        printf("ERR: Only metric of cardinality and histogram support get serialized blob\n");
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