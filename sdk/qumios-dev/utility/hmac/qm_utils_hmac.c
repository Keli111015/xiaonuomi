#include "qm_types.h"
#include "qm_utils_md5.h"
#include "qm_utils_sha1.h"
#include "qm_utils_sha256.h"
#include "qm_utils_hmac.h"
#include "qm_errno.h"

#define KEY_IOPAD_SIZE 64

#define MD5_DIGEST_SIZE 16
#define SHA1_DIGEST_SIZE 20
#define SHA256_DIGEST_SIZE 32

static void qm_utils_hmac_zeroize(void *v, uint32_t n)
{
    volatile uint8_t *p = (volatile uint8_t *)v;

    while (n--) {
        *p++ = 0;
    }
}

int qm_utils_hmac_md5_init(qm_hmac_md5_context *ctx,
                           const uint8_t *key,
                           uint32_t key_len)
{
    uint8_t k_ipad[KEY_IOPAD_SIZE];
    uint8_t key_hash[MD5_DIGEST_SIZE] = {0};
    uint32_t i;

    if (!ctx || (!key && key_len > 0)) {
        return -QM_EINVAL;
    }

    memset(ctx, 0, sizeof(*ctx));
    memset(k_ipad, 0, sizeof(k_ipad));
    memset(ctx->opad, 0, sizeof(ctx->opad));

    if (key_len > KEY_IOPAD_SIZE) {
        qm_utils_md5(key, key_len, key_hash);
        memcpy(k_ipad, key_hash, MD5_DIGEST_SIZE);
        memcpy(ctx->opad, key_hash, MD5_DIGEST_SIZE);
        qm_utils_hmac_zeroize(key_hash, sizeof(key_hash));
    } else {
        if (key_len > 0) {
            memcpy(k_ipad, key, key_len);
            memcpy(ctx->opad, key, key_len);
        }
    }

    for (i = 0; i < KEY_IOPAD_SIZE; i++) {
        k_ipad[i] ^= 0x36;
        ctx->opad[i] ^= 0x5c;
    }

    qm_utils_md5_init(&ctx->inner_ctx);
    qm_utils_md5_starts(&ctx->inner_ctx);
    qm_utils_md5_update(&ctx->inner_ctx, k_ipad, KEY_IOPAD_SIZE);
    ctx->started = QM_TRUE;

    qm_utils_hmac_zeroize(k_ipad, sizeof(k_ipad));
    return QM_EOK;
}

int qm_utils_hmac_md5_update(qm_hmac_md5_context *ctx,
                             const uint8_t *data,
                             uint32_t data_len)
{
    if (!ctx || !ctx->started) {
        return -QM_EINVAL;
    }
    if (data_len == 0) {
        return QM_EOK;
    }
    if (!data) {
        return -QM_EINVAL;
    }

    qm_utils_md5_update(&ctx->inner_ctx, data, data_len);
    return QM_EOK;
}

int qm_utils_hmac_md5_finish(qm_hmac_md5_context *ctx,
                             uint8_t output[QM_UTILS_HMAC_MD5_DIGEST_LEN])
{
    uint8_t inner_hash[MD5_DIGEST_SIZE];
    qm_md5_context outer_ctx;

    if (!ctx || !ctx->started || !output) {
        return -QM_EINVAL;
    }

    qm_utils_md5_finish(&ctx->inner_ctx, inner_hash);

    qm_utils_md5_init(&outer_ctx);
    qm_utils_md5_starts(&outer_ctx);
    qm_utils_md5_update(&outer_ctx, ctx->opad, KEY_IOPAD_SIZE);
    qm_utils_md5_update(&outer_ctx, inner_hash, MD5_DIGEST_SIZE);
    qm_utils_md5_finish(&outer_ctx, output);
    qm_utils_md5_free(&outer_ctx);

    qm_utils_hmac_zeroize(inner_hash, sizeof(inner_hash));
    qm_utils_hmac_md5_free(ctx);
    return QM_EOK;
}

void qm_utils_hmac_md5_free(qm_hmac_md5_context *ctx)
{
    if (!ctx) {
        return;
    }

    qm_utils_md5_free(&ctx->inner_ctx);
    qm_utils_hmac_zeroize(ctx, sizeof(*ctx));
}

int qm_utils_hmac_md5(const char *msg, int msg_len, char *digest, const char *key, int key_len)
{
    qm_hmac_md5_context ctx;
    uint8_t out[MD5_DIGEST_SIZE];
    int ret;
    int i;

    if (!msg || !digest || (!key && key_len > 0) || msg_len < 0 || key_len < 0) {
        return -QM_EINVAL;
    }

    ret = qm_utils_hmac_md5_init(&ctx, (const uint8_t *)key, (uint32_t)key_len);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_utils_hmac_md5_update(&ctx, (const uint8_t *)msg, (uint32_t)msg_len);
    if (ret != QM_EOK) {
        qm_utils_hmac_md5_free(&ctx);
        return ret;
    }
    ret = qm_utils_hmac_md5_finish(&ctx, out);
    if (ret != QM_EOK) {
        return ret;
    }

    for (i = 0; i < MD5_DIGEST_SIZE; ++i) {
        digest[i * 2] = qm_utils_hb2hex(out[i] >> 4);
        digest[i * 2 + 1] = qm_utils_hb2hex(out[i]);
    }
    qm_utils_hmac_zeroize(out, sizeof(out));
    return QM_EOK;
}

