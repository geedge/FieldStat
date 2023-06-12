#pragma once

#include "serializer.h"
#include "field_and_metric.h"
#include "fieldstat.h"

struct metric_info_query_inheritance {
    struct metric_info base;
    struct fieldstat_tag *field_kvpairs;
    size_t field_num;
};

struct serializer_query {
    struct serializer base;
    struct metric_info_query_inheritance **metric_info;
    size_t metric_num;
    size_t write_pos;
};


// // field_name: refer to enum field_type
// // ID: <its parent id>
//    name: <its parent name>
// // TAG: "IP"
// // TAG_VALUE:"100"
// // fields:[NULL:10]  (for counter/gauge/hll)
// // fields:["50%":10, "90%": 1234, "99%": 65498.] (for Histogram)
// // fields:["100.56465":10, "beijing":1234] (for TOPK)

struct serializer_query *serializer_query_create(size_t metric_num);