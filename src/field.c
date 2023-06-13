#include <assert.h>


#include "fieldstat.h"
#include "field.h"
#include "uthash.h"
#include "metric.h"
#include "mpack/mpack.h"

struct metric_hash_node {
    char *key; // kv_pair of tags
    size_t key_len; // exclude '\0'
    struct metrics *metric;
    UT_hash_handle hh;
};

// struct para_hdr{
//     const char *bins;
//     long long lowest_trackable_value;
//     long long highest_trackable_value;
//     int significant_figures;
// };
struct para_topk {
    int K;
};
// struct para_unique{
//     const char precision;
// };
struct field_paras {
    union {
        // struct para_hdr hdr;
        struct para_topk topk;
        // struct para_unique unique;
    };
};

struct fs_field {
    enum field_type type;
    int field_id;
    char *name;
    struct metric_hash_node *hash_tbl;
    struct field_paras *field_paras;
};

static struct metrics *find_metric_in_field(const struct fs_field *field, const char *key, size_t key_len)
{
    struct metric_hash_node *node;
    HASH_FIND(hh, field->hash_tbl, key, key_len, node);
    if (node == NULL) {
        return NULL;
    }
    return node->metric;
}

// key_len does not include '\0'
void field_insert_metric(struct fs_field *field, struct metrics *metric, const char *key, size_t key_len)
{
    struct metric_hash_node *node = (struct metric_hash_node *)malloc(sizeof(struct metric_hash_node));

    node->key_len = key_len;
    node->key = (char *)malloc(key_len + 1);
    memcpy(node->key, key, key_len);
    node->metric = metric;
    HASH_ADD_KEYPTR(hh, field->hash_tbl, node->key, node->key_len, node);
}

struct fs_field *field_new_topk(const char *name, int K)
{
    struct fs_field *field = (struct fs_field *)malloc(sizeof(struct fs_field));
    field->name = strdup(name);
    field->hash_tbl = NULL;
    field->type = FIELD_TYPE_TOPK;
    field->field_paras = (struct field_paras *)malloc(sizeof(struct field_paras));
    field->field_paras->topk.K = K;
    return field;
}

void field_free(struct fs_field *field)
{
    struct metric_hash_node *node, *tmp;
    HASH_ITER(hh, field->hash_tbl, node, tmp) {
        struct metrics *metric = node->metric;
        metric->del(metric);
        HASH_DEL(field->hash_tbl, node);
        free(node);
    }
    free(field);
}

void field_refresh(struct fs_field *field)
{
    struct metric_hash_node *node, *tmp;
    HASH_ITER(hh, field->hash_tbl, node, tmp) {
        struct metrics *metric = node->metric;
        metric->refresh(metric);
    }
}

struct fs_field *field_dup(const struct fs_field *src)
{
    struct fs_field *dst = (struct fs_field *)malloc(sizeof(struct fs_field));
    dst->name = strdup(src->name);
    dst->hash_tbl = NULL;
    dst->type = src->type;
    dst->field_paras = (struct field_paras *)malloc(sizeof(struct field_paras));
    memcpy(dst->field_paras, src->field_paras, sizeof(struct field_paras));
    struct metric_hash_node *node, *tmp;
    HASH_ITER(hh, src->hash_tbl, node, tmp) {
        struct metrics *metric = node->metric;
        struct metrics *new_metric = metric->dup(metric);
        field_insert_metric(dst, new_metric, node->key, node->key_len);
    }
    return dst;
}

