
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

// todo: 感觉不需要用这个头文件了
/*
方案1：用这个库，并且改cell_manager 的接口。主要的工作量在sorted set上，需要把char *改成my hash tag. 此外，key 的序列化也要改，本来就是一长串char *，现在要多blob 一次。
优点是在merge的时候，大量倒来倒去的哈希调用，不用一直拼字符串了。
缺点是工作量稍大，容易改出毛病。

方案2：还是拼一下，不过不用fn printf了。
效果不错，而且本身构造hash key的时候，就需要memcpy，所以比起方案1慢的应该只有merge 过程。感觉优化的意义不大，因为merge 对性能的要求不高。
因此选择这个方案
*/

#ifdef __cplusplus
}
#endif