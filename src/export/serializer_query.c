
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "serializer.h"
#include "serializer_query.h"
#include "fieldstat_tag.h"


//    Its very similar to a serializer, but do not output a blob. So its gen() method is NULL
//    instance_query is used to output the current metrics of instance server, which will:
//         firstly merge all instance from different process or thread, then output the merged result.
//    Also have a set of APIs to access the query result.

// 直接让query 在最上层，调serializer和metric，serializer 里拿到base_info 的那个函数，然后简单for metric 用就行了。
// 这样metric_info 也不用继承了，让dump的时候调一下就行。

static void serializer_query_del(struct serializer *pthis)
{
    struct serializer_query *p = (struct serializer_query *)pthis;

    if (p->metric_info) {
        for (size_t i = 0; i < p->metric_num; i++) {
            for (size_t j = 0; j < p->metric_info[i]->field_num; j++) {
                kv_pair_clear(&p->metric_info[i]->field_kvpairs[j]);
            }
            free(p->metric_info[i]->field_kvpairs);
            metric_info_clear(&p->metric_info[i]->base);
            free(p->metric_info[i]);
        }

        free(p->metric_info);
    }
    free(p);
}

static int serializer_query_access(struct serializer *pthis, const struct metrics *metrics)
{
    struct serializer_query *p = (struct serializer_query *)pthis;

    if (p->write_pos >= p->metric_num) {
        return -1;
    }

    int ret = metrics->dump(metrics, (struct metric_info *)p->metric_info[p->write_pos]);
    p->write_pos++;

    return ret;
}

static int serializer_query_gen(const struct serializer *pthis, const char *instance_name, char **blob, size_t * blob_size)
{
    printf("serializer_query_gen should not be called\n");
    return -1;
}

struct serializer_query *serializer_query_create(size_t metric_num)
{
    struct serializer_query *p = (struct serializer_query *)malloc(sizeof(struct serializer_query));
    p->base.type = SERIALIZER_TYPE_QUERY;
    p->base.access = serializer_query_access;
    p->base.gen = serializer_query_gen;
    p->base.del = serializer_query_del;
    p->metric_info = (struct metric_info_query_inheritance **)malloc(sizeof(struct metric_info_query_inheritance *) * metric_num);

    for (size_t i = 0; i < metric_num; i++) {
        p->metric_info[i] = (struct metric_info_query_inheritance *)malloc(sizeof(struct metric_info_query_inheritance));
        memset(p->metric_info[i], 0, sizeof(struct metric_info_query_inheritance));
    }
    p->metric_num = metric_num;
    p->write_pos = 0;

    return p;
}