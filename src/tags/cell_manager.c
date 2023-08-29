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
    if (pthis->sampling_mode == SAMPLING_MODE_TOPK) {
        heavy_keeper_clear(pthis->topk_tag_id_map);
    } else {
        tag_id_map_free(pthis->comprehensive_tag_id_map);
        pthis->comprehensive_tag_id_map = NULL;
    }

    pthis->next_cell_id = 0;
    pthis->max_cell_id = -1;

    memset(pthis->id_tag_array, 0, sizeof(struct tag_hash_key *) * pthis->id_tag_array_len);
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
    struct tag_id_map *node = NULL;
    HASH_FIND_TAG(pthis->comprehensive_tag_id_map, tag, node);
    if (node != NULL) {
        return node->cell_id;
    }
    if (HASH_COUNT(pthis->comprehensive_tag_id_map) >= pthis->max_cell_num) {
        return -1;
    }

    struct tag_hash_key *tag_copy = tag_hash_key_copy(tag);
    node = (struct tag_id_map *)malloc(sizeof(struct tag_id_map));
    node->tag = tag_copy;
    node->cell_id = pthis->next_cell_id;
    HASH_ADD_TAG(pthis->comprehensive_tag_id_map, tag, node);

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
        return -1;
    }

    if (*popped_cell_id != -1) { // popped, no need to free the tag
        pthis->id_tag_array[*popped_cell_id] = NULL;
    }
    cell_manager_id_tag_array_add(pthis, pthis->next_cell_id, tag_copy);
    return pthis->next_cell_id++;
}


/*
mode:uint32 | max_n_cell:uint32 | id_tag_id_array:int_array | id_tag_array:bin_array(tag_hash_key_serialize) | 
                                | topk_map:bin(heavy_keeper_serialization)
*/
int cell_manager_serialize(const struct cell_manager *pthis, char **blob, size_t *blob_size)
{
    struct fs_reader *reader = fs_reader_new();

    fs_reader_read_uint(reader, (uint32_t)pthis->sampling_mode);
    fs_reader_read_uint(reader, (uint32_t)pthis->max_cell_num);


    if (pthis->sampling_mode == SAMPLING_MODE_COMPREHENSIVE) {
        int n_cell = pthis->max_cell_id + 1;

        int id_rec[n_cell];
        int actual_len = 0;
        for (int i = 0; i < n_cell; i++) {
            if (pthis->id_tag_array[i] == NULL) {
                continue;
            }
            id_rec[actual_len++] = i;
        }
        fs_reader_read_int_array(reader, id_rec, actual_len);

        fs_reader_start_bin_array(reader, actual_len);
        for (int i = 0; i < actual_len; i++) {
            int tmp_id = id_rec[i];

            char *tag_blob = NULL;
            size_t tag_blob_size = 0;
            tag_hash_key_serialize(pthis->id_tag_array[tmp_id], &tag_blob, &tag_blob_size);
            fs_reader_read_bin(reader, tag_blob, tag_blob_size);
            free(tag_blob);
        }
    } else {
        char *topk_blob = NULL;
        size_t topk_blob_size = 0;
        heavy_keeper_serialization(pthis->topk_tag_id_map, &topk_blob, &topk_blob_size);
        fs_reader_read_bin(reader, topk_blob, topk_blob_size);
        free(topk_blob);
    }

    fs_reader_finalize(reader, blob, blob_size);

    return 0;
}

struct cell_manager *cell_manager_deserialize(const char *blob, size_t blob_size)
{
    struct fs_writer *writer = fs_writer_new(blob, blob_size);

    enum sampling_mode mode;
    fs_writer_write_uint(writer, (uint32_t *)&mode);
    int max_n_cell;
    fs_writer_write_uint(writer, (uint32_t *)&max_n_cell);
    struct cell_manager *pthis = cell_manager_new_without_map(mode, max_n_cell);
    int cell_id_max = -1;

    if (mode == SAMPLING_MODE_TOPK) {
        char *topk_blob;
        size_t topk_blob_size;
        fs_writer_write_bin(writer, &topk_blob, &topk_blob_size);
        pthis->topk_tag_id_map = heavy_keeper_deserialization(topk_blob, topk_blob_size);
        free(topk_blob);

        struct heavy_keeper_result *result = heavy_keeper_query(pthis->topk_tag_id_map);
        for (size_t i = 0; i < result->n_key; i++) {
            cell_manager_id_tag_array_add(pthis, result->cell_id[i], result->tags[i]);

            if (result->cell_id[i] > cell_id_max) {
                cell_id_max = result->cell_id[i];
            }
        }
        heavy_keeper_result_free(result);
    } else {
        size_t n_cell;
        int *cell_id_array;
        fs_writer_write_int_array(writer, &cell_id_array, &n_cell);
        char **tag_blob;
        size_t *tag_blob_size;
        fs_writer_write_bin_array(writer, &tag_blob, &n_cell, &tag_blob_size);
    
        for (size_t i = 0; i < n_cell; i++) {
            int cell_id = cell_id_array[i];

            struct tag_hash_key *tag = tag_hash_key_deserialize(tag_blob[i], tag_blob_size[i]);
            struct tag_id_map *node = (struct tag_id_map *)malloc(sizeof(struct tag_id_map));
            node->tag = tag;
            node->cell_id = cell_id;
            HASH_ADD_TAG(pthis->comprehensive_tag_id_map, tag, node);
   
            cell_manager_id_tag_array_add(pthis, cell_id, tag);

            if (cell_id > cell_id_max) {
                cell_id_max = cell_id;
            }
        }

        for (size_t i = 0; i < n_cell; i++) {
            free((void *)tag_blob[i]);
        }
        free(tag_blob);
        free(tag_blob_size);
        free(cell_id_array);
    }
    pthis->next_cell_id = cell_id_max + 1;

    fs_writer_free(writer);
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
    HASH_ADD_TAG(pthis->head, tag, node);
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