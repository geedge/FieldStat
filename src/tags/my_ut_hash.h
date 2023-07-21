
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include "fieldstat.h"

struct tag_hash_key;

struct tag_hash_key *tag_hash_key_construct_with_fieldstat_tag(const struct fieldstat_tag *src, size_t n_src);
void tag_hash_key_convert_to_fieldstat_tag(const struct tag_hash_key *tag_key, struct fieldstat_tag **out, size_t *n_out);
struct tag_hash_key *tag_hash_key_copy(const struct tag_hash_key *src);
void tag_hash_key_free(struct tag_hash_key *tag_key);
unsigned tag_hash_key_cal_fnv_hash(const struct tag_hash_key *tag, unsigned seed);
void tag_hash_key_serialize(const struct tag_hash_key *tag, char **blob, size_t *blob_size);
struct tag_hash_key *tag_hash_key_deserialize(const char *blob, size_t blob_size);

void fieldtag_list_free(struct fieldstat_tag *tags, size_t n_tags);
void fieldtag_list_serialize(const struct fieldstat_tag *tag_list, size_t tag_num, char **blob, size_t *blob_size);
void fieldtag_list_deserialize(const char *blob, size_t blob_size, struct fieldstat_tag **tag_list_out, size_t *tag_num_out);


#ifdef __cplusplus
}
#endif