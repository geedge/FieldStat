
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "fs_cell.h"

#include "fieldstat.h"
#include "hdr/hdr_histogram.h"
#include "hdr/hdr_histogram_log.h"
#include "st_hyperloglog.h"
#include "mpack/mpack.h"

enum counter_metric_type {
    COUNTER_TYPE_COUNTER,
    COUNTER_TYPE_GAUGE,
};

struct para_counter {
    enum counter_metric_type type;
};

struct para_hdr{
    // const char *bins;
    long long lowest_trackable_value;
    long long highest_trackable_value;
    int significant_figures;
};

struct para_hll {
    char precision;
};

struct field_paras {
    union {
        struct para_counter counter;
        struct para_hdr hdr;
        struct para_hll hll;
    };
};

struct metric_info {
    int id;
    char *name;
    struct field_paras *paras;
    enum field_type type;
};

typedef void (*metrics_func_del)(struct metrics *);
typedef void (*metrics_func_refresh)(struct metrics *);
typedef int (*metrics_func_merge)(struct metrics *, const struct metrics *);
typedef int (*metrics_func_copy)(struct metrics *, const struct metrics *);
typedef void (*metrics_func_serialize)(const struct metrics *, char **/*blob*/, size_t */*blob_size*/);
typedef int (*metrics_func_deserialize)(struct metrics *, const char *, size_t );
struct metrics {
    metrics_func_del del;
    metrics_func_refresh refresh;
    metrics_func_merge merge;
    metrics_func_copy copy;
    metrics_func_serialize serialize;
    metrics_func_deserialize deserialize;

    struct metric_info *infos;
};

struct metric_counter {
    struct metrics base;
    long long value;
};

struct metric_hll {
    struct metrics base;
    struct ST_hyperloglog *hll;
};

struct metric_histogram {
    struct metrics base;
    struct hdr_histogram *hdr;
};




/*
{
    "type":<enum field_type>,
    "PL":<content_blob>
}
*/
void mpack_blob_wrap_basic_info(enum field_type type, const char *blob, size_t blob_size, char **new_blob, size_t *new_blob_size)
{
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, new_blob, new_blob_size);
    mpack_start_map(&writer, 2);
    mpack_write_cstr(&writer, "type");
    mpack_write_int(&writer, type);
    mpack_write_cstr(&writer, "PL");
    mpack_write_bin(&writer, blob, blob_size);
    mpack_finish_map(&writer);
    mpack_writer_destroy(&writer);
}

// user must free new_blob
void mpack_blob_unpack_basic_info(const char *blob, size_t blob_size, enum field_type *type, char **new_blob, size_t *new_blob_size)
{
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, blob_size);
    mpack_tree_parse(&tree);
    mpack_node_t root = mpack_tree_root(&tree);

    *type = mpack_node_int(mpack_node_map_cstr(root, "type"));
    mpack_node_t payload_node = mpack_node_map_cstr(root, "PL");
    *new_blob_size = mpack_node_bin_size(payload_node);
    *new_blob = (char *)malloc(*new_blob_size);
    memcpy(*new_blob, mpack_node_bin_data(payload_node), *new_blob_size);

    mpack_tree_destroy(&tree);
}

// TODO: 这个废除，改成只写一个id
void cell_serialize_write_fieldtag(mpack_writer_t *writer, const struct cell *pthis)
{
    struct fieldstat_tag *tag_list;
    size_t tag_num;
    cell_read_tag(pthis, &tag_list, &tag_num);
    // sort tag so that when merge by blob, we can merge cells with the same tags
    qsort(tag_list, tag_num, sizeof(struct fieldstat_tag), my_cmp_tag);

    mpack_write_cstr(writer, "tags");
    mpack_start_array(writer, tag_num);
    for (int j = 0; j < tag_num; j++) {
        mpack_start_map(writer, 3);
        mpack_write_cstr(writer, "key");
        mpack_write_cstr(writer, tag_list[j].key);
        mpack_write_cstr(writer, "type");
        mpack_write_i8(writer, tag_list[j].type);
        mpack_write_cstr(writer, "value");
        switch (tag_list[j].type)
        {
            case TAG_INTEGER:
                mpack_write_i64(writer, tag_list[j].value_int);
                break;
            case TAG_DOUBLE:
                mpack_write_double(writer, tag_list[j].value_double);
                break;
            case TAG_CSTRING:
                mpack_write_cstr(writer, tag_list[j].value_str);
                break;
            default:
                assert(0);
                break;
        }
        mpack_finish_map(writer);
    }
    mpack_finish_array(writer);

    free(tag_list);
}

