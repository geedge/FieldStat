#pragma once


#include "fieldstat.h"
#include "metrics.h"
#include "uthash.h"

#ifdef __cplusplus
extern "C" {
#endif

struct fs_cell;


void cell_get_metric_group(const struct fs_cell *cell, struct metrics **metric_group_out, size_t *n_metric_group_out);
int cell_get_dimension_id(const struct fs_cell *cell);
struct fs_cell *cell_deserialize(const char *blob, size_t blob_size);
int cell_serialize(const struct fs_cell *cell, char **blob, size_t *blob_size);
void cell_free(struct fs_cell *cell);
void cell_reset(struct fs_cell *cell);
struct fs_cell *cell_dup(const struct fs_cell *src);
int cell_merge(struct fs_cell *dst, const struct fs_cell *src);
struct metrics *cell_find_or_create_metric(const struct fs_cell *cell, sturct metrics *metric);
struct fs_cell *cell_new(int dimension_id);

#ifdef __cplusplus
}
#endif