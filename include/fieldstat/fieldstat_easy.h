#pragma once
#include <stddef.h>
#include "fieldstat.h" // for fieldstat_tag

#ifdef __cplusplus
extern "C"
{
#endif

struct fieldstat_easy;

/* 
 * new a fieldstat_easy instance.
    * @param max_thread_num: max thread number of this instance.
    * @param name: name of this instance. Will appear in output json. Can be NULL.
    * @param tags: tags of this instance. Will appear in output json. Can be NULL.
*/
struct fieldstat_easy *fieldstat_easy_new(int max_thread_num, const char *name, const struct fieldstat_tag *tags, size_t n_tag);
/*
    * free a fieldstat_easy instance.
*/
void fieldstat_easy_free(struct fieldstat_easy *fse);
/*
    * enable auto output. both data of accumulated and delta will be output.
    * @param output_path: output file path. Should be identical to the one in python config.
    * @param interval_second: output interval in second.
    * @return: 0 if success, -1 if failed to open file. -2 if the output is already enabled.
*/
int fieldstat_easy_enable_auto_output(struct fieldstat_easy *pthis, const char *output_path, int interval_second);
/*
    * @brief add a metric to the cube of cube_id. One metric may be associated with different cells.
    * @param metric_name: name of the metric. Cannot be NULL. Must be unique.
    * @return metric id>=0 if success. If failed, return FS_ERR_NULL_HANDLER, FS_ERR_INVALID_KEY(when metric_name is not unique in this cube).
    * For the error code, see fieldstat.h
*/
int fieldstat_easy_register_counter(struct fieldstat_easy *fse, const char *name);
/*
    * @brief refer to fieldstat_easy_register_counter.
    * @param lowest_trackable_value: the lowest value that can be tracked (distinguishable from 0) by the histogram. Must be >= 1.
    * @param highest_trackable_value: the highest value to be tracked by the histogram. Must be >= 2 * lowest_trackable_value.
    * @param significant_figures: the precision of the histogram. Must be in [1, 5].
    * @return metric id if success. If failed, return FS_ERR_NULL_HANDLER, FS_ERR_INVALID_KEY(when metric_name is not unique in this cube), or FS_ERR_INVALID_PARAM(if any of the 3 params are out of range)
    * For the error code, see fieldstat.h
*/
int fieldstat_easy_register_histogram(struct fieldstat_easy *fse, const char *name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures);
/*
    * Output the accumulated data to a json string.
    * @param buff: output buffer. User should free it after use.
*/
void fieldstat_easy_output(struct fieldstat_easy *fse, char **buff, size_t *buff_len);
void fieldstat_easy_output_array(struct fieldstat_easy *fse, char ***json_objects, size_t *n_object);
/*
    * @brief let the value of counter metric of cell_id increase by `increment`.
    * @param thread_id: thread id. Must be in [0, max_thread_num).
    * @param metric_id: metric id, previously returned by fieldstat_register_counter.
    * @param increment: increment of the counter metric. Can be negative.
    * @return FS_OK if success. FS_ERR_NULL_HANDLER, FS_ERR_INVALID_CUBE_ID, FS_ERR_INVALID_METRIC_ID if fail.
    * return -1 also when the thread_id is out of range.
*/
int fieldstat_easy_counter_incrby(struct fieldstat_easy *fse, int thread_id, int metric_id, const struct fieldstat_tag *tags, size_t n_tag, long long increment);
/* 
    * @brief Add a value to the histogram metric of cell_id. Histogram will record the distribution of the values.
             The value bigger than highest_trackable_value will be set to highest_trackable_value. The value less than lowest_trackable_value will be tried to record, and, if succeed, remains in the record as -inf(most of the time) or 0(if value == 0)
    * @param value: value of the histogram metric.
    * @return FS_OK if success. FS_ERR_NULL_HANDLER, FS_ERR_INVALID_CUBE_ID, FS_ERR_INVALID_METRIC_ID if fail.
    * return -1 also when the thread_id is out of range.
*/
int fieldstat_easy_histogram_record(struct fieldstat_easy *fse, int thread_id, int metric_id, const struct fieldstat_tag *tags, size_t n_tag, long long value);

#ifdef __cplusplus
}
#endif


/*
关于输出delta：
fs_write;
fs_accumulate;
fs_delta;

一个在write上register的metric，同样会随merge进入accumulate，delta的metric 名字后面希望跟一个delta，但是因为它是切换过来的，而且最终要加到accumulate上，所以。。。
delta的形成一定靠reset。

输出的时候，acc 和 delta的本来分别在两个json上，但是文件里只有一个json。
最好提供一个exportPair 的接口，由json聚合。如果输出两个json，我还要读出来再写一套，更恶心了。

另外一个方法，干脆给histogram算差值，这样是不是更干净一点？从算法上讲，histogram没道理不能算差。
然后就变成fs_write, fs_reading. 最终的输出是一个总量。
这个方法总体很好，但是需要我在exporter上保存metric数据，依赖metric。光这一条就可以直接否了方案。

这个easy自己维护一套metric呢？查询到的histogram和counter都merge，然后改名，再merge进来。。。这个改名就特别莫名其妙，输出的东西当然要输出自己来搞。
算了，调用两遍exporter得了，然后我把一个的加到另外一个上？这样的问题是我要用cjson来解，真的不要，太恶心了。。

还是搞个duo吧..?

不行，因为我真的没办法保证acc 和 delta的cell一样，delta的cell是调用者来搞的， 太容易顺序错了。于是counter history 那套是必须的。
不过传入delta而不是由exporter保留，这部分还是对的。如果还是想要这个history，那么exporter就还得是有状态的，不然总不能每次都拿delta重建一份。（感觉就重建一份吧）
算了，果然只能这样了。。

那么干脆每次都新来一个acc呢？我记录acc，合成acc，delta咋办？也不对。
得得得得，算delta这个逃不掉了。

提供一个fieldstat减法怎么样？首先hll不支持减。另外我不希望动fs4的接口。


 ---------------------------------- plan1 --------------------------------- 
改动最小，性能影响最大，所有的还都得搞两份

instance = new()
register_metric(i,"A");
intance_delt
register_metric(i_delta, "A_delta");
counter_incyby(instance, id, 1);
counter_incyby(instance_delta, id, 1);


dst = merge(instance);
dst_delta = merge(instance_delta);

merge(dst, dst_delta)


 ---------------------------------- plan2 --------------------------------- 
 // 最莫名其妙，而且merge 改起来容易出问题
instance_delta = new()
incyby

instance = merge_and_add_delta(instance_delta)  // 新增接口


---------------------------------- plan3 --------------------------------- 
// 仅仅需要修改exporter，但是编码量比较大，选择这种。export只有一遍，而merge是每个线程一遍，还是慢export吧
json_total = exporter_export_duo(acc, delta); // 新增接口


 ---------------------------------- plan4 --------------------------------- 
enable-delta()
histogram_decode()
histogram_substract()
*/