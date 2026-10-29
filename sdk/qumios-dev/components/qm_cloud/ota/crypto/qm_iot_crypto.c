#include "qm_cloud_log.h"
#include "qm.h"
#include "qm_iot_crypto.h"

#include "qm_utils_sha1.h"
#include "qm_utils_sha256.h"

#include "mbedtls/platform.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"

typedef struct 
{
    qm_iot_crypto_hash_t hash_crypto;
    qm_iot_crypto_asymmetric_t asymmetric_crypto;
    
    qm_sha1_context sha1_context;
    qm_sha256_context sha256_context;

}qm_iot_crypto_handle_t;

/**
 * @brief 初始化加密库。
 */
void *qm_iot_crypto_init(void)
{
    qm_iot_crypto_handle_t *iot_crypto_handle = (qm_iot_crypto_handle_t *)qm_malloc(sizeof(qm_iot_crypto_handle_t));
    if(iot_crypto_handle == NULL){
        return NULL;
    }
    memset(iot_crypto_handle, 0, sizeof(qm_iot_crypto_handle_t));

    return iot_crypto_handle;
}

/**
 * @brief 反初始化加密库。
 */
int32_t qm_iot_crypto_deinit(void ** crypto_handle)
{
    qm_iot_crypto_handle_t *iot_crypto_handle = NULL;
    if(crypto_handle == NULL){
        return -QM_EINVAL;
    }

    iot_crypto_handle = (qm_iot_crypto_handle_t *)(*crypto_handle);
    if(iot_crypto_handle == NULL){
        return -QM_EINVAL;
    }

    qm_free(iot_crypto_handle);

    *crypto_handle = NULL;

    return QM_EOK;
}

/**
 * @brief 初始化数字签名验证
 *
 * @param[out] crypto_handle crypto句柄
 * @param[in] xAsymmetricAlgorithm 加密公钥密码系统.
 * @param[in] xHashAlgorithm 用于签名的加密哈希算法。.
 *
 * @return 如果初始化成功，则为 QM_EOK，否则失败
 */
int32_t qm_iot_crypto_signature_verification_start(void *crypto_handle,
                                              qm_iot_crypto_hash_t xHashAlgorithmx,
                                              qm_iot_crypto_asymmetric_t AsymmetricAlgorithm )
{
    qm_iot_crypto_handle_t *iot_crypto_handle = NULL;
    if (crypto_handle == NULL){
        return -QM_EINVAL;
    }

    iot_crypto_handle = (qm_iot_crypto_handle_t *)crypto_handle;
    /*
    * 存储算法标识符
    */  
    iot_crypto_handle->hash_crypto = xHashAlgorithmx;
    iot_crypto_handle->asymmetric_crypto = AsymmetricAlgorithm;

        /*
        * Initialize the requested hash type
        */
    if( QM_OTA_CRYPTO_HASH_ALGORITHM_SHA1 == iot_crypto_handle->hash_crypto){
        qm_utils_sha1_init(&iot_crypto_handle->sha1_context);
        qm_utils_sha1_starts(&iot_crypto_handle->sha1_context);
    }else{
        qm_utils_sha256_init(&iot_crypto_handle->sha256_context);
        qm_utils_sha256_starts(&iot_crypto_handle->sha256_context);
    }

    return QM_EOK;
}
                

/**
 * @brief 使用指定的字节数组更新加密哈希计算。.
 *
 * @param[in] crypto_handle crypto句柄
 * @param[in] pucData 已签名的字节数组.
 * @param[in] xDataLength 已签名的数据的长度（以字节为单位）。
 * 
 * @return 如果初始化成功，则为 QM_EOK，否则失败
 */
int32_t qm_iot_crypto_signature_verification_update(void * crypto_handle, const uint8_t *pucData, int xDataLength)
{
    qm_iot_crypto_handle_t *iot_crypto_handle = NULL;
    if (crypto_handle == NULL){
        return -QM_EINVAL;
    }

    iot_crypto_handle = (qm_iot_crypto_handle_t *)crypto_handle;

    /*
     * Add the data to the hash of the requested type
     */
    if( QM_OTA_CRYPTO_HASH_ALGORITHM_SHA1 == iot_crypto_handle->hash_crypto){
        qm_utils_sha1_update(&iot_crypto_handle->sha1_context,  pucData, xDataLength);
    }else{
        qm_utils_sha256_update(&iot_crypto_handle->sha256_context,  pucData, xDataLength);
    }

    return QM_EOK;
}


/**
 * @brief Verifies a cryptographic signature based on the signer
 * certificate, hash algorithm, and the data that was signed.
 */
