#include "hash_table.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "uthash.h"

#include "fieldstat.h"
#include "exdata.h"

struct tag_exdata_item {
    char *key;
    size_t key_len;
    void *exdata;
    bool dying;
    UT_hash_handle hh;
};

struct hash_table {
    struct tag_exdata_item *tag_id_map;
    int current_cell_num;
    int max_cell_num;

    exdata_new_cb new_fn;
    exdata_free_cb free_fn;
    exdata_merge_cb merge_fn;
    exdata_reset_cb reset_fn;
    exdata_copy_cb copy_fn;
};
static void *default_new_fn(void *arg) {
    return NULL;
}

static void default_free_fn(void *exdata) {
    return;
}

static void default_merge_fn(void *dest, void *src) {
    return;
}

static void default_reset_fn(void *exdata) {
    return;
} 

static void *default_copy_fn(void *exdata) {
    return exdata;
}


struct hash_table *hash_table_new(int max_query_num) {
    struct hash_table *pthis = calloc(1, sizeof(struct hash_table));

    pthis->max_cell_num = max_query_num;

    pthis->new_fn = default_new_fn;
    pthis->free_fn = default_free_fn;
    pthis->merge_fn = default_merge_fn;
    pthis->reset_fn = default_reset_fn;
    pthis->copy_fn = default_copy_fn;
    return pthis;
}

void hash_table_free(struct hash_table *pthis) {
    if (pthis == NULL) {
        return;
    }
    struct tag_exdata_item *item, *tmp;
    HASH_ITER(hh, pthis->tag_id_map, item, tmp) {
        HASH_DEL(pthis->tag_id_map, item);
        free(item->key);
        pthis->free_fn(item->exdata);
        free(item);
    }
    free(pthis);
}

void hash_table_reset(struct hash_table *pthis) {
    struct tag_exdata_item *node, *tmp;
    HASH_ITER(hh, pthis->tag_id_map, node, tmp) {
        if (!node->dying) {
            node->dying = true;
            pthis->reset_fn(node->exdata);
            continue;
        }
        HASH_DEL(pthis->tag_id_map, node);
        free(node->key);
        pthis->free_fn(node->exdata);
        free(node);
    }

    pthis->current_cell_num = 0;
}

static char *my_keydup(const char *key, size_t key_len) {
    char *ret = calloc(1, key_len + 1);
    memcpy(ret, key, key_len);
    return ret;
}

int hash_table_add(struct hash_table *pthis, const char *key, size_t key_len, void *arg) {
    struct tag_exdata_item *item;
    HASH_FIND(hh, pthis->tag_id_map, key, key_len, item);
    if (item != NULL && !item->dying) {
        return 1;
    }

    if (pthis->current_cell_num >= pthis->max_cell_num) {
        return 0;
    }

    if (item != NULL) {
        assert(item->dying);
        item->dying = false;
        pthis->current_cell_num++;
        return 1;
    }

    item = calloc(1, sizeof(struct tag_exdata_item));
    item->key = my_keydup(key, key_len);
    item->key_len = key_len;
    item->exdata = pthis->new_fn(arg);
    item->dying = false;
    HASH_ADD_KEYPTR(hh, pthis->tag_id_map, item->key, key_len, item);

    pthis->current_cell_num++;
    return 1;
}

void hash_table_set_exdata_schema(struct hash_table *pthis, exdata_new_cb new_fn, exdata_free_cb free_fn, exdata_merge_cb merge_fn, exdata_reset_cb reset_fn, exdata_copy_cb copy_fn) {
    pthis->new_fn = new_fn;
    pthis->free_fn = free_fn;
    pthis->merge_fn = merge_fn;
    pthis->reset_fn = reset_fn;
    pthis->copy_fn = copy_fn;
}

void *hash_table_get0_exdata(struct hash_table *pthis, const char *key, size_t key_len) {
    struct tag_exdata_item *item;
    HASH_FIND(hh, pthis->tag_id_map, key, key_len, item);
    if (item == NULL || item->dying) {
        return NULL;
    }
    return item->exdata;
}

int hash_table_get_count(const struct hash_table *pthis) {
    return pthis->current_cell_num;
}

size_t hash_table_list(const struct hash_table *pthis, void **exdatas, size_t n_exdatas) {
    size_t actual_len = pthis->current_cell_num;
    if (actual_len == 0) {
        return 0;
    }

    struct tag_exdata_item *item, *tmp;
    size_t i = 0;
    HASH_ITER(hh, pthis->tag_id_map, item, tmp) {
        if (item->dying) {
            continue;
        }
        if (i >= n_exdatas) {
            break;
        }
        exdatas[i] = item->exdata;
        i++;
    }
    return actual_len < n_exdatas ? actual_len : n_exdatas;
}

int hash_table_merge(struct hash_table *dest, struct hash_table *src) {
    struct tag_exdata_item *item_src, *tmp;
    struct tag_exdata_item *item_dst;
    HASH_ITER(hh, src->tag_id_map, item_src, tmp) {
        if (item_src->dying) {
            continue;
        }

        HASH_FIND(hh, dest->tag_id_map, item_src->key, item_src->key_len, item_dst);
        if (item_dst != NULL && !item_dst->dying) {
            dest->merge_fn(item_dst->exdata, item_src->exdata);
            continue;
        }

        if (dest->current_cell_num >= dest->max_cell_num) {
            continue; // cannot add more cells
        }

        if (item_dst == NULL) {
            item_dst = calloc(1, sizeof(struct tag_exdata_item));
            item_dst->key = my_keydup(item_src->key, item_src->key_len);
            item_dst->key_len = item_src->key_len;
            item_dst->dying = false;
            item_dst->exdata = dest->copy_fn(item_src->exdata);
            HASH_ADD_KEYPTR(hh, dest->tag_id_map, item_dst->key, item_dst->key_len, item_dst);
            dest->current_cell_num++;
        } else {
            assert(item_dst->dying);
            item_dst->dying = false;
            dest->merge_fn(item_dst->exdata, item_src->exdata); //item_dst->exdata should be empty, but just merge to it.
            dest->current_cell_num++;
        }
    }
    return 0;
}

struct hash_table *hash_table_copy(const struct hash_table *src) {
    struct hash_table *pthis = calloc(1, sizeof(struct hash_table));
    pthis->max_cell_num = src->max_cell_num;
    pthis->current_cell_num = src->current_cell_num;
    pthis->new_fn = src->new_fn;
    pthis->free_fn = src->free_fn;
    pthis->merge_fn = src->merge_fn;
    pthis->reset_fn = src->reset_fn;
    pthis->copy_fn = src->copy_fn;

    struct tag_exdata_item *item, *tmp;
    HASH_ITER(hh, src->tag_id_map, item, tmp) {
        if (item->dying) {
            continue;
        }
        struct tag_exdata_item *new_item = calloc(1, sizeof(struct tag_exdata_item));
        new_item->key = my_keydup(item->key, item->key_len);
        new_item->key_len = item->key_len;
        new_item->exdata = pthis->copy_fn(item->exdata);
        new_item->dying = false;
        HASH_ADD_KEYPTR(hh, pthis->tag_id_map, new_item->key, new_item->key_len, new_item);
    }
    return pthis;
}
