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
    struct counter_history *history;
};

struct cell_iter {
    int *cube_ids;
    int n_cube;
    int curr_cube_idx;
    struct fieldstat_tag_list *shared_tag;

    struct fieldstat_tag_list *tag_list;
    size_t n_cell;
    size_t curr_cell_idx;

    int *metric_ids;
    size_t n_metric;

    const struct fieldstat *instance;
};

struct cellwise_rec_for_export {
    struct json_writer *cjson_tags;
    struct json_writer *cjson_fields;

    char **metric_name; // used for delta export
    long long *metric_value; // used for delta export
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

    unsigned long cell_version;
    unsigned long *cube_version;
    size_t n_cube;

    char *exporter_name;
    struct fieldstat_tag_list *global_tag_list;
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

    free(history->cube_version);

    free(history->exporter_name);

    if (history->global_tag_list != NULL) {
        fieldstat_tag_list_arr_free(history->global_tag_list, 1);
    }

    free(history);
}

bool fieldstat_tag_list_cmp(const struct fieldstat_tag_list *a, const struct fieldstat_tag_list *b)
{
    if (a->n_tag != b->n_tag) {
        return false;
    }

    for (int i = 0; i < a->n_tag; i++) {
        if (strcmp(a->tag[i].key, b->tag[i].key) != 0) {
            return false;
        }
        if (a->tag[i].type != b->tag[i].type) {
            return false;
        }
        switch (a->tag[i].type)
        {
        case TAG_INTEGER:
            if (a->tag[i].value_longlong != b->tag[i].value_longlong) {
                return false;
            }
            break;
        case TAG_DOUBLE:
            if (a->tag[i].value_double != b->tag[i].value_double) {
                return false;
            }
            break;
        case TAG_CSTRING:
            if (strcmp(a->tag[i].value_str, b->tag[i].value_str) != 0) {
                return false;
            }
            break;
        default:
            assert(0);
        }
    }

    return true;
}

struct fieldstat_tag_list *my_copy_fs_tag_list(const struct fieldstat_tag_list *src)
{
    struct fieldstat_tag_list *dest = malloc(sizeof(struct fieldstat_tag_list));
    dest->n_tag = src->n_tag;
    dest->tag = malloc(sizeof(struct fieldstat_tag) * dest->n_tag);
    for (int i = 0; i < dest->n_tag; i++) {
        dest->tag[i].key = strdup(src->tag[i].key);
        dest->tag[i].type = src->tag[i].type;
        switch (src->tag[i].type)
        {
        case TAG_INTEGER:
            dest->tag[i].value_longlong = src->tag[i].value_longlong;
            break;
        case TAG_DOUBLE:
            dest->tag[i].value_double = src->tag[i].value_double;
            break;
        case TAG_CSTRING:
            dest->tag[i].value_str = strdup(src->tag[i].value_str);
            break;
        default:
            assert(0);
        }
    }

    return dest;
}

bool counter_history_check_if_need_to_update(const struct fieldstat_json_exporter *exporter)
{
    if (exporter->history == NULL) {
        return false; // delta export disabled
    }

    unsigned long cur_cell_version = fieldstat_get_cell_version(exporter->instance);
    const char *cur_exporter_name = exporter->name ? exporter->name : DEFAULT_EXPORTER_NAME;
    const struct fieldstat_tag_list *cur_global_tag_list = exporter->global_tag_list;

    if (exporter->history->cell_version != cur_cell_version) {
        return true;
    }

    if (strcmp(exporter->history->exporter_name, cur_exporter_name) != 0) {
        return true;
    }

    if (exporter->history->global_tag_list == NULL && cur_global_tag_list != NULL) {
        return true;
    }
    if (exporter->history->global_tag_list == NULL && cur_global_tag_list == NULL) {
        return false;
    }
    // global tag cant be deleted, so no need to check if cur_global_tag_list == NULL && exporter->history->global_tag_list != NULL
    if (fieldstat_tag_list_cmp(cur_global_tag_list, exporter->history->global_tag_list) == false) {
        return true;
    }

    int *cube_ids = NULL;
    int n_cube = 0;
    fieldstat_get_cubes(exporter->instance, &cube_ids, &n_cube);
    if (n_cube != exporter->history->n_cube) {
        return true;
    }
    for (int i = 0; i < n_cube; i++) {
        unsigned long cube_version = fieldstat_get_cube_version(exporter->instance, cube_ids[i]);
        if (cube_version != exporter->history->cube_version[i]) {
            return true;
        }
    }
    free(cube_ids);

    return false;
}

