#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "mpack/mpack.h"
#include "fieldstat.h"
#include "tags/dimension_cube_map.h"
#include "tags/heavy_keeper.h"
// #include "tags/my_ut_hash_inner.h"
#include "tags/my_ut_hash.h"
#include "tags/tag_id_map.h"

struct id_tag_map {
    int cell_id;
    struct my_hash_tag_key *tag;
    UT_hash_handle hh;
};

struct cell_manager {
    union {
        struct heavy_keeper *topk_tag_id_map;
        struct tag_id_map *comprehensive_tag_id_map;
    };
    struct id_tag_map *id_tag_map;
    enum sampling_mode sampling_mode;
    int max_cell_num;
    int next_cell_id; // only used in comprehensive mode
};

void build_dynamic_cell_key(const struct fieldstat_tag tags[], size_t n_tags, char **out_key, size_t *out_key_size)
{
	int i = 0;
	int used_len = 0;
	struct fieldstat_tag *tag = NULL;

    char one_time_key[1024] = {0};
    size_t tmp_key_size = sizeof(one_time_key);
    size_t remain_key_size = tmp_key_size;
    size_t total_key_size = tmp_key_size;
    char *dynamic_mem = (char *)malloc(remain_key_size);
    size_t key_len;
    size_t val_len;

	for(i = 0; i < (int)n_tags; i++)
	{
		tag = (struct fieldstat_tag *)&tags[i];
		switch(tag->type)
		{
			case 0:
                key_len = strlen(tag->key);
                val_len = sizeof(long long);
                used_len = key_len + val_len;
                memcpy(one_time_key, tag->key, key_len);
                memcpy(one_time_key + key_len, &tag->value_longlong, val_len);
				break;
			case 1:
                key_len = strlen(tag->key);
                val_len = sizeof(double);
                used_len = key_len + val_len;
                memcpy(one_time_key, tag->key, key_len);
                memcpy(one_time_key + key_len, &tag->value_double, val_len);
				break;
			case 2:
                key_len = strlen(tag->key);
                val_len = strlen(tag->value_str);
                used_len = key_len + val_len;
                memcpy(one_time_key, tag->key, key_len);
                memcpy(one_time_key + key_len, tag->value_str, val_len);
				break;
			default:
				assert(0);
				break;
		}
        
        if (used_len >= remain_key_size) {
            total_key_size += tmp_key_size;
            dynamic_mem = (char *)realloc(dynamic_mem, total_key_size);
            remain_key_size += tmp_key_size;
        }

        memcpy(dynamic_mem + total_key_size - remain_key_size, one_time_key, used_len);
        remain_key_size -= used_len;
	}

    *out_key = (char *)malloc(total_key_size - remain_key_size + 1);
    memcpy(*out_key, dynamic_mem, total_key_size - remain_key_size);
    (*out_key)[total_key_size - remain_key_size] = '\0';
    *out_key_size = total_key_size - remain_key_size; // not include '\0'

    free(dynamic_mem);
}

struct cell_manager *cell_manager_new_without_map(enum sampling_mode sampling_mode, int max_cell_num)
{
    struct cell_manager *cell_manager = (struct cell_manager *)malloc(sizeof(struct cell_manager));
    memset(cell_manager, 0, sizeof(struct cell_manager));

