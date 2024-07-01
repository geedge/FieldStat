#include "tag_map.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "fieldstat.h"
#include "my_ut_hash_inner.h"
#include "my_ut_hash.h"
#include "exdata.h"

struct tag_exdata_item {
    struct tag_hash_key *tag;
    void *exdata;
    bool dying;
    UT_hash_handle hh;
};

struct tag_map {
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


struct tag_map *tag_map_new(int max_query_num) {
    struct tag_map *pthis = calloc(1, sizeof(struct tag_map));

    pthis->max_cell_num = max_query_num;

    pthis->new_fn = default_new_fn;
    pthis->free_fn = default_free_fn;
    pthis->merge_fn = default_merge_fn;
    pthis->reset_fn = default_reset_fn;
    pthis->copy_fn = default_copy_fn;
    return pthis;
}

void tag_map_free(struct tag_map *pthis) {
    struct tag_exdata_item *item, *tmp;
    HASH_ITER(hh, pthis->tag_id_map, item, tmp) {
        HASH_DEL(pthis->tag_id_map, item);
        tag_hash_key_free(item->tag);
        pthis->free_fn(item->exdata);
        free(item);
    }
    free(pthis);
}

void tag_map_reset(struct tag_map *pthis) {
    struct tag_exdata_item *node, *tmp;
    HASH_ITER(hh, pthis->tag_id_map, node, tmp) {
        if (!node->dying) {
            node->dying = true;
            pthis->reset_fn(node->exdata);
            continue;
        }
        HASH_DEL(pthis->tag_id_map, node);
        tag_hash_key_free(node->tag);
        pthis->free_fn(node->exdata);
        free(node);
    }

    pthis->current_cell_num = 0;
}

int tag_map_add(struct tag_map *pthis, const struct tag_hash_key *tag, void *arg) {
    struct tag_exdata_item *item;
    HASH_FIND_TAG(pthis->tag_id_map, tag, item);
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
    item->tag = tag_hash_key_copy(tag);
    item->exdata = pthis->new_fn(arg);
    item->dying = false;
    HASH_ADD_TAG(pthis->tag_id_map, tag, item);

    pthis->current_cell_num++;
    return 1;
}

void tag_map_set_exdata_schema(struct tag_map *pthis, exdata_new_cb new_fn, exdata_free_cb free_fn, exdata_merge_cb merge_fn, exdata_reset_cb reset_fn, exdata_copy_cb copy_fn) {
    pthis->new_fn = new_fn;
    pthis->free_fn = free_fn;
    pthis->merge_fn = merge_fn;
    pthis->reset_fn = reset_fn;
    pthis->copy_fn = copy_fn;
}

void *tag_map_get0_exdata(struct tag_map *pthis, const struct tag_hash_key *tag) {
    struct tag_exdata_item *item;
    HASH_FIND_TAG(pthis->tag_id_map, tag, item);
    if (item == NULL || item->dying) {
        return NULL;
    }
    return item->exdata;
}

int tag_map_get_count(struct tag_map *pthis) {
    return pthis->current_cell_num;
}

void tag_map_list(const struct tag_map *pthis, void ***exdatas, size_t *n_exdatas) {
    *n_exdatas = pthis->current_cell_num;
    if (*n_exdatas == 0) {
        *exdatas = NULL;
        return;
    }

    void **exdatas_ret = calloc(*n_exdatas, sizeof(void *));
    *exdatas = exdatas_ret;

    struct tag_exdata_item *item, *tmp;
    size_t i = 0;
    HASH_ITER(hh, pthis->tag_id_map, item, tmp) {
        if (item->dying) {
            continue;
        }
        exdatas_ret[i] = item->exdata;
        i++;
    }
}

int tag_map_merge(struct tag_map *dest, struct tag_map *src) {
    struct tag_exdata_item *item_src, *tmp;
    struct tag_exdata_item *item_dst;
    HASH_ITER(hh, src->tag_id_map, item_src, tmp) {
        if (item_src->dying) {
            continue;
        }

        HASH_FIND_TAG(dest->tag_id_map, item_src->tag, item_dst);
        if (item_dst != NULL && !item_dst->dying) {
            dest->merge_fn(item_dst->exdata, item_src->exdata);
            continue;
        }

        if (dest->current_cell_num >= dest->max_cell_num) {
            continue; // cannot add more cells
        }

        if (item_dst == NULL) {
            item_dst = calloc(1, sizeof(struct tag_exdata_item));
            item_dst->tag = tag_hash_key_copy(item_src->tag);
            item_dst->dying = false;
            item_dst->exdata = dest->copy_fn(item_src->exdata);
            HASH_ADD_TAG(dest->tag_id_map, tag, item_dst);
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

struct tag_map *tag_map_copy(const struct tag_map *src) {
    struct tag_map *pthis = calloc(1, sizeof(struct tag_map));
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
        new_item->tag = tag_hash_key_copy(item->tag);
        new_item->exdata = pthis->copy_fn(item->exdata);
        new_item->dying = false;
        HASH_ADD_TAG(pthis->tag_id_map, tag, new_item);
    }
    return pthis;
}
