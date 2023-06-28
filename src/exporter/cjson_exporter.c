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
        tag_list_clear(&iter->tag_list[i]);
    }
    free(iter->tag_list);
    iter->tag_list = NULL;
    iter->tag_num = 0;
}

void cell_iter_free_topk_iter(struct cell_iter *iter)
{
    if (iter->topk_iter == NULL) {
        return;
    }
    for (int i = 0; i < iter->topk_iter->result_n; i++) {
        free(iter->topk_iter->result_key[i]);        
    }
    free(iter->topk_iter->result_key);
    free(iter->topk_iter->result_count);

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

void cell_iter_step_into_topk(struct cell_iter *iter) {
    iter->topk_iter = (struct topk_iter *)malloc(sizeof(struct topk_iter));
    memset(iter->topk_iter, 0, sizeof(struct topk_iter));
    fieldstat_topk_query(iter->instance, iter->curr_id, iter->tag_list, &iter->topk_iter->result_key, &iter->topk_iter->result_count, &iter->topk_iter->result_n);

    if (iter->tag_list == NULL) {
        fieldstat_get_tag_lists(iter->instance, iter->curr_id, &iter->tag_list, &iter->tag_num);
    }

    iter->state = ITER_STATE_TOPK_MOVING;
}

// return 0 if iter reaches end, 1 if not
int cell_iter_next_nonempty_field(struct cell_iter *iter) {
    while (iter->curr_id < iter->field_num - 1) {
        iter->curr_id++;

        fieldstat_get_tag_lists(iter->instance, iter->curr_id, &iter->tag_list, &iter->tag_num);
        printf("cell_iter_next tag list new, keys:\n");
        for (int i = 0; i < iter->tag_num; i++) {
            printf("%s\n", iter->tag_list[i].tag->key);
        }
        printf("----------------------\n");
        if (iter->tag_list == NULL) {
            continue;
        }
        if (fieldstat_get_field_type(iter->instance, iter->curr_id) == FIELD_TYPE_TOPK) {
            // here we do not need to worry if topk is empty, because when we find tag, it is ensured that topk_add occurred at least once
            cell_iter_step_into_topk(iter);

            printf("goto topk, id: %d\n", iter->curr_id);
            return 1;
        }
        iter->state = ITER_STATE_TAG_MOVING;
        iter->tag_idx = 0;
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
            break;
        case FIELD_TYPE_CARDINALITY:
        case FIELD_TYPE_HISTOGRAM:
            ret->type = TAG_CSTRING;
            fieldstat_get_serialized_blob(iter->instance, cell_iter_get_id(iter), cell_iter_get_tag_list(iter), (char **)&ret->value_str, &len_dummy);
            break;
        case FIELD_TYPE_TOPK: // topk is special, its value is only an entry from its query result
            ret->type = TAG_INTEGER;
            ret->value_int = iter->topk_iter->result_count[iter->topk_iter->curr_idx];
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

    for (int i = 0; i < tag_list_len; i++) {
        kv_pair_add_to_cjson(&tag_list->tag[i], cjson_tags);
    }

    return cjson_tags;
}

void cell_iter_get_topk_result_entry(struct cell_iter *iter, Export_kv_pair *pairs)
{
    pairs->key = strdup(JSON_EXPORTER_TOPK_DEMENSION); // TODO: 应该是一个输出的时候设置的名字，比如统计client IP 的income byte，topk的名字是income byte，每个输入的键值的key是client IP
    pairs->type = TAG_CSTRING;
    pairs->value_str = iter->topk_iter->result_key[iter->topk_iter->curr_idx];
}

void cell_iter_shallow_copy_tag_list(struct cell_iter *iter, struct fieldstat_tag_list *tag_list)
{
    struct fieldstat_tag_list *tag_list_src = cell_iter_get_tag_list(iter);

    size_t out_tag_list_len = tag_list_src->n_tag;
    if (iter->state == ITER_STATE_TOPK_MOVING) {
        out_tag_list_len++;
    }
    tag_list->tag = malloc(sizeof(Export_kv_pair) * out_tag_list_len);
    tag_list->n_tag = out_tag_list_len;

    memcpy(tag_list->tag, tag_list_src->tag, sizeof(Export_kv_pair) * tag_list_src->n_tag);

    if (iter->state == ITER_STATE_TOPK_MOVING) {
        cell_iter_get_topk_result_entry(iter, &tag_list->tag[out_tag_list_len - 1]);
    }
}

void cjson_map_add_new_pair_for_one_loop(struct cjson_map **map_head_in, struct cell_iter *iter)
{
    struct cjson_map *map_head = *map_head_in;
    printf("cjson_map_add_new_pair_for_one_loop enter\n");

    struct fieldstat_tag_list tag_list;

    cell_iter_shallow_copy_tag_list(iter, &tag_list); // copy the tag because we need to sort it
    // different tag order will cause different cjson string, so sort it first.
    kv_pair_sort(tag_list.tag, tag_list.n_tag);
    cJSON *cjson_tags = tag_list_to_tag_object(&tag_list);
    free(tag_list.tag); // shallow copy, so we only need to free the pointer itself

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
    if (fieldstat_get_max_field_id(instance) == -1) {
        return NULL;
    }

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
