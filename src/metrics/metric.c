
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>

#include "mpack/mpack.h"
#include "uthash.h"

#include "metric.h"
#include "fieldstat.h"
#include "tags/cell_manager.h"
#include "hdr/hdr_histogram.h"
#include "hdr/hdr_histogram_log.h"
#include "st_hyperloglog.h"

#define MAX_N_DATA_ARRAY_ITEM_INIT 1024

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
    char *name;
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

struct metric {
    const struct metric_scheme *scheme;
    struct metric_info *info;
    struct metric_measure_data **data_array;
    size_t max_n_array_item;
    size_t n_array_item;
};


/* -------------------------------------------------------------------------- */
/*                                 id cell map                                */
/* -------------------------------------------------------------------------- */
struct metric_measure_data *metric_find_one_cell(const struct metric *pthis, int id) {
    if (id >= pthis->max_n_array_item) {
        return NULL;
    }
    return pthis->data_array[id];
}

void metric_add_one_cell(struct metric *pthis, int id, struct metric_measure_data *data) {
    if (id >= pthis->max_n_array_item) {
        while (id >= pthis->max_n_array_item) {
            pthis->max_n_array_item *= 2;
        }
        pthis->data_array = (struct metric_measure_data **)realloc(pthis->data_array, pthis->max_n_array_item * sizeof(struct metric_measure_data *));
        memset(pthis->data_array + pthis->n_array_item, 0, (pthis->max_n_array_item - pthis->n_array_item) * sizeof(struct metric_measure_data *));
    }
    if (pthis->data_array[id] != NULL) {
        printf("err, metric_add_one_cell, id: %d, data_array[id] is not NULL\n", id);
    }
    pthis->data_array[id] = data;

    if (id + 1 > pthis->n_array_item) {
        pthis->n_array_item = id + 1;
    }
    return;
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
    const struct metric_counter_or_gauge *counter = data->counter;
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
            printf("ERR: unknown counter type\n");
            return -1;
    }

    return 0;
}

static int metric_scheme_counter_copy(struct metric_measure_data *pthis, const struct metric_measure_data *from)
{
    struct metric_counter_or_gauge *counter = pthis->counter;
    const struct metric_counter_or_gauge *from_counter = from->counter;
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

    if (mpack_tree_destroy(&content_tree) != mpack_ok) {
        printf("ERR: An error occurred in counter_metric_deserialize!\n");
    }
    return ret;
}

/* --------------------------------- hll -------------------------------- */

struct metric_measure_data *metric_scheme_hll_new(const struct metric_parameter *para)
{
    struct metric_measure_data *data = (struct metric_measure_data *)malloc(sizeof(struct metric_measure_data));
    struct ST_hyperloglog *hll = ST_hyperloglog_new(para->hll.precision);
    data->hll = hll;
    return data;
}

static void metric_scheme_hll_free(struct metric_measure_data *data)
{
    ST_hyperloglog_free(data->hll);
    free(data);
}

static void metric_scheme_hll_serialize(const struct metric_measure_data *data, char **blob, size_t *blob_size)
{
    ST_hyperloglog_serialize(data->hll, blob, blob_size);
}

static int metric_scheme_hll_merge(struct metric_measure_data *pthis, const struct metric_measure_data *from)
{
    return ST_hyperloglog_merge(pthis->hll, from->hll);
}

static int metric_scheme_hll_copy(struct metric_measure_data *pthis, const struct metric_measure_data *from)
{
    return ST_hyperloglog_merge(pthis->hll, from->hll);
}

struct metric_measure_data *metric_scheme_hll_deserialize(const char *blob, size_t blob_size) 
{
    struct metric_measure_data *ret = (struct metric_measure_data *)malloc(sizeof(struct metric_measure_data));
    struct ST_hyperloglog *hll = ST_hyperloglog_deserialize(blob, blob_size);
    ret->hll = hll;

    return ret;
}


/* --------------------------------- histogram -------------------------------- */

struct metric_measure_data *metric_scheme_histogram_new(const struct metric_parameter *para)
{
    struct hdr_histogram* tmp_p_hdr = NULL;
    int ret = hdr_init((int64_t)para->hdr.lowest_trackable_value, (int64_t)para->hdr.highest_trackable_value, para->hdr.significant_figures, &tmp_p_hdr);
	if (ret != 0) {
        printf("ERR: hdr_init failed!\n");
        return NULL;
    }
    struct metric_measure_data *data = (struct metric_measure_data *)malloc(sizeof(struct metric_measure_data));
    data->hdr = tmp_p_hdr;

