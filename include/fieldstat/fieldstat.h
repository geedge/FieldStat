#pragma once
#include <stdio.h>
#ifdef __cplusplus
extern "C"
{
#endif

enum field_type
{
	FIELD_TYPE_COUNTER,
	FIELD_TYPE_TOPK,
	FIELD_TYPE_HLL,
	FIELD_TYPE_HISTOGRAM
};
enum fs_tag_type
{
	TAG_INTEGER,
	TAG_DOUBLE,
	TAG_CSTRING
};
struct fieldstat_tag{
	const char *key;
	enum fs_tag_type type;
	union{
		long long value_int;
		double value_double;
		const char *value_str;
	};
};

struct fieldstat;
struct fieldstat *fieldstat_new(const char *symbol);
int fieldstat_register_counter(struct fieldstat *instance, const char *field_name);
int fieldstat_register_topk(struct fieldstat *instance, const char *field_name, int K);
int fieldstat_register_hll(struct fieldstat *instance, const char *field_name);
int fieldstat_register_histogram(struct fieldstat *instance, const char *field_name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures);

int fieldstat_counter_incrby(struct fieldstat *instance, int field_id, long long increment, const struct fieldstat_tag tags[], size_t n_tag);
int fieldstat_topk_add(struct fieldstat *instance, int field_id, const char *key, size_t key_len, const struct fieldstat_tag tags[], size_t n_tag);
int fieldstat_hll_add(struct fieldstat *instance, int field_id, const char *key, size_t key_len, const struct fieldstat_tag tags[], size_t n_tag);
int fieldstat_histogram_record(struct fieldstat *instance, int field_id, long long value, long long expected, const struct fieldstat_tag tags[], size_t n_tag);

enum field_type
{
	FIELD_TYPE_COUNTER,
	FIELD_TYPE_GAUGE,
	FILED_TYPE_HISTOGRAM,
	FIELD_TYPE_SUMMARY
};

enum field_type fieldstat_get_type(struct fieldstat *instance, int field_id);
long long fieldstat_histogram_value_at_percentile(struct fieldstat *instance, int field_id);

#ifdef __cplusplus
}
#endif