void counter_history_fill_version_info(struct counter_history *history, struct fieldstat_json_exporter *exporter)
{
    unsigned long cur_cell_version = fieldstat_get_cell_version(exporter->instance);
    const char *cur_exporter_name = exporter->name ? exporter->name : DEFAULT_EXPORTER_NAME;
    const struct fieldstat_tag_list *cur_global_tag_list = exporter->global_tag_list;

    free(history->exporter_name);
    history->exporter_name = strdup(cur_exporter_name);
    if (cur_global_tag_list != NULL) {
        history->global_tag_list = my_copy_fs_tag_list(cur_global_tag_list);
    }

    history->cell_version = cur_cell_version;

    int *cube_ids = NULL;
    int n_cube = 0;
    fieldstat_get_cubes(exporter->instance, &cube_ids, &n_cube);
    history->n_cube = n_cube;
    history->cube_version = malloc(sizeof(unsigned long) * n_cube);
    for (int i = 0; i < n_cube; i++) {
        history->cube_version[i] = fieldstat_get_cube_version(exporter->instance, cube_ids[i]);
    }
    free(cube_ids);
}

void fieldstat_json_exporter_update_history(struct fieldstat_json_exporter *exporter)
{
    if (counter_history_check_if_need_to_update(exporter) == false) {
        return;
    }

    counter_history_free(exporter->history);
    exporter->history = counter_history_new();

    counter_history_fill_version_info(exporter->history, exporter);
}

