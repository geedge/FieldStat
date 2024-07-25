
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>

#include "uthash.h"
#define XXH_INLINE_ALL
#include "xxhash/xxhash.h"

#include "cube.h"
#include "metric_manifest.h"
#include "metric.h"
#include "heavy_keeper.h"
#include "hash_table.h"
#include "spread_sketch.h"

#define DEFAULT_N_METRIC 32
#define DEFAULT_N_CUBE 64
static const struct timeval DUMMY_TIME_VAL = {0, 0};

struct exdata_new_args {
    const struct field *cell_dimensions;
    size_t n_dimensions;
};

struct cube_manager {
    struct cube *hash_table; // the key of cube is serialized cube dimensions
    
    struct cube **cube_slots;
    size_t next_index; // next_index
    size_t slots_number;
};

struct cell {
    struct metric **slots;
    size_t next_index; //index of next available slot
    size_t slots_number;
    struct field_list cell_dimensions;
};

struct cube {
    enum sampling_mode sampling_mode;
    union {
        struct heavy_keeper *heavykeeper;
        struct hash_table *table;
        struct spread_sketch *spread_sketch;
    };
    size_t max_n_cell;
    struct metric_manifest_manager *manifest_manager;
    
    struct field *cube_dimensions;
    size_t n_dimensions;

    int primary_metric_id;
    char *serialized_dimensions;     // the key of cube is serialized cube dimensions
    size_t serialized_dimensions_len;
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
        case FIELD_VALUE_INTEGER:
            ret[i].value_longlong = fields_src[i].value_longlong;
            break;
        case FIELD_VALUE_CSTRING:
            ret[i].value_str = strdup(fields_src[i].value_str);
            break;
        case FIELD_VALUE_DOUBLE:
            ret[i].value_double = fields_src[i].value_double;
            break;
        default:
            break;
        }
    }

    return ret;
}

