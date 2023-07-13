

// TODO: tag naming -> tag
// todo : 又改回去了，MAX_TAG_KEY_LENGTH 和 MAX_TAG_STRING_VAL_LENGTH不在了
// todo: 传入的应该是my tag，带length的那种
// todo: 这个length要放到哈希里，快一点（优化时）

// todo: 修改哈希算法

// todo: topk 里传的也是一个key。


// NOTE: 之前讨论的有问题，如果是comprehensive，那确实每个session都只有一个cell_id，但是如果是topk，那么每次add metric前都要更新，不然topk的榜单咋办。

/* -------------------------------------------------------------------------- */
/*                                讨论怎么分配tag id                                */
/* -------------------------------------------------------------------------- */
// /* ----------------------------------- 方案1 ---------------------------------- */
// 每次id 由fieldstat 分配，每次add topk 都+1，覆盖掉原来的id。
// 建立cell回收机制，不然cell 越来越多。

// 回收要满足的：
// heavy keeper 添加的时候，如果pop了元素，把这个元素的data（就是cellid) 返回出来

// 我要能通过这个id找到cell(id 到cell是不是也得是一个哈希。。。有没有巧办法。）


// 或者由heavy keeper 自己释放，但是cell的释放还是挺麻烦的，会让代码变得很复杂很恶心。

/* ----------------------------------- 方案2 ---------------------------------- */
// 不回收了
// fixme: 当前使用方案2，合理吗

// TODO: 应该是用这个，暂时先拿uthash凑合一下
// #include "my_ut_hash.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "uthash.h"
#include "mpack/mpack.h"
#include "fieldstat.h"
#include "tags/dimension_cube_map.h"
#include "tags/heavy_keeper.h"

struct cell_hash_node {
    char *key; // kv_pair of tags
    size_t key_len; // exclude '\0'
    int cell_id;
    UT_hash_handle hh;
};

struct tag_cube_map {
    int cell_id;
    struct fieldstat_tag *tags;
    size_t n_tags;
    UT_hash_handle hh;
};

struct cell_manager {
    union {
        struct heavy_keeper *topk_tag_id_map;
        struct cell_hash_node *comprehensive_tag_id_map;
    };
    struct tag_cube_map *id_tag_map;
    enum sampling_mode sampling_mode;
    int max_cell_num;
    int next_cell_id; // only used in comprehensive mode
};

void fieldtag_copy(const struct fieldstat_tag *kv_pair, struct fieldstat_tag *dest)
{
    dest->key = strdup(kv_pair->key);
    dest->type = kv_pair->type;
    switch(kv_pair->type)
    {
        case TAG_INTEGER:
            dest->value_int = kv_pair->value_int;
            break;
        case TAG_DOUBLE:
            dest->value_double = kv_pair->value_double;
            break;
        case TAG_CSTRING:
            dest->value_str = strdup(kv_pair->value_str);
            break;
        default:
            assert(0);
            break;
    }
}

void fieldtag_clear(struct fieldstat_tag *kv_pair)
{
    free((char *)kv_pair->key);
    if (kv_pair->type == TAG_CSTRING) {
        free((char *)kv_pair->value_str);
    }
}

void fieldtag_list_free(struct fieldstat_tag *kv_pairs, size_t n_pairs)
{
    for (int i = 0; i < (int)n_pairs; i++) {
        fieldtag_clear(&kv_pairs[i]);
    }
    free(kv_pairs);
}

