#pragma once
#include <stdio.h>
#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <stdbool.h>
#include "fieldstat.h" // for tags
#include "metric_manifest.h"

struct cube;

struct cube *cube_new(const struct fieldstat_tag *shared_tags, size_t n_tag, enum sampling_mode mode, size_t max_n_cell);
void cube_free(struct cube *cube);
void cube_reset(struct cube *cube);
struct cube *cube_copy(const struct cube *cube);
void cube_merge(struct cube *dest, const struct cube *src);
struct cube *cube_fork(const struct cube *cube); // only copy the cube configurations, leave the cells empty

int cube_histogram_record(struct cube *cube, const struct metric_manifest *manifest, const struct fieldstat_tag *tags, size_t n_tag, long long value);
int cube_hll_add(struct cube *cube, const struct metric_manifest *manifest, const struct fieldstat_tag *tags, size_t n_tag, const char *key, size_t key_len);
int cube_counter_incrby(struct cube *cube, const struct metric_manifest *manifest, const struct fieldstat_tag *tags, size_t n_tag, long long increment);
int cube_counter_set(struct cube *cube, const struct metric_manifest *manifest, const struct fieldstat_tag *tags, size_t n_tag, long long value);

int cube_counter_get(const struct cube *cube, int metric_id, const struct fieldstat_tag_list *tags, long long *value);
int cube_hll_get(const struct cube *cube, int metric_id, const struct fieldstat_tag_list *tags, double *value);
int cube_histogram_value_at_percentile(const struct cube *cube, int metric_id, const struct fieldstat_tag_list *tags, double percentile, long long *value);
int cube_histogram_count_le_value(const struct cube *cube, int metric_id, const struct fieldstat_tag_list *tags, long long value, long long *count);
int cube_get_serialization(const struct cube *cube, int metric_id, const struct fieldstat_tag_list *tags, char **blob, size_t *blob_size);

int cube_get_cell_count(const struct cube *cube);
void cube_get_cells(const struct cube *cube, struct fieldstat_tag_list **tag_list, size_t *n_cell);
void cube_get_cells_used_by_metric(const struct cube *cube, const struct fieldstat_tag_list *tags, int **metric_id_out, size_t *n_metric_out);
void cube_set_primary_metric(struct cube *cube, int metric_id);
struct fieldstat_tag_list *cube_get_identifier(const struct cube *cube);

#ifdef __cplusplus
}
#endif