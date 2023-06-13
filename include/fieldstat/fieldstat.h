#pragma once
#include <stdio.h>
#ifdef __cplusplus
extern "C"
{
#endif

#define INSTANCE_NAME_MAX_LEN 32
#define MAX_TAG_NUM	     	32
#define CELL_MAX_NUMBER 1024

enum field_type
{
	FIELD_TYPE_COUNTER,
	FIELD_TYPE_TOPK,
	FIELD_TYPE_CARDINALITY,
	FIELD_TYPE_HISTOGRAM,
    // gauge
    // summery
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
struct fieldstat *fieldstat_new(const char *symbol);
void fieldstat_free(struct fieldstat *instance);
int fieldstat_register_counter(struct fieldstat *instance, const char *field_name);
int fieldstat_register_topk(struct fieldstat *instance, const char *field_name, int K);
int fieldstat_register_hll(struct fieldstat *instance, const char *field_name);
int fieldstat_register_histogram(struct fieldstat *instance, const char *field_name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures);

int fieldstat_counter_incrby(struct fieldstat *instance, int field_id, long long increment, const struct fieldstat_tag tags[], size_t n_tag);
int fieldstat_topk_add(struct fieldstat *instance, int field_id, const char *key, size_t key_len, const struct fieldstat_tag tags[], size_t n_tag);
int fieldstat_hll_add(struct fieldstat *instance, int field_id, const char *key, size_t key_len, const struct fieldstat_tag tags[], size_t n_tag);
int fieldstat_histogram_record(struct fieldstat *instance, int field_id, long long value, long long expected, const struct fieldstat_tag tags[], size_t n_tag);



/* -------------------------------------------------------------------------- */
/*                                    query                                   */
/* -------------------------------------------------------------------------- */
long long fieldstat_histogram_value_at_percentile(struct fieldstat *instance, int field_id, const struct fieldstat_tag_list *tag_list, double percentile);
long long fieldstat_counter_get(struct fieldstat *instance, int field_id, const struct fieldstat_tag_list *tag_list);
int fieldstat_get_tag_lists(struct fieldstat *instance, int field_id, struct fieldstat_tag_list *lists, size_t n_list);
void fieldstat_topk_query(const struct fieldstat *instance, int id, const struct fieldstat_tag_list *tag_list , char ***keys_out, long long **count_out, size_t *n_key_out);
int fieldstat_query(struct fieldstat *instance, int field_id);

#ifdef __cplusplus
}
#endif