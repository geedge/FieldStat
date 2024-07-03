
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "cube.h"
#include "metric_manifest.h"
#include "metric.h"
#include "heavy_keeper.h"
#include "tag_map.h"

#define DEFAULT_N_METRIC 32

struct exdata_new_args {
    const struct fieldstat_tag *tags;
    size_t n_tags;
};

struct cell {
    struct metric **metrics;
    size_t max_n_metric;
    struct fieldstat_tag_list tags; // cell identifier
};

struct cube {
    enum sampling_mode sampling_mode;
    union {
        struct heavy_keeper *topk;
        struct tag_map *comprehensive;
    };
    size_t max_n_cell;

    // the key of cube is the combination of shared tags
    struct fieldstat_tag *cube_identifier;
    size_t n_shared_tags;

    int primary_metric_id;
};

static void tag_array_copy(struct fieldstat_tag *tags_dst, const struct fieldstat_tag *tags_src, size_t n_tag)
{
    for (size_t i = 0; i < n_tag; i++) {
        tags_dst[i].key = strdup(tags_src[i].key);
        tags_dst[i].type = tags_src[i].type;
        switch (tags_src[i].type)
        {
        case TAG_INTEGER:
            tags_dst[i].value_longlong = tags_src[i].value_longlong;
            break;
        case TAG_CSTRING:
            tags_dst[i].value_str = strdup(tags_src[i].value_str);
            break;
        case TAG_DOUBLE:
            tags_dst[i].value_double = tags_src[i].value_double;
            break;
        default:
            break;
        }
    }
}

static void fieldstat_free_tag_array(struct fieldstat_tag *tags, size_t n_tags)
{
    for (size_t i = 0; i < n_tags; i++) {
        struct fieldstat_tag *tag = &tags[i];
        free((char *)tag->key);
        if (tag->type == TAG_CSTRING) {
            free((char *)tag->value_str);
        }
    }
    free(tags);
}


struct metric *find_metric_in_cell(const struct cell *cell, int metric_id)
{
    if (metric_id >= cell->max_n_metric) {
        return NULL;
    }
    return cell->metrics[metric_id];
}

void add_metric_to_cell(struct cell *cell, struct metric *metric, int metric_id)
{
    if (metric_id >= cell->max_n_metric) {
        cell->metrics = realloc(cell->metrics, sizeof(struct metric *) * cell->max_n_metric * 2);
        memset(cell->metrics + cell->max_n_metric, 0, sizeof(struct metric *) * cell->max_n_metric);
        cell->max_n_metric *= 2;
    }

    cell->metrics[metric_id] = metric;
}

struct metric *add_or_find_metric_in_cell(const struct metric_manifest *manifest, struct cell *cell)
{
    struct metric *metric = find_metric_in_cell(cell, manifest->id);
    if (metric != NULL) {
        return metric;
    }
   
    metric = metric_new(manifest);
    add_metric_to_cell(cell, metric, manifest->id);
    return metric;
}

struct cell *cell_new(const struct exdata_new_args *args) {
    struct cell *pthis = malloc(sizeof(struct cell));
    pthis->metrics = calloc(DEFAULT_N_METRIC, sizeof(struct metric *));
    pthis->max_n_metric = DEFAULT_N_METRIC;

    pthis->tags.n_tag = args->n_tags;
    pthis->tags.tag = malloc(sizeof(struct fieldstat_tag) * args->n_tags);
    tag_array_copy(pthis->tags.tag, args->tags, args->n_tags);
    return pthis;
}

void cell_free(struct cell *pthis) {
    for (size_t i = 0; i < pthis->max_n_metric; i++) {
        metric_free(pthis->metrics[i]);
    }
    free(pthis->metrics);
    for (size_t i = 0; i < pthis->tags.n_tag; i++) {
        free((char *)pthis->tags.tag[i].key);
        if (pthis->tags.tag[i].type == TAG_CSTRING) {
            free((char *)pthis->tags.tag[i].value_str);
        }
    }
    free(pthis->tags.tag);
    free(pthis);
}

