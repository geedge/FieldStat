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

int fieldstat_register_cube(struct fieldstat *instance, struct fieldstat_tag *shared_tags, size_t n_tag, enum sampling_mode mode, size_t max_n_cell);
//return a cell id >= 0, if success; otherwise, return -1. Fail if: mode is (SAMPLING_MODE_COMPREHENSIVE) and cell number >= (max_n_cell) ; or SAMPLING_MODE_TOPK and cell already exists.
int fieldstat_cube_add(struct fieldstat *instance, int cube_id, const struct fieldstat_tag *tags, size_t n_tag, unsigned int count);

int fieldstat_counter_incrby(struct fieldstat *instance, int metric_id, int cube_id, int cell_id, long long increment);

void fieldstat_reset(struct fieldstat *instance); //所有register的不变，所有 tag 清除， field 不删除，但是field 内的数据删除
unsigned long fieldstat_get_version(const struct fieldstat *instance); // return the time when the instance is created.



int fieldstat_merge(struct fieldstat *instance, struct fieldstat *src);
// todo: merge 的伪代码
// for cube（merge topk 的情况）
//     struct cell_manager merged_tags = dest.
//     merged_tags.push(src.cube.map.pop().get_tag()) for every cube in cube_arr
//      merged_tags.push(dest.cube.map.pop().get_tag()) for every cube in cube_arr
// note: 这部分实际上放在了metric.c 里
// metric需要支持的操作：
//          修改id： 修改dest[tag].id 到merged_cell[tag.id]
//          merge： 给定一个dest metric，一个src metric，并给出dest id和src id
//          copy:   给定一个src metric，一个dest metric，并给出src id
// dest cube manager   |   merged cube manager  | src cube manager    |  操作
// 有                            有                  有                   dest[tag] = src[tag] + dest[tag]，修改id
// 有                            有                  无                     修改id
// 有                            无                  有                     删掉dest[tag]
// 有                            无                  无                     删掉dest[tag]
// 无                            有                  有                     创建dest[tag] = src[tag]， 修改id
// 无                            有                  无                     不可能
// 无                            无                  有                      无操作
// 无                            无                  无                     无操作


// merge comprehensive 的情况：
// 构建increment tag，每个src 里有，但是dest里没有的，加入到increment里。直到dest放满。
// 对于每个increment tag， dest[tag] = src[tag]，修改id为现在dest cube manager 里的id

struct fieldstat *fieldstat_deserialize(const char *blob, size_t blob_size);
int fieldstat_serialize(const struct fieldstat *instance, char **blob_out, size_t *blob_size_out);


/* -------------------------------------------------------------------------- */
/*                                    query                                   */
/* -------------------------------------------------------------------------- */
int fieldstat_get_max_cube_id(const struct fieldstat *instance);
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

#ifdef __cplusplus
}
#endif