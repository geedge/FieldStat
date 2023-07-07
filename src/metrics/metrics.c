
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>

#include "mpack/mpack.h"
#include "uthash.h"

#include "metrics.h"
#include "fieldstat.h"
#include "tags/dimension_cube_map.h"
#include "hdr/hdr_histogram.h"
#include "hdr/hdr_histogram_log.h"
#include "st_hyperloglog.h"

enum counter_metric_type {
    COUNTER_TYPE_COUNTER,
    COUNTER_TYPE_GAUGE,
};

struct metric_counter_or_gauge {
    enum counter_metric_type type;
    long long value;
};

struct para_counter {
    enum counter_metric_type type;
};

struct para_hdr{
    long long lowest_trackable_value;
    long long highest_trackable_value;
    int significant_figures;
};

struct para_hll {
    char precision;
};

struct metric_parameter {
    union {
        struct para_counter counter;
        struct para_hdr hdr;
        struct para_hll hll;
    };
};

struct metric_info {
    int id;
    const char *name;
    struct metric_parameter *paras;
    enum metric_type type;
};

typedef struct metric_measure_data *(*metric_func_new)(const struct metric_parameter *);
typedef void (*metric_func_del)(struct metric_measure_data *);
typedef int (*metric_func_merge)(struct metric_measure_data *, const struct metric_measure_data *);
typedef int (*metric_func_copy)(struct metric_measure_data *, const struct metric_measure_data *);
typedef void (*metric_func_serialize)(const struct metric_measure_data *, char **/*blob*/, size_t */*blob_size*/);
typedef struct metric_measure_data * (*metric_func_deserialize)(const char *, size_t );
struct metric_scheme {
    metric_func_new new;
    metric_func_del del;
    metric_func_merge merge;
    metric_func_copy copy;
    metric_func_serialize serialize;
    metric_func_deserialize deserialize;
};

struct metric_measure_data {
    union {
        struct metric_counter_or_gauge *counter;
        struct ST_hyperloglog *hll;
        struct hdr_histogram *hdr;
    };
};

struct cell_id_data_map {
    int id;
    struct metric_measure_data *data;
    UT_hash_handle hh;
};

struct metric {
    const struct metric_scheme *scheme;
    struct metric_info *info;
    struct cell_id_data_map *data_map;
};


struct metric_measure_data *metric_find_one_cell(struct metric *pthis, int id) {
    struct cell_id_data_map *map = pthis->data_map;
    struct cell_id_data_map *s;
    HASH_FIND_INT(map, &id, s);
    if (s == NULL) {
        return NULL;
    }
    return s->data;
}

/* -------------------------------------------------------------------------- */
/*                                   scheme                                   */
/* -------------------------------------------------------------------------- */

/* --------------------------------- counter -------------------------------- */

struct metric_measure_data *metric_scheme_counter_new(const struct metric_parameter *para)
{
    struct metric_measure_data *data = (struct metric_measure_data *)malloc(sizeof(struct metric_measure_data));
    struct metric_counter_or_gauge *counter = (struct metric_counter_or_gauge *)malloc(sizeof(struct metric_counter_or_gauge));
    data->counter = counter;
    counter->value = 0;
    counter->type = para->counter.type;
    return data;
}

static void metric_scheme_counter_free(struct metric_measure_data *data)
{
    free(data->counter);
    free(data);
}

/*
    "counter": {
        "value": 0
        "type": 0(counter) / 1(gauge)
    }
*/
static void metric_scheme_counter_serialize(const struct metric_measure_data *data, char **blob, size_t *blob_size)
{
    struct metric_counter_or_gauge *counter = data->counter;
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_size);
    mpack_build_map(&writer);

    mpack_write_cstr(&writer, "value");
    mpack_write_i64(&writer, counter->value);
    mpack_write_cstr(&writer, "type");
    mpack_write_i8(&writer, counter->type);

    mpack_complete_map(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        printf("ERR: An error occurred in counter_metric_serialize!\n");
    }
}