    return data;
}

static void metric_scheme_histogram_free(struct metric_measure_data *data)
{
    hdr_close(data->hdr);
    free(data);
}

static void metric_scheme_histogram_serialize(const struct metric_measure_data *data, char **blob, size_t *blob_size)
{
    struct hdr_histogram *hdr = data->hdr;
    hdr_log_encode(hdr, blob);
    *blob_size = strlen(*blob);
}

static int metric_scheme_histogram_merge(struct metric_measure_data *pthis, const struct metric_measure_data *from)
{
    (void)hdr_add(pthis->hdr, from->hdr);
    return 0;
}

static int metric_scheme_histogram_copy(struct metric_measure_data *pthis, const struct metric_measure_data *from)
{
    (void)hdr_add(pthis->hdr, from->hdr);
    return 0;
}

struct metric_measure_data *metric_scheme_histogram_deserialize(const char *blob, size_t blob_size) 
{
    struct hdr_histogram *hdr = NULL;
    int ret = hdr_log_decode(&hdr, (char *)blob, blob_size);
    if (ret != 0 || hdr == NULL) {
        printf("ERR: hdr_log_decode failed!\n");
        return NULL;
    }
    struct metric_measure_data *data = (struct metric_measure_data *)malloc(sizeof(struct metric_measure_data));
    data->hdr = hdr;
    return data;
}

static const struct metric_scheme g_metric_scheme_table[] = {
    [METRIC_TYPE_COUNTER] = {
        .new = metric_scheme_counter_new,
        .del = metric_scheme_counter_free,
        .serialize = metric_scheme_counter_serialize,
        .deserialize = metric_scheme_counter_deserialize,
        .merge = metric_scheme_counter_merge,
        .copy = metric_scheme_counter_copy,
    },
    [METRIC_TYPE_HLL] = {
        .new = metric_scheme_hll_new,
        .del = metric_scheme_hll_free,
        .serialize = metric_scheme_hll_serialize,
        .deserialize = metric_scheme_hll_deserialize,
        .merge = metric_scheme_hll_merge,
        .copy = metric_scheme_hll_copy,
    },
    [METRIC_TYPE_HISTOGRAM] = {
        .new = metric_scheme_histogram_new,
        .del = metric_scheme_histogram_free,
        .serialize = metric_scheme_histogram_serialize,
        .deserialize = metric_scheme_histogram_deserialize,
        .merge = metric_scheme_histogram_merge,
        .copy = metric_scheme_histogram_copy,
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
            printf("ERR: construct_parameters: unknown type\n");
            assert(0);
    }
    va_end(ap);
    return paras;
}

struct metric_info *metric_info_new(const char *name, enum metric_type type, struct metric_parameter *para)
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

struct metric *metric_new(const char *name, enum metric_type type, struct metric_parameter *para)
{
    struct metric *pthis = (struct metric *)calloc(1, sizeof(struct metric));
    pthis->info = metric_info_new(name, type, para);
    pthis->scheme = &g_metric_scheme_table[type];
    pthis->max_n_array_item = MAX_N_DATA_ARRAY_ITEM_INIT;
    pthis->data_array = (struct metric_measure_data **)calloc(MAX_N_DATA_ARRAY_ITEM_INIT, sizeof(struct metric_measure_data *));

    return pthis;
}

void metric_free(struct metric *pthis)
{
    metric_info_free(pthis->info);

    for (size_t i = 0; i < pthis->n_array_item; i++) {
        if (pthis->data_array[i] == NULL) {
            continue;
        }
        pthis->scheme->del(pthis->data_array[i]);
    }
    free(pthis->data_array);
    free(pthis);
}

void metric_reset(struct metric *pthis) {
    for (int i = 0; i < pthis->n_array_item; i++) {
        if (pthis->data_array[i] == NULL) {
            continue;
        }
        pthis->scheme->del(pthis->data_array[i]);
    }
    pthis->n_array_item = 0;
    memset(pthis->data_array, 0, sizeof(struct metric_measure_data *) * pthis->max_n_array_item);
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

    dst->n_array_item = 0;
    dst->max_n_array_item = src->max_n_array_item;
    dst->data_array = (struct metric_measure_data **)calloc(src->max_n_array_item, sizeof(struct metric_measure_data *));

    return dst;
}

struct metric *metric_copy(const struct metric *src)
{
    struct metric *dst = metric_copy_structure(src);
    dst->n_array_item = src->n_array_item;

