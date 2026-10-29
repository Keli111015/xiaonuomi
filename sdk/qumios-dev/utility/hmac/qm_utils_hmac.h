#ifndef _QM_UTILS_HMAC_H_
#define _QM_UTILS_HMAC_H_

#include "qm_types.h"
#include "qm_utils_md5.h"
#include "qm_utils_sha1.h"
#include "qm_utils_sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QM_UTILS_HMAC_MD5_DIGEST_LEN       16
#define QM_UTILS_HMAC_MD5_HEX_LEN          32
#define QM_UTILS_HMAC_SHA1_DIGEST_LEN      20
#define QM_UTILS_HMAC_SHA1_HEX_LEN         40
#define QM_UTILS_HMAC_SHA256_DIGEST_LEN    32
#define QM_UTILS_HMAC_SHA256_HEX_LEN       64

/**
 * @brief Streaming HMAC-MD5 context.
 */
typedef struct {
    qm_md5_context inner_ctx;
    uint8_t opad[64];
    qm_bool started;
} qm_hmac_md5_context;

/**
 * @brief Streaming HMAC-SHA1 context.
 */
typedef struct {
    qm_sha1_context inner_ctx;
    uint8_t opad[64];
    qm_bool started;
} qm_hmac_sha1_context;

/**
 * @brief Streaming HMAC-SHA256 context.
 */
typedef struct {
    qm_sha256_context inner_ctx;
    uint8_t opad[64];
    qm_bool started;
} qm_hmac_sha256_context;

/**
 * @brief Calculate HMAC-MD5 and return lowercase hex output.
 *
 * This legacy helper writes 32 hex characters to digest and does not append
 * a trailing '\0'. New streaming users should prefer
 * qm_utils_hmac_md5_init/update/finish.
 *
 * @param[in] msg Input message
 * @param[in] msg_len Input message length in bytes
 * @param[out] digest Output buffer, at least QM_UTILS_HMAC_MD5_HEX_LEN bytes
 * @param[in] key HMAC key; may be NULL only when key_len is 0
 * @param[in] key_len HMAC key length in bytes
 * @return QM_EOK on success, negative error code on failure
 */
int qm_utils_hmac_md5(const char *msg, int msg_len, char *digest, const char *key, int key_len);

/**
 * @brief Calculate HMAC-SHA1 and return lowercase hex output.
 *
 * This legacy helper writes 40 hex characters to digest and does not append
 * a trailing '\0'. New streaming users should prefer
 * qm_utils_hmac_sha1_init/update/finish.
 *
 * @param[in] msg Input message
 * @param[in] msg_len Input message length in bytes
 * @param[out] digest Output buffer, at least 40 bytes
 * @param[in] key HMAC key; may be NULL only when key_len is 0
 * @param[in] key_len HMAC key length in bytes
 * @return QM_EOK on success, negative error code on failure
 */
int qm_utils_hmac_sha1(const char *msg, int msg_len, char *digest, const char *key, int key_len);

/**
 * @brief Initialize a streaming HMAC-MD5 context.
 *
 * @param[out] ctx HMAC-MD5 context
 * @param[in] key HMAC key; may be NULL only when key_len is 0
 * @param[in] key_len HMAC key length in bytes
 * @return QM_EOK on success, negative error code on failure
 */
int qm_utils_hmac_md5_init(qm_hmac_md5_context *ctx,
                           const uint8_t *key,
                           uint32_t key_len);

/**
 * @brief Feed data into a streaming HMAC-MD5 context.
 *
 * @param[in,out] ctx HMAC-MD5 context initialized by qm_utils_hmac_md5_init()
 * @param[in] data Data chunk; may be NULL only when data_len is 0
 * @param[in] data_len Data chunk length in bytes
 * @return QM_EOK on success, negative error code on failure
 */
int qm_utils_hmac_md5_update(qm_hmac_md5_context *ctx,
                             const uint8_t *data,
                             uint32_t data_len);

/**
 * @brief Finish streaming HMAC-MD5 and clear the context.
 *
 * @param[in,out] ctx HMAC-MD5 context
 * @param[out] output Output buffer, QM_UTILS_HMAC_MD5_DIGEST_LEN bytes
 * @return QM_EOK on success, negative error code on failure
 */
int qm_utils_hmac_md5_finish(qm_hmac_md5_context *ctx,
                             uint8_t output[QM_UTILS_HMAC_MD5_DIGEST_LEN]);

