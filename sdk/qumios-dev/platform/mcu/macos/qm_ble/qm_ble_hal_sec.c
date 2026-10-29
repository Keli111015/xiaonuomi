#include <stddef.h>
#include <stdint.h>

#include "qm_ble_hal_sec.h"
#include "qm_ble_hal_os.h"

#include <CommonCrypto/CommonCryptor.h>
#include <string.h>

typedef struct {
    uint8_t key[16];
    uint8_t iv[16];
} mac_aes_ctx_t;

void *qm_ble_aes128_init(const uint8_t *key, const uint8_t *iv)
{
    mac_aes_ctx_t *ctx;
    if (key == NULL || iv == NULL) {
        return NULL;
    }
    ctx = (mac_aes_ctx_t *)qm_ble_malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return NULL;
    }
    memcpy(ctx->key, key, sizeof(ctx->key));
    memcpy(ctx->iv, iv, sizeof(ctx->iv));
    return ctx;
}

int qm_ble_aes128_destroy(void *aes)
{
    if (aes != NULL) {
        memset(aes, 0, sizeof(mac_aes_ctx_t));
        qm_ble_free(aes);
    }
    return 0;
}

static int crypt(void *aes, const void *src, size_t block_num, void *dst, CCOperation operation)
{
    mac_aes_ctx_t *ctx = (mac_aes_ctx_t *)aes;
    size_t length = block_num * 16u;
    size_t moved = 0;
    CCCryptorStatus status;

    if (ctx == NULL || src == NULL || dst == NULL || block_num == 0) {
        return -1;
    }
    status = CCCrypt(operation, kCCAlgorithmAES, 0, ctx->key, sizeof(ctx->key),
                     ctx->iv, src, length, dst, length, &moved);
    return status == kCCSuccess && moved == length ? 0 : -1;
}

int qm_ble_aes128_cbc_encrypt(void *aes, const void *src, size_t block_num, void *dst)
{
    return crypt(aes, src, block_num, dst, kCCEncrypt);
}

int qm_ble_aes128_cbc_decrypt(void *aes, const void *src, size_t block_num, void *dst)
{
    return crypt(aes, src, block_num, dst, kCCDecrypt);
}
