#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <sys/time.h>
#include <time.h>
#include <stddef.h>

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

struct export_kv_pair {
	char *key;
    enum fs_tag_type type;
	union{
		long long value_longlong;
		double value_double;
		char *value_str;
	};
};

struct fieldstat_json_exporter {
    const struct fieldstat *instance;
    char *name;
    struct fieldstat_tag_list *global_tag_list;
};

struct cell_iter {
    int *cube_ids;
    int n_cube;
    int curr_cube_idx;
    struct fieldstat_tag_list *shared_tag;

    int max_metric_id;
    int curr_metric_id;

    struct fieldstat_tag_list *tag_list;
    int *cell_ids;
    int n_cell;
    int curr_cell_idx;

    const struct fieldstat *instance;
};

struct cjson_map {
    char *key; // use cjson string as key
    size_t key_len;

    cJSON *cjson_tags;
    cJSON *cjson_fields;

    UT_hash_handle hh;
};

void kv_pair_free(struct export_kv_pair *pair) {
    if (pair->type == TAG_CSTRING) {
        free((char *)pair->value_str);
    }
    free((char *)pair->key);
    free(pair);
}

void kv_pair_fill_with_tags(struct export_kv_pair *dest, const struct fieldstat_tag *src)
{
    dest->key = strdup(src->key);
    dest->type = src->type;
    switch (src->type) {
        case TAG_INTEGER:
            dest->value_longlong = src->value_longlong;
            break;
        case TAG_DOUBLE:
            dest->value_double = src->value_double;
            break;
        case TAG_CSTRING:
            dest->value_str = strdup(src->value_str);
            break;
        default:
            assert(0);
    }
}

long get_current_time()
{
    time_t currentTime;
    time(&currentTime);

    return currentTime;
}

/* -------------------------------------------------------------------------- */
/*                                    iter                                    */
/* -------------------------------------------------------------------------- */
struct cell_iter *cell_iter_new(const struct fieldstat *instance) {
    struct cell_iter *iter = malloc(sizeof(struct cell_iter));
    memset(iter, 0, sizeof(struct cell_iter));
    iter->instance = instance;
    fieldstat_get_cubes(instance, &iter->cube_ids, &iter->n_cube);

    iter->curr_cube_idx = -1;
    iter->curr_metric_id = -1;

    return iter;
}

void cell_iter_free_cell_records(struct cell_iter *iter) {
    fieldstat_tag_list_arr_free(iter->tag_list, iter->n_cell);
    iter->tag_list = NULL;
    iter->n_cell = 0;
    free(iter->cell_ids);
    iter->cell_ids = NULL;
    iter->curr_cell_idx = 0;
}

void cell_iter_free(struct cell_iter *iter) {
    cell_iter_free_cell_records(iter);

    if (iter->shared_tag != NULL) {
        fieldstat_tag_list_arr_free(iter->shared_tag, 1);
    }
    free(iter->cube_ids);

    free(iter);
}

enum metric_type cell_iter_get_type(const struct cell_iter *iter) {
    return fieldstat_get_metric_type(iter->instance, iter->cube_ids[iter->curr_cube_idx], iter->curr_metric_id);
}

const char *cell_iter_get_name(const struct cell_iter *iter) {
    return fieldstat_get_metric_name(iter->instance, iter->cube_ids[iter->curr_cube_idx], iter->curr_metric_id);
}

int cell_iter_next_nonempty_cube(struct cell_iter *iter) {
    const struct fieldstat *instance = iter->instance;
    while (iter->curr_cube_idx < iter->n_cube - 1) {
        int cube_id_next = iter->cube_ids[++iter->curr_cube_idx];

        int metric_id_max_next = fieldstat_get_max_metric_id(instance, cube_id_next);
        if (metric_id_max_next == -1) {
            continue;
        }

        iter->max_metric_id = metric_id_max_next;
        iter->curr_metric_id = -1;

        return 1;
    }

    return 0;
}

int cell_iter_next_cell(struct cell_iter *iter) {
    if (iter->curr_cell_idx < iter->n_cell - 1) {
        iter->curr_cell_idx++;
        return 1;
    }
    return 0;
}

int cell_iter_next_nonempty_metric(struct cell_iter *iter) {
    const struct fieldstat *instance = iter->instance;
    while (iter->curr_metric_id < iter->max_metric_id) {
        int metric_id_next = ++iter->curr_metric_id;
        int cube_id = iter->cube_ids[iter->curr_cube_idx];

        fieldstat_get_cells(instance, cube_id, metric_id_next, &iter->cell_ids, &iter->tag_list, (size_t *)&iter->n_cell);
        if (iter->n_cell == 0) {
            continue;
        }

        iter->curr_cell_idx = 0;
        return 1;
    }

    return 0;
}

