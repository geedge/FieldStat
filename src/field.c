#include <assert.h>
#include <stdarg.h>


#include "fieldstat.h"
#include "field.h"
#include "uthash.h"
#include "fs_cell.h"
#include "mpack/mpack.h"

struct cell_hash_node {
    char *key; // kv_pair of tags
    size_t key_len; // exclude '\0'
    struct cell *cell;
    UT_hash_handle hh;
};

struct para_hdr{
    // const char *bins;
    long long lowest_trackable_value;
    long long highest_trackable_value;
    int significant_figures;
};

struct para_topk {
    int K;
};

struct para_cardinality {
    char precision;
};

struct field_paras {
    union {
        // struct para_hdr hdr;
        struct para_topk topk;
        struct para_hdr hdr;
        struct para_cardinality cardinality;
    };
};

struct fs_field {
    enum field_type type;
    int field_id;
    char *name;
    struct cell_hash_node *hash_tbl;
    struct field_paras *field_paras;
};

static void build_dynamic_cell_key(const struct fieldstat_tag tags[], size_t n_tags, char **out_key, size_t *out_key_size)
{
	int i = 0;
	int used_len = 0;
	struct fieldstat_tag *tag = NULL;

    char one_time_key[1024] = {0};
    size_t tmp_key_size = sizeof(one_time_key);
    size_t remain_key_size = tmp_key_size;
    size_t total_key_size = tmp_key_size;
    char *dynamic_mem = (char *)malloc(remain_key_size);

	for(i = 0; i < (int)n_tags; i++)
	{
		tag = (struct fieldstat_tag *)&tags[i];
		switch(tag->type)
		{
			case 0:
				used_len = snprintf(one_time_key, tmp_key_size, "%s%lld", tag->key, tag->value_int);
				break;
			case 1:
				used_len = snprintf(one_time_key, tmp_key_size, "%s%lf", tag->key, tag->value_double);
				break;
			case 2:
				used_len = snprintf(one_time_key, tmp_key_size, "%s%s", tag->key, tag->value_str);
				break;
			default:
				assert(0);
				break;
		}
        
        if (used_len >= remain_key_size) {
            total_key_size += tmp_key_size;
            dynamic_mem = (char *)realloc(dynamic_mem, total_key_size);
            remain_key_size += tmp_key_size;
        }

        memcpy(dynamic_mem + total_key_size - remain_key_size, one_time_key, used_len);
        remain_key_size -= used_len;
	}

    *out_key = (char *)malloc(total_key_size - remain_key_size + 1);
    memcpy(*out_key, dynamic_mem, total_key_size - remain_key_size);
    (*out_key)[total_key_size - remain_key_size] = '\0';
    *out_key_size = total_key_size - remain_key_size; // not include '\0'

    free(dynamic_mem);
}

struct cell *field_spawn_cell(const struct fs_field *field)
{
    struct cell *cell = NULL;
    switch (field->type) {
        case FIELD_TYPE_COUNTER: {
            cell = cell_create_for_counter();
        }
        break;
        case FIELD_TYPE_TOPK:{
            cell = cell_create_for_topk(field->field_paras->topk.K);
        }
        break;
        case FIELD_TYPE_HISTOGRAM:{
            struct para_hdr *tmp_paras = &field->field_paras->hdr;
            cell = cell_create_for_histogram(tmp_paras->lowest_trackable_value, tmp_paras->highest_trackable_value, tmp_paras->significant_figures);
        }
        break;
        case FIELD_TYPE_CARDINALITY:{
            cell = cell_create_for_cardinality(field->field_paras->cardinality.precision);
        }
        break;
        default:
            printf("field_spawn_cell:unknown field type\n");
            return NULL;
    }
    return cell;
}