static int metric_scheme_counter_merge(struct metric_measure_data *pthis, const struct metric_measure_data *from)
{
    struct metric_counter_or_gauge *counter = pthis->counter;
    struct metric_counter_or_gauge *from_counter = from->counter;
    
    if (counter->type != from_counter->type) {
        printf("ERR: counter type not match!\n");
        return -1;
    }
    switch (counter->type) {
        case COUNTER_TYPE_COUNTER:
            counter->value += from_counter->value;
            break;
        case COUNTER_TYPE_GAUGE:
            counter->value = counter->value > from_counter->value ? counter->value : from_counter->value;
            break;
        default:
            printf("ERR: counter type not match!\n");
            return -1;
    }

    return 0;
}

static int metric_scheme_counter_copy(struct metric_measure_data *pthis, const struct metric_measure_data *from)
{
    struct metric_counter_or_gauge *counter = pthis->counter;
    struct metric_counter_or_gauge *from_counter = from->counter;
    counter->value = from_counter->value;
    counter->type = from_counter->type;
    return 0;
}

struct metric_measure_data *metric_scheme_counter_deserialize(const char *blob, size_t blob_size) 
{
    mpack_tree_t content_tree;
    mpack_tree_init_data(&content_tree, blob, blob_size);
    mpack_tree_parse(&content_tree);
    mpack_node_t content_root = mpack_tree_root(&content_tree);

    struct metric_measure_data *ret = (struct metric_measure_data *)malloc(sizeof(struct metric_measure_data));
    struct metric_counter_or_gauge *counter = (struct metric_counter_or_gauge *)malloc(sizeof(struct metric_counter_or_gauge));
    ret->counter = counter;

    counter->value = mpack_node_i64(mpack_node_map_cstr(content_root, "value"));
    counter->type = mpack_node_i8(mpack_node_map_cstr(content_root, "type"));

    mpack_tree_destroy(&content_tree);
    return ret;
}


// todo other metric schemes write here

static const struct metric_scheme g_metric_scheme_table[] = {
    [METRIC_TYPE_COUNTER] = {
        .new = metric_scheme_counter_new,
        .del = metric_scheme_counter_free,
        .serialize = metric_scheme_counter_serialize,
        .deserialize = metric_scheme_counter_deserialize,
        .merge = metric_scheme_counter_merge,
        .copy = metric_scheme_counter_copy,
    },
};


/* -------------------------------------------------------------------------- */
/*                                   metric                                   */
/* -------------------------------------------------------------------------- */

struct metric_parameter *construct_parameters(enum metric_type type, ...)
{
    struct metric_parameter *paras = (struct metric_parameter *)malloc(sizeof(struct metric_parameter));
    va_list ap;
    va_start(ap, type);
    switch (type) {
        case METRIC_TYPE_COUNTER:
            paras->counter.type = (enum counter_metric_type)va_arg(ap, int);
            break;
        case METRIC_TYPE_HLL:
            paras->hll.precision = (char)va_arg(ap, int);
            break;
        case METRIC_TYPE_HISTOGRAM:
            paras->hdr.lowest_trackable_value = va_arg(ap, long long);
            paras->hdr.highest_trackable_value = va_arg(ap, long long);
            paras->hdr.significant_figures = va_arg(ap, int);
            break;
        default:
            printf("construct_parameters: unknown type\n");
            assert(0);
    }
    va_end(ap);
    return paras;
}

struct metric_info *metric_info_new(const char *name, enum metric_type type, const struct metric_parameter *para)
{
    struct metric_info *info = (struct metric_info *)malloc(sizeof(struct metric_info));
    info->name = strdup(name);
    info->paras = para;
    info->type = type;
    info->id = -1;
    return info;
}

void metric_info_free(struct metric_info *info)
{
    free(info->name);
    free(info->paras);
    free(info);
}

const char *metric_get_name(const struct metric *metrics)
{
    return metrics->info->name;
}

void metric_free(struct metric *pthis)
{
    metric_info_free(pthis->info);

    struct cell_id_data_map *cur, *tmp;
    HASH_ITER(hh, pthis->data_map, cur, tmp) {
        pthis->scheme->del(cur->data);
        HASH_DEL(pthis->data_map, cur);
        free(cur);
    }

    free(pthis);
}

void metric_info_copy(struct metric_info *dst, const struct metric_info *src)
{
    dst->name = strdup(src->name);
    dst->paras = (struct metric_parameter *)malloc(sizeof(struct metric_parameter));
    memcpy(dst->paras, src->paras, sizeof(struct metric_parameter));
    dst->type = src->type;
    dst->id = src->id;
}

