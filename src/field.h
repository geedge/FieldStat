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

struct fs_field *field_new_by_deserialize(const char *blob, size_t blob_size);
int field_serialize(const struct fs_field *field, char **blob, size_t *blob_size);
void field_free(struct fs_field *field);
void field_refresh(struct fs_field *field);
struct fs_field *field_dup(const struct fs_field *src);
int field_merge(struct fs_field *dst, const struct fs_field *src);

struct fs_field *field_new_topk(const char *name, int K);
struct fs_field *field_new_counter(const char *name);
struct fs_field *field_new_cardinality(const char *name, char precision);
struct fs_field *field_new_histogram(const char *name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures);

int field_topk_add(struct fs_field *field, const char *key, size_t key_len, unsigned int score, const struct fieldstat_tag tags[], size_t n_tag);
int field_counter_incrby(struct fs_field *field, long long score, const struct fieldstat_tag tags[], size_t n_tag);
int field_histogram_record(struct fs_field *field, long long value, const struct fieldstat_tag tags[], size_t n_tag);
int field_cardinality_add(struct fs_field *field, const char *key, size_t key_len, const struct fieldstat_tag tags[], size_t n_tag);

/* -------------------------------------------------------------------------- */
/*                                    query                                   */
/* -------------------------------------------------------------------------- */
void field_query_for_topk(const struct fs_field *field, const struct fieldstat_tag *tag_list, size_t tag_num,
    char ***keys_out, long long **values_out, size_t *num_out);
long long field_query_for_counter(const struct fs_field *field, const struct fieldstat_tag *tag_list, size_t tag_num);
long long field_query_for_histogram(const struct fs_field *field, const struct fieldstat_tag *tag_list, size_t tag_num, double percentile);
double field_query_for_cardinality(const struct fs_field *field, const struct fieldstat_tag *tag_list, size_t tag_num);

void field_get_tags(const struct fs_field *field, struct fieldstat_tag_list **tag_lists_out, size_t *n_tag_lists_out);

#ifdef __cplusplus
}
#endif