int cell_iter_next(struct cell_iter *iter) {
    if (iter->curr_cube_idx == -1) { // first time
        while (cell_iter_next_nonempty_cube(iter)) {
            if (cell_iter_next_nonempty_metric(iter)) {
                iter->shared_tag = fieldstat_get_shared_tags(iter->instance, iter->cube_ids[iter->curr_cube_idx]);
                return 1;
            }
        }
        return 0;
    }

    if (cell_iter_next_cell(iter)) {
        return 1;
    }

    cell_iter_free_cell_records(iter);
    if (cell_iter_next_nonempty_metric(iter)) {
        return 1;
    }

    fieldstat_tag_list_arr_free(iter->shared_tag, 1);
    printf("free shared tag for cube id: %d\n", iter->cube_ids[iter->curr_cube_idx]);
    iter->shared_tag = NULL;
    while (cell_iter_next_nonempty_cube(iter)) {
        if (cell_iter_next_nonempty_metric(iter)) {
            iter->shared_tag = fieldstat_get_shared_tags(iter->instance, iter->cube_ids[iter->curr_cube_idx]);
            return 1;
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/*                                   export                                   */
/* -------------------------------------------------------------------------- */

struct export_kv_pair *cell_query_with_iter(const struct cell_iter *iter) {
    struct export_kv_pair *ret = malloc(sizeof(struct export_kv_pair));
    size_t len_dummy;

    ret->key = strdup(cell_iter_get_name(iter));

    switch (cell_iter_get_type(iter)) {
        case METRIC_TYPE_COUNTER:
            ret->type = TAG_INTEGER;
            ret->value_longlong = fieldstat_counter_get(iter->instance, iter->cube_ids[iter->curr_cube_idx], iter->curr_metric_id, iter->cell_ids[iter->curr_cell_idx]);
            break;
        case METRIC_TYPE_HLL:
        case METRIC_TYPE_HISTOGRAM:
            ret->type = TAG_CSTRING;
            fieldstat_get_serialized_blob(iter->instance, iter->cube_ids[iter->curr_cube_idx], iter->curr_metric_id, iter->cell_ids[iter->curr_cell_idx],
                (char **)&ret->value_str, &len_dummy);
            break;

        default:
            assert(0);
    }
    return ret;
}

void kv_pair_add_to_cjson(const struct export_kv_pair *pairs, cJSON *cjson_pair)
{
    switch (pairs->type)
    {
        case TAG_INTEGER:
            cJSON_AddNumberToObject(cjson_pair, pairs->key, pairs->value_longlong);
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

struct cjson_map *cjson_map_find(struct cjson_map *head, char *key, size_t key_len)
{
    struct cjson_map *s;
    HASH_FIND(hh, head, key, key_len, s);
    return s;
}

void tag_list_append_to_tag_object(const struct fieldstat_tag_list *tag_list, cJSON *cjson_tags)
{
    size_t tag_list_len = tag_list->n_tag;
    struct export_kv_pair pairs = {0};

    for (int i = 0; i < tag_list_len; i++) {
        memset(&pairs, 0, sizeof(struct export_kv_pair));
        pairs.key = (char *)tag_list->tag[i].key;
        pairs.type = tag_list->tag[i].type;
        switch (pairs.type)
        {
            case TAG_INTEGER:
                pairs.value_longlong = tag_list->tag[i].value_longlong;
                break;
            case TAG_DOUBLE:
                pairs.value_double = tag_list->tag[i].value_double;
                break;
            case TAG_CSTRING:
                pairs.value_str = (char *)tag_list->tag[i].value_str;
                break;
            default:
                break;
        }
        kv_pair_add_to_cjson(&pairs, cjson_tags);
    }
}

void cell_iter_read_tag_list(struct cell_iter *iter, struct export_kv_pair **exported_tags, size_t *len_out)
{
    struct fieldstat_tag_list *tag_list_cell = &iter->tag_list[iter->curr_cell_idx];
    struct fieldstat_tag_list *tag_arr_shared = iter->shared_tag;

    size_t ret_tag_list_len = tag_list_cell->n_tag + tag_arr_shared->n_tag;

    struct export_kv_pair *tag_list = malloc(sizeof(struct export_kv_pair) * ret_tag_list_len);
    *exported_tags = tag_list;
    *len_out = ret_tag_list_len;

    for (int i = 0; i < tag_list_cell->n_tag; i++) {
        kv_pair_fill_with_tags(tag_list++, &tag_list_cell->tag[i]);
    }
    for (int i = 0; i < tag_arr_shared->n_tag; i++) {
        kv_pair_fill_with_tags(tag_list++, &tag_arr_shared->tag[i]);
    }
}

void cell_iter_add_field_to_field_object(const struct cell_iter *iter, cJSON *obj)
{
    struct export_kv_pair *field = cell_query_with_iter(iter);
    kv_pair_add_to_cjson(field, obj);
    kv_pair_free(field);
}

void kv_pair_free_list(struct export_kv_pair *pairs, size_t len)
{
    for (int i = 0; i < len; i++) {
        struct export_kv_pair *pair = &pairs[i];
        if (pair->type == TAG_CSTRING) {
            free(pair->value_str);
        }
        free(pair->key);
    }
    free(pairs);
}

void cjson_map_add_new_pair_for_one_loop(struct cjson_map **map_head_in, struct cell_iter *iter)
{
    struct cjson_map *map_head = *map_head_in;
    struct export_kv_pair *tag_list;
    size_t tag_list_len;

    cell_iter_read_tag_list(iter, &tag_list, &tag_list_len);
    cJSON *cjson_tags = cJSON_CreateObject(); // when this cjson_tags is added to a new map entry, it is freed when the map is freed
    for (int i = 0; i < tag_list_len; i++) {
        kv_pair_add_to_cjson(&tag_list[i], cjson_tags);
    }
    kv_pair_free_list(tag_list, tag_list_len);

    char *key = cJSON_PrintUnformatted(cjson_tags);  // when this key is added to a new map entry, it is freed when the map is freed
    size_t key_len = strlen(key);

    struct cjson_map *map = cjson_map_find(map_head, key, key_len);
    if (map == NULL) {
        map = malloc(sizeof(struct cjson_map));
        map->key = key;
        map->key_len = key_len;
        map->cjson_tags = cjson_tags;
        map->cjson_fields = cJSON_CreateObject();
        HASH_ADD_KEYPTR(hh, map_head, map->key, map->key_len, map);
    } else {
        cJSON_Delete(cjson_tags);
        free(key);
    }

    cell_iter_add_field_to_field_object(iter, map->cjson_fields);

    *map_head_in = map_head;
}

struct cjson_map *hash_map_read_tag_and_field(const struct fieldstat *instance)
{
    struct cjson_map *map_head = NULL; // new a hash head
    struct cell_iter *iter = cell_iter_new(instance);

    while (cell_iter_next(iter)) {
        cjson_map_add_new_pair_for_one_loop(&map_head, iter);
    }

    cell_iter_free(iter);
    return map_head;
}

void fieldstat_json_exporter_write_global_tags(const struct fieldstat_json_exporter *exporter, struct cjson_map *map_head)
{
    if (exporter->global_tag_list == NULL) {
        return;
    }
    struct cjson_map *current, *tmp;
    HASH_ITER(hh, map_head, current, tmp) {
        tag_list_append_to_tag_object(exporter->global_tag_list, current->cjson_tags);
    }
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
            "in_latency":<blob of histogram>
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
    int *cube_id_dummy;
    int cube_id_len;
    fieldstat_get_cubes(instance, &cube_id_dummy, &cube_id_len);
    if (cube_id_len == 0) {
        return NULL;
    }
    free(cube_id_dummy);

    struct cjson_map *map_head = hash_map_read_tag_and_field(instance);
    if (map_head == NULL) {
        return NULL;
    }

    fieldstat_json_exporter_write_global_tags(exporter, map_head);

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

    HASH_ITER(hh, map_head, current, tmp) {
        HASH_DEL(map_head, current);
        free(current->key);
        free(current);
        // cjson object will be freed with cjson root
    }

    char *out_cjson = NULL;
    if (cJSON_GetArraySize(cjson_root_arr) != 0) {
        out_cjson = cJSON_PrintUnformatted(cjson_root_arr);
    }
    cJSON_Delete(cjson_root_arr);

    return out_cjson;
}

struct fieldstat_json_exporter *fieldstat_json_exporter_new(const struct fieldstat *instance)
{
    struct fieldstat_json_exporter *exporter = (struct fieldstat_json_exporter *)malloc(sizeof(struct fieldstat_json_exporter));
    exporter->instance = instance;
    exporter->name = NULL;
    exporter->global_tag_list = NULL;
    return exporter;
}

void fieldstat_json_exporter_set_global_tag(struct fieldstat_json_exporter *exporter, const struct fieldstat_tag tag_list[], size_t n_tag)
{
    if (exporter->global_tag_list != NULL) {
        free(exporter->global_tag_list->tag);
        free(exporter->global_tag_list);
    }

    exporter->global_tag_list = malloc(sizeof(struct fieldstat_tag_list));
    exporter->global_tag_list->n_tag = n_tag;
    exporter->global_tag_list->tag = malloc(sizeof(struct fieldstat_tag) * n_tag);
    for (size_t i = 0; i < n_tag; i++) {
        struct fieldstat_tag *tag = &exporter->global_tag_list->tag[i];
        tag->key = strdup(tag_list[i].key);
        tag->type = tag_list[i].type;
        switch (tag->type)
        {
        case TAG_INTEGER:
            tag->value_longlong = tag_list[i].value_longlong;
            break;
        case TAG_CSTRING:
            tag->value_str = strdup(tag_list[i].value_str);
            break;
        case TAG_DOUBLE:
            tag->value_double = tag_list[i].value_double;
            break;
        
        default:
            break;
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
    if (exporter->global_tag_list != NULL) {
        fieldstat_tag_list_arr_free(exporter->global_tag_list, 1);
    }
    free(exporter);
}
