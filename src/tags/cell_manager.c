#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "fieldstat.h"
#include "tags/cell_manager.h"
#include "tags/heavy_keeper.h"
#include "tags/my_ut_hash_inner.h"
#include "tags/my_ut_hash.h"
#include "serializer.h"


struct tag_id_map {
    struct tag_hash_key *tag;
    int cell_id;
    bool dying;
    UT_hash_handle hh;
};

struct cell_manager {
    union {
        struct heavy_keeper *topk_tag_id_map;
        struct tag_id_map *comprehensive_tag_id_map;
    };
    struct tag_hash_key **id_tag_array;
    int id_tag_array_len;
    int max_cell_id;

    enum sampling_mode sampling_mode;
    int max_cell_num;
    int current_cell_num;
    int next_cell_id;
};

struct cube_manager {
    struct tag_id_map *head;
};

struct cell_manager *cell_manager_new_without_map(enum sampling_mode sampling_mode, int max_cell_num)
{
    struct cell_manager *cell_manager = (struct cell_manager *)calloc(1, sizeof(struct cell_manager));

    cell_manager->sampling_mode = sampling_mode;
    cell_manager->max_cell_num = max_cell_num;
    cell_manager->next_cell_id = 0;

    cell_manager->id_tag_array_len = max_cell_num;
    if (cell_manager->id_tag_array_len > 4096) { // support dynamic size cube (especially when max_max_num is unlimited)
        cell_manager->id_tag_array_len = 4096;
    }
    cell_manager->id_tag_array = calloc(cell_manager->id_tag_array_len, sizeof(struct tag_hash_key *));
    cell_manager->max_cell_id = -1;

    return cell_manager;
}

struct cell_manager *cell_manager_new(enum sampling_mode sampling_mode, int max_cell_num)
{
    struct cell_manager *cell_manager = cell_manager_new_without_map(sampling_mode, max_cell_num);

    if (sampling_mode == SAMPLING_MODE_TOPK) {
        cell_manager->topk_tag_id_map = heavy_keeper_with_data_new(max_cell_num);
    } else {
        cell_manager->comprehensive_tag_id_map = NULL;
    }

    return cell_manager;
}

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

void cell_manager_reset(struct cell_manager *pthis)
{
    pthis->next_cell_id = 0;
    pthis->max_cell_id = -1;
    pthis->current_cell_num = 0;
    memset(pthis->id_tag_array, 0, sizeof(struct tag_hash_key *) * pthis->id_tag_array_len);

    if (pthis->sampling_mode == SAMPLING_MODE_TOPK) {
        heavy_keeper_reset(pthis->topk_tag_id_map);
        return;
    }

    struct tag_id_map *node = NULL;
    struct tag_id_map *tmp = NULL;
    HASH_ITER(hh, pthis->comprehensive_tag_id_map, node, tmp) {
        if (!node->dying) {
            node->dying = true;
            continue;
        }
        HASH_DEL(pthis->comprehensive_tag_id_map, node);
        tag_hash_key_free(node->tag);
        free(node);
    }
}

void cell_manager_free(struct cell_manager *pthis)
{
    free(pthis->id_tag_array);

    if (pthis->sampling_mode == SAMPLING_MODE_TOPK) {
        heavy_keeper_free(pthis->topk_tag_id_map);
    } else {
        tag_id_map_free(pthis->comprehensive_tag_id_map);
    }

    free(pthis);
}

const struct tag_hash_key *cell_manager_get_tag_by_cell_id(const struct cell_manager *pthis, int cell_id)
{
    return pthis->id_tag_array[cell_id];
}

