#include "cloud_dynreg.h"
#include "util_httpc.h"
#include "json_parser.h"
#include "iot_ca.h"
#include "iot_cloud_debug.h"
#include "cloudapi.h"
#include "qm_utils_aes.h"
#include "qm_utils_hmac.h"
#include "qm_utils_base64.h"
#include "qm_utils_string.h"
#include "iot_guider.h"
#include "qm_config.h"

#include "qm_log.h"

#define LOG_TAG "cloud"
#if CONFIG_DYN_REG_SUPPORT

#define DEVICE_DYN_REG_SIGN_LEN            (64)
#define DEVICE_DYNREG_NONCE_MAX  ((1U << 31) - 1)

#define DEVICE_DYNREG_PARAM_FORMAT   "{\"sn\":\"%s\",\"nonce\":%d,\"productId\":\"%d\",\"timestamp\":%d,\"signature\":\"%s\"}"
#define DEVICE_DYNREG_SIGN_FORMAT   "productId=%s&timestamp=%s&nonce=%s&sn=%s"

#define DEVICE_DYNREG_URL  "https://%s/baseApi/iotservice/askCertificate"


static int32_t dynreg_sign_cal(dev_dynreg_info_t *dynreg_info, char *nonce, char *timestamp, uint8_t *signout, uint32_t max_signlen)
{

    uint32_t olen;
    int32_t     sign_len;
    char *      sign_source            = NULL;
    char        sign[DEVICE_DYN_REG_SIGN_LEN] = {0};
    char str_pid[12] = {0};

    hal_snprintf(str_pid, 12, "%d", dynreg_info->product_id);

    /*format sign data*/
    sign_len = strlen(DEVICE_DYNREG_SIGN_FORMAT) + strlen(dynreg_info->sn) + strlen(str_pid) + strlen(timestamp) + strlen(nonce);

    sign_source = hal_malloc(sign_len);
    if (sign_source == NULL) {
        QM_LOGE(LOG_TAG,"malloc sign source buff fail");
        return ALLOC_MEM_ERR;
    }
    memset(sign_source, 0, sign_len);
    hal_snprintf((char *)sign_source, sign_len, DEVICE_DYNREG_SIGN_FORMAT, str_pid, timestamp, nonce, dynreg_info->sn);

    QM_LOGD(LOG_TAG,"sign source:%s", sign_source);
     /*cal hmac sha1*/
    qm_utils_hmac_sha1(sign_source, strlen(sign_source), sign, dynreg_info->product_secret, strlen(dynreg_info->product_secret));

    /*base64 encode*/
    qm_utils_base64encode((uint8_t *)sign, strlen(sign), max_signlen, signout, &olen);

    hal_free(sign_source);

    return NO_ERR;

}

static int32_t dev_info_decrypt(dev_dynreg_info_t *dynreg_info, uint8_t *cipher, uint32_t len, uint8_t *plaintext, uint32_t *plaintext_len)
{
    int32_t ret = NO_ERR;
    qm_aes_context aes_ctx = {0};
    char *giv = "0123456789ABCDEF";
    uint8_t iv[16];
    uint8_t key[16];
    uint8_t pad_len = 0;

    hex_str_to_nums(dynreg_info->product_secret, PRODUCT_SECRET_LEN, key);

    QM_HEX_LOGD(LOG_TAG,"key: ", key, 16);

    memcpy( iv, giv, 16 );
    qm_utils_aes_setkey_dec( &aes_ctx, key, 128 );
    qm_utils_aes_crypt_cbc(&aes_ctx, AES_DECRYPT, len, iv, cipher, plaintext);

    pad_len = plaintext[len - 1];

    *plaintext_len = len - pad_len;

    return ret;
}