static void build_dynamic_cell_key(const struct fieldstat_tag tags[], size_t n_tags, char **out_key, size_t *out_key_size)
{
	int i = 0;
	int used_len = 0;
	struct fieldstat_tag *tag = NULL;

    char one_time_key[1024] = {0};
    size_t tmp_key_size = sizeof(one_time_key);
    size_t remain_key_size = tmp_key_size;
    size_t total_key_size = tmp_key_size;
    char *dynamic_mem = (char *)malloc(remain_key_size);

	for(i = 0; i < (int)n_tags; i++)
	{
		tag = (struct fieldstat_tag *)&tags[i];
		switch(tag->type)
		{
			case 0:
				used_len = snprintf(one_time_key, tmp_key_size, "%s%lld", tag->key, tag->value_int);
				break;
			case 1:
				used_len = snprintf(one_time_key, tmp_key_size, "%s%lf", tag->key, tag->value_double);
				break;
			case 2:
				used_len = snprintf(one_time_key, tmp_key_size, "%s%s", tag->key, tag->value_str);
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
    struct tag_cube_map *node = NULL;
    struct tag_cube_map *tmp = NULL;
    HASH_ITER(hh, pthis->id_tag_map, node, tmp) {
        HASH_DEL(pthis->id_tag_map, node);
        fieldtag_list_free(node->tags, node->n_tags);
        free(node);
    }
}

void cell_manager_comprehensive_tag_id_map_free(struct cell_manager *pthis)
{
    struct cell_hash_node *node = NULL;
    struct cell_hash_node *tmp = NULL;
    HASH_ITER(hh, pthis->comprehensive_tag_id_map, node, tmp) {
        HASH_DEL(pthis->comprehensive_tag_id_map, node);
        free(node->key);
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
        printf("cell_manager_free going to call free heavy_keeper\n");
        heavy_keeper_free(pthis->topk_tag_id_map);
    } else {
        cell_manager_comprehensive_tag_id_map_free(pthis);
    }

    free(pthis);
}

int cell_manager_get_tag_by_cell_id(const struct cell_manager *pthis, int cell_id, struct fieldstat_tag **tag_out, size_t *tag_count_out)
{
    printf("cell_manager_get_tag_by_cell_id enter, id: %d\n", cell_id);
    struct tag_cube_map *hash_hdr = pthis->id_tag_map;
    struct tag_cube_map *node = NULL;
    HASH_FIND_INT(hash_hdr, &cell_id, node);
    if (node == NULL) {
        return -1;
    }

    *tag_out = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * node->n_tags);
    for (int i = 0; i < (int)node->n_tags; i++) {
        fieldtag_copy(&node->tags[i], &(*tag_out)[i]);
    }
    *tag_count_out = node->n_tags;
    printf("cell_manager_get_tag_by_cell_id exit\n");
    return 0;
}

void cell_manager_id_tag_map_add(struct cell_manager *pthis, int cell_id, const struct fieldstat_tag tags[], size_t n_tags)
{
    struct tag_cube_map *node = (struct tag_cube_map *)malloc(sizeof(struct tag_cube_map));
    node->cell_id = cell_id;
    node->tags = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * n_tags);
    for (int i = 0; i < (int)n_tags; i++) {
        fieldtag_copy(&tags[i], &node->tags[i]);
    }
    node->n_tags = n_tags;

    HASH_ADD_INT(pthis->id_tag_map, cell_id, node);
}

int cell_manager_get_cell_id_by_tag(const struct cell_manager *pthis, const struct fieldstat_tag *tag, size_t tag_count)
{
    char *key = NULL;
    size_t key_size = 0;
    build_dynamic_cell_key(tag, tag_count, &key, &key_size);
    int cell_id = -1;

    if (pthis->sampling_mode == SAMPLING_MODE_TOPK) {
        unsigned int count_dummy = 0;
        heavy_keeper_query_one(pthis->topk_tag_id_map, key, key_size, &count_dummy, &cell_id);
    } else {
        printf("cell_manager_get_cell_id_by_tag find comprehensive, key: %s, comprehensive_tag_id_map card: %d\n", key, (int)HASH_COUNT(pthis->comprehensive_tag_id_map));
        struct cell_hash_node *node = NULL;
        HASH_FIND(hh, pthis->comprehensive_tag_id_map, key, key_size, node);
        if (node == NULL) {
            printf("cell_manager_get_cell_id_by_tag find comprehensive, not found\n");
            cell_id = -1;
        } else {
            cell_id = node->cell_id;
            printf("cell_manager_get_cell_id_by_tag find comprehensive, found, cell id : %d\n", cell_id);
        }
    }

    free(key);
    return cell_id;
}

int cell_manager_get_count_by_tag(const struct cell_manager *pthis, const struct fieldstat_tag *tag, size_t tag_count)
{
    if (pthis->sampling_mode == SAMPLING_MODE_COMPREHENSIVE) {
        return -1;
    }
    char *key = NULL;
    size_t key_size = 0;
    build_dynamic_cell_key(tag, tag_count, &key, &key_size);
    unsigned int count = 0;

    if (pthis->sampling_mode == SAMPLING_MODE_TOPK) {
        heavy_keeper_query_one(pthis->topk_tag_id_map, key, key_size, &count, NULL);
    }

    free(key);
    return count;
}

