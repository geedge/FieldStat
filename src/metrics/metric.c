
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>

#include "histogram_encoder.h"
#include "serializer.h"
#include "metric.h"
#include "hdr/hdr_histogram.h"
#include "hdr/hdr_histogram_log.h"
#include "st_hyperloglog.h"

struct metric_counter_or_gauge {
    long long value;
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
        struct para_hdr hdr;
        struct para_hll hll;
    };
};

struct metric_manifest {
    int id;
    char *name;
    struct metric_parameter *paras;
    enum metric_type type;
};

typedef struct metric_data *(*metric_func_new)(const struct metric_parameter *);
typedef void (*metric_func_free)(struct metric_data *);
typedef int (*metric_func_merge)(struct metric_data *, const struct metric_data *);
typedef int (*metric_func_copy)(struct metric_data *, const struct metric_data *);
typedef void (*metric_func_serialize)(const struct metric_data *, char **/*blob*/, size_t */*blob_size*/);
typedef struct metric_data * (*metric_func_deserialize)(const char *, size_t );
typedef void (*metric_func_reset)(struct metric_data *);
struct metric_scheme {
    metric_func_new new;
    metric_func_free free;
    metric_func_merge merge;
    metric_func_copy copy;
    metric_func_serialize serialize;
    metric_func_deserialize deserialize;
    metric_func_reset reset;
};

struct metric_data {
    union {
        struct metric_counter_or_gauge *counter;
        struct ST_hyperloglog *hll;
        struct hdr_histogram *hdr;
    };
};

struct metric {
    const struct metric_scheme *scheme;
    struct metric_manifest *info;
    struct metric_data *data;
};

/* -------------------------------------------------------------------------- */
/*                                   scheme                                   */
/* -------------------------------------------------------------------------- */

/* --------------------------------- counter -------------------------------- */

struct metric_data *metric_scheme_counter_new(const struct metric_parameter *_para)
{
    struct metric_data *data = (struct metric_data *)malloc(sizeof(struct metric_data));
    struct metric_counter_or_gauge *counter = (struct metric_counter_or_gauge *)malloc(sizeof(struct metric_counter_or_gauge));
    data->counter = counter;
    counter->value = 0;
    return data;
}

static void metric_scheme_counter_free(struct metric_data *data)
{
    free(data->counter);
    free(data);
}

static void metric_scheme_counter_serialize(const struct metric_data *data, char **blob, size_t *blob_size)
{
    const struct metric_counter_or_gauge *counter = data->counter;
    struct fs_reader *reader = fs_reader_new();

    fs_reader_read_longlong(reader, counter->value);

    fs_reader_finalize(reader, blob, blob_size);
}

static int metric_scheme_counter_merge(struct metric_data *pthis, const struct metric_data *from)
{
    struct metric_counter_or_gauge *counter = pthis->counter;
    const struct metric_counter_or_gauge *from_counter = from->counter;
    counter->value += from_counter->value;
    
    return 0;
}

static int metric_scheme_counter_copy(struct metric_data *pthis, const struct metric_data *from)
{
    struct metric_counter_or_gauge *counter = pthis->counter;
    const struct metric_counter_or_gauge *from_counter = from->counter;
    counter->value = from_counter->value;
    return 0;
}

struct metric_data *metric_scheme_counter_deserialize(const char *blob, size_t blob_size) 
{
    struct fs_writer *writer = fs_writer_new(blob, blob_size);
    struct metric_data *ret = (struct metric_data *)malloc(sizeof(struct metric_data));
    struct metric_counter_or_gauge *counter = (struct metric_counter_or_gauge *)malloc(sizeof(struct metric_counter_or_gauge));
    ret->counter = counter;

    fs_writer_write_longlong(writer, &counter->value);

    fs_writer_free(writer);
    return ret;
}

void metric_scheme_counter_reset(struct metric_data *data)
{
    data->counter->value = 0;
}

/* --------------------------------- hll -------------------------------- */

struct metric_data *metric_scheme_hll_new(const struct metric_parameter *para)
{
    struct metric_data *data = (struct metric_data *)malloc(sizeof(struct metric_data));
    struct ST_hyperloglog *hll = ST_hyperloglog_new(para->hll.precision);
    data->hll = hll;
    return data;
}

static void metric_scheme_hll_free(struct metric_data *data)
{
    ST_hyperloglog_free(data->hll);
    free(data);
}

static void metric_scheme_hll_serialize(const struct metric_data *data, char **blob, size_t *blob_size)
{
    ST_hyperloglog_serialize(data->hll, blob, blob_size);
}

