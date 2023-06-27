#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <sys/time.h>
#include <time.h>

#include "cjson/cJSON.h"
#include "uthash.h"
#include "fieldstat.h"
#include "fieldstat_exporter.h"

/*
    Author : Chenzizhan 
    Date   : 2023-06-27
    Implement struct fieldstat_json_exporter
    An example: 
    [
       {
        "name":"exporter_name",
        "tags":[
            {"device ID":123},
            {"CLIENT IP":"123.1.1.1"}
        ],
        "fields":[
            {"in_bytes":1024},
            {"in_pkts":123},
            {"sessions":<hll blob base64-encoded string>}
        ]
        "timestamp":123456789
       }.
      {
        "name":"exporter_name",
        "tags":[
            {"device ID":123},
            {"CLIENT IP":"1.2.3.4"}
        ],
        "fields":[
            {"in_bytes":1},
            {"in_pkts":2},
        ]
        "timestamp":123456789
       }
    ]
*/

typedef struct fieldstat_tag Export_kv_pair;

struct fieldstat_json_exporter{
    const struct fieldstat *instance;
    char *name;
    Export_kv_pair *global_tag;
    size_t n_global_tag;
};


struct topk_iter {
    size_t result_n;
    int curr_idx;
    char **result_key;
    unsigned int *result_count;
};

