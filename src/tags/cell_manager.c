#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "fieldstat.h"
#include "cell_manager.h"
#include "my_ut_hash_inner.h"
#include "my_ut_hash.h"

struct tag_id_map {
    struct tag_hash_key *tag;
    int cell_id;
    bool dying;
    UT_hash_handle hh;
};

struct cube_manager {
    struct tag_id_map *head;
};

void tag_id_map_free(struct tag_id_map *head)
{
    struct tag_id_map *node = NULL;
    struct tag_id_map *tmp = NULL;
    HASH_ITER(hh, head, node, tmp) {
        HASH_DEL(head, node);
        tag_hash_key_free(node->tag);
        free(node);
    }
}

void cube_manager_free(struct cube_manager *pthis)
{
    tag_id_map_free(pthis->head);
    free(pthis);
}

struct cube_manager *cube_manager_new()
{
    struct cube_manager *pthis = (struct cube_manager *)malloc(sizeof(struct cube_manager));
    pthis->head = NULL;
    return pthis;
}

void cube_manager_add(struct cube_manager *pthis, const struct tag_hash_key *tag, int id)
{
    struct tag_id_map *node = (struct tag_id_map *)malloc(sizeof(struct tag_id_map));
    node->tag = tag_hash_key_copy(tag);
    node->cell_id = id;
    node->dying = false; // this is not used in cube manager, just give it a value
    HASH_ADD_TAG(pthis->head, tag, node);
}

void cube_manager_delete(struct cube_manager *pthis, const struct tag_hash_key *tag)
{
    struct tag_id_map *node = NULL;
    HASH_FIND_TAG(pthis->head, tag, node);
    if (node != NULL) {
        HASH_DEL(pthis->head, node);
        tag_hash_key_free(node->tag);
        free(node);
    }
}

int cube_manager_find(const struct cube_manager *pthis, const struct tag_hash_key *tag)
{
    struct tag_id_map *node = NULL;
    HASH_FIND_TAG(pthis->head, tag, node);
    if (node == NULL) {
        return -1;
    } else {
        return node->cell_id;
    }
}

void cube_manager_calibrate(struct cube_manager *pthis, const struct cube_manager *master)
{
    struct tag_id_map *node = NULL;
    struct tag_id_map *tmp = NULL;
    HASH_ITER(hh, pthis->head, node, tmp) {
        int cube_id = cube_manager_find(master, node->tag);
        if (cube_id == -1) {
            HASH_DEL(pthis->head, node);
            tag_hash_key_free(node->tag);
            free(node);
        } else {
            node->cell_id = cube_id;
        }
    }

    // exist in master but not in self
    HASH_ITER(hh, master->head, node, tmp) {
        int cube_id = cube_manager_find(pthis, node->tag);
        if (cube_id == -1) {
            cube_manager_add(pthis, node->tag, node->cell_id);
        }
    }
}