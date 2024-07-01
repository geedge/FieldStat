#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C"{
#endif

#include "exdata.h"
#include "my_ut_hash.h"


struct heavy_keeper;

struct heavy_keeper *heavy_keeper_new(int max_query_num);
/**
 * @brief   free a heavy keeper.
 * @param   hk            the pointer to the heavy keeper.
 */
void heavy_keeper_free(struct heavy_keeper *hk);

/*
 * @brief   clear a heavy keeper. It will clear all the data in the heavy keeper. as for every exdata, use the reset function `exdata_reset_cb` to reset it.
    * @param   hk            the pointer to the heavy keeper.
*/
void heavy_keeper_reset(struct heavy_keeper *hk);

// int heavy_keeper_add(struct heavy_keeper *hk, const char *key, size_t key_len, int count, void *arg);

int heavy_keeper_add(struct heavy_keeper *hk, const struct tag_hash_key *tag, int count, void *arg);


int heavy_keeper_set_exdata_schema(struct heavy_keeper *hk, exdata_new_cb new_fn, exdata_free_cb free_fn, exdata_merge_cb merge_fn, exdata_reset_cb reset_fn, exdata_copy_cb copy_fn);

// void *heavy_keeper_get0_exdata(struct heavy_keeper *hk, const char *key, size_t key_len);
void *heavy_keeper_get0_exdata(struct heavy_keeper *hk, const struct tag_hash_key *tag);

// get the number of cells in the heavy keeper
int heavy_keeper_get_count(struct heavy_keeper *hk);

void heavy_keeper_list(const struct heavy_keeper *hk, void ***exdatas, size_t *n_exdatas); //use list: void **exdatas, heavy_keeper_list(&exdatas); void *exdata = exdatas[i]; free(exdatas);

void heavy_keeper_merge(struct heavy_keeper *dest, struct heavy_keeper *src);
/*
    Equal to heavy_keeper_merge, with empty dest, but much faster.
*/
struct heavy_keeper *heavy_keeper_copy(const struct heavy_keeper *src);

// for test
void heavy_keeper_one_point_query(struct heavy_keeper *hk, const struct tag_hash_key *tag, int *count_out, void **exdata_out);


#ifdef __cplusplus
}
#endif