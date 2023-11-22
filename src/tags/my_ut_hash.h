
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include "fieldstat.h"

struct tag_hash_key;

// note that: As to improve performance, the tag_hash_key is a shallow copy of fieldstat_tag, just after been constructed by `tag_hash_key_construct_with_fieldstat_tag`. Use `tag_hash_key_store` to make it a deep copy. 
struct tag_hash_key *tag_hash_key_construct_with_fieldstat_tag(const struct fieldstat_tag *src, size_t n_src);
void tag_hash_key_convert_to_fieldstat_tag(const struct tag_hash_key *tag_key, struct fieldstat_tag **out, size_t *n_out);
struct tag_hash_key *tag_hash_key_copy(const struct tag_hash_key *src); // Deep copy
void tag_hash_key_free(struct tag_hash_key *tag_key);
unsigned tag_hash_key_cal_hash_val(const struct tag_hash_key *tag, unsigned seed);
void tag_hash_key_store(struct tag_hash_key *tag_key); // let the shallow copy of fieldstat_tag in tag_key to be deep copy, if they are shallow copy

void fieldtag_list_free(struct fieldstat_tag *tags, size_t n_tags);
char *tag_hash_key_get_compound_key(const struct tag_hash_key *tag);
int tag_hash_key_cmp(const struct tag_hash_key *a, const struct tag_hash_key *b);

#ifdef __cplusplus
}
#endif