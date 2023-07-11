
#pragma once
#include "fieldstat.h"

// cube manager
struct cell_manager;

void cell_manager_free(struct cell_manager *pthis);
struct cell_manager *cell_manager_new(enum sampling_mode sampling_mode, int max_cell_num);
void cell_manager_reset(struct cell_manager *pthis);

int cell_manager_get_cube_id_by_tag(const struct cell_manager *pthis, const struct fieldstat_tag *tag, size_t tag_count);
int cell_manager_get_tag_by_cube_id(const struct cell_manager *pthis, int cube_id, struct fieldstat_tag **tag_out, size_t *tag_count_out);
int cell_manager_get_count_by_tag(const struct cell_manager *pthis, const struct fieldstat_tag *tag, size_t tag_count); // only for topk

// todo: 由cell manager控制id，且topk 的id为其在sorted set里的id
int cell_manager_add_cube(struct cell_manager *pthis, const struct fieldstat_tag *tag, size_t tag_count, unsigned int count);


void fieldtag_arr_free(struct fieldstat_tag *kv_pairs, size_t n_pairs);