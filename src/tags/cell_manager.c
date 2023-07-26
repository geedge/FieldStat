#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "mpack/mpack.h"
#include "fieldstat.h"
#include "tags/cell_manager.h"
#include "tags/heavy_keeper.h"
// #include "tags/my_ut_hash_inner.h"
#include "tags/my_ut_hash.h"
#include "tags/tag_id_map.h"

struct cell_manager {
    union {
        struct heavy_keeper *topk_tag_id_map;
        struct tag_id_map *comprehensive_tag_id_map;
    };
    const struct tag_hash_key **id_tag_array;
    int id_tag_array_len;
    int max_cell_id;

    enum sampling_mode sampling_mode;
    int max_cell_num;
    int next_cell_id;
};

struct cell_manager *cell_manager_new_without_map(enum sampling_mode sampling_mode, int max_cell_num)
{
    struct cell_manager *cell_manager = (struct cell_manager *)malloc(sizeof(struct cell_manager));
    memset(cell_manager, 0, sizeof(struct cell_manager));

    cell_manager->sampling_mode = sampling_mode;
    cell_manager->max_cell_num = max_cell_num;
    cell_manager->next_cell_id = 0;

    cell_manager->id_tag_array_len = max_cell_num;
    cell_manager->id_tag_array = (const struct tag_hash_key **)malloc(sizeof(struct tag_hash_key *) * cell_manager->id_tag_array_len);
    memset(cell_manager->id_tag_array, 0, sizeof(struct tag_hash_key *) * cell_manager->id_tag_array_len);
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

void cell_manager_comprehensive_tag_id_map_free(struct cell_manager *pthis)
{
    struct tag_id_map *node = NULL;
    struct tag_id_map *tmp = NULL;
    HASH_ITER(hh, pthis->comprehensive_tag_id_map, node, tmp) {
        HASH_DEL(pthis->comprehensive_tag_id_map, node);
        tag_hash_key_free(node->tag);
        free(node);
    }
}

void cell_manager_reset(struct cell_manager *pthis)
{
    if (pthis->sampling_mode == SAMPLING_MODE_TOPK) {
        heavy_keeper_clear(pthis->topk_tag_id_map);
    } else {
        cell_manager_comprehensive_tag_id_map_free(pthis);
    }

    pthis->next_cell_id = 0;

    free(pthis->id_tag_array);
    pthis->max_cell_id = -1;
    pthis->id_tag_array = (const struct tag_hash_key **)malloc(sizeof(struct tag_hash_key *) * pthis->id_tag_array_len);
    memset(pthis->id_tag_array, 0, sizeof(struct tag_hash_key *) * pthis->id_tag_array_len);
}

void cell_manager_free(struct cell_manager *pthis)
{
    free(pthis->id_tag_array);

    if (pthis->sampling_mode == SAMPLING_MODE_TOPK) {
        heavy_keeper_free(pthis->topk_tag_id_map);
    } else {
        cell_manager_comprehensive_tag_id_map_free(pthis);
    }

    free(pthis);
}

const struct tag_hash_key *cell_manager_get_tag_by_cell_id(const struct cell_manager *pthis, int cell_id)
{
    const struct tag_hash_key *tag_key = pthis->id_tag_array[cell_id];
    if (tag_key == NULL) {
        return NULL;
    }

    return tag_key;
}

void cell_manager_id_tag_array_add(struct cell_manager *pthis, int cell_id, const struct tag_hash_key *tag)
{
    if (cell_id >= pthis->id_tag_array_len) {
        int new_len = pthis->id_tag_array_len;
        while (cell_id >= new_len) {
            new_len *= 2;
        }
        struct tag_hash_key **new_array = (struct tag_hash_key **)malloc(sizeof(struct tag_hash_key *) * new_len);
        memset(new_array, 0, sizeof(struct tag_hash_key *) * new_len);
        memcpy(new_array, pthis->id_tag_array, sizeof(struct tag_hash_key *) * pthis->id_tag_array_len);
        free(pthis->id_tag_array);
        pthis->id_tag_array = (const struct tag_hash_key **)new_array;
        pthis->id_tag_array_len = new_len;
    }

    pthis->id_tag_array[cell_id] = tag;

    if (cell_id > pthis->max_cell_id) {
        pthis->max_cell_id = cell_id;
    }
}

int cell_manager_get_cell_id_by_tag(const struct cell_manager *pthis, const struct tag_hash_key *tag)
{
    int cell_id = -1;

    if (pthis->sampling_mode == SAMPLING_MODE_TOPK) {
        heavy_keeper_query_one(pthis->topk_tag_id_map, tag, NULL, &cell_id);
    } else {
        struct tag_id_map *node = my_hash_key_list_find(pthis->comprehensive_tag_id_map, tag);
        if (node == NULL) {
            cell_id = -1;
        } else {
            cell_id = node->cell_id;
        }
    }

    return cell_id;
}

int cell_manager_get_count_by_tag(const struct cell_manager *pthis, const struct tag_hash_key *tag)
{
    if (pthis->sampling_mode == SAMPLING_MODE_COMPREHENSIVE) {
        return -1;
    }

    unsigned int count = 0;
    heavy_keeper_query_one(pthis->topk_tag_id_map, tag, &count, NULL);

    return count;
}

int cell_manager_add_cell(struct cell_manager *pthis, const struct tag_hash_key *tag)
{
    if (HASH_COUNT(pthis->comprehensive_tag_id_map) >= pthis->max_cell_num) {
        return -1;
    }
    struct tag_id_map *node = NULL;
    if (HASH_COUNT(pthis->comprehensive_tag_id_map) != 0) {
        node = my_hash_key_list_find(pthis->comprehensive_tag_id_map, tag);
    }
    if (node != NULL) {
        return node->cell_id;
    }
    struct tag_hash_key *tag_copy = tag_hash_key_copy(tag);
    node = (struct tag_id_map *)malloc(sizeof(struct tag_id_map));
    node->tag = tag_copy;
    node->cell_id = pthis->next_cell_id;
    my_hash_key_list_add(&pthis->comprehensive_tag_id_map, node);

    cell_manager_id_tag_array_add(pthis, pthis->next_cell_id, tag_copy);
    return pthis->next_cell_id++;
}

int cell_manager_add_cell_topk(struct cell_manager *pthis, const struct tag_hash_key *tag, unsigned int count, int *popped_cell_id, int *existing_cell_id)
{
    struct tag_hash_key *tag_copy = tag_hash_key_copy(tag);
    int ret = heavy_keeper_add(pthis->topk_tag_id_map, tag_copy, count, pthis->next_cell_id, popped_cell_id);
    if (ret == -1) { // didn't add
        *existing_cell_id = -1;
        *popped_cell_id = -1;
        tag_hash_key_free(tag_copy);
        return -1;
    }

    if (ret == 1) { // use the original id
        heavy_keeper_query_one(pthis->topk_tag_id_map, tag, NULL, existing_cell_id);

        *popped_cell_id = -1;
        tag_hash_key_free(tag_copy);
        return *existing_cell_id;
    }
    /* -------------------------------- new cell added-------------------------------- */
/*
To mitigate the risk of unbounded array size due to potentially infinite growth of cell IDs, a maximum limit is set for cell IDs in the metric.
This limit is configured as 8 times the value of K. Even in the worst-case scenario of malicious attacks, this prevents excessive memory usage.
Even though there is a small possibility of not counting certain flows due to the limit, it is expected that the unallocated IDs would correspond to mouse flows, 
    which would have minimal impact on accuracy.
The number 8 was determined through experiment, where multiple tests were conducted, randomly generating flows by which cells are determined.
    The observed stable cell ID count did not exceed 3 times the value of K. Therefore, a conservative value of 8 was chosen.*/
    if (pthis->next_cell_id >= pthis->max_cell_num * 8) { // max int is much larger, no risk of overflow
        printf("ERR: the number of cells exceeds the limit when topk get bad flows\n");
        return -1;
    }

    if (*popped_cell_id != -1) { // popped, no need to free the tag
        pthis->id_tag_array[*popped_cell_id] = NULL;
    }
    cell_manager_id_tag_array_add(pthis, pthis->next_cell_id, tag_copy);
    return pthis->next_cell_id++;
}


/*
{
    "mode":<sampling mode int8>
    "max_n_cell":<max cell number int32>

    "id_tag_array": [{
        "id": <id in cell manager>,
        "tags": [<my_tag_key serialized bin blob,...]
    "topk_map": <topk_serializing_blob>
}
*/
int cell_manager_serialize(const struct cell_manager *pthis, char **blob, size_t *blob_size)
{
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_size);
    mpack_build_map(&writer);
    mpack_write_cstr(&writer, "mode");
    mpack_write_i8(&writer, pthis->sampling_mode);
    mpack_write_cstr(&writer, "max_n_cell");
    mpack_write_i32(&writer, pthis->max_cell_num);

