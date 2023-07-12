#pragma once
#include <stdio.h>
#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <stdbool.h>

#define EXPORT_VER 1 // increase by 1 when the serialization format is changed.

enum metric_type
{
	METRIC_TYPE_COUNTER,
	METRIC_TYPE_HLL,
	METRIC_TYPE_HISTOGRAM,
};

enum fs_tag_type
{
	TAG_INTEGER,
	TAG_DOUBLE,
	TAG_CSTRING,
};

enum sampling_mode {
    SAMPLING_MODE_COMPREHENSIVE,
    SAMPLING_MODE_TOPK,
};

struct fieldstat_tag {
	const char *key;
    enum fs_tag_type type;
	union{
		long long value_int;
		double value_double;
		const char *value_str;
	};
};

struct fieldstat_tag_list
{
    struct fieldstat_tag *tag;
    size_t n_tag;
};

struct fieldstat;
struct fieldstat *fieldstat_new();
void fieldstat_free(struct fieldstat *instance);

int fieldstat_register_counter(struct fieldstat *instance, int cube_id, const char *field_name, bool is_gauge);
int fieldstat_register_hll(struct fieldstat *instance, int cube_id, const char *field_name, unsigned char precision);
int fieldstat_register_histogram(struct fieldstat *instance, int cube_id, const char *field_name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures);

//  fieldstat_register_cube(instance, shared_tags, n_tag, mode, max_n_cell)  创建一个用tag id 做key的topk
//  fieldstat_cube_add(instance, cube_id, tags, n_tag, count)  在topk 1里map一个，如果不在top里，返回-1
// fieldstat_counter_incrby(instance, metric_id, cube_id, cell_id, increment)  相当于topk_add(cell_id, increment

int fieldstat_register_cube(struct fieldstat *instance, const struct fieldstat_tag *shared_tags, size_t n_tag, enum sampling_mode mode, size_t max_n_cell);
//return a cell id >= 0, if success; otherwise, return -1. Fail if: mode is (SAMPLING_MODE_COMPREHENSIVE) and cell number >= (max_n_cell) ; or SAMPLING_MODE_TOPK and cell already exists.
int fieldstat_cube_add(struct fieldstat *instance, int cube_id, const struct fieldstat_tag *tags, size_t n_tag, unsigned int count);
// 这个接口对于topk的情况，变成用哈希算出来一个FP，然后这个FP
// 为了防止hash collision,设计sketch结构，如果一个tags命中几个sketch，取出sketch中的id。如果不对，就是在sketch上垒一个。

int fieldstat_counter_incrby(struct fieldstat *instance, int metric_id, int cube_id, int cell_id, long long increment);

void fieldstat_reset(struct fieldstat *instance); //所有register的不变，所有 tag 清除， field 不删除，但是field 内的数据删除
unsigned long fieldstat_get_version(const struct fieldstat *instance); // return the time when the instance is created.

// fixme: topk 会丢，后来的统计就上不来了。确认一下


int fieldstat_merge(struct fieldstat *instance, struct fieldstat *src);
struct fieldstat *fieldstat_deserialize(const char *blob, size_t blob_size);
int fieldstat_serialize(const struct fieldstat *instance, char **blob_out, size_t *blob_size_out);


/* -------------------------------------------------------------------------- */
/*                                    query                                   */
/* -------------------------------------------------------------------------- */
void fieldstat_get_cubes(const struct fieldstat *instance, int **cube_ids, size_t *n_cube);
int fieldstat_get_max_metric_id(const struct fieldstat *instance, int cube_id);
void fieldstat_get_cells(const struct fieldstat *instance, int cube_id, int metric_id,
    int **cell_ids, struct fieldstat_tag_list **tag_list, size_t *n_cell);

// long long fieldstat_histogram_value_at_percentile(const struct fieldstat *instance, int metric_id, int cell_id, double percentile);
long long fieldstat_counter_get(const struct fieldstat *instance, int cube_id, int metric_id, int cell_id);
// double fieldstat_hll_get(const struct fieldstat *instance, int metric_id, int cell_id);
// void fieldstat_get_serialized_blob(const struct fieldstat *instance, int metric_id, int cell_id, char **blob, size_t *blob_size);

const char *fieldstat_get_metric_name(const struct fieldstat *instance, int cube_id, int metric_id);
enum metric_type fieldstat_get_metric_type(const struct fieldstat *instance, int cube_id, int metric_id);

void fieldstat_tag_list_arr_free(struct fieldstat_tag_list *tag_list, size_t n_cell);

// todo: 增加cube 的free，配套一个新的version接口
// todo: 这个操作影响挺大的，比如新申请的cube，如果之前有的话，肯定不能再来一个，所以要有shared tag 到cube id 的匹配。比如append 操作的时候，不能跟n_cube 走，而是先找一个空。比如n_cube 要重命名成last_cube_id
// todo: 加注释，返回值是什么等等
// todo: fieldstat_cube_add 对于topk 写一个新的接口
// todo: incrby 时加一套校验，并且区别错误返回值

#ifdef __cplusplus
}
#endif