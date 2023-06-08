
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "cjson/cJSON.h"
#include "mpack/mpack.h"
#include "fieldstat.h"
#include "serializer.h"
#include ""

#include "uthash.h"



struct para_hdr{
    const char *bins;
    long long lowest_trackable_value;
    long long highest_trackable_value;
    int significant_figures;
};

struct para_topk {
    int K;
};

struct para_unique{
    const char precision;
};

struct field_paras {
    union {
        struct para_hdr hdr;
        struct para_topk topk;
        struct para_unique unique;
    } para;
}

struct fieldstat {
    char name[INSTANCE_NAME_MAX_LEN];
	// size_t n_tag;
	// char *tag_key[N_TAG_MAX];
	// char *tag_value[N_TAG_MAX];

    struct fs_field *field_array[METRICS_MAX_NUMBER]; // all the metrics are dynamic(differ from field stat 4.0)
    struct field_paras *field_paras_array[METRICS_MAX_NUMBER];
    size_t n_field;
};


int append_field_to_instance(struct fieldstat *instance, struct fs_field *field)
{
    if (instance->n_field >= METRICS_MAX_NUMBER) {
        printf("append_field_to_instance: too many metrics\n");
        return -1;
    }
    instance->field_array[instance->n_field++] = field;
    return instance->n_field - 1;
}

void tag_pair_unzip(const struct fieldstat_tag *tag, char *tag_key_out, char *tag_value_out)
{
    tag = (struct fieldstat_tag *)&tags[i];
    tag_key_out[i] = strdup(tag->key);
    switch(tag->value_type)
    {
        case TAG_INTEGER:
            memset(str_longlong, 0, sizeof(str_longlong));
            snprintf(str_longlong, sizeof(str_longlong), "%lld", tag->value_int);
            tag_value_out[i] = strdup((const char *)str_longlong);
            break;
        case TAG_DOUBLE:
            memset(str_double, 0, sizeof(str_double));
            snprintf(str_double, sizeof(str_double), "%lf", tag->value_double);
            tag_value_out[i] = strdup((const char *)str_double);
            break;
        case TAG_CSTRING:
            tag_value_out[i] = strdup(tag->value_str);
            break;
        default:
            assert(0);
            break;
    }
}

void tags_unzip_to_metric(const struct fieldstat_tag *tags, size_t n_tag, struct metrics *metric)
{
    for (int i = 0; i < n_tag; i++) {
        tag_pair_unzip(&tags[i], metric->tag_key[i], metric->tag_value[i]);
    }
    metric->tag_num = n_tag;
}

struct fieldstat *fieldstat_new(const char *symbol)
{
    struct fieldstat *instance = malloc(sizeof(struct fieldstat));
    memset(instance, 0, sizeof(struct fieldstat));
    strncpy(instance->name, name, INSTANCE_NAME_MAX_LEN);

    return instance;
}

void field_free(struct field *field)
{
    struct metric_hash_node *node, *tmp;
    HASH_ITER(hh, field->metrics, node, tmp) {
        struct metric *metric = node->metric;
        metric->del(metric);
        HASH_DEL(field->metrics, node);
        free(node);
    }
    free(field);
}

void fieldstat_free(struct fieldstat *instance)
{
    for (int i = 0; i < instance->n_field; i++) {
        field_free(instance->field_array[i]);
        free(instance->field_paras_array[i]);
    }
    free(instance);
}

void fieldstat_serialize(struct fieldstat *instance, char **blob, size_t *blob_size)
{
    struct serializer* serializer = serializer_create(SERIALIZER_TYPE_MERGE, instance->n_field);

    for (int i = 0; i < instance->n_field; i++) {
        struct fs_field *field = instance->field_array[i];
        struct metric_hash_node *node, *tmp;
        HASH_ITER(hh, field->metrics, node, tmp) {
            struct metric *metric = node->metric;
            if (serializer->access(serializer, instance->metrics[i]) != 0) {
                fprintf(stderr, "serializer access failed\n");
                return;
            }
        }
    }

    serializer->gen(serializer, instance->name, (const char **)instance->tag_key, (const char **)instance->tag_value, instance->n_tag, blob, blob_size);
    serializer->del(serializer);
}

void fieldstat_refresh(struct fieldstat *instance)
{
    for (int i = 0; i < instance->n_field; i++) {
        struct fs_field *field = instance->field_array[i];
        struct metric_hash_node *node, *tmp;
        HASH_ITER(hh, field->metrics, node, tmp) {
            struct metric *metric = node->metric;
            metric->refresh(metric);
        }
    }
}