/**
 * @brief Clear a streaming HMAC-MD5 context.
 *
 * @param[in,out] ctx HMAC-MD5 context
 */
void qm_utils_hmac_md5_free(qm_hmac_md5_context *ctx);

/**
 * @brief Initialize a streaming HMAC-SHA1 context.
 *
 * @param[out] ctx HMAC-SHA1 context
 * @param[in] key HMAC key; may be NULL only when key_len is 0
 * @param[in] key_len HMAC key length in bytes
 * @return QM_EOK on success, negative error code on failure
 */
int qm_utils_hmac_sha1_init(qm_hmac_sha1_context *ctx,
                            const uint8_t *key,
                            uint32_t key_len);

/**
 * @brief Feed data into a streaming HMAC-SHA1 context.
 *
 * @param[in,out] ctx HMAC-SHA1 context initialized by qm_utils_hmac_sha1_init()
 * @param[in] data Data chunk; may be NULL only when data_len is 0
 * @param[in] data_len Data chunk length in bytes
 * @return QM_EOK on success, negative error code on failure
 */
int qm_utils_hmac_sha1_update(qm_hmac_sha1_context *ctx,
                              const uint8_t *data,
                              uint32_t data_len);

/**
 * @brief Finish streaming HMAC-SHA1 and clear the context.
 *
 * @param[in,out] ctx HMAC-SHA1 context
 * @param[out] output Output buffer, QM_UTILS_HMAC_SHA1_DIGEST_LEN bytes
 * @return QM_EOK on success, negative error code on failure
 */
int qm_utils_hmac_sha1_finish(qm_hmac_sha1_context *ctx,
                              uint8_t output[QM_UTILS_HMAC_SHA1_DIGEST_LEN]);

/**
 * @brief Clear a streaming HMAC-SHA1 context.
 *
 * @param[in,out] ctx HMAC-SHA1 context
 */
void qm_utils_hmac_sha1_free(qm_hmac_sha1_context *ctx);

/**
 * @brief Initialize a streaming HMAC-SHA256 context.
 *
 * @param[out] ctx HMAC-SHA256 context
 * @param[in] key HMAC key; may be NULL only when key_len is 0
 * @param[in] key_len HMAC key length in bytes
 * @return QM_EOK on success, negative error code on failure
 */
int qm_utils_hmac_sha256_init(qm_hmac_sha256_context *ctx,
                              const uint8_t *key,
                              uint32_t key_len);

/**
 * @brief Feed data into a streaming HMAC-SHA256 context.
 *
 * @param[in,out] ctx HMAC-SHA256 context initialized by qm_utils_hmac_sha256_init()
 * @param[in] data Data chunk; may be NULL only when data_len is 0
 * @param[in] data_len Data chunk length in bytes
 * @return QM_EOK on success, negative error code on failure
 */
int qm_utils_hmac_sha256_update(qm_hmac_sha256_context *ctx,
                                const uint8_t *data,
                                uint32_t data_len);

/**
 * @brief Finish streaming HMAC-SHA256 and clear the context.
 *
 * @param[in,out] ctx HMAC-SHA256 context
 * @param[out] output Output buffer, QM_UTILS_HMAC_SHA256_DIGEST_LEN bytes
 * @return QM_EOK on success, negative error code on failure
 */
int qm_utils_hmac_sha256_finish(qm_hmac_sha256_context *ctx,
                                uint8_t output[QM_UTILS_HMAC_SHA256_DIGEST_LEN]);

/**
 * @brief Clear a streaming HMAC-SHA256 context.
 *
 * @param[in,out] ctx HMAC-SHA256 context
 */
void qm_utils_hmac_sha256_free(qm_hmac_sha256_context *ctx);

/**
 * @brief Calculate HMAC-SHA256 in one call.
 *
 * @param[in] msg Input message; may be NULL only when msg_len is 0
 * @param[in] msg_len Input message length in bytes
 * @param[out] output Output buffer, QM_UTILS_HMAC_SHA256_DIGEST_LEN bytes
 * @param[in] key HMAC key; may be NULL only when key_len is 0
 * @param[in] key_len HMAC key length in bytes
 * @return QM_EOK on success, negative error code on failure
 */
int qm_utils_hmac_sha256(const uint8_t *msg,
                         uint32_t msg_len,
                         uint8_t output[QM_UTILS_HMAC_SHA256_DIGEST_LEN],
                         const uint8_t *key,
                         uint32_t key_len);

#ifdef __cplusplus
}
#endif

#endif