    cell_manager->sampling_mode = sampling_mode;
    cell_manager->max_cell_num = max_cell_num;
    cell_manager->next_cell_id = 0;

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

void cell_manager_tag_cube_map_free(struct cell_manager *pthis)
{
    struct id_tag_map *node = NULL;
    struct id_tag_map *tmp = NULL;
    HASH_ITER(hh, pthis->id_tag_map, node, tmp) {
        HASH_DEL(pthis->id_tag_map, node);
        my_hash_tag_key_free(node->tag);
        free(node);
    }
}

void cell_manager_comprehensive_tag_id_map_free(struct cell_manager *pthis)
{
    struct tag_id_map *node = NULL;
    struct tag_id_map *tmp = NULL;
    HASH_ITER(hh, pthis->comprehensive_tag_id_map, node, tmp) {
        HASH_DEL(pthis->comprehensive_tag_id_map, node);
        my_hash_tag_key_free(node->tag);
        free(node);
    }
}

void cell_manager_reset(struct cell_manager *pthis)
{
    if (pthis->sampling_mode == SAMPLING_MODE_TOPK) {
        heavy_keeper_clear(pthis->topk_tag_id_map);
    } else {
        cell_manager_comprehensive_tag_id_map_free(pthis);
        pthis->next_cell_id = 0;
    }

    cell_manager_tag_cube_map_free(pthis);
}

void cell_manager_free(struct cell_manager *pthis)
{
    cell_manager_tag_cube_map_free(pthis);

    if (pthis->sampling_mode == SAMPLING_MODE_TOPK) {
        heavy_keeper_free(pthis->topk_tag_id_map);
    } else {
        cell_manager_comprehensive_tag_id_map_free(pthis);
    }

    free(pthis);
}

const struct my_hash_tag_key *cell_manager_get_tag_by_cell_id(const struct cell_manager *pthis, int cell_id)
{
    struct id_tag_map *hash_hdr = pthis->id_tag_map;
    struct id_tag_map *node = NULL;
    HASH_FIND_INT(hash_hdr, &cell_id, node);
    if (node == NULL) {
        return NULL;
    }

    return node->tag;
}

void cell_manager_id_tag_map_add(struct cell_manager *pthis, int cell_id, const struct my_hash_tag_key *tag)
{
    struct id_tag_map *node = (struct id_tag_map *)malloc(sizeof(struct id_tag_map));
    node->cell_id = cell_id;
    node->tag = my_hash_tag_key_copy(tag);
    HASH_ADD_INT(pthis->id_tag_map, cell_id, node);
}

int cell_manager_get_cell_id_by_tag(const struct cell_manager *pthis, const struct my_hash_tag_key *tag)
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

int cell_manager_get_count_by_tag(const struct cell_manager *pthis, const struct my_hash_tag_key *tag)
{
    if (pthis->sampling_mode == SAMPLING_MODE_COMPREHENSIVE) {
        return -1;
    }

    unsigned int count = 0;
    heavy_keeper_query_one(pthis->topk_tag_id_map, tag, &count, NULL);

    return count;
}

int cell_manager_comprehensive_map_add(struct cell_manager *pthis, const struct my_hash_tag_key *tag, int cell_id, int *existing_cell_id)
{
    struct tag_id_map *node = NULL;
    if (HASH_COUNT(pthis->comprehensive_tag_id_map) != 0) {
        node = my_hash_key_list_find(pthis->comprehensive_tag_id_map, tag);
    }

    if (node != NULL) {
        *existing_cell_id = node->cell_id;
        return 0;
    }
    if (HASH_COUNT(pthis->comprehensive_tag_id_map) >= pthis->max_cell_num) {
        return -1;
    }

    node = (struct tag_id_map *)malloc(sizeof(struct tag_id_map));
    node->tag = my_hash_tag_key_copy(tag);
    node->cell_id = cell_id;
    my_hash_key_list_add(&pthis->comprehensive_tag_id_map, node);
    *existing_cell_id = -1;
    return 0;
}

int cell_manager_topk_map_add(struct cell_manager *pthis, const struct my_hash_tag_key *tag, unsigned int count, int cell_id, int *popped_cell_id, int *existing_cell_id)
{
    *existing_cell_id = -1;

    int ret = heavy_keeper_add(pthis->topk_tag_id_map, tag, count, cell_id, popped_cell_id);
    if (ret == 1) { // use the original id
        heavy_keeper_query_one(pthis->topk_tag_id_map, tag, NULL, existing_cell_id);
        *popped_cell_id = -1;
        return 1;
    }

    if (ret == -1) { // didn't add
        *popped_cell_id = -1;
        return -1;
    }

    if (*popped_cell_id != -1) { // popped one, then add the new one with new id
        struct id_tag_map *node = NULL;
        HASH_FIND_INT(pthis->id_tag_map, popped_cell_id, node);

        if (node != NULL) {
            HASH_DEL(pthis->id_tag_map, node);
            my_hash_tag_key_free(node->tag);
            free(node);
        }
    }
    return ret;
}

int cell_manager_add_cell(struct cell_manager *pthis, const struct my_hash_tag_key *tag)
{
    int existing_cell_id = -1;
    int ret = cell_manager_comprehensive_map_add(pthis, tag, pthis->next_cell_id, &existing_cell_id);
    if (ret == -1) {
        return -1;
    }
    if (existing_cell_id != -1) {
        return existing_cell_id;
    }

    cell_manager_id_tag_map_add(pthis, pthis->next_cell_id, tag);
    return pthis->next_cell_id++;
}

int cell_manager_add_cell_topk(struct cell_manager *pthis, const struct my_hash_tag_key *tag, unsigned int count, int *popped_cell_id)
{
    int ret_cell_id = -1;
    int ret = cell_manager_topk_map_add(pthis, tag, count, pthis->next_cell_id, popped_cell_id, &ret_cell_id);
    if (ret == -1) {
        return -1;
    }
    if (ret == 1) {
        return ret_cell_id;
    }

/*
To mitigate the risk of unbounded array size due to potentially infinite growth of cell IDs, a maximum limit is set for cell IDs in the metric.
This limit is configured as 5 times the value of K. Even in the worst-case scenario of malicious attacks, this prevents excessive memory usage.
Even though there is a small possibility of not counting certain flows due to the limit, it is expected that the unallocated IDs would correspond to mouse flows, 
    which would have minimal impact on accuracy.
The number 5 was determined through experiment, where multiple tests were conducted, randomly generating flows by which cells are determined.
    The observed stable cell ID count did not exceed 3 times the value of K. Therefore, a conservative value of 5 was chosen.
*/
    if (pthis->next_cell_id >= pthis->max_cell_num * 5) { // max int is much larger, no risk of overflow
        return -1;
    }

    cell_manager_id_tag_map_add(pthis, pthis->next_cell_id, tag);
    return pthis->next_cell_id++;
}


/*
{
    "mode":<sampling mode int8>
    "max_n_cell":<max cell number int32>

    "id_tag_map": [{
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
    
    mpack_write_cstr(&writer, "id_tag_map");
    mpack_build_array(&writer);
    struct id_tag_map *node = NULL;
    struct id_tag_map *tmp = NULL;
    HASH_ITER(hh, pthis->id_tag_map, node, tmp) {
        mpack_build_map(&writer);
        mpack_write_cstr(&writer, "id");
        mpack_write_int(&writer, node->cell_id);
        mpack_write_cstr(&writer, "tags");
        char *tag_blob = NULL;
        size_t tag_blob_size = 0;
        my_hash_tag_key_serialize(node->tag, &tag_blob, &tag_blob_size);
        mpack_write_bin(&writer, tag_blob, tag_blob_size);
        free(tag_blob);
        mpack_complete_map(&writer);
    }
    mpack_complete_array(&writer); // id_tag_map

    if (pthis->sampling_mode == SAMPLING_MODE_TOPK) {
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

    mpack_node_t id_tag_map = mpack_node_map_cstr(root, "id_tag_map");
    int n_cell = mpack_node_array_length(id_tag_map);
    int cell_id_max = -1;
    for (int i = 0; i < n_cell; i++) {
        mpack_node_t cell = mpack_node_array_at(id_tag_map, i);
        int cell_id = mpack_node_i32(mpack_node_map_cstr(cell, "id"));
        mpack_node_t tags_node = mpack_node_map_cstr(cell, "tags");
        const char *tag_blob = mpack_node_bin_data(tags_node);
        size_t tag_blob_size = mpack_node_bin_size(tags_node);
        struct my_hash_tag_key *tag = my_hash_tag_key_deserialize(tag_blob, tag_blob_size);
        cell_manager_id_tag_map_add(pthis, cell_id, tag);
        if (mode == SAMPLING_MODE_COMPREHENSIVE) {
            int dummy;
            cell_manager_comprehensive_map_add(pthis, tag, cell_id, &dummy);
        }

        if (cell_id > cell_id_max) {
            cell_id_max = cell_id;
        }

        my_hash_tag_key_free(tag);
    }
    if (mode == SAMPLING_MODE_TOPK) {
        mpack_node_t topk_map = mpack_node_map_cstr(root, "topk_map");
        const char *topk_blob = mpack_node_bin_data(topk_map);
        size_t topk_blob_size = mpack_node_bin_size(topk_map);
        pthis->topk_tag_id_map = heavy_keeper_deserialization(topk_blob, topk_blob_size);
    }
    pthis->next_cell_id = cell_id_max + 1;

    mpack_tree_destroy(&tree);
    return pthis;
}