struct cell_iter {
    int max_id;
    int curr_id;
    struct fieldstat_tag_list *tag_list;
    size_t tag_num;
    size_t tag_idx;
    const struct fieldstat *instance;
    bool topk_mode;
    struct topk_iter *topk_iter;
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

long get_current_time()
{
	struct timespec this_output_time;

	clock_gettime(CLOCK_MONOTONIC ,&this_output_time);

    return this_output_time.tv_sec;
}


struct fieldstat_json_exporter *fieldstat_json_exporter_new(const struct fieldstat *instance)
{
    struct fieldstat_json_exporter *exporter = (struct fieldstat_json_exporter *)malloc(sizeof(struct fieldstat_json_exporter));
    exporter->instance = instance;
    exporter->name = NULL;
    exporter->global_tag = NULL;
    exporter->n_global_tag = 0;
    return exporter;
}

void fieldstat_json_exporter_set_global_tag(struct fieldstat_json_exporter *exporter, const struct fieldstat_tag tag_list[], size_t n_tag)
{
    if (exporter->global_tag != NULL) {
        for (size_t i = 0; i < exporter->n_global_tag; i++) {
            kv_pair_free(&exporter->global_tag[i]);
        }
        free(exporter->global_tag);
    }

    exporter->global_tag = (Export_kv_pair *)malloc(sizeof(Export_kv_pair) * n_tag);
    exporter->n_global_tag = n_tag;
    for (size_t i = 0; i < n_tag; i++) {
        exporter->global_tag[i].key = strdup(tag_list[i].key);
        exporter->global_tag[i].type = tag_list[i].type;
        switch (tag_list[i].type) {
            case TAG_INTEGER:
                exporter->global_tag[i].value_int = tag_list[i].value_int;
                break;
            case TAG_DOUBLE:
                exporter->global_tag[i].value_double = tag_list[i].value_double;
                break;
            case TAG_CSTRING:
                exporter->global_tag[i].value_str = strdup(tag_list[i].value_str);
                break;
            default:
                assert(0);
        }
    }
}

void fieldstat_json_exporter_set_name(struct fieldstat_json_exporter *exporter, const char *name)
{
    if (exporter->name != NULL) {
        free(exporter->name);
    }
    exporter->name = strdup(name);
}

void fieldstat_json_exporter_free(struct fieldstat_json_exporter *exporter)
{
    if (exporter->name != NULL) {
        free(exporter->name);
    }
    if (exporter->global_tag != NULL) {
        for (size_t i = 0; i < exporter->n_global_tag; i++) {
            kv_pair_free(&exporter->global_tag[i]);
        }
        free(exporter->global_tag);
    }
    free(exporter);
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
struct cell_iter *cell_iter_new(const struct fieldstat *instance) {
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

int cell_iter_find_next_field(struct cell_iter *iter)
{
    if (iter->tag_list == NULL) { // first time
        return 0;
    }
    if (iter->tag_idx < iter->tag_num - 1) {
        return iter->curr_id;
    }

    // next field
    return iter->curr_id + 1;
}

void iter_point_to_topk(struct cell_iter *iter)
{
    iter->topk_iter = (struct topk_iter *)malloc(sizeof(struct topk_iter));
    memset(iter->topk_iter, 0, sizeof(struct topk_iter));

    if (iter->tag_list == NULL) {
        fieldstat_get_tag_lists(iter->instance, iter->curr_id, &iter->tag_list, &iter->tag_num);
    }
    fieldstat_topk_query(iter->instance, iter->curr_id, iter->tag_list, &iter->topk_iter->result_key, &iter->topk_iter->result_count, &iter->topk_iter->result_n);
    
}

void iter_leave_topk(struct cell_iter *iter)
{
    topk_iter_free(iter->topk_iter);
    free(iter->topk_iter);
    iter->topk_iter = NULL;
}

int cell_iter_next_for_topk(struct cell_iter *iter)
{
    if (!iter->topk_mode) {
        iter->curr_id++;
        iter_point_to_topk(iter);
        return 1;
    }
    if (iter->topk_iter->curr_idx == iter->topk_iter->result_n - 1) {
        iter_leave_topk(iter);
        iter->curr_id++;
        iter_point_to_topk(iter);
        return 1;
    }
    iter->topk_iter->curr_idx++;
    return 1;
}

bool cell_iter_check_if_next_is_topk(struct cell_iter *iter, int next_id)
{
    if (iter->topk_mode) {
        if (iter->topk_iter->curr_idx < iter->topk_iter->result_n - 1) {
            return true;
        }
    }

    if (fieldstat_get_field_type(iter->instance, next_id) == FIELD_TYPE_TOPK) {
        return true;
    }
    return false;
}

int cell_iter_next(struct cell_iter *iter) {
    int next_id = cell_iter_find_next_field(iter);
    if (next_id > iter->max_id) {
        return 0;
    }

    if (cell_iter_check_if_next_is_topk(iter, next_id)) {
        printf("cell_iter_next new field: %d is topk\n", iter->curr_id);
        return cell_iter_next_for_topk(iter);
    }


    /* ------------------------ types other than topk ------------------------ */
    if (iter->tag_list == NULL) { // first time
        fieldstat_get_tag_lists(iter->instance, iter->curr_id, &iter->tag_list, &iter->tag_num);
        return 1;
    }

    if (next_id == iter->curr_id) {
        iter->tag_idx++;
        return 1;
    }

    // next field
    iter->curr_id++;
    cell_iter_free_tag_list(iter);

    fieldstat_get_tag_lists(iter->instance, iter->curr_id, &iter->tag_list, &iter->tag_num);

    iter->tag_idx = 0;
    return 1;
}

int cell_iter_next(struct cell_iter *iter) {
    if (iter->topk_mode) {
        return cell_iter_next(iter);
    }
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

    if (fieldstat_get_field_type(iter->instance, iter->curr_id) == FIELD_TYPE_TOPK) {
        printf("cell_iter_next new field: %d is topk\n", iter->curr_id);
        return cell_iter_next(iter);
    }

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

struct cjson_map *fieldstat_hash_tag_fields_to_map(const struct fieldstat *instance)
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
        "name":<exporter name>
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
        "timestamp":<timestamp long>
    }
*/
/*
    Output the fieldstat instance to json string array. User must free the output string.
*/
char *fieldstat_json_exporter_export(const struct fieldstat_json_exporter *exporter)
{
    const struct fieldstat *instance = exporter->instance;
    struct cjson_map *map_head = fieldstat_hash_tag_fields_to_map(instance);

    cJSON *cjson_root_arr = cJSON_CreateArray();

    cJSON *cjson_root;
    struct cjson_map *current, *tmp;
    HASH_ITER(hh, map_head, current, tmp) {
        cjson_root = cJSON_CreateObject();

        cJSON_AddStringToObject(cjson_root, "name", exporter->name ? exporter->name : "-");
        cJSON_AddItemToObject(cjson_root, "tags", current->cjson_tags);
        cJSON_AddItemToObject(cjson_root, "fields", current->cjson_fields);
        cJSON_AddNumberToObject(cjson_root, "timestamp", get_current_time());

        cJSON_AddItemToArray(cjson_root_arr, cjson_root);
    }
    char *out_cjson = cJSON_PrintUnformatted(cjson_root_arr);

    HASH_ITER(hh, map_head, current, tmp) {
        HASH_DEL(map_head, current);
        free(current->key);
        free(current);
        // cjson object will be freed with cjson root
    }
    cJSON_Delete(cjson_root_arr);

    return out_cjson;
}
