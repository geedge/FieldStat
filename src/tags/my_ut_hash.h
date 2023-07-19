
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include "fieldstat.h"

struct my_hash_tag_key;

struct my_hash_tag_key *my_hash_key_convert_from_fieldstat_tag(const struct fieldstat_tag *src, size_t n_src);
struct my_hash_tag_key *my_hash_tag_key_copy(const struct my_hash_tag_key *src);
void my_hash_tag_key_free(struct my_hash_tag_key *my_hash_key);


#ifdef __cplusplus
}
#endif