struct field_paras *construct_parameters(enum field_type type, ...)
{
    struct field_paras *paras = (struct field_paras *)malloc(sizeof(struct field_paras));
    va_list ap;
    va_start(ap, type);
    switch (type) {
        case FIELD_TYPE_TOPK:
            paras->topk.K = va_arg(ap, int);
            break;
        case FIELD_TYPE_CARDINALITY:
            paras->cardinality.precision = (char)va_arg(ap, int);
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

// TODO: deserialize parameters, 要有新的好结构，而不是简单一个数组

struct fs_field *field_new(enum field_type type, const char *name, struct field_paras *paras)
{
    struct fs_field *field = (struct fs_field *)malloc(sizeof(struct fs_field));
    field->type = type;
    field->field_id = -1;
    field->name = strdup(name);
    field->hash_tbl = NULL;
    field->field_paras = paras;
    return field;
}

static struct cell *field_find_cell_with_key(const struct fs_field *field, const char *key, size_t key_len)
{
    struct cell_hash_node *node;
    HASH_FIND(hh, field->hash_tbl, key, key_len, node);
    if (node == NULL) {
        printf("field_find_cell_with_key: node == NULL\n");
        return NULL;
    }
    return node->cell;
}

static struct cell *field_find_cell(const struct fs_field *field, const struct fieldstat_tag tag_list[], size_t tag_num)
{
    char *dynamic_cell_key = NULL;
    size_t dynamic_key_size = 0;
    build_dynamic_cell_key(tag_list, tag_num, &dynamic_cell_key, &dynamic_key_size);
    struct cell *cell = field_find_cell_with_key(field, dynamic_cell_key, dynamic_key_size);
    free(dynamic_cell_key);
    return cell;
}
// key_len does not include '\0'
static void field_insert_cell_with_key(struct fs_field *field, struct cell *cell, const char *key, size_t key_len)
{
    struct cell_hash_node *node = (struct cell_hash_node *)malloc(sizeof(struct cell_hash_node));

    node->key_len = key_len;
    node->key = (char *)malloc(key_len + 1);
    memcpy(node->key, key, key_len);
    node->cell = cell;
    HASH_ADD_KEYPTR(hh, field->hash_tbl, node->key, node->key_len, node);
}

void field_insert_cell(struct fs_field *field, struct cell *cell, const struct fieldstat_tag tag_list[], size_t tag_num)
{
    char *dynamic_cell_key = NULL;
    size_t dynamic_key_size = 0;
    build_dynamic_cell_key(tag_list, tag_num, &dynamic_cell_key, &dynamic_key_size);
    field_insert_cell_with_key(field, cell, dynamic_cell_key, dynamic_key_size);
    free(dynamic_cell_key);
}

struct cell *field_find_or_create_cell(struct fs_field *field, const struct fieldstat_tag tag_list[], size_t tag_num)
{
    char *dynamic_cell_key = NULL;
    size_t dynamic_key_size = 0;
    build_dynamic_cell_key(tag_list, tag_num, &dynamic_cell_key, &dynamic_key_size);
    struct cell *cell = field_find_cell_with_key(field, dynamic_cell_key, dynamic_key_size);
    if (cell == NULL) {
        printf("cell is null, going to create one\n");
        cell = field_spawn_cell(field);
        cell_add_tag(cell, tag_list, tag_num);
        field_insert_cell_with_key(field, cell, dynamic_cell_key, dynamic_key_size);
    }
    free(dynamic_cell_key);

    return cell;
}

struct fs_field *field_new_topk(const char *name, int K)
{
    printf("field_new_topk enter\n");
    struct field_paras *paras = construct_parameters(FIELD_TYPE_TOPK, K);

    struct fs_field *field = field_new(FIELD_TYPE_TOPK, name, paras);

    return field;
}

struct fs_field *field_new_counter(const char *name)
{
    return  field_new(FIELD_TYPE_COUNTER, name, NULL);
}

struct fs_field *field_new_cardinality(const char *name, char precision)
{
    struct field_paras *paras = construct_parameters(FIELD_TYPE_CARDINALITY, precision);
    struct fs_field *field = field_new(FIELD_TYPE_CARDINALITY, name, paras);
    return field;
}
struct fs_field *field_new_histogram(const char *name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures)
{
    struct field_paras *paras = construct_parameters(FIELD_TYPE_HISTOGRAM, lowest_trackable_value, highest_trackable_value, significant_figures);
    struct fs_field *field = field_new(FIELD_TYPE_HISTOGRAM, name, paras);
    return field;
}

void field_free(struct fs_field *field)
{
    struct cell_hash_node *node, *tmp;
    HASH_ITER(hh, field->hash_tbl, node, tmp) {
        struct cell *cell = node->cell;
        cell_free(cell);
        HASH_DEL(field->hash_tbl, node);
        free(node->key);
        free(node);
    }
    free(field->field_paras);
    free(field->name);
    free(field);
}

void field_refresh(struct fs_field *field)
{
    struct cell_hash_node *node, *tmp;
    HASH_ITER(hh, field->hash_tbl, node, tmp) {
        struct cell *cell = node->cell;
        cell_refresh(cell);
    }
}

struct fs_field *field_dup(const struct fs_field *src)
{
    struct field_paras *params = NULL;
    if (src->field_paras != NULL) {
        params = (struct field_paras *)malloc(sizeof(struct field_paras));
        memcpy(params, src->field_paras, sizeof(struct field_paras));
    }

    struct fs_field *dst = field_new(src->type, src->name, params);
    field_set_id(dst, src->field_id);
    struct cell_hash_node *node, *tmp;
    HASH_ITER(hh, src->hash_tbl, node, tmp) {
        struct cell *new_cell = cell_duplicate(node->cell);
        field_insert_cell_with_key(dst, new_cell, node->key, node->key_len);
    }
    return dst;
}

int field_merge(struct fs_field *dst, const struct fs_field *src)
{
    if (dst->type != src->type) {
        printf("field_merge:field type not match\n");
        return -1;
    }
    struct cell_hash_node *node, *tmp;
    HASH_ITER(hh, src->hash_tbl, node, tmp) {
        struct cell *cell = node->cell;
        struct cell *dst_cell = field_find_cell_with_key(dst, node->key, node->key_len);
        if (dst_cell == NULL) {
            struct cell *new_cell = cell_duplicate(cell);
            field_insert_cell_with_key(dst, new_cell, node->key, node->key_len);
        } else {
            if (cell_merge(dst_cell, cell) != 0) {
                printf("field_merge:cell merge failed\n");
                return -1;
            }
        }
    }
    return 0;
}

void field_get_tags(const struct fs_field *field, struct fieldstat_tag_list **tag_lists_out, size_t *n_tag_lists_out)
{
    *tag_lists_out = (struct fieldstat_tag_list *)malloc(sizeof(struct fieldstat_tag_list) * HASH_COUNT(field->hash_tbl));
    *n_tag_lists_out = HASH_COUNT(field->hash_tbl);

    struct cell_hash_node *node, *tmp;
    int i = 0;
    HASH_ITER(hh, field->hash_tbl, node, tmp) {
        struct cell *cell = node->cell;
        struct fieldstat_tag *tags = NULL;
        size_t n_tags = 0;
        cell_read_tag(cell, &tags, &n_tags);
        (*tag_lists_out)[i].tag = tags;
        (*tag_lists_out)[i].n_tag = n_tags;
        i++;
    }
}

int field_topk_add(struct fs_field *field, const char *key, size_t key_len, unsigned int score, const struct fieldstat_tag tag_list[], size_t tag_num)
{
    printf("field_topk_add enter :key: %s\n", key);

    struct cell *cell = field_find_or_create_cell(field, tag_list, tag_num);
    cell_topk_add(cell, key, key_len, score);

    return 0;
}

int field_counter_incrby(struct fs_field *field, long long score, const struct fieldstat_tag tags[], size_t n_tag)
{
    printf("field_counter_incrby enter\n");
    
    struct cell *cell = field_find_or_create_cell(field, tags, n_tag);

    cell_counter_incrby(cell, score);

    return 0;
}

int field_histogram_record(struct fs_field *field, long long value, const struct fieldstat_tag tags[], size_t n_tag)
{
    printf("field_histogram_record enter\n");

    struct cell *cell = field_find_or_create_cell(field, tags, n_tag);

    return cell_histogram_record(cell, value);
}

int field_cardinality_add(struct fs_field *field, const char *key, size_t key_len, const struct fieldstat_tag tags[], size_t n_tag)
{
    printf("field_cardinality_add enter\n");

    struct cell *cell = field_find_or_create_cell(field, tags, n_tag);

    return cell_cardinality_add(cell, key, key_len);
}

struct field_paras *deserialize_parameters(mpack_node_t *root, enum field_type type)
{
    mpack_node_t field_paras_arr = mpack_node_map_cstr(*root, "para");
    struct field_paras *field_para = NULL;
    switch (type) {
        case FIELD_TYPE_TOPK:{
            int K = mpack_node_i32(mpack_node_array_at(field_paras_arr, 0));
            field_para = construct_parameters(type, K);
        }
        break;
        case FIELD_TYPE_COUNTER: {
            // para is null
        }
        break;
        case FIELD_TYPE_CARDINALITY: {
            int precision = mpack_node_i32(mpack_node_array_at(field_paras_arr, 0));
            field_para = construct_parameters(type, precision);
        }
        break;
        case FIELD_TYPE_HISTOGRAM: {
            int lowest_trackable_value = mpack_node_i32(mpack_node_array_at(field_paras_arr, 0));
            int highest_trackable_value = mpack_node_i32(mpack_node_array_at(field_paras_arr, 1));
            int significant_figures = mpack_node_i32(mpack_node_array_at(field_paras_arr, 2));
            field_para = construct_parameters(type, lowest_trackable_value, highest_trackable_value, significant_figures);
        }
        break;
        default:
            assert(0);
            break;
    }
    return field_para;
}

/*
{
    name=<instance_name>,
    "fields"=[
        {
            "name":<field_name>
            "type":<field_type>
            "id":<field_id>
            "para":[may be null, one number, or a array]
            // todo: 不要光是一个array，"type": , 等等
            "cells":<bin_blob>
            ]
        }
}
*/
static void my_tag_list_free(struct fieldstat_tag *tag_list, size_t tag_num) {
    for (int i = 0; i < tag_num; i++) {
        if (tag_list->key) {
            free((char *)tag_list->key);
            tag_list->key = NULL;
        }
        if (tag_list->type == TAG_CSTRING && tag_list->value_str) {
            free((char *)tag_list->value_str);
            tag_list->value_str = NULL;
        }
    }
    free(tag_list);
}

struct fs_field *field_deserialize(const char *blob, size_t blob_size)
{
    printf("field_deserialize enter, blob size:: %zu\n", blob_size);
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, blob_size);
    mpack_tree_parse(&tree);
    mpack_node_t root = mpack_tree_root(&tree);

    char *field_name = mpack_node_cstr_alloc(mpack_node_map_cstr(root, "name"), 0xFFFF); // 0xFFFF: a very large number
    enum field_type type = mpack_node_i32(mpack_node_map_cstr(root, "type"));
    int id = mpack_node_i32(mpack_node_map_cstr(root, "id"));
    struct field_paras *field_para = deserialize_parameters(&root, type);
    printf("field_deserialize name is: %s, id is : %d, type is: %d\n", field_name, id, type);

    struct fs_field *field_out = field_new(type, field_name, field_para);
    field_set_id(field_out, id);
    free(field_name);

    mpack_node_t cells_arr = mpack_node_map_cstr(root, "cells");
    if (mpack_node_is_nil(cells_arr)) {
        printf("The field has no serialized cell object.\n");
        mpack_tree_destroy(&tree);
        return field_out;
    }
    
    for (int i = 0; i < mpack_node_array_length(cells_arr); i++) {
        mpack_node_t cell_node = mpack_node_array_at(cells_arr, i);
        struct cell *cell = cell_deserialize(mpack_node_bin_data(cell_node), mpack_node_bin_size(cell_node));
        if (cell == NULL) {
            fprintf(stderr, "deserialize_for_cell_type failed\n");
            mpack_tree_destroy(&tree);
            return NULL;
        }

        struct fieldstat_tag *tag_list = NULL;
        size_t tag_num = 0;
        cell_read_tag(cell, &tag_list, &tag_num);
        field_insert_cell(field_out, cell, tag_list, tag_num);

        my_tag_list_free(tag_list, tag_num);
    }
    printf("debug, the field has %u cells\n", HASH_COUNT(field_out->hash_tbl));
    mpack_tree_destroy(&tree);
    return field_out;
}

void serialize_and_write_field_parameters(mpack_writer_t *writer, const struct fs_field *field)
{
    if (field->field_paras == NULL) {
        mpack_write_nil(writer);
        return;
    }
    switch (field->type) {
        case FIELD_TYPE_TOPK: {

            mpack_build_array(writer);
            mpack_write_i32(writer, field->field_paras->topk.K);
            mpack_complete_array(writer);
        }
            break;
        case FIELD_TYPE_CARDINALITY: {
            mpack_build_array(writer);
            mpack_write_i32(writer, field->field_paras->cardinality.precision);
            mpack_complete_array(writer);
        }
            break;
        case FIELD_TYPE_HISTOGRAM: {
            mpack_build_array(writer);
            mpack_write_int(writer, field->field_paras->hdr.lowest_trackable_value);
            mpack_write_int(writer, field->field_paras->hdr.highest_trackable_value);
            mpack_write_i32(writer, field->field_paras->hdr.significant_figures);
            mpack_complete_array(writer);
        }
            break;
        default:
            assert(0);
            break;
    }
}

/*
    field blob:
        {
            "name":<field_name>
            "type":<field_type>
            "id":<field_id>
            "para":[may be null, one number, or a double array]
            "cells":[<bin_blob>]
        }*/
int field_serialize(const struct fs_field *field, char **blob, size_t *blob_size)
{
    printf("field_serialize enter, field name: %s\n", field->name);
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_size);

