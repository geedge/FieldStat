#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <sys/time.h>
#include <time.h>
#include <stddef.h>

#include "utils/very_fast_json_writer.h"
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
        "timestamp_ms":123456789
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
        "timestamp_ms":123456789
       }
    ]
*/
#define DEFAULT_EXPORTER_NAME "-"

struct counter_history;
struct export_kv_pair {
	char *key;
    enum field_type type;
	union{
		long long value_longlong;
		double value_double;
		char *value_str;
	};
};

struct fieldstat_json_exporter {
    char *name;
    struct field_list *global_tag_list;
    struct counter_history *history;
    long long last_ts;
};

struct cell_iter {
    int *cube_ids;
    int n_cube;
    int curr_cube_idx;
    struct field_list *shared_tag;

    struct field_list *tag_list;
    size_t n_cell;
    size_t curr_cell_idx;

    const struct fieldstat *instance;
};

struct cellwise_rec_for_export {
    struct json_writer *cjson_tags;
    struct export_kv_pair **metric_pairs;
    size_t n_metric;
};

struct name_value_map {
    char *name;
    long long value;
    UT_hash_handle hh;
};

struct tag_metric_map {
    char *key;
    struct name_value_map *value;
    UT_hash_handle hh;
};

struct counter_history {
    struct tag_metric_map *rec;
    long long ts;

    char *exporter_name;
    struct field_list *global_tag_list;
};

struct couple_export_table_item {
    char *key;
    int kv_pair_id;
    UT_hash_handle hh;
};

struct couple_export_table {
    struct couple_export_table_item *items;
};


void kv_pair_free(struct export_kv_pair *pair) {
    if (pair->type == FIELD_VALUE_CSTRING) {
        free((char *)pair->value_str);
    }
    free((char *)pair->key);
    free(pair);
}

void kv_pair_fill_with_tags(struct export_kv_pair *dest, const struct field *src)
{
    dest->key = strdup(src->key);
    dest->type = src->type;
    switch (src->type) {
        case FIELD_VALUE_INTEGER:
            dest->value_longlong = src->value_longlong;
            break;
        case FIELD_VALUE_DOUBLE:
            dest->value_double = src->value_double;
            break;
        case FIELD_VALUE_CSTRING:
            dest->value_str = strdup(src->value_str);
            break;
        default:
            assert(0);
    }
}

long long cal_ms_time(const struct timeval *ts)
{
    long long time_stamp_in_ms = ts->tv_sec * 1000LL + ts->tv_usec / 1000;

    return time_stamp_in_ms;
}

/* -------------------------------------------------------------------------- */
/*                                 history rec                                */
/* -------------------------------------------------------------------------- */
struct counter_history *counter_history_new()
{
    struct counter_history *history = calloc(1, sizeof(struct counter_history));
    history->exporter_name = strdup(DEFAULT_EXPORTER_NAME);
    return history;
}

void counter_history_add(struct counter_history *history, const char *json, const char *name, long long value)
{
    struct name_value_map *name_value_node = malloc(sizeof(struct name_value_map));
    name_value_node->name = strdup(name);
    name_value_node->value = value;

    struct tag_metric_map *tag_node = malloc(sizeof(struct tag_metric_map));
    tag_node->key = strdup(json);
    tag_node->value = NULL;

    HASH_ADD_KEYPTR(hh, history->rec, tag_node->key, strlen(tag_node->key), tag_node);
    HASH_ADD_KEYPTR(hh, tag_node->value, name_value_node->name, strlen(name_value_node->name), name_value_node);
}

struct name_value_map *counter_history_find(struct counter_history *history, const char *json, const char *name)
{
    struct tag_metric_map *tag_node;
    HASH_FIND_STR(history->rec, json, tag_node);
    if (tag_node == NULL) {
        return NULL;
    }

    struct name_value_map *name_value_node;
    HASH_FIND_STR(tag_node->value, name, name_value_node);
    return name_value_node;
}

