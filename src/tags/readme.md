# 要做的

## exdata
typedef void *exdata_new_cb(void *arg);

typedef void exdata_free(void *exdata);

typedef void exdata_reset(void *exdata);
typedef void exdata_merge(void *exdata_dst, exdata_src);

typedef void exdata_serialize(void *exdata, &blob, &blob_sz );

int hevay_keeper_set_exdata_schema(instance, new_callback, free_callback, reset_callback);

int heavy_keeper_add(instance, key, key_len, count, void *arg);

void *heavy_keeper_get0_exdata(instance, key, key_len); // 只是获得这个指针

void heavy_keeper_soft_reset(instance);
 
heavy_keeper_get_count() // 获得一共有几个key

heavy_keeper_list(instance, **key, *key_len,  **exdata);

typedef foreach_callback(const char * key, size_t keylen, void * exdata, void *arg);

heavy_keeper_iterate(instance, foreach_callback, void *arg);


## add 逻辑
1. 调用heavy keeper add。这里有几种情况，一个是存在dying 记录，其实就是count = 0的记录，sorted set 其实感知dying。肯定要感知，不然exdata reset 掉存哪。
2. 尝试使用heavy_keeper_get0_exdata ，获得一个cell：struct fs_cell{long long metrics[MAX];int primay_metric_idx;}
3. 对metric 进行操作。

## merge 逻辑
1. 直接cube对齐以后，直接拿出来里面的cell manager 数据，做merge。这里会有一个根据Sampling mode 的Switch。
2. 在内部，不同的有不同策略，比如sorted set，得到两个cell 对应以后，对于内部的ex data，调用merge。

## reset 逻辑
- dying cell 的处理
  - sorted set 在reset 时总是不直接删除dying cell，这部分流程不修改，单纯给它们所有count = 0
  - 所有相关的exdata 调用reset。

## 输出逻辑
1. 输出逻辑其实就是查询逻辑，因为exporter 直接调用fieldstat 相关的查询接口。
2. 涉及到的查询接口，主要是得到cube 里的所有tag，和根据tag 查询metric 的数据。通过heavy_keeper_get0_exdata 和 heavy_keeper_list 实现。另一个方案（修改比较多的），是把写json 输出的过程变成call back，然后每个cell 的数据实际上可以直接输出（包括tag 和 field）。

# 数据结构
fieldstat -> cube -> cell -> exdata(metric)

注意，fieldstat 知道exdata 的具体用法，但exdata 的所有权归cell,  这里，所有权和使用分离了。

cell 被heavy keeper，comprehensive 等等东西管理，传入的是单一的key，实际上就是tag。不过现在cell 层不感知tag。

metric 过去包含了一个cell （称为metric_measure_data） 的array，现在单纯就是counter、hll 或者histogram的包装。

# todo
- cell_manager get count by tag 已删除，这个和cell manager 职责不符，只用来测试。
  - 处理相关unit test(实际上可以去掉了)
- merge 是个大块儿
  - heavy_keeper_get_true_pst_tag 删除
  - 关于metric 的操作全部移动到topk 等
- 目前先用tag_hash_key ，减少修改点，后面换成 char *
- sorted_set_get_cell_id 这个函数被大量使用，但是现在所有操作都直接定位entry 了，思考怎么替换他
- 当添加的不是primary metric，fieldstat 直接查询heavy keeper，如果不存在，才调用add。
- 如果闲得无聊，研究一下merge 时候的metric 复用（而不是copy）问题，现在就先copy 吧。
- cube manager, 现在肯定还要继续用，得修改一下，把tag_hash_key 淘汰掉，还要换个地方。


## further
在火焰图上，明显能观察到get cell id的调用个数太多，说明是太多dummy 操作了。可能跟贺岚风商量一下，用batch incrby。

## metric
metric
 的变化也是一套，现在metric 没有正常reset，而是每次reset 都删，挺恶心的。关键是，exdata 里面放着什么metric 其实是fieldstat 定的，所以具体reset 多少个counter 之类是fieldstat 操作决定的。所以，我单纯用传入的reset 函数，简简单单处理一下，具体真的add 是时候发现，某个metric 已经不用了（这个事现在感知不到，也就认为不可能），也有办法解决。反正instance 的metric 是只能register 不能unregister 的。metric 始终增加就始终增加吧。

还有一个问题，merge 的时候总不能一直创建新的metric_id_map，所以只能是让exdata 获得一个fieldstat metric id map 的指针。注意一下，exdata_new 的arg 里有这个指针。

# 查询相关
- fieldstat_get_cells_used_by_metric 
目前应该是get metric in cell 了。这个函数只在测试中使用，用get metric in cell 替换掉吧

- fieldstat_get_metrics_used_by_cube
 在测试中少量使用。另外主要是exporter 里。每进入一个新的cube，就调一遍。每个step，到新cell 时，就对每个metric 都查找数据。
 当前，得到一个metric 的方法是：list exdata，然后其实就知道metric 都是什么了。但是注意，当前必须指定cell 才能得到metric。所以这个接口也不能用了。但是是好事，实际上，现在基于exdata 可以更简单的得到一个cell（也就是exporter 里的一次tags{} fields{} 输出）。

## 设计接口
fieldstat_get_cell_used_by_cube() // 已经有这个接口了
fieldstat_get_metric_in_cell() // 返回一系列metric id。

