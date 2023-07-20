
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
unsigned my_hash_tag_key_fnv_hash(const struct my_hash_tag_key *tag, unsigned seed);
void my_hash_tag_key_serialize(const struct my_hash_tag_key *tag, char **blob, size_t *blob_size);
struct my_hash_tag_key *my_hash_tag_key_deserialize(const char *blob, size_t blob_size);

#ifdef __cplusplus
}
#endif