void counter_history_free(struct counter_history *history)
{
    if (history == NULL) {
        return;
    }

    struct tag_metric_map *tag_node, *tmp;
    HASH_ITER(hh, history->rec, tag_node, tmp) {
        struct name_value_map *name_value_node, *tmp2;
        HASH_ITER(hh, tag_node->value, name_value_node, tmp2) {
            HASH_DEL(tag_node->value, name_value_node);
            free(name_value_node->name);
            free(name_value_node);
        }
        HASH_DEL(history->rec, tag_node);
        free(tag_node->key);
        free(tag_node);
    }

    free(history->exporter_name);

    if (history->global_tag_list != NULL) {
        fieldstat_tag_list_arr_free(history->global_tag_list, 1);
    }

    free(history);
}

bool fieldstat_tag_list_cmp(const struct field_list *a, const struct field_list *b)
{
    if (a->n_field != b->n_field) {
        return false;
    }

    for (int i = 0; i < a->n_field; i++) {
        if (strcmp(a->field[i].key, b->field[i].key) != 0) {
            return false;
        }
        if (a->field[i].type != b->field[i].type) {
            return false;
        }
        switch (a->field[i].type)
        {
        case FIELD_VALUE_INTEGER:
            if (a->field[i].value_longlong != b->field[i].value_longlong) {
                return false;
            }
            break;
        case FIELD_VALUE_DOUBLE:
            if (a->field[i].value_double != b->field[i].value_double) {
                return false;
            }
            break;
        case FIELD_VALUE_CSTRING:
            if (strcmp(a->field[i].value_str, b->field[i].value_str) != 0) {
                return false;
            }
            break;
        default:
            assert(0);
        }
    }

    return true;
}

struct field_list *my_copy_fs_tag_list(const struct field_list *src)
{
    struct field_list *dest = malloc(sizeof(struct field_list));
    dest->n_field = src->n_field;
    dest->field = malloc(sizeof(struct field) * dest->n_field);
    for (int i = 0; i < dest->n_field; i++) {
        dest->field[i].key = strdup(src->field[i].key);
        dest->field[i].type = src->field[i].type;
        switch (src->field[i].type)
        {
        case FIELD_VALUE_INTEGER:
            dest->field[i].value_longlong = src->field[i].value_longlong;
            break;
        case FIELD_VALUE_DOUBLE:
            dest->field[i].value_double = src->field[i].value_double;
            break;
        case FIELD_VALUE_CSTRING:
            dest->field[i].value_str = strdup(src->field[i].value_str);
            break;
        default:
            assert(0);
        }
    }

    return dest;
}

bool counter_history_check_if_need_to_update(const struct fieldstat_json_exporter *exporter, const struct fieldstat *instance)
{
    const struct counter_history *history = exporter->history;
    if (history == NULL) {
        return false; // delta export disabled
    }
    if (history->exporter_name == NULL) { // first time
        return true;
    }

    const char *cur_exporter_name = exporter->name ? exporter->name : DEFAULT_EXPORTER_NAME;
    const struct field_list *cur_global_tag_list = exporter->global_tag_list;

    if (strcmp(history->exporter_name, cur_exporter_name) != 0) {
        return true;
    }

    if (history->global_tag_list == NULL && cur_global_tag_list != NULL) {
        return true;
    }
    if (history->global_tag_list == NULL && cur_global_tag_list == NULL) {
        return false;
    }
    // global field cant be deleted, so no need to check if cur_global_tag_list == NULL && history->global_tag_list != NULL
    if (fieldstat_tag_list_cmp(cur_global_tag_list, history->global_tag_list) == false) {
        return true;
    }

    return false;
}

void counter_history_fill_version_info(struct counter_history *history, struct fieldstat_json_exporter *exporter, const struct fieldstat *instance)
{
    const char *cur_exporter_name = exporter->name ? exporter->name : DEFAULT_EXPORTER_NAME;
    const struct field_list *cur_global_tag_list = exporter->global_tag_list;

    free(history->exporter_name);
    history->exporter_name = strdup(cur_exporter_name);
    if (cur_global_tag_list != NULL) {
        history->global_tag_list = my_copy_fs_tag_list(cur_global_tag_list);
    }
}

