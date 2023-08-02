#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <sys/time.h>
#include <time.h>
#include <stddef.h>

#include "cjson/cJSON.h"
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

    struct fieldstat_tag_list *tag_list;
    int *cell_ids;
    size_t n_cell;
    size_t curr_cell_idx;

    int max_metric_id;

    const struct fieldstat *instance;
};

struct cjson_tag_field_pair {
    cJSON *cjson_tags;
    cJSON *cjson_fields;
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

long long get_current_time()
{
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    long long time_stamp_in_ms = current_time.tv_sec * 1000LL + current_time.tv_usec / 1000;

    return time_stamp_in_ms;
}


/* -------------------------------------------------------------------------- */
/*                                    iter                                    */
/* -------------------------------------------------------------------------- */
struct cell_iter *cell_iter_new(const struct fieldstat *instance) {
    struct cell_iter *iter = calloc(1, sizeof(struct cell_iter));
    iter->instance = instance;
    fieldstat_get_cubes(instance, &iter->cube_ids, &iter->n_cube);

    iter->curr_cube_idx = -1;

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

int cell_iter_next_cube(struct cell_iter *iter) {
    const struct fieldstat *instance = iter->instance;
    while (iter->curr_cube_idx < iter->n_cube - 1) {
        int cube_id_next = iter->cube_ids[++iter->curr_cube_idx];

        int metric_id_max_next = fieldstat_get_max_metric_id(instance, cube_id_next);
        if (metric_id_max_next == -1) {
            continue;
        }

        // get cell info 
        fieldstat_cube_read_cell(instance, cube_id_next, &iter->cell_ids, &iter->tag_list, &iter->n_cell);
        if (iter->n_cell == 0) {
            continue;
        }
        iter->max_metric_id = metric_id_max_next;
        iter->shared_tag = fieldstat_get_shared_tags(iter->instance, iter->cube_ids[iter->curr_cube_idx]);
        iter->curr_cell_idx = 0;

        // todo: 现在直接拿一个cell manager 过来，所以没办法很好的判断metric里是不是有cell，要考虑add 了但是metric里没有cell 的情况
        // 但是没必要在这里判断，添加field的时候判断一下就好
        // todo：先添加field，再添加tag

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

int cell_iter_next(struct cell_iter *iter) {
    if (iter->curr_cube_idx == -1) { // first time
        if (cell_iter_next_cube(iter)) {
            return 1;
        }
        return 0;
    }

    if (cell_iter_next_cell(iter)) {
        return 1;
    }

    cell_iter_free_cell_records(iter);
    fieldstat_tag_list_arr_free(iter->shared_tag, 1);
    iter->shared_tag = NULL;
    if (cell_iter_next_cube(iter)) {
        return 1;
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/*                                   export                                   */
/* -------------------------------------------------------------------------- */

struct export_kv_pair *cell_query_with_iter(const struct cell_iter *iter, int metric_id) {
    enum metric_type type = fieldstat_get_metric_type(iter->instance, iter->cube_ids[iter->curr_cube_idx], metric_id);
    struct export_kv_pair *ret = NULL;
    if (type == METRIC_TYPE_COUNTER) {
        long long value = fieldstat_counter_get(iter->instance, iter->cube_ids[iter->curr_cube_idx], metric_id, iter->cell_ids[iter->curr_cell_idx]);
        if (value == -1) {
            return NULL;
        }
        ret = malloc(sizeof(struct export_kv_pair));
        ret->key = strdup(fieldstat_get_metric_name(iter->instance, iter->cube_ids[iter->curr_cube_idx], metric_id));
        ret->type = TAG_INTEGER;
        ret->value_longlong = value;
        return ret;
    }
    if (type == METRIC_TYPE_HLL || type == METRIC_TYPE_HISTOGRAM) {
        char *value;
        size_t len;
        fieldstat_get_serialized_blob(iter->instance, iter->cube_ids[iter->curr_cube_idx], metric_id, iter->cell_ids[iter->curr_cell_idx], &value, &len);
        if (value == NULL) {
            return NULL;
        }
        ret = malloc(sizeof(struct export_kv_pair));
        ret->key = strdup(fieldstat_get_metric_name(iter->instance, iter->cube_ids[iter->curr_cube_idx], metric_id));
        ret->type = TAG_CSTRING;
        ret->value_str = value;
        return ret;
    }
    assert(0);
    return NULL;
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

void cell_iter_read_tag_list(const struct cell_iter *iter, struct export_kv_pair **exported_tags, size_t *len_out)
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

// return 1 if added, 0 if not added
int cjson_map_add(struct cjson_tag_field_pair *tag_field_pair, const struct cell_iter *iter)
{
    cJSON *cjson_fields = NULL;
    for (int i = 0; i <= iter->max_metric_id; i++) {
        struct export_kv_pair *field = cell_query_with_iter(iter, i);
        if (field == NULL) {
            continue;
        }
        if (cjson_fields == NULL) {
            cjson_fields = cJSON_CreateObject();
        }
        kv_pair_add_to_cjson(field, cjson_fields);
        kv_pair_free(field);
    }
    if (cjson_fields == NULL) {
        return 0;
    }
    tag_field_pair->cjson_fields = cjson_fields;

    struct export_kv_pair *tag_list;
    size_t tag_list_len;
    cell_iter_read_tag_list(iter, &tag_list, &tag_list_len);

    cJSON *cjson_tags = cJSON_CreateObject();
    for (int i = 0; i < tag_list_len; i++) {
        kv_pair_add_to_cjson(&tag_list[i], cjson_tags);
    }
    kv_pair_free_list(tag_list, tag_list_len);
    tag_field_pair->cjson_tags = cjson_tags;

    return 1;
}

struct cjson_tag_field_pair *read_tag_and_field(const struct fieldstat *instance, size_t *n_pair_out)
{
    int *cube_id = NULL;
    int cube_id_len = 0;
    fieldstat_get_cubes(instance, &cube_id, &cube_id_len);
    if (cube_id_len == 0) {
        return NULL;
    }

    size_t n_cell_total = 0;
    for (int i = 0; i < cube_id_len; i++) {
        n_cell_total += fieldstat_get_max_cell_num(instance, cube_id[i]);
    }
    free(cube_id);
    if (n_cell_total == 0) {
        return NULL;
    }

    struct cell_iter *iter = cell_iter_new(instance);
    struct cjson_tag_field_pair *tag_field_pair = calloc(n_cell_total, sizeof(struct cjson_tag_field_pair));
    int i = 0;
    while (cell_iter_next(iter)) {
        if (cjson_map_add(&tag_field_pair[i], iter) == 0) {
            continue;
        }
        i++;
    }

    *n_pair_out = i;
    cell_iter_free(iter);
    return tag_field_pair;
}

void fieldstat_json_exporter_write_global_tags(const struct fieldstat_json_exporter *exporter, struct cjson_tag_field_pair *tag_field_pair_arr, size_t arr_len)
{
    if (exporter->global_tag_list == NULL) {
        return;
    }
    
    for (int i = 0; i < arr_len; i++) {
        tag_list_append_to_tag_object(exporter->global_tag_list, tag_field_pair_arr[i].cjson_tags);
    }
}

int add_object_to_json_array_start(char *buf, int buf_len)
{
    memset(buf, 0, buf_len);
    memcpy(buf, "[", 1);
    return 1;
}

int add_object_to_json_array_end(char **buf, int buf_len, int start)
{
    if (start == 1) { // only [
        strcpy(*buf, "[]");
        return 3;
    }
    start = start - 1; // eat the last ','
    int used_len = start + 2; // 1 for ] and 1 for \0
    if (used_len >= buf_len) { 
        *buf = realloc(*buf, used_len);
    }
    strcpy(*buf + start, "]");
    return used_len;
}

/* [{<str>},*/
int add_object_to_json_array(char **buf, int *buf_len, int start, const char *str)
{
    int str_len = strlen(str);
    int used_len = start + str_len + 1; // 1 for ','
    int cur_len = *buf_len;
    char *buf_ptr = *buf;
    if (used_len >= cur_len) {
        // realloc
        while (used_len >= cur_len) {
            cur_len *= 2;
        }
        *buf = realloc(buf_ptr, cur_len);
        *buf_len = cur_len;
    }

    buf_ptr = *buf;
    memcpy(buf_ptr + start, str, str_len);
    buf_ptr[start + str_len] = ',';

    return used_len;
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

    size_t n_pair;
    struct cjson_tag_field_pair *tag_field_pair = read_tag_and_field(instance, &n_pair);
    if (tag_field_pair == NULL) {
        return NULL;
    }
    if (n_pair == 0) {
        free(tag_field_pair);
        return NULL;
    }

    fieldstat_json_exporter_write_global_tags(exporter, tag_field_pair, n_pair);

    int buf_len = 4096;
    char *cjson_str_arr = (char *)malloc(buf_len);
    // cjson is so slow, so we construct the json array manually
    int used_len = add_object_to_json_array_start(cjson_str_arr, buf_len);
    cJSON *cjson_root;
    for (int i = 0; i < n_pair; i++) {
        struct cjson_tag_field_pair *current = &tag_field_pair[i];
        cjson_root = cJSON_CreateObject();

        cJSON_AddStringToObject(cjson_root, "name", exporter->name ? exporter->name : "-");
        cJSON_AddItemToObject(cjson_root, "tags", current->cjson_tags);
        cJSON_AddItemToObject(cjson_root, "fields", current->cjson_fields);
        cJSON_AddNumberToObject(cjson_root, "timestamp", get_current_time());

        char *cjson_str = cJSON_PrintUnformatted(cjson_root);
        cJSON_Delete(cjson_root);
        used_len = add_object_to_json_array(&cjson_str_arr, &buf_len, used_len, cjson_str);
        free(cjson_str);
    }
    add_object_to_json_array_end(&cjson_str_arr, buf_len, used_len);

    free(tag_field_pair);    // cjson object will be freed with cjson root

    return cjson_str_arr;
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
