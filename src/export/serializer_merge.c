// /* Interface realization: 
//     this serializer is responsible for merge_with_blob_method, so it should carry detailed info so as to recover the instance completely.
//     There is a corresponding deserialization method for such serializer. 

//     serializer_func_access: simply call metric.dump to fill the serializer
//     serializer_func_gen gen: return a mpack blob. user must free the blob
//     enum serializer_type type: SERIALIZER_TYPE_MERGE (1); //  TODO: type -> int annote


//     */

// /*
/*
{
    name=<instance_name>,
    "metrics"=[
        {
            "tag_k":[<tags key string>]
            "tag_v":[<tags value string>]
            "field_id":<field_id>
            "field_name":<its_container_name>
            "blob":<blob>
            "blob_size":<blob_size>
        }
    ts=<unix timestamp> //TODO
}
*/
#include "mpack/mpack.h"
#include "serializer.h"
#include "serializer_merge.h"
#include "metric_type_internal.h"
// TODO: 给付明卫的output里应该有可以复用的代码，不要这么多个output了，所有serializer放一个文件
// TODO：抽空跟付明卫和OLAP的人讨论一下序列化的结果怎么对齐


static void serializer_merge_del(struct serializer *pthis)
{
    struct serializer_merge *p = (struct serializer_merge *)pthis;

    if (p->metric_info) {
        for (size_t i = 0; i < p->metric_num; i++) {
            metric_info_clear(&p->metric_info[i]->base);
            free(p->metric_info[i]->blob);
            free(p->metric_info[i]);
        }

        free(p->metric_info);
    }
    free(p);
}

static int serializer_merge_access(struct serializer *pthis, const struct metrics *metrics)
{
    struct serializer_merge *p = (struct serializer_merge *)pthis;

    if (p->write_pos >= p->metric_num) {
        return -1;
    }

    int ret = metrics->dump(metrics, (struct metric_info *)p->metric_info[p->write_pos]);
    p->write_pos++;

    return ret;
}

static void serialize_metric(const struct serializer_merge *p, mpack_writer_t *writer)
{
    mpack_write_cstr(writer, "metrics");
    mpack_start_array(writer, p->metric_num);

    for (int i = 0; i < p->metric_num; i++) {
        struct metric_info_merge_inheritance *info = p->metric_info[i];
        mpack_start_map(writer, 5);

        mpack_write_cstr(writer, "tag_k");
        mpack_start_array(writer, info->base.tag_num);
        for (int j = 0; j < info->base.tag_num; j++) {
            mpack_write_cstr(writer, info->base.tag_key[j]);
        }
        mpack_finish_array(writer);

        mpack_write_cstr(writer, "tag_v");
        mpack_start_array(writer, info->base.tag_num);
        for (int j = 0; j < info->base.tag_num; j++) {
            mpack_write_cstr(writer, info->base.tag_val[j]);
        }
        mpack_finish_array(writer);

        mpack_write_cstr(writer, "field_id");
        mpack_write_i32(writer, info->base.field_id);

        mpack_write_cstr(writer, "field_name");
        mpack_write_cstr(writer, info->base.field_name);

        mpack_write_cstr(writer, "blob");
        mpack_write_bin(writer, info->blob, info->blob_size);

        mpack_write_cstr(writer, "blob_size");
        mpack_write_i32(writer, info->blob_size);

        mpack_finish_map(writer);
    }
    mpack_finish_array(writer);
}

static int serializer_merge_gen(const struct serializer *pthis, const char *instance_name, char **blob, size_t * blob_size)
{
    struct serializer_merge *p = (struct serializer_merge *)pthis;
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_size);
    mpack_build_map(&writer);

    mpack_write_cstr(&writer, "name");
    mpack_write_cstr(&writer, instance_name);

    serialize_metric(p, &writer);

    if (mpack_writer_destroy(&writer) != mpack_ok) {
        fprintf(stderr, "An error occurred in serializer_merge_gen!\n");
        return -1;
    }
    return 0;
}

struct serializer_merge *serializer_merge_create(size_t metric_num)
{
    struct serializer_merge *p = (struct serializer_merge *)malloc(sizeof(struct serializer_merge));
    p->base.type = SERIALIZER_TYPE_MERGE;
    p->base.access = serializer_merge_access;
    p->base.gen = serializer_merge_gen;
    p->base.del = serializer_merge_del;
    p->metric_info = (struct metric_info_merge_inheritance **)malloc(sizeof(struct metric_info_merge_inheritance *) * metric_num);

    for (size_t i = 0; i < metric_num; i++) {
        p->metric_info[i] = (struct metric_info_merge_inheritance *)malloc(sizeof(struct metric_info_merge_inheritance));
        memset(p->metric_info[i], 0, sizeof(struct metric_info_merge_inheritance));
    }
    p->metric_num = metric_num;
    p->write_pos = 0;

    return p;
}

