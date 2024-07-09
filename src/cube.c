
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "uthash.h"
#define XXH_INLINE_ALL
#include "xxhash/xxhash.h"

#include "cube.h"
#include "metric_manifest.h"
#include "metric.h"
#include "heavy_keeper.h"
#include "tag_map.h"

#define DEFAULT_N_METRIC 32
#define DEFAULT_N_CUBE 64

struct exdata_new_args {
    const struct field *cell_dimensions;
    size_t n_dimensions;
};

struct cube_manager {
    struct cube *hash_table;
    
    struct cube **cube;
    size_t cube_cnt;
    size_t cube_size;
};

struct cell {
    struct metric **metrics;
    size_t metrics_len;
    size_t max_n_metric;
    struct field_list cell_dimensions;
};

struct cube {
    enum sampling_mode sampling_mode;
    union {
        struct heavy_keeper *topk;
        struct hash_table *comprehensive;
    };
    size_t max_n_cell;

    struct field *cube_dimensions;
    size_t n_dimensions;

    int primary_metric_id;
    char *key;     // the key of cube is the combination of cube_dimensions
    size_t key_len;
    int id;
    UT_hash_handle hh;
};

static struct field *field_array_duplicate(const struct field *fields_src, size_t n_field)
{
    struct field *ret = malloc(sizeof(struct field) * n_field);

    for (size_t i = 0; i < n_field; i++) {
        ret[i].key = strdup(fields_src[i].key);
        ret[i].type = fields_src[i].type;
        switch (fields_src[i].type)
        {
        case TAG_INTEGER:
            ret[i].value_longlong = fields_src[i].value_longlong;
            break;
        case TAG_CSTRING:
            ret[i].value_str = strdup(fields_src[i].value_str);
            break;
        case TAG_DOUBLE:
            ret[i].value_double = fields_src[i].value_double;
            break;
        default:
            break;
        }
    }

    return ret;
}

static void fieldstat_free_tag_array(struct field *fields, size_t n_tags)
{
    for (size_t i = 0; i < n_tags; i++) {
        struct field *field = &fields[i];
        free((char *)field->key);
        if (field->type == TAG_CSTRING) {
            free((char *)field->value_str);
        }
    }
    free(fields);
}

void add_cube_to_position(struct cube_manager *instance, struct cube *cube, int id)
{
    if (id >= instance->cube_size) {
        struct cube **old_cube_arr = instance->cube;
        instance->cube = calloc(instance->cube_size * 2, sizeof(struct cube *));
        memcpy(instance->cube, old_cube_arr, sizeof(struct cube *) * instance->cube_size);
        free(old_cube_arr);

        instance->cube_size *= 2;
    }
    instance->cube[id] = cube;

    if (id >= instance->cube_cnt) {
        instance->cube_cnt = id + 1;
    }
}

void cube_manager_free(struct cube_manager *pthis) {
    struct cube *node = NULL;
    struct cube *tmp = NULL;
    struct cube *head = pthis->hash_table;
    HASH_ITER(hh, head, node, tmp) {
        HASH_DEL(head, node);
        cube_free(node);
    }
    
    free(pthis->cube);
    free(pthis);
}

struct cube_manager *cube_manager_new() {
    struct cube_manager *pthis = (struct cube_manager *)malloc(sizeof(struct cube_manager));
    pthis->hash_table = NULL;

    pthis->cube = (struct cube **)calloc(DEFAULT_N_CUBE, sizeof(struct cube *));
    pthis->cube_cnt = 0;
    pthis->cube_size = DEFAULT_N_CUBE;
    return pthis;
}