    mpack_build_map(&writer);
    mpack_write_cstr(&writer, "name");
    mpack_write_str(&writer, field->name, strlen(field->name));
    mpack_write_cstr(&writer, "type");
    mpack_write_i32(&writer, field->type);
    mpack_write_cstr(&writer, "id");
    mpack_write_i32(&writer, field->field_id);
    mpack_write_cstr(&writer, "para");
    serialize_and_write_field_parameters(&writer, field);
    mpack_write_cstr(&writer, "cells");

    if (HASH_COUNT(field->hash_tbl) == 0) {
        mpack_write_nil(&writer);
    } else {
        printf("serialize cells\n");
        struct cell_hash_node *node, *tmp;
        mpack_build_array(&writer);
        char *cell_blob;
        size_t cell_blob_size;
        HASH_ITER(hh, field->hash_tbl, node, tmp) {
            struct cell *cell = node->cell;
            cell_serialize(cell, &cell_blob, &cell_blob_size);
            mpack_write_bin(&writer, cell_blob, cell_blob_size);
            free(cell_blob);
        }
        mpack_complete_array(&writer);
    }

    mpack_complete_map(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        fprintf(stderr, "field_serialize failed\n");
        return -1;
    }

    return 0;
}

int field_get_id(const struct fs_field *field)
{
    return field->field_id;
}

