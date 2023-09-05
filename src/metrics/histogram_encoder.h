#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include "hdr/hdr_histogram.h"
#include "hdr/hdr_histogram_log.h"


/*
    author: Chenzizhan 
    new HDR encoding method without compression

    java cookie: 
    private static final int V0EncodingCookieBase = 0x1c849308;    
    private static final int V0CompressedEncodingCookieBase = 0x1c849309;
    private static final int V1EncodingCookieBase = 0x1c849301;
    private static final int V1CompressedEncodingCookieBase = 0x1c849302;
    private static final int V2EncodingCookieBase = 0x1c849303;   <-------- use this
    private static final int V2CompressedEncodingCookieBase = 0x1c849304;

    exported buffer is after base64 encoding
*/

void histogram_encode_into_b64(struct hdr_histogram *hdr, char **buffer, size_t *buffer_size);

#ifdef __cplusplus
}
#endif