void fieldstat_json_exporter_update_history(struct fieldstat_json_exporter *exporter, const struct fieldstat *instance)
{
    if (counter_history_check_if_need_to_update(exporter, instance) == false) {
        return;
    }

    counter_history_free(exporter->history);
    exporter->history = counter_history_new();

    counter_history_fill_version_info(exporter->history, exporter, instance);
}

void write_delta_to_json(struct fieldstat_json_exporter *exporter, struct cellwise_rec_for_export *tag_field_pair, struct json_writer *field_json)
{
    // for every tag_field_pair, get the field json string
    const char *tag_json = json_writer_unwrap(tag_field_pair->cjson_tags);
    if (tag_json == NULL) {
        tag_json = "\a\t\a"; // just a dummy string
    }
    for (int j = 0; j < tag_field_pair->n_metric; j++) {
        if (tag_field_pair->metric_pairs[j]->type != FIELD_VALUE_INTEGER) { // only counter type need to write delta
            continue;
        }
        const char *metric_name = tag_field_pair->metric_pairs[j]->key;
        assert(metric_name != NULL); // mute cppcheck
        long long cur_value = tag_field_pair->metric_pairs[j]->value_longlong;
        struct name_value_map *node = counter_history_find(exporter->history, tag_json, metric_name);
        long long last_value = node == NULL ? 0 : node->value;
        long long delta = cur_value - last_value;

        if (node == NULL) { // new added cell/cube/metric
            counter_history_add(exporter->history, tag_json, metric_name, cur_value);
        } else { // previous one
            node->value = cur_value;
        }
        // cell will never delete. If delete cube / cell happen, the exporter history will be reset.

        json_writer_longlong_field(field_json, metric_name, delta);
    }
}

/* -------------------------------------------------------------------------- */
/*                                export couple                               */
/* -------------------------------------------------------------------------- */
struct couple_export_table *couple_export_table_new()
{
    struct couple_export_table *ret = calloc(1, sizeof(struct couple_export_table));
    return ret;
}

void couple_export_table_add(struct couple_export_table *tbl, const char *json, int id)
{
    struct couple_export_table_item *node = malloc(sizeof(struct couple_export_table_item));
    node->key = strdup(json);
    node->kv_pair_id = id;

    HASH_ADD_KEYPTR(hh, tbl->items, node->key, strlen(node->key), node);
}

int couple_export_table_find(const struct couple_export_table *tbl, const char *json)
{
    struct couple_export_table_item *node;
    HASH_FIND_STR(tbl->items, json, node);
    if (node == NULL) {
        return -1;
    }
    return node->kv_pair_id;
}

void couple_export_table_free(struct couple_export_table *tbl)
{
    if (tbl == NULL) {
        return;
    }

    struct couple_export_table_item *node, *tmp;
    HASH_ITER(hh, tbl->items, node, tmp) {
        HASH_DEL(tbl->items, node);
        free(node->key);
        free(node);
    }

    free(tbl);
}

#define NIL_TAG_JSON "\anil"

