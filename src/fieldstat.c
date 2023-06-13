
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "cjson/cJSON.h"
#include "mpack/mpack.h"
#include "fieldstat.h"
#include "field.h"

#include "uthash.h"


struct fieldstat {
    char name[INSTANCE_NAME_MAX_LEN];

    struct fs_field *field_array[METRICS_MAX_NUMBER]; // all the metrics are dynamic(differ from field stat 3.0) // TODO: 改成动态的
    size_t n_field;
};

struct fieldstat *fieldstat_new(const char *symbol)
{
    struct fieldstat *instance = malloc(sizeof(struct fieldstat));
    memset(instance, 0, sizeof(struct fieldstat));
    strncpy(instance->name, symbol, INSTANCE_NAME_MAX_LEN);

    return instance;
}

void fieldstat_free(struct fieldstat *instance)
{
    for (int i = 0; i < instance->n_field; i++) { // FIXME: 当前添加都是添加到最后，所以可以直接这样循环，会不会有unregister 的需求？
        field_free(instance->field_array[i]);
    }
    free(instance);
}

void fieldstat_refresh(struct fieldstat *instance)
{
    for (int i = 0; i < instance->n_field; i++) {
        field_refresh(instance->field_array[i]);
    }
}

static int append_field_to_instance(struct fieldstat *instance, const struct fs_field *field)
{
    if (instance->n_field >= METRICS_MAX_NUMBER) {  // TODO: realloc
        printf("append_field_to_instance: too many metrics\n");
        return -1;
    }
    instance->field_array[instance->n_field++] = (struct fs_field *)field;
    return instance->n_field - 1;
}

/* -------------------------------------------------------------------------- */
/*                               field operation                              */
/* -------------------------------------------------------------------------- */
int fieldstat_register_topk(struct fieldstat *instance, const char *field_name, int K)
{
    struct fs_field *field = field_new_topk(field_name, K);

    return append_field_to_instance(instance, field);
}

int fieldstat_topk_add(struct fieldstat *instance, int field_id, const char *key, size_t key_len, const struct fieldstat_tag tags[], size_t n_tag)
{
    struct fs_field *field = (struct fs_field *)instance->field_array[field_id];
    if (field == NULL) {
        printf("ERR: field is not registered yet\n");
        return -1;
    }
    field_topk_add(field, key, key_len, 1, tags, n_tag);
    return 0;
}



/* -------------------------------------------------------------------------- */
/*                                serialization                               */
/* -------------------------------------------------------------------------- */

/*
{
    name=<instance_name>,
    "fields"= [<binary of fields, refer to field.c annotation for its detail>]
}
*/
struct fieldstat *fieldstat_deserialize_by_mpack(const char *blob, size_t blob_size)
{
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, blob_size);
    mpack_tree_parse(&tree);
    mpack_node_t root = mpack_tree_root(&tree);

    struct fieldstat *instance = fieldstat_new(mpack_node_str(mpack_node_map_cstr(root, "name")));

    mpack_node_t field_arr = mpack_node_map_cstr(root, "fields");
    for (int i = 0 ; i < mpack_node_array_length(field_arr); i++) {
        struct fs_field *tmp_field = field_new_by_deserialize(mpack_node_bin_data(field_arr), mpack_node_bin_size(field_arr));
        int field_id_tmp = field_get_id(tmp_field);
        instance->field_array[field_id_tmp] = tmp_field;
    }
 
    return instance;
}

int fieldstat_merge_with_blob(struct fieldstat *instance, const char *blob, size_t blob_size)
{
    struct fieldstat *src = fieldstat_deserialize_by_mpack(blob, blob_size);
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
       
        if (instance->field_array[field_id_tmp] == NULL) {
            instance->field_array[field_id_tmp] = field_dup(tmp_field);
        } else {
            field_merge(instance->field_array[field_id_tmp], tmp_field);
        }
    }

    fieldstat_free(src);
    return 0;
}

// todo: serialize for specific field(具体讨论接口和实现，最终是付明卫调用)


/* -------------------------------------------------------------------------- */
/*                                    query                                   */
/* -------------------------------------------------------------------------- */
// user should free the returned pointer field_id_out
int fieldstat_get_field_id(const struct fieldstat *instance, int **field_id_out)
{
    *field_id_out = malloc(sizeof(int) * instance->n_field);
    for (int i = 0; i < instance->n_field; i++) {
        (*field_id_out)[i] = i;
    }
    return instance->n_field;
}

enum field_type fieldstat_get_type(const struct fieldstat *instance, int id)
{
    return field_get_type(instance->field_array[id]);
}

void fieldstat_topk_query(const struct fieldstat *instance, int id, const struct fieldstat_tag_list *tag_list , char ***keys_out, long long **count_out, size_t *n_key_out)
{
    struct fs_field *field = instance->field_array[id];
    if (field == NULL || field_get_type(field) != FIELD_TYPE_TOPK) {
        printf("ERR: field is not registered yet\n");
        return;
    }
    field_query_for_topk(field, tag_list->tag, tag_list->n_tag, keys_out, count_out, n_key_out);
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

// free fieldstat_tag_list: for every tag_list in tag_lists, clear tag_list->tag, free the pointer. free tag_lists
int fieldstat_get_tag_lists(struct fieldstat *instance, int field_id, struct fieldstat_tag_list *lists, size_t n_list)
{
    struct fieldstat_tag_list *tag_lists_out = NULL;
    size_t n_tag_lists_out = 0;

    struct fs_field *field = instance->field_array[field_id];
    if (field == NULL) {
        printf("ERR: field is not registered yet\n");
        return -1;
    }

    field_get_tags(field, &tag_lists_out, &n_tag_lists_out);

    if (n_tag_lists_out > n_list) {
        printf("ERR: n_tag_lists_out > n_list\n");
        for (int i = 0; i < n_tag_lists_out; i++) {
            tag_list_clear(&tag_lists_out[i]);
        }
        free(tag_lists_out);
        return -1;
    }
    for (int i = 0; i < n_tag_lists_out; i++) {
        lists[i] = tag_lists_out[i];
    }
    free(tag_lists_out); // don't need to free tag_list->tag, because we just copy the pointer. User should free it.
    return 0;
}

// fieldstat_counter_get(instance, field_id[i], tag_lists[j]);
// p50=fieldstat_histogram_value_at_percentile(instance, field_id[i], tag_lists[j], 0.5);
//  avg=fieldstat_histogram_average(instance, field_id[i], tag_lists[j]);
// 

// 