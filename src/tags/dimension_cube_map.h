
#pragma once
#include "fieldstat.h"

// cube manager
struct cell_manager;

void cell_manager_free(struct cell_manager *pthis);
struct cell_manager *cell_manager_create(enum sampling_mode sampling_mode, int max_cell_num);

int cell_manager_get_cube_id_by_tag(const struct cell_manager *pthis, const struct fieldstat_tag *tag, size_t tag_count);
int cell_manager_get_tag_by_cube_id(const struct cell_manager *pthis, int cube_id, struct fieldstat_tag **tag_out, size_t *tag_count_out);

int cell_manager_add_cube(struct cell_manager *pthis, const struct fieldstat_tag *tag, size_t tag_count, unsigned int count, int allocated_cube_id);