// align delta fields to acc fields by matching field json string, return an array with length n_acc, each of whose element match the corresponding acc element by both field and index
const struct cellwise_rec_for_export **rearrange_metric_delta(const struct cellwise_rec_for_export *acc, size_t n_acc, const struct cellwise_rec_for_export *delta, size_t n_delta)
{
    struct couple_export_table *map = couple_export_table_new();
    for (int i = 0; i < n_delta; i++) {
        const char *tag_json = json_writer_unwrap(delta[i].cjson_tags);
        if (tag_json == NULL) { // field might be empty, give it a dummy name
            tag_json = NIL_TAG_JSON;
        }
        couple_export_table_add(map, tag_json, i);
    }

    const struct cellwise_rec_for_export **ret = calloc(n_acc, sizeof(struct cellwise_rec_for_export *));

    for (int id_acc = 0; id_acc < n_acc; id_acc++) {
        const char *tag_str_acc = json_writer_unwrap(acc[id_acc].cjson_tags);
        if (tag_str_acc == NULL) { // field might be empty, give it a dummy name
            tag_str_acc = NIL_TAG_JSON;
        }

        int id_delta = couple_export_table_find(map, tag_str_acc);
        if (id_delta == -1) {
            continue;
        }
        ret[id_acc] = &delta[id_delta];
    }

    couple_export_table_free(map);
    return ret;
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

        fieldstat_cube_get_cells(instance, cube_id_next, &iter->tag_list, &iter->n_cell);
        if (iter->n_cell == 0) {
            continue;
        }

        iter->shared_tag = fieldstat_cube_get_tags(iter->instance, iter->cube_ids[iter->curr_cube_idx]);
        iter->curr_cell_idx = 0;
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
    int cube_id = iter->cube_ids[iter->curr_cube_idx];
    enum metric_type type = fieldstat_get_metric_type(iter->instance, cube_id, metric_id);
    struct export_kv_pair *ret = NULL;
    if (type == METRIC_TYPE_COUNTER) {
        long long value;
        int tmp_ret = fieldstat_counter_get(iter->instance, cube_id, &iter->tag_list[iter->curr_cell_idx], metric_id, &value);
        if (tmp_ret < 0) {
            return NULL;
        }
        ret = malloc(sizeof(struct export_kv_pair));
        ret->key = strdup(fieldstat_get_metric_name(iter->instance, cube_id, metric_id));
        ret->type = FIELD_VALUE_INTEGER;
        ret->value_longlong = value;
        return ret;
    }
    if (type == METRIC_TYPE_HLL || type == METRIC_TYPE_HISTOGRAM) {
        char *value;
        size_t len;
        fieldstat_metric_get_serialization_as_base64(iter->instance, cube_id, metric_id, &iter->tag_list[iter->curr_cell_idx], &value, &len);
        if (value == NULL) {
            return NULL;
        }
        ret = malloc(sizeof(struct export_kv_pair));
        ret->key = strdup(fieldstat_get_metric_name(iter->instance, cube_id, metric_id));
        ret->type = FIELD_VALUE_CSTRING;
        ret->value_str = value;
        return ret;
    }
    assert(0);
    return NULL;
}

void kv_pair_write_to_json(const struct export_kv_pair *pairs, struct json_writer *writer)
{
    switch (pairs->type)
    {
        case FIELD_VALUE_INTEGER:
            json_writer_longlong_field(writer, pairs->key, pairs->value_longlong);
            break;
        case FIELD_VALUE_DOUBLE:
            json_writer_double_field(writer, pairs->key, pairs->value_double);
            break;
        case FIELD_VALUE_CSTRING:
            json_writer_str_field(writer, pairs->key, pairs->value_str, strlen(pairs->value_str));
            break;
        default:
            break;
    }
}

void tag_list_append_to_tag_object(const struct field_list *tag_list, struct json_writer *cjson_tags)
{
    size_t tag_list_len = tag_list->n_field;
    struct export_kv_pair pairs = {0};

    for (int i = 0; i < tag_list_len; i++) {
        if (tag_list->n_field == 0) {
            continue;
        }
        memset(&pairs, 0, sizeof(struct export_kv_pair));
        pairs.key = (char *)tag_list->field[i].key;
        pairs.type = tag_list->field[i].type;
        switch (pairs.type)
        {
            case FIELD_VALUE_INTEGER:
                pairs.value_longlong = tag_list->field[i].value_longlong;
                break;
            case FIELD_VALUE_DOUBLE:
                pairs.value_double = tag_list->field[i].value_double;
                break;
            case FIELD_VALUE_CSTRING:
                pairs.value_str = (char *)tag_list->field[i].value_str;
                break;
            default:
                break;
        }
        kv_pair_write_to_json(&pairs, cjson_tags);
    }
}

