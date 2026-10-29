#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "qm.h"
#include "qm_ble_hal_sec.h"
#include "qm_ble_hal_os.h"
#include "qm_utils_aes.h"


#define KEY_LEN 16

typedef struct
{
    qm_aes_context aes_ctx;
    uint8_t iv[16];
    uint8_t key[16];
}qm_ble_aes_context_t;

void *qm_ble_aes128_init(const uint8_t *key, const uint8_t *iv)
{
    qm_ble_aes_context_t *qm_aes_ctx = NULL;
#if CONFIG_QM_BLE_AUTH_SUPPORT
    qm_aes_ctx = (qm_ble_aes_context_t*)qm_ble_malloc(sizeof(qm_ble_aes_context_t));
    if(qm_aes_ctx == NULL){
        return NULL;
    }

    memset(qm_aes_ctx, 0, sizeof(qm_ble_aes_context_t));

    memcpy(qm_aes_ctx->iv, iv, 16);
    memcpy(qm_aes_ctx->key, key, 16);
#endif
    return qm_aes_ctx;
}

int qm_ble_aes128_destroy(void *aes)
{
    if(aes){
        qm_ble_free(aes);
    }
    return 0;
}

static int platform_aes128_encrypt_decrypt(void *aes_ctx, const void *src,
                                           size_t siz, void *dst, int is_enc)
{
    size_t  dlen, in_len = siz;
    in_len <<= 4;
    dlen = in_len;
#if CONFIG_QM_BLE_AUTH_SUPPORT
    uint8_t iv[16] = {0};
#endif
    qm_ble_aes_context_t *qm_aes_ctx = (qm_ble_aes_context_t*)aes_ctx;
    if(qm_aes_ctx == NULL){
       return -1;
    }
#if CONFIG_QM_BLE_AUTH_SUPPORT
    if(is_enc){
        qm_utils_aes_setkey_enc( &qm_aes_ctx->aes_ctx, qm_aes_ctx->key, 128 );
        memcpy( iv, qm_aes_ctx->iv, 16 ); 
        qm_utils_aes_crypt_cbc(&qm_aes_ctx->aes_ctx, AES_ENCRYPT, dlen, iv, (uint8_t*)src, dst);
    }else{
        
        qm_utils_aes_setkey_dec( &qm_aes_ctx->aes_ctx, qm_aes_ctx->key, 128 );
        memcpy( iv, qm_aes_ctx->iv, 16 ); 
        qm_utils_aes_crypt_cbc(&qm_aes_ctx->aes_ctx, AES_DECRYPT, dlen, iv, (uint8_t*)src, dst);
    }
#endif
    return 0;

}

int qm_ble_aes128_cbc_encrypt(void *aes, const void *src, size_t block_num,
                           void *dst)
{
#if CONFIG_QM_BLE_AUTH_SUPPORT
    return platform_aes128_encrypt_decrypt(aes, src, block_num, dst, 1);
#else
    return 0;
#endif
}

int qm_ble_aes128_cbc_decrypt(void *aes, const void *src, size_t block_num,
                           void *dst)
{
#if CONFIG_QM_BLE_AUTH_SUPPORT
    return platform_aes128_encrypt_decrypt(aes, src, block_num, dst, 0);
#else
    return 0;
#endif
}