struct metric *metric_copy_structure(const struct metric *src)
{
    struct metric *dst = (struct metric *)malloc(sizeof(struct metric));
    dst->info = (struct metric_info *)malloc(sizeof(struct metric_info));
    metric_info_copy(dst->info, src->info);
    dst->scheme = &g_metric_scheme_table[src->info->type];
    dst->data_map = NULL;
    return dst;
}

/*

---------------------------------- counter ---------------------------------- 
{
    "type": 0
    "merge_type"
}

---------------------------------- hll ---------------------------------- 
{
    "type": 1,
    "prec": <precision value>
}

---------------------------------- histogram ---------------------------------- 
{
    "type": 2,
    "min": <lowest_trackable_value>
    "max": <highest_trackable_value>
    "prec": <significant_figures>
}
*/
void metric_serialize_parameters(const struct metric *metric, char **blob, size_t *blob_size)
{
    struct metric_parameter *para = metric->info->paras;
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_size);
    mpack_build_map(&writer);

    mpack_write_cstr(&writer, "type");
    mpack_write_i32(&writer, metric->info->type);

    switch (metric->info->type)
    {
    case METRIC_TYPE_COUNTER:
        mpack_write_cstr(&writer, "merge_type");
        mpack_write_i32(&writer, para->counter.type);
    case METRIC_TYPE_HLL:
        mpack_write_cstr(&writer, "prec");
        mpack_write_i32(&writer, para->hll.precision);
        break;
    case METRIC_TYPE_HISTOGRAM:
        mpack_write_cstr(&writer, "min");
        mpack_write_i64(&writer, para->hdr.lowest_trackable_value);
        mpack_write_cstr(&writer, "max");
        mpack_write_i64(&writer, para->hdr.highest_trackable_value);
        mpack_write_cstr(&writer, "prec");
        mpack_write_i8(&writer, para->hdr.significant_figures);
        break;
    default:
        break;
    }
    mpack_complete_map(&writer);

    mpack_writer_destroy(&writer);
}

struct metric_parameter *deserialize_parameters(const char *blob, size_t blob_size)
{
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, blob_size);
    mpack_tree_parse(&tree);
    mpack_node_t root = mpack_tree_root(&tree);

    mpack_node_t type_obj = mpack_node_map_cstr(root, "type");
    enum metric_type type = mpack_node_i32(type_obj);
    struct metric_parameter *field_para = NULL;
    switch (type) {
        case METRIC_TYPE_COUNTER:{
            int counter_type = mpack_node_i32(mpack_node_map_cstr(root, "merge_type"));
            field_para = construct_parameters(type, counter_type);
        }
        break;
        case METRIC_TYPE_HLL: {
            int precision = mpack_node_i32(mpack_node_map_cstr(root, "prec"));
            field_para = construct_parameters(type, precision);
        }
        break;
        case METRIC_TYPE_HISTOGRAM: {
            int64_t min = mpack_node_i64(mpack_node_map_cstr(root, "min"));
            int64_t max = mpack_node_i64(mpack_node_map_cstr(root, "max"));
            int8_t prec = mpack_node_i8(mpack_node_map_cstr(root, "prec"));
            field_para = construct_parameters(type, min, max, prec);
        }
        break;
        default:
            break;
    }
    mpack_tree_destroy(&tree);
    return field_para;
}

/*
{
    "id":<cell_id>,
    "PL":<content_blob>
}
*/
void mpack_blob_wrap_basic_info(int cell_id, const char *blob, size_t blob_size, char **new_blob, size_t *new_blob_size)
{
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, new_blob, new_blob_size);
    mpack_start_map(&writer, 3);
    mpack_write_cstr(&writer, "id");
    mpack_write_int(&writer, cell_id);
    mpack_write_cstr(&writer, "PL");
    mpack_write_bin(&writer, blob, blob_size);
    mpack_finish_map(&writer);
    mpack_writer_destroy(&writer);
}

