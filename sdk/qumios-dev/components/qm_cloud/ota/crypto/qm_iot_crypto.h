/*
 * AWS IoT Jobs v1.5.1
 * Copyright (C) 2023 Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: MIT
 *
 * Licensed under the MIT License. See the LICENSE accompanying this file
 * for the specific language governing permissions and limitations under
 * the License.
 */

#ifndef QM_IOT_CRYPTO_H
#define QM_IOT_CRYPTO_H

#include "qm.h"


/**
 * @brief 用于存储加密哈希计算结果的常用缓冲区大小。
 */
#define QM_OTA_CRYPTO_SHA1_DIGEST_BYTES      20
#define QM_OTA_CRYPTO_SHA256_DIGEST_BYTES    32


/**
 * @brief Library-independent cryptographic algorithm identifiers.
 */
typedef enum
{
    QM_OTA_CRYPTO_HASH_ALGORITHM_SHA1,
    QM_OTA_CRYPTO_HASH_ALGORITHM_SHA256,
}qm_iot_crypto_hash_t;

typedef enum
{
    QM_OTA_CRYPTO_ASYMMETRIC_ALGORITHM_RSA,
    QM_OTA_CRYPTO_ASYMMETRIC_ALGORITHM_ECDSA,
}qm_iot_crypto_asymmetric_t;

/**
 * @brief 初始化加密库。
 */
void *qm_iot_crypto_init(void);

/**
 * @brief 反初始化加密库。
 */
int32_t qm_iot_crypto_deinit(void ** crypto_handle);

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
                                              qm_iot_crypto_asymmetric_t AsymmetricAlgorithm );

/**
 * @brief 使用指定的字节数组更新加密哈希计算。.
 *
 * @param[in] crypto_handle crypto句柄
 * @param[in] pucData 已签名的字节数组.
 * @param[in] xDataLength 已签名的数据的长度（以字节为单位）。
 * 
 * @return 如果初始化成功，则为 QM_EOK，否则失败
 */
int32_t qm_iot_crypto_signature_verification_update(void * crypto_handle, const uint8_t *pucData, int xDataLength);

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
                                                    char * pcSignerCertificate,int xSignerCertificateLength);

#endif