#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#include "cjson/cJSON.h"
#include "uthash.h"
#include "fieldstat.h"
#include "fieldstat_exporter.h"

typedef struct fieldstat_tag Export_kv_pair;

// struct topk_cell_iter {
//     int result_n;
//     int curr_idx;
//     char **result_key;
//     unsigned int *result_count;
// };

struct cell_iter {
    int max_id;
    int curr_id;
    struct fieldstat_tag_list *tag_list;
    size_t tag_num;
    size_t tag_idx;
    struct fieldstat *instance;
};


void kv_pair_free(Export_kv_pair *pair) {
    if (pair->type == TAG_CSTRING) {
        free((char *)pair->value_str);
    }
    free((char *)pair->key);
    free(pair);
}

int my_cmp(const void *a, const void *b)
{
    Export_kv_pair *aa = (Export_kv_pair *)a;
    Export_kv_pair *bb = (Export_kv_pair *)b;
    int key_cmp = strcmp(aa->key, bb->key);
    if (key_cmp != 0) {
        return key_cmp;
    }

    assert(aa->type == bb->type);
    switch (aa->type) {
        case TAG_INTEGER:
            return aa->value_int - bb->value_int;
        case TAG_DOUBLE:
            return aa->value_double - bb->value_double;
        case TAG_CSTRING:
            return strcmp(aa->value_str, bb->value_str);
        default:
            return 0;
    }
}

void kv_pair_sort(Export_kv_pair *tag_list, size_t arr_length)
{
    qsort(tag_list, arr_length, sizeof(Export_kv_pair), my_cmp);
}

/* -------------------------------------------------------------------------- */
/*                                    iter                                    */
/* -------------------------------------------------------------------------- */
struct cell_iter *cell_iter_new(struct fieldstat *instance) {
    struct cell_iter *iter = malloc(sizeof(struct cell_iter));
    iter->max_id = fieldstat_get_max_field_id(instance);
    iter->curr_id = 0;
    iter->tag_list = NULL;
    iter->tag_num = 0;
    iter->tag_idx = 0;
    iter->instance = instance;
    return iter;
}

void cell_iter_free_tag_list(struct cell_iter *iter) {
    for (int i = 0; i < iter->tag_num; i++){
        tag_list_clear(&iter->tag_list[i]);
    }
    free(iter->tag_list);
    iter->tag_list = NULL;
    iter->tag_num = 0;
}

void cell_iter_free(struct cell_iter *iter) {
    if (iter->tag_list) {
        cell_iter_free_tag_list(iter);
    }
    free(iter);
}

int cell_iter_next(struct cell_iter *iter) {
    if (iter->tag_list == NULL) { // first time
        fieldstat_get_tag_lists(iter->instance, iter->curr_id, &iter->tag_list, &iter->tag_num);
        return 1;
    }

    if (iter->tag_idx < iter->tag_num) {
        iter->tag_idx++;
        return 1;
    }

    if (iter->curr_id == iter->max_id) { // iter end
        return 0;
    }

    // next field
    iter->curr_id++;
    cell_iter_free_tag_list(iter);
    fieldstat_get_tag_lists(iter->instance, iter->curr_id, &iter->tag_list, &iter->tag_num);
    kv_pair_sort(iter->tag_list->tag, iter->tag_num);

    iter->tag_idx = 0;
    return 1;
}

enum field_type cell_iter_get_type(struct cell_iter *iter) {
    return fieldstat_get_field_type(iter->instance, iter->curr_id);
}

int cell_iter_get_id(struct cell_iter *iter) {
    return iter->curr_id;
}

const char *cell_iter_get_name(struct cell_iter *iter) {
    return fieldstat_get_field_name(iter->instance, iter->curr_id);
}

struct fieldstat_tag_list *cell_iter_get_tag_list(struct cell_iter *iter) {
    return &iter->tag_list[iter->tag_idx];
}

// TODO: TOPK 的iter

/* -------------------------------------------------------------------------- */
/*                                   export                                   */
/* -------------------------------------------------------------------------- */

Export_kv_pair *cell_query_with_iter(struct cell_iter *iter) {
    Export_kv_pair *ret = malloc(sizeof(Export_kv_pair));
    size_t len_dummy;

    ret->key = strdup(cell_iter_get_name(iter));

    switch (cell_iter_get_type(iter)) {
        case FIELD_TYPE_COUNTER:
            ret->type = TAG_INTEGER;
            ret->value_int = fieldstat_counter_get(iter->instance, cell_iter_get_id(iter), cell_iter_get_tag_list(iter));
            break;
        case FIELD_TYPE_CARDINALITY:
        case FIELD_TYPE_HISTOGRAM:
            ret->type = TAG_CSTRING;
            fieldstat_get_serialized_blob(iter->instance, cell_iter_get_id(iter), cell_iter_get_tag_list(iter), (char **)&ret->value_str, &len_dummy);
            break;
        default:
            assert(0);
    }
    return ret;
}

