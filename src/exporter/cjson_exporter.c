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

enum iter_state_enum {
    ITER_STATE_INIT,

    ITER_STATE_TAG_MOVING,
    ITER_STATE_TOPK_MOVING,
};

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
		long long value_int;
		double value_double;
		char *value_str;
	};
};

struct fieldstat_json_exporter {
    const struct fieldstat *instance;
    char *name;
    struct fieldstat_tag_list *global_tag_list;
};

struct topk_iter {
    size_t result_n;
    int curr_idx;
    char **result_key;

    char **metric_names;
    size_t n_metric;
    unsigned int **metric_values;
};

struct cell_iter {
    int field_num;
    int curr_id;
    struct fieldstat_tag_list *tag_list;
    size_t tag_num;
    size_t tag_idx;
    const struct fieldstat *instance;
    struct topk_iter *topk_iter;
    enum iter_state_enum state;
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

long get_current_time()
{
    time_t currentTime;
    time(&currentTime);

    return currentTime;
}

int my_cmp_kv_pair(const void *a, const void *b)
{
    struct export_kv_pair *aa = (struct export_kv_pair *)a;
    struct export_kv_pair *bb = (struct export_kv_pair *)b;
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

void kv_pair_sort(struct export_kv_pair *tag_list, size_t arr_length)
{
    qsort(tag_list, arr_length, sizeof(struct export_kv_pair), my_cmp_kv_pair);
}

/* -------------------------------------------------------------------------- */
/*                                    iter                                    */
/* -------------------------------------------------------------------------- */
struct cell_iter *cell_iter_new(const struct fieldstat *instance) {
    struct cell_iter *iter = malloc(sizeof(struct cell_iter));
    iter->field_num = fieldstat_get_max_field_id(instance) + 1; // at least 1, because instance has been ensured to have at least 1 field
    iter->curr_id = 0;
    iter->tag_list = NULL;
    iter->tag_num = 0;
    iter->tag_idx = 0;
    iter->instance = instance;
    iter->topk_iter = NULL;
    iter->state = ITER_STATE_INIT;
    return iter;
}

void cell_iter_free_tag_list(struct cell_iter *iter) {
    for (int i = 0; i < iter->tag_num; i++){
        free(iter->tag_list[i].tag);
    }
    free(iter->tag_list);
    iter->tag_list = NULL;
    iter->tag_num = 0;
}

void cell_iter_free_topk_iter(struct cell_iter *iter)
{
    struct topk_iter *it = iter->topk_iter;
    if (it == NULL) {
        return;
    }
    for (int i = 0; i < it->result_n; i++) {
        free(it->result_key[i]);
        free(it->metric_values[i]);
    }
    for (int j = 0; j < it->n_metric; j++) {
        free(it->metric_names[j]);
    }
    free(it->result_key);
    free(it->metric_names);
    free(it->metric_values);

    free(iter->topk_iter);
    iter->topk_iter = NULL;
}

void cell_iter_free(struct cell_iter *iter) {
    if (iter->tag_list) {
        cell_iter_free_tag_list(iter);
    }
    cell_iter_free_topk_iter(iter);

    free(iter);
}

enum field_type cell_iter_get_type(const struct cell_iter *iter) {
    return fieldstat_get_field_type(iter->instance, iter->curr_id);
}

const char *cell_iter_get_name(const struct cell_iter *iter) {
    return fieldstat_get_field_name(iter->instance, iter->curr_id);
}

struct fieldstat_tag_list *cell_iter_get_tag_list(const struct cell_iter *iter) {
    return &iter->tag_list[iter->tag_idx];
}

void cell_iter_step_into_topk(struct cell_iter *iter) {
    iter->topk_iter = (struct topk_iter *)malloc(sizeof(struct topk_iter));
    memset(iter->topk_iter, 0, sizeof(struct topk_iter));
    size_t primary_metric_id_dummy;

    if (iter->tag_list == NULL) {
        fieldstat_get_tag_lists(iter->instance, iter->curr_id, &iter->tag_list, &iter->tag_num);
        iter->tag_idx = 0;
    }

    fieldstat_topk_query(iter->instance, iter->curr_id, cell_iter_get_tag_list(iter), 
        &iter->topk_iter->result_key, &iter->topk_iter->result_n,
        &iter->topk_iter->metric_names, &iter->topk_iter->n_metric, &primary_metric_id_dummy,
        &iter->topk_iter->metric_values);

    iter->state = ITER_STATE_TOPK_MOVING;
}

// return 0 if iter reaches end, 1 if not
int cell_iter_next_nonempty_field(struct cell_iter *iter) {
    while (iter->curr_id < iter->field_num - 1) {
        iter->curr_id++;

        fieldstat_get_tag_lists(iter->instance, iter->curr_id, &iter->tag_list, &iter->tag_num);
        if (iter->tag_list == NULL) {
            continue;
        }
        iter->tag_idx = 0;

        if (fieldstat_get_field_type(iter->instance, iter->curr_id) == FIELD_TYPE_TOPK) {
            // here we do not need to worry if topk is empty, because when we find tag, it is ensured that topk_add occurred at least once
            cell_iter_step_into_topk(iter);

            printf("goto topk, id: %d\n", iter->curr_id);
            return 1;
        }
        iter->state = ITER_STATE_TAG_MOVING;
        printf("cell_iter_next new field: %d\n", iter->curr_id);

        return 1;
    }

    return 0;
}

int cell_iter_next_tag(struct cell_iter *iter) {
    if (iter->tag_idx < iter->tag_num - 1) {
        iter->tag_idx++;
        printf("cell_iter_next move to new tag, key: %s\n", iter->tag_list[iter->tag_idx].tag->key);
        return 1;
    }
    return 0;
}

int cell_iter_next_topk(struct cell_iter *iter) {
    if (iter->topk_iter->curr_idx < iter->topk_iter->result_n - 1) {
        iter->topk_iter->curr_idx++;
        return 1;
    }
    return 0;
}

int cell_iter_next(struct cell_iter *iter) {
    printf("cell_iter_next enter, current id: %d, current tag id: %zu\n", iter->curr_id, iter->tag_idx);
    if (iter->state == ITER_STATE_INIT) {
        iter->curr_id = -1;
        return cell_iter_next_nonempty_field(iter);
    }

    if (iter->state == ITER_STATE_TAG_MOVING) {
        if (cell_iter_next_tag(iter)) {
            return 1;
        }
    } else if (iter->state == ITER_STATE_TOPK_MOVING) {
        if (cell_iter_next_topk(iter)) {
            return 1;
        }
        if (cell_iter_next_tag(iter)) {
            cell_iter_free_topk_iter(iter);
            cell_iter_step_into_topk(iter);
            return 1;
        }
    } else {
        assert(0);
    }

    cell_iter_free_tag_list(iter);
    cell_iter_free_topk_iter(iter);
    return cell_iter_next_nonempty_field(iter);
}

/* -------------------------------------------------------------------------- */
/*                                   export                                   */
/* -------------------------------------------------------------------------- */

struct export_kv_pair *cell_query_with_iter(const struct cell_iter *iter) {
    struct export_kv_pair *ret = malloc(sizeof(struct export_kv_pair));
    size_t len_dummy;

    ret->key = strdup(cell_iter_get_name(iter));

    printf("cell_query_with_iter enter, type: %d, key: %s \n", cell_iter_get_type(iter), ret->key);

    switch (cell_iter_get_type(iter)) {
        case FIELD_TYPE_COUNTER:
            ret->type = TAG_INTEGER;
            ret->value_int = fieldstat_counter_get(iter->instance, iter->curr_id, cell_iter_get_tag_list(iter));
            break;
        case FIELD_TYPE_HLL:
        case FIELD_TYPE_HISTOGRAM:
            ret->type = TAG_CSTRING;
            fieldstat_get_serialized_blob(iter->instance, iter->curr_id, cell_iter_get_tag_list(iter), (char **)&ret->value_str, &len_dummy);
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
                pairs.value_int = tag_list->tag[i].value_int;
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

void cell_iter_get_topk_result_entry(struct cell_iter *iter, struct export_kv_pair *pairs)
{
    pairs->key = strdup(cell_iter_get_name(iter));
    pairs->type = TAG_CSTRING;
    pairs->value_str = strdup(iter->topk_iter->result_key[iter->topk_iter->curr_idx]);
}

void cell_iter_read_tag_list(struct cell_iter *iter, struct export_kv_pair **exported_tags, size_t *len_out)
{
    struct fieldstat_tag_list *tag_list_src = cell_iter_get_tag_list(iter);
    size_t ret_tag_list_len = tag_list_src->n_tag;

    if (iter->state == ITER_STATE_TOPK_MOVING) {
        ret_tag_list_len++;
    }

    struct export_kv_pair *tag_list = malloc(sizeof(struct export_kv_pair) * ret_tag_list_len);
    *exported_tags = tag_list;
    *len_out = ret_tag_list_len;

    for (int i = 0; i < tag_list_src->n_tag; i++) {
        tag_list[i].key = strdup(tag_list_src->tag[i].key);
        tag_list[i].type = tag_list_src->tag[i].type;
        switch (tag_list[i].type)
        {
            case TAG_INTEGER:
                tag_list[i].value_int = tag_list_src->tag[i].value_int;
                break;
            case TAG_DOUBLE:
                tag_list[i].value_double = tag_list_src->tag[i].value_double;
                break;
            case TAG_CSTRING:
                tag_list[i].value_str = strdup(tag_list_src->tag[i].value_str);
                break;
            default:
                break;
        }
    }

    if (iter->state == ITER_STATE_TOPK_MOVING) {
        cell_iter_get_topk_result_entry(iter, &tag_list[ret_tag_list_len - 1]);
    }
}

struct export_kv_pair *cell_topk_query_with_iter_at_index(const struct topk_iter *iter, size_t field_id)
{
    struct export_kv_pair *ret = malloc(sizeof(struct export_kv_pair));

    ret->key = strdup(iter->metric_names[field_id]);
    ret->type = TAG_INTEGER;
    ret->value_int = iter->metric_values[iter->curr_idx][field_id];

    return ret;
}

void cell_iter_add_field_to_field_object(const struct cell_iter *iter, cJSON *obj)
{
    if (cell_iter_get_type(iter) == FIELD_TYPE_TOPK) {
        for (int i = 0; i < iter->topk_iter->n_metric; i++) {
            struct export_kv_pair *field = cell_topk_query_with_iter_at_index(iter->topk_iter, i);
            kv_pair_add_to_cjson(field, obj);
            kv_pair_free(field);
        }
        return;
    }

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
    printf("cjson_map_add_new_pair_for_one_loop enter\n");
    struct export_kv_pair *tag_list;
    size_t tag_list_len;

    cell_iter_read_tag_list(iter, &tag_list, &tag_list_len);
    // different tag order will cause different cjson string, so sort it first.
    kv_pair_sort(tag_list, tag_list_len);
    cJSON *cjson_tags = cJSON_CreateObject(); // when this cjson_tags is added to a new map entry, it is freed when the map is freed
    for (int i = 0; i < tag_list_len; i++) {
        kv_pair_add_to_cjson(&tag_list[i], cjson_tags);
    }
    kv_pair_free_list(tag_list, tag_list_len);

    char *key = cJSON_PrintUnformatted(cjson_tags);  // when this key is added to a new map entry, it is freed when the map is freed
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
    } else {
        cJSON_Delete(cjson_tags);
        free(key);
    }

    cell_iter_add_field_to_field_object(iter, map->cjson_fields);

    *map_head_in = map_head;
    printf("cjson_map_add_new_pair_for_one_loop end\n");
}

struct cjson_map *hash_map_read_tag_and_field(const struct fieldstat *instance)
{
    printf("hash_map_read_tag_and_field enter\n");
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
    if (fieldstat_get_max_field_id(instance) == -1) {
        return NULL;
    }

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
        memcpy((char *)tag->key, tag_list[i].key, MAX_TAG_KEY_LENGTH);
        tag->type = tag_list[i].type;
        switch (tag->type)
        {
        case TAG_INTEGER:
            tag->value_int = tag_list[i].value_int;
            break;
        case TAG_CSTRING:
            memcpy((char *)tag->value_str, tag_list[i].value_str, MAX_TAG_STRING_VAL_LENGTH);
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
        free(exporter->global_tag_list->tag);
        free(exporter->global_tag_list);
    }
    free(exporter);
}
