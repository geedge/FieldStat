#pragma once
#include <stdio.h>
#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <stdbool.h>
#include "fieldstat.h" // for dimensions
#include "metric_manifest.h"

struct cube;
struct cube_manager;

struct cube *cube_new(const struct field *dimensions, size_t n_dimensions, enum sampling_mode mode, size_t max_n_cell);
void cube_free(struct cube *cube);
void cube_reset(struct cube *cube);
struct cube *cube_copy(const struct cube *cube);
void cube_merge(struct cube *dest, const struct cube *src);
struct cube *cube_fork(const struct cube *cube); // only copy the cube configurations, leave the cells empty

int cube_histogram_record(struct cube *cube, const struct metric_manifest *manifest, const struct field *dimensions, size_t n_dimensions, long long value);
int cube_hll_add(struct cube *cube, const struct metric_manifest *manifest, const struct field *dimensions, size_t n_dimensions, const char *key, size_t key_len);
int cube_hll_add_field(struct cube *cube, const struct metric_manifest *manifest, const struct field *dimensions, size_t n_dimensions, const struct field *tags_key, size_t n_tag_key);
int cube_counter_incrby(struct cube *cube, const struct metric_manifest *manifest, const struct field *dimensions, size_t n_dimensions, long long increment);
int cube_counter_set(struct cube *cube, const struct metric_manifest *manifest, const struct field *dimensions, size_t n_dimensions, long long value);

int cube_counter_get(const struct cube *cube, int metric_id, const struct field_list *dimensions, long long *value);
int cube_hll_get(const struct cube *cube, int metric_id, const struct field_list *dimensions, double *value);
int cube_histogram_value_at_percentile(const struct cube *cube, int metric_id, const struct field_list *dimensions, double percentile, long long *value);
int cube_histogram_count_le_value(const struct cube *cube, int metric_id, const struct field_list *dimensions, long long value, long long *count);
int cube_get_serialization(const struct cube *cube, int metric_id, const struct field_list *dimensions, char **blob, size_t *blob_size);

int cube_get_cell_count(const struct cube *cube);
enum sampling_mode cube_get_sampling_mode(const struct cube *cube);
void cube_get_cells(const struct cube *cube, struct field_list **tag_list, size_t *n_cell);
void cube_get_metrics_in_cell(const struct cube *cube, const struct field_list *dimensions, int **metric_id_out, size_t *n_metric_out);
void cube_set_primary_metric(struct cube *cube, int metric_id);
struct field_list *cube_get_identifier(const struct cube *cube);

struct cube *cube_manager_get_cube_by_id(const struct cube_manager *manager, int cube_id);

// the cube will be taken over by the manager, user do not free it.
int cube_manager_add(struct cube_manager *pthis, struct cube *cube);
void cube_manager_delete(struct cube_manager *pthis, struct cube *cube); // the cube will be freed by the manager
int cube_manager_find(const struct cube_manager *pthis, const struct field *cube_dimensions, size_t n_dimensions);
struct cube_manager *cube_manager_new();
void cube_manager_free(struct cube_manager *pthis);
void cube_manager_merge(struct cube_manager *dest, const struct cube_manager *src);

void cube_manager_reset(struct cube_manager *pthis);
void cube_manager_calibrate(struct cube_manager *pthis, const struct cube_manager *master);
struct cube_manager *cube_manager_fork(const struct cube_manager *src);

void cube_manager_list(const struct cube_manager *pthis, int **cube_ids, int *n_cube);

#ifdef __cplusplus
}
#endif