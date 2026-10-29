#define BOOL_DEFINE_CONFLICT

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "qm_ble_hal_os.h"
#include "qm_ble_hal_sec.h"

#include "qm_utils_aes.h"

typedef struct {
    qm_aes_context aes_ctx;
    uint8_t iv[16];
    uint8_t key[16];
} qm_ble_aes_context_t;

void *qm_ble_aes128_init(const uint8_t *key, const uint8_t *iv)
{
    qm_ble_aes_context_t *context;

    if (key == NULL || iv == NULL) {
        return NULL;
    }

    context = (qm_ble_aes_context_t *)qm_ble_malloc(sizeof(*context));
    if (context == NULL) {
        return NULL;
    }

    memset(context, 0, sizeof(*context));
    memcpy(context->key, key, sizeof(context->key));
    memcpy(context->iv, iv, sizeof(context->iv));
    return context;
}

int qm_ble_aes128_destroy(void *aes)
{
    if (aes != NULL) {
        qm_ble_free(aes);
    }
    return 0;
}

static int qm_ble_aes128_crypt(void *aes, const void *src, size_t block_num,
                               void *dst, int mode)
{
    qm_ble_aes_context_t *context = (qm_ble_aes_context_t *)aes;
    uint8_t iv[sizeof(context->iv)];
    size_t length;

    if (context == NULL || src == NULL || dst == NULL || block_num == 0) {
        return -1;
    }

    length = block_num << 4;
    memcpy(iv, context->iv, sizeof(iv));
    if (mode == AES_ENCRYPT) {
        qm_utils_aes_setkey_enc(&context->aes_ctx, context->key, 128);
    } else {
        qm_utils_aes_setkey_dec(&context->aes_ctx, context->key, 128);
    }
    qm_utils_aes_crypt_cbc(&context->aes_ctx, mode, length, iv,
                           (uint8_t *)src, dst);
    return 0;
}

int qm_ble_aes128_cbc_encrypt(void *aes, const void *src, size_t block_num,
                              void *dst)
{
    return qm_ble_aes128_crypt(aes, src, block_num, dst, AES_ENCRYPT);
}

int qm_ble_aes128_cbc_decrypt(void *aes, const void *src, size_t block_num,
                              void *dst)
{
    return qm_ble_aes128_crypt(aes, src, block_num, dst, AES_DECRYPT);
}
