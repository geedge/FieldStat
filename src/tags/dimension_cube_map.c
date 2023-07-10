

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
#include "stdio.h"
#include "assert.h"
#include "stdlib.h"
#include "string.h"

#include "uthash.h"
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
};


static void fieldtag_copy(const struct fieldstat_tag *kv_pair, struct fieldstat_tag *dest)
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

void fieldtag_arr_free(struct fieldstat_tag *kv_pairs, size_t n_pairs)
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

struct cell_manager *cell_manager_new(enum sampling_mode sampling_mode, int max_cell_num)
{
    struct cell_manager *cell_manager = (struct cell_manager *)malloc(sizeof(struct cell_manager));

    cell_manager->sampling_mode = sampling_mode;
    cell_manager->max_cell_num = max_cell_num;
    if (sampling_mode == SAMPLING_MODE_TOPK) {
        cell_manager->topk_tag_id_map = heavy_keeper_with_data_new(max_cell_num);
    } else {
        cell_manager->comprehensive_tag_id_map = NULL;
    }

    return cell_manager;
}

int cell_manager_get_tag_by_cube_id(const struct cell_manager *pthis, int cube_id, struct fieldstat_tag **tag_out, size_t *tag_count_out)
{
    printf("cell_manager_get_tag_by_cube_id enter, id: %d\n", cube_id);
    struct tag_cube_map *hash_hdr = pthis->id_tag_map;
    struct tag_cube_map *node = NULL;
    HASH_FIND_INT(hash_hdr, &cube_id, node);
    if (node == NULL) {
        return -1;
    }

    *tag_out = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * node->n_tags);
    for (int i = 0; i < (int)node->n_tags; i++) {
        fieldtag_copy(&node->tags[i], &(*tag_out)[i]);
    }
    *tag_count_out = node->n_tags;
    printf("cell_manager_get_tag_by_cube_id exit\n");
    return 0;
}

void cell_manager_id_tag_map_add(struct cell_manager *pthis, int cube_id, const struct fieldstat_tag tags[], size_t n_tags)
{
    struct tag_cube_map *node = (struct tag_cube_map *)malloc(sizeof(struct tag_cube_map));
    node->cell_id = cube_id;
    node->tags = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * n_tags);
    for (int i = 0; i < (int)n_tags; i++) {
        fieldtag_copy(&tags[i], &node->tags[i]);
    }
    node->n_tags = n_tags;

    HASH_ADD_INT(pthis->id_tag_map, cell_id, node);
}

int cell_manager_get_cube_id_by_tag(const struct cell_manager *pthis, const struct fieldstat_tag *tag, size_t tag_count)
{
    char *key = NULL;
    size_t key_size = 0;
    build_dynamic_cell_key(tag, tag_count, &key, &key_size);
    int cube_id = -1;

    if (pthis->sampling_mode == SAMPLING_MODE_TOPK) {
        unsigned int count_dummy = 0;
        heavy_keeper_query_one(pthis->topk_tag_id_map, key, key_size, &count_dummy, &cube_id);
    } else {
        struct cell_hash_node *node = NULL;
        HASH_FIND(hh, pthis->comprehensive_tag_id_map, key, key_size, node);
        if (node == NULL) {
            cube_id = -1;
        } else {
            cube_id = node->cell_id;
        }
    }

    free(key);
    return cube_id;
}

void cell_manager_comprehensive_map_add(struct cell_manager *pthis, const struct fieldstat_tag *tag, size_t tag_count, int cube_id)
{
    char *key = NULL;
    size_t key_size = 0;
    build_dynamic_cell_key(tag, tag_count, &key, &key_size);

    struct cell_hash_node *node = (struct cell_hash_node *)malloc(sizeof(struct cell_hash_node));
    node->key = key;
    node->key_len = key_size;
    node->cell_id = cube_id;
    HASH_ADD_KEYPTR(hh, pthis->comprehensive_tag_id_map, node->key, node->key_len, node);
}

int cell_manager_topk_map_add(struct cell_manager *pthis, const struct fieldstat_tag *tag, size_t tag_count, unsigned int count, int cube_id)
{
    char *key = NULL;
    size_t key_size = 0;
    build_dynamic_cell_key(tag, tag_count, &key, &key_size);

    int popped_cube_id = -1;

    int ret = heavy_keeper_with_data_add(pthis->topk_tag_id_map, key, key_size, count, cube_id, &popped_cube_id);
    free(key);

    if (ret == -1) {
        return -1;
    }
    if (popped_cube_id != -1) {
        struct tag_cube_map *node = NULL;
        HASH_FIND_INT(pthis->id_tag_map, &popped_cube_id, node);
        if (node != NULL) {
            HASH_DEL(pthis->id_tag_map, node);
            fieldtag_arr_free(node->tags, node->n_tags);
            free(node);
        }
    }
    return 0;
}

int cell_manager_add_cube(struct cell_manager *pthis, const struct fieldstat_tag *tag, size_t tag_count, unsigned int count, int allocated_cube_id)
{
    if (pthis->sampling_mode == SAMPLING_MODE_COMPREHENSIVE) {
        if (HASH_COUNT(pthis->comprehensive_tag_id_map) >= pthis->max_cell_num) {
            return -1;
        }
        cell_manager_comprehensive_map_add(pthis, tag, tag_count, allocated_cube_id);
    } else {
        if (cell_manager_topk_map_add(pthis, tag, tag_count, count, allocated_cube_id) == -1) {
            return -1;
        }
    }

    cell_manager_id_tag_map_add(pthis, allocated_cube_id, tag, tag_count);
    return 0; 
}

void cell_manager_free(struct cell_manager *pthis)
{
    struct tag_cube_map *node = NULL;
    struct tag_cube_map *tmp = NULL;
    HASH_ITER(hh, pthis->id_tag_map, node, tmp) {
        HASH_DEL(pthis->id_tag_map, node);
        fieldtag_arr_free(node->tags, node->n_tags);
        free(node);
    }

    if (pthis->sampling_mode == SAMPLING_MODE_COMPREHENSIVE) {
        struct cell_hash_node *node2 = NULL;
        struct cell_hash_node *tmp2 = NULL;
        HASH_ITER(hh, pthis->comprehensive_tag_id_map, node2, tmp2) {
            HASH_DEL(pthis->comprehensive_tag_id_map, node2);
            free(node2->key);
            free(node2);
        }
    } else {
        heavy_keeper_free(pthis->topk_tag_id_map);
    }

    free(pthis);
}