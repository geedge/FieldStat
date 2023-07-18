
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include "fieldstat.h"

struct my_hash_tag {
    char *key;
    size_t key_len;
    unsigned char *value;
    size_t value_len;
};

unsigned int my_tag_hash_func(struct my_hash_tag *my_tag, size_t n_my_tag);
struct my_hash_tag *convert_fieldstat_tag_arr_to_my_hash_tag(const struct fieldstat_tag *src, size_t n_src);

#define HASH_FUNCTION(keyptr, keylen, hashv) { \
    hashv = my_tag_hash_func((struct my_hash_tag *)keyptr, keylen); \
    }

#define HASH_ADD_TAG(hh,head,fieldname,keylen_in,add)                                \
  HASH_ADD_KEYPTR(hh, head, (add)->fieldname, keylen_in, add)


#include "uthash.h"


#ifdef __cplusplus
}
#endif