#pragma once
#include <stdio.h>
#ifdef __cplusplus
extern "C"
{
#endif

#define EXPORT_VER 1 // increase by 1 when the serialization format is changed.

enum metric_type
{
	metric_type_COUNTER,
	metric_type_HLL,
	metric_type_HISTOGRAM,
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

int fieldstat_register_counter(struct fieldstat *instance, const char *field_name);
int fieldstat_register_hll(struct fieldstat *instance, const char *field_name, unsigned char precision);
int fieldstat_register_histogram(struct fieldstat *instance, const char *field_name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures);

int fieldstat_register_cube(struct fieldstat *instance, struct fieldstat_tag *shared_dimensions, size_t n_dimension, enum sampling_mode mode, size_t max_n_cell);
//return a cell id >= 0, if success; otherwise, return -1. Fail if: mode is (SAMPLING_MODE_COMPREHENSIVE) and cell number >= (max_n_cell) ; or SAMPLING_MODE_TOPK and cell already exists.
int fieldstat_cube_add(struct fieldstat *instance, int cube_id,  struct fieldstat_tag *dimensions, size_t n_tag, long long count);

int fieldstat_counter_incrby(struct fieldstat *instance, int metric_id, int cube_id, int cell_id, long long increment);

void fieldstat_reset(struct fieldstat *instance); //所有register的不变，所有 tag 清除， field 不删除，但是field 内的数据删除
unsigned long fieldstat_get_timestamp(const struct fieldstat *instance); // return the time when the instance is created.

struct fieldstat *fieldstat_deserialize(const char *blob, size_t blob_size);
int fieldstat_merge_with_blob(struct fieldstat *instance, const char *blob, size_t blob_size);
int fieldstat_serialize(const struct fieldstat *instance, char **blob_out, size_t *blob_size_out);


/* -------------------------------------------------------------------------- */
/*                                    query                                   */
/* -------------------------------------------------------------------------- */
int fieldstat_get_max_cube_id(const struct fieldstat *instance);
void fieldstat_get_cube_info(const struct fieldstat *instance, int group_id,
    int **cell_id, size_t *n_cell_id,
    enum sampling_mode *mode);

void fieldstat_get_cell_info(const struct fieldstat *instance, int group_id, int cell_id,
    struct fieldstat_tag **dimensions, size_t *n_dimension,
    enum metric_type **metric_types, size_t *n_field);

long long fieldstat_histogram_value_at_percentile(const struct fieldstat *instance, int metric_id, int cell_id, double percentile);
long long fieldstat_counter_get(const struct fieldstat *instance, int metric_id, int cell_id);
double fieldstat_hll_get(const struct fieldstat *instance, int metric_id, int cell_id);
void fieldstat_get_serialized_blob(const struct fieldstat *instance, int metric_id, int cell_id, char **blob, size_t *blob_size);

const char *fieldstat_get_metric_name(const struct fieldstat *instance, int metric_id);
enum metric_type fieldstat_get_metric_type(const struct fieldstat *instance, int metric_id);

#ifdef __cplusplus
}
#endif