

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



// TODO: 应该是用这个，暂时先拿uthash凑合一下
// #include "my_ut_hash.h"

// NOTE:查询一个cell的tag时，本身就存在着tag的拼接，所以cell里不体现tag的概念，仅传ID。然而关于单个tag的哈希和结构定义一定要另外搞一个文件，不然ut哈希的头文件没法写
// fIXME:于是，就有了这个文件，这里面的uthash是自定义的。


#include "uthash.h"
#include "fieldstat.h"

struct cell_hash_node {
    char *key; // kv_pair of tags
    size_t key_len; // exclude '\0'
    int cell_id;
    UT_hash_handle hh;
};

struct cell_manager {
    union {
        struct heavy_keeper *topk_tag_id_map;
        struct cell_hash_node *comprehensive_tag_id_map;
    };
    enum sampling_mode sampling_mode;
    int max_cell_num;
};

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

struct cell_manager *cell_manager_create(enum sampling_mode sampling_mode, int max_cell_num)
{
    struct cell_manager *cell_manager = (struct cell_manager *)malloc(sizeof(struct cell_manager));

    cell_manager->sampling_mode = sampling_mode;
    cell_manager->max_cell_num = max_cell_num;
    if (sampling_mode == SAMPLING_MODE_TOPK) {
        cell_manager->topk_tag_id_map = heavy_keeper_create(max_cell_num);
    } else {
        cell_manager->comprehensive_tag_id_map = NULL;
    }

    return cell_manager;
}

int cell_manager_get_tag_by_cube_id(const struct tag_cube_map *pthis, int cube_id, struct fieldstat_tag **tag_out, size_t *tag_count_out);
