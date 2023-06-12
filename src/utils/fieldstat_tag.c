#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "fieldstat_tag.h"


void tag_pair_unzip(const struct fieldstat_tag *tag, char **tag_key_out, char **tag_value_out)
{
    *tag_key_out = strdup(tag->key);
    char str_double[sizeof(double) + 1];
    char str_longlong[sizeof(long long) + 1];
    switch(tag->type)
    {
        case TAG_INTEGER:
            memset(str_longlong, 0, sizeof(str_longlong));
            snprintf(str_longlong, sizeof(str_longlong), "%lld", tag->value_int);
            *tag_value_out = strdup((const char *)str_longlong);
            break;
        case TAG_DOUBLE:
            memset(str_double, 0, sizeof(str_double));
            snprintf(str_double, sizeof(str_double), "%lf", tag->value_double);
            *tag_value_out = strdup((const char *)str_double);
            break;
        case TAG_CSTRING:
            *tag_value_out = strdup(tag->value_str);
            break;
        default:
            assert(0);
            break;
    }
}

int build_dynamic_metric_key(const struct fieldstat_tag tags[], size_t n_tags, size_t out_key_size, char *out_key)
{
	int i = 0;
	int used_len = 0;
	struct fieldstat_tag *tag = NULL;

	for(i = 0; i < (int)n_tags; i++)
	{
		tag = (struct fieldstat_tag *)&tags[i];
		switch(tag->type)
		{
			case 0:
				used_len += snprintf(out_key + used_len, out_key_size - used_len, "%s%lld", tag->key, tag->value_int);
				break;
			case 1:
				used_len += snprintf(out_key + used_len, out_key_size - used_len, "%s%lf", tag->key, tag->value_double);
				break;
			case 2:
				used_len += snprintf(out_key + used_len, out_key_size - used_len, "%s%s", tag->key, tag->value_str);
				break;
			default:
				assert(0);
				break;
		}
	}
	return  used_len;
}

int build_dynamic_metric_key_with_unzipped_tag(const char * const tag_key[], const char * const tag_value[], size_t n_tags, char *out_key, size_t out_key_size)
{
    int used_len = 0;

    for(size_t i = 0; i < n_tags; i++)
    {
        int ret = snprintf(out_key + used_len, out_key_size - used_len, "%s%s", tag_key[i], tag_value[i]);
        if (ret < 0) {
            printf("ERR: snprintf failed in build_dynamic_metric_key_with_unzipped_tag\n");
            return -1;
        }

        used_len += ret;
    }
    return  used_len;
}

void kv_pair_write_value_int(struct fieldstat_tag *kv_pair, const char *key, long long value)
{
    if (key) { // key is optional
        kv_pair->key = strdup(key);
    }
    kv_pair->type = TAG_INTEGER;
    kv_pair->value_int = value;
}

void kv_pair_write_value_double(struct fieldstat_tag *kv_pair, const char *key, double value)
{
    if (key) { // key is optional
        kv_pair->key = strdup(key);
    }

    kv_pair->type = TAG_DOUBLE;
    kv_pair->value_double = value;
}

void kv_pair_write_value_str(struct fieldstat_tag *kv_pair, const char *key, const char *value)
{
    if (key) { // key is optional
        kv_pair->key = strdup(key);
    }
    kv_pair->type = TAG_CSTRING;
    kv_pair->value_str = strdup(value);
}

void kv_pair_clear(struct fieldstat_tag *kv_pair)
{
    if (kv_pair->key) {
        free(kv_pair->key);
        kv_pair->key = NULL;
    }
    if (kv_pair->type == TAG_CSTRING && kv_pair->value_str) {
        free(kv_pair->value_str);
        kv_pair->value_str = NULL;
    }
}