int fieldstat_insert_field(struct fieldstat *instance, struct fs_field *field)
{
    instance->field_array[instance->n_field++] = field;
    field->field_id = instance->n_field - 1;
    return instance->n_field - 1;
}

// int fieldstat_register_counter(struct fieldstat *instance, const char *field_name)
// {
//     struct fs_field *field = (struct fs_field *)malloc(sizeof(struct fs_field));
//     field->name = strdup(field_name);
//     field->name_len = strlen(field_name);
//     field->hash_tbl = NULL;
//     field->type = FIELD_TYPE_COUNTER;
//     return fieldstat_insert_field(instance, field);
// }

int fieldstat_register_topk(struct fieldstat *instance, const char *field_name, int K)
{
    struct fs_field *field = (struct fs_field *)malloc(sizeof(struct fs_field));
    field->name = strdup(field_name);
    field->name_len = strlen(field_name);
    field->hash_tbl = NULL;
    field->type = FIELD_TYPE_TOPK;
    int id = fieldstat_insert_field(instance, field);
    instance->field_paras_array[id] = malloc(sizeof(struct topk_paras));
    struct topk_paras *paras = (struct topk_paras *)instance->field_paras_array[id];
    paras->K = K;

    return id;
}

struct metrics *find_metric_in_field(struct fs_field *field, const char *key, size_t key_len)
{
    struct metric_hash_node *node;
    HASH_FIND_STR(field->metrics, key, node);
    if (node == NULL) {
        return NULL;
    }
    return node->metric;
}

static int build_dynamic_metric_key(const struct fieldstat_tag tags[], size_t n_tags, size_t out_key_size, char *out_key)
{
	int i = 0;
	int used_len = 0;
	struct fieldstat_tag *tag = NULL;

	for(i = 0; i < (int)n_tags; i++)
	{
		tag = (struct fieldstat_tag *)&tags[i];
		switch(tag->value_type)
		{
			case 0:
				used_len += snprintf(out_key + used_len, out_key_size - used_len, "%s%lld", tag->key, tag->value_int);
				break;
			case 1:
				used_len += snprintf(out_key + used_len, out_key_size - used_len, "%s%lf", tag->key, tag->value_double);
				break;
			case 2:
				used_len += snprintf(out_key + used_len, out_key_size - used_len, "%s%s", tag->key, tag->value_str);
				break;
			default:
				assert(0);
				break;
		}
	}
	return  used_len;
}

void insert_metric_to_field(struct fs_field *field, struct metrics *metric, const char *key, size_t key_len)
{
    metric->father = field;
    struct metric_hash_node *node = (struct metric_hash_node *)malloc(sizeof(struct metric_hash_node));
    node->key = strdup(key);
    node->key_len = key_len;
    node->metric = metric;
    HASH_ADD_KEYPTR(hh, field->metrics, node->key, node->key_len, node);
}

// int fieldstat_counter_incrby(struct fieldstat *instance, int field_id, long long increment, const struct fieldstat_tag tags[], size_t n_tag);
// {
//     char dynamic_metric_key[512];
//     int len = build_dynamic_metric_key(tags, n_tag, sizeof(dynamic_metric_key), dynamic_metric_key);
//     struct fs_field *field = (struct fs_field *)instance->metrics[metric_id];
    // if (field->type != FIELD_TYPE_COUNTER) {
    //     printf("ERR: field is not a counter field\n");
    //     return -1;
    // }
//     struct metrics *metric = find_metric_in_field(field, dynamic_metric_key, len);
//     if (metric == NULL) {
//         metric = metric_counter_create();
//         // for tag:
//         //     tags_pair_unzip

//         insert_metric_to_field(field, metric, dynamic_metric_key, len);
//     }
//     metric_counter_add(metric, increment);
//     return 0;
// }

int fieldstat_topk_add(struct fieldstat *instance, int field_id, const char *key, size_t key_len, double score, const struct fieldstat_tag tags[], size_t n_tag)
{
    char dynamic_metric_key[512];
    int len = build_dynamic_metric_key(tags, n_tag, sizeof(dynamic_metric_key), dynamic_metric_key);
    struct fs_field *field = (struct fs_field *)instance->field_array[field_id];
    if (field->type != FIELD_TYPE_TOPK) {
        printf("ERR: field is not a topk field\n");
        return -1;
    }
    struct metrics *metric = find_metric_in_field(field, dynamic_metric_key, len);
    if (metric == NULL) {
        int K = ((struct topk_paras *)instance->field_paras_array[field_id])->K;
        metric = metric_topk_create(K);
        tags_unzip_to_metric(tags, n_tag, metric);
        insert_metric_to_field(field, metric, dynamic_metric_key, len);
    }
    metric_topk_add(metric, key, key_len, score);
    return 0;
}

