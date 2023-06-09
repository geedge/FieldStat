#pragma once

#include <stddef.h>

#include "field_and_metric.h"

#ifdef __cplusplus
extern "C" {
#endif

enum serializer_type {
    SERIALIZER_TYPE_BIN, // // only an array of <instance name>, [<arrayID>:<bin_which_has_java_deserialize_method_and_convert_by_base64>]
    SERIALIZER_TYPE_MERGE, // the basic serializer. May recover the instance.
    SERIALIZER_TYPE_RESULT, // return an array: struct metric_output[]. Not s standard serializer
    SERIALIZER_TYPE_MAX
};


/*
    * serializer
    *  - serializer_func_access : call a metric.visit to fill this serializer with metric data.
    * - serializer_func_gen: output a blob. Blob is the serialized data. This function should not depend on the metric anymore. The blob are free by user.
    * - serializer_func_del: free the serializer, including all the data writen by metric.visit. Not include the blob allocated in serializer_func_gen.
    * 
    * you should also write a serializer_new function to create a serializer, where realizations of the above three functions are specified.
*/
typedef int (*serializer_func_access)(struct serializer *, struct metrics *);
typedef int (*serializer_func_gen)
    (struct serializer *, char */*instance name*/ ,char ** /*blob*/, size_t * /*blob_size*/);
typedef void (*serializer_func_del)(struct serializer *);
struct serializer {
    serializer_func_del del;
    serializer_func_access access;
    serializer_func_gen gen;
    enum serializer_type type;
};

struct serializer *serializer_create(enum serializer_type type, size_t metric_number);

#ifdef __cplusplus
}
#endif