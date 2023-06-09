
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "cjson/cJSON.h"
#include "mpack/mpack.h"
#include "fieldstat.h"
#include "serializer.h"
#include "field_and_metric.h"
#include "fieldstat_tag.h"

#include "uthash.h"


struct fieldstat {
    char name[INSTANCE_NAME_MAX_LEN];
	// size_t n_tag;
	// char *tag_key[MAX_TAG_NUM];
	// char *tag_value[MAX_TAG_NUM];

    struct fs_field *field_array[METRICS_MAX_NUMBER]; // all the metrics are dynamic(differ from field stat 4.0)
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


struct fieldstat *fieldstat_new(const char *symbol)
{
    struct fieldstat *instance = malloc(sizeof(struct fieldstat));
    memset(instance, 0, sizeof(struct fieldstat));
    strncpy(instance->name, symbol, INSTANCE_NAME_MAX_LEN);

    return instance;
}

void fieldstat_free(struct fieldstat *instance)
{
    for (int i = 0; i < instance->n_field; i++) {
        field_free(instance->field_array[i]);
    }
    free(instance);
}

void fieldstat_serialize(struct fieldstat *instance, char **blob, size_t *blob_size)
{
    struct serializer* serializer = serializer_create(SERIALIZER_TYPE_MERGE, instance->n_field);

    for (int i = 0; i < instance->n_field; i++) {
        struct fs_field *field = instance->field_array[i];
        struct metric_hash_node *node, *tmp;
        HASH_ITER(hh, field->hash_tbl, node, tmp) {
            struct metrics *metric = node->metric;
            if (serializer->access(serializer, metric) != 0) {
                fprintf(stderr, "serializer access failed\n");
                return;
            }
        }
    }

    serializer->gen(serializer, instance->name, blob, blob_size);
    serializer->del(serializer);
}

void fieldstat_refresh(struct fieldstat *instance)
{
    for (int i = 0; i < instance->n_field; i++) {
        struct fs_field *field = instance->field_array[i];
        struct metric_hash_node *node, *tmp;
        HASH_ITER(hh, field->hash_tbl, node, tmp) {
            struct metrics *metric = node->metric;
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
    //TODO K->struct para   
    struct field_paras *params = malloc(sizeof(struct field_paras));
    params->para.topk.K = K;

    struct fs_field *field = field_new(FIELD_TYPE_TOPK, field_name, params);

    return fieldstat_insert_field(instance, field);
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

int fieldstat_topk_add(struct fieldstat *instance, int field_id, const char *key, size_t key_len, const struct fieldstat_tag tags[], size_t n_tag)
{
    struct fs_field *field = (struct fs_field *)instance->field_array[field_id];
    if (field == NULL || field->type != FIELD_TYPE_TOPK) {
        printf("ERR: field is not a topk field\n");
        return -1;
    }
    field_topk_add(field, key, key_len, 1, tags, n_tag);
    return 0;
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


struct fieldstat *fieldstat_deserialize_by_mpack(const char *blob, size_t blob_size)
{
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, blob_size);
    mpack_tree_parse(&tree);
    mpack_node_t root = mpack_tree_root(&tree);

    struct fieldstat *instance = fieldstat_new(mpack_node_str(mpack_node_map_cstr(root, "name")));

    // serialized data dont  have field, so we recover field info from metrics

    mpack_node_t metrics_obj = mpack_node_map_cstr(root, "metrics");
    for (int i = 0; i < mpack_node_array_length(metrics_obj); i++) {
        mpack_node_t metric_obj = mpack_node_array_at(metrics_obj, i);
        mpack_node_t tag_k = mpack_node_map_cstr(metric_obj, "tag_k");
        mpack_node_t tag_v = mpack_node_map_cstr(metric_obj, "tag_v");

        int field_id = mpack_node_int(mpack_node_map_cstr(metric_obj, "field_id"));
        const char *field_name = mpack_node_str(mpack_node_map_cstr(metric_obj, "field_name"));
        mpack_node_t metric_blob_obj = mpack_node_map_cstr(root, "blob");
        size_t metric_blob_size = mpack_node_bin_size(metric_blob_obj);
        const char *metric_blob = mpack_node_bin_data(metric_blob_obj);
        enum field_type type = (enum field_type)mpack_node_int(mpack_node_map_cstr(metric_obj, "type"));

        struct fs_field *field = (struct fs_field *)instance->field_array[field_id];
        if (field == NULL) {
            field = (struct fs_field *)malloc(sizeof(struct fs_field));
            field->hash_tbl = NULL;
            field->type = type;
            field->field_id = field_id;
            field->name = strdup(field_name);
            field->field_paras = NULL;    // Because the result of deserialization is only used for query, the param of registered field is not recorded.

            instance->field_array[field_id] = field; // FIXME: 所有的instance必须register同样的field，且register顺序也要相同           
        }
        struct metrics *metric = deserialize_for_metric_type(metric_blob, metric_blob_size, type);
        if (metric == NULL) {
            fprintf(stderr, "deserialize_for_metric_type failed\n");
            return NULL;
        }
        for (int j = 0; j < mpack_node_array_length(tag_k); j++) {
            metric->tag_key[j] = strdup(mpack_node_str(mpack_node_array_at(tag_k, j)));
            metric->tag_value[j] = strdup(mpack_node_str(mpack_node_array_at(tag_v, j)));
            metric->tag_num++;
        }
        char dynamic_metric_key[512];
        int tmp_len = build_dynamic_metric_key_with_unzipped_tag((const char **)metric->tag_key, (const char **)metric->tag_value, metric->tag_num, dynamic_metric_key, 512);
        if (tmp_len < 0) {
            fprintf(stderr, "build_dynamic_metric_key_with_unzipped_tag failed\n");
            return NULL;
        }
        insert_metric_to_field(field, metric, dynamic_metric_key, tmp_len);
    }
    return instance;
}

int fieldstat_merge_with_blob(struct fieldstat *instance, const char *blob, size_t blob_size)
{
    struct fieldstat *src = fieldstat_deserialize_by_mpack(blob, blob_size);
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
        if (instance->field_array[i] == NULL) {
            instance->field_array[i] = field_new(field->type, field->name, field->field_paras);
        }

        struct metric_hash_node *node, *tmp;
        HASH_ITER(hh, field->hash_tbl, node, tmp) {
            struct metrics *dest_metric = find_metric_in_field(instance->field_array[i], node->key, node->key_len);
            if (dest_metric == NULL) {
                struct metrics *dup = node->metric->dup(node->metric);
                insert_metric_to_field(instance->field_array[i], dup, node->key, node->key_len);
            } else {
                dest_metric->merge(dest_metric, node->metric);
            }
        }
    }
    return 0;
}