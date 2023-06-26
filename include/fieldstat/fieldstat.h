#pragma once
#include <stdio.h>
#ifdef __cplusplus
extern "C"
{
#endif

#define EXPORT_VER 1 // increase by 1 when the serialization format is changed.

enum field_type
{
	FIELD_TYPE_COUNTER,
	FIELD_TYPE_TOPK,
	FIELD_TYPE_CARDINALITY,
	FIELD_TYPE_HISTOGRAM,
    // add more type here(`MUST` in the rear of the enum)
};

enum fs_tag_type
{
	TAG_INTEGER,
	TAG_DOUBLE,
	TAG_CSTRING
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
int fieldstat_register_topk(struct fieldstat *instance, const char *field_name, int K);
int fieldstat_register_cardinality(struct fieldstat *instance, const char *field_name, unsigned char precision);
int fieldstat_register_histogram(struct fieldstat *instance, const char *field_name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures);

// operations on different type of metrics. The creation of cell is dynamic.
// tag: notice that: "TAG_KEY": "123" and "TAG_KEY": 123 are map to the same cell. 
// "TAG_KEY": 123.0 is also expected to map to the same cell, but not necessary. Just avoid declare tags in this way.
// key and value_str are both c-style string, which means that you need to add '\0' at the end of the string.
// tag grouping is ordered, which means that {"TAG_KEY": "123", "TAG_KEY2": "456"} and {"TAG_KEY2": "456", "TAG_KEY": "123"} are map to different cell.
// It is recommended that you use the same order when you declare tags.
// @return 0 if success, -1 if failed.
int fieldstat_counter_incrby(struct fieldstat *instance, int field_id, long long increment, const struct fieldstat_tag tags[], size_t n_tag);
int fieldstat_topk_add(struct fieldstat *instance, int field_id, const char *key, size_t key_len, const struct fieldstat_tag tags[], size_t n_tag);

int fieldstat_cardinality_add(struct fieldstat *instance, int field_id, const char *key, size_t key_len, const struct fieldstat_tag tags[], size_t n_tag);
int fieldstat_histogram_record(struct fieldstat *instance, int field_id, long long value, const struct fieldstat_tag tags[], size_t n_tag);
// TODO: 特别大的值，则使用corrected value 
struct fieldstat *fieldstat_deserialize(const char *blob, size_t blob_size);
int fieldstat_merge_with_blob(struct fieldstat *instance, const char *blob, size_t blob_size);
int fieldstat_serialize(const struct fieldstat *instance, char **blob_out, size_t *blob_size_out);

/* -------------------------------------------------------------------------- */
/*                                    query                                   */
/* -------------------------------------------------------------------------- */
long long fieldstat_histogram_value_at_percentile(struct fieldstat *instance, int field_id, const struct fieldstat_tag_list *tag_list, double percentile);
long long fieldstat_counter_get(const struct fieldstat *instance, int field_id, const struct fieldstat_tag_list *tag_list);
double fieldstat_cardinality_get(const struct fieldstat *instance, int field_id, const struct fieldstat_tag_list *tag_list);
void fieldstat_get_tag_lists(const struct fieldstat *instance, int field_id, struct fieldstat_tag_list **lists, size_t *n_list);
void fieldstat_topk_query(const struct fieldstat *instance, int id, const struct fieldstat_tag_list *tag_list , char ***keys_out, long long **count_out, size_t *n_key_out);
void fieldstat_get_serialized_blob(const struct fieldstat *instance, int field_id, const struct fieldstat_tag_list *tag_list, char **blob, size_t *blob_size);

void tag_list_clear(struct fieldstat_tag_list *tag_list);

int fieldstat_get_max_field_id(const struct fieldstat *instance);
const char *fieldstat_get_field_name(const struct fieldstat *instance, int field_id);
enum field_type fieldstat_get_field_type(const struct fieldstat *instance, int field_id);

#ifdef __cplusplus
}
#endif