static int field_array_to_key_safe(const struct field fields[], size_t n_tags, char *out_key, size_t out_key_size)
{
    if (n_tags == 0) {
        const char dummy[] = "no fields";
        size_t len_dummy = sizeof(dummy) - 1;
        if (out_key_size < len_dummy) {
            return -1;
        }
        memcpy(out_key, dummy, len_dummy);
        return len_dummy;
    }
	int i = 0;
	size_t used_len = 0;
	struct field *field = NULL;

    size_t remain_key_size = out_key_size;
    void *val_position = NULL;

    size_t key_len = 0;
    size_t val_len = 0;

	for(i = 0; i < (int)n_tags; i++)
	{
		field = (struct field *)&fields[i];
        key_len = strlen(field->key);
		switch(field->type)
		{
			case TAG_INTEGER:
                val_len = sizeof(long long);
                val_position = (void *)&field->value_longlong;
				break;
			case TAG_DOUBLE:
                val_len = sizeof(double);
                val_position = (void *)&field->value_double;

				break;
			case TAG_CSTRING:
                val_len = strlen(field->value_str);
                val_position = (void *)field->value_str;
				break;
			default:
				assert(0);
				break;
		}
        
        used_len = key_len + val_len;
        while (used_len >= remain_key_size) {
            return -1;
        }
        memcpy(out_key + out_key_size - remain_key_size, field->key, key_len);
        memcpy(out_key + out_key_size - remain_key_size + key_len, val_position, val_len);
        remain_key_size -= used_len;
	}

    return out_key_size - remain_key_size;
}

static void field_array_to_key_endeavor(const struct field fields[], size_t n_tags, char **out_key, size_t *out_key_size)
{
    if (n_tags == 0) {
        // use a default dummy key
        *out_key = strdup("no fields");
        *out_key_size = strlen(*out_key);
        return;
    }
	int i = 0;
	size_t used_len = 0;
	struct field *field = NULL;

    size_t alloced_every_time = 1024;
    size_t remain_key_size = 4096 + 1024;
    size_t total_key_size = 4096 + 1024;
    char *dynamic_mem = (char *)malloc(total_key_size);
    void *val_position = NULL;

    size_t key_len = 0;
    size_t val_len = 0;

	for(i = 0; i < (int)n_tags; i++)
	{
		field = (struct field *)&fields[i];
        key_len = strlen(field->key);
		switch(field->type)
		{
			case TAG_INTEGER:
                val_len = sizeof(long long);
                val_position = (void *)&field->value_longlong;
				break;
			case TAG_DOUBLE:
                val_len = sizeof(double);
                val_position = (void *)&field->value_double;

				break;
			case TAG_CSTRING:
                val_len = strlen(field->value_str);
                val_position = (void *)field->value_str;
				break;
			default:
				assert(0);
				break;
		}
        
        used_len = key_len + val_len;
        while (used_len >= remain_key_size) {
            total_key_size += alloced_every_time;
            remain_key_size += alloced_every_time;
            dynamic_mem = (char *)realloc(dynamic_mem, total_key_size);
        }
        memcpy(dynamic_mem + total_key_size - remain_key_size, field->key, key_len);
        memcpy(dynamic_mem + total_key_size - remain_key_size + key_len, val_position, val_len);
        remain_key_size -= used_len;
	}

    *out_key = dynamic_mem;
    *out_key_size = total_key_size - remain_key_size;
}

int cube_manager_add(struct cube_manager *pthis, struct cube *cube)
{
    char *key = cube->key;
    size_t key_len = cube->key_len;

    struct cube *old_cube = NULL;
    HASH_FIND(hh, pthis->hash_table, key, key_len, old_cube);
    if (old_cube != NULL) {
        return -1;
    }

    int id = 0;
    for ( ;id < pthis->cube_cnt; id++) {
        if (pthis->cube[id] == NULL) {
            break;
        }
    }

    cube->id = id;
    HASH_ADD_KEYPTR(hh, pthis->hash_table, cube->key, key_len, cube);

    add_cube_to_position(pthis, cube, id);

    return id;
}

void cube_manager_delete(struct cube_manager *pthis, struct cube *cube)
{
    // char *key = cube->key;
    // size_t key_len = cube->key_len;
    // struct cube *node = NULL;
    // HASH_FIND(hh, pthis->head, key, key_len, node);
    // if (node == NULL) {
    //     return;
    // }

    int id = cube->id;
    HASH_DEL(pthis->hash_table, cube);

    cube_free(cube);
    pthis->cube[id] = NULL;
    if (id == pthis->cube_cnt - 1) {
        pthis->cube_cnt--;
    }
}

