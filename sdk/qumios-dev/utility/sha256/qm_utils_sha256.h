#ifndef _QM_UTILS_SHA256_H_
#define _QM_UTILS_SHA256_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"


#define SHA256_BLOCK_LENGTH     64
#define SHA256_DIGEST_LENGTH    32
#define SHA256_DIGEST_STRING_LENGTH (SHA256_DIGEST_LENGTH * 2 + 1)

#define SHA256_SHORT_BLOCK_LENGTH   (SHA256_BLOCK_LENGTH - 8)
typedef struct {
    uint32_t state[8];
    uint64_t bitcount;
    unsigned char buffer[SHA256_BLOCK_LENGTH];
} qm_sha256_context;
typedef union {
    char sptr[8];
    uint64_t lint;
} u_retLen;

/**
 * \brief          Initialize SHA-256 context
 *
 * \param ctx      SHA-256 context to be initialized
 */
void qm_utils_sha256_init(qm_sha256_context *ctx);

/**
 * \brief          Clear SHA-256 context
 *
 * \param ctx      SHA-256 context to be cleared
 */
void qm_utils_sha256_free(qm_sha256_context *ctx);

/**
 * \brief          Clone (the state of) a SHA-256 context
 *
 * \param dst      The destination context
 * \param src      The context to be cloned
 */
void qm_utils_sha256_clone(qm_sha256_context *dst,
                        const qm_sha256_context *src);

/**
 * \brief          SHA-256 context setup
 *
 * \param ctx      context to be initialized
 */
void qm_utils_sha256_starts(qm_sha256_context *ctx);

/**
 * \brief          SHA-256 process buffer
 *
 * \param ctx      SHA-256 context
 * \param input    buffer holding the  data
 * \param ilen     length of the input data
 */
void qm_utils_sha256_update(qm_sha256_context *ctx, const unsigned char *input, uint32_t ilen);

/**
 * \brief          SHA-256 final digest
 *
 * \param ctx      SHA-256 context
 * \param output   SHA-256 checksum result
 */
void qm_utils_sha256_finish(qm_sha256_context *ctx, unsigned char output[32]);

/* Internal use */
void qm_utils_sha256_process(qm_sha256_context *ctx, const uint32_t *data);

/**
 * \brief          Output = SHA-256( input buffer )
 *
 * \param input    buffer holding the  data
 * \param ilen     length of the input data
 * \param output   SHA-256 checksum result
 */
void qm_utils_sha256(const unsigned char *input, uint32_t ilen, unsigned char output[32]);

#ifdef __cplusplus
}
#endif

#endif
