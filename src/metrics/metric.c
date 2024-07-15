
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <limits.h>

#include "histogram_encoder.h"
#include "serializer.h"
#include "metric.h"
#include "hdr/hdr_histogram.h"
#include "hdr/hdr_histogram_log.h"
#include "hyperloglog.h"
#include "metric_manifest.h"

struct metric_counter_or_gauge {
    long long value;
};

typedef struct metric_data *(*metric_func_new)(const union metric_parameter *);
typedef void (*metric_func_free)(struct metric_data *);
typedef int (*metric_func_merge)(struct metric_data *, const struct metric_data *);
typedef struct metric_data * (*metric_func_copy)(const struct metric_data *);
typedef void (*metric_func_serialize)(const struct metric_data *, char **/*blob*/, size_t */*blob_size*/);
// typedef struct metric_data * (*metric_func_deserialize)(const char *, size_t );
typedef void (*metric_func_reset)(struct metric_data *);
struct metric_scheme {
    metric_func_new new;
    metric_func_free free;
    metric_func_merge merge;
    metric_func_copy copy;
    metric_func_serialize serialize;
    // metric_func_deserialize deserialize;
    metric_func_reset reset;
};

struct metric_data {
    union {
        struct metric_counter_or_gauge *counter;
        struct hyperloglog *hll;
        struct hdr_histogram *hdr;
    };
};

struct metric {
    enum metric_type type;
    struct metric_data *data;
};

/* -------------------------------------------------------------------------- */
/*                                   scheme                                   */
/* -------------------------------------------------------------------------- */

/* --------------------------------- counter -------------------------------- */

struct metric_data *metric_scheme_counter_new(const union metric_parameter *_para)
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

static struct metric_data *metric_scheme_counter_copy(const struct metric_data *from)
{
    struct metric_data *pthis = (struct metric_data *)malloc(sizeof(struct metric_data));
    struct metric_counter_or_gauge *counter = (struct metric_counter_or_gauge *)malloc(sizeof(struct metric_counter_or_gauge));
    pthis->counter = counter;

    counter->value = from->counter->value;
    return pthis;
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

struct metric_data *metric_scheme_hll_new(const union metric_parameter *para)
{
    struct metric_data *data = (struct metric_data *)malloc(sizeof(struct metric_data));
    struct hyperloglog *hll = hyperloglog_new(para->hll.precision);
    data->hll = hll;
    return data;
}

static void metric_scheme_hll_free(struct metric_data *data)
{
    hyperloglog_free(data->hll);
    free(data);
}

static void metric_scheme_hll_serialize(const struct metric_data *data, char **blob, size_t *blob_size)
{
    hyperloglog_serialize(data->hll, blob, blob_size);
}

static int metric_scheme_hll_merge(struct metric_data *pthis, const struct metric_data *from)
{
    return hyperloglog_merge(pthis->hll, from->hll);
}

struct metric_data *metric_scheme_hll_copy(const struct metric_data *from)
{
    struct metric_data *pthis = (struct metric_data *)malloc(sizeof(struct metric_data));
    struct hyperloglog *hll = hyperloglog_new(from->hll->cfg.precision);
    pthis->hll = hll;
    hyperloglog_merge(hll, from->hll);
    return pthis;
}

struct metric_data *metric_scheme_hll_deserialize(const char *blob, size_t blob_size) 
{
    struct metric_data *ret = (struct metric_data *)malloc(sizeof(struct metric_data));
    struct hyperloglog *hll = hyperloglog_deserialize(blob, blob_size);
    ret->hll = hll;

    return ret;
}

void metric_scheme_hll_reset(struct metric_data *data)
{
    hyperloglog_reset(data->hll);
}

/* --------------------------------- histogram -------------------------------- */

struct metric_data *metric_scheme_histogram_new(const union metric_parameter *para)
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

static struct metric_data *metric_scheme_histogram_copy(const struct metric_data *from)
{
    struct hdr_histogram* tmp_p_hdr = NULL;
    hdr_init(from->hdr->lowest_discernible_value, from->hdr->highest_trackable_value, from->hdr->significant_figures, &tmp_p_hdr);