    for (int i = 0; i < src->n_array_item; i++) {
        if (src->data_array[i] == NULL) {
            continue;
        }
        struct metric_measure_data *dest_data = src->scheme->new(src->info->paras);
        src->scheme->copy(dest_data, src->data_array[i]);
        dst->data_array[i] = dest_data;
    }
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
    mpack_start_map(&writer, 2);
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
int metric_serialize(const struct metric *pthis, char **blob, size_t *blob_size)
{
    char *para_blob = NULL;
    size_t para_blob_size = 0;
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
    int *cell_ids = NULL;
    size_t cell_ids_size = 0;
    metric_get_cell_ids(pthis, &cell_ids, &cell_ids_size);
    if (cell_ids == NULL) {
        mpack_write_nil(&writer);
    } else {
        mpack_build_array(&writer);
        for (int i = 0; i < cell_ids_size; i++) {
            struct metric_measure_data *data = metric_find_one_cell(pthis, cell_ids[i]);

            char *data_blob = NULL;
            size_t data_blob_size = 0;
            pthis->scheme->serialize(data, &data_blob, &data_blob_size);

            char *tmp_data_wrapped_blob = NULL;
            size_t tmp_data_wrapped_blob_size = 0;
            mpack_blob_wrap_basic_info(cell_ids[i], data_blob, data_blob_size, &tmp_data_wrapped_blob, &tmp_data_wrapped_blob_size);
            mpack_write_bin(&writer, tmp_data_wrapped_blob, tmp_data_wrapped_blob_size);
            free(data_blob);
            free(tmp_data_wrapped_blob);
        }
        free(cell_ids);
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
    struct metric *metric_out = metric_new(metric_name, type, metric_para);
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
        struct metric_measure_data *data = metric_out->scheme->deserialize(data_blob, data_blob_size);
        free(data_blob);
        metric_add_one_cell(metric_out, cell_id, data);
    }
    mpack_tree_destroy(&tree);
    return metric_out;
}

int metric_merge_or_copy_cell(struct metric *dest, const struct metric *src, int dest_cell_id, int src_cell_id)
{
    struct metric_measure_data *src_data = metric_find_one_cell(src, src_cell_id);
    if (src_data == NULL) {
        return 1;
    }

    struct metric_measure_data *dest_data = metric_find_one_cell(dest, dest_cell_id);
    if (dest_data == NULL) { // copy
        dest_data = src->scheme->new(src->info->paras);
        int ret = dest->scheme->copy(dest_data, src_data);
        if (ret == -1) {
            dest->scheme->del(dest_data);
            printf("ERR: metric_merge_or_copy_cell copy failed\n");
            return -1;
        }
        metric_add_one_cell(dest, dest_cell_id, dest_data);
        return 0;
    }

    //merge
    return dest->scheme->merge(dest_data, src_data);
}

void metric_get_plain_blob(const struct metric *pthis, int cell_id, char **blob, size_t *blob_size)
{
    struct metric_measure_data *data = metric_find_one_cell(pthis, cell_id);
    if (data == NULL) {
        *blob = NULL;
        *blob_size = 0;
        return;
    }
    if (pthis->info->type == METRIC_TYPE_HLL) {
        ST_hyperloglog_serialize_for_networking(data->hll, blob, blob_size);
        return;
    }
    pthis->scheme->serialize(data, blob, blob_size);
}

void metric_delete_cell(struct metric *pthis, int cell_id)
{
    struct metric_measure_data *data = metric_find_one_cell(pthis, cell_id);
    if (data == NULL) {
        return;
    }
    pthis->scheme->del(data);
    pthis->data_array[cell_id] = NULL;
}

/* -------------------------------------------------------------------------- */
/*                           metric operations                                */ 
/* -------------------------------------------------------------------------- */

struct metric_measure_data *metric_find_or_new_cell(struct metric *pthis, int cell_id)
{
    struct metric_measure_data *data = metric_find_one_cell(pthis, cell_id);

    if (data == NULL) {
        data = pthis->scheme->new(pthis->info->paras);
        metric_add_one_cell(pthis, cell_id, data);
    }
    return data;
}

struct metric *metric_counter_new(const char *name, bool is_gauge)
{
    struct metric_parameter *metric_para = construct_parameters(METRIC_TYPE_COUNTER, is_gauge ? COUNTER_TYPE_GAUGE : COUNTER_TYPE_COUNTER);
    struct metric *pthis = metric_new(name, METRIC_TYPE_COUNTER, metric_para);

