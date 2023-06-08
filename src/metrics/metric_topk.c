
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>


// #include "cjson/cJSON.h"
#include "fieldstat.h"
#include "serializer.h"
#include "heavy_keeper.h"
#include "field_and_metric.h"

struct metric_topk {
    struct metrics base;
    struct heavy_keeper *hk;
};

void metric_topk_refresh(struct metrics *pthis)
{
    struct metric_topk *topk = (struct metric_topk *)pthis;
    heavy_keeper_clear(topk->hk);
}

void metric_topk_destroy(struct metrics *pthis)
{
    struct metric_topk *topk = (struct metric_topk *)pthis;
    free(pthis->name);
    heavy_keeper_free(topk->hk);
    free(topk);
}

// void heavy_keeper_serialization_with_cjson(struct heavy_keeper *hk, char **blob, size_t *blob_size)
// {
//     cJSON *root = cJSON_CreateObject();
//     cJSON_AddStringToObject(root, "type", "topk");
//     cJSON_AddNumberToObject(root, "K", hk->K);
//     cJSON_AddNumberToObject(root, "width", hk->params.max_bucket_num);
//     cJSON_AddNumberToObject(root, "depth", hk->params.array_num);
//     cJSON_AddNumberToObject(root, "b", hk->params.decay_exp_rate);

//     cJSON *sketch = cJSON_CreateArray();
//     for (int i = 0; i < hk->params.array_num * hk->params.max_bucket_num; i++) {
//         cJSON *item = cJSON_CreateObject();
//         cJSON_AddNumberToObject(item, "FP", hk->sketch[i].finger_print);
//         cJSON_AddNumberToObject(item, "C", hk->sketch[i].count);
//         cJSON_AddItemToArray(sketch, item);
//     }
//     cJSON_AddItemToObject(root, "sketch", sketch);

//     cJSON *ss = cJSON_CreateArray();
//     struct heavy_keeper_result *result = heavy_keeper_query(hk);
//     for (int i = 0; i < result->n_key; i++) {
//         cJSON *item = cJSON_CreateObject();
//         cJSON_AddStringToObject(item, "k", result->key[i]);
//         cJSON_AddNumberToObject(item, "v", result->count[i]);
//         cJSON_AddItemToArray(ss, item);
//     }
//     cJSON_AddItemToObject(root, "ss", ss);

//     char *out = cJSON_PrintUnformatted(root);
//     *blob = out;
//     *blob_size = strlen(out) + 1;
//     cJSON_Delete(root);
//     heavy_keeper_result_free(result);
// }

int metric_topk_visit(struct metrics *pthis, struct serializer *serializer)
{
    struct metric_topk *topk = (struct metric_topk *)pthis;

    switch (serializer->type) {
        case SERIALIZER_TYPE_MERGE:
        {
            struct serializer_merge *merge = (struct serializer_merge *)serializer;
            char *blob;
            size_t blob_size;
            heavy_keeper_serialization(topk->hk, &blob, &blob_size);

            struct metric_info *info = merge->metric_info[merge->write_pos];

            info->blob = blob;
            info->blob_size = blob_size;
            info->type = (int)FIELD_TYPE_TOPK;
            // tag is filled by caller serialize_merge

            return 0;
            break;
        }
        default:
            assert(0);
            break;
    }
}

int metric_topk_merge(struct metrics *pthis, struct metrics *from)
{
    struct metric_topk *topk = (struct metric_topk *)pthis;
    struct metric_topk *from_topk = (struct metric_topk *)from;
    return heavy_keeper_merge(topk->hk, from_topk->hk);
}

void metric_topk_base_init(struct metrics *base)
{
    base->type = FIELD_TYPE_TOPK;
    base->del = metric_topk_destroy;
    base->visit = metric_topk_visit;
    base->refresh = metric_topk_refresh;
    base->merge = metric_topk_merge;
}

struct metrics *metrics_topk_deserialize(const char *blob, size_t blob_size)
{
    struct heavy_keeper *hk = heavy_keeper_deserialization(blob, blob_size);
    struct metric_topk *topk = (struct metric_topk *)malloc(sizeof(struct metric_topk));
    topk->hk = hk;
    metric_topk_base_init((struct metrics *)topk);
    return (struct metrics *)topk;
}

struct metric_topk *metric_topk_create(int K)
{
    struct metric_topk *topk = (struct metric_topk *)malloc(sizeof(struct metric_topk));
    topk->hk = heavy_keeper_new(K, NULL);

    struct metrics *base = (struct metrics *)topk;
    metric_topk_base_init(base);
    return topk;
}

void metric_topk_add(struct metric_topk *counter, const char *key, size_t key_len, unsigned value)
{
    heavy_keeper_add(counter->hk, key, key_len, value);
}