void cell_manager_id_tag_array_add(struct cell_manager *pthis, int cell_id, struct tag_hash_key *tag)
{
    if (cell_id >= pthis->id_tag_array_len) {
        int new_len = pthis->id_tag_array_len;
        while (cell_id >= new_len) {
            new_len *= 2;
        }

        struct tag_hash_key **new_array = (struct tag_hash_key **)calloc(new_len, sizeof(struct tag_hash_key *));
        assert(new_array != NULL);
        memcpy(new_array, pthis->id_tag_array, sizeof(struct tag_hash_key *) * pthis->id_tag_array_len);
        free(pthis->id_tag_array);
        pthis->id_tag_array = new_array;
        pthis->id_tag_array_len = new_len;
    }

    pthis->id_tag_array[cell_id] = tag;

    if (cell_id > pthis->max_cell_id) {
        pthis->max_cell_id = cell_id;
    }
    pthis->current_cell_num++;
}

long long cell_manager_get_count_by_tag(const struct cell_manager *pthis, const struct tag_hash_key *tag)
{
    if (pthis->sampling_mode == SAMPLING_MODE_COMPREHENSIVE) {
        return -1;
    }

    unsigned long long int count = 0;
    int ret = heavy_keeper_query_one(pthis->topk_tag_id_map, tag, &count, NULL);
    if (ret == -1) {
        return -1;
    }

    return count;
}

int cell_manager_add_cell(struct cell_manager *pthis, const struct tag_hash_key *tag)
{
    struct tag_id_map *node = NULL;
    HASH_FIND_TAG(pthis->comprehensive_tag_id_map, tag, node);
    if (node != NULL && !node->dying) {
        return node->cell_id;
    }

    if (pthis->current_cell_num >= pthis->max_cell_num) {
        return -1;
    }

    if (node != NULL) {
        node->dying = false;
        node->cell_id = pthis->next_cell_id;
        cell_manager_id_tag_array_add(pthis, pthis->next_cell_id, node->tag);
        return pthis->next_cell_id++;
    }

    struct tag_hash_key *tag_copy = tag_hash_key_copy(tag);
    node = (struct tag_id_map *)malloc(sizeof(struct tag_id_map));
    node->tag = tag_copy;
    node->cell_id = pthis->next_cell_id;
    node->dying = false;
    HASH_ADD_TAG(pthis->comprehensive_tag_id_map, tag, node);

    cell_manager_id_tag_array_add(pthis, pthis->next_cell_id, tag_copy);
    return pthis->next_cell_id++;
}

int cell_manager_add_cell_topk(struct cell_manager *pthis, const struct tag_hash_key *tag, unsigned long long int count, int *popped_cell_id)
{
    int ret = heavy_keeper_add(pthis->topk_tag_id_map, tag, count, pthis->next_cell_id, popped_cell_id);
    if (ret == -1) { // didn't add
        *popped_cell_id = -1;
        return -1;
    }

    if (ret == 1) { // use the original id
        int existing_cell_id = -1;
        heavy_keeper_query_one(pthis->topk_tag_id_map, tag, NULL, &existing_cell_id);

        *popped_cell_id = -1;
        return existing_cell_id;
    }
    
    struct tag_hash_key *tag_ret = heavy_keeper_get_true_pst_tag(pthis->topk_tag_id_map, tag);

    /* -------------------------------- new cell added-------------------------------- */
/*
To mitigate the risk of unbounded array size due to potentially infinite growth of cell IDs, a maximum limit is set for cell IDs in the metric.
This limit is configured as 8 times the value of K. Even in the worst-case scenario of malicious attacks, this prevents excessive memory usage.
Even though there is a small possibility of not counting certain flows due to the limit, it is expected that the unallocated IDs would correspond to mouse flows, 
    which would have minimal impact on accuracy.
The number 8 was determined through experiment, where multiple tests were conducted, randomly generating flows by which cells are determined.
    The observed stable cell ID count did not exceed 3 times the value of K. Therefore, a conservative value of 8 was chosen.*/
    if (pthis->next_cell_id >= pthis->max_cell_num * 8) { // max int is much larger, no risk of overflow
        return -1;
    }

    if (*popped_cell_id != -1) { // popped, no need to free the tag
        pthis->id_tag_array[*popped_cell_id] = NULL;
    }
    cell_manager_id_tag_array_add(pthis, pthis->next_cell_id, tag_ret);
    return pthis->next_cell_id++;
}

