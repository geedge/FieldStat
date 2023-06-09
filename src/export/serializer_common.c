#include <assert.h>

#include "serializer.h"
#include "serializer_merge.h"


struct serializer *serializer_create(enum serializer_type type, size_t metric_number)
{
    switch (type) {
        case SERIALIZER_TYPE_MERGE:
            return (struct serializer *)serializer_merge_create(metric_number);
        default:
            assert(0);
            return NULL;
    }
}