static int metric_scheme_hll_merge(struct metric_data *pthis, const struct metric_data *from)
{
    return ST_hyperloglog_merge(pthis->hll, from->hll);
}

static int metric_scheme_hll_copy(struct metric_data *pthis, const struct metric_data *from)
{
    return ST_hyperloglog_merge(pthis->hll, from->hll);
}

struct metric_data *metric_scheme_hll_deserialize(const char *blob, size_t blob_size) 
{
    struct metric_data *ret = (struct metric_data *)malloc(sizeof(struct metric_data));
    struct ST_hyperloglog *hll = ST_hyperloglog_deserialize(blob, blob_size);
    ret->hll = hll;

    return ret;
}

void metric_scheme_hll_reset(struct metric_data *data)
{
    ST_hyperloglog_reset(data->hll);
}

/* --------------------------------- histogram -------------------------------- */

struct metric_data *metric_scheme_histogram_new(const struct metric_parameter *para)
{
    struct hdr_histogram* tmp_p_hdr = NULL;
    int ret = hdr_init((int64_t)para->hdr.lowest_trackable_value, (int64_t)para->hdr.highest_trackable_value, para->hdr.significant_figures, &tmp_p_hdr);
	if (ret != 0) {
        return NULL;
    }
    struct metric_data *data = (struct metric_data *)malloc(sizeof(struct metric_data));
    data->hdr = tmp_p_hdr;

    return data;
}

static void metric_scheme_histogram_free(struct metric_data *data)
{
    hdr_close(data->hdr);
    free(data);
}

static void metric_scheme_histogram_serialize(const struct metric_data *data, char **blob, size_t *blob_size)
{
    histogram_encode_into_blob(data->hdr, blob, blob_size);
}

static int metric_scheme_histogram_merge(struct metric_data *pthis, const struct metric_data *from)
{
    (void)hdr_add(pthis->hdr, from->hdr);
    return 0;
}

static int metric_scheme_histogram_copy(struct metric_data *pthis, const struct metric_data *from)
{
    (void)hdr_add(pthis->hdr, from->hdr);
    return 0;
}

struct metric_data *metric_scheme_histogram_deserialize(const char *blob, size_t blob_size) 
{
    struct hdr_histogram *hdr = histogram_decode_from_blob(blob, blob_size);
    struct metric_data *data = (struct metric_data *)malloc(sizeof(struct metric_data));
    data->hdr = hdr;
    return data;
}

void metric_scheme_histogram_reset(struct metric_data *data)
{
    hdr_reset(data->hdr);
}

static const struct metric_scheme g_metric_scheme_table[] = {
    [METRIC_TYPE_COUNTER] = {
        .new = metric_scheme_counter_new,
        .free = metric_scheme_counter_free,
        .serialize = metric_scheme_counter_serialize,
        .deserialize = metric_scheme_counter_deserialize,
        .merge = metric_scheme_counter_merge,
        .copy = metric_scheme_counter_copy,
        .reset = metric_scheme_counter_reset,
    },
    [METRIC_TYPE_HLL] = {
        .new = metric_scheme_hll_new,
        .free = metric_scheme_hll_free,
        .serialize = metric_scheme_hll_serialize,
        .deserialize = metric_scheme_hll_deserialize,
        .merge = metric_scheme_hll_merge,
        .copy = metric_scheme_hll_copy,
        .reset = metric_scheme_hll_reset,
    },
    [METRIC_TYPE_HISTOGRAM] = {
        .new = metric_scheme_histogram_new,
        .free = metric_scheme_histogram_free,
        .serialize = metric_scheme_histogram_serialize,
        .deserialize = metric_scheme_histogram_deserialize,
        .merge = metric_scheme_histogram_merge,
        .copy = metric_scheme_histogram_copy,
        .reset = metric_scheme_histogram_reset,
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
            assert(0);
    }
    va_end(ap);
    return paras;
}

struct metric_manifest *metric_info_new(const char *name, enum metric_type type, struct metric_parameter *para)
{
    struct metric_manifest *info = (struct metric_manifest *)malloc(sizeof(struct metric_manifest));
    info->name = strdup(name);
    info->paras = para;
    info->type = type;
    info->id = -1;
    return info;
}

void metric_info_free(struct metric_manifest *info)
{
    free(info->name);
    free(info->paras);
    free(info);
}

struct metric *metric_new(const char *name, enum metric_type type, struct metric_parameter *para)
{
    struct metric *pthis = (struct metric *)calloc(1, sizeof(struct metric));
    pthis->info = metric_info_new(name, type, para);
    pthis->scheme = &g_metric_scheme_table[type]; // todo: 改成Switch type
    pthis->data = NULL;

