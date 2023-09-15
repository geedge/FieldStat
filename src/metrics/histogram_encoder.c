
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>

#include "hdr/hdr_encoding.h"
#include "hdr/hdr_tests.h"
#include "hdr/hdr_malloc.h"
#include "base64/b64.h"
#include "histogram_encoder.h"

// #define V0EncodingCookieBase 0x1c849308
static const uint32_t V2_ENCODING_COOKIE = 0x1c849303;
#define SIZEOF_ENCODING_FLYWEIGHT_V1 (sizeof(encoding_flyweight_v1_t) - sizeof(uint8_t))
#define MAX_BYTES_LEB128 9

typedef struct /*__attribute__((__packed__))*/
{
    uint32_t cookie;
    int32_t payload_len;
    int32_t normalizing_index_offset;
    int32_t significant_figures;
    int64_t lowest_discernible_value;
    int64_t highest_trackable_value;
    uint64_t conversion_ratio_bits;
    uint8_t counts[1];
} encoding_flyweight_v1_t;

struct simple_encoded/*__attribute__((__packed__))*/
{
    int32_t significant_figures;
    int64_t lowest_discernible_value;
    int64_t highest_trackable_value;
    uint8_t counts[0];
};

union uint64_dbl_cvt
{
    uint64_t l;
    double d;
};

static uint64_t double_to_int64_bits(double d)
{
    union uint64_dbl_cvt x;

    x.d = d;
    return x.l;
}

int hdr_encode_v2_buf(struct hdr_histogram* h, uint8_t** encoded_buf, size_t* blob_len)
{
    encoding_flyweight_v1_t* encoded = NULL;
    int i;
    int result = 0;
    int data_index = 0;
    int32_t payload_len;
    size_t encoded_size;

    int32_t len_to_max = counts_index_for(h, h->max_value) + 1;
    int32_t counts_limit = len_to_max < h->counts_len ? len_to_max : h->counts_len;

    const size_t encoded_len = SIZEOF_ENCODING_FLYWEIGHT_V1 + MAX_BYTES_LEB128 * (size_t) counts_limit;
    if ((encoded = (encoding_flyweight_v1_t*) hdr_calloc(encoded_len, sizeof(uint8_t))) == NULL)
    {
        hdr_free(encoded);
    }

    for (i = 0; i < counts_limit;)
    {
        int64_t value = h->counts[i];
        i++;

        if (value == 0)
        {
            int32_t zeros = 1;

            while (i < counts_limit && 0 == h->counts[i])
            {
                zeros++;
                i++;
            }

            data_index += zig_zag_encode_i64(&encoded->counts[data_index], -zeros);
        }
        else
        {
            data_index += zig_zag_encode_i64(&encoded->counts[data_index], value);
        }
    }

    payload_len = data_index;
    encoded_size = SIZEOF_ENCODING_FLYWEIGHT_V1 + data_index;

    encoded->cookie                   = htobe32(V2_ENCODING_COOKIE | 0x10U);
    encoded->payload_len              = htobe32(payload_len);
    encoded->normalizing_index_offset = htobe32(h->normalizing_index_offset);
    encoded->significant_figures      = htobe32(h->significant_figures);
    encoded->lowest_discernible_value   = htobe64(h->lowest_discernible_value);
    encoded->highest_trackable_value  = htobe64(h->highest_trackable_value);
    encoded->conversion_ratio_bits    = htobe64(double_to_int64_bits(h->conversion_ratio));

    *encoded_buf = (uint8_t*) encoded;
    *blob_len = encoded_size;

    return result;
}

void histogram_encode_into_b64(struct hdr_histogram *hdr, char **buffer, size_t *buffer_size)
{
    uint8_t *encoded_buf;
    size_t encoded_len;
    hdr_encode_v2_buf(hdr, &encoded_buf, &encoded_len);

    char *enc = b64_encode(encoded_buf, encoded_len);
    free(encoded_buf);
    *buffer_size = strlen(enc);
    *buffer = enc;
}

static uint32_t get_cookie_base(uint32_t cookie)
{
    return (cookie & ~0xf0U);
}