    if (pthis->sampling_mode == SAMPLING_MODE_COMPREHENSIVE) {
        mpack_write_cstr(&writer, "id_tag_array");
        mpack_build_array(&writer);
        
        for (int i = 0; i <= pthis->max_cell_id; i++) {
            if (pthis->id_tag_array[i] == NULL) {
                continue;
            }
            mpack_build_map(&writer);
            mpack_write_cstr(&writer, "id");
            mpack_write_int(&writer, i);
            mpack_write_cstr(&writer, "tags");
            char *tag_blob = NULL;
            size_t tag_blob_size = 0;
            tag_hash_key_serialize(pthis->id_tag_array[i], &tag_blob, &tag_blob_size);
            mpack_write_bin(&writer, tag_blob, tag_blob_size);
            free(tag_blob);
            mpack_complete_map(&writer);
        }
        mpack_complete_array(&writer); // id_tag_array
    } else {
        mpack_write_cstr(&writer, "topk_map");
        char *topk_blob = NULL;
        size_t topk_blob_size = 0;
        heavy_keeper_serialization(pthis->topk_tag_id_map, &topk_blob, &topk_blob_size);
        mpack_write_bin(&writer, topk_blob, topk_blob_size);
        free(topk_blob);
    }

    mpack_complete_map(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        printf("ERR: An error occurred in cell_manager_serialize\n");
    }