static int prvVerifySignature( char * pcSignerCertificate,
                                    size_t xSignerCertificateLength,
                                    qm_iot_crypto_hash_t xHashAlgorithm,
                                    uint8_t * pucHash,
                                    size_t xHashLength,
                                    uint8_t * pucSignature,
                                    size_t xSignatureLength )
{
    int xResult = QM_EOK;
    mbedtls_md_type_t xMbedHashAlg = MBEDTLS_MD_NONE;

    mbedtls_x509_crt *xCertCtx  = (mbedtls_x509_crt *)qm_malloc(sizeof(mbedtls_x509_crt));
    if(xCertCtx == NULL){
        return -QM_EINVAL;
    }
    memset(xCertCtx, 0, sizeof(mbedtls_x509_crt));

    /*
     * Map the hash algorithm
     */
    if( QM_OTA_CRYPTO_HASH_ALGORITHM_SHA1 == xHashAlgorithm ){
        xMbedHashAlg = MBEDTLS_MD_SHA1;
    }else{
        xMbedHashAlg = MBEDTLS_MD_SHA256;
    }

    /*
     * Decode and create a certificate context
     */
    mbedtls_x509_crt_init( xCertCtx );

    xResult = mbedtls_pk_parse_public_key(&xCertCtx->pk, 
                                    (const unsigned char *)pcSignerCertificate, xSignerCertificateLength + 1);
    
    if (xResult < 0) {
        QM_CLOUD_LOGE("1", "mbedtls_pk_parse_public_key returned -0x%04X", -xResult);
        goto __exit;
    }else if (xResult > 0) {
        /* This will happen if the CA chain contains one or more invalid certs, going ahead as the hadshake
         * may still succeed if the other certificates in the CA chain are enough for the authentication */
        QM_CLOUD_LOGE("1", "mbedtls_pk_parse_public_key was partly successful. No. of failed certificates: %d", xResult);
        goto __exit;
    }

    /*
     * Verify the signature using the public key from the decoded certificate
     */
    xResult = mbedtls_pk_verify(&xCertCtx->pk, xMbedHashAlg,
                                pucHash, xHashLength,
                                pucSignature, xSignatureLength);
    if(xResult != 0) {
        /* This will happen if the CA chain contains one or more invalid certs, going ahead as the hadshake
         * may still succeed if the other certificates in the CA chain are enough for the authentication */
        QM_CLOUD_LOGE("1", "mbedtls_pk_verify failed: -0x%04X", -xResult);
        goto __exit;
    }

__exit:
    /*
     * Clean-up
     */
    mbedtls_x509_crt_free(xCertCtx);
    qm_free(xCertCtx);
    xCertCtx = NULL;

    return xResult;
}

/**
 * @brief 使用指定证书中的公钥验证数字签名计算。
 *
 * @param[in] crypto_handle crypto句柄
 * @param[in] pucSignature 要验证的数字签名结果。
 * @param[in] xSignatureLength 数字签名结果（以字节为单位）。
 * @param[in] pucSignerCertificate  Base64 和 DER 编码的 X.509 证书
 * @param[in] xSignerCertificateLength 证书的长度（以字节为单位）。
 *
 * @return pdTRUE if the signature is correct or pdFALSE if the signature is invalid.
 */
int32_t qm_iot_crypto_verify_finish(void * crypto_handle, 
                                                    uint8_t * pucSignature, int xSignatureLength,
                                                    char * pcSignerCertificate,int xSignerCertificateLength)
{

    size_t xHashLength = 0;
    uint8_t * pucHash = NULL;
    qm_iot_crypto_handle_t *iot_crypto_handle = NULL;
    uint8_t ucSHA1or256[ QM_OTA_CRYPTO_SHA256_DIGEST_BYTES ] = {0};  
    if (crypto_handle == NULL ||
        pucSignature == NULL || xSignatureLength == 0 ||
        pcSignerCertificate == NULL || xSignerCertificateLength == 0){

        return -QM_EINVAL;
    }

    iot_crypto_handle = (qm_iot_crypto_handle_t *)crypto_handle;
    /*
        * Finish the hash
        */
    if( QM_OTA_CRYPTO_HASH_ALGORITHM_SHA1 == iot_crypto_handle->hash_crypto){
        qm_utils_sha1_finish(&iot_crypto_handle->sha1_context,  ucSHA1or256);
        pucHash = ucSHA1or256;
        xHashLength = QM_OTA_CRYPTO_SHA1_DIGEST_BYTES;
    }else{
        qm_utils_sha256_finish(&iot_crypto_handle->sha256_context,  ucSHA1or256);
        pucHash = ucSHA1or256;

        xHashLength = QM_OTA_CRYPTO_SHA256_DIGEST_BYTES;    
    }

    return prvVerifySignature(pcSignerCertificate,
                                xSignerCertificateLength,
                                iot_crypto_handle->hash_crypto,
                                pucHash,
                                xHashLength,
                                pucSignature,
                                xSignatureLength);
}