void cell_iter_read_tag_list(const struct cell_iter *iter, struct export_kv_pair **exported_tags, size_t *len_out)
{
    struct field_list *tag_list_cell = &iter->tag_list[iter->curr_cell_idx];
    struct field_list *tag_arr_shared = iter->shared_tag;

    size_t ret_tag_list_len = tag_list_cell->n_field + tag_arr_shared->n_field;

    struct export_kv_pair *tag_list = malloc(sizeof(struct export_kv_pair) * ret_tag_list_len);
    *exported_tags = tag_list;
    *len_out = ret_tag_list_len;

    for (int i = 0; i < tag_list_cell->n_field; i++) {
        kv_pair_fill_with_tags(tag_list++, &tag_list_cell->field[i]);
    }
    for (int i = 0; i < tag_arr_shared->n_field; i++) {
        kv_pair_fill_with_tags(tag_list++, &tag_arr_shared->field[i]);
    }
}

void kv_pair_free_list(struct export_kv_pair *pairs, size_t len)
{
    for (int i = 0; i < len; i++) {
        struct export_kv_pair *pair = &pairs[i];
        if (pair->type == FIELD_VALUE_CSTRING) {
            free(pair->value_str);
        }
        free(pair->key);
    }
    free(pairs);
}

// return 1 if added, 0 if not added
int cjson_map_add(struct cellwise_rec_for_export *tag_field_pair, const struct cell_iter *iter)
{
    struct export_kv_pair **fields = NULL;
    int *metric_ids = NULL;
    size_t n_metric = 0;
    fieldstat_get_metric_in_cell(iter->instance, iter->cube_ids[iter->curr_cube_idx], &iter->tag_list[iter->curr_cell_idx], &metric_ids, &n_metric);
    if (n_metric == 0) {
        // printf("cannot get metric in cell\n");
        return 0;
    }

    int n_nonempty_metrics = 0;
    for (int i = 0; i < n_metric; i++) {
        struct export_kv_pair *field = cell_query_with_iter(iter, metric_ids[i]);
        if (field == NULL) {
            continue;
        }
        if (fields == NULL) {
            fields = malloc(sizeof(struct export_kv_pair *) * n_metric);
        }
        fields[n_nonempty_metrics++] = field;
    }
    free(metric_ids);
    if (fields == NULL) { // all fields are null
        return 0;
    }
    tag_field_pair->metric_pairs = fields;
    tag_field_pair->n_metric = n_nonempty_metrics;

    struct export_kv_pair *tag_list;
    size_t tag_list_len;
    cell_iter_read_tag_list(iter, &tag_list, &tag_list_len);
    struct json_writer *writer_tag = json_writer_init();
    for (int i = 0; i < tag_list_len; i++) {
        kv_pair_write_to_json(&tag_list[i], writer_tag);
    }
    kv_pair_free_list(tag_list, tag_list_len);
    tag_field_pair->cjson_tags = writer_tag;

    return 1;
}

struct cellwise_rec_for_export *read_tag_and_field(const struct fieldstat *instance, size_t *n_pair_out)
{
    // clock_t prepare_start = clock();
    int *cube_id = NULL;
    int cube_id_len = 0;
    *n_pair_out = 0;
    fieldstat_get_cubes(instance, &cube_id, &cube_id_len);
    if (cube_id_len == 0) {
        return NULL;
    }

    size_t n_cell_total = 0;
    for (int i = 0; i < cube_id_len; i++) {
        n_cell_total += fieldstat_get_used_sampling(instance, cube_id[i]);
    }
    free(cube_id);
    if (n_cell_total == 0) {
        return NULL;
    }

    struct cell_iter *iter = cell_iter_new(instance);
    struct cellwise_rec_for_export *tag_field_pair = calloc(n_cell_total, sizeof(struct cellwise_rec_for_export));
    int i = 0;
    while (cell_iter_next(iter)) {
        // next_num++;
        if (cjson_map_add(&tag_field_pair[i], iter) == 0) {
            continue;
        }
        i++;
    }

    *n_pair_out = i;

    cell_iter_free(iter);
    return tag_field_pair;
}

