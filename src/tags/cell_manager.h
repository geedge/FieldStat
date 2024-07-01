
#pragma once
#include "my_ut_hash.h"

#ifdef __cplusplus
extern "C"{
#endif


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