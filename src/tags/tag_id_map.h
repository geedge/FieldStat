// todo: 这部分应该放在inner里，然后把inner里的哈希完全放在.c

#include "uthash.h"

struct tag_id_map {
    struct my_hash_tag_key *tag;
    int cell_id;
    UT_hash_handle hh;
};

void my_hash_key_list_add(struct tag_id_map **tbl, struct tag_id_map *node);
struct tag_id_map *my_hash_key_list_find(const struct tag_id_map *tbl, const struct my_hash_tag_key *my_hash_key);