    return 0;
}

struct cell_manager *cell_manager_deserialize(const char *blob, size_t blob_size)
{
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, blob_size);
    mpack_tree_parse(&tree);
    mpack_node_t root = mpack_tree_root(&tree);

    enum sampling_mode mode = (enum sampling_mode) mpack_node_i8(mpack_node_map_cstr(root, "mode"));
    int max_n_cell = mpack_node_i32(mpack_node_map_cstr(root, "max_n_cell"));
    struct cell_manager *pthis = cell_manager_new_without_map(mode, max_n_cell);
    int cell_id_max = -1;

    if (mode == SAMPLING_MODE_TOPK) {
        mpack_node_t topk_map = mpack_node_map_cstr(root, "topk_map");
        const char *topk_blob = mpack_node_bin_data(topk_map);
        size_t topk_blob_size = mpack_node_bin_size(topk_map);
        pthis->topk_tag_id_map = heavy_keeper_deserialization(topk_blob, topk_blob_size);

        struct heavy_keeper_result *result = heavy_keeper_query(pthis->topk_tag_id_map);
        for (size_t i = 0; i < result->n_key; i++) {
            cell_manager_id_tag_array_add(pthis, result->cell_id[i], result->tags[i]);

            if (result->cell_id[i] > cell_id_max) {
                cell_id_max = result->cell_id[i];
            }
        }
        heavy_keeper_result_free(result);
    } else {
        mpack_node_t id_tag_array = mpack_node_map_cstr(root, "id_tag_array");
        int n_cell = mpack_node_array_length(id_tag_array);
        for (int i = 0; i < n_cell; i++) {
            mpack_node_t cell = mpack_node_array_at(id_tag_array, i);
            int cell_id = mpack_node_i32(mpack_node_map_cstr(cell, "id"));
            mpack_node_t tags_node = mpack_node_map_cstr(cell, "tags");
            const char *tag_blob = mpack_node_bin_data(tags_node);
            size_t tag_blob_size = mpack_node_bin_size(tags_node);
            struct tag_hash_key *tag = tag_hash_key_deserialize(tag_blob, tag_blob_size);
            struct tag_id_map *node = (struct tag_id_map *)malloc(sizeof(struct tag_id_map));
            node->tag = tag;
            node->cell_id = cell_id;
            my_hash_key_list_add(&pthis->comprehensive_tag_id_map, node);

            cell_manager_id_tag_array_add(pthis, cell_id, tag);

            if (cell_id > cell_id_max) {
                cell_id_max = cell_id;
            }
        }
    }
    pthis->next_cell_id = cell_id_max + 1;

    mpack_tree_destroy(&tree);
    return pthis;
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
            my_hash_key_list_add(&pthis->comprehensive_tag_id_map, tmp_node);

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