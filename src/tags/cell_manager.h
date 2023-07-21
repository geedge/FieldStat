
#pragma once
#include "my_ut_hash.h"

#ifdef __cplusplus
extern "C"{
#endif

struct cell_manager;

void cell_manager_free(struct cell_manager *pthis);
struct cell_manager *cell_manager_new(enum sampling_mode sampling_mode, int max_cell_num);
void cell_manager_reset(struct cell_manager *pthis);

int cell_manager_get_cell_id_by_tag(const struct cell_manager *pthis, const struct tag_hash_key *tag);
const struct tag_hash_key *cell_manager_get_tag_by_cell_id(const struct cell_manager *pthis, int cell_id);
int cell_manager_get_count_by_tag(const struct cell_manager *pthis, const struct tag_hash_key *tag);

int cell_manager_add_cell(struct cell_manager *pthis, const struct tag_hash_key *tag);
int cell_manager_add_cell_topk(struct cell_manager *pthis, const struct tag_hash_key *tag, unsigned int count, int *popped_cell_id);

int cell_manager_serialize(const struct cell_manager *pthis, char **blob, size_t *blob_size);
struct cell_manager *cell_manager_deserialize(const char *blob, size_t blob_size);

#ifdef __cplusplus
}
#endif