struct cell *cell_copy(const struct cell *src) {
    struct cell *pthis = malloc(sizeof(struct cell));
    pthis->metrics = calloc(src->max_n_metric, sizeof(struct metric *));
    pthis->max_n_metric = src->max_n_metric;
    for (size_t i = 0; i < src->max_n_metric; i++) {
        if (src->metrics[i] == NULL) {
            continue;
        }

        pthis->metrics[i] = metric_copy(src->metrics[i]);
    }
    
    pthis->tags.n_tag = src->tags.n_tag;
    pthis->tags.tag = malloc(sizeof(struct fieldstat_tag) * src->tags.n_tag);
    tag_array_copy(pthis->tags.tag, src->tags.tag, src->tags.n_tag);

    return pthis;
}

void cell_reset(struct cell *pthis) {
    for (size_t i = 0; i < pthis->max_n_metric; i++) {
        if (pthis->metrics[i] == NULL) {
            continue;
        }
        metric_reset(pthis->metrics[i]);
    }
}

void cell_merge(struct cell *dest, const struct cell *src) {
    for (size_t i = 0; i < src->max_n_metric; i++) {
        const struct metric *metric_src = src->metrics[i];
        if (metric_src == NULL) {
            continue;
        }
        struct metric *metric_dst = find_metric_in_cell(dest, i);

        if (metric_dst == NULL) {
            metric_dst = metric_copy(metric_src);
            add_metric_to_cell(dest, metric_dst, i);
        } else {
            metric_merge(metric_dst, metric_src);
        }
    }
}

void *exdata_new_i(void *arg) {
    return cell_new((struct exdata_new_args *)arg);
}

void exdata_free_i(void *exdata) {
    cell_free((struct cell *)exdata);
}

void exdata_reset_i(void *exdata) {
    cell_reset((struct cell *)exdata);
}

void exdata_merge_i(void *dest, void *src) {
    cell_merge((struct cell *)dest, (struct cell *)src);
}

void *exdata_copy_i(void *exdata) {
    return cell_copy((struct cell *)exdata);
}

struct cube *fieldstat_cube_info_init(const struct fieldstat_tag *shared_tags, size_t n_tag, enum sampling_mode mode, size_t max_n_cell)
{
    struct cube *cube = calloc(1, sizeof(struct cube));
    cube->sampling_mode = mode;

    if (n_tag == 0) {
        cube->cube_identifier = NULL;
    } else {
        cube->cube_identifier = malloc(sizeof(struct fieldstat_tag) * n_tag);
        tag_array_copy(cube->cube_identifier, shared_tags, n_tag);
    }

    cube->n_shared_tags = n_tag;

    cube->max_n_cell = max_n_cell;

    return cube;
}

struct cube *cube_new(const struct fieldstat_tag *shared_tags, size_t n_tag, enum sampling_mode mode, size_t max_n_cell)
{
    struct cube *cube = fieldstat_cube_info_init(shared_tags, n_tag, mode, max_n_cell);

    switch (mode)
    {
        case SAMPLING_MODE_TOPK:
            cube->topk = heavy_keeper_new(max_n_cell);
            heavy_keeper_set_exdata_schema(cube->topk, exdata_new_i, exdata_free_i, exdata_merge_i, exdata_reset_i, exdata_copy_i);
            break;
        case SAMPLING_MODE_COMPREHENSIVE:
            cube->comprehensive = tag_map_new(max_n_cell);
            tag_map_set_exdata_schema(cube->comprehensive, exdata_new_i, exdata_free_i, exdata_merge_i, exdata_reset_i, exdata_copy_i);
            break;
        default:
            assert(0);
            break;
    }

    return cube;
}

void cube_free(struct cube *cube) {
    switch (cube->sampling_mode)
    {
    case SAMPLING_MODE_TOPK:
        heavy_keeper_free(cube->topk);
        break;
    case SAMPLING_MODE_COMPREHENSIVE:
        tag_map_free(cube->comprehensive);
        break;
    default:
        assert(0);
        break;
    }

    fieldstat_free_tag_array(cube->cube_identifier, cube->n_shared_tags);

    free(cube);
}

void cube_reset(struct cube *cube) {
    if (cube->sampling_mode == SAMPLING_MODE_TOPK) {
        heavy_keeper_reset(cube->topk);
    } else {
        tag_map_reset(cube->comprehensive);
    }
}

