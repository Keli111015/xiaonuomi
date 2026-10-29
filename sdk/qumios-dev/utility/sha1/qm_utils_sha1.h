
#ifndef _QM_UTILS_SHA1_H_
#define _QM_UTILS_SHA1_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"

/**
 * \brief          SHA-1 context structure
 */
typedef struct {
    uint32_t total[2];          /*!< number of bytes processed  */
    uint32_t state[5];          /*!< intermediate digest state  */
    unsigned char buffer[64];   /*!< data block being processed */
} qm_sha1_context;

/**
 * \brief          Initialize SHA-1 context
 *
 * \param ctx      SHA-1 context to be initialized
 */
void qm_utils_sha1_init(qm_sha1_context *ctx);

/**
 * \brief          Clear SHA-1 context
 *
 * \param ctx      SHA-1 context to be cleared
 */
void qm_utils_sha1_free(qm_sha1_context *ctx);

/**
 * \brief          Clone (the state of) a SHA-1 context
 *
 * \param dst      The destination context
 * \param src      The context to be cloned
 */
void qm_utils_sha1_clone(qm_sha1_context *dst,
                      const qm_sha1_context *src);

/**
 * \brief          SHA-1 context setup
 *
 * \param ctx      context to be initialized
 */
void qm_utils_sha1_starts(qm_sha1_context *ctx);

/**
 * \brief          SHA-1 process buffer
 *
 * \param ctx      SHA-1 context
 * \param input    buffer holding the  data
 * \param ilen     length of the input data
 */
void qm_utils_sha1_update(qm_sha1_context *ctx, const unsigned char *input, size_t ilen);

/**
 * \brief          SHA-1 final digest
 *
 * \param ctx      SHA-1 context
 * \param output   SHA-1 checksum result
 */
void qm_utils_sha1_finish(qm_sha1_context *ctx, unsigned char output[20]);

/* Internal use */
void qm_utils_sha1_process(qm_sha1_context *ctx, const unsigned char data[64]);

/**
 * \brief          Output = SHA-1( input buffer )
 *
 * \param input    buffer holding the  data
 * \param ilen     length of the input data
 * \param output   SHA-1 checksum result
 */
void qm_utils_sha1(const unsigned char *input, size_t ilen, unsigned char output[20]);

#ifdef __cplusplus
}
#endif

#endif
