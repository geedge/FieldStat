#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include "hdr/hdr_histogram.h"
#include "hdr/hdr_histogram_log.h"


/*
    This is used for exporting json message to kafka, which is then read by JAVA. 
    For java decoding: refer to 
    https://github.com/HdrHistogram/HdrHistogram
    AbstractHistogram.java:synchronized public int encodeIntoCompressedByteBuffer()

    java cookie: 
    private static final int V0EncodingCookieBase = 0x1c849308;    
    private static final int V0CompressedEncodingCookieBase = 0x1c849309;
    private static final int V1EncodingCookieBase = 0x1c849301;
    private static final int V1CompressedEncodingCookieBase = 0x1c849302;
    private static final int V2EncodingCookieBase = 0x1c849303;   <-------- use this
    private static final int V2CompressedEncodingCookieBase = 0x1c849304;

    exported buffer is after base64 encoding./
*/
void histogram_encode_into_b64(struct hdr_histogram *hdr, char **buffer, size_t *buffer_size);
/*
    the most simple and fast version of encoding. No compression, no base64 encoding, no LEB128 encoding, no user-defined extra parameters, only carry the most basic information.
*/
void histogram_encode_into_blob(const struct hdr_histogram *hdr, char **buffer, size_t *blob_len);
/*
    decode from buffer output by histogram_encode_into_blob.
*/
struct hdr_histogram *histogram_decode_from_blob(const char *buffer, size_t blob_len);

#ifdef __cplusplus
}
#endif