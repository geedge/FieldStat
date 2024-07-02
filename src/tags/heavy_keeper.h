#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C"{
#endif

#include "exdata.h"


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

int heavy_keeper_add(struct heavy_keeper *hk, const char *key, size_t key_len, int count, void *arg);

int heavy_keeper_set_exdata_schema(struct heavy_keeper *hk, exdata_new_cb new_fn, exdata_free_cb free_fn, exdata_merge_cb merge_fn, exdata_reset_cb reset_fn, exdata_copy_cb copy_fn);

void *heavy_keeper_get0_exdata(const struct heavy_keeper *hk, const char *key, size_t key_len);

// get the number of cells in the heavy keeper
int heavy_keeper_get_count(const struct heavy_keeper *hk);

// size_t heavy_keeper_list(const struct heavy_keeper *hk, void **exdatas, size_t n_exdatas); //use list: void **exdatas, heavy_keeper_list(&exdatas); void *exdata = exdatas[i]; // TODO: 用户申请exdata 数组
void heavy_keeper_list(const struct heavy_keeper *hk, void ***exdatas, size_t *n_exdatas); 

void heavy_keeper_merge(struct heavy_keeper *dest, const struct heavy_keeper *src); // TODO: dst 和 dest 统一一下

struct heavy_keeper *heavy_keeper_copy(const struct heavy_keeper *src);

// for test
void heavy_keeper_one_point_query(const struct heavy_keeper *hk, const char *key, size_t key_len, int *count_out, void **exdata_out);


#ifdef __cplusplus
}
#endif