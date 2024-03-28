
#pragma once
#include "my_ut_hash.h"

#ifdef __cplusplus
extern "C"{
#endif

struct cell_manager;

void cell_manager_free(struct cell_manager *pthis);
struct cell_manager *cell_manager_new(enum sampling_mode sampling_mode, int max_cell_num);
struct cell_manager *cell_manager_copy(const struct cell_manager *src);
void cell_manager_reset(struct cell_manager *pthis);

const struct tag_hash_key *cell_manager_get_tag_by_cell_id(const struct cell_manager *pthis, int cell_id);
long long cell_manager_get_count_by_tag(const struct cell_manager *pthis, const struct tag_hash_key *tag);
const struct tag_hash_key **cell_manager_dump(const struct cell_manager *pthis, int *array_len);
int cell_manager_find(const struct cell_manager *pthis, const struct tag_hash_key *tag);
int cell_manager_get_cardinality(const struct cell_manager *pthis);

int cell_manager_add_cell(struct cell_manager *pthis, const struct tag_hash_key *tag);
int cell_manager_add_cell_topk(struct cell_manager *pthis, const struct tag_hash_key *tag, unsigned int count, int *popped_cell_id);
int cell_manager_delete_cell(struct cell_manager *pthis, const struct tag_hash_key *tag);

void cell_manager_merge_topk(struct cell_manager *dest, const struct cell_manager *src, 
    int **cell_id_popped, int *n_cell_id_popped, int **cell_id_old, int **cell_id_added, int *n_cell_id_added);

/* ------- a simpler cell_manager, which only support check if tag exist and add. ------- */
struct cube_manager;

struct cube_manager *cube_manager_new();
void cube_manager_free(struct cube_manager *pthis);
void cube_manager_add(struct cube_manager *pthis, const struct tag_hash_key *tag, int id);
int cube_manager_find(const struct cube_manager *pthis, const struct tag_hash_key *tag);
void cube_manager_delete(struct cube_manager *pthis, const struct tag_hash_key *tag);
void cube_manager_calibrate(struct cube_manager *pthis, const struct cube_manager *master);

#ifdef __cplusplus
}
#endif