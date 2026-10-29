#ifndef _QM_UTILS_MD5_H_
#define _QM_UTILS_MD5_H_


#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"


#ifdef BUILD_QMOS

#include "mbedtls/md5.h"

#define qm_md5_context       mbedtls_md5_context

#define qm_utils_md5_init        mbedtls_md5_init
#define qm_utils_md5_free        mbedtls_md5_free
#define qm_utils_md5_clone       mbedtls_md5_clone
#define qm_utils_md5_starts      mbedtls_md5_starts
#define qm_utils_md5_update      mbedtls_md5_update
#define qm_utils_md5_finish      mbedtls_md5_finish
#define qm_utils_md5_process     mbedtls_md5_process
#define qm_utils_md5             mbedtls_md5

int8_t qm_utils_hb2hex(uint8_t hb);

void qm_utils_md5_hexstr(unsigned char input[16],unsigned char output[32]);

#else

typedef struct {
    uint32_t total[2];          /*!< number of bytes processed  */
    uint32_t state[4];          /*!< intermediate digest state  */
    unsigned char buffer[64];   /*!< data block being processed */
} qm_md5_context;

/**
 * \brief          Initialize MD5 context
 *
 * \param ctx      MD5 context to be initialized
 */
void qm_utils_md5_init(qm_md5_context *ctx);

/**
 * \brief          Clear MD5 context
 *
 * \param ctx      MD5 context to be cleared
 */
void qm_utils_md5_free(qm_md5_context *ctx);

/**
 * \brief          Clone (the state of) an MD5 context
 *
 * \param dst      The destination context
 * \param src      The context to be cloned
 */
void qm_utils_md5_clone(qm_md5_context *dst,
                     const qm_md5_context *src);

/**
 * \brief          MD5 context setup
 *
 * \param ctx      context to be initialized
 */
void qm_utils_md5_starts(qm_md5_context *ctx);

/**
 * \brief          MD5 process buffer
 *
 * \param ctx      MD5 context
 * \param input    buffer holding the  data
 * \param ilen     length of the input data
 */
void qm_utils_md5_update(qm_md5_context *ctx, const unsigned char *input, size_t ilen);

/**
 * \brief          MD5 final digest
 *
 * \param ctx      MD5 context
 * \param output   MD5 checksum result
 */
void qm_utils_md5_finish(qm_md5_context *ctx, unsigned char output[16]);

/* Internal use */
void qm_utils_md5_process(qm_md5_context *ctx, const unsigned char data[64]);

/**
 * \brief          Output = MD5( input buffer )
 *
 * \param input    buffer holding the  data
 * \param ilen     length of the input data
 * \param output   MD5 checksum result
 */
void qm_utils_md5(const unsigned char *input, size_t ilen, unsigned char output[16]);


int8_t qm_utils_hb2hex(uint8_t hb);

void qm_utils_md5_hexstr(unsigned char input[16], unsigned char output[32]);

#endif

#ifdef __cplusplus
}
#endif

#endif