static double int64_bits_to_double(int64_t i)
{
    union uint64_dbl_cvt x;

    x.l = (uint64_t) i;
    return x.d;
}

static int hdr_decode_v2_buf(const unsigned char* buffer, size_t length, struct hdr_histogram** histogram)
{
    struct hdr_histogram* h = NULL;
    int result = 0;
    int rc = 0;
    const uint8_t* counts_array = NULL;
    uint32_t encoding_cookie;
    int32_t counts_limit, significant_figures;
    int64_t lowest_discernible_value, highest_trackable_value;

    encoding_flyweight_v1_t encoding_flyweight;
    memcpy(&encoding_flyweight, buffer, SIZEOF_ENCODING_FLYWEIGHT_V1);

    encoding_cookie = get_cookie_base(be32toh(encoding_flyweight.cookie));
    if (V2_ENCODING_COOKIE != encoding_cookie) {
        return -1;
    }

    counts_limit = be32toh(encoding_flyweight.payload_len);
    lowest_discernible_value = be64toh(encoding_flyweight.lowest_discernible_value);
    highest_trackable_value = be64toh(encoding_flyweight.highest_trackable_value);
    significant_figures = be32toh(encoding_flyweight.significant_figures);

    rc = hdr_init(lowest_discernible_value, highest_trackable_value, significant_figures, &h);
    if (rc) {
        return -1;
    }

    encoding_flyweight_v1_t *buffer_aligner = (encoding_flyweight_v1_t *)buffer;

    counts_array = buffer_aligner->counts;

    rc = apply_to_counts_zz(h, counts_array, counts_limit);
    if (rc) {
        free(h);
        return -1;
    }

    h->normalizing_index_offset = be32toh(encoding_flyweight.normalizing_index_offset);
    h->conversion_ratio = int64_bits_to_double(be64toh(encoding_flyweight.conversion_ratio_bits));
    hdr_reset_internal_counters(h);

    *histogram = h;
    return result;
}

struct hdr_histogram *histogram_decode_from_b64(const char *buffer, size_t buffer_size)
{
    size_t dec_size = 0;
    unsigned char *dec = b64_decode_ex(buffer, buffer_size, &dec_size);

    struct hdr_histogram *hdr = NULL;
    int ret = hdr_decode_v2_buf(dec, dec_size, &hdr);
    free(dec);

    if (ret != 0) {
        return NULL;
    }

    return hdr;
}

void histogram_encode_into_blob(const struct hdr_histogram *hdr, char **blob, size_t *blob_len)
{
    struct simple_encoded *encoded = NULL;
    size_t counts_size = hdr->counts_len * sizeof(int64_t);
    const size_t encoded_len = sizeof(struct simple_encoded) + counts_size;

    if ((encoded = (struct simple_encoded *) hdr_calloc(encoded_len, sizeof(uint8_t))) == NULL) {
        hdr_free(encoded);
    }

    memcpy(encoded->counts, hdr->counts, counts_size);
    encoded->significant_figures      = hdr->significant_figures;
    encoded->lowest_discernible_value   = hdr->lowest_discernible_value;
    encoded->highest_trackable_value  = hdr->highest_trackable_value;

    *blob = (char *)encoded;
    *blob_len = encoded_len;
}

struct hdr_histogram *histogram_decode_from_blob(const char *blob, size_t blob_len)
{
    const struct simple_encoded *encoded = (const struct simple_encoded *)blob;
    struct hdr_histogram *hdr = NULL;
    int ret = hdr_init(encoded->lowest_discernible_value, encoded->highest_trackable_value, encoded->significant_figures, &hdr);
    if (ret != 0) {
        return NULL;
    }

    memcpy(hdr->counts, encoded->counts, blob_len - sizeof(struct simple_encoded));
    hdr_reset_internal_counters(hdr);
    
    return hdr;
}

long long hdr_count_le_value(const struct hdr_histogram* h, long long value)
{
	struct hdr_iter iter;
	long long count = 0;
	hdr_iter_recorded_init(&iter, h);
	while (hdr_iter_next(&iter))
	{
		if((long long)iter.value <= value)
			count ++;
	}
	return count;
}