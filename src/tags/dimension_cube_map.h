
#pragma once
#include "fieldstat.h"

// cube manager
struct cell_manager;

int cell_manager_get_cube_id_by_tag(const struct tag_cube_map *pthis, const struct fieldstat_tag *tag, size_t tag_count);
int cell_manager_get_tag_by_cube_id(const struct tag_cube_map *pthis, int cube_id, struct fieldstat_tag **tag_out, size_t *tag_count_out);

int cell_manager_insert_tag(struct cell_manager *pthis, const struct fieldstat_tag *tag, size_t tag_count);
int cell_manager_insert_tag(struct cell_manager *pthis, const struct fieldstat_tag *tag, size_t tag_count);