#include <assert.h>


#include "fieldstat.h"
#include "field_and_metric.h"
#include "uthash.h"
#include "metric_type_internal.h"

// struct metrics *field_spawn_new_metrics(struct fs_field *field, const char *tag_key[], const char *tag_value[], size_t tag_num)

// struct fs_field *field_create(enum field_type type, int field_id, const char *name, size_t name_len)
// field_register_topk()..


static void tags_unzip_to_metric(const struct fieldstat_tag *tags, size_t n_tag, struct metrics *metric)
{
    for (int i = 0; i < n_tag; i++) {
        tag_pair_unzip(&tags[i], &(metric->tag_key[i]), &(metric->tag_value[i]));
    }
    metric->tag_num = n_tag;
}

struct fs_field *field_new(enum field_type type, const char *name, struct field_paras *param)
{
    struct fs_field *field = (struct fs_field *)malloc(sizeof(struct fs_field));
    field->name = strdup(name);
    field->hash_tbl = NULL;
    field->type = FIELD_TYPE_TOPK;
    field->field_paras = (struct field_paras *)malloc(sizeof(struct field_paras));
    memcpy(field->field_paras, param, sizeof(struct field_paras));
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

struct metrics *find_metric_in_field(struct fs_field *field, const char *key, size_t key_len)
{
    struct metric_hash_node *node;
    HASH_FIND(hh, field->hash_tbl, key, key_len, node);
    if (node == NULL) {
        return NULL;
    }
    return node->metric;
}

void insert_metric_to_field(struct fs_field *field, struct metrics *metric, const char *key, size_t key_len)
{
    metric->container = field;
    struct metric_hash_node *node = (struct metric_hash_node *)malloc(sizeof(struct metric_hash_node));
    node->key = strdup(key);
    node->key_len = key_len;
    node->metric = metric;
    HASH_ADD_KEYPTR(hh, field->hash_tbl, node->key, node->key_len, node);
}

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

void metric_add_tag(struct metrics *metric, const char *key, const char *value)
{
    if (metric->tag_num >= MAX_TAG_NUM) {
        printf("ERR: tag num exceed max tag num\n");
        return;
    }
    metric->tag_key[metric->tag_num] = strdup(key);
    metric->tag_value[metric->tag_num] = strdup(value);
    metric->tag_num++;
}

struct metrics *field_spawn_new_metrics(struct fs_field *field, const struct fieldstat_tag tags[], size_t n_tag)
{
    struct metrics *metric = NULL;
    switch (field->type) {
        // case FIELD_TYPE_COUNTER:
        //     metric = metric_counter_create();
        //     break;

        case FIELD_TYPE_TOPK:
            metric = metric_topk_create(field->field_paras->para.topk.K);
            break;

        // case FIELD_TYPE_UNIQUE:
        //     metric = metric_unique_create();
        //     break;

        default:
            assert(0);
            break;
    }
    tags_unzip_to_metric(tags, n_tag, metric);
    return metric;
}

int field_topk_add(struct fs_field *field, const char *key, size_t key_len, double score, const struct fieldstat_tag tags[], size_t n_tag)
{
    if (field->type != FIELD_TYPE_TOPK) {
        printf("ERR: field is not a topk field\n");
        return -1;
    }

    char dynamic_metric_key[512];
    int len = build_dynamic_metric_key(tags, n_tag, sizeof(dynamic_metric_key), dynamic_metric_key);
    struct metrics *metric = find_metric_in_field(field, dynamic_metric_key, len);
    if (metric == NULL) {
        metric = field_spawn_new_metrics(field, tags, n_tag);
        insert_metric_to_field(field, metric, dynamic_metric_key, len);
    }

    metric_topk_add(metric, key, key_len, score);
    return 0;
}