void fieldstat_json_exporter_write_global_tags(const struct fieldstat_json_exporter *exporter, struct cellwise_rec_for_export *tag_field_pair_arr, size_t arr_len)
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
        "timestamp_ms":<timestamp long>
    }
*/
/*
    Output the fieldstat instance to json string array. User must free the output string.
*/
void fieldstat_json_exporter_export_array(const struct fieldstat_json_exporter *exporter, const struct fieldstat *instance, const struct timeval *timestamp, char ***output, size_t *output_size)
{
    if (instance == NULL) {
        *output = NULL;
        *output_size = 0;
        return;
    }
    long long timestamp_ms = cal_ms_time(timestamp);
    if (exporter->history != NULL) {
        fieldstat_json_exporter_update_history((struct fieldstat_json_exporter *)exporter, instance);
    }

    size_t n_pair;
    struct cellwise_rec_for_export *tag_field_pair = read_tag_and_field(instance, &n_pair);
    if (tag_field_pair == NULL || n_pair == 0) {
        free(tag_field_pair); // tag_field_pair is not NULL when there are registered metrics but no valid cells
        *output = NULL;
        *output_size = 0;
        return;
    }

    fieldstat_json_exporter_write_global_tags(exporter, tag_field_pair, n_pair);

    char **cjson_str_arr = (char **)malloc(sizeof(char *) * n_pair);
    
    for (int i = 0; i < n_pair; i++) {
        struct cellwise_rec_for_export *current = &tag_field_pair[i];
        struct json_writer *root = json_writer_init();

        struct json_writer *field_json = json_writer_init();
        for (int j = 0; j < tag_field_pair[i].n_metric; j++) {
            kv_pair_write_to_json(tag_field_pair[i].metric_pairs[j], field_json);
        }
        struct json_writer *field_json_delta = NULL;
        if (exporter->history != NULL) { // not null when fieldstat_json_exporter_enable_delta is called
            field_json_delta = json_writer_init();
            write_delta_to_json((struct fieldstat_json_exporter *)exporter, &tag_field_pair[i], field_json_delta);
        }

        const char *tmp_name = exporter->name ? exporter->name : DEFAULT_EXPORTER_NAME;

        json_writer_start_map(root);
        json_writer_str_field(root, "name", tmp_name, strlen(tmp_name));
        json_writer_object_item(root, "tags", current->cjson_tags);
        json_writer_object_item(root, "fields", field_json);
        if (exporter->history != NULL) {
            json_writer_object_item(root, "fields_delta", field_json_delta);
        }
        json_writer_longlong_field(root, "timestamp_ms", timestamp_ms);
        if (exporter->history != NULL) {
            json_writer_longlong_field(root, "timestamp_ms_delta", timestamp_ms - exporter->history->ts);
            exporter->history->ts = timestamp_ms;
        }

        json_writer_end_map(root);

        char *cjson_str;
        size_t cjson_str_len;
        json_writer_finish(root, &cjson_str, &cjson_str_len);
        cjson_str_arr[i] = cjson_str;
        // user are responsible to free the cjson_str
    }

    *output = cjson_str_arr;
    *output_size = n_pair;

    for (int i = 0; i < n_pair; i++) {
        for (int j = 0; j < tag_field_pair[i].n_metric; j++) {
            kv_pair_free(tag_field_pair[i].metric_pairs[j]);
        }
        free(tag_field_pair[i].metric_pairs);
    }

    free(tag_field_pair);
}

char *fieldstat_json_exporter_export(const struct fieldstat_json_exporter *exporter, const struct fieldstat *instance, const struct timeval *timestamp)
{
    char **str_arr = NULL;
    size_t n_pair = 0;
    fieldstat_json_exporter_export_array(exporter, instance, timestamp, &str_arr, &n_pair);
    if (str_arr == NULL || n_pair == 0) {
        return NULL;
    }

    int buf_len = 4096;
    char *cjson_str_arr = (char *)malloc(buf_len);
    // cjson is so slow, so we construct the json array manually, the reason why we don't use json_writer is simply because I wrote this before implementing json_writer
    int used_len = add_object_to_json_array_start(cjson_str_arr, buf_len);
    for (int i = 0; i < n_pair; i++) {
        used_len = add_object_to_json_array(&cjson_str_arr, &buf_len, used_len, str_arr[i]);
    }
    add_object_to_json_array_end(&cjson_str_arr, buf_len, used_len);

    for (int i = 0; i < n_pair; i++) {
        free(str_arr[i]);
    }
    free(str_arr);

    return cjson_str_arr;
}

