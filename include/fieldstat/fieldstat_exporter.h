#pragma once
#include <stdio.h>
#include <time.h>
#include "fieldstat.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* -------------------------------------------------------------------------- */
/*                           fieldstat_json_exporter                          */
/* -------------------------------------------------------------------------- */
struct fieldstat_json_exporter;

struct fieldstat_json_exporter *fieldstat_json_exporter_new();
void fieldstat_json_exporter_set_global_tag(struct fieldstat_json_exporter *exporter, const struct fieldstat_tag tag_list[], size_t n_tag);
void fieldstat_json_exporter_set_name(struct fieldstat_json_exporter *exporter, const char *name);
void fieldstat_json_exporter_free(struct fieldstat_json_exporter *exporter);

/*
    Output the fieldstat instance to json string array. User must free the output string.
*/
char *fieldstat_json_exporter_export(const struct fieldstat_json_exporter *exporter, const struct fieldstat *instance, const struct timeval *timestamp);
/*
    after call fieldstat_json_exporter_export_array, user must free the output array and each string.
*/
void fieldstat_json_exporter_export_array(const struct fieldstat_json_exporter *exporter, const struct fieldstat *instance, const struct timeval *timestamp, char ***output, size_t *output_size);

/*
    let json exporter output delta value by the side of the original value(accumulated). If a cell / metric is new, the delta value is the same as the original value.
    Outputting delta value is disabled by default.
    When the exporter name or exporter global tags are changed, or the fieldstat instance is reset, or one of the cube is deleted, the delta value will be reset. (next output will be the original value)
    it is recommended to call this function after fieldstat_json_exporter_set_name and fieldstat_json_exporter_set_global_tag.
    Only affects the metrics of counter type.
    Outputting delta value is time-consuming.
*/
void fieldstat_json_exporter_enable_delta(struct fieldstat_json_exporter *exporter);

#ifdef __cplusplus
}
#endif