int qm_utils_hmac_sha1_init(qm_hmac_sha1_context *ctx,
                            const uint8_t *key,
                            uint32_t key_len)
{
    uint8_t k_ipad[KEY_IOPAD_SIZE];
    uint8_t key_hash[SHA1_DIGEST_SIZE] = {0};
    uint32_t i;

    if (!ctx || (!key && key_len > 0)) {
        return -QM_EINVAL;
    }

    memset(ctx, 0, sizeof(*ctx));
    memset(k_ipad, 0, sizeof(k_ipad));
    memset(ctx->opad, 0, sizeof(ctx->opad));

    if (key_len > KEY_IOPAD_SIZE) {
        qm_utils_sha1(key, key_len, key_hash);
        memcpy(k_ipad, key_hash, SHA1_DIGEST_SIZE);
        memcpy(ctx->opad, key_hash, SHA1_DIGEST_SIZE);
        qm_utils_hmac_zeroize(key_hash, sizeof(key_hash));
    } else {
        if (key_len > 0) {
            memcpy(k_ipad, key, key_len);
            memcpy(ctx->opad, key, key_len);
        }
    }

    for (i = 0; i < KEY_IOPAD_SIZE; i++) {
        k_ipad[i] ^= 0x36;
        ctx->opad[i] ^= 0x5c;
    }

    qm_utils_sha1_init(&ctx->inner_ctx);
    qm_utils_sha1_starts(&ctx->inner_ctx);
    qm_utils_sha1_update(&ctx->inner_ctx, k_ipad, KEY_IOPAD_SIZE);
    ctx->started = QM_TRUE;

    qm_utils_hmac_zeroize(k_ipad, sizeof(k_ipad));
    return QM_EOK;
}

int qm_utils_hmac_sha1_update(qm_hmac_sha1_context *ctx,
                              const uint8_t *data,
                              uint32_t data_len)
{
    if (!ctx || !ctx->started) {
        return -QM_EINVAL;
    }
    if (data_len == 0) {
        return QM_EOK;
    }
    if (!data) {
        return -QM_EINVAL;
    }

    qm_utils_sha1_update(&ctx->inner_ctx, data, data_len);
    return QM_EOK;
}

int qm_utils_hmac_sha1_finish(qm_hmac_sha1_context *ctx,
                              uint8_t output[QM_UTILS_HMAC_SHA1_DIGEST_LEN])
{
    uint8_t inner_hash[SHA1_DIGEST_SIZE];
    qm_sha1_context outer_ctx;

    if (!ctx || !ctx->started || !output) {
        return -QM_EINVAL;
    }

    qm_utils_sha1_finish(&ctx->inner_ctx, inner_hash);

    qm_utils_sha1_init(&outer_ctx);
    qm_utils_sha1_starts(&outer_ctx);
    qm_utils_sha1_update(&outer_ctx, ctx->opad, KEY_IOPAD_SIZE);
    qm_utils_sha1_update(&outer_ctx, inner_hash, SHA1_DIGEST_SIZE);
    qm_utils_sha1_finish(&outer_ctx, output);
    qm_utils_sha1_free(&outer_ctx);

    qm_utils_hmac_zeroize(inner_hash, sizeof(inner_hash));
    qm_utils_hmac_sha1_free(ctx);
    return QM_EOK;
}

void qm_utils_hmac_sha1_free(qm_hmac_sha1_context *ctx)
{
    if (!ctx) {
        return;
    }

    qm_utils_sha1_free(&ctx->inner_ctx);
    qm_utils_hmac_zeroize(ctx, sizeof(*ctx));
}

int qm_utils_hmac_sha1(const char *msg, int msg_len, char *digest, const char *key, int key_len)
{
    qm_hmac_sha1_context ctx;
    uint8_t out[SHA1_DIGEST_SIZE];
    int ret;
    int i;

    if (!msg || !digest || (!key && key_len > 0) || msg_len < 0 || key_len < 0) {
        return -QM_EINVAL;
    }

    ret = qm_utils_hmac_sha1_init(&ctx, (const uint8_t *)key, (uint32_t)key_len);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_utils_hmac_sha1_update(&ctx, (const uint8_t *)msg, (uint32_t)msg_len);
    if (ret != QM_EOK) {
        qm_utils_hmac_sha1_free(&ctx);
        return ret;
    }
    ret = qm_utils_hmac_sha1_finish(&ctx, out);
    if (ret != QM_EOK) {
        return ret;
    }

    for (i = 0; i < SHA1_DIGEST_SIZE; ++i) {
        digest[i * 2] = qm_utils_hb2hex(out[i] >> 4);
        digest[i * 2 + 1] = qm_utils_hb2hex(out[i]);
    }
    qm_utils_hmac_zeroize(out, sizeof(out));
    return QM_EOK;
}

