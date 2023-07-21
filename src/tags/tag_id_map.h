
#include "uthash.h"

struct tag_id_map {
    struct tag_hash_key *tag;
    int cell_id;
    UT_hash_handle hh;
};

void my_hash_key_list_add(struct tag_id_map **tbl, struct tag_id_map *node);
struct tag_id_map *my_hash_key_list_find(const struct tag_id_map *tbl, const struct tag_hash_key *my_hash_key);
