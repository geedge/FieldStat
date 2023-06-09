// /* Interface realization: 
//     this serializer is responsible for merge_with_blob_method, so it should carry detailed info so as to recover the instance completely.
//     There is a corresponding deserialization method for such serializer. 

//     serializer_func_access: get metric array ID , tag_key[] tag_value[], bin_array[](base64)   
//     serializer_func_gen gen: return a json string by cjson, as for the format, refer to annotations on serializer_bin_gen, user must free the output blob
//     enum serializer_type type: SERIALIZER_TYPE_MERGE (1);


//     */

// /*
// {
//     name=<instance_name>,

//     "metrics"=[
//         {
//             "tag_k":[<tags key string>]
//             "tag_v":[<tags value string>]
//             "metric_id":<field_id>
//             "blob":<blob>
//             "blob_size":<blob_size>

//         }
//         ...
//         ]
    
//     ts=<unix timestamp>
// }
// */

#include "mpack/mpack.h"
#include "serializer.h"
#include "serializer_merge.h"


// TODO: 给付明卫的output里应该有可以复用的代码，不要这么多个output了，所有serializer放一个文件
// TODO：抽空跟付明卫和OLAP的人讨论一下序列化的结果怎么对齐
void metric_info_free(struct metric_info *p)
{
    if (!p) {
        return;
    }
    if (p->tag_key) {
        for (size_t i = 0; i < p->tag_num; i++) {
            free(p->tag_key[i]);
        }
        free(p->tag_key);
    }
    if (p->tag_val) {
        for (size_t i = 0; i < p->tag_num; i++) {
            free(p->tag_val[i]);
        }
        free(p->tag_val);
    }
    if (p->blob) {
        free(p->blob);
    }
    free(p);
}

void serializer_merge_del(struct serializer *pthis)
{
    struct serializer_merge *p = (struct serializer_merge *)pthis;

    if (p->metric_info) {
        for (size_t i = 0; i < p->metric_num; i++) {
            metric_info_free(p->metric_info[i]);
        }
        free(p->metric_info);
    }
    free(p);
}

void fill_metric_base_info_to_serializer(const struct metrics *base, struct metric_info *info_out)
{
    info_out->field_id = base->container->field_id;
    info_out->field_name = strdup(base->container->name);

    info_out->tag_num = base->tag_num;
    for (int i = 0; i < base->tag_num; i++) {
        info_out->tag_key[i] = strdup(base->tag_key[i]);
        info_out->tag_val[i] = strdup(base->tag_value[i]);
    }
}

int serializer_merge_access(struct serializer *pthis, struct metrics *metrics)
{
    struct serializer_merge *p = (struct serializer_merge *)pthis;

    if (p->write_pos >= p->metric_num) {
        return -1;
    }

    fill_metric_base_info_to_serializer(metrics, p->metric_info[p->write_pos]);
    int ret = metrics->visit(metrics, pthis);
    p->write_pos++;

    return ret;
}
/*
{
    name=<instance_name>,
    "metrics"=[
        {
            "tag_k":[<tags key string>]
            "tag_v":[<tags value string>]
            "field_id":<field_id>
            "blob":<blob>
            "blob_size":<blob_size>
        }
    "fields" = [ // FIXME： 这个方案改动太大了。。说实话关键是我一定要有一个metric为粒度的序列化（或者说打印），那么反正要另外加，不如加到deserilize里，还不破坏结构
        {
        ... id: 
            name:
            type: 
            para(bin)
        ]
    
    ts=<unix timestamp> //TODO
}
*/
//TODO 
void serialize_metric(struct serializer_merge *p, mpack_writer_t *writer)
{
    mpack_write_cstr(writer, "metrics");
    mpack_start_array(writer, p->metric_num);

    for (int i = 0; i < p->metric_num; i++) {
        struct metric_info *info = p->metric_info[i];
        mpack_start_map(writer, 5);

        mpack_write_cstr(writer, "tag_k");
        mpack_start_array(writer, info->tag_num);
        for (int j = 0; j < info->tag_num; j++) {
            mpack_write_cstr(writer, info->tag_key[j]);
        }
        mpack_finish_array(writer);

        mpack_write_cstr(writer, "tag_v");
        mpack_start_array(writer, info->tag_num);
        for (int j = 0; j < info->tag_num; j++) {
            mpack_write_cstr(writer, info->tag_val[j]);
        }
        mpack_finish_array(writer);

        mpack_write_cstr(writer, "field_id");
        mpack_write_i32(writer, info->field_id);

        mpack_write_cstr(writer, "field_name");
        mpack_write_cstr(writer, info->field_name);

        mpack_write_cstr(writer, "blob");
        mpack_write_bin(writer, info->blob, info->blob_size);

        mpack_write_cstr(writer, "blob_size");
        mpack_write_i32(writer, info->blob_size);

        mpack_finish_map(writer);
    }
    mpack_finish_array(writer);
}

int serializer_merge_gen(struct serializer *pthis, const char *instance_name, char **blob, size_t * blob_size)
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
    p->metric_info = (struct metric_info **)malloc(sizeof(struct metric_info *) * metric_num);
    p->metric_num = metric_num;
    p->write_pos = 0;

    return p;
}

