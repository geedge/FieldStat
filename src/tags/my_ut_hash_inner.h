
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>

struct my_hash_tag_key;

unsigned int my_tag_hash_func(struct my_hash_tag_key *my_hash_key);
// return 1 if not equal, 0 if equal
int my_hash_tag_cmp(const struct my_hash_tag_key *a, const struct my_hash_tag_key *b);

// cover the original HASH_FUNCTION
#define HASH_FUNCTION(keyptr, keylen, hashv) { \
    hashv = my_tag_hash_func((struct my_hash_tag_key *)keyptr); \
    }

#define HASH_KEYCMP(a, b, n) (my_hash_tag_cmp((const struct my_hash_tag_key *)a, (const struct my_hash_tag_key *)b))


#include "uthash.h"

#define HASH_ADD_TAG(head,fieldname,add)                                \
  HASH_ADD_KEYPTR(hh, head, (add)->fieldname, 1, add)

#define HASH_FIND_TAG(head,keyptr,out)                 \
  HASH_FIND(hh, head, keyptr, 1, out)



// todo: define HASH_KEYCMP

#ifdef __cplusplus
}
#endif