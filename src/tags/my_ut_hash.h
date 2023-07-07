
#include "fieldstat.h"

struct my_tag {
    char *key;
    size_t key_len;
    unsigned char *value;
    size_t value_len;
};

unsigned int my_tag_hash_func(const struct my_tag *my_tag, size_t n_my_tag);
#define HASH_FUNCTION(keyptr, keylen, hashv) {hashv = my_tag_hash_func((struct my_tag *)keyptr, keylen)}

#include "uthash.h"


// todo: 但是我还有要用普通哈希的地方，注意一点，因为heavy keeper里的哈希要用这个，不好改了。