// TODO；一样废除
void deserialize_tag(mpack_node_t *root, struct fieldstat_tag **tag_list_out, size_t *tag_num_out)
{
    mpack_node_t tag_arr = mpack_node_map_cstr(*root, "tags");
    size_t tag_num = mpack_node_array_length(tag_arr);

    struct fieldstat_tag *tag_list = malloc(sizeof(struct fieldstat_tag) * tag_num);
    for (int j = 0; j < tag_num; j++) {
        mpack_node_t tag = mpack_node_array_at(tag_arr, j);

        mpack_node_t key = mpack_node_map_cstr(tag, "key");
        mpack_node_copy_cstr(key, (char *)tag_list[j].key, MAX_TAG_KEY_LENGTH);
        tag_list[j].type = mpack_node_i8(mpack_node_map_cstr(tag, "type"));

        mpack_node_t value = mpack_node_map_cstr(tag, "value");

        switch (tag_list[j].type)
        {
            case TAG_INTEGER:
                tag_list[j].value_int = mpack_node_i64(value);
                break;
            case TAG_DOUBLE:
                tag_list[j].value_double = mpack_node_double(value);
                break;
            case TAG_CSTRING:
                mpack_node_copy_cstr(value, (char *)tag_list[j].value_str, MAX_TAG_STRING_VAL_LENGTH);
                break;
            default:
                assert(0);
                break;
        }
    }
    *tag_list_out = tag_list;
    *tag_num_out = tag_num;
}

/* -------------------------------------------------------------------------- */
/*                                   metric-counter                           */
/* -------------------------------------------------------------------------- */

static void metric_counter_refresh(struct metrics *pthis)
{
    struct metric_counter *counter = (struct metric_counter *)pthis;
    counter->value = 0;
}

static void metric_counter_free(struct metrics *pthis)
{
    struct metric_counter *counter = (struct metric_counter *)pthis;
    free(counter);
}

/*
    "counter": {
        "value": 0
    }
*/
static void metric_counter_serialize(const struct metrics *pthis, char **blob, size_t *blob_size)
{
    struct metric_counter *counter = (struct metric_counter *)pthis;
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_size);
    mpack_build_map(&writer);

    mpack_write_cstr(&writer, "value");
    mpack_write_i64(&writer, counter->value);

    mpack_complete_map(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        printf("ERR: An error occurred in counter_metric_serialize!\n");
    }
}

static int metric_counter_merge(struct metrics *pthis, const struct metrics *from)
{
    struct metric_counter *counter = (struct metric_counter *)pthis;
    struct metric_counter *from_counter = (struct metric_counter *)from;
    counter->value += from_counter->value;
    return 0;
}

static int metric_counter_copy(struct metrics *pthis, const struct metrics *from)
{
    struct metric_counter *counter = (struct metric_counter *)pthis;
    struct metric_counter *from_counter = (struct metric_counter *)from;
    counter->value = from_counter->value;
    return 0;
}

int metric_counter_deserialize(struct metrics *pthis, const char *blob, size_t blob_size) 
{
    mpack_tree_t content_tree;
    mpack_tree_init_data(&content_tree, blob, blob_size);
    mpack_tree_parse(&content_tree);
    mpack_node_t content_root = mpack_tree_root(&content_tree);

    struct metric_counter *counter = (struct metric_counter *)pthis;
    counter->value = mpack_node_i64(mpack_node_map_cstr(content_root, "value"));

    mpack_tree_destroy(&content_tree);
    return 0;
}