int cell_manager_delete_cell(struct cell_manager *pthis, const struct tag_hash_key *tag)
{
    if (pthis->sampling_mode == SAMPLING_MODE_TOPK) {
        return -1;
    }

    struct tag_id_map *node = NULL;
    HASH_FIND_TAG(pthis->comprehensive_tag_id_map, tag, node);
    if (node == NULL) {
        return -1;
    }

    int cell_id = node->cell_id;

    HASH_DEL(pthis->comprehensive_tag_id_map, node);
    tag_hash_key_free(node->tag);
    free(node);
    pthis->id_tag_array[cell_id] = NULL;

    return cell_id;
}

struct cell_manager *cell_manager_copy(const struct cell_manager *src)
{
    struct cell_manager *pthis = cell_manager_new_without_map(src->sampling_mode, src->max_cell_num);
    pthis->next_cell_id = src->next_cell_id;

    if (src->sampling_mode == SAMPLING_MODE_TOPK) {
        pthis->topk_tag_id_map = heavy_keeper_copy(src->topk_tag_id_map);

        struct heavy_keeper_result *result = heavy_keeper_query(pthis->topk_tag_id_map);
        for (size_t i = 0; i < result->n_key; i++) {
            cell_manager_id_tag_array_add(pthis, result->cell_id[i], result->tags[i]);
        }
        heavy_keeper_result_free(result);
    } else {
        for (int i = 0; i <= src->max_cell_id; i++) {
            const struct tag_hash_key *tag = src->id_tag_array[i];
            if (tag == NULL) {
                continue;
            }
            struct tag_hash_key *tag_copy = tag_hash_key_copy(tag);
            struct tag_id_map *tmp_node = (struct tag_id_map *)malloc(sizeof(struct tag_id_map));
            tmp_node->tag = tag_copy;
            tmp_node->cell_id = i;
            tmp_node->dying = false;
            HASH_ADD_TAG(pthis->comprehensive_tag_id_map, tag, tmp_node);

            cell_manager_id_tag_array_add(pthis, i, tag_copy);
        }
    }

    return pthis;
}

const struct tag_hash_key **cell_manager_dump(const struct cell_manager *pthis, int *array_len)
{
    *array_len = pthis->max_cell_id + 1;

    return (const struct tag_hash_key **)pthis->id_tag_array;
}

int cell_manager_find(const struct cell_manager *pthis, const struct tag_hash_key *tag)
{
    if (pthis->sampling_mode == SAMPLING_MODE_COMPREHENSIVE) {
        struct tag_id_map *node = NULL;
        HASH_FIND_TAG(pthis->comprehensive_tag_id_map, tag, node);
        if (node == NULL) {
            return -1;
        } else {
            return node->dying ? -1 : node->cell_id;
        }
    } else {
        int cell_id = -1;
        heavy_keeper_query_one(pthis->topk_tag_id_map, tag, NULL, &cell_id);
        return cell_id;
    }
}

void cell_manager_merge_topk(struct cell_manager *dest, const struct cell_manager *src, 
    int **cell_id_popped, int *n_cell_id_popped, int **cell_id_old, int **cell_id_added, int *n_cell_id_added)
{
    heavy_keeper_merge_recording_id_details(dest->topk_tag_id_map, src->topk_tag_id_map, 
        cell_id_popped, n_cell_id_popped, cell_id_old, cell_id_added, n_cell_id_added);

    memset(dest->id_tag_array, 0, sizeof(struct tag_hash_key *) * dest->id_tag_array_len);
    struct heavy_keeper_result *result = heavy_keeper_query(dest->topk_tag_id_map);
    for (size_t i = 0; i < result->n_key; i++) {
        cell_manager_id_tag_array_add(dest, result->cell_id[i], result->tags[i]);
    }
    heavy_keeper_result_free(result);
}

int cell_manager_get_cardinality(const struct cell_manager *pthis)
{
    if (pthis->sampling_mode == SAMPLING_MODE_COMPREHENSIVE) {
        return pthis->current_cell_num;
    } else {
        return heavy_keeper_get_cardinality(pthis->topk_tag_id_map);
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