struct metrics *deserialize_for_metric_type(const char *data, size_t data_len, int type)
{
    struct metrics *metric = NULL;
    switch (type) {
        case FIELD_TYPE_COUNTER:
            metric = metric_counter_deserialize(data, data_len, 0);
            break;

        case FIELD_TYPE_TOPK:
            metric = metrics_topk_deserialize(struct metrics *pthis, const char *blob, size_t blob_size)
            break;

        default:
            assert(0);
            break;
    }
    return metric;
}
// {
//     name=<instance_name>,

//     "metrics"=[
//         {
//             "tag_k":[<tags key string>]
//             "tag_v":[<tags value string>]
//             "field_id":<field_id>
//             "blob":<blob>
//             "blob_size":<blob_size>
//              "type":<type>

//         }
//         ...
//         ]
    
//     ts=<unix timestamp> //TODO
// }
struct swarm_sketch *fieldstat_deserialize_by_mpack(const char *data, size_t data_len)
{
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, size);
    mpack_tree_parse(&tree);
    mpack_node_t root = mpack_tree_root(&tree);

    mpack_node_t name = mpack_node_map_cstr(root, "name");
    mpack_node_t metrics = mpack_node_map_cstr(root, "metrics");
    char **tag_ks;
    char **tag_vs;
    // parse metrics
    for (int i = 0; i < mpack_node_array_length(metrics); i++) {
        mpack_node_t metric = mpack_node_array_at(metrics, i);
        mpack_node_t tag_k = mpack_node_map_cstr(metric, "tag_k");
        mpack_node_t tag_v = mpack_node_map_cstr(metric, "tag_v");
        mpack_node_t field_id = mpack_node_map_cstr(metric, "field_id");
        mpack_node_t field_name = mpack_node_map_cstr(metric, "field_name");
        mpack_node_t blob = mpack_node_map_cstr(metric, "blob");
        mpack_node_t blob_size = mpack_node_map_cstr(metric, "blob_size");
        mpack_node_t type = mpack_node_map_cstr(metric, "type");


        struct fs_field *field = (struct fs_field *)instance->metrics[mpack_node_int(field_id)];
        if (field == NULL) {
            field = (struct fs_field *)malloc(sizeof(struct fs_field));
            field->hash_tbl = NULL;
            field->type = mpack_node_int(type);
            field->field_id = mpack_node_int(field_id);
            field->name = strdup(mpack_node_str(field_name));
            instance->metrics[field->field_id] = field;
        }
        struct metrics* metric = deserialize_for_metric_type(type->valueint, mpack_node_str(blob), mpack_node_int(blob_size));
        if (metric == NULL) {
            fprintf(stderr, "deserialize_for_metric_type failed\n");
            return NULL;
        }
        for (int j = 0; j < mpack_node_array_length(tag_k); j++) {
            metric->tag_key[j] = strdup(mpack_node_str(mpack_node_array_at(tag_k, j)));
            metric->tag_value[j] = strdup(mpack_node_str(mpack_node_array_at(tag_v, j)));
        }
        char dynamic_metric_key[512];
        int tmp_len = 0;
        for (int j = 0; j < mpack_node_array_length(tag_k); j++) {
            tmp_len += snprintf(dynamic_metric_key + tmp_len, 512 - tmp_len, "%s=%s,", metric->tag_key[j], metric->tag_value[j]);
        }
        insert_metric_to_field(field, metric, dynamic_metric_key, tmp_len);
    }
}

int fieldstat_merge_with_blob(struct swarm_sketch *instance, const char *blob, size_t blob_size)
{
    struct swarm_sketch *src = fieldstat_deserialize_by_mpack(blob, blob_size);
    if (src == NULL) {
        fprintf(stderr, "fieldstat_deserialize failed\n");
        return -1;
    }
    if (instance->n_field != src->n_field) {
        fprintf(stderr, "instance->n_field != src->n_field\n");
        return -1;
    }

    for (int i = 0; i < src->n_field; i++) {
        struct fs_field *field = src->field_array[i];
        struct metric_hash_node *node, *tmp;
        HASH_ITER(hh, field->metrics, node, tmp) {
            find_metric_in_field(instance->field_array[i], node->key, node->key_len);
            if (node->metric == NULL) {
                struct metrics *dup = node->metric->dup(node->metric);
                // TODO: 复制方法
                insert_metric_to_field(instance->field_array[i], dup, node->key, node->key_len);
                // TODO: 错误检查
            } else {
                instance->field_array[i]->metrics->metric->merge(instance->field_array[i]->metrics->metric, node->metric);
            }
        }
    }
}