void cube_set_primary_metric(struct cube *cube, int metric_id) {
    cube->primary_metric_id = metric_id;
}

struct cell *find_or_add_exdata_comprehensive(struct tag_map *comprehensive, const char *key, size_t key_len, struct exdata_new_args *args)
{
    struct cell *cell_data = tag_map_get0_exdata(comprehensive, key, key_len);
    if (cell_data == NULL) {
        int tmp_ret = tag_map_add(comprehensive, key, key_len, (void *)args);
        if (tmp_ret != 1) {
            return NULL;
        }
        cell_data = tag_map_get0_exdata(comprehensive, key, key_len);
    }
    return cell_data;
}

struct cell *find_or_add_exdata_none_primary_topk(struct heavy_keeper *topk, const char *key, size_t key_len, struct exdata_new_args *args)
{
    struct cell *cell_data = heavy_keeper_get0_exdata(topk, key, key_len);
    if (cell_data == NULL) {
        int tmp_ret = heavy_keeper_add(topk, key, key_len, 0, (void *)args);
        if (tmp_ret != 1) {
            return NULL;
        }
        cell_data = heavy_keeper_get0_exdata(topk, key, key_len);
    }
    return cell_data;
}


int cube_histogram_record(struct cube *cube, const struct metric_manifest *manifest, const struct fieldstat_tag *tags, size_t n_tag, long long value) {
    char *tag_in_string;
    size_t tag_len;
    build_dynamic_cell_key(tags, n_tag, &tag_in_string, &tag_len);

    struct exdata_new_args args;
    args.tags = tags;
    args.n_tags = n_tag;

    struct cell *cell_data = NULL;
    switch (cube->sampling_mode) {
    case SAMPLING_MODE_TOPK:
        cell_data = find_or_add_exdata_none_primary_topk(cube->topk, tag_in_string, tag_len, &args);
        break;
    case SAMPLING_MODE_COMPREHENSIVE:
        cell_data = find_or_add_exdata_comprehensive(cube->comprehensive, tag_in_string, tag_len, &args);
        break;
    }
    free(tag_in_string);

    if (cell_data == NULL) {
        return FS_ERR_TOO_MANY_CELLS;
    }
    struct metric *metric = add_or_find_metric_in_cell(manifest, cell_data);

    int ret = metric_histogram_record(metric, value);
    if (ret < 0) {
        return FS_ERR_INVALID_PARAM;
    }
    return FS_OK;
}

int cube_hll_add(struct cube *cube, const struct metric_manifest *manifest, const struct fieldstat_tag *tags, size_t n_tag, const char *key, size_t key_len) {
    char *tag_in_string;
    size_t tag_len;
    build_dynamic_cell_key(tags, n_tag, &tag_in_string, &tag_len);

    struct exdata_new_args args;
    args.tags = tags;
    args.n_tags = n_tag;

    struct cell *cell_data = NULL;
    switch (cube->sampling_mode) {
    case SAMPLING_MODE_TOPK:
        cell_data = find_or_add_exdata_none_primary_topk(cube->topk, tag_in_string, tag_len, &args);
        break;
    case SAMPLING_MODE_COMPREHENSIVE:
        cell_data = find_or_add_exdata_comprehensive(cube->comprehensive, tag_in_string, tag_len, &args);
        break;
    }

    if (cell_data == NULL) {
        return FS_ERR_TOO_MANY_CELLS;
    }
    struct metric *metric = add_or_find_metric_in_cell(manifest, cell_data);

    metric_hll_add(metric, key, key_len);
    free(tag_in_string);

    return FS_OK;
}

