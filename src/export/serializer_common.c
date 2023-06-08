#include <assert.h>

#include "serializer.h"

#include "serializer_bin.h"
#include "serializer_bin.h"


struct serializer *serializer_create(enum serializer_type type, size_t metric_number)
{
    switch (type) {
        case SERIALIZER_TYPE_BIN:
            return (struct serializer *)serializer_bin_create(metric_number);
        default:
            assert(0);
            break;
    }
}