    return pthis;
}

void metric_counter_incrby(struct metric *pthis, int cell_id, long long value)
{
    struct metric_measure_data *data = metric_find_or_new_cell(pthis, cell_id);
    struct metric_counter_or_gauge *counter = data->counter;
    counter->value += value;
}

int metric_counter_set(struct metric *pthis, int cell_id, long long value)
{
    struct metric_measure_data *data = metric_find_or_new_cell(pthis, cell_id);
    struct metric_counter_or_gauge *counter = data->counter;
    if (counter->type == COUNTER_TYPE_COUNTER) {
        return -1;
    }
    counter->value = value;

    return 0;
}

long long metric_counter_get(const struct metric *pthis, int cell_id)
{
    const struct metric_measure_data *data = metric_find_one_cell(pthis, cell_id);
    if (data == NULL) {
        return -1;
    }
    const struct metric_counter_or_gauge *counter = data->counter;
    return counter->value;
}

struct metric *metric_hll_new(const char *name, unsigned char precision)
{
    struct metric_parameter *metric_para = construct_parameters(METRIC_TYPE_HLL, precision);
    struct metric *pthis = metric_new(name, METRIC_TYPE_HLL, metric_para);

    return pthis;
}

void metric_hll_add(struct metric *pthis, int cell_id, const char *key, size_t key_len)
{
    struct metric_measure_data *data = metric_find_or_new_cell(pthis, cell_id);

    ST_hyperloglog_add(data->hll, key, key_len);
}

double metric_hll_get(const struct metric *pthis, int cell_id)
{
    const struct metric_measure_data *data = metric_find_one_cell(pthis, cell_id);
    if (data == NULL) {
        return -1;
    }
    return ST_hyperloglog_count(data->hll);
}

struct metric *metric_histogram_new(const char *name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures)
{
    struct metric_parameter *metric_para = construct_parameters(METRIC_TYPE_HISTOGRAM, lowest_trackable_value, highest_trackable_value, significant_figures);
    struct metric *pthis = metric_new(name, METRIC_TYPE_HISTOGRAM, metric_para);

    return pthis;
}

int metric_histogram_record(struct metric *pthis, int cell_id, long long value)
{
    struct metric_measure_data *data = metric_find_or_new_cell(pthis, cell_id);
    
    bool ret = hdr_record_value(data->hdr, value);
    if (!ret) {
        printf("ERR: metric_histogram_record: hdr_record_value failed\n");
        return -1;
    }
    return 0;
}

long long metric_histogram_value_at_percentile(const struct metric *pthis, int cell_id, double percentile)
{
    const struct metric_measure_data *data = metric_find_one_cell(pthis, cell_id);
    if (data == NULL) {
        return -1;
    }
    return hdr_value_at_percentile(data->hdr, percentile);
}

static long long hdr_count_le_value(const struct hdr_histogram* h, long long value)
{
	struct hdr_iter iter;
	long long count = 0;
	hdr_iter_recorded_init(&iter, h);
	while (hdr_iter_next(&iter))
	{
		if((long long)iter.value <= value)
			count ++;
	}
	return count;
}

long long metric_histogram_count_le_value(const struct metric *pthis, int cell_id, long long value)
{
    const struct metric_measure_data *data = metric_find_one_cell(pthis, cell_id);
    if (data == NULL) {
        printf("ERR: metric_histogram_count_le_value cell id %d does not exist\n", cell_id);
        return -1;
    }
    return hdr_count_le_value(data->hdr, value);
}

/* -------------------------------------------------------------------------- */
/*                             query on the metric                            */
/* -------------------------------------------------------------------------- */
void metric_get_cell_ids(const struct metric *pthis, int **cell_ids, size_t *cell_id_count)
{
    size_t n_id_max = pthis->n_array_item;
    if (n_id_max == 0) {
        *cell_ids = NULL;
        *cell_id_count = 0;
        return;
    }

    int *tmp_id_container = (int *)malloc(sizeof(int) * n_id_max);
    int n_id = 0;
    for (int i = 0; i < n_id_max; i++) {
        if (pthis->data_array[i] != NULL) {
            tmp_id_container[n_id++] = i;
        }
    }
    
    if (n_id == 0) {
        *cell_ids = NULL;
        *cell_id_count = 0;
        free(tmp_id_container);
        return;
    }

    *cell_id_count = n_id;
    *cell_ids = tmp_id_container;
}

const char *metric_get_name(const struct metric *metrics)
{
    return metrics->info->name;
}

enum metric_type metric_get_type(const struct metric *metrics)
{
    return metrics->info->type;
}