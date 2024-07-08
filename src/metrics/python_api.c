#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

#include "histogram_encoder.h"
#include "base64/b64.h"
#include "st_hyperloglog.h"

// user must free the buf after use
void *fieldstat_histogram_base64_decode(char *buf)
{
    size_t buffer_size = strlen(buf);
    struct hdr_histogram *hdr = histogram_decode_from_b64(buf, buffer_size);

    return hdr;
}

long long fieldstat_histogram_value_at_percentile(void* h, double percentile)
{
    return hdr_value_at_percentile((const struct hdr_histogram *)h, percentile);
}

long long fieldstat_histogram_count_le_value(void* h, long long value)
{
	return hdr_count_le_value((const struct hdr_histogram *)h, value);
}

long long fieldstat_histogram_value_min(void* h)
{
    return hdr_min((const struct hdr_histogram *)h);
}

long long fieldstat_histogram_value_max(void* h)
{
    return hdr_max((const struct hdr_histogram *)h);
}

double fieldstat_histogram_value_mean(void* h)
{
    return hdr_mean((const struct hdr_histogram *)h);
}

double fieldstat_histogram_value_stddev(void* h)
{
    return hdr_stddev((const struct hdr_histogram *)h);
}

long long fieldstat_histogram_value_total_count(void *h)
{
    return ((struct hdr_histogram *)h)->total_count;
}

long long fieldstat_histogram_value_sum(void *h)
{
    struct hdr_iter iter;
	long long sum = 0;
	hdr_iter_recorded_init(&iter, (const struct hdr_histogram *)h);
	while (hdr_iter_next(&iter))
	{
		sum+=(long long)iter.value;
	}
	return sum;
}

void fieldstat_histogram_free(void *h)
{
    hdr_close((struct hdr_histogram *)h);
}


/* -------------------------------------------------------------------------- */
/*                                 hyperloglog                                */
/* -------------------------------------------------------------------------- */
void *hll_base64_decode(char *buf);
double fieldstat_hll_base64_to_count(char *buf)
{
    struct hyperloglog *hll = hll_base64_decode(buf);
    double count = hyperloglog_count(hll);
    hyperloglog_free(hll);
    return count;
}

#define BigLittleSwap32(A)        ((((uint32_t)(A) & 0xff000000) >> 24) | \
                                  (((uint32_t)(A) & 0x00ff0000) >> 8) | \
                                  (((uint32_t)(A) & 0x0000ff00) << 8) | \
                                  (((uint32_t)(A) & 0x000000ff) << 24))

void *hll_base64_decode(char *buf)
{
    size_t buffer_size = strlen(buf);
    size_t dec_size = 0;
    unsigned char *dec = b64_decode_ex(buf, buffer_size, &dec_size);

    unsigned char version;
    memcpy(&version, dec, sizeof(unsigned char));
    if (version != 1) {
        return NULL;
    }

    unsigned char precision;
    memcpy(&precision, dec + sizeof(unsigned char), sizeof(unsigned char));

    struct hyperloglog *hll_from_blob = hyperloglog_new(precision);

    int num_reg = NUM_REG(precision);
    int words = INT_CEIL(num_reg, REG_PER_WORD);
    size_t reg_size = words * sizeof(uint32_t);
    uint32_t *registers = (uint32_t *)malloc(reg_size);
    memcpy(registers, dec + 2 * sizeof(unsigned char), reg_size);
    for (int i = 0; i < words; i++) {
        registers[i] = BigLittleSwap32(registers[i]);
    }
    memcpy(hll_from_blob->registers, registers, reg_size);
    free(registers);

    free(dec);
    return hll_from_blob;
}

void fieldstat_hll_free(void *hll)
{
    hyperloglog_free((struct hyperloglog *)hll);
}

// cppcheck-suppress [constParameterPointer, unmatchedSuppression]
bool fieldstat_is_hll(char *buf)
{
    size_t dec_size = 0;
    unsigned char *dec = b64_decode_ex(buf, 4, &dec_size); // 4: base64 convert every 3 bytes to 4 bytes
    char cookie = *(char *)dec;
    free(dec);

    if (cookie == 1) { // hll version is 1
        return true;
    }

    // histogram cookie is also fixed, it's first byte is always 28, refer to V2_ENCODING_COOKIE (1c == 28)
    return false;
}
