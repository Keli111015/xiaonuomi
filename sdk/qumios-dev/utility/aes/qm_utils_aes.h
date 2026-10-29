#ifndef _QM_UTILS_AES_H
#define _QM_UTILS_AES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"

#define AES_ENCRYPT     1
#define AES_DECRYPT     0

#define ERR_AES_INVALID_KEY_LENGTH                 -0x0800

/**
 * \brief          AES context structure
 */
typedef struct
{
    int32_t nr;                     /*!<  number of rounds  */
    unsigned long *rk;          /*!<  AES round keys    */
    unsigned long buf[68];      /*!<  unaligned data    */
}qm_aes_context;

/**
 * \brief          AES key schedule (encryption)
 *
 * \param ctx      AES context to be initialized
 * \param key      encryption key
 * \param keysize  must be 128, 192 or 256
 *
 * \return         0 if successful, or POLARSSL_ERR_AES_INVALID_KEY_LENGTH
 */
int32_t qm_utils_aes_setkey_enc( qm_aes_context *ctx, uint8_t *key, int32_t keysize );

/**
 * \brief          AES key schedule (decryption)
 *
 * \param ctx      AES context to be initialized
 * \param key      decryption key
 * \param keysize  must be 128, 192 or 256
 *
 * \return         0 if successful, or POLARSSL_ERR_AES_INVALID_KEY_LENGTH
 */
int32_t qm_utils_aes_setkey_dec( qm_aes_context *ctx, uint8_t *key, int32_t keysize );

/**
 * \brief          AES-ECB block encryption/decryption
 *
 * \param ctx      AES context
 * \param mode     AES_ENCRYPT or AES_DECRYPT
 * \param input    16-byte input block
 * \param output   16-byte output block
 */
void qm_utils_aes_crypt_ecb( qm_aes_context *ctx,
                    int32_t mode,
                    uint8_t input[16],
                    uint8_t output[16] );

/**
 * \brief          AES-CBC buffer encryption/decryption
 *                 Length should be a multiple of the block
 *                 size (16 bytes)
 *
 * \param ctx      AES context
 * \param mode     AES_ENCRYPT or AES_DECRYPT
 * \param length   length of the input data
 * \param iv       initialization vector (updated after use)
 * \param input    buffer holding the input data
 * \param output   buffer holding the output data
 */
void qm_utils_aes_crypt_cbc( qm_aes_context *ctx,
                    int32_t mode,
                    int32_t length,
                    uint8_t iv[16],
                    uint8_t *input,
                    uint8_t *output );

/**
 * \brief          AES-CFB128 buffer encryption/decryption.
 *
 * \param ctx      AES context
 * \param mode     AES_ENCRYPT or AES_DECRYPT
 * \param length   length of the input data
 * \param iv_off   offset in IV (updated after use)
 * \param iv       initialization vector (updated after use)
 * \param input    buffer holding the input data
 * \param output   buffer holding the output data
 */
void qm_utils_aes_crypt_cfb128( qm_aes_context *ctx,
                       int32_t mode,
                       int32_t length,
                       int32_t *iv_off,
                       uint8_t iv[16],
                       uint8_t *input,
                       uint8_t *output );


#ifdef __cplusplus
}
#endif

#endif /* aes.h */
