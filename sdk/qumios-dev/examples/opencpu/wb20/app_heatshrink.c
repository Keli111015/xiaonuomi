#include "qm_utils_base64.h"
#include "app_heatshrink.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// int qm_utils_base64decode(const uint8_t *data, uint32_t inputLength, uint32_t outputLenMax,
//                               uint8_t *decodedData, uint32_t *outputLength)


int app_heatshrink_decode(uint8_t *base64_encode_in, uint16_t base64_encode_in_len, uint8_t *decode_out, uint16_t decode_out_len)
{
    uint16_t len = 0; //base64解码后的数据长度
    size_t count = DEF_DECODER_BASE64_BUFFER_LEN;
    uint8_t base64_decode_out[DEF_DECODER_BASE64_BUFFER_LEN] = {0};
    heatshrink_encoder *hsd = NULL;
    HSE_sink_res status = -1;

    uint32_t outputLength = 0;

    uint8_t window_sz2 = DEF_WINDOW_SZ2;
    uint8_t lookahead_sz2 = DEF_LOOKAHEAD_SZ2;

    if(base64_encode_in == NULL || base64_encode_in_len == 0 \
            || decode_out == NULL || decode_out_len == 0)
    {
        return -1;
    }
    if(base64_encode_in_len > DEF_DECODER_BASE64_BUFFER_LEN || \
        decode_out_len > DEF_DECODER_BASE64_BUFFER_LEN)
    {
        return -1;
    }
    len = base64_decode((const char *)base64_encode_in, (unsigned int)base64_encode_in_len, DEF_DECODER_BASE64_BUFFER_LEN, base64_decode_out, &outputLength);
    if(len == 0)
    {
        return -1;
    }

    hsd = heatshrink_decoder_alloc( len, window_sz2, lookahead_sz2);
    if(hsd == NULL)
    {
        return -1;
    }
// ghjhh
    status =  heatshrink_decoder_sink(hsd, base64_decode_out, len, &count);
    if(status != 0)
    {
        goto _exit;
    }

    status =  heatshrink_decoder_poll(hsd, decode_out, decode_out_len, &count);
    if(status != 0)
    {
        goto _exit;
    }

    status =  heatshrink_decoder_finish(hsd);
    if(status != 0)
    {
        goto _exit;
    }

    heatshrink_decoder_free(hsd);
    return 0;

_exit:
    heatshrink_decoder_free(hsd);
    return -1;
}

#if 0
int main()
{  
    //需要解压缩的字符串
    uint8_t *base64_decode_string = "mc2m0wlkxnEwmEsmgAZTEAQwCMAD8CDwA/BC8BnwA/Ay8BnwCPAD8BjwGPAD8APwA/AD8APwA/AD8APwkPAD8BjwGPAh8APwA/BB8BDwEPAD8APwA/BI8AjwA/AD8APwA/AD8APwA/AD8APwA/AD8APwA/AD8APwA/AD8RjwGPAI8AjwEImEwmA=";

    uint8_t decode[4096] = {0};
    size_t count = 4096;

    if(app_heatshrink_decode(base64_decode_string, strlen(base64_decode_string), decode, sizeof(decode)) != 0)
    {
        printf("app_heatshrink_decode error\n");
        return -1;
    }
    printf("decode:%s\n", decode);
    return 0;


}
#endif
