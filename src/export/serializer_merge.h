#pragma once


#ifdef __cplusplus
extern "C" {
#endif
#include "serializer.h"

struct metric_info {
    char **tag_key;
    char **tag_val;
    size_t tag_num;
    int field_id;
    char *field_name; // its father name
    char *blob;
    size_t blob_size;
    int type; // refer to enum field_type
};

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