int field_merge(struct fs_field *dst, const struct fs_field *src)
{
    if (dst->type != src->type) {
        printf("field_merge:field type not match\n");
        return -1;
    }
    struct metric_hash_node *node, *tmp;
    HASH_ITER(hh, src->hash_tbl, node, tmp) {
        struct metrics *metric = node->metric;
        struct metrics *dst_metric = find_metric_in_field(dst, node->key, node->key_len);
        if (dst_metric == NULL) {
            struct metrics *new_metric = metric->dup(metric);
            field_insert_metric(dst, new_metric, node->key, node->key_len);
        } else {
            if (metric->merge(metric, dst_metric) != 0) {
                printf("field_merge:metric merge failed\n");
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

    struct metric_hash_node *node, *tmp;
    int i = 0;
    HASH_ITER(hh, field->hash_tbl, node, tmp) {
        struct metrics *metric = node->metric;
        struct fieldstat_tag *tags = NULL;
        size_t n_tags = 0;
        metric->read_tag(metric, &tags, &n_tags);
        (*tag_lists_out)[i].tag = tags;
        (*tag_lists_out)[i].n_tag = n_tags;
        i++;
    }
}

static void build_dynamic_metric_key(const struct fieldstat_tag tags[], size_t n_tags, char **out_key, size_t *out_key_size)
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

int field_topk_add(struct fs_field *field, const char *key, size_t key_len, double score, const struct fieldstat_tag tag_list[], size_t tag_num)
{
    if (field->type != FIELD_TYPE_TOPK) {
        printf("field_topk_add: field type is not topk\n");
        return -1;
    }
    char *dynamic_metric_key = NULL;
    size_t dynamic_key_size = 0;
    build_dynamic_metric_key(tag_list, tag_num, &dynamic_metric_key, &dynamic_key_size);

    struct metrics *metric = find_metric_in_field(field, dynamic_metric_key, dynamic_key_size);
    if (metric == NULL) {
        metric = metric_topk_create(field->field_paras->topk.K);
        metric->add_tag(metric, tag_list, tag_num);
        field_insert_metric(field, metric, dynamic_metric_key, dynamic_key_size);
        free(dynamic_metric_key);
    }

    metric_topk_add(metric, key, key_len, score);
    return 0;
}

/*
{
    name=<instance_name>,
    "fields"=[
        {
            "name":<field_name>
            "type":<field_type>
            "id":<field_id>
            "para":[may be null, one number, or a double array]
            "metrics":<bin_blob>
            ]
        }
}
*/
struct metrics *deserialize_for_metric_type(const char *blob, size_t blob_size, int type)
{
    struct metrics *metric = NULL;
    switch (type) {
        // case FIELD_TYPE_COUNTER:
        //     metric = metric_counter_deserialize(data, data_len, 0);
        //     break;

        case FIELD_TYPE_TOPK:
            metric = metrics_topk_deserialize(blob, blob_size);
            break;

        default:
            assert(0);
            break;
    }
    return metric;
}

struct fs_field *field_new_by_deserialize(const char *blob, size_t blob_size)
{
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, blob_size);
    mpack_tree_parse(&tree);
    mpack_node_t root = mpack_tree_root(&tree);

    const char *field_name = mpack_node_str(mpack_node_map_cstr(root, "name"));
    enum field_type type = mpack_node_i32(mpack_node_map_cstr(root, "type"));
    int id = mpack_node_i32(mpack_node_map_cstr(root, "id"));

    mpack_node_t field_paras_arr = mpack_node_map_cstr(root, "para");
    struct fs_field *field_out = NULL;
    switch (type) {
        case FIELD_TYPE_TOPK:{
            int K = mpack_node_i32(mpack_node_array_at(field_paras_arr, 0));
            field_new_topk(field_name, K);
        }
        break;
        default:
            assert(0);
            break;
    }
    if (field_out == NULL) {
        fprintf(stderr, "field_new failed, field name: %s\n", field_name);
        return NULL;
    }
    field_out->field_id = id;
    mpack_node_t metrics_arr = mpack_node_map_cstr(root, "metrics");
    for (int i = 0; i < mpack_node_array_length(metrics_arr); i++) {
        mpack_node_t metric_node = mpack_node_array_at(metrics_arr, i);
        struct metrics *metric = deserialize_for_metric_type(mpack_node_str(metric_node), mpack_node_i32(mpack_node_map_cstr(metric_node, "blob_size")), type);
        if (metric == NULL) {
            fprintf(stderr, "deserialize_for_metric_type failed\n");
            return NULL;
        }

        struct fieldstat_tag *tag_list = NULL;
        size_t tag_num = 0;
        metric->read_tag(metric, &tag_list, &tag_num);

        char *dynamic_metric_key = NULL;
        size_t dynamic_key_size = 0;
        build_dynamic_metric_key(tag_list, tag_num, &dynamic_metric_key, &dynamic_key_size);
        field_insert_metric(field_out, metric, dynamic_metric_key, dynamic_key_size); // dynamic_metric_key is add to hashtable, so no need to free

        free(dynamic_metric_key);
        free(tag_list);
    }
    return field_out;
}

/*
    field blob:
        {
            "name":<field_name>
            "type":<field_type>
            "id":<field_id>
            "para":[may be null, one number, or a double array]
            "metrics":[<bin_blob>]
        }*/
int field_serialize(struct fs_field *field, char **blob, size_t *blob_size)
{
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
    switch (field->type) {
        case FIELD_TYPE_TOPK:
            mpack_build_array(&writer);
            mpack_write_i32(&writer, field->field_paras->topk.K);
            mpack_complete_array(&writer);
            break;
        default:
            assert(0);
            break;
    }
    struct metric_hash_node *node, *tmp;
    mpack_write_cstr(&writer, "metrics");
    mpack_build_array(&writer);
    char *metric_blob;
    size_t metric_blob_size;
    HASH_ITER(hh, field->hash_tbl, node, tmp) {
        struct metrics *metric = node->metric;
        metric->serialize(metric, &metric_blob, &metric_blob_size);
        mpack_write_bin(&writer, metric_blob, metric_blob_size);
        free(metric_blob);
    }
    mpack_complete_array(&writer);

    mpack_complete_map(&writer);

    if (mpack_writer_destroy(&writer) != mpack_ok) {
        fprintf(stderr, "field_serialize failed\n");
        return -1;
    }
    return 0;
}

int field_get_id(struct fs_field *field)
{
    return field->field_id;
}

enum field_type field_get_type(struct fs_field *field)
{
    return field->type;
}

void field_query_for_topk(const struct fs_field *field, const struct fieldstat_tag *tag_list, size_t tag_num,
    char ***keys_out, long long **values_out, size_t *num_out)
{
    char *dynamic_metric_key = NULL;
    size_t dynamic_key_size = 0;
    build_dynamic_metric_key(tag_list, tag_num, &dynamic_metric_key, &dynamic_key_size);
    struct metrics *metric = find_metric_in_field(field, dynamic_metric_key, dynamic_key_size);

    if (metric == NULL) {
        *num_out = 0;
        *keys_out = NULL;
        *values_out = NULL;
        return;
    }

    metric_topk_query(metric, keys_out, values_out, num_out);
    free(dynamic_metric_key);
}