int cube_manager_find(const struct cube_manager *pthis, const struct field *cube_dimensions, size_t n_dimension)
{
    char key_stack[4096];
    char *key = key_stack;
    int key_len = field_array_to_key_safe(cube_dimensions, n_dimension, key, sizeof(key));
    bool free_key = false;
    if (key_len < 0) {  // very unlikely to happen
        char *key_heap;
        size_t key_len_tmp;
        field_array_to_key_endeavor(cube_dimensions, n_dimension, &key_heap, &key_len_tmp);
        key = key_heap;
        key_len = key_len_tmp;
        free_key = true;
    }

    struct cube *node = NULL;
    HASH_FIND(hh, pthis->hash_table, key, key_len, node);
    if (free_key) {
        free(key);
    }
    if (node == NULL) {
        return -1;
    } else {
        return node->id;
    }
}

struct cube *cube_manager_get_cube_by_id(const struct cube_manager *manager, int cube_id) {
    if (cube_id < 0 || cube_id >= manager->cube_size) {
        return NULL;
    }
    return manager->cube[cube_id];
}

void cube_manager_list(const struct cube_manager *pthis, int **cube_ids, int *n_cube) {
    int all_available_cube_count = 0;

    int *tmp_ids = (int *)malloc(sizeof(int) * pthis->cube_cnt);
    for (int i = 0; i < pthis->cube_cnt; i++) {
        if (pthis->cube[i] != NULL) {
            tmp_ids[all_available_cube_count++] = i;
        }
    }
    if (all_available_cube_count == 0) {
        free(tmp_ids);
        *cube_ids = NULL;
        *n_cube = 0;
        return;
    }

    *cube_ids = tmp_ids;
    *n_cube = all_available_cube_count;
}

void cube_manager_calibrate(struct cube_manager *pthis, const struct cube_manager *master)
{
    struct cube *node_in_master, *node_in_dest, *tmp;

    // exist in self but not in master
    HASH_ITER(hh, pthis->hash_table, node_in_dest, tmp) {
        HASH_FIND(hh, master->hash_table, node_in_dest->key, node_in_dest->key_len, node_in_master);

        if (node_in_master == NULL) {
            cube_manager_delete(pthis, node_in_dest);
        }
    }

    // exist in master but not in self
    HASH_ITER(hh, master->hash_table, node_in_master, tmp) {
        HASH_FIND(hh, pthis->hash_table, node_in_master->key, node_in_master->key_len, node_in_dest);

        if (node_in_dest == NULL) {
            cube_manager_add(pthis, cube_fork(node_in_master));
        }
    }
}

struct cube_manager *cube_manager_fork(const struct cube_manager *src)
{
    struct cube_manager *pthis = cube_manager_new();
    struct cube *node = NULL;
    struct cube *tmp = NULL;
    HASH_ITER(hh, src->hash_table, node, tmp) {
        cube_manager_add(pthis, cube_fork(node));
    }
    return pthis;
}

void cube_manager_merge(struct cube_manager *dest, const struct cube_manager *src)
{
    struct cube *node = NULL;
    struct cube *tmp = NULL;
    HASH_ITER(hh, src->hash_table, node, tmp) {
        struct cube *node_in_dest = NULL;
        HASH_FIND(hh, dest->hash_table, node->key, node->key_len, node_in_dest);

        if (node_in_dest == NULL) {
            cube_manager_add(dest, cube_copy(node));
        } else {
            cube_merge(node_in_dest, node);
        }
    }
}

void cube_manager_reset(struct cube_manager *pthis)
{
    for (int i = 0; i < pthis->cube_cnt; i++) {
        if (pthis->cube[i] == NULL) {
            continue;
        }
        cube_reset(pthis->cube[i]);
    }
}

struct metric *find_metric_in_cell(const struct cell *cell, int metric_id)
{
    if (metric_id >= cell->metrics_len) {
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

    if (metric_id >= cell->metrics_len) {
        cell->metrics_len = metric_id + 1;
    }
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
    pthis->metrics_len = 0;

    pthis->cell_dimensions.n_field = args->n_dimensions;
    pthis->cell_dimensions.field = field_array_duplicate(args->cell_dimensions, args->n_dimensions);
    return pthis;
}

void cell_free(struct cell *pthis) {
    for (size_t i = 0; i < pthis->metrics_len; i++) {
        metric_free(pthis->metrics[i]);
    }
    free(pthis->metrics);
    for (size_t i = 0; i < pthis->cell_dimensions.n_field; i++) {
        free((char *)pthis->cell_dimensions.field[i].key);
        if (pthis->cell_dimensions.field[i].type == TAG_CSTRING) {
            free((char *)pthis->cell_dimensions.field[i].value_str);
        }
    }
    free(pthis->cell_dimensions.field);
    free(pthis);
}

struct cell *cell_copy(const struct cell *src) {
    struct cell *pthis = malloc(sizeof(struct cell));
    pthis->metrics = calloc(src->max_n_metric, sizeof(struct metric *));
    pthis->max_n_metric = src->max_n_metric;
    pthis->metrics_len = src->metrics_len;
    for (size_t i = 0; i < src->metrics_len; i++) {
        if (src->metrics[i] == NULL) {
            continue;
        }

        pthis->metrics[i] = metric_copy(src->metrics[i]);
    }
    
