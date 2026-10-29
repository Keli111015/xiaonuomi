#include "qm.h"
#include "qm_log.h"
#include "harmony_ble_hal.h"
#include "harmony_ble_comm.h"
#include "harmony_ble_core.h"
#include "harmony_ble_cipher.h"

#include "gcm.h"
#include "md.h"
#include "pkcs5.h"

#define LOG_TAG "harmony cipher"


int harmony_ble_cipher_flush(uint8_t *in_salt, uint8_t *in_authcode, 
                            uint8_t *out_session, uint8_t *out_hmac)
{
    int ret = QM_EOK;
    mbedtls_md_context_t context = {0};
    const mbedtls_md_info_t *md_info = NULL;

    md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if(md_info == NULL){
        QM_LOGE(LOG_TAG, "mbedtls_md_info_from_type ERR ");
        return -QM_ENOMEM;
    }
    mbedtls_md_init(&context);
    ret = mbedtls_md_setup(&context, md_info, 1);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "mbedtls_md_setup ERR %d", ret);
        goto __exit;
    }

    ret = mbedtls_pkcs5_pbkdf2_hmac(&context, 
                                    in_authcode, HARMONY_BLE_CIPHER_AUTHCODE_LEN, 
                                    in_salt, HARMONY_BLE_CIPHER_SALT_LEN, 1, 
                                    HARMONY_BLE_CIPHER_SESSIONKEY_LEN, out_session);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "mbedtls_pkcs5_pbkdf2_hmac ERR %d", ret);
        goto __exit;
    }

    ret = mbedtls_pkcs5_pbkdf2_hmac(&context, 
                                    out_session, HARMONY_BLE_CIPHER_SESSIONKEY_LEN, 
                                    in_salt, HARMONY_BLE_CIPHER_SALT_LEN, 1, 
                                    HARMONY_BLE_CIPHER_HMACKEY_LEN, out_hmac);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "mbedtls_pkcs5_pbkdf2_hmac ERR %d", ret);
        goto __exit;
    }

__exit:
    mbedtls_md_free(&context);
    return QM_EOK;
}

int harmony_ble_cipher_decrypt_data(harmony_ble_cipher_info_t *cipher_info)
{
    int ret = QM_EOK;
    mbedtls_gcm_context context = {0};

    mbedtls_gcm_init(&context);
    ret = mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES,cipher_info->session_key, HARMONY_BLE_CIPHER_AES128_LENGTH);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "gcm_setkey ERR %d", ret);
        goto __exit;
    }
    ret = mbedtls_gcm_auth_decrypt(&context, 
                                cipher_info->data_len, 
                                cipher_info->iv, HARMONY_BLE_CIPHER_IV_LEN, 
                                cipher_info->prod_id, cipher_info->prod_length, 
                                cipher_info->tag, HARMONY_BLE_CIPHER_TAG_LEN, 
                                cipher_info->in_data, cipher_info->out_data);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "mbedtls_gcm_auth_decrypt ERR %d", ret);
        goto __exit;
    }
    
__exit:
    mbedtls_gcm_free(&context);
    return QM_EOK;
}

int harmony_ble_cipher_encrypt_data(harmony_ble_cipher_info_t *cipher_info)
{
    int ret = QM_EOK;
    mbedtls_gcm_context context = {0};

    mbedtls_gcm_init(&context);
    ret = mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES,cipher_info->session_key, HARMONY_BLE_CIPHER_AES128_LENGTH);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "gcm_setkey ERR %d", ret);
        goto __exit;
    }

    ret = mbedtls_gcm_crypt_and_tag(&context, MBEDTLS_GCM_ENCRYPT,
                                cipher_info->data_len, 
                                cipher_info->iv, HARMONY_BLE_CIPHER_IV_LEN, 
                                cipher_info->prod_id, cipher_info->prod_length, 
                                cipher_info->in_data, cipher_info->out_data,
                                HARMONY_BLE_CIPHER_TAG_LEN, cipher_info->tag);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "mbedtls_gcm_auth_decrypt ERR %d", ret);
        goto __exit;
    }
    
__exit:
    mbedtls_gcm_free(&context);
    return QM_EOK;
}

int harmony_ble_cipher_encrpy_hmac_flush(uint8_t *data, uint16_t len, uint8_t *hmac_key, uint8_t *hmac_value)
{
    int ret = QM_EOK;
    const mbedtls_md_info_t *mdInfo = NULL; 
    mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256); 
    ret = mbedtls_md_hmac(mdInfo, 
                        hmac_key, //hmacKey。
                        HARMONY_BLE_CIPHER_HMACKEY_LEN, //hmacKey 长度，32Byte 
                        data, //打包的 payload 数据 
                        len, //payload 数据 
                        hmac_value);//Hmac 校验值
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "mbedtls_gcm_auth_decrypt ERR %d", ret);
    }
    return ret;
}