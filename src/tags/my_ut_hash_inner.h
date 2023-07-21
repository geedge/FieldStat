
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>

struct tag_hash_key;

unsigned int tag_hash_key_cal_hash_value(struct tag_hash_key *my_hash_key);
// return 1 if not equal, 0 if equal
int tag_hash_key_cmp(const struct tag_hash_key *a, const struct tag_hash_key *b);

// cover the original HASH_FUNCTION
#define HASH_FUNCTION(keyptr, keylen, hashv) { \
    hashv = tag_hash_key_cal_hash_value((struct tag_hash_key *)keyptr); \
    }

#define HASH_KEYCMP(a, b, n) (tag_hash_key_cmp((const struct tag_hash_key *)a, (const struct tag_hash_key *)b))


#include "uthash.h"

#define HASH_ADD_TAG(head,fieldname,add)                                \
  HASH_ADD_KEYPTR(hh, head, (add)->fieldname, 1, add)

#define HASH_FIND_TAG(head,keyptr,out)                 \
  HASH_FIND(hh, head, keyptr, 1, out)



// todo: define HASH_KEYCMP

#ifdef __cplusplus
}
#endif