void fieldstat_json_exporter_write_delta(struct fieldstat_json_exporter *exporter, struct cellwise_rec_for_export *tag_field_pair_arr, size_t arr_len)
{
    // for every tag_field_pair, get the tag json string
    for (int i = 0; i < arr_len; i++) {
        const char *tag_json = json_writer_unwrap(tag_field_pair_arr[i].cjson_tags);
        for (int j = 0; j < tag_field_pair_arr[i].n_metric; j++) {
            const char *metric_name = tag_field_pair_arr[i].metric_name[j];
            long long cur_value = tag_field_pair_arr[i].metric_value[j];
            struct name_value_map *node = counter_history_find(exporter->history, tag_json, metric_name);
            long long last_value = node == NULL ? 0 : node->value;
            long long delta = cur_value - last_value;

            if (node == NULL) { // new added cell/cube/metric
                counter_history_add(exporter->history, tag_json, metric_name, cur_value);
            } else { // previous one
                node->value = cur_value;
            }
            // cell will never delete. If delete cube / cell happen, the exporter history will be reset.

            size_t delta_metric_name_len = strlen(metric_name) + 6; // 6 for "_delta"
            char *delta_metric_name = malloc(delta_metric_name_len + 1);
            snprintf(delta_metric_name, delta_metric_name_len + 1, "%s_delta", metric_name);
            delta_metric_name[delta_metric_name_len] = '\0';
            json_writer_longlong_field(tag_field_pair_arr[i].cjson_fields, delta_metric_name, delta);
            free(delta_metric_name);
        }
    }
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

        fieldstat_get_metrics_using_by_cube(instance, cube_id_next, &iter->metric_ids, &iter->n_metric);
        if (iter->n_metric == 0) {
            continue;
        }

        // get cell info 
        fieldstat_get_cells_using_by_cube(instance, cube_id_next, &iter->tag_list, &iter->n_cell);
        if (iter->n_cell == 0) {
            continue;
        }

        iter->shared_tag = fieldstat_get_shared_tags(iter->instance, iter->cube_ids[iter->curr_cube_idx]);
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
    enum metric_type type = fieldstat_get_metric_type(iter->instance, metric_id);
    struct export_kv_pair *ret = NULL;
    if (type == METRIC_TYPE_COUNTER) {
        long long value;
        int tmp_ret = fieldstat_counter_get(iter->instance, iter->cube_ids[iter->curr_cube_idx], metric_id, &iter->tag_list[iter->curr_cell_idx], &value);
        if (tmp_ret == -1) {
            return NULL;
        }
        ret = malloc(sizeof(struct export_kv_pair));
        ret->key = strdup(fieldstat_get_metric_name(iter->instance, metric_id));
        ret->type = TAG_INTEGER;
        ret->value_longlong = value;
        return ret;
    }
    if (type == METRIC_TYPE_HLL || type == METRIC_TYPE_HISTOGRAM) {
        char *value;
        size_t len;
        fieldstat_get_serialized_blob(iter->instance, iter->cube_ids[iter->curr_cube_idx], metric_id, &iter->tag_list[iter->curr_cell_idx], &value, &len);
        if (value == NULL) {
            return NULL;
        }
        ret = malloc(sizeof(struct export_kv_pair));
        ret->key = strdup(fieldstat_get_metric_name(iter->instance, metric_id));
        ret->type = TAG_CSTRING;
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
        case TAG_INTEGER:
            json_writer_longlong_field(writer, pairs->key, pairs->value_longlong);
            break;
        case TAG_DOUBLE:
            json_writer_double_field(writer, pairs->key, pairs->value_double);
            break;
        case TAG_CSTRING:
            json_writer_str_field(writer, pairs->key, pairs->value_str, strlen(pairs->value_str));
            break;
        default:
            break;
    }
}