int cube_counter_incrby(struct cube *cube, const struct metric_manifest *manifest, const struct fieldstat_tag *tags, size_t n_tag, long long increment) {
    char *tag_in_string;
    size_t tag_len;
    build_dynamic_cell_key(tags, n_tag, &tag_in_string, &tag_len);

    struct exdata_new_args args;
    args.tags = tags;
    args.n_tags = n_tag;

    struct cell *cell_data = NULL;
    switch (cube->sampling_mode)
    {
    case SAMPLING_MODE_TOPK:
        if (cube->primary_metric_id != manifest->id) {
            cell_data = heavy_keeper_get0_exdata(cube->topk, tag_in_string, tag_len);

            if (cell_data == NULL) {
                int tmp_ret = heavy_keeper_add(cube->topk, tag_in_string, tag_len, 0, &args);
                if (tmp_ret != 1) {
                    free(tag_in_string);
                    return FS_ERR_TOO_MANY_CELLS;
                }
                cell_data = heavy_keeper_get0_exdata(cube->topk, tag_in_string, tag_len);
            }
        } else {
            if (increment < 0) {
                free(tag_in_string);
                return FS_ERR_INVALID_PARAM;
            } else if (increment == 0) {
                free(tag_in_string);
                return FS_OK;
            }

            // heavy_keeper_add should be called anyway, to let the topk record update.
            int tmp_ret = heavy_keeper_add(cube->topk, tag_in_string, tag_len, increment, &args);
            if (tmp_ret != 1) {
                free(tag_in_string);
                return FS_ERR_TOO_MANY_CELLS;
            }
            cell_data = heavy_keeper_get0_exdata(cube->topk, tag_in_string, tag_len);
        }
        break;
    case SAMPLING_MODE_COMPREHENSIVE:
        cell_data = find_or_add_exdata_comprehensive(cube->comprehensive, tag_in_string, tag_len, &args);
        if (cell_data == NULL) {
            free(tag_in_string);
            return FS_ERR_TOO_MANY_CELLS;
        }
        break;
    default:
        assert(0);
        break;
    }

    struct metric *metric = add_or_find_metric_in_cell(manifest, cell_data);

    metric_counter_incrby(metric, increment);
    free(tag_in_string);
    return FS_OK;
}

int cube_counter_set(struct cube *cube, const struct metric_manifest *manifest, const struct fieldstat_tag *tags, size_t n_tag, long long value) {
    char *tag_in_string;
    size_t tag_len;
    build_dynamic_cell_key(tags, n_tag, &tag_in_string, &tag_len);

    struct exdata_new_args args;
    args.tags = tags;
    args.n_tags = n_tag;
    int metric_id = manifest->id;

    struct cell *cell_data = NULL;
    switch (cube->sampling_mode)
    {
    case SAMPLING_MODE_TOPK: { // TODO: 这个地方想办法拿到值以后进counter incrby 流程，重构
        if (cube->primary_metric_id != metric_id) {
            cell_data = find_or_add_exdata_none_primary_topk(cube->topk, tag_in_string, tag_len, &args);
        } else {
            long long current_count = 0;
            cell_data = heavy_keeper_get0_exdata(cube->topk, tag_in_string, tag_len);
            if (cell_data != NULL) {
                const struct metric *tmp_metric = find_metric_in_cell(cell_data, metric_id);
                if (tmp_metric != NULL) {
                    current_count = metric_counter_get(tmp_metric);
                }
            }
            long long increment = value - current_count;
            if (increment < 0) {
                free(tag_in_string);
                return FS_ERR_INVALID_PARAM;
            } else if (increment == 0) {
                free(tag_in_string);
                return FS_OK;
            }
            
            int tmp_ret = heavy_keeper_add(cube->topk, tag_in_string, tag_len, increment, &args);
            if (tmp_ret != 1) {
                return FS_ERR_TOO_MANY_CELLS;
            }
            cell_data = heavy_keeper_get0_exdata(cube->topk, tag_in_string, tag_len);
        }
        break;}
    case SAMPLING_MODE_COMPREHENSIVE: {
        cell_data = find_or_add_exdata_comprehensive(cube->comprehensive, tag_in_string, tag_len, &args);
        break;}
    default:
        assert(0);
        break;
    }

    struct metric *metric = add_or_find_metric_in_cell(manifest, cell_data);
    free(tag_in_string);
    metric_counter_set(metric, value);
    return FS_OK;
}

struct cube *cube_copy(const struct cube *cube)
{
    struct cube *cube_dup = fieldstat_cube_info_init(cube->cube_identifier, cube->n_shared_tags, cube->sampling_mode, cube->max_n_cell);
    cube_dup->primary_metric_id = cube->primary_metric_id;

