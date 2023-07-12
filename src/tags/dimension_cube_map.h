
#pragma once
#include "fieldstat.h"

#ifdef __cplusplus
extern "C"{
#endif


// cube manager
struct cell_manager;

void cell_manager_free(struct cell_manager *pthis);
struct cell_manager *cell_manager_new(enum sampling_mode sampling_mode, int max_cell_num);
void cell_manager_reset(struct cell_manager *pthis);

int cell_manager_get_cell_id_by_tag(const struct cell_manager *pthis, const struct fieldstat_tag *tag, size_t tag_count);
int cell_manager_get_tag_by_cell_id(const struct cell_manager *pthis, int cube_id, struct fieldstat_tag **tag_out, size_t *tag_count_out);
int cell_manager_get_count_by_tag(const struct cell_manager *pthis, const struct fieldstat_tag *tag, size_t tag_count); // only for topk

// todo: 由cell manager控制id，且topk 的id为其在sorted set里的id
int cell_manager_add_cell(struct cell_manager *pthis, const struct fieldstat_tag *tag, size_t tag_count, unsigned int count);


void fieldtag_list_free(struct fieldstat_tag *kv_pairs, size_t n_pairs);
void fieldtag_list_serialize(const struct fieldstat_tag *tag_list, size_t tag_num, char **blob, size_t *blob_size);
void fieldtag_list_deserialize(const char *blob, size_t blob_size, struct fieldstat_tag **tag_list_out, size_t *tag_num_out);

int cell_manager_serialize(const struct cell_manager *pthis, char **blob, size_t *blob_size);
struct cell_manager *cell_manager_deserialize(const char *blob, size_t blob_size);

#ifdef __cplusplus
}
#endif