char *fieldstat_json_exporter_export_with_delta(const struct fieldstat_json_exporter *exporter, const struct fieldstat *instance, const struct fieldstat *delta,
    const struct timeval *timestamp, const struct timeval *timestamp_delta)
{
    size_t n_pair_acc;
    struct cellwise_rec_for_export *expair_acc = read_tag_and_field(instance, &n_pair_acc);
    if (expair_acc == NULL || n_pair_acc == 0) {
        free(expair_acc); // tag_field_pair is not NULL when there are registered metrics but no valid cells
        return NULL;
    }
    size_t n_pair_delta;
    struct cellwise_rec_for_export *expair_delta = read_tag_and_field(delta, &n_pair_delta);
    const struct cellwise_rec_for_export **expair_delta_rearranged = rearrange_metric_delta(expair_acc, n_pair_acc, expair_delta, n_pair_delta);

    fieldstat_json_exporter_write_global_tags(exporter, expair_acc, n_pair_acc);
    
    struct json_writer *root = json_writer_init();
    json_writer_array_start(root);
    for (int i = 0; i < n_pair_acc; i++) {
        struct cellwise_rec_for_export *current = &expair_acc[i];

        const char *tmp_name = exporter->name ? exporter->name : DEFAULT_EXPORTER_NAME;

        struct json_writer *field_json = json_writer_init();
        for (int j = 0; j < expair_acc[i].n_metric; j++) {
            kv_pair_write_to_json(expair_acc[i].metric_pairs[j], field_json);
        }

        struct json_writer *field_json_delta = json_writer_init();
        if (expair_delta_rearranged[i] != NULL) {
            for (int j = 0; j < expair_delta_rearranged[i]->n_metric; j++) {
                kv_pair_write_to_json(expair_delta_rearranged[i]->metric_pairs[j], field_json_delta);
            }
        }

        json_writer_start_map(root);
        json_writer_str_field(root, "name", tmp_name, strlen(tmp_name));
        json_writer_object_item(root, "tags", current->cjson_tags);
        json_writer_object_item(root, "fields", field_json);
        json_writer_object_item(root, "fields_delta", field_json_delta);
        json_writer_longlong_field(root, "timestamp_ms", cal_ms_time(timestamp));
        json_writer_longlong_field(root, "timestamp_ms_delta", cal_ms_time(timestamp_delta));

        json_writer_end_map(root);
    }
    json_writer_array_end(root);

    char *cjson_str;
    size_t cjson_str_len;
    json_writer_finish(root, &cjson_str, &cjson_str_len);

    for (int i = 0; i < n_pair_acc; i++) {
        for (int j = 0; j < expair_acc[i].n_metric; j++) {
            kv_pair_free(expair_acc[i].metric_pairs[j]);
        }
        free(expair_acc[i].metric_pairs);
    }
    free(expair_acc);

    for (int i = 0; i < n_pair_delta; i++) {
        for (int j = 0; j < expair_delta[i].n_metric; j++) {
            kv_pair_free(expair_delta[i].metric_pairs[j]);
        }
        free(expair_delta[i].metric_pairs);
        char *dummy_s;
        size_t dummy_len;
        json_writer_finish(expair_delta[i].cjson_tags, &dummy_s, &dummy_len); // the field json is not added to the root json, so we need to free it manually
        free(dummy_s);
    }
    free(expair_delta);

    free(expair_delta_rearranged);

    return cjson_str;
}

