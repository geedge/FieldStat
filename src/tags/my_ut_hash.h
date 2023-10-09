
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

void fieldtag_list_free(struct fieldstat_tag *tags, size_t n_tags);
const char *tag_hash_key_get_compound_key(const struct tag_hash_key *tag);
int tag_hash_key_cmp(const struct tag_hash_key *a, const struct tag_hash_key *b);

#ifdef __cplusplus
}
#endif