    switch (cube->sampling_mode)
    {
    case SAMPLING_MODE_TOPK:
        cube_dup->topk = heavy_keeper_copy(cube->topk);
        break;
    case SAMPLING_MODE_COMPREHENSIVE:
        cube_dup->comprehensive = tag_map_copy(cube->comprehensive);
        break;
    default:
        assert(0);
        break;
    }

    return cube_dup;
}

void cube_merge(struct cube *dest, const struct cube *src)
{
    assert(dest->sampling_mode == src->sampling_mode);

    switch (dest->sampling_mode)
    {
    case SAMPLING_MODE_TOPK:
        heavy_keeper_merge(dest->topk, src->topk);
        break;
    case SAMPLING_MODE_COMPREHENSIVE:
        tag_map_merge(dest->comprehensive, src->comprehensive);
        break;
    default:
        assert(0);
        break;
    }
}

struct cube *cube_fork(const struct cube *cube) {
    struct cube *ret = cube_new(cube->cube_identifier, cube->n_shared_tags, cube->sampling_mode, cube->max_n_cell);
    ret->primary_metric_id = cube->primary_metric_id;

    return ret;
}

void cube_get_cells(const struct cube *cube, struct fieldstat_tag_list **tag_list, size_t *n_cell)
{
    size_t n_cell_tmp = 0;
    switch (cube->sampling_mode) {
        case SAMPLING_MODE_COMPREHENSIVE:
            n_cell_tmp = tag_map_get_count(cube->comprehensive);
            break;
        case SAMPLING_MODE_TOPK:
            n_cell_tmp = heavy_keeper_get_count(cube->topk);
            break;
        default:
            assert(0);
    }

    if (n_cell_tmp == 0) {
        *tag_list = NULL;
        *n_cell = 0;
        return;
    }

    struct cell **cell_datas = (struct cell **)malloc(sizeof(struct cell *) * n_cell_tmp);
    switch (cube->sampling_mode) {
        case SAMPLING_MODE_COMPREHENSIVE:
            tag_map_list(cube->comprehensive, (void **)cell_datas, n_cell_tmp);
            break;
        case SAMPLING_MODE_TOPK:
            heavy_keeper_list(cube->topk, (void **)cell_datas, n_cell_tmp);
            break;
        default:
            assert(0);
    }

    struct fieldstat_tag_list *tag_list_ret = (struct fieldstat_tag_list *)malloc(sizeof(struct fieldstat_tag_list) * n_cell_tmp);
    *tag_list = tag_list_ret;
    *n_cell = n_cell_tmp;

    for (int i = 0; i < n_cell_tmp; i++) {
        struct cell *cell_data = cell_datas[i];
        struct fieldstat_tag_list *tag_list_tmp = &tag_list_ret[i];
        tag_list_tmp->n_tag = cell_data->tags.n_tag;
        if (tag_list_tmp->n_tag == 0) {
            tag_list_tmp->tag = NULL;
            continue;
        }
        tag_list_tmp->tag = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * tag_list_tmp->n_tag);
        tag_array_copy(tag_list_tmp->tag, cell_data->tags.tag, tag_list_tmp->n_tag);
    }

    free(cell_datas);
}

const struct cell *get_cell_by_tag_list(const struct cube *cube, const struct fieldstat_tag_list *tags)
{
    const struct cell *ret = NULL;
    char *tag_in_string;
    size_t tag_len;
    build_dynamic_cell_key(tags->tag, tags->n_tag, &tag_in_string, &tag_len);

    switch (cube->sampling_mode)
    {
    case SAMPLING_MODE_TOPK:
        ret = heavy_keeper_get0_exdata(cube->topk, tag_in_string, tag_len);
        break;
    case SAMPLING_MODE_COMPREHENSIVE:
        ret = tag_map_get0_exdata(cube->comprehensive, tag_in_string, tag_len);
        break;
    default:
        assert(0);
        return NULL;
    }
    free(tag_in_string);

    return ret;
}