    return pthis;
}

void metric_free(struct metric *pthis)
{
    if (pthis == NULL) {
        return;
    }

    if (pthis->data) {
        pthis->scheme->free(pthis->data);
    }
    metric_info_free(pthis->info);
    free(pthis);
}

void metric_reset(struct metric *pthis) {
    pthis->scheme->reset(pthis->data);
}

void metric_info_copy(struct metric_manifest *dest, const struct metric_manifest *src)
{
    dest->name = strdup(src->name);
    if (src->paras == NULL) {
        dest->paras = NULL;
    } else {
        dest->paras = (struct metric_parameter *)malloc(sizeof(struct metric_parameter));
        memcpy(dest->paras, src->paras, sizeof(struct metric_parameter));
    }

    dest->type = src->type;
    dest->id = src->id;
}

struct metric *metric_fork(const struct metric *src) {
    struct metric *dest = (struct metric *)malloc(sizeof(struct metric));
    dest->info = (struct metric_manifest *)malloc(sizeof(struct metric_manifest));
    metric_info_copy(dest->info, src->info);
    dest->scheme = &g_metric_scheme_table[src->info->type];

    dest->data = dest->scheme->new(dest->info->paras);

    return dest;
}

struct metric *metric_copy(const struct metric *src) {
    struct metric *dest = metric_fork(src);
    src->scheme->copy(dest->data, src->data);

    return dest;
}

//return -1 when merge error. 0 when success. 1 when src has no cell.
int metric_merge(struct metric *dest, const struct metric *src) {
    return dest->scheme->merge(dest->data, src->data);
}

void metric_get_plain_blob(const struct metric *pthis, char **blob, size_t *blob_size) {
    struct metric_data *data = pthis->data;
    if (pthis->info->type == METRIC_TYPE_HLL) {
        ST_hyperloglog_serialize_for_networking(data->hll, blob, blob_size);
        return;
    }
    if (pthis->info->type == METRIC_TYPE_HISTOGRAM) {
        histogram_encode_into_b64(data->hdr, blob, blob_size);
        return;
    }
    pthis->scheme->serialize(data, blob, blob_size);
}

/* -------------------------------------------------------------------------- */
/*                          specific metric operations                                */ 
/* -------------------------------------------------------------------------- */

struct metric *metric_counter_new(const char *name) {
    return metric_new(name, METRIC_TYPE_COUNTER, NULL);
}

void metric_counter_incrby(struct metric *pthis, long long value) {
    struct metric_counter_or_gauge *counter = pthis->data->counter;
    counter->value += value;
}

void metric_counter_set(struct metric *pthis, long long value) {
    struct metric_counter_or_gauge *counter = pthis->data->counter;
    counter->value = value;
}

long long metric_counter_get(const struct metric *pthis) {
    return pthis->data->counter->value;
}

struct metric *metric_hll_new(const char *name, unsigned char precision) {
    struct metric_parameter *para = construct_parameters(METRIC_TYPE_HLL, precision);
    return metric_new(name, METRIC_TYPE_HLL, para);
}

void metric_hll_add(struct metric *pthis, const char *key, size_t key_len) {
    ST_hyperloglog_add(pthis->data->hll, key, key_len);
}

double metric_hll_get(const struct metric *pthis) {
    return ST_hyperloglog_count(pthis->data->hll);
}

struct metric *metric_histogram_new(const char *name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures) {
    struct metric_parameter *para = construct_parameters(METRIC_TYPE_HISTOGRAM, lowest_trackable_value, highest_trackable_value, significant_figures);
    return metric_new(name, METRIC_TYPE_HISTOGRAM, para);
}

int metric_histogram_record(struct metric *pthis, long long value) {
    if (value > pthis->info->paras->hdr.highest_trackable_value) {
        value = pthis->info->paras->hdr.highest_trackable_value;
    }
    bool ret = hdr_record_value(pthis->data->hdr, value);
    if (!ret) {
        return -1;
    }
    return 0;
}

long long metric_histogram_value_at_percentile(const struct metric *pthis, double percentile) {
    return hdr_value_at_percentile(pthis->data->hdr, percentile);
}

long long metric_histogram_count_le_value(const struct metric *pthis, long long value) {
    return hdr_count_le_value(pthis->data->hdr, value);
}

const char *metric_get_name(const struct metric *metrics)
{
    return metrics->info->name;
}

enum metric_type metric_get_type(const struct metric *metrics)
{
    return metrics->info->type;
}