void cell_manager_comprehensive_map_add(struct cell_manager *pthis, const struct fieldstat_tag *tag, size_t tag_count, int cell_id)
{
    char *key = NULL;
    size_t key_size = 0;
    build_dynamic_cell_key(tag, tag_count, &key, &key_size);

    printf("cell_manager_comprehensive_map_add enter, dynamic key: %s, key_size: %zu, cell_id: %d\n", key, key_size, cell_id);

    struct cell_hash_node *node = (struct cell_hash_node *)malloc(sizeof(struct cell_hash_node));
    node->key = key;
    node->key_len = key_size;
    node->cell_id = cell_id;
    HASH_ADD_KEYPTR(hh, pthis->comprehensive_tag_id_map, node->key, node->key_len, node);
}

int cell_manager_topk_map_add(struct cell_manager *pthis, const struct fieldstat_tag *tag, size_t tag_count, unsigned int count, int cell_id)
{
    char *key = NULL;
    size_t key_size = 0;
    build_dynamic_cell_key(tag, tag_count, &key, &key_size);

    int popped_cell_id = -1;

    int ret = heavy_keeper_with_data_add(pthis->topk_tag_id_map, key, key_size, count, cell_id, &popped_cell_id);
    // todo: 想办法通知一下metric，把这个cube删了
    free(key);

    if (ret == -1) {
        return -1;
    }
    if (popped_cell_id != -1) {
        struct tag_cube_map *node = NULL;
        HASH_FIND_INT(pthis->id_tag_map, &popped_cell_id, node);
        if (node != NULL) {
            HASH_DEL(pthis->id_tag_map, node);
            fieldtag_list_free(node->tags, node->n_tags);
            free(node);
        }
    }
    return ret;
}

int cell_manager_add_cell(struct cell_manager *pthis, const struct fieldstat_tag *tag, size_t tag_count, unsigned int count)
{
    if (pthis->sampling_mode == SAMPLING_MODE_COMPREHENSIVE) {
        if (HASH_COUNT(pthis->comprehensive_tag_id_map) >= pthis->max_cell_num) {
            return -1;
        }
        cell_manager_comprehensive_map_add(pthis, tag, tag_count, pthis->next_cell_id);
    } else {
        int ret = cell_manager_topk_map_add(pthis, tag, tag_count, count, pthis->next_cell_id);
        if (ret == -1) {
            return -1;
        }
        if (ret == 1) { // no new cube added, use original cube id
            return 0;
        }
    }

    cell_manager_id_tag_map_add(pthis, pthis->next_cell_id, tag, tag_count);
    return pthis->next_cell_id++;
}

int my_cmp_tag(const void *a, const void *b)
{
    struct fieldstat_tag *aa = (struct fieldstat_tag *)a;
    struct fieldstat_tag *bb = (struct fieldstat_tag *)b;
    int key_cmp = strcmp(aa->key, bb->key);
    if (key_cmp != 0) {
        return key_cmp;
    }

    assert(aa->type == bb->type);
    switch (aa->type) {
        case TAG_INTEGER:
            return aa->value_int - bb->value_int;
        case TAG_DOUBLE:
            return aa->value_double - bb->value_double;
        case TAG_CSTRING:
            return strcmp(aa->value_str, bb->value_str);
        default:
            return 0;
    }
}

/* 
    [{
        "key":<tag key string>
        "type":0 1 or 2
        "value":<tag value, can be int(0) double(1) or str(2)>
    }]
*/
void fieldtag_list_serialize(const struct fieldstat_tag *tag_list, size_t tag_num, char **blob, size_t *blob_size)
{
    struct fieldstat_tag *tag_list_sorted = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * tag_num);
    for (int i = 0; i < tag_num; i++) {
        fieldtag_copy(&tag_list[i], &tag_list_sorted[i]);
    }
    // sort tag so that when merge by blob, we can merge cells with the same tags
    qsort(tag_list_sorted, tag_num, sizeof(struct fieldstat_tag), my_cmp_tag);

    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_size);

    mpack_start_array(&writer, tag_num);
    for (int j = 0; j < tag_num; j++) {
        mpack_start_map(&writer, 3);
        mpack_write_cstr(&writer, "key");
        mpack_write_cstr(&writer, tag_list_sorted[j].key);
        mpack_write_cstr(&writer, "type");
        mpack_write_i8(&writer, tag_list_sorted[j].type);
        mpack_write_cstr(&writer, "value");
        switch (tag_list_sorted[j].type)
        {
            case TAG_INTEGER:
                mpack_write_i64(&writer, tag_list_sorted[j].value_int);
                break;
            case TAG_DOUBLE:
                mpack_write_double(&writer, tag_list_sorted[j].value_double);
                break;
            case TAG_CSTRING:
                mpack_write_cstr(&writer, tag_list_sorted[j].value_str);
                break;
            default:
                assert(0);
                break;
        }
        mpack_finish_map(&writer);
    }
    mpack_finish_array(&writer);

    if (mpack_writer_destroy(&writer) != mpack_ok) {
        fprintf(stderr, "mpack writer fieldtag_list_serialize destroy failed\n");
        exit(1);
    }

    fieldtag_list_free(tag_list_sorted, tag_num);
}


