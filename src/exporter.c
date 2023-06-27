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

struct cjson_map {
    char *key; // use cjson string as key
    size_t key_len;

    cJSON *cjson_tags;
    cJSON *cjson_fields;

    UT_hash_handle hh;
};


void kv_pair_free(Export_kv_pair *pair) {
    if (pair->type == TAG_CSTRING) {
        free((char *)pair->value_str);
    }
    free((char *)pair->key);
    free(pair);
}

int my_cmp_kv_pair(const void *a, const void *b)
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
    qsort(tag_list, arr_length, sizeof(Export_kv_pair), my_cmp_kv_pair);
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

    if (iter->tag_idx < iter->tag_num - 1) {
        iter->tag_idx++;
        return 1;
    }

    if (iter->curr_id == iter->max_id) { // iter end
        return 0;
    }
    printf("cell_iter_next new field: %d\n", iter->curr_id + 1);
    // next field
    iter->curr_id++;
    cell_iter_free_tag_list(iter);
    printf("cell_iter_free_tag_list done\n");
    fieldstat_get_tag_lists(iter->instance, iter->curr_id, &iter->tag_list, &iter->tag_num);

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

    printf("cell_query_with_iter enter, type: %d, key: %s \n", cell_iter_get_type(iter), ret->key);

    switch (cell_iter_get_type(iter)) {
        case FIELD_TYPE_COUNTER:
            ret->type = TAG_INTEGER;
            ret->value_int = fieldstat_counter_get(iter->instance, cell_iter_get_id(iter), cell_iter_get_tag_list(iter));
            printf("cell_query_with_iter counter value: %lld\n", ret->value_int);
            break;
        case FIELD_TYPE_CARDINALITY:
        case FIELD_TYPE_HISTOGRAM:
            ret->type = TAG_CSTRING;
            fieldstat_get_serialized_blob(iter->instance, cell_iter_get_id(iter), cell_iter_get_tag_list(iter), (char **)&ret->value_str, &len_dummy);
            printf("cell_query_with_iter blob: %s\n", ret->value_str);
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
            printf("kv_pair_add_to_cjson, int: key: %s, value: %lld\n", pairs->key, pairs->value_int);
            cJSON_AddNumberToObject(cjson_pair, pairs->key, pairs->value_int);
            break;
        case TAG_DOUBLE:
            cJSON_AddNumberToObject(cjson_pair, pairs->key, pairs->value_double);
            break;
        case TAG_CSTRING:
            printf("kv_pair_add_to_cjson, string: key: %s, value: %s\n", pairs->key, pairs->value_str);

            cJSON_AddStringToObject(cjson_pair, pairs->key, pairs->value_str);
            break;
        default:
            break;
    }
}

struct cjson_map *cjson_map_find(struct cjson_map *head, char *key, size_t key_len)
{
    struct cjson_map *s;
    HASH_FIND(hh, head, key, key_len, s);
    return s;
}

cJSON *tag_list_to_tag_object(const struct fieldstat_tag_list *tag_list)
{
    cJSON *cjson_tags = cJSON_CreateObject();
    size_t tag_list_len = tag_list->n_tag;

    // different tag order will cause different cjson string, so sort it first.
    // Because the key in fieldstat is ordered, sorting in the origin place will change the key we use to query.
    // In order to avoid this, we copy the tag list before sorting. Note that such copy is shallow copy for strings.
    Export_kv_pair *tag_list_copy = malloc(sizeof(Export_kv_pair) * tag_list_len);
    memcpy(tag_list_copy, tag_list->tag, sizeof(Export_kv_pair) * tag_list_len);
    kv_pair_sort(tag_list_copy, tag_list_len);

    for (int i = 0; i < tag_list_len; i++) {
        kv_pair_add_to_cjson(&tag_list_copy[i], cjson_tags);
    }

    free(tag_list_copy); // shallow copy, so we only need to free the pointer itself

    return cjson_tags;
}

void cjson_map_add_new_pair_for_one_loop(struct cjson_map **map_head_in, struct cell_iter *iter)
{
    struct cjson_map *map_head = *map_head_in;
    printf("cjson_map_add_new_pair_for_one_loop enter\n");
    struct fieldstat_tag_list *tag_list = cell_iter_get_tag_list(iter);

    cJSON *cjson_tags = tag_list_to_tag_object(tag_list);

    char *key = cJSON_PrintUnformatted(cjson_tags);
    printf("tagkey: %s\n", key);
    size_t key_len = strlen(key);

    struct cjson_map *map = cjson_map_find(map_head, key, key_len);
    if (map == NULL) {
        map = malloc(sizeof(struct cjson_map));
        map->key = key;
        map->key_len = key_len;
        map->cjson_tags = cjson_tags;
        map->cjson_fields = cJSON_CreateObject();
        HASH_ADD_KEYPTR(hh, map_head, map->key, map->key_len, map);

        // the key and cjson_tags will be freed when the map is freed
    } else {
        cJSON_Delete(cjson_tags);
        free(key);
    }

    Export_kv_pair *field = cell_query_with_iter(iter);
    kv_pair_add_to_cjson(field, map->cjson_fields);
    kv_pair_free(field);

    *map_head_in = map_head;
    printf("cjson_map_add_new_pair_for_one_loop end\n");
}

struct cjson_map *fieldstat_hash_tag_fields_to_map(struct fieldstat *instance)
{
    printf("fieldstat_export_instance_to_cjson enter\n");
    struct cjson_map *map_head = NULL; // new a hash head
    struct cell_iter *iter = cell_iter_new(instance);
    printf("cell_iter_new done\n");
    while (cell_iter_next(iter)) {
        cjson_map_add_new_pair_for_one_loop(&map_head, iter);
    }
    printf("cell_iter done\n");
    cell_iter_free(iter);

    return map_head;
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
void fieldstat_export_to_json_arr(struct fieldstat *instance, char ***out, size_t *n_out)
{
    struct cjson_map *map_head = fieldstat_hash_tag_fields_to_map(instance);

    int n = HASH_COUNT(map_head);
    *out = malloc(sizeof(char *) * n);
    *n_out = n;

    cJSON *cjson_root;
    struct cjson_map *current, *tmp;
    size_t i = 0;
    HASH_ITER(hh, map_head, current, tmp) {
        cjson_root = cJSON_CreateObject();

        // TODO: more tags
        cJSON_AddItemToObject(cjson_root, "tags", current->cjson_tags);
        cJSON_AddItemToObject(cjson_root, "fields", current->cjson_fields);
        // FIXME: 实际上没有name这一项，name 就是field 的key
        // TODO: timestamp
        char *cjson_str = cJSON_PrintUnformatted(cjson_root);
        (*out)[i++] = cjson_str;
        cJSON_Delete(cjson_root);
    }
    printf("fieldstat_export_to_json_arr done\n");

    HASH_ITER(hh, map_head, current, tmp) {
        HASH_DEL(map_head, current);
        free(current->key);
        free(current);

        // cjson object will be freed with cjson root
    }
}
