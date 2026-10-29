/*
 * SH510 OTA 验签：板端 libmbedtls.a 无 pk/x509，用 OpenSSL 1.1。
 * 其它 MCU 仍用 components/qm_cloud/ota/crypto/qm_iot_crypto.c。
 */
#include "qm.h"
#include "qm_iot_crypto.h"
#include "qm_utils_sha1.h"
#include "qm_utils_sha256.h"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <string.h>

typedef struct {
    qm_iot_crypto_hash_t hash_crypto;
    qm_iot_crypto_asymmetric_t asymmetric_crypto;
    qm_sha1_context sha1_context;
    qm_sha256_context sha256_context;
} qm_iot_crypto_handle_t;

void *qm_iot_crypto_init(void)
{
    qm_iot_crypto_handle_t *h = (qm_iot_crypto_handle_t *)qm_malloc(sizeof(*h));

    if (h == NULL) {
        return NULL;
    }
    memset(h, 0, sizeof(*h));
    return h;
}

int32_t qm_iot_crypto_deinit(void **crypto_handle)
{
    if (crypto_handle == NULL || *crypto_handle == NULL) {
        return -QM_EINVAL;
    }
    qm_free(*crypto_handle);
    *crypto_handle = NULL;
    return QM_EOK;
}

int32_t qm_iot_crypto_signature_verification_start(void *crypto_handle,
                                                   qm_iot_crypto_hash_t xHashAlgorithmx,
                                                   qm_iot_crypto_asymmetric_t AsymmetricAlgorithm)
{
    qm_iot_crypto_handle_t *h = (qm_iot_crypto_handle_t *)crypto_handle;

    if (h == NULL) {
        return -QM_EINVAL;
    }

    h->hash_crypto = xHashAlgorithmx;
    h->asymmetric_crypto = AsymmetricAlgorithm;

    if (xHashAlgorithmx == QM_OTA_CRYPTO_HASH_ALGORITHM_SHA1) {
        qm_utils_sha1_init(&h->sha1_context);
        qm_utils_sha1_starts(&h->sha1_context);
    } else {
        qm_utils_sha256_init(&h->sha256_context);
        qm_utils_sha256_starts(&h->sha256_context);
    }
    return QM_EOK;
}

int32_t qm_iot_crypto_signature_verification_update(void *crypto_handle,
                                                    const uint8_t *pucData,
                                                    int xDataLength)
{
    qm_iot_crypto_handle_t *h = (qm_iot_crypto_handle_t *)crypto_handle;

    if (h == NULL || pucData == NULL || xDataLength <= 0) {
        return -QM_EINVAL;
    }

    if (h->hash_crypto == QM_OTA_CRYPTO_HASH_ALGORITHM_SHA1) {
        qm_utils_sha1_update(&h->sha1_context, pucData, (uint32_t)xDataLength);
    } else {
        qm_utils_sha256_update(&h->sha256_context, pucData, (uint32_t)xDataLength);
    }
    return QM_EOK;
}

int32_t qm_iot_crypto_verify_finish(void *crypto_handle,
                                    uint8_t *pucSignature,
                                    int xSignatureLength,
                                    char *pcSignerCertificate,
                                    int xSignerCertificateLength)
{
    qm_iot_crypto_handle_t *h = (qm_iot_crypto_handle_t *)crypto_handle;
    uint8_t hash[QM_OTA_CRYPTO_SHA256_DIGEST_BYTES];
    size_t hash_len;
    BIO *bio = NULL;
    EVP_PKEY *pkey = NULL;
    EVP_MD_CTX *md_ctx = NULL;
    const EVP_MD *md;
    int ret = -QM_EIO;

    if (h == NULL || pucSignature == NULL || xSignatureLength <= 0 ||
        pcSignerCertificate == NULL || xSignerCertificateLength <= 0) {
        return -QM_EINVAL;
    }

    if (h->hash_crypto == QM_OTA_CRYPTO_HASH_ALGORITHM_SHA1) {
        qm_utils_sha1_finish(&h->sha1_context, hash);
        hash_len = QM_OTA_CRYPTO_SHA1_DIGEST_BYTES;
        md = EVP_sha1();
    } else {
        qm_utils_sha256_finish(&h->sha256_context, hash);
        hash_len = QM_OTA_CRYPTO_SHA256_DIGEST_BYTES;
        md = EVP_sha256();
    }

    bio = BIO_new_mem_buf(pcSignerCertificate, xSignerCertificateLength);
    if (bio == NULL) {
        return -QM_ENOMEM;
    }
    pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (pkey == NULL) {
        return -QM_EINVAL;
    }

    md_ctx = EVP_MD_CTX_new();
    if (md_ctx == NULL) {
        EVP_PKEY_free(pkey);
        return -QM_ENOMEM;
    }

    if (EVP_DigestVerifyInit(md_ctx, NULL, md, NULL, pkey) == 1 &&
        EVP_DigestVerify(md_ctx, pucSignature, (size_t)xSignatureLength,
                         hash, hash_len) == 1) {
        ret = QM_EOK;
    }

    EVP_MD_CTX_free(md_ctx);
    EVP_PKEY_free(pkey);
    return ret;
}
