#pragma once


#include "fieldstat.h"
#include "uthash.h"

#ifdef __cplusplus
extern "C" {
#endif

struct fs_field;


enum field_type field_get_type(const struct fs_field *field);
int field_get_id(const struct fs_field *field);
void field_set_id(struct fs_field *field, int id);
const char *field_get_name(const struct fs_field *field); // don't free the return value
void field_get_tags(const struct fs_field *field, struct fieldstat_tag_list **tag_lists_out, size_t *n_tag_lists_out);

struct fs_field *field_deserialize(const char *blob, size_t blob_size);
int field_serialize(const struct fs_field *field, char **blob, size_t *blob_size);
void field_free(struct fs_field *field);
void field_refresh(struct fs_field *field);
void field_reset(struct fs_field *field);
struct fs_field *field_dup(const struct fs_field *src);
int field_merge(struct fs_field *dst, const struct fs_field *src);
struct fs_field *field_new_topk(const char *name, int K, const char **metric_names, size_t num_metric, size_t primary_metric_id);
struct fs_field *field_new_counter(const char *name);
struct fs_field *field_new_hll(const char *name, char precision);
struct fs_field *field_new_histogram(const char *name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures);

struct cell *field_find_cell(const struct fs_field *field, const struct fieldstat_tag tag_list[], size_t tag_num);
struct cell *field_find_or_create_cell(struct fs_field *field, const struct fieldstat_tag tag_list[], size_t tag_num);


#ifdef __cplusplus
}
#endif