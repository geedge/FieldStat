
#pragma once
#include "fieldstat.h"

struct dimension_cube_map;

int dimension_cube_map_get_cube_id_by_dimension(const struct dimension_cube_map *pthis, struct fieldstat_tag_list *dimension);
int dimension_cube_map_get_dimension_by_cube_id(const struct dimension_cube_map *pthis, int cube_id, struct fieldstat_tag_list **dimension_out);