int32_t cloud_device_dynreg(dev_dynreg_info_t *dynreg_info)
{
   
    uint32_t      nonce;
    int32_t      ret = NO_ERR;
    uint32_t timestamp;
    int32_t      len;
    char     sign[DEVICE_DYN_REG_SIGN_LEN] = {0};
    char *   request               = NULL;

    uint8_t *cipher_hex = NULL;
    uint8_t *cipher_str_hex = NULL;
    uint8_t *cipher = NULL;
    uint32_t cipher_hex_len = 0;
    uint32_t cipher_len = 0;
    int32_t cipher_base64_len = 0;
    uint8_t *plaintext = NULL;
    uint8_t *plaintext_len = NULL;
    char *device_secret = NULL;

    char *val = NULL;
    uint8_t *pvalue = NULL;
	
	char url[XJ_GUIDER_DYNAMIC_URL_LEN] = {0};
	char dest_url[96] = {0};

    #define DEVICE_DYNREG_RESP_MAX_LEN  256
    char rsp_payload[DEVICE_DYNREG_RESP_MAX_LEN] = {0};

    httpclient_t        httpc = {0};
    httpclient_data_t   httpc_data = {0};

    char str_pid[12] = {0};
    char str_nonce[12] = {0};
    char str_timestamp[12] = {0};

    hal_srandom(hal_uptime_ms());

    nonce = hal_random_get(DEVICE_DYNREG_NONCE_MAX);
    timestamp = hal_uptime_ms();

    hal_snprintf(str_nonce, 12, "%d", nonce);
    hal_snprintf(str_timestamp, 12, "%d", timestamp);
    hal_snprintf(str_pid, 12, "%d", dynreg_info->product_id);

    /*cal sign*/
    if (NO_ERR == dynreg_sign_cal(dynreg_info, str_nonce, str_timestamp, (uint8_t*)sign, DEVICE_DYN_REG_SIGN_LEN)) {
        QM_LOGD(LOG_TAG,"dynreg sign:%s", sign);
    } else {
        QM_LOGE(LOG_TAG,"dynreg cal sign fail");
        return XCLOUD_ERR;
    }

    /*format http request*/
    len = strlen(DEVICE_DYNREG_PARAM_FORMAT) + strlen(dynreg_info->sn) + strlen(str_pid) + strlen(str_timestamp) + strlen(str_nonce) + strlen(sign);
    request = hal_malloc(len);
    if (!request) {
        QM_LOGE(LOG_TAG,"malloc request memory fail");
        return XCLOUD_ERR;
    }

    memset(request, 0, len);
    hal_snprintf(request, len, DEVICE_DYNREG_PARAM_FORMAT, dynreg_info->sn, nonce, dynreg_info->product_id, timestamp, sign);
    QM_LOGD(LOG_TAG,"dynreg request:%s", request);

    httpc_data.post_buf = request;
    httpc_data.post_buf_len = strlen(request);
    httpc_data.post_content_type = "application/json";

    httpc_data.response_buf = rsp_payload;
    httpc_data.response_buf_len = DEVICE_DYNREG_RESP_MAX_LEN;
	
	iot_guider_get_domain(GUIDER_XJ_DOMAIN_HTTP, url, XJ_GUIDER_DYNAMIC_URL_LEN);
	
	hal_snprintf(dest_url, 96, DEVICE_DYNREG_URL, url);

#ifndef IOT_WITHOUT_TLS
    if (0 != http_client_common(&httpc, dest_url, 443, iot_ca_get(), HTTPCLIENT_POST, 8000, &httpc_data)) {
        QM_LOGD(LOG_TAG,"dynamic register response fail");
        ret = XCLOUD_ERR;
        goto exit;
    }
#else
    if (0 != http_client_common(&httpc, dest_url, 80, NULL, HTTPCLIENT_POST, 8000, &httpc_data)) {
        QM_LOGD(LOG_TAG,"dynamic register response fail");
        ret = XCLOUD_ERR;
        goto exit;
    }
#endif
    QM_LOGD(LOG_TAG,"dynamic register response %s",httpc_data.response_buf);

    val = qm_json_get_value_by_name((char *)httpc_data.response_buf, strlen(httpc_data.response_buf), "payload", &cipher_base64_len, NULL);
    if (NULL == val) {
        ret = PARAM_ERR;
        goto exit;
    }

    len = cipher_base64_len + cipher_base64_len + cipher_base64_len/2 + cipher_base64_len/2;
    pvalue = (uint8_t*)hal_malloc(len);
    if(pvalue == NULL){
        ret = ALLOC_MEM_ERR;
        goto exit;
    }

    memset(pvalue, 0, len);
    cipher = pvalue;

    memcpy(cipher, val, cipher_base64_len);
    
    cipher_str_hex = pvalue + cipher_base64_len;

    qm_utils_base64decode((const uint8_t*)cipher, cipher_base64_len, cipher_base64_len, cipher_str_hex, &cipher_hex_len); 

    cipher_hex = pvalue + cipher_base64_len + cipher_base64_len;

    hex_str_to_nums((char*)cipher_str_hex, cipher_hex_len, cipher_hex);

    //mqtt_hex_log("cipher: ", cipher_hex, cipher_hex_len/2);

    plaintext = pvalue + cipher_base64_len + cipher_base64_len + cipher_base64_len/2;

    plaintext_len = cipher_hex_len/2;

    ret = dev_info_decrypt(dynreg_info, cipher_hex, cipher_hex_len/2, plaintext, &plaintext_len);

    //QM_LOGD(LOG_TAG,"plaintext: %s", plaintext);

    device_secret = (char*)plaintext + DEVICE_ID_LEN + 1;

    memcpy(dynreg_info->device_id, plaintext, DEVICE_ID_LEN);

    memcpy(dynreg_info->device_secret, device_secret, DEVICE_SECRET_LEN);

    QM_LOGD(LOG_TAG,"device id: %s", dynreg_info->device_id);
    QM_LOGD(LOG_TAG,"device secret: %s", dynreg_info->device_secret);

exit:
    if(pvalue){
        hal_free(pvalue);
        pvalue = NULL;
    }
   
    http_client_close(&httpc);
    return ret;
} 



#endif