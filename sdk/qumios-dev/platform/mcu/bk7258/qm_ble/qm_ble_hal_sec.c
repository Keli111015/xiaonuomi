#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "qm_ble_hal_sec.h"
#include "qm_ble_hal_os.h"
#include "qm_utils_aes.h"

#define KEY_LEN 16

typedef struct {
    qm_aes_context aes_ctx;
    uint8_t iv[16];
    uint8_t key[16];
} qm_ble_aes_context_t;

void *qm_ble_aes128_init(const uint8_t *key, const uint8_t *iv)
{
    qm_ble_aes_context_t *ctx;

    ctx = (qm_ble_aes_context_t *)qm_ble_malloc(sizeof(qm_ble_aes_context_t));
    if (ctx == NULL) {
        return NULL;
    }

    memset(ctx, 0, sizeof(*ctx));
    memcpy(ctx->iv, iv, KEY_LEN);
    memcpy(ctx->key, key, KEY_LEN);
    return ctx;
}

int qm_ble_aes128_destroy(void *aes)
{
    if (aes) {
        qm_ble_free(aes);
    }
    return 0;
}

static int platform_aes128_crypt(void *aes_ctx, const void *src, size_t block_num, void *dst, int encrypt)
{
    size_t dlen = block_num << 4;
    uint8_t iv[16];
    qm_ble_aes_context_t *ctx = (qm_ble_aes_context_t *)aes_ctx;

    if (ctx == NULL) {
        return -1;
    }

    if (encrypt) {
        qm_utils_aes_setkey_enc(&ctx->aes_ctx, ctx->key, 128);
        memcpy(iv, ctx->iv, 16);
        qm_utils_aes_crypt_cbc(&ctx->aes_ctx, AES_ENCRYPT, dlen, iv, (uint8_t *)src, dst);
    } else {
        qm_utils_aes_setkey_dec(&ctx->aes_ctx, ctx->key, 128);
        memcpy(iv, ctx->iv, 16);
        qm_utils_aes_crypt_cbc(&ctx->aes_ctx, AES_DECRYPT, dlen, iv, (uint8_t *)src, dst);
    }

    return 0;
}

int qm_ble_aes128_cbc_encrypt(void *aes, const void *src, size_t block_num, void *dst)
{
    return platform_aes128_crypt(aes, src, block_num, dst, 1);
}

int qm_ble_aes128_cbc_decrypt(void *aes, const void *src, size_t block_num, void *dst)
{
    return platform_aes128_crypt(aes, src, block_num, dst, 0);
}