int qm_utils_hmac_sha256_init(qm_hmac_sha256_context *ctx,
                              const uint8_t *key,
                              uint32_t key_len)
{
    uint8_t k_ipad[KEY_IOPAD_SIZE];
    uint8_t key_hash[SHA256_DIGEST_SIZE] = {0};
    uint32_t i;

    if (!ctx || (!key && key_len > 0)) {
        return -QM_EINVAL;
    }

    memset(ctx, 0, sizeof(*ctx));
    memset(k_ipad, 0, sizeof(k_ipad));
    memset(ctx->opad, 0, sizeof(ctx->opad));

    if (key_len > KEY_IOPAD_SIZE) {
        qm_utils_sha256(key, key_len, key_hash);
        memcpy(k_ipad, key_hash, SHA256_DIGEST_SIZE);
        memcpy(ctx->opad, key_hash, SHA256_DIGEST_SIZE);
        qm_utils_hmac_zeroize(key_hash, sizeof(key_hash));
    } else {
        if (key_len > 0) {
            memcpy(k_ipad, key, key_len);
            memcpy(ctx->opad, key, key_len);
        }
    }

    for (i = 0; i < KEY_IOPAD_SIZE; i++) {
        k_ipad[i] ^= 0x36;
        ctx->opad[i] ^= 0x5c;
    }

    qm_utils_sha256_init(&ctx->inner_ctx);
    qm_utils_sha256_starts(&ctx->inner_ctx);
    qm_utils_sha256_update(&ctx->inner_ctx, k_ipad, KEY_IOPAD_SIZE);
    ctx->started = QM_TRUE;

    qm_utils_hmac_zeroize(k_ipad, sizeof(k_ipad));
    return QM_EOK;
}

int qm_utils_hmac_sha256_update(qm_hmac_sha256_context *ctx,
                                const uint8_t *data,
                                uint32_t data_len)
{
    if (!ctx || !ctx->started) {
        return -QM_EINVAL;
    }
    if (data_len == 0) {
        return QM_EOK;
    }
    if (!data) {
        return -QM_EINVAL;
    }

    qm_utils_sha256_update(&ctx->inner_ctx, data, data_len);
    return QM_EOK;
}

int qm_utils_hmac_sha256_finish(qm_hmac_sha256_context *ctx,
                                uint8_t output[QM_UTILS_HMAC_SHA256_DIGEST_LEN])
{
    uint8_t inner_hash[SHA256_DIGEST_SIZE];
    qm_sha256_context outer_ctx;

    if (!ctx || !ctx->started || !output) {
        return -QM_EINVAL;
    }

    qm_utils_sha256_finish(&ctx->inner_ctx, inner_hash);

    qm_utils_sha256_init(&outer_ctx);
    qm_utils_sha256_starts(&outer_ctx);
    qm_utils_sha256_update(&outer_ctx, ctx->opad, KEY_IOPAD_SIZE);
    qm_utils_sha256_update(&outer_ctx, inner_hash, SHA256_DIGEST_SIZE);
    qm_utils_sha256_finish(&outer_ctx, output);
    qm_utils_sha256_free(&outer_ctx);

    qm_utils_hmac_zeroize(inner_hash, sizeof(inner_hash));
    qm_utils_hmac_sha256_free(ctx);
    return QM_EOK;
}

void qm_utils_hmac_sha256_free(qm_hmac_sha256_context *ctx)
{
    if (!ctx) {
        return;
    }

    qm_utils_sha256_free(&ctx->inner_ctx);
    qm_utils_hmac_zeroize(ctx, sizeof(*ctx));
}

int qm_utils_hmac_sha256(const uint8_t *msg,
                         uint32_t msg_len,
                         uint8_t output[QM_UTILS_HMAC_SHA256_DIGEST_LEN],
                         const uint8_t *key,
                         uint32_t key_len)
{
    qm_hmac_sha256_context ctx;
    int ret;

    if (!output || (!key && key_len > 0) || (!msg && msg_len > 0)) {
        return -QM_EINVAL;
    }

    ret = qm_utils_hmac_sha256_init(&ctx, key, key_len);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_utils_hmac_sha256_update(&ctx, msg, msg_len);
    if (ret != QM_EOK) {
        qm_utils_hmac_sha256_free(&ctx);
        return ret;
    }
    return qm_utils_hmac_sha256_finish(&ctx, output);
}
