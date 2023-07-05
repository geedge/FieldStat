#pragma once
#include <stdio.h>
#ifdef __cplusplus
extern "C"
{
#endif

#define EXPORT_VER 1 // increase by 1 when the serialization format is changed.
#define MAX_TAG_KEY_LENGTH 32 // the max length of tag key, including '\0'
#define MAX_TAG_STRING_VAL_LENGTH 32 // the max length of tag value, including '\0'

enum field_type
{
	FIELD_TYPE_COUNTER,
	FIELD_TYPE_TOPK,
	FIELD_TYPE_HLL,
	FIELD_TYPE_HISTOGRAM,
    // add more type here(`MUST` in the rear of the enum)
};

enum fs_tag_type
{
	TAG_INTEGER,
	TAG_DOUBLE,
	TAG_CSTRING,
    TAG_END
};

enum sampling_method {
    SAMPLING_MODE_COMPREHENSIVE,
    SAMPLING_MODE_TOPK,
};


struct fieldstat_tag_list
{
    struct fieldstat_tag *tag;
    size_t n_tag;
};
struct fieldstat;
struct fieldstat *fieldstat_new();
void fieldstat_free(struct fieldstat *instance);

//TODO: 库vs plugin（查资料）


// TODO: merge 方法会变，如果是gauge模式，我应该是用max 来merge。
// 这是一个输出时的概念，放到exporter里

// register metric.
// 数量很少，所以动态数组要再小一点（现在是256，改成20）
int fieldstat_register_counter(struct fieldstat *instance, const char *field_name);
int fieldstat_register_hll(struct fieldstat *instance, const char *field_name, unsigned char precision);
int fieldstat_register_histogram(struct fieldstat *instance, const char *field_name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures);

register counter: 创建的是metric_info，对应当前的field

cell -> find_metric(metric_id) 
if not find, call metric_spawn, write a metric to the cell.

cell 还是原来的cell，



//有可能没有dimension，单纯在group里拿一个metric

//implicit tag key, rule_id=101, chart_id=99    
int fieldstat_register_cube(instance, struct fieldstat_tag *implicit_dimension, size_t n_tag, 
                     enum sampling_mode mode, size_t max_n_cell);
//return a cell id >= 0, if success; otherwise, return -1.
int fieldstat_cube_add(instance, cube_id,  struct fieldstat_tag *dimensions, size_t n_tag, long long count);

// TODO：field这个概念没了，是metric，metric + tag 找到cell
int fieldstat_counter_incrby(struct fieldstat *instance, int metric_id, int cell_id, long long increment);
int fieldstat_hll_add(struct fieldstat *instance, int field_id, const char *key, size_t key_len, int tag_id);
int fieldstat_histogram_record(struct fieldstat *instance, int field_id, long long value, int tag_id);

void fieldstat_reset(struct fieldstat *instance); //所有register的不变，所有 tag 清除， field 不删除，但是field 内的数据删除
unsigned long fieldstat_get_version(const struct fieldstat *instance);


/* -------------------------------------------------------------------------- */
/*            used for upper instance(promethus, line protocol...)            */
/* -------------------------------------------------------------------------- */

// 一个线程对一个fieldstat instance。
// 线程读。。。
// line_protocol_exporter()
// read_blob...
// set gauge metric
// 这个exporter ，在3.0里，应该是一个instance只写到一个地方，现在应该要输出到好几个地方去（或者创建几个exporter？）
struct new_fieldstat_instance = deserialize();
exporter.merge_with(new_fieldstat_instance);


struct fieldstat *fieldstat_deserialize(const char *blob, size_t blob_size);
int fieldstat_merge_with_blob(struct fieldstat *instance, const char *blob, size_t blob_size);
int fieldstat_serialize(const struct fieldstat *instance, char **blob_out, size_t *blob_size_out);


/* -------------------------------------------------------------------------- */
/*                                    query                                   */
/* -------------------------------------------------------------------------- */
int fieldstat_get_max_chart_id(const struct fieldstat *instance);
void fieldstat_get_chart_info(const struct fieldstat *instance, int group_id,
    struct fieldstat_tag_list **lists, size_t *n_list,
    char ***metric_name_out, size_t *n_metric_out, size_t *primary_metric_id_out);



long long fieldstat_histogram_value_at_percentile(const struct fieldstat *instance, int field_id, const struct fieldstat_tag_list *tag_list, double percentile);
long long fieldstat_counter_get(const struct fieldstat *instance, int field_id, const struct fieldstat_tag_list *tag_list);
double fieldstat_hll_get(const struct fieldstat *instance, int field_id, const struct fieldstat_tag_list *tag_list);
void fieldstat_get_tag_lists(const struct fieldstat *instance, int field_id, struct fieldstat_tag_list **lists, size_t *n_list);
// int fieldstat_topk_query(const struct fieldstat *instance, int id, const struct fieldstat_tag_list *tag_list, // infos
//     struct fieldstat_tag **key_out, size_t *n_key_out, // row info
//     char ***metric_name_out, size_t *n_metric_out, size_t *primary_metric_id_out, // column info
//     unsigned int ***metric_value_out); // values
int fieldstat_topk_query(const struct fieldstat *instance, int id, const struct fieldstat_tag_list *tag_list, // infos
    char ***keys_out, size_t *n_key_out, // row info
    char ***metric_name_out, size_t *n_metric_out, size_t *primary_metric_id_out, // column info
    unsigned int ***metric_value_out); // values

void fieldstat_get_serialized_blob(const struct fieldstat *instance, int field_id, const struct fieldstat_tag_list *tag_list, char **blob, size_t *blob_size);


const char *fieldstat_get_field_name(const struct fieldstat *instance, int field_id);
enum field_type fieldstat_get_field_type(const struct fieldstat *instance, int field_id);


// TODO: 通过name查询id


#ifdef __cplusplus
}
#endif