struct field_paras *construct_parameters(enum field_type type, ...)
{
    struct field_paras *paras = (struct field_paras *)malloc(sizeof(struct field_paras));
    va_list ap;
    va_start(ap, type);
    switch (type) {
        case FIELD_TYPE_TOPK:
            paras->topk.K = va_arg(ap, int);
            paras->topk.metric_names = va_arg(ap, char **);
            paras->topk.num_metric = va_arg(ap, size_t);
            paras->topk.primary_metric_id = va_arg(ap, size_t);
            break;
        case FIELD_TYPE_HLL:
            paras->hll.precision = (char)va_arg(ap, int);
            break;
        case FIELD_TYPE_HISTOGRAM:
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

static struct metric_counter *metric_counter_new_scheme()
{
    struct metric_counter *counter = malloc(sizeof(struct metric_counter));
    struct metrics *base = &counter->base;
    base->del = metric_counter_free;
    base->refresh = metric_counter_refresh;
    base->merge = metric_counter_merge;
    base->copy = metric_counter_copy;
    base->serialize = metric_counter_serialize;
    base->deserialize = metric_counter_deserialize;
    base->type = FIELD_TYPE_COUNTER;

    counter->value = 0;
    return counter;
}

struct metrics *metric_counter_new(const char *name, )
{
    struct metrics *ret = (struct metrics *)metric_counter_new_scheme();
    ret->infos->paras = construct_parameters()
}

/* -------------------------------------------------------------------------- */
/*                                cell-counter                                */
/* -------------------------------------------------------------------------- */

void cell_counter_incrby(struct cell *pthis, long long value)
{
    struct metric_counter *counter = (struct metric_counter *)pthis->metric;
    counter->value += value;
}

long long cell_counter_get(const struct cell *pthis)
{
    struct metric_counter *counter = (struct metric_counter *)pthis->metric;
    return counter->value;
}

struct cell *cell_create_for_counter()
{
    struct metric_counter *counter = metric_counter_new_scheme();
    struct cell *cell = malloc(sizeof(struct cell));
    memset(cell, 0, sizeof(struct cell));
    cell->metric = (struct metrics *)counter;
    return cell;
}

/* -------------------------------------------------------------------------- */
/*                                 metric-hll                         */
/* -------------------------------------------------------------------------- */

static int metric_hll_copy(struct metrics *pthis, const struct metrics *from)
{
    struct metric_hll *hll = (struct metric_hll *)pthis;
    struct metric_hll *hll_from = (struct metric_hll *)from;
    hll->hll = ST_hyperloglog_new(hll_from->hll->cfg.precision);
    return ST_hyperloglog_merge(hll->hll, hll_from->hll);
}

int metric_hll_deserialize(struct metrics *pthis, const char *blob, size_t blob_size) 
{
    struct metric_hll *hll = (struct metric_hll *)pthis;
    hll->hll = ST_hyperloglog_deserialize(blob, blob_size);

    return 0;
}

static void metric_hll_refresh(struct metrics *pthis)
{
    struct metric_hll *hll = (struct metric_hll *)pthis;
    ST_hyperloglog_reset(hll->hll);
}

static void metric_hll_free(struct metrics *pthis)
{
    struct metric_hll *hll = (struct metric_hll *)pthis;
    ST_hyperloglog_free(hll->hll);
    free(hll);
}

static void metric_hll_serialize(const struct metrics *pthis, char **blob, size_t *blob_size)
{
    struct metric_hll *hll = (struct metric_hll *)pthis;
    ST_hyperloglog_serialize(hll->hll, blob, blob_size);
}

static int metric_hll_merge(struct metrics *pthis, const struct metrics *from)
{
    struct metric_hll *hll = (struct metric_hll *)pthis;
    struct metric_hll *from_hll = (struct metric_hll *)from;
    return ST_hyperloglog_merge(hll->hll, from_hll->hll);
}

static struct metric_hll *metric_hll_new_scheme()
{
    struct metric_hll *hll = malloc(sizeof(struct metric_hll));
    struct metrics *base = &hll->base;
    base->del = metric_hll_free;
    base->refresh = metric_hll_refresh;
    base->merge = metric_hll_merge;
    base->copy = metric_hll_copy;
    base->serialize = metric_hll_serialize;
    base->deserialize = metric_hll_deserialize;
    base->type = FIELD_TYPE_HLL;

    return hll;
}

/* -------------------------------------------------------------------------- */
/*                              cell-hll                              */
/* -------------------------------------------------------------------------- */

struct cell *cell_create_for_hll(const char precision)
{
    struct cell *cell = (struct cell *)malloc(sizeof(struct cell));
    memset(cell, 0, sizeof(struct cell));

    struct metric_hll *hll = metric_hll_new_scheme();
    hll->hll = ST_hyperloglog_new(precision);

    cell->metric = (struct metrics *)hll;

    return cell;
}

int cell_hll_add(struct cell *pthis, const char *key, size_t key_len)
{
    struct metric_hll *hll = (struct metric_hll *)pthis->metric;
    return ST_hyperloglog_add(hll->hll, key, key_len);
}

double cell_hll_get(const struct cell *pthis)
{
    struct metric_hll *hll = (struct metric_hll *)pthis->metric;
    return ST_hyperloglog_count(hll->hll);
}

/* -------------------------------------------------------------------------- */
/*                              metric-histogram                              */
/* -------------------------------------------------------------------------- */
static void metric_histogram_refresh(struct metrics *pthis)
{
    struct metric_histogram *histogram = (struct metric_histogram *)pthis;
    hdr_reset(histogram->hdr);
}

static void metric_histogram_free(struct metrics *pthis)
{
    struct metric_histogram *histogram = (struct metric_histogram *)pthis;
    hdr_close(histogram->hdr);
    free(histogram);
}

static void metric_histogram_serialize(const struct metrics *pthis, char **blob, size_t *blob_size)
{
    struct metric_histogram *histogram = (struct metric_histogram *)pthis;
    hdr_log_encode(histogram->hdr, blob);
    *blob_size = strlen(*blob);
}

static int metric_histogram_merge(struct metrics *pthis, const struct metrics *from)
{
    struct metric_histogram *histogram = (struct metric_histogram *)pthis;
    struct metric_histogram *from_histogram = (struct metric_histogram *)from;
    return hdr_add(histogram->hdr, from_histogram->hdr);
}

static int metric_histogram_copy(struct metrics *pthis, const struct metrics *from)
{
    struct metric_histogram *histogram = (struct metric_histogram *)pthis;
    struct metric_histogram *from_histogram = (struct metric_histogram *)from;
    histogram->hdr = NULL;
    int ret1 = hdr_init(from_histogram->hdr->lowest_discernible_value, from_histogram->hdr->highest_trackable_value, from_histogram->hdr->significant_figures, &histogram->hdr);
    if (ret1 != 0) {
        return -1;
    }
    long ret = hdr_add(histogram->hdr, from_histogram->hdr);
    if (ret != 0) {
        return -1;
    }
    return 0;
}

static int metric_histogram_deserialize(struct metrics *pthis, const char *blob, size_t blob_size)
{
    struct metric_histogram *histogram = (struct metric_histogram *)pthis;
    histogram->hdr = NULL;
    int ret = hdr_log_decode(&histogram->hdr, (char *)blob, blob_size); // still const, just to avoid warning
    if (ret != 0 || histogram->hdr == NULL) {
        printf("ERR: metric_histogram_deserialize failed\n");
        return -1;
    }
    return 0;
}

static struct metric_histogram *metric_histogram_new_scheme()
{
    struct metric_histogram *histogram = malloc(sizeof(struct metric_histogram));
    struct metrics *base = &histogram->base;
    base->del = metric_histogram_free;
    base->refresh = metric_histogram_refresh;
    base->merge = metric_histogram_merge;
    base->copy = metric_histogram_copy;
    base->serialize = metric_histogram_serialize;
    base->deserialize = metric_histogram_deserialize;
    base->type = FIELD_TYPE_HISTOGRAM;

    return histogram;
}

/* -------------------------------------------------------------------------- */
/*                               cell-histogram                               */
/* -------------------------------------------------------------------------- */

struct cell *cell_create_for_histogram(long long lowest_trackable_value, long long highest_trackable_value, int significant_figures) 
{
    struct hdr_histogram* tmp_p_hdr = NULL;
    int ret = hdr_init((int64_t)lowest_trackable_value, (int64_t)highest_trackable_value, significant_figures, &tmp_p_hdr);
	if (ret != 0) {
        printf("ERR: hdr_init failed!\n");
        return NULL;
    }

    struct metric_histogram *histogram = metric_histogram_new_scheme();
    histogram->hdr = tmp_p_hdr;

    struct cell *cell = (struct cell *)malloc(sizeof(struct cell));
    memset(cell, 0, sizeof(struct cell));
    cell->metric = (struct metrics *)histogram;

    return cell;
}

int cell_histogram_record(struct cell *pthis, long long value)
{
    struct metric_histogram *histogram = (struct metric_histogram *)pthis->metric;
    bool ret = hdr_record_value(histogram->hdr, value);
    if (ret == false) {
        return -1;
    }
    return 0;
}

long long cell_histogram_value_at_percentile(const struct cell *pthis, double percentile)
{
    struct metric_histogram *histogram = (struct metric_histogram *)pthis->metric;

    return hdr_value_at_percentile(histogram->hdr, percentile);
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

long long cell_histogram_count_le_value(const struct cell *pthis, long long value)
{
    struct metric_histogram *histogram = (struct metric_histogram *)pthis->metric;
    return hdr_count_le_value(histogram->hdr, value);
}

/* -------------------------------------------------------------------------- */
/*                                   cell                                     */
/* -------------------------------------------------------------------------- */
// todo: 这里的接口大概要复制成现在field.c 里的
// 另外一个参考是new_field.h 里写的想法
static struct metrics *metric_scheme_new(enum field_type type)
{
    switch (type)
    {
        case FIELD_TYPE_TOPK:
            return (struct metrics *)metric_topk_new_scheme();
        case FIELD_TYPE_HISTOGRAM:
            return (struct metrics *)metric_histogram_new_scheme();
        case FIELD_TYPE_COUNTER:
            return (struct metrics *)metric_counter_new_scheme();
        case FIELD_TYPE_HLL:
            return (struct metrics *)metric_hll_new_scheme();
        default:
            assert(0);
            return NULL;
    }
}

// todo: 现在read tag 的是fieldstat
void cell_read_tag(const struct cell *pthis, struct fieldstat_tag **tag_list, size_t *tag_count)
{
    *tag_list = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * pthis->tag_num);
    for (size_t i = 0; i < pthis->tag_num; i++) {
        fieldtag_copy(&pthis->tag[i], &(*tag_list)[i]);
    }

    *tag_count = pthis->tag_num;
}

void cell_add_tag(struct cell *pthis, const struct fieldstat_tag *tag_list, size_t tag_num)
{
    if (pthis->tag_num != 0) {
        free(pthis->tag);
    }
    
    pthis->tag_num = tag_num;
    pthis->tag = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * tag_num);
    
    for (size_t i = 0; i < tag_num; i++) {
        fieldtag_copy(&(tag_list)[i], &pthis->tag[i]);
    }
}

/*
{
    cell bin blob: 
    "tags":[{
        "key":<tag key string>
        "type":0 1 or 2
        "value":<tag value, can be int(0) double(1) or str(2)>
    }]
    "c_bin":<wrapped content_blob(have type)>
    }
*/

void cell_serialize(const struct cell *pthis, char **blob, size_t *blob_size)
{
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_size);
    mpack_build_map(&writer);

