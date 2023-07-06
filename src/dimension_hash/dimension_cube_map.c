

// TODO: tag naming -> dimension
// todo : 又改回去了，MAX_TAG_KEY_LENGTH 和 MAX_TAG_STRING_VAL_LENGTH不在了
// todo: 传入的应该是my dimension，带length的那种
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




#include "my_ut_hash.h"

// NOTE:查询一个cell的tag时，本身就存在着dimension的拼接，所以cell里不体现dimension的概念，仅传ID。然而关于单个dimension的哈希和结构定义一定要另外搞一个文件，不然ut哈希的头文件没法写
// fIXME:于是，就有了这个文件，这里面的uthash是自定义的。







struct cell_hash_node {
    char *key; // kv_pair of tags
    size_t key_len; // exclude '\0'
    int dimension_id;
    UT_hash_handle hh;
};


union dimension_id_map {
    struct heavy_keeper *topk_dimension_id_map;
    struct cell_hash_node *comprehensive_dimension_id_map;
};




void my_dimension_new(struct fieldstat_tag *dimensions, size_t n_dimension, struct my_dimension **my_dimension_out, size_t *n_my_dimension_out);
void my_dimension_free(struct my_dimension **my_dimension, size_t n_my_dimension);
void my_dimension_parse(const struct my_dimension *my_dimension, size_t n_my_dimension, struct fieldstat_tag **dimensions_out, size_t *n_dimension_out);






// FIXME: 现在文件里放的这两个函数，还是不在头文件里了。
static void fieldtag_copy(const struct fieldstat_tag *kv_pair, struct fieldstat_tag *dest)
{
    memcpy((char *)dest->key, kv_pair->key, MAX_TAG_KEY_LENGTH);
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
            memcpy((char *)dest->value_str, kv_pair->value_str, MAX_TAG_STRING_VAL_LENGTH);
            break;
        default:
            assert(0);
            break;
    }
}


// TODO: naming
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