void kv_pair_add_to_cjson(Export_kv_pair *pairs, cJSON *cjson_pair)
{
    switch (pairs->type)
    {
        case TAG_INTEGER:
            cJSON_AddNumberToObject(cjson_pair, pairs->key, pairs->value_int);
            break;
        case TAG_DOUBLE:
            cJSON_AddNumberToObject(cjson_pair, pairs->key, pairs->value_double);
            break;
        case TAG_CSTRING:
            cJSON_AddStringToObject(cjson_pair, pairs->key, pairs->value_str);
            break;
        default:
            break;
    }
}

struct cjson_map {
    char *key; // use cjson string as key
    size_t key_len;

    cJSON *cjson_tags;
    cJSON *cjson_fields;

    UT_hash_handle hh;
};

struct cjson_map *cjson_map_find(struct cjson_map *head, char *key, size_t key_len)
{
    struct cjson_map *s;
    HASH_FIND(hh, head, key, key_len, s);
    return s;
}

void cjson_map_add_new_pair_for_one_loop(struct cjson_map *map_head, struct cell_iter *iter)
{
    struct fieldstat_tag_list *tag_list = cell_iter_get_tag_list(iter);
    size_t tag_list_len = tag_list->n_tag;

    // parse tags to get a key
    // different tag order will cause different cjson string, so sort it first.
    // Because the key in fieldstat is ordered, sorting in the origin place will change the key we use to query.
    // In order to avoid this, we copy the tag list before sorting. Note that such copy is shallow copy for strings.
    cJSON *cjson_tags = cJSON_CreateObject();
    Export_kv_pair *tag_list_copy = malloc(sizeof(Export_kv_pair) * tag_list_len);
    memcpy(tag_list_copy, tag_list->tag, sizeof(Export_kv_pair) * tag_list_len);
    kv_pair_sort(tag_list_copy, tag_list_len);

    // add tags to cjson
    for (int i = 0; i < tag_list_len; i++) {
        kv_pair_add_to_cjson(&tag_list_copy[i], cjson_tags);
    }

    char *key = cJSON_PrintUnformatted(cjson_tags);
    size_t key_len = strlen(key);
    struct cjson_map *map = cjson_map_find(map_head, key, key_len);
    if (map == NULL) {
        map = malloc(sizeof(struct cjson_map));
        map->key = key;
        map->key_len = key_len;
        map->cjson_tags = cjson_tags;
        map->cjson_fields = cJSON_CreateObject();
        HASH_ADD_KEYPTR(hh, map_head, map->key, map->key_len, map);

        // the key will be freed when the map is freed
    } else {
        cJSON_Delete(cjson_tags);
        free(key);
    }

    Export_kv_pair *field = cell_query_with_iter(iter);
    kv_pair_add_to_cjson(field, map->cjson_fields);

    kv_pair_free(field);
    free(tag_list_copy); // shallow copy, so we only need to free the pointer itself
}

void cjson_map_free(struct cjson_map *head)
{
    struct cjson_map *current, *tmp;
    HASH_ITER(hh, head, current, tmp) {
        HASH_DEL(head, current);
        cJSON_Delete(current->cjson_tags);
        cJSON_Delete(current->cjson_fields);
        free(current->key);
        free(current);
    }
}

/*
    {
        "tags": {
            "device_id":<>
            "IP":<>
            ...
        },
        "fields":{
            "c2s_flow":<int counter value>
            "in_lantency":<blob of histogram>
            "client_ip_sketch":<blob of hyperloglog>
        }
        "timestamp":<timestamp long unsigned>
    }
*/
cJSON *fieldstat_export_instance_to_cjson(struct fieldstat *instance)
{
    struct cjson_map *map_head = NULL; // new a hash head
    struct cell_iter *iter = cell_iter_new(instance);
    while (cell_iter_next(iter)) {
        cjson_map_add_new_pair_for_one_loop(map_head, iter);
    }
    cell_iter_free(iter);

    cJSON *out_array = cJSON_CreateArray();
    cJSON *cjson_root;
    struct cjson_map *current, *tmp;
    HASH_ITER(hh, map_head, current, tmp) {
        cjson_root = cJSON_CreateObject();

        // TODO: more tags
        cJSON_AddItemToObject(cjson_root, "tags", current->cjson_tags);
        cJSON_AddItemToObject(cjson_root, "fields", current->cjson_fields);
        // FIXME: 实际上没有name这一项，name 就是field 的key
        // TODO: timestamp

        cJSON_AddItemToArray(out_array, cjson_root);
    }

    cjson_map_free(map_head);

    return out_array;
}

void fieldstat_export_to_json_arr(struct fieldstat *instance, char ***out, size_t *n_out)
{
    cJSON *cjson = Export_instance_with_cjson(instance);
    // parse every cjson object
    int n = cJSON_GetArraySize(cjson);
    *out = malloc(sizeof(char *) * n);
    for (int i = 0; i < n; i++) {
        cJSON *cjson_obj = cJSON_GetArrayItem(cjson, i);
        (*out)[i] = cJSON_PrintUnformatted(cjson_obj);
    }

    *n_out = n;
    cJSON_Delete(cjson);
}