void field_set_id(struct fs_field *field, int id)
{
    field->field_id = id;
}

enum field_type field_get_type(const struct fs_field *field)
{
    return field->type;
}

void field_query_for_topk(const struct fs_field *field, const struct fieldstat_tag *tag_list, size_t tag_num,
    char ***keys_out, long long **values_out, size_t *num_out)
{
    struct cell *cell = field_find_cell(field, tag_list, tag_num);
    if (cell == NULL) {
        *num_out = 0;
        *keys_out = NULL;
        *values_out = NULL;

        return;
    }

    cell_query_for_topk(cell, keys_out, values_out, num_out);
}

long long field_query_for_counter(const struct fs_field *field, const struct fieldstat_tag *tag_list, size_t tag_num)
{
    struct cell *cell = field_find_cell(field, tag_list, tag_num);
    if (cell == NULL) {
        return 0;
    }

    long long ret = cell_get_for_counter(cell);
    return ret;
}

long long field_query_for_histogram(const struct fs_field *field, const struct fieldstat_tag *tag_list, size_t tag_num, double percentile)
{
    struct cell *cell = field_find_cell(field, tag_list, tag_num);
    if (cell == NULL) {
        return 0;
    }

    long long ret = cell_query_for_histogram(cell, percentile);
    return ret;
}

double field_query_for_cardinality(const struct fs_field *field, const struct fieldstat_tag *tag_list, size_t tag_num)
{
    struct cell *cell = field_find_cell(field, tag_list, tag_num);
    if (cell == NULL) {
        return 0;
    }

    double ret = cell_query_for_cardinality(cell);
    return ret;
}