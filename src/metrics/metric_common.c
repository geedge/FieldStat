
#include "field_and_metric.h"
#include "metric_type_internal.h"

void fill_metric_base_info(const struct metrics *base, struct metric_info *info_out)
{
    info_out->field_id = base->container->field_id;
    info_out->field_name = strdup(base->container->name);

    info_out->tag_num = base->tag_num;
    for (int i = 0; i < base->tag_num; i++) {
        info_out->tag_key[i] = strdup(base->tag_key[i]);
        info_out->tag_val[i] = strdup(base->tag_value[i]);
    }
}

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