void fieldtag_list_deserialize(const char *blob, size_t blob_size, struct fieldstat_tag **tag_list_out, size_t *tag_num_out)
{
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, blob_size);
    mpack_tree_parse(&tree);
    mpack_node_t tag_arr = mpack_tree_root(&tree);

    size_t tag_num = mpack_node_array_length(tag_arr);

    struct fieldstat_tag *tag_list = malloc(sizeof(struct fieldstat_tag) * tag_num);
    for (int j = 0; j < tag_num; j++) {
        mpack_node_t tag = mpack_node_array_at(tag_arr, j);

        mpack_node_t key = mpack_node_map_cstr(tag, "key");
        tag_list[j].key = (const char *)malloc(mpack_node_strlen(key) + 1);
        mpack_node_copy_cstr(key, (char *)tag_list[j].key, mpack_node_strlen(key) + 1);
        tag_list[j].type = mpack_node_i8(mpack_node_map_cstr(tag, "type"));

        mpack_node_t value = mpack_node_map_cstr(tag, "value");

        switch (tag_list[j].type)
        {
            case TAG_INTEGER:
                tag_list[j].value_int = mpack_node_i64(value);
                break;
            case TAG_DOUBLE:
                tag_list[j].value_double = mpack_node_double(value);
                break;
            case TAG_CSTRING:
                tag_list[j].value_str = (const char *)malloc(mpack_node_strlen(value) + 1);
                mpack_node_copy_cstr(value, (char *)tag_list[j].value_str, mpack_node_strlen(value) + 1);
                break;
            default:
                assert(0);
                break;
        }
    }
    *tag_list_out = tag_list;
    *tag_num_out = tag_num;

    mpack_tree_destroy(&tree);
}

/*
{
    "mode":<sampling mode int8>
    "max_n_cell":<max cell number int32>

    "id_tag_map": [{
        "id": <id in cell manager>,
        "tags": [{
                "key": "field1",
                "type":0, 1, or 2
                "value": "value1"
                },
                <tag2>,...],
        },
        ...,
        ]
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
    struct tag_cube_map *node = NULL;
    struct tag_cube_map *tmp = NULL;
    HASH_ITER(hh, pthis->id_tag_map, node, tmp) {
        mpack_build_map(&writer);
        mpack_write_cstr(&writer, "id");
        mpack_write_int(&writer, node->cell_id);
        mpack_write_cstr(&writer, "tags");
        char *tag_blob = NULL;
        size_t tag_blob_size = 0;
        fieldtag_list_serialize(node->tags, node->n_tags, &tag_blob, &tag_blob_size);
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
        mpack_node_t tags = mpack_node_map_cstr(cell, "tags");
        size_t n_tag;
        struct fieldstat_tag *tag_list;
        const char *tag_blob = mpack_node_bin_data(tags);
        size_t tag_blob_size = mpack_node_bin_size(tags);
        fieldtag_list_deserialize(tag_blob, tag_blob_size, &tag_list, &n_tag);
        cell_manager_id_tag_map_add(pthis, cell_id, tag_list, n_tag);
        if (mode == SAMPLING_MODE_COMPREHENSIVE) {
            cell_manager_comprehensive_map_add(pthis, tag_list, n_tag, cell_id);
            printf("cell_manager_deserialize add a cell %d, tag key(0): %s\n", cell_id, tag_list[0].key);
        }

        if (cell_id > cell_id_max) {
            cell_id_max = cell_id;
        }

        fieldtag_list_free(tag_list, n_tag);
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