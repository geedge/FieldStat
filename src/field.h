#pragma once


#include "fieldstat.h"
#include "uthash.h"

#ifdef __cplusplus
extern "C" {
#endif


struct fs_field;

enum field_type field_get_type(struct fs_field *field);
int field_get_id(struct fs_field *field);

struct fs_field *field_new_by_deserialize(const char *blob, size_t blob_size);
int field_serialize(struct fs_field *field, char **blob, size_t *blob_size);

struct fs_field *field_new_topk(const char *name, int K);
void field_free(struct fs_field *field);
void field_refresh(struct fs_field *field);
struct fs_field *field_dup(const struct fs_field *src);
int field_merge(struct fs_field *dst, const struct fs_field *src);

int field_topk_add(struct fs_field *field, const char *key, size_t key_len, double score, const struct fieldstat_tag tags[], size_t n_tag);
// TODO: other type


// TODO: 只暴露field   
// TODO: tag 不要unzip了。

/* -------------------------------------------------------------------------- */
/*                                    query                                   */
/* -------------------------------------------------------------------------- */
void field_query_for_topk(const struct fs_field *field, const struct fieldstat_tag *tag_list, size_t tag_num,
    char ***keys_out, long long **values_out, size_t *num_out);

void field_get_tags(const struct fs_field *field, struct fieldstat_tag_list **tag_lists_out, size_t *n_tag_lists_out);

#ifdef __cplusplus
}
#endif