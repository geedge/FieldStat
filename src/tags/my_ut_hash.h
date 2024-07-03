
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include "fieldstat.h"
#include "xxhash/xxhash.h"

struct tag_hash_key {
    struct fieldstat_tag *tags; // not used for hash, just to recover the original tags.
    size_t n_my_tag;
    unsigned int hashv;
    XXH128_hash_t hashv128;
    bool is_deep_copy;
};

void tag_hash_key_init_with_fieldstat_tag(struct tag_hash_key *tag_key, const struct fieldstat_tag *src, size_t n_src, bool deep_copy);
void tag_hash_key_convert_to_fieldstat_tag(const struct tag_hash_key *tag_key, struct fieldstat_tag **out, size_t *n_out);
struct tag_hash_key *tag_hash_key_copy(const struct tag_hash_key *src); // Always deep copy
void tag_hash_key_free(struct tag_hash_key *tag_key);
unsigned tag_hash_key_cal_hash_val(const struct tag_hash_key *tag, unsigned seed);

void fieldtag_list_free(struct fieldstat_tag *tags, size_t n_tags);
char *tag_hash_key_get_compound_key(const struct tag_hash_key *tag);
int tag_hash_key_cmp(const struct tag_hash_key *a, const struct tag_hash_key *b);

void build_dynamic_cell_key(const struct fieldstat_tag tags[], size_t n_tags, char **out_key, size_t *out_key_size);
void tag_hash_key_print(const struct tag_hash_key *tag);

#ifdef __cplusplus
}
#endif