    // write tags
    cell_serialize_write_fieldtag(&writer, pthis);

    // write content blob, wrap it with type info 
    char *blob_content = NULL;
    size_t blob_size_content = 0;
    pthis->metric->serialize(pthis->metric, &blob_content, &blob_size_content);
    char *wrapped_content = NULL;
    size_t wrapped_size = 0;
    mpack_blob_wrap_basic_info(pthis->metric->type, blob_content, blob_size_content, &wrapped_content, &wrapped_size);
    mpack_write_cstr(&writer, "c_bin");
    mpack_write_bin(&writer, wrapped_content, wrapped_size);

    mpack_complete_map(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        printf("ERR: An error occurred in metric_topk_serialize!\n");
    }
    free(blob_content);
    free(wrapped_content);
}

struct cell *cell_deserialize(const char *blob, size_t blob_size) 
{
    struct cell *cell = (struct cell *)malloc(sizeof(struct cell));
    memset(cell, 0, sizeof(struct cell));

    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, blob_size);
    mpack_tree_parse(&tree);
    mpack_node_t root = mpack_tree_root(&tree);
    
    struct fieldstat_tag *tag_list = NULL;
    size_t tag_num = 0;
    deserialize_tag(&root, &tag_list, &tag_num);
    cell_add_tag(cell, tag_list, tag_num);

    mpack_node_t wrapped_content_blob_node = mpack_node_map_cstr(root, "c_bin");

    enum field_type type;
    char *content_blob;
    size_t content_blob_size;
    mpack_blob_unpack_basic_info(mpack_node_bin_data(wrapped_content_blob_node), mpack_node_bin_size(wrapped_content_blob_node), &type, &content_blob, &content_blob_size);
    struct metrics *metric = metric_scheme_new(type);
    cell->metric = metric;
    metric->deserialize(metric, content_blob, content_blob_size);

    mpack_tree_destroy(&tree);
    free(tag_list);
    free(content_blob);

    return cell;
}

