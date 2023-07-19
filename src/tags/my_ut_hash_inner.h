
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

struct my_hash_tag {
    char *key;
    size_t key_len;
    unsigned char *value;
    size_t value_len;
};

struct my_hash_tag_key {
    struct my_hash_tag *my_tag;
    size_t n_my_tag;
    unsigned int hashv;
};

unsigned int my_tag_hash_func(struct my_hash_tag_key *my_hash_key);

// cover the original HASH_FUNCTION
#define HASH_FUNCTION(keyptr, keylen, hashv) { \
    hashv = my_tag_hash_func((struct my_hash_tag_key *)keyptr); \
    }


#include "uthash.h"

#define HASH_ADD_TAG(head,fieldname,add)                                \
  HASH_ADD_KEYPTR(hh, head, (add)->fieldname, 1, add)

#define HASH_FIND_TAG(head,keyptr,out)                 \
  HASH_FIND(hh, head, keyptr, 1, out)



#ifdef __cplusplus
}
#endif