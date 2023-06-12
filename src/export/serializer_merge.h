#pragma once


#ifdef __cplusplus
extern "C" {
#endif
#include "serializer.h"
#include "field_and_metric.h"

struct metric_info_merge_inheritance {
    struct metric_info base;
    char *blob;
    size_t blob_size;
};

struct serializer_merge {
    struct serializer base;
    struct metric_info_merge_inheritance **metric_info;
    size_t metric_num;
    size_t write_pos;
};

// todo: 定义一个新的output info， 继承当前的metric_info

struct serializer_merge *serializer_merge_create(size_t metric_num);

#ifdef __cplusplus
}
#endif