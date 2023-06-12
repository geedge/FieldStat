#pragma once
#include "fieldstat.h"


struct metric_info {
    char *tag_key[MAX_TAG_NUM];
    char *tag_val[MAX_TAG_NUM];
    size_t tag_num;
    int field_id;
    char *field_name; // the name of field, its container.
    enum field_type field_type;
    int type; // metric_info type, refer to enum serializer_type(since they are corresponding)
};

void metric_info_clear(struct metric_info *p);