    pthis->cell_dimensions.n_field = src->cell_dimensions.n_field;
    pthis->cell_dimensions.field = field_array_duplicate(src->cell_dimensions.field, src->cell_dimensions.n_field);

    return pthis;
}

void cell_reset(struct cell *pthis) {
    for (size_t i = 0; i < pthis->metrics_len; i++) {
        if (pthis->metrics[i] == NULL) {
            continue;
        }
        metric_reset(pthis->metrics[i]);
    }
}

void cell_merge(struct cell *dest, const struct cell *src) {
    for (size_t i = 0; i < src->metrics_len; i++) {
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

struct cube *cube_info_new(const struct field *dimensions, size_t n_dimensions, enum sampling_mode mode, size_t max_n_cell)
{
    struct cube *cube = calloc(1, sizeof(struct cube));
    cube->sampling_mode = mode;

    if (n_dimensions == 0) {
        cube->cube_dimensions = NULL;
    } else {
        cube->cube_dimensions = field_array_duplicate(dimensions, n_dimensions);
    }

    cube->n_dimensions = n_dimensions;
    cube->max_n_cell = max_n_cell;
    field_array_to_key_endeavor(dimensions, n_dimensions, &cube->key, &cube->key_len);

    cube->id = -1;

    return cube;
}

struct cube *cube_new(const struct field *dimensions, size_t n_dimensions, enum sampling_mode mode, size_t max_n_cell)
{
    struct cube *cube = cube_info_new(dimensions, n_dimensions, mode, max_n_cell);

    switch (mode)
    {
        case SAMPLING_MODE_TOPK:
            cube->topk = heavy_keeper_new(max_n_cell);
            heavy_keeper_set_exdata_schema(cube->topk, exdata_new_i, exdata_free_i, exdata_merge_i, exdata_reset_i, exdata_copy_i);
            break;
        case SAMPLING_MODE_COMPREHENSIVE:
            cube->comprehensive = hash_table_new(max_n_cell);
            hash_table_set_exdata_schema(cube->comprehensive, exdata_new_i, exdata_free_i, exdata_merge_i, exdata_reset_i, exdata_copy_i);
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
        hash_table_free(cube->comprehensive);
        break;
    default:
        assert(0);
        break;
    }

    fieldstat_free_tag_array(cube->cube_dimensions, cube->n_dimensions);
    free(cube->key);

    free(cube);
}

void cube_reset(struct cube *cube) {
    if (cube->sampling_mode == SAMPLING_MODE_TOPK) {
        heavy_keeper_reset(cube->topk);
    } else {
        hash_table_reset(cube->comprehensive);
    }
}

void cube_set_primary_metric(struct cube *cube, int metric_id) {
    cube->primary_metric_id = metric_id;
}

struct cell *get_cell(struct cube *cube, const struct field *dimensions, size_t n_dimension, long long increment, int metric_id) {
    char key_stack[4096];
    char *key = key_stack;
    int key_len = field_array_to_key_safe(dimensions, n_dimension, key, sizeof(key));
    bool free_key = false;
    if (key_len < 0) {  // very unlikely to happen
        char *key_heap;
        size_t key_len_tmp;
        field_array_to_key_endeavor(dimensions, n_dimension, &key_heap, &key_len_tmp);
        key = key_heap;
        key_len = key_len_tmp;
        free_key = true;
    }

    struct exdata_new_args args;
    args.cell_dimensions = dimensions;
    args.n_dimensions = n_dimension;

    struct cell *cell_data = NULL;
    switch (cube->sampling_mode) {
    case SAMPLING_MODE_TOPK: {
        if (cube->primary_metric_id != metric_id) {
            cell_data = heavy_keeper_get0_exdata(cube->topk, key, key_len);
            if (cell_data == NULL) {
                int tmp_ret = heavy_keeper_add(cube->topk, key, key_len, 0, (void *)&args);
                if (tmp_ret == 1) {
                    cell_data = heavy_keeper_get0_exdata(cube->topk, key, key_len);
                }
            }
        } else {
            // heavy_keeper_add should be called anyway, to let the topk record update.
            int tmp_ret = heavy_keeper_add(cube->topk, key, key_len, increment, (void *)&args);
            if (tmp_ret == 1) {
                cell_data = heavy_keeper_get0_exdata(cube->topk, key, key_len);
            }
        }
        break;}
    case SAMPLING_MODE_COMPREHENSIVE: {
        cell_data = hash_table_get0_exdata(cube->comprehensive, key, key_len);
        if (cell_data == NULL) {
            int tmp_ret = hash_table_add(cube->comprehensive, key, key_len, (void *)&args);
            if (tmp_ret == 1) {
                cell_data = hash_table_get0_exdata(cube->comprehensive, key, key_len);
            }
        }
        break;}
    }

    if (free_key) {
        free(key);
    }
    return cell_data;
}

int cube_histogram_record(struct cube *cube, const struct metric_manifest *manifest, const struct field *dimensions, size_t n_dimensions, long long value) {
    assert(manifest->type == METRIC_TYPE_HISTOGRAM);
    assert(cube->sampling_mode == SAMPLING_MODE_COMPREHENSIVE || (cube->primary_metric_id != manifest->id));

    struct cell *cell_data = get_cell(cube, dimensions, n_dimensions, 0, manifest->id);
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

int cube_hll_add(struct cube *cube, const struct metric_manifest *manifest, const struct field *dimensions, size_t n_dimensions, const char *key, size_t key_len) {
    assert(manifest->type == METRIC_TYPE_HLL);
    assert(cube->sampling_mode == SAMPLING_MODE_COMPREHENSIVE || (cube->primary_metric_id != manifest->id));

    struct cell *cell_data = get_cell(cube, dimensions, n_dimensions, 0, manifest->id);
    if (cell_data == NULL) {
        return FS_ERR_TOO_MANY_CELLS;
    }
    struct metric *metric = add_or_find_metric_in_cell(manifest, cell_data);

    metric_hll_add(metric, key, key_len);
    return FS_OK;
}

uint64_t tags2hash(const struct field *field, size_t n_dimensions) {
    XXH3_state_t state = {0};
    XXH3_64bits_reset(&state);

    for (int i = 0; i < n_dimensions; i++) {
        XXH3_64bits_update(&state, field[i].key, strlen(field[i].key));
        if (field[i].type != TAG_CSTRING) {
            XXH3_64bits_update(&state, &field[i].value_longlong, sizeof(long long));
        } else {
            XXH3_64bits_update(&state, field[i].value_str, strlen(field[i].value_str));
        }
    }

    return XXH3_64bits_digest(&state);
}

int cube_hll_add_tag(struct cube *cube, const struct metric_manifest *manifest, const struct field *dimensions, size_t n_dimensions, const struct field *tags_key, size_t n_tag_key) 
{
    assert(manifest->type == METRIC_TYPE_HLL);
    assert(cube->sampling_mode != SAMPLING_MODE_TOPK || (cube->primary_metric_id != manifest->id));

    struct cell *cell_data = get_cell(cube, dimensions, n_dimensions, 0, manifest->id);
    if (cell_data == NULL) {
        return FS_ERR_TOO_MANY_CELLS;
    }
    struct metric *metric = add_or_find_metric_in_cell(manifest, cell_data);

    uint64_t hash = tags2hash(tags_key, n_tag_key);
    metric_hll_add_hash(metric, hash);
    return FS_OK;
}

int cube_counter_incrby(struct cube *cube, const struct metric_manifest *manifest, const struct field *dimensions, size_t n_dimensions, long long increment) {
    assert(manifest->type == METRIC_TYPE_COUNTER);
    assert(cube->sampling_mode == SAMPLING_MODE_COMPREHENSIVE || (cube->primary_metric_id != manifest->id || increment >= 0));

    struct cell *cell_data = get_cell(cube, dimensions, n_dimensions, increment, manifest->id);
    if (cell_data == NULL) {
        return FS_ERR_TOO_MANY_CELLS;
    }

    struct metric *metric = add_or_find_metric_in_cell(manifest, cell_data);

    metric_counter_incrby(metric, increment);
    return FS_OK;
}

int cube_counter_set(struct cube *cube, const struct metric_manifest *manifest, const struct field *dimensions, size_t n_dimensions, long long value) {
    assert(manifest->type == METRIC_TYPE_COUNTER);
    assert(cube->sampling_mode == SAMPLING_MODE_COMPREHENSIVE || (cube->primary_metric_id != manifest->id));

    struct cell *cell_data = get_cell(cube, dimensions, n_dimensions, 0, manifest->id);
    if (cell_data == NULL) {
        return FS_ERR_TOO_MANY_CELLS;
    }

    struct metric *metric = add_or_find_metric_in_cell(manifest, cell_data);
    metric_counter_set(metric, value);
    return FS_OK;
}

struct cube *cube_copy(const struct cube *cube)
{
    struct cube *cube_dup = cube_info_new(cube->cube_dimensions, cube->n_dimensions, cube->sampling_mode, cube->max_n_cell);
    cube_dup->primary_metric_id = cube->primary_metric_id;

    switch (cube->sampling_mode)
    {
    case SAMPLING_MODE_TOPK:
        cube_dup->topk = heavy_keeper_copy(cube->topk);
        break;
    case SAMPLING_MODE_COMPREHENSIVE:
        cube_dup->comprehensive = hash_table_copy(cube->comprehensive);
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
        hash_table_merge(dest->comprehensive, src->comprehensive);
        break;
    default:
        assert(0);
        break;
    }
}

struct cube *cube_fork(const struct cube *cube) {
    struct cube *ret = cube_new(cube->cube_dimensions, cube->n_dimensions, cube->sampling_mode, cube->max_n_cell);
    ret->primary_metric_id = cube->primary_metric_id;

    return ret;
}

void cube_get_cells(const struct cube *cube, struct field_list **cell_dimensions, size_t *n_cell)
{
    size_t n_cell_tmp = 0;
    switch (cube->sampling_mode) {
        case SAMPLING_MODE_COMPREHENSIVE:
            n_cell_tmp = hash_table_get_count(cube->comprehensive);
            break;
        case SAMPLING_MODE_TOPK:
            n_cell_tmp = heavy_keeper_get_count(cube->topk);
            break;
        default:
            assert(0);
    }

    if (n_cell_tmp == 0) {
        *cell_dimensions = NULL;
        *n_cell = 0;
        return;
    }

    struct cell **cell_datas = (struct cell **)malloc(sizeof(struct cell *) * n_cell_tmp);
    switch (cube->sampling_mode) {
        case SAMPLING_MODE_COMPREHENSIVE:
            hash_table_list(cube->comprehensive, (void **)cell_datas, n_cell_tmp);
            break;
        case SAMPLING_MODE_TOPK:
            heavy_keeper_list(cube->topk, (void **)cell_datas, n_cell_tmp);
            break;
        default:
            assert(0);
    }

    struct field_list *tag_list_ret = (struct field_list *)malloc(sizeof(struct field_list) * n_cell_tmp);
    *cell_dimensions = tag_list_ret;
    *n_cell = n_cell_tmp;

    for (int i = 0; i < n_cell_tmp; i++) {
        struct cell *cell_data = cell_datas[i];
        struct field_list *tag_list_tmp = &tag_list_ret[i];
        tag_list_tmp->n_field = cell_data->cell_dimensions.n_field;
        if (tag_list_tmp->n_field == 0) {
            tag_list_tmp->field = NULL;
            continue;
        }
        tag_list_tmp->field = field_array_duplicate(cell_data->cell_dimensions.field, tag_list_tmp->n_field);
    }

    free(cell_datas);
}

const struct cell *get_cell_by_tag_list(const struct cube *cube, const struct field_list *fields)
{
    const struct cell *ret = NULL;
    char *tag_in_string;
    size_t tag_len;
    field_array_to_key_endeavor(fields->field, fields->n_field, &tag_in_string, &tag_len);

    switch (cube->sampling_mode)
    {
    case SAMPLING_MODE_TOPK:
        ret = heavy_keeper_get0_exdata(cube->topk, tag_in_string, tag_len);
        break;
    case SAMPLING_MODE_COMPREHENSIVE:
        ret = hash_table_get0_exdata(cube->comprehensive, tag_in_string, tag_len);
        break;
    default:
        assert(0);
        return NULL;
    }
    free(tag_in_string);

    return ret;
}

const struct metric *get_metric_by_tag_list(const struct cube *cube, const struct field_list *fields, int metric_id,int *ret)
{
    const struct cell *data = get_cell_by_tag_list(cube, fields);
    
    if (data == NULL) {
        *ret = FS_ERR_INVALID_TAG;
        return NULL;
    }

    if (metric_id < 0 || metric_id >= data->metrics_len) {
        *ret = FS_ERR_INVALID_METRIC_ID;
        return NULL;
    }
    *ret = FS_OK;

    return data->metrics[metric_id];
}

int cube_counter_get(const struct cube *cube, int metric_id, const struct field_list *fields, long long *value)
{
    int ret;
    const struct metric *metric = get_metric_by_tag_list(cube, fields, metric_id, &ret);
    if (ret != FS_OK) {
        return ret;
    }
    if (metric == NULL) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    *value = metric_counter_get(metric);
    return FS_OK;
}

int cube_hll_get(const struct cube *cube, int metric_id, const struct field_list *fields, double *value)
{
    int ret;
    const struct metric *metric = get_metric_by_tag_list(cube, fields, metric_id, &ret);
    if (ret != FS_OK) {
        return ret;
    }
    if (metric == NULL) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    *value = metric_hll_get(metric);
    return FS_OK;
}

int cube_histogram_value_at_percentile(const struct cube *cube, int metric_id, const struct field_list *fields, double percentile, long long *value)
{
    int ret;
    const struct metric *metric = get_metric_by_tag_list(cube, fields, metric_id, &ret);
    if (ret != FS_OK) {
        return ret;
    }
    if (metric == NULL) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    *value = metric_histogram_value_at_percentile(metric, percentile);
    return FS_OK;
}

int cube_histogram_count_le_value(const struct cube *cube, int metric_id, const struct field_list *fields, long long value, long long *count) {
    int ret;
    const struct metric *metric = get_metric_by_tag_list(cube, fields, metric_id, &ret);
    if (ret != FS_OK) {
        return ret;
    }
    if (metric == NULL) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    *count = metric_histogram_count_le_value(metric, value);
    return FS_OK;
}

int cube_get_serialization(const struct cube *cube, int metric_id, const struct field_list *fields, char **blob, size_t *blob_size) {
    int ret;
    const struct metric *metric = get_metric_by_tag_list(cube, fields, metric_id, &ret);
    if (ret != FS_OK) {
        return ret;
    }
    if (metric == NULL) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    metric_serialize(metric, blob, blob_size);
    return FS_OK;
}

int cube_get_cell_count(const struct cube *cube) {
    switch (cube->sampling_mode) {
        case SAMPLING_MODE_COMPREHENSIVE:
            return hash_table_get_count(cube->comprehensive);
        case SAMPLING_MODE_TOPK:
            return heavy_keeper_get_count(cube->topk);
        default:
            return FS_ERR_INVALID_PARAM;
    }
}

void cube_get_cells_used_by_metric(const struct cube *cube, const struct field_list *fields, int **metric_id_out, size_t *n_metric_out) {
    const struct cell *cell_data = get_cell_by_tag_list(cube, fields);
    if (cell_data == NULL) {
        *metric_id_out = NULL;
        *n_metric_out = 0;
        return;
    }

    *metric_id_out = (int *)malloc(sizeof(int) * cell_data->metrics_len);
    int n_metric = 0;
    for (int i = 0; i < cell_data->metrics_len; i++) {
        if (cell_data->metrics[i] != NULL) {
            (*metric_id_out)[n_metric] = i;
            n_metric++;
        }
    }
    *n_metric_out = n_metric;
}

struct field_list *cube_get_identifier(const struct cube *cube) {
    struct field_list *tag_list = (struct field_list *)malloc(sizeof(struct field_list));

    if (cube->n_dimensions == 0) {
        tag_list->field = NULL;
        tag_list->n_field = 0;
        return tag_list;
    }

    tag_list->field = field_array_duplicate(cube->cube_dimensions, cube->n_dimensions);
    tag_list->n_field = cube->n_dimensions;
    

    return tag_list;
}