    struct metric_data *data = (struct metric_data *)malloc(sizeof(struct metric_data));
    data->hdr = tmp_p_hdr;

    hdr_add(data->hdr, from->hdr);
    return data;
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
        // .deserialize = metric_scheme_counter_deserialize,
        .merge = metric_scheme_counter_merge,
        .copy = metric_scheme_counter_copy,
        .reset = metric_scheme_counter_reset,
    },
    [METRIC_TYPE_HLL] = {
        .new = metric_scheme_hll_new,
        .free = metric_scheme_hll_free,
        .serialize = metric_scheme_hll_serialize,
        // .deserialize = metric_scheme_hll_deserialize,
        .merge = metric_scheme_hll_merge,
        .copy = metric_scheme_hll_copy,
        .reset = metric_scheme_hll_reset,
    },
    [METRIC_TYPE_HISTOGRAM] = {
        .new = metric_scheme_histogram_new,
        .free = metric_scheme_histogram_free,
        .serialize = metric_scheme_histogram_serialize,
        // .deserialize = metric_scheme_histogram_deserialize,
        .merge = metric_scheme_histogram_merge,
        .copy = metric_scheme_histogram_copy,
        .reset = metric_scheme_histogram_reset,
    },
};


/* -------------------------------------------------------------------------- */
/*                                   metric                                   */
/* -------------------------------------------------------------------------- */


struct metric *metric_new(const struct metric_manifest *manifest)
{
    struct metric *pthis = (struct metric *)calloc(1, sizeof(struct metric));
    pthis->data = g_metric_scheme_table[manifest->type].new(manifest->parameters);
    pthis->type = manifest->type;

    return pthis;
}

void metric_free(struct metric *pthis)
{
    if (pthis == NULL) {
        return;
    }

    g_metric_scheme_table[pthis->type].free(pthis->data);
    free(pthis);
}

void metric_reset(struct metric *pthis) {
    g_metric_scheme_table[pthis->type].reset(pthis->data);
}

struct metric *metric_copy(const struct metric *src) {
    struct metric *dest = (struct metric *)malloc(sizeof(struct metric));
    dest->type = src->type;
    dest->data = g_metric_scheme_table[dest->type].copy(src->data);

    return dest;
}

//return -1 when merge error. 0 when success. 1 when src has no cell.
int metric_merge(struct metric *dest, const struct metric *src) {
    return g_metric_scheme_table[dest->type].merge(dest->data, src->data);
}

void metric_serialize(const struct metric *pthis, char **blob, size_t *blob_size) {
    struct metric_data *data = pthis->data;
    enum metric_type type = pthis->type;
    if (type == METRIC_TYPE_HLL) {
        hyperloglog_serialize_for_networking(data->hll, blob, blob_size);
        return;
    }
    if (type == METRIC_TYPE_HISTOGRAM) {
        histogram_encode_into_b64(data->hdr, blob, blob_size);
        return;
    }
    g_metric_scheme_table[type].serialize(data, blob, blob_size);
}

/* -------------------------------------------------------------------------- */
/*                          specific metric operations                                */ 
/* -------------------------------------------------------------------------- */

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

void metric_hll_add(struct metric *pthis, const char *key, size_t key_len) {
    hyperloglog_add(pthis->data->hll, key, key_len);
}

void metric_hll_add_hash(struct metric *pthis, unsigned long long hash) {
    hyperloglog_add_hash(pthis->data->hll, hash);
}

double metric_hll_get(const struct metric *pthis) {
    return hyperloglog_count(pthis->data->hll);
}

int metric_histogram_record(struct metric *pthis, long long value) {
    if (value > pthis->data->hdr->highest_trackable_value) {
        value = pthis->data->hdr->highest_trackable_value;
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
