#include <assert.h>

#include "serializer.h"
#include "serializer_merge.h"
#include "field_and_metric.h"


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

void fill_metric_base_info(const struct metrics *base, enum serializer_type serializer_type, struct metric_info *info_out)
{
    info_out->field_id = base->container->field_id;
    info_out->field_name = strdup(base->container->name);

    info_out->tag_num = base->tag_num;
    for (int i = 0; i < base->tag_num; i++) {
        info_out->tag_key[i] = strdup(base->tag_key[i]);
        info_out->tag_val[i] = strdup(base->tag_value[i]);
    }
    info_out->type = serializer_type;
    info_out->type = base->type;
}

void metric_info_clear(struct metric_info *p)
{
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
}