void tag_list_append_to_tag_object(const struct fieldstat_tag_list *tag_list, struct json_writer *cjson_tags)
{
    size_t tag_list_len = tag_list->n_tag;
    struct export_kv_pair pairs = {0};

    for (int i = 0; i < tag_list_len; i++) {
        if (tag_list->n_tag == 0) {
            continue;
        }
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
        kv_pair_write_to_json(&pairs, cjson_tags);
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
int cjson_map_add(struct cellwise_rec_for_export *tag_field_pair, const struct cell_iter *iter)
{
    struct json_writer *writer = NULL;
    
    for (int i = 0; i < iter->n_metric; i++) {
        struct export_kv_pair *field = cell_query_with_iter(iter, iter->metric_ids[i]);
        if (field == NULL) {
            continue;
        }
        if (writer == NULL) {
            writer = json_writer_init();
        }
        kv_pair_write_to_json(field, writer);
        kv_pair_free(field);
    }

    if (writer == NULL) { // all fields are null
        return 0;
    }
    tag_field_pair->cjson_fields = writer;

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

void cjson_map_record_metrics(struct cellwise_rec_for_export *tag_field_pair, const struct cell_iter *iter)
{
    tag_field_pair->metric_name = malloc(sizeof(char *) * iter->n_metric);
    tag_field_pair->metric_value = malloc(sizeof(long long) * iter->n_metric);

    int n_counter_metric = 0;
    for (int i = 0; i <= iter->n_metric; i++) {
        int metric_id = iter->metric_ids[i];
        int cube_id = iter->cube_ids[iter->curr_cube_idx];
        if (fieldstat_get_metric_type(iter->instance, metric_id) != METRIC_TYPE_COUNTER) {
            continue;
        }
        long long value;
        int ret = fieldstat_counter_get(iter->instance, cube_id, metric_id, &iter->tag_list[iter->curr_cell_idx], &value);
        if (ret == -1) { // no value, happens when the two cells in one cube has different metrics
            continue;
        }

        tag_field_pair->metric_name[n_counter_metric] = strdup(fieldstat_get_metric_name(iter->instance, metric_id));
        tag_field_pair->metric_value[n_counter_metric] = value;

        n_counter_metric++;
    }
    tag_field_pair->n_metric = n_counter_metric;
}

struct cellwise_rec_for_export *read_tag_and_field(const struct fieldstat *instance, size_t *n_pair_out, bool output_delta)
{
    // clock_t prepare_start = clock();
    int *cube_id = NULL;
    int cube_id_len = 0;
    fieldstat_get_cubes(instance, &cube_id, &cube_id_len);
    if (cube_id_len == 0) {
        return NULL;
    }

    size_t n_cell_total = 0;
    for (int i = 0; i < cube_id_len; i++) {
        n_cell_total += fieldstat_get_max_cell_id(instance, cube_id[i]);
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

        if (output_delta) {
            cjson_map_record_metrics(&tag_field_pair[i], iter);
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
void fieldstat_json_exporter_export_array(const struct fieldstat_json_exporter *exporter, const struct timeval *timestamp, char ***output, size_t *output_size)
{
    long long timestamp_ms = cal_ms_time(timestamp);
    if (exporter->history != NULL) {
        fieldstat_json_exporter_update_history((struct fieldstat_json_exporter *)exporter);
    }

    const struct fieldstat *instance = exporter->instance;

    size_t n_pair;
    struct cellwise_rec_for_export *tag_field_pair = read_tag_and_field(instance, &n_pair, exporter->history != NULL);
    if (tag_field_pair == NULL || n_pair == 0) {
        free(tag_field_pair); // tag_field_pair is not NULL when there are registered metrics but no valid cells
        *output = NULL;
        *output_size = 0;
        return;
    }

    fieldstat_json_exporter_write_global_tags(exporter, tag_field_pair, n_pair);
    if (exporter->history != NULL) { // not null when fieldstat_json_exporter_enable_delta is called
        fieldstat_json_exporter_write_delta((struct fieldstat_json_exporter *)exporter, tag_field_pair, n_pair);
    }

    char **cjson_str_arr = (char **)malloc(sizeof(char *) * n_pair);
    
    for (int i = 0; i < n_pair; i++) {
        struct cellwise_rec_for_export *current = &tag_field_pair[i];
        struct json_writer *root = json_writer_init();

        json_writer_start_map(root);
        const char *tmp_name = exporter->name ? exporter->name : DEFAULT_EXPORTER_NAME;
        json_writer_str_field(root, "name", tmp_name, strlen(tmp_name));
        json_writer_object_item(root, "tags", current->cjson_tags);
        json_writer_object_item(root, "fields", current->cjson_fields);
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
    if (exporter->history != NULL) { // read_tag_and_field does not allocate memory for metric_name and metric_value when exporter->history == NULL
        for (int i = 0; i < n_pair; i++) {
            for (int j = 0; j < tag_field_pair[i].n_metric; j++) {
                free(tag_field_pair[i].metric_name[j]);
            }
            free(tag_field_pair[i].metric_name);
            free(tag_field_pair[i].metric_value);
        }
    }
    // json object is added in json_writer_object_item
    free(tag_field_pair);
}

char *fieldstat_json_exporter_export(const struct fieldstat_json_exporter *exporter, const struct timeval *timestamp)
{
    char **str_arr = NULL;
    size_t n_pair = 0;
    fieldstat_json_exporter_export_array(exporter, timestamp, &str_arr, &n_pair);
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

struct fieldstat_json_exporter *fieldstat_json_exporter_new(const struct fieldstat *instance)
{
    struct fieldstat_json_exporter *exporter = (struct fieldstat_json_exporter *)malloc(sizeof(struct fieldstat_json_exporter));
    exporter->instance = instance;
    exporter->name = NULL;
    exporter->global_tag_list = NULL;
    exporter->history = NULL;
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
    counter_history_fill_version_info(exporter->history, exporter);
}