const struct metric *get_metric_by_tag_list(const struct cube *cube, const struct fieldstat_tag_list *tags, int metric_id,int *ret)
{
    const struct cell *data = get_cell_by_tag_list(cube, tags);
    
    if (data == NULL) {
        *ret = FS_ERR_INVALID_TAG;
        return NULL;
    }

    if (metric_id < 0 || metric_id >= data->max_n_metric) {
        *ret = FS_ERR_INVALID_METRIC_ID;
        return NULL;
    }
    *ret = FS_OK;

    return data->metrics[metric_id];
}

int cube_counter_get(const struct cube *cube, int metric_id, const struct fieldstat_tag_list *tags, long long *value)
{
    int ret;
    const struct metric *metric = get_metric_by_tag_list(cube, tags, metric_id, &ret);
    if (ret != FS_OK) {
        return ret;
    }
    if (metric == NULL) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    *value = metric_counter_get(metric);
    return FS_OK;
}

int cube_hll_get(const struct cube *cube, int metric_id, const struct fieldstat_tag_list *tags, double *value)
{
    int ret;
    const struct metric *metric = get_metric_by_tag_list(cube, tags, metric_id, &ret);
    if (ret != FS_OK) {
        return ret;
    }
    if (metric == NULL) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    *value = metric_hll_get(metric);
    return FS_OK;
}

int cube_histogram_value_at_percentile(const struct cube *cube, int metric_id, const struct fieldstat_tag_list *tags, double percentile, long long *value)
{
    int ret;
    const struct metric *metric = get_metric_by_tag_list(cube, tags, metric_id, &ret);
    if (ret != FS_OK) {
        return ret;
    }
    if (metric == NULL) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    *value = metric_histogram_value_at_percentile(metric, percentile);
    return FS_OK;
}

int cube_histogram_count_le_value(const struct cube *cube, int metric_id, const struct fieldstat_tag_list *tags, long long value, long long *count) {
    int ret;
    const struct metric *metric = get_metric_by_tag_list(cube, tags, metric_id, &ret);
    if (ret != FS_OK) {
        return ret;
    }
    if (metric == NULL) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    *count = metric_histogram_count_le_value(metric, value);
    return FS_OK;
}

int cube_get_serialization(const struct cube *cube, int metric_id, const struct fieldstat_tag_list *tags, char **blob, size_t *blob_size) {
    int ret;
    const struct metric *metric = get_metric_by_tag_list(cube, tags, metric_id, &ret);
    if (ret != FS_OK) {
        return ret;
    }
    if (metric == NULL) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    metric_get_plain_blob(metric, blob, blob_size);
    return FS_OK;
}

int cube_get_cell_count(const struct cube *cube) {
    switch (cube->sampling_mode) {
        case SAMPLING_MODE_COMPREHENSIVE:
            return tag_map_get_count(cube->comprehensive);
        case SAMPLING_MODE_TOPK:
            return heavy_keeper_get_count(cube->topk);
        default:
            return FS_ERR_INVALID_PARAM;
    }
}

void cube_get_cells_used_by_metric(const struct cube *cube, const struct fieldstat_tag_list *tags, int **metric_id_out, size_t *n_metric_out) {
    const struct cell *cell_data = get_cell_by_tag_list(cube, tags);
    if (cell_data == NULL) {
        *metric_id_out = NULL;
        *n_metric_out = 0;
        return;
    }

    *metric_id_out = (int *)malloc(sizeof(int) * cell_data->max_n_metric);
    int n_metric = 0;
    for (int i = 0; i < cell_data->max_n_metric; i++) {
        if (cell_data->metrics[i] != NULL) {
            (*metric_id_out)[n_metric] = i;
            n_metric++;
        }
    }
    *n_metric_out = n_metric;
}

struct fieldstat_tag_list *cube_get_identifier(const struct cube *cube) {
    struct fieldstat_tag_list *tag_list = (struct fieldstat_tag_list *)malloc(sizeof(struct fieldstat_tag_list));

    if (cube->n_shared_tags == 0) {
        tag_list->tag = NULL;
        tag_list->n_tag = 0;
        return tag_list;
    }

    tag_list->tag = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * cube->n_shared_tags);
    tag_list->n_tag = cube->n_shared_tags;
    tag_array_copy(tag_list->tag, cube->cube_identifier, cube->n_shared_tags);

    return tag_list;
}