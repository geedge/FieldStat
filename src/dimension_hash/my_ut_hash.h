
#include "fieldstat.h"

struct my_dimension {
    char *key;
    size_t key_len;
    unsigned char *value;
    size_t value_len;
};

void my_dimension_new(struct fieldstat_tag *dimensions, size_t n_dimension, struct my_dimension **my_dimension_out, size_t *n_my_dimension_out);
void my_dimension_free(struct my_dimension **my_dimension, size_t n_my_dimension);
void my_dimension_parse(const struct my_dimension *my_dimension, size_t n_my_dimension, struct fieldstat_tag **dimensions_out, size_t *n_dimension_out);

unsigned int my_dimension_hash_func(const struct my_dimension *my_dimension, size_t n_my_dimension);
#define HASH_FUNCTION(keyptr, keylen, hashv) {hashv = my_dimension_hash_func((struct my_dimension *)keyptr, keylen)}

#include "uthash.h"


// todo: 但是我还有要用普通哈希的地方，注意一点，因为heavy keeper里的哈希要用这个，不好改了。