void add_cube_to_position(struct cube_manager *pthis, struct cube *cube, int id)
{
    if (id >= pthis->slots_number) {
        struct cube **old_cube_arr = pthis->cube_slots;
        pthis->cube_slots = calloc(pthis->slots_number * 2, sizeof(struct cube *));
        memcpy(pthis->cube_slots, old_cube_arr, sizeof(struct cube *) * pthis->slots_number);
        free(old_cube_arr);

        pthis->slots_number *= 2;
    }
    pthis->cube_slots[id] = cube;

    if (id >= pthis->next_index) {
        pthis->next_index = id + 1;
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
    
    free(pthis->cube_slots);
    free(pthis);
}

struct cube_manager *cube_manager_new() {
    struct cube_manager *pthis = (struct cube_manager *)malloc(sizeof(struct cube_manager));
    pthis->hash_table = NULL;

    pthis->cube_slots = (struct cube **)calloc(DEFAULT_N_CUBE, sizeof(struct cube *));
    pthis->next_index = 0;
    pthis->slots_number = DEFAULT_N_CUBE;
    return pthis;
}

void print_field_array(const struct field *fields, size_t n_field)
{
    printf("dimension with length %zu \n", n_field);
    for (size_t i = 0; i < n_field; i++) {
        printf("%s: ", fields[i].key);
        switch (fields[i].type)
        {
        case FIELD_VALUE_INTEGER:
            printf("%lld\n", fields[i].value_longlong);
            break;
        case FIELD_VALUE_DOUBLE:
            printf("%lf\n", fields[i].value_double);
            break;
        case FIELD_VALUE_CSTRING:
            printf("%s\n", fields[i].value_str);
            break;
        default:
            break;
        }
    }
}

static void field_array_to_key(const struct field fields[], size_t n_fields, char **out_key, size_t *out_key_size)
{
    if (n_fields == 0) {
        // use a default dummy key
        *out_key = strdup("\a\tN");
        *out_key_size = strlen(*out_key);
        return;
    }
	int i = 0;
	size_t used_len = 0;
	struct field *field = NULL;

    size_t alloced_every_time = 1024;
    size_t remain_key_size = 1024;
    size_t total_key_size = 1024;
    char *dynamic_mem = (char *)malloc(total_key_size);
    void *val_position = NULL;

    size_t key_len = 0;
    size_t val_len = 0;

	for(i = 0; i < (int)n_fields; i++)
	{
		field = (struct field *)&fields[i];
        key_len = strlen(field->key);
		switch(field->type)
		{
			case FIELD_VALUE_INTEGER:
                val_len = sizeof(long long);
                val_position = (void *)&field->value_longlong;
				break;
			case FIELD_VALUE_DOUBLE:
                val_len = sizeof(double);
                val_position = (void *)&field->value_double;

				break;
			case FIELD_VALUE_CSTRING:
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
    char *key = cube->serialized_dimensions;
    size_t key_len = cube->serialized_dimensions_len;

    struct cube *old_cube = NULL;
    HASH_FIND(hh, pthis->hash_table, key, key_len, old_cube);
    if (old_cube != NULL) {
        return -1;
    }

    int id = 0;
    for ( ;id < pthis->next_index; id++) {
        if (pthis->cube_slots[id] == NULL) {
            break;
        }
    }

    cube->id = id;
    HASH_ADD_KEYPTR(hh, pthis->hash_table, cube->serialized_dimensions, key_len, cube);

    add_cube_to_position(pthis, cube, id);

    return id;
}

void cube_manager_delete(struct cube_manager *pthis, struct cube *cube)
{
    int id = cube->id;
    HASH_DEL(pthis->hash_table, cube);

    cube_free(cube);
    pthis->cube_slots[id] = NULL;
    if (id == pthis->next_index - 1) {
        pthis->next_index--;
    }
}

int cube_manager_find(const struct cube_manager *pthis, const struct field *cube_dimensions, size_t n_dimension)
{
    char *key;
    size_t key_len;
    field_array_to_key(cube_dimensions, n_dimension, &key, &key_len);

    struct cube *node = NULL;
    HASH_FIND(hh, pthis->hash_table, key, key_len, node);

    free(key);
    if (node == NULL) {
        return -1;
    } else {
        return node->id;
    }
}

struct cube *cube_manager_get_cube_by_id(const struct cube_manager *manager, int cube_id) {
    if (cube_id < 0 || cube_id >= manager->slots_number) {
        return NULL;
    }
    return manager->cube_slots[cube_id];
}

void cube_manager_list(const struct cube_manager *pthis, int **cube_ids, int *n_cube) {
    int all_available_cube_count = 0;

    int *tmp_ids = (int *)malloc(sizeof(int) * pthis->next_index);
    for (int i = 0; i < pthis->next_index; i++) {
        if (pthis->cube_slots[i] != NULL) {
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

    HASH_ITER(hh, pthis->hash_table, node_in_dest, tmp) {
        HASH_FIND(hh, master->hash_table, node_in_dest->serialized_dimensions, node_in_dest->serialized_dimensions_len, node_in_master);

        if (node_in_master == NULL) { // exist in self but not in master
            cube_manager_delete(pthis, node_in_dest);
        } else {
            metric_manifest_manager_free(node_in_dest->manifest_manager);
            node_in_dest->manifest_manager = metric_manifest_manager_copy(node_in_master->manifest_manager);
        }
    }

    // exist in master but not in self
    HASH_ITER(hh, master->hash_table, node_in_master, tmp) {
        HASH_FIND(hh, pthis->hash_table, node_in_master->serialized_dimensions, node_in_master->serialized_dimensions_len, node_in_dest);

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

int cube_manager_merge(struct cube_manager *dest, const struct cube_manager *src)
{
    struct cube *node = NULL;
    struct cube *tmp = NULL;
    int ret = FS_OK;
    HASH_ITER(hh, src->hash_table, node, tmp) {
        struct cube *node_in_dest = NULL;
        HASH_FIND(hh, dest->hash_table, node->serialized_dimensions, node->serialized_dimensions_len, node_in_dest);

        if (node_in_dest == NULL) {
            cube_manager_add(dest, cube_copy(node));
        } else {
            int tmp_ret = cube_merge(node_in_dest, node);
            if (tmp_ret != FS_OK) {
                ret = tmp_ret;
            }
        }
    }

    return ret;
}

void cube_manager_reset(struct cube_manager *pthis)
{
    for (int i = 0; i < pthis->next_index; i++) {
        if (pthis->cube_slots[i] == NULL) {
            continue;
        }
        cube_reset(pthis->cube_slots[i]);
    }
}

struct metric *find_metric_in_cell(const struct cell *cell, int metric_id)
{
    if (metric_id >= cell->next_index) {
        return NULL;
    }
    return cell->slots[metric_id];
}

void add_metric_to_cell(struct cell *cell, struct metric *metric, int metric_id)
{
    if (metric_id >= cell->slots_number) {
        cell->slots = realloc(cell->slots, sizeof(struct metric *) * cell->slots_number * 2);
        memset(cell->slots + cell->slots_number, 0, sizeof(struct metric *) * cell->slots_number);
        cell->slots_number *= 2;
    }

    cell->slots[metric_id] = metric;

    if (metric_id >= cell->next_index) {
        cell->next_index = metric_id + 1;
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
    pthis->slots = calloc(DEFAULT_N_METRIC, sizeof(struct metric *));
    pthis->slots_number = DEFAULT_N_METRIC;
    pthis->next_index = 0;

    pthis->cell_dimensions.n_field = args->n_dimensions;
    pthis->cell_dimensions.field = field_array_duplicate(args->cell_dimensions, args->n_dimensions);
    return pthis;
}

void cell_free(struct cell *pthis) {
    for (size_t i = 0; i < pthis->next_index; i++) {
        metric_free(pthis->slots[i]);
    }
    free(pthis->slots);
    for (size_t i = 0; i < pthis->cell_dimensions.n_field; i++) {
        free((char *)pthis->cell_dimensions.field[i].key);
        if (pthis->cell_dimensions.field[i].type == FIELD_VALUE_CSTRING) {
            free((char *)pthis->cell_dimensions.field[i].value_str);
        }
    }
    free(pthis->cell_dimensions.field);
    free(pthis);
}

struct cell *cell_copy(const struct cell *src) {
    struct cell *pthis = malloc(sizeof(struct cell));
    pthis->slots = calloc(src->slots_number, sizeof(struct metric *));
    pthis->slots_number = src->slots_number;
    pthis->next_index = src->next_index;
    for (size_t i = 0; i < src->next_index; i++) {
        if (src->slots[i] == NULL) {
            continue;
        }

        pthis->slots[i] = metric_copy(src->slots[i]);
    }
    
    pthis->cell_dimensions.n_field = src->cell_dimensions.n_field;
    pthis->cell_dimensions.field = field_array_duplicate(src->cell_dimensions.field, src->cell_dimensions.n_field);

    return pthis;
}

void cell_reset(struct cell *pthis) {
    for (size_t i = 0; i < pthis->next_index; i++) {
        if (pthis->slots[i] == NULL) {
            continue;
        }
        metric_reset(pthis->slots[i]);
    }
}

void cell_merge(struct cell *dest, const struct cell *src) {
    for (size_t i = 0; i < src->next_index; i++) {
        const struct metric *metric_src = src->slots[i];
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

struct cube *cube_info_new(const struct field *dimensions, size_t n_dimensions)
{
    struct cube *cube = calloc(1, sizeof(struct cube));

    if (n_dimensions == 0) {
        cube->cube_dimensions = NULL;
    } else {
        cube->cube_dimensions = field_array_duplicate(dimensions, n_dimensions);
    }

    cube->n_dimensions = n_dimensions;
    field_array_to_key(dimensions, n_dimensions, &cube->serialized_dimensions, &cube->serialized_dimensions_len);

    cube->id = -1;
    cube->primary_metric_id = -1;

    return cube;
}

struct cube *cube_new(const struct field *dimensions, size_t n_dimensions)
{
    struct cube *cube = cube_info_new(dimensions, n_dimensions);
    cube->manifest_manager = metric_manifest_manager_new();

    return cube;
}

int cube_set_sampling(struct cube *cube, enum sampling_mode mode, int max_n_cell, int primary_metric_id) {
    if (cube->sampling_mode == mode && cube->max_n_cell == max_n_cell && cube->primary_metric_id == primary_metric_id) {
        return FS_OK;
    }
    const struct metric_manifest *manifest = metric_manifest_manager_get_by_id(cube->manifest_manager, primary_metric_id);
    if (manifest == NULL && mode != SAMPLING_MODE_COMPREHENSIVE) {
        return FS_ERR_INVALID_METRIC_ID;
    }
    if ((mode == SAMPLING_MODE_TOPK && manifest->type != METRIC_TYPE_COUNTER) ||
        (mode == SAMPLING_MODE_TOP_CARDINALITY && manifest->type != METRIC_TYPE_HLL)) {
        return FS_ERR_INVALID_METRIC_TYPE;
    }

    if (cube->primary_metric_id != -1) {
        // delete previous settings
        switch (cube->sampling_mode)
        {
        case SAMPLING_MODE_TOPK:
            heavy_keeper_free(cube->heavykeeper);
            break;
        case SAMPLING_MODE_COMPREHENSIVE:
            hash_table_free(cube->table);
            break;
        case SAMPLING_MODE_TOP_CARDINALITY:
            spread_sketch_free(cube->spread_sketch);
            break;
        default:
            assert(0);
            break;
        }
    }

    switch (mode)
    {
    case SAMPLING_MODE_TOPK:
        cube->heavykeeper = heavy_keeper_new(max_n_cell);
        heavy_keeper_set_exdata_schema(cube->heavykeeper, exdata_new_i, exdata_free_i, exdata_merge_i, exdata_reset_i, exdata_copy_i);
        break;
    case SAMPLING_MODE_COMPREHENSIVE:
        cube->table = hash_table_new(max_n_cell);
        hash_table_set_exdata_schema(cube->table, exdata_new_i, exdata_free_i, exdata_merge_i, exdata_reset_i, exdata_copy_i);
        break;
    case SAMPLING_MODE_TOP_CARDINALITY: {
        int width, depth;
        unsigned char precision;
        spread_sketch_get_parameter_recommendation(max_n_cell, &depth, &width, &precision);
        cube->spread_sketch = spread_sketch_new(depth, width, precision, 0, DUMMY_TIME_VAL);
        spread_sketch_set_exdata_schema(cube->spread_sketch, exdata_new_i, exdata_free_i, exdata_merge_i, exdata_reset_i, exdata_copy_i);
        break; }
    default:
        assert(0);
        break;
    }

    cube->sampling_mode = mode;
    cube->max_n_cell = max_n_cell;
    cube->primary_metric_id = primary_metric_id;

    return FS_OK;
}

void cube_free(struct cube *cube) {
    switch (cube->sampling_mode)
    {
    case SAMPLING_MODE_TOPK:
        heavy_keeper_free(cube->heavykeeper);
        break;
    case SAMPLING_MODE_COMPREHENSIVE:
        hash_table_free(cube->table);
        break;
    case SAMPLING_MODE_TOP_CARDINALITY:
        spread_sketch_free(cube->spread_sketch);
        break;
    default:
        assert(0);
        break;
    }

    for (size_t i = 0; i < cube->n_dimensions; i++) {
        struct field *field = &cube->cube_dimensions[i];
        free((char *)field->key);
        if (field->type == FIELD_VALUE_CSTRING) {
            free((char *)field->value_str);
        }
    }
    free(cube->cube_dimensions);

    free(cube->serialized_dimensions);
    metric_manifest_manager_free(cube->manifest_manager);

    free(cube);
}

void cube_reset(struct cube *cube) {
    switch (cube->sampling_mode)
    {
    case SAMPLING_MODE_TOPK:
        heavy_keeper_reset(cube->heavykeeper);
        break;
    case SAMPLING_MODE_COMPREHENSIVE:
        hash_table_reset(cube->table);
        break;  
    case SAMPLING_MODE_TOP_CARDINALITY:
        spread_sketch_reset(cube->spread_sketch);
        break;
    default:
        assert(0);
        break;
    }
}

struct cell *get_cell_in_cube_generic(struct cube *cube, const struct field *dimensions, size_t n_dimensions) {
    char *compound_dimension;
    size_t compound_dimension_len;
    field_array_to_key(dimensions, n_dimensions, &compound_dimension, &compound_dimension_len);

    struct exdata_new_args args;
    args.cell_dimensions = dimensions;
    args.n_dimensions = n_dimensions;

    struct cell *cell_data = NULL;
    int tmp_ret;
    switch (cube->sampling_mode)
    {
    case SAMPLING_MODE_COMPREHENSIVE: {
        cell_data = hash_table_get0_exdata(cube->table, compound_dimension, compound_dimension_len);
        if (cell_data == NULL) {
            tmp_ret = hash_table_add(cube->table, compound_dimension, compound_dimension_len, (void *)&args);
            if (tmp_ret == 1) {
                cell_data = hash_table_get0_exdata(cube->table, compound_dimension, compound_dimension_len);
            }
        }
        break;}
    case SAMPLING_MODE_TOPK: {
        cell_data = heavy_keeper_get0_exdata(cube->heavykeeper, compound_dimension, compound_dimension_len);
        if (cell_data == NULL) {
            tmp_ret = heavy_keeper_add(cube->heavykeeper, compound_dimension, compound_dimension_len, 0, (void *)&args);
            if (tmp_ret == 1) {
                cell_data = heavy_keeper_get0_exdata(cube->heavykeeper, compound_dimension, compound_dimension_len);
            }
        }
        break;}
    case SAMPLING_MODE_TOP_CARDINALITY: {
        cell_data = spread_sketch_get0_exdata(cube->spread_sketch, compound_dimension, compound_dimension_len);
        if (cell_data == NULL) {
            tmp_ret = spread_sketch_add_hash(cube->spread_sketch,  compound_dimension, compound_dimension_len, DUMMY_ITEM_HASH, (void *)&args, DUMMY_TIME_VAL);
            if (tmp_ret == 1) {
                cell_data = spread_sketch_get0_exdata(cube->spread_sketch,  compound_dimension, compound_dimension_len);
            }
        }
        break;}
    default:
        assert(0);
        break;
    }

    free(compound_dimension);
    return cell_data;
}

union metric_parameter *construct_parameters(enum metric_type type, ...)
{
    union metric_parameter *paras = (union metric_parameter *)malloc(sizeof(union metric_parameter));
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

int cube_register_counter(struct cube *cube, const char *metric_name) {
    struct metric_manifest *metric = malloc(sizeof(struct metric_manifest));
    metric->name = strdup(metric_name);
    metric->parameters = construct_parameters(METRIC_TYPE_COUNTER);
    metric->type = METRIC_TYPE_COUNTER;

    int id = metric_manifest_manager_add(cube->manifest_manager, metric);
    if (id < 0) {
        free(metric->name);
        free(metric->parameters);
        free(metric);
        return FS_ERR_METRIC_NAME_ALREADY_EXISTS;
    }

    metric->id = id;
    return id;
}

int cube_register_hll(struct cube *cube,const char *metric_name, unsigned char precision) {
    if (precision < 4 || precision > 18) {
        return FS_ERR_INVALID_PARAM;
    }

    struct metric_manifest *metric = malloc(sizeof(struct metric_manifest));
    metric->name = strdup(metric_name);
    metric->parameters = construct_parameters(METRIC_TYPE_HLL, precision);
    metric->type = METRIC_TYPE_HLL;

    int id = metric_manifest_manager_add(cube->manifest_manager, metric);
    if (id < 0) {
        free(metric->name);
        free(metric->parameters);
        free(metric);
        return FS_ERR_METRIC_NAME_ALREADY_EXISTS;
    }

    metric->id = id;
    return id;
}

int cube_register_hist(struct cube *cube,const char *metric_name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures) {
    // refer to hdr_histogram.h for the rules of parameters. Just copy them here
    if (lowest_trackable_value < 1) {
        return FS_ERR_INVALID_PARAM;
    }
    if (significant_figures < 1 || significant_figures > 5) {
        return FS_ERR_INVALID_PARAM;
    }
    if (lowest_trackable_value * 2 > highest_trackable_value)
	{
		return FS_ERR_INVALID_PARAM;
	}

    struct metric_manifest *metric = malloc(sizeof(struct metric_manifest));
    metric->name = strdup(metric_name);
    metric->parameters = construct_parameters(METRIC_TYPE_HISTOGRAM, lowest_trackable_value, highest_trackable_value, significant_figures);
    metric->type = METRIC_TYPE_HISTOGRAM;

    int id = metric_manifest_manager_add(cube->manifest_manager, metric);
    if (id < 0) {
        free(metric->name);
        free(metric->parameters);
        free(metric);
        return FS_ERR_METRIC_NAME_ALREADY_EXISTS;
    }
    metric->id = id;

    return id;
}

int cube_histogram_record(struct cube *cube, int metric_id, const struct field *dimensions, size_t n_dimensions, long long value) {
    if (cube->primary_metric_id == -1) {
        return FS_ERR_CUBE_SAMPLING_NOT_INITIALIZED;
    }
    assert(cube->sampling_mode == SAMPLING_MODE_COMPREHENSIVE || (cube->primary_metric_id != metric_id));

    const struct metric_manifest *manifest = metric_manifest_manager_get_by_id(cube->manifest_manager, metric_id);
    if (manifest == NULL || manifest->type != METRIC_TYPE_HISTOGRAM) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    struct cell *cell_data = get_cell_in_cube_generic(cube, dimensions, n_dimensions);
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

int cube_hll_add(struct cube *cube, int metric_id, const struct field *dimensions, size_t n_dimensions, const char *key, size_t key_len) {
    if (cube->primary_metric_id == -1) {
        return FS_ERR_CUBE_SAMPLING_NOT_INITIALIZED;
    }

    assert(cube->sampling_mode != SAMPLING_MODE_TOPK || cube->primary_metric_id != metric_id);

    const struct metric_manifest *manifest = metric_manifest_manager_get_by_id(cube->manifest_manager, metric_id);
    if (manifest == NULL || manifest->type != METRIC_TYPE_HLL) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    if (cube->sampling_mode == SAMPLING_MODE_TOP_CARDINALITY && cube->primary_metric_id == metric_id) {
        char *compound_dimension;
        size_t compound_dimension_len;
        field_array_to_key(dimensions, n_dimensions, &compound_dimension, &compound_dimension_len);

        struct exdata_new_args args;
        args.cell_dimensions = dimensions;
        args.n_dimensions = n_dimensions;

        int tmp_ret = spread_sketch_add(cube->spread_sketch,  compound_dimension, compound_dimension_len, key, key_len, (void *)&args, DUMMY_TIME_VAL);
        free(compound_dimension);
        return tmp_ret == 1 ? FS_OK : FS_ERR_TOO_MANY_CELLS;
    }

    struct cell *cell_data = get_cell_in_cube_generic(cube, dimensions, n_dimensions);
    if (cell_data == NULL) {
        return FS_ERR_TOO_MANY_CELLS;
    }

    struct metric *metric = add_or_find_metric_in_cell(manifest, cell_data);
    metric_hll_add(metric, key, key_len);
    return FS_OK;
}

uint64_t field_array_to_hash(const struct field *field, size_t n_dimensions) {
    XXH3_state_t state = {0};
    XXH3_64bits_reset(&state);

    for (int i = 0; i < n_dimensions; i++) {
        XXH3_64bits_update(&state, field[i].key, strlen(field[i].key));
        if (field[i].type != FIELD_VALUE_CSTRING) {
            XXH3_64bits_update(&state, &field[i].value_longlong, sizeof(long long));
        } else {
            XXH3_64bits_update(&state, field[i].value_str, strlen(field[i].value_str));
        }
    }

    return XXH3_64bits_digest(&state);
}

int cube_hll_add_field(struct cube *cube, int metric_id, const struct field *dimensions, size_t n_dimensions, const struct field *item_fields, size_t n_item) 
{
    if (cube->primary_metric_id == -1) {
        return FS_ERR_CUBE_SAMPLING_NOT_INITIALIZED;
    }
    assert(cube->sampling_mode != SAMPLING_MODE_TOPK || (cube->primary_metric_id != metric_id));
    const struct metric_manifest *manifest = metric_manifest_manager_get_by_id(cube->manifest_manager, metric_id);
    if (manifest == NULL || manifest->type != METRIC_TYPE_HLL) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    if (cube->sampling_mode == SAMPLING_MODE_TOP_CARDINALITY && cube->primary_metric_id == metric_id) {
        char *compound_dimension;
        size_t compound_dimension_len;
        field_array_to_key(dimensions, n_dimensions, &compound_dimension, &compound_dimension_len);

        struct exdata_new_args args;
        args.cell_dimensions = dimensions;
        args.n_dimensions = n_dimensions;

        uint64_t hash = field_array_to_hash(item_fields, n_item);

        int tmp_ret = spread_sketch_add_hash(cube->spread_sketch, compound_dimension, compound_dimension_len, hash, (void *)&args, DUMMY_TIME_VAL);
        free(compound_dimension);
        return tmp_ret == 1 ? FS_OK : FS_ERR_TOO_MANY_CELLS;
    }

    struct cell *cell_data = get_cell_in_cube_generic(cube, dimensions, n_dimensions);
    if (cell_data == NULL) {
        return FS_ERR_TOO_MANY_CELLS;
    }
    struct metric *metric = add_or_find_metric_in_cell(manifest, cell_data);
    
    uint64_t hash = field_array_to_hash(item_fields, n_item);
    metric_hll_add_hash(metric, hash);
    return FS_OK;
}

int cube_counter_incrby(struct cube *cube, int metric_id, const struct field *dimensions, size_t n_dimensions, long long increment) {
    if (cube->primary_metric_id == -1) {
        return FS_ERR_CUBE_SAMPLING_NOT_INITIALIZED;
    }
    if (cube->sampling_mode == SAMPLING_MODE_TOPK && cube->primary_metric_id == metric_id && increment < 0) {
        return FS_ERR_OPERATION_NOT_SUPPORTED_FOR_PRIMARY_METRIC;
    }
    assert(cube->sampling_mode == SAMPLING_MODE_COMPREHENSIVE || 
          (cube->sampling_mode == SAMPLING_MODE_TOPK)  ||
          (cube->sampling_mode == SAMPLING_MODE_TOP_CARDINALITY && cube->primary_metric_id != metric_id)
          );

    const struct metric_manifest *manifest = metric_manifest_manager_get_by_id(cube->manifest_manager, metric_id);
    if (manifest == NULL || manifest->type != METRIC_TYPE_COUNTER) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    if (cube->primary_metric_id == metric_id && cube->sampling_mode == SAMPLING_MODE_TOPK) {
        if (increment < 0) {
            return FS_ERR_INVALID_PARAM;
        }

        char *compound_dimension;
        size_t compound_dimension_len;
        field_array_to_key(dimensions, n_dimensions, &compound_dimension, &compound_dimension_len);

        struct exdata_new_args args;
        args.cell_dimensions = dimensions;
        args.n_dimensions = n_dimensions;

        int tmp_ret = heavy_keeper_add(cube->heavykeeper, compound_dimension, compound_dimension_len, increment, (void *)&args);
        free(compound_dimension);
        return tmp_ret == 1 ? FS_OK : FS_ERR_TOO_MANY_CELLS;
    }
    
    struct cell *cell_data = get_cell_in_cube_generic(cube, dimensions, n_dimensions);
    if (cell_data == NULL) {
        return FS_ERR_TOO_MANY_CELLS;
    }

    struct metric *metric = add_or_find_metric_in_cell(manifest, cell_data);

    metric_counter_incrby(metric, increment);
    return FS_OK;
}

int cube_counter_set(struct cube *cube, int metric_id, const struct field *dimensions, size_t n_dimensions, long long value) {
    if (cube->primary_metric_id == -1) {
        return FS_ERR_CUBE_SAMPLING_NOT_INITIALIZED;
    }
    if (cube->sampling_mode == SAMPLING_MODE_TOPK && cube->primary_metric_id == metric_id) {
        return FS_ERR_OPERATION_NOT_SUPPORTED_FOR_PRIMARY_METRIC;
    }

    const struct metric_manifest *manifest = metric_manifest_manager_get_by_id(cube->manifest_manager, metric_id);
    if (manifest == NULL || manifest->type != METRIC_TYPE_COUNTER) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    struct cell *cell_data = get_cell_in_cube_generic(cube, dimensions, n_dimensions);
    if (cell_data == NULL) {
        return FS_ERR_TOO_MANY_CELLS;
    }

    struct metric *metric = add_or_find_metric_in_cell(manifest, cell_data);
    metric_counter_set(metric, value);
    return FS_OK;
}

struct cube *cube_copy(const struct cube *cube)
{
    struct cube *cube_dup = cube_info_new(cube->cube_dimensions, cube->n_dimensions);
    cube_dup->primary_metric_id = cube->primary_metric_id;
    cube_dup->sampling_mode = cube->sampling_mode;
    cube_dup->max_n_cell = cube->max_n_cell;

    switch (cube->sampling_mode)
    {
    case SAMPLING_MODE_TOPK:
        cube_dup->heavykeeper = heavy_keeper_copy(cube->heavykeeper);
        break;
    case SAMPLING_MODE_COMPREHENSIVE:
        cube_dup->table = hash_table_copy(cube->table);
        break;
    case SAMPLING_MODE_TOP_CARDINALITY:
        cube_dup->spread_sketch = spread_sketch_copy(cube->spread_sketch);
        break;
    default:
        assert(0);
        break;
    }

    cube_dup->manifest_manager = metric_manifest_manager_copy(cube->manifest_manager);

    return cube_dup;
}

int cube_merge(struct cube *dest, const struct cube *src)
{
    if (dest->sampling_mode != src->sampling_mode) {
        return FS_ERR_DIFFERENT_CONFIGURATION_FOR_SAME_CUBE;
    }

    size_t n_metric_src = 0;
    const struct metric_manifest **list_src = metric_manifest_manager_list(src->manifest_manager, &n_metric_src);
    size_t n_metric_dst = 0;
    const struct metric_manifest **list_dst = metric_manifest_manager_list(dest->manifest_manager, &n_metric_dst);
    int len_min = n_metric_src < n_metric_dst ? n_metric_src : n_metric_dst;
    for (int i = 0; i < len_min; i++) {
        if (list_src[i]->type != list_dst[i]->type) {
            return FS_ERR_DIFFERENT_CONFIGURATION_FOR_SAME_CUBE;
        }
        if (strcmp(list_src[i]->name, list_dst[i]->name) != 0) {
            return FS_ERR_DIFFERENT_CONFIGURATION_FOR_SAME_CUBE;
        }
    }
    for (int i = n_metric_dst; i < n_metric_src; i++) {
        metric_manifest_manager_add(dest->manifest_manager, metric_manifest_copy(list_src[i]));
    }

    switch (dest->sampling_mode)
    {
    case SAMPLING_MODE_TOPK:
        heavy_keeper_merge(dest->heavykeeper, src->heavykeeper);
        break;
    case SAMPLING_MODE_COMPREHENSIVE:
        hash_table_merge(dest->table, src->table);
        break;
    case SAMPLING_MODE_TOP_CARDINALITY:
        spread_sketch_merge(dest->spread_sketch, src->spread_sketch);
        break;
    default:
        assert(0);
        break;
    }

    return FS_OK;
}

struct cube *cube_fork(const struct cube *cube) {
    struct cube *ret = cube_info_new(cube->cube_dimensions, cube->n_dimensions);
    ret->primary_metric_id = cube->primary_metric_id;
    ret->sampling_mode = cube->sampling_mode;
    ret->max_n_cell = cube->max_n_cell;
    ret->manifest_manager = metric_manifest_manager_copy(cube->manifest_manager);

    switch (cube->sampling_mode) {
        case SAMPLING_MODE_TOPK:
            ret->heavykeeper = heavy_keeper_new(cube->max_n_cell);
            heavy_keeper_set_exdata_schema(ret->heavykeeper, exdata_new_i, exdata_free_i, exdata_merge_i, exdata_reset_i, exdata_copy_i);
            break;
        case SAMPLING_MODE_COMPREHENSIVE:
            ret->table = hash_table_new(cube->max_n_cell);
            hash_table_set_exdata_schema(ret->table, exdata_new_i, exdata_free_i, exdata_merge_i, exdata_reset_i, exdata_copy_i);
            break;
        case SAMPLING_MODE_TOP_CARDINALITY: {
            int width, depth, dummy_time;
            unsigned char precision;
            spread_sketch_get_parameter(cube->spread_sketch, &depth, &width, &precision, &dummy_time);
            ret->spread_sketch = spread_sketch_new(depth, width, precision, 0, DUMMY_TIME_VAL);
            spread_sketch_set_exdata_schema(ret->spread_sketch, exdata_new_i, exdata_free_i, exdata_merge_i, exdata_reset_i, exdata_copy_i);
            break;}
        default:
            assert(0);
            break;
    }
    return ret;
}

/* -------------------------------------------------------------------------- */
/*                                    query                                   */
/* -------------------------------------------------------------------------- */

struct tmp_sorted_data_spread_sketch_cell {
    double hll_value;
    struct cell *data;
};
static int compare_tmp_sorted_data_spread_sketch_cell(const void *a, const void *b) { // sort in descending order
    const struct tmp_sorted_data_spread_sketch_cell *aa = (const struct tmp_sorted_data_spread_sketch_cell *)a;
    const struct tmp_sorted_data_spread_sketch_cell *bb = (const struct tmp_sorted_data_spread_sketch_cell *)b;
    if (aa->hll_value < bb->hll_value) {
        return 1;
    } else if (aa->hll_value > bb->hll_value) {
        return -1;
    } else {
        return 0;
    }
}
void cube_get_cells(const struct cube *cube, struct field_list **cell_dimensions, size_t *n_cell)
{
    size_t n_cell_tmp = cube_get_cell_count(cube);
    if (n_cell_tmp == 0) {
        *cell_dimensions = NULL;
        *n_cell = 0;
        return;
    }

    char **spread_sketch_keys = NULL;
    size_t *spread_sketch_keys_lens = NULL;
    struct cell **cell_datas = (struct cell **)malloc(sizeof(struct cell *) * n_cell_tmp);
    switch (cube->sampling_mode) {
        case SAMPLING_MODE_COMPREHENSIVE:
            hash_table_list(cube->table, (void **)cell_datas, n_cell_tmp);
            break;
        case SAMPLING_MODE_TOPK:
            heavy_keeper_list(cube->heavykeeper, (void **)cell_datas, n_cell_tmp);
            break;
        case SAMPLING_MODE_TOP_CARDINALITY: {
            spread_sketch_list_keys(cube->spread_sketch, &spread_sketch_keys, &spread_sketch_keys_lens, &n_cell_tmp);
            for (int i = 0; i < n_cell_tmp; i++) {
                cell_datas[i] = spread_sketch_get0_exdata(cube->spread_sketch, spread_sketch_keys[i], spread_sketch_keys_lens[i]);
            }
        }
            break;
        default:
            assert(0);
    }

    // spread sketch often stores more than max_n_cell. So sort out the top max_n_cell cells.
    if (cube->sampling_mode == SAMPLING_MODE_TOP_CARDINALITY && n_cell_tmp > cube->max_n_cell) {
        struct tmp_sorted_data_spread_sketch_cell *tmp_sorted_data = (struct tmp_sorted_data_spread_sketch_cell *)malloc(sizeof(struct tmp_sorted_data_spread_sketch_cell) * n_cell_tmp);
        for (int i = 0; i < n_cell_tmp; i++) {
            tmp_sorted_data[i].data = cell_datas[i];
            tmp_sorted_data[i].hll_value = spread_sketch_get_cardinality(cube->spread_sketch, spread_sketch_keys[i], spread_sketch_keys_lens[i]);
        }
        qsort(tmp_sorted_data, n_cell_tmp, sizeof(struct tmp_sorted_data_spread_sketch_cell), compare_tmp_sorted_data_spread_sketch_cell);

        free(cell_datas);
        cell_datas = (struct cell **)malloc(sizeof(struct cell *) * cube->max_n_cell);
        for (int i = 0; i < cube->max_n_cell; i++) {
            cell_datas[i] = tmp_sorted_data[i].data;
        }
        n_cell_tmp = cube->max_n_cell;
        free(tmp_sorted_data);
    }

    struct field_list *ret = (struct field_list *)malloc(sizeof(struct field_list) * n_cell_tmp);
    *cell_dimensions = ret;
    *n_cell = n_cell_tmp;
    for (int i = 0; i < n_cell_tmp; i++) {
        struct cell *cell_data = cell_datas[i];
        struct field_list *field_list_tmp = &ret[i];
        field_list_tmp->n_field = cell_data->cell_dimensions.n_field;
        if (field_list_tmp->n_field == 0) {
            field_list_tmp->field = NULL;
            continue;
        }
        field_list_tmp->field = field_array_duplicate(cell_data->cell_dimensions.field, field_list_tmp->n_field);
    }

    free(cell_datas);
    free(spread_sketch_keys);
    free(spread_sketch_keys_lens);
}

const struct cell *cube_find_cell_by_dimension(const struct cube *cube, const struct field_list *fields)
{
    const struct cell *ret = NULL;
    char *compound_dimension;
    size_t dimension_len;
    field_array_to_key(fields->field, fields->n_field, &compound_dimension, &dimension_len);

    switch (cube->sampling_mode)
    {
    case SAMPLING_MODE_TOPK:
        ret = heavy_keeper_get0_exdata(cube->heavykeeper, compound_dimension, dimension_len);
        break;
    case SAMPLING_MODE_COMPREHENSIVE:
        ret = hash_table_get0_exdata(cube->table, compound_dimension, dimension_len);
        break;
    case SAMPLING_MODE_TOP_CARDINALITY:
        ret = spread_sketch_get0_exdata(cube->spread_sketch, compound_dimension, dimension_len);
        break;
    default:
        assert(0);
        return NULL;
    }
    free(compound_dimension);

    return ret;
}

const struct metric *cube_find_uncleared_metric_in_cell(const struct cube *cube, const struct field_list *fields, int metric_id,int *ret_code)
{
    const struct cell *data = cube_find_cell_by_dimension(cube, fields);
    
    if (data == NULL) {
        *ret_code = FS_ERR_INVALID_DIMENSION;
        return NULL;
    }

    if (metric_id < 0 || metric_id >= data->next_index) {
        *ret_code = FS_ERR_INVALID_METRIC_ID;
        return NULL;
    }
    *ret_code = FS_OK;

    const struct metric *ret_metric = data->slots[metric_id];
    if (ret_metric == NULL || metric_check_if_cleared(ret_metric)) {
        printf("metric is: %p, name: %s\n", ret_metric, metric_manifest_manager_get_by_id(cube->manifest_manager, metric_id)->name);
        *ret_code = FS_ERR_INVALID_METRIC_ID;
        return NULL;
    }

    return ret_metric;
}

int cube_counter_get(const struct cube *cube, int metric_id, const struct field_list *fields, long long *value)
{
    if (cube->sampling_mode == SAMPLING_MODE_TOPK && cube->primary_metric_id == metric_id) {
        char *dimension_in_string;
        size_t dimension_string_len;
        field_array_to_key(fields->field, fields->n_field, &dimension_in_string, &dimension_string_len);

        long long count = 0;
        void *exdata_dummy = NULL;
        heavy_keeper_one_point_query(cube->heavykeeper, dimension_in_string, dimension_string_len, &count, &exdata_dummy);
        *value = count;

        free(dimension_in_string);
        return count == 0 ? FS_ERR_INVALID_DIMENSION : FS_OK;
    }

    int ret;
    const struct metric *metric = cube_find_uncleared_metric_in_cell(cube, fields, metric_id, &ret);
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
    if (cube->sampling_mode == SAMPLING_MODE_TOP_CARDINALITY && cube->primary_metric_id == metric_id) {
        char *dimension_in_string;
        size_t dimension_string_len;
        field_array_to_key(fields->field, fields->n_field, &dimension_in_string, &dimension_string_len);

        double hll_value = spread_sketch_get_cardinality(cube->spread_sketch, dimension_in_string, dimension_string_len);
        free(dimension_in_string);
        if (hll_value < 0) {
            return FS_ERR_INVALID_DIMENSION;
        }

        *value = hll_value;
        return FS_OK;
    }

    int ret;
    const struct metric *metric = cube_find_uncleared_metric_in_cell(cube, fields, metric_id, &ret);
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
    const struct metric *metric = cube_find_uncleared_metric_in_cell(cube, fields, metric_id, &ret);
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
    const struct metric *metric = cube_find_uncleared_metric_in_cell(cube, fields, metric_id, &ret);
    if (ret != FS_OK) {
        return ret;
    }
    if (metric == NULL) {
        return FS_ERR_INVALID_METRIC_ID;
    }

    *count = metric_histogram_count_le_value(metric, value);
    return FS_OK;
}

int cube_get_serialization_as_base64(const struct cube *cube, int metric_id, const struct field_list *fields, char **blob, size_t *blob_size) {
    if (cube->sampling_mode == SAMPLING_MODE_TOP_CARDINALITY && cube->primary_metric_id == metric_id) {
        char *dimension_in_string;
        size_t dimension_string_len;
        field_array_to_key(fields->field, fields->n_field, &dimension_in_string, &dimension_string_len);

        *blob = spread_sketch_get_hll_base64_serialization(cube->spread_sketch, dimension_in_string, dimension_string_len);
        *blob_size = strlen(*blob);
        free(dimension_in_string);
        return FS_OK;
    }

    int ret;
    const struct metric *metric = cube_find_uncleared_metric_in_cell(cube, fields, metric_id, &ret);
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
            return hash_table_get_count(cube->table);
        case SAMPLING_MODE_TOPK:
            return heavy_keeper_get_count(cube->heavykeeper);
        case SAMPLING_MODE_TOP_CARDINALITY:
            return spread_sketch_get_count(cube->spread_sketch);
        default:
            assert(0);
            return -1; // to mute cppcheck
    }
}

void cube_get_metrics_in_cell(const struct cube *cube, const struct field_list *fields, int **metric_id_out, size_t *n_metric_out) {
    const struct cell *cell_data = cube_find_cell_by_dimension(cube, fields);
    if (cell_data == NULL) {
        *metric_id_out = NULL;
        *n_metric_out = 0;
        return;
    }

    *metric_id_out = (int *)malloc(sizeof(int) * cell_data->next_index + 1); // +1: for primary metric
    int n_metric = 0;
    if (cube->sampling_mode == SAMPLING_MODE_TOP_CARDINALITY || cube->sampling_mode == SAMPLING_MODE_TOPK) { // primary metric is not stored in cell_data
        (*metric_id_out)[n_metric] = cube->primary_metric_id;
        n_metric++;
    }

    for (int i = 0; i < cell_data->next_index; i++) {
        if (cell_data->slots[i] != NULL && !metric_check_if_cleared(cell_data->slots[i])) {
            (*metric_id_out)[n_metric] = i;
            n_metric++;
        }
    }
    *n_metric_out = n_metric;
}

struct field_list *cube_get_identifier(const struct cube *cube) {
    struct field_list *ret = (struct field_list *)malloc(sizeof(struct field_list));

    if (cube->n_dimensions == 0) {
        ret->field = NULL;
        ret->n_field = 0;
        return ret;
    }

    ret->field = field_array_duplicate(cube->cube_dimensions, cube->n_dimensions);
    ret->n_field = cube->n_dimensions;
    

    return ret;
}

const char *cube_get_metric_name(const struct cube *cube, int metric_id) {
    const struct metric_manifest *metric = metric_manifest_manager_get_by_id(cube->manifest_manager, metric_id);
    if (metric == NULL) {
        return NULL;
    }

    return metric->name;
}

enum metric_type cube_get_metric_type(const struct cube *cube, int metric_id) {
    const struct metric_manifest *metric = metric_manifest_manager_get_by_id(cube->manifest_manager, metric_id);
    if (metric == NULL) {
        return (enum metric_type)(-1);
    }

    return metric->type;
}

void cube_get_metrics(const struct cube *cube, int **metric_id_out, size_t *n_metric)
{
    const struct metric_manifest **list = metric_manifest_manager_list(cube->manifest_manager, n_metric);
    if (*n_metric == 0) {
        *metric_id_out = NULL;
        return;
    }

    int *tmp_ids = (int *)malloc(sizeof(int) * (*n_metric));
    *metric_id_out = tmp_ids;

    for (int i = 0; i < *n_metric; i++) {
        tmp_ids[i] = list[i]->id;
    }
}