void cell_free(struct cell *pthis)
{
    pthis->tag_num = 0;
    free(pthis->tag);

    pthis->metric->del(pthis->metric);

    free(pthis);
}

void cell_get_plain_blob(const struct cell *pthis, char **blob, size_t *blob_size) 
{
    if (pthis->metric->type == FIELD_TYPE_HLL) {
        struct metric_hll *card = (struct metric_hll *)pthis->metric;
        ST_hyperloglog_serialize_for_networking(card->hll, blob, blob_size);
        return;
    }

    pthis->metric->serialize(pthis->metric, blob, blob_size);
}

void cell_refresh(struct cell *pthis)
{
    pthis->metric->refresh(pthis->metric);
}

int cell_merge(struct cell *pthis, const struct cell *from)
{
    return pthis->metric->merge(pthis->metric, from->metric);
}

struct cell *cell_duplicate(const struct cell *pthis)
{
    struct cell *new_cell = (struct cell *)malloc(sizeof(struct cell));
    memset(new_cell, 0, sizeof(struct cell));
    new_cell->tag_num = pthis->tag_num;
    new_cell->tag = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * new_cell->tag_num);
    for (int i = 0; i < new_cell->tag_num; i++) {
        fieldtag_copy(&pthis->tag[i], &new_cell->tag[i]);
    }

    new_cell->metric = metric_scheme_new(pthis->metric->type);
    if (new_cell->metric == NULL) {
        return NULL;
    }
    if (new_cell->metric->copy(new_cell->metric, pthis->metric) != 0) {
        return NULL;
    }

    return new_cell;
}
