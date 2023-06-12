#pragma once

#include <stddef.h>

#include "fieldstat.h"

#ifdef __cplusplus
extern "C" {
#endif

void tag_pair_unzip(const struct fieldstat_tag *tag, char **tag_key_out, char **tag_value_out);
int build_dynamic_metric_key(const struct fieldstat_tag tags[], size_t n_tags, size_t out_key_size, char *out_key);
int build_dynamic_metric_key_with_unzipped_tag(const char * const tag_key[], const char * const tag_value[], size_t n_tags, char *out_key, size_t out_key_size);


void kv_pair_write_value_int(struct fieldstat_tag *kv_pair, const char *key, long long value);
void kv_pair_write_value_double(struct fieldstat_tag *kv_pair, const char *key, double value);
void kv_pair_write_value_str(struct fieldstat_tag *kv_pair, const char *key, const char *value);
void kv_pair_clear(struct fieldstat_tag *kv_pair);

#ifdef __cplusplus
}
#endif
