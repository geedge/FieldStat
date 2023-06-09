#pragma once


#ifdef __cplusplus
extern "C" {
#endif
#include "serializer.h"


struct serializer_merge {
    struct serializer base;
    struct metric_info **metric_info;
    size_t metric_num;
    size_t write_pos;
};

struct serializer_merge *serializer_merge_create(size_t metric_num);

#ifdef __cplusplus
}
#endif