struct fieldstat_json_exporter *fieldstat_json_exporter_new()
{
    struct fieldstat_json_exporter *exporter = (struct fieldstat_json_exporter *)malloc(sizeof(struct fieldstat_json_exporter));
    exporter->name = NULL;
    exporter->global_tag_list = NULL;
    exporter->history = NULL;
    exporter->last_ts = 0;
    return exporter;
}

void fieldstat_json_exporter_set_global_tag(struct fieldstat_json_exporter *exporter, const struct field tag_list[], size_t n_field)
{
    if (exporter->global_tag_list != NULL) {
        free(exporter->global_tag_list->field);
        free(exporter->global_tag_list);
    }

    exporter->global_tag_list = malloc(sizeof(struct field_list));
    exporter->global_tag_list->n_field = n_field;
    exporter->global_tag_list->field = malloc(sizeof(struct field) * n_field);
    for (size_t i = 0; i < n_field; i++) {
        struct field *field = &exporter->global_tag_list->field[i];
        field->key = strdup(tag_list[i].key);
        field->type = tag_list[i].type;
        switch (field->type)
        {
        case FIELD_VALUE_INTEGER:
            field->value_longlong = tag_list[i].value_longlong;
            break;
        case FIELD_VALUE_CSTRING:
            field->value_str = strdup(tag_list[i].value_str);
            break;
        case FIELD_VALUE_DOUBLE:
            field->value_double = tag_list[i].value_double;
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

    if (exporter->history != NULL) {
        counter_history_free(exporter->history);
    }

    free(exporter);
}

void fieldstat_json_exporter_enable_delta(struct fieldstat_json_exporter *exporter)
{
    if (exporter->history != NULL) {
        return;
    }
    exporter->history = counter_history_new();
}

void fieldstat_json_exporter_export_flat(const struct fieldstat_json_exporter *exporter, const struct fieldstat *instance, const struct timeval *timestamp, char ***output, size_t *output_size) {
    long long timestamp_ms = cal_ms_time(timestamp);

    size_t n_pair;
    struct cellwise_rec_for_export *tag_field_pair = read_tag_and_field(instance, &n_pair);
    if (tag_field_pair == NULL || n_pair == 0) {
        free(tag_field_pair); // tag_field_pair is not NULL when there are registered metrics but no valid cells
        *output = NULL;
        *output_size = 0;
        return;
    }

    fieldstat_json_exporter_write_global_tags(exporter, tag_field_pair, n_pair);

    char **cjson_str_arr = (char **)malloc(sizeof(char *) * n_pair);
    for (int i = 0; i < n_pair; i++) {
        struct cellwise_rec_for_export *current = &tag_field_pair[i];
        struct json_writer *root = current->cjson_tags;
        for (int j = 0; j < tag_field_pair[i].n_metric; j++) {
            kv_pair_write_to_json(tag_field_pair[i].metric_pairs[j], root);
        }

        const char *tmp_name = exporter->name ? exporter->name : DEFAULT_EXPORTER_NAME;
        json_writer_str_field(root, "name", tmp_name, strlen(tmp_name));
        json_writer_longlong_field(root, "timestamp_ms", timestamp_ms);

        char *cjson_str;
        size_t cjson_str_len;
        json_writer_finish(root, &cjson_str, &cjson_str_len);
        char *cjson_with_braces = malloc(cjson_str_len + 3);
        cjson_with_braces[0] = '{';
        memcpy(cjson_with_braces + 1, cjson_str, cjson_str_len);
        cjson_with_braces[cjson_str_len + 1] = '}';
        cjson_with_braces[cjson_str_len + 2] = '\0';

        cjson_str_arr[i] = cjson_with_braces;
        free(cjson_str);
    }

    *output = cjson_str_arr;
    *output_size = n_pair;

    for (int i = 0; i < n_pair; i++) {
        for (int j = 0; j < tag_field_pair[i].n_metric; j++) {
            kv_pair_free(tag_field_pair[i].metric_pairs[j]);
        }
        free(tag_field_pair[i].metric_pairs);
    }

    free(tag_field_pair);
}