// user must free new_blob
void mpack_blob_unpack_basic_info(const char *blob, size_t blob_size, char **new_blob, size_t *new_blob_size, int *cell_id)
{
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, blob_size);
    mpack_tree_parse(&tree);
    mpack_node_t root = mpack_tree_root(&tree);

    mpack_node_t id_node = mpack_node_map_cstr(root, "id");
    *cell_id = mpack_node_int(id_node);

    mpack_node_t payload_node = mpack_node_map_cstr(root, "PL");
    *new_blob_size = mpack_node_bin_size(payload_node);
    *new_blob = (char *)malloc(*new_blob_size);
    memcpy(*new_blob, mpack_node_bin_data(payload_node), *new_blob_size);

    mpack_tree_destroy(&tree);
}


/*
metric blob:
    {
        "name":<field_name>
        "type":<field_type>
        "id":<field_id>
        "para":[may be null, one number, or a double array]
        "data":[{
            "id":<cell_id>
            "PL":<content_blob(gen by scheme)>
        }]
}*/
void metric_serialize(const struct metric *pthis, char **blob, size_t *blob_size)
{
    char *para_blob = NULL;
    size_t para_blob_size = 0;
    char *data_blob = NULL;
    size_t data_blob_size = 0;
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_size);

    mpack_build_map(&writer);
    mpack_write_cstr(&writer, "name");
    mpack_write_str(&writer, pthis->info->name, strlen(pthis->info->name));
    mpack_write_cstr(&writer, "type");
    mpack_write_i32(&writer, pthis->info->type);

    mpack_write_cstr(&writer, "para");
    metric_serialize_parameters(pthis, &para_blob, &para_blob_size);
    mpack_write_bin(&writer, para_blob, para_blob_size);
    free(para_blob);

    mpack_write_cstr(&writer, "data");
    if (HASH_COUNT(pthis->data_map) == 0) {
        mpack_write_nil(&writer);
    } else {
        struct cell_id_data_map *node, *tmp;
        mpack_build_array(&writer);
        HASH_ITER(hh, pthis->data_map, node, tmp) {
            pthis->scheme->serialize(node->data, &data_blob, &data_blob_size);
            char *tmp_data_wrapped_blob = NULL;
            size_t tmp_data_wrapped_blob_size = 0;
            mpack_blob_wrap_basic_info(node->id, data_blob, data_blob_size, &tmp_data_wrapped_blob, &tmp_data_wrapped_blob_size);
            mpack_write_bin(&writer, tmp_data_wrapped_blob, tmp_data_wrapped_blob_size);
            free(data_blob);
            free(tmp_data_wrapped_blob);
        }
        mpack_complete_array(&writer);
    }

    mpack_complete_map(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        return -1;
    }

    return 0;
}

struct metric *metric_deserialize(const char *blob, size_t blob_size)
{
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, blob_size);
    mpack_tree_parse(&tree);
    mpack_node_t root = mpack_tree_root(&tree);

    char *metric_name = mpack_node_cstr_alloc(mpack_node_map_cstr(root, "name"), 0xFFFF); // 0xFFFF: a very large number
    enum metric_type type = mpack_node_i32(mpack_node_map_cstr(root, "type"));
    mpack_node_t para_obj = mpack_node_map_cstr(root, "para");
    struct metric_parameter *metric_para = deserialize_parameters(mpack_node_bin_data(para_obj), mpack_node_bin_size(para_obj));

    struct metric *metric_out = metric_new(type, metric_name, metric_para);
    free(metric_name);

    mpack_node_t data_arr = mpack_node_map_cstr(root, "data");
    if (mpack_node_is_nil(data_arr)) {
        mpack_tree_destroy(&tree);
        return metric_out;
    }
    
    for (int i = 0; i < mpack_node_array_length(data_arr); i++) {
        mpack_node_t data_node = mpack_node_array_at(data_arr, i);
        int cell_id;
        char *data_blob = NULL;
        size_t data_blob_size = 0;
        mpack_blob_unpack_basic_info(mpack_node_bin_data(data_node), mpack_node_bin_size(data_node), &data_blob, &data_blob_size, &cell_id);
        struct cell_id_data_map *node = (struct cell_id_data_map *)malloc(sizeof(struct cell_id_data_map));
        node->id = cell_id;
        node->data = metric_out->scheme->deserialize(data_blob, data_blob_size);
        free(data_blob);
        HASH_ADD_INT(metric_out->data_map, id, node);
    }
    mpack_tree_destroy(&tree);
    return metric_out;
}

int metric_merge_cell(struct metric *dest, const struct metric *src, int dest_cell_id, int src_cell_id)
{
    if (metric_info_check_is_equal(dest->info, src->info) == false) {
        return -1;
    }
    dest->scheme->merge(metric_find_one_cell(dest, dest_cell_id), metric_find_one_cell(src, src_cell_id));
}

void metric_get_plain_blob(const struct metric *pthis, int cell_id, char **blob, size_t *blob_size)
{
    if (pthis->info->type == METRIC_TYPE_HLL) {
        ST_hyperloglog_serialize_for_networking(metric_find_one_cell(pthis, cell_id)->hll, blob, blob_size);
        return;
    }
    pthis->scheme->serialize(metric_find_one_cell(pthis, cell_id), blob, blob_size);
}

void metric_del_at_position(struct metric *pthis, int cell_id)
{
    struct cell_id_data_map *node;
    HASH_FIND_INT(pthis->data_map, &cell_id, node);
    if (node == NULL) {
        return;
    }
    HASH_DEL(pthis->data_map, node);
    pthis->scheme->del(node->data);
    free(node);
}

int metric_copy_cell(struct metric *dest, const struct metric *src, int dest_cell_id, int src_cell_id)
{
    struct cell_id_data_map *node;
    HASH_FIND_INT(dest->data_map, &dest_cell_id, node);
    if (node != NULL) {
        printf("cell id %d already exists in dest\n", dest_cell_id);
        return -1;
    }
    HASH_FIND_INT(src->data_map, &src_cell_id, node);
    if (node == NULL) {
        printf("cell id %d does not exist in src\n", src_cell_id);
        return -1;
    }
    struct cell_id_data_map *new_node = (struct cell_id_data_map *)malloc(sizeof(struct cell_id_data_map));
    new_node->id = dest_cell_id;
    new_node->data = dest->scheme->new();
    int ret = dest->scheme->copy(new_node->data, node->data);
    if (ret == -1) {
        dest->scheme->del(new_node->data);
        free(new_node);
        printf("copy failed\n");
        return -1;
    }
    HASH_ADD_INT(dest->data_map, id, new_node);

    return 0;
}

int metric_change_cell_id(struct metric *pthis, int old_cell_id, int new_cell_id)
{
    struct cell_id_data_map *node;
    HASH_FIND_INT(pthis->data_map, &old_cell_id, node);
    if (node == NULL) {
        return -1;
    }
    HASH_DEL(pthis->data_map, node);
    node->id = new_cell_id;
    HASH_ADD_INT(pthis->data_map, id, node);
    return 0;
}


/* -------------------------------------------------------------------------- */
/*                           metric operations                                */ 
/* -------------------------------------------------------------------------- */

struct metric *metric_create_for_counter(struct metric *pthis, const char *name, bool is_gauge)
{
    struct metric *pthis = (struct metric *)malloc(sizeof(struct metric));
    struct metric_parameter *metric_para = construct_parameters(METRIC_TYPE_COUNTER, is_gauge ? COUNTER_TYPE_GAUGE : COUNTER_TYPE_COUNTER);
    pthis->info = metric_info_new(name, METRIC_TYPE_COUNTER, metric_para);
    pthis->scheme = &g_metric_scheme_table[METRIC_TYPE_COUNTER];

    return pthis;
}

int metric_counter_incrby(struct metric *pthis, int cell_id, long long value)
{
    struct metric_measure_data *data = metric_find_one_cell(pthis, cell_id);
    if (data == NULL) {
        printf("cell id %d does not exist\n", cell_id);
        return -1;
    }
    struct metric_counter_or_gauge *counter = data->counter;
    counter->value += value;

    return 0;
}

int metric_counter_set(struct metric *pthis, int cell_id, long long value)
{
    struct metric_measure_data *data = metric_find_one_cell(pthis, cell_id);
    if (data == NULL) {
        printf("cell id %d does not exist\n", cell_id);
        return -1;
    }
    struct metric_counter_or_gauge *counter = data->counter;
    counter->value = value;

    return 0;
}

long long metric_counter_get(const struct metric *pthis, int cell_id)
{
    struct metric_measure_data *data = metric_find_one_cell(pthis, cell_id);
    if (data == NULL) {
        printf("cell id %d does not exist\n", cell_id);
        return -1;
    }
    struct metric_counter_or_gauge *counter = data->counter;
    return counter->value;
}



// TODO: other metric
