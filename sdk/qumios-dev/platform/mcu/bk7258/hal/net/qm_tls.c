#include "qm.h"
#include "qm_tls.h"
#include "qm_tcp.h"

#include <string.h>

#include <os/os.h>

#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#include "mbedtls/pk.h"

#define LOG_TAG "TLS"
#define QM_TLS_ESTABLISH_TIMEOUT_MS  (15 * 1000)

typedef struct {
    int tcp_fd;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cacert;
    mbedtls_x509_crt clicert;
    mbedtls_pk_context pkey;
} qm_tls_ctx_t;

static int tls_bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    qm_tls_ctx_t *tls = (qm_tls_ctx_t *)ctx;
    int ret;

    if (tls == NULL || buf == NULL) {
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }

    ret = qm_tcp_write(tls->tcp_fd, (const char *)buf, (uint32_t)len, 5000);
    if (ret < 0) {
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    if (ret == 0) {
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    return ret;
}

static int tls_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    qm_tls_ctx_t *tls = (qm_tls_ctx_t *)ctx;
    int ret;

    if (tls == NULL || buf == NULL) {
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }

    ret = qm_tcp_read(tls->tcp_fd, (char *)buf, (uint32_t)len, 5000);
    if (ret < 0) {
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    if (ret == 0) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    return ret;
}

static int tls_bio_recv_timeout(void *ctx, unsigned char *buf, size_t len, uint32_t timeout_ms)
{
    qm_tls_ctx_t *tls = (qm_tls_ctx_t *)ctx;
    int ret;

    if (tls == NULL || buf == NULL) {
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }

    ret = qm_tcp_read(tls->tcp_fd, (char *)buf, (uint32_t)len, timeout_ms);
    if (ret < 0) {
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    if (ret == 0) {
        return MBEDTLS_ERR_SSL_TIMEOUT;
    }
    return ret;
}

static void tls_ctx_free(qm_tls_ctx_t *tls)
{
    if (tls == NULL) {
        return;
    }

    mbedtls_ssl_free(&tls->ssl);
    mbedtls_ssl_config_free(&tls->conf);
    mbedtls_ctr_drbg_free(&tls->ctr_drbg);
    mbedtls_entropy_free(&tls->entropy);
    mbedtls_x509_crt_free(&tls->cacert);
    mbedtls_x509_crt_free(&tls->clicert);
    mbedtls_pk_free(&tls->pkey);

    if (tls->tcp_fd > 0) {
        qm_tcp_destroy(tls->tcp_fd);
        tls->tcp_fd = 0;
    }

    qm_free(tls);
}

void *qm_tls_establish(const char *host, uint16_t port, const qm_tls_cfg_t *cfg)
{
    qm_tls_ctx_t *tls;
    int ret;
    uint32_t start_ms;
    uint32_t hs_start_ms;
    int tcp_handle;

    if (host == NULL) {
        return NULL;
    }

    start_ms = qm_now_ms();

    tls = (qm_tls_ctx_t *)qm_malloc(sizeof(*tls));
    if (tls == NULL) {
        QM_LOGE(LOG_TAG, "malloc tls_ctx failed size=%u free_heap=%u psram_heap=%u",
                (unsigned)sizeof(qm_tls_ctx_t), (unsigned)qm_free_mem_get(),
                (unsigned)rtos_get_psram_free_heap_size());
        return NULL;
    }
    memset(tls, 0, sizeof(*tls));

    tcp_handle = qm_tcp_establish(host, port);
    if (tcp_handle == 0) {
        QM_LOGE(LOG_TAG, "tcp establish failed %s:%u free_heap=%u",
                host, (unsigned)port, (unsigned)qm_free_mem_get());
        tls_ctx_free(tls);
        return NULL;
    }
    tls->tcp_fd = tcp_handle;

    mbedtls_ssl_init(&tls->ssl);
    mbedtls_ssl_config_init(&tls->conf);
    mbedtls_ctr_drbg_init(&tls->ctr_drbg);
    mbedtls_entropy_init(&tls->entropy);
    mbedtls_x509_crt_init(&tls->cacert);
    mbedtls_x509_crt_init(&tls->clicert);
    mbedtls_pk_init(&tls->pkey);

    ret = mbedtls_ctr_drbg_seed(&tls->ctr_drbg, mbedtls_entropy_func, &tls->entropy,
                                (const unsigned char *)"qm_tls_bk7258", 13);
    if (ret != 0) {
        QM_LOGE(LOG_TAG, "ctr_drbg_seed -0x%x free_heap=%u", -ret,
                (unsigned)qm_free_mem_get());
        goto fail;
    }

    if (cfg != NULL && cfg->ca_crt != NULL && cfg->ca_crt[0] != '\0') {
        ret = mbedtls_x509_crt_parse(&tls->cacert,
                                     (const unsigned char *)cfg->ca_crt,
                                     strlen(cfg->ca_crt) + 1);
        if (ret < 0) {
            QM_LOGE(LOG_TAG, "parse ca -0x%x free_heap=%u", -ret,
                    (unsigned)qm_free_mem_get());
            goto fail;
        }
    }

    if (cfg != NULL && cfg->client_crt != NULL && cfg->client_key != NULL &&
        cfg->client_crt[0] != '\0' && cfg->client_key[0] != '\0') {
        ret = mbedtls_x509_crt_parse(&tls->clicert,
                                     (const unsigned char *)cfg->client_crt,
                                     strlen(cfg->client_crt) + 1);
        if (ret != 0) {
            QM_LOGE(LOG_TAG, "parse client crt -0x%x free_heap=%u", -ret,
                    (unsigned)qm_free_mem_get());
            goto fail;
        }
        ret = mbedtls_pk_parse_key(&tls->pkey,
                                   (const unsigned char *)cfg->client_key,
                                   strlen(cfg->client_key) + 1,
                                   NULL, 0);
        if (ret != 0) {
            QM_LOGE(LOG_TAG, "parse client key -0x%x free_heap=%u", -ret,
                    (unsigned)qm_free_mem_get());
            goto fail;
        }
    }

    ret = mbedtls_ssl_config_defaults(&tls->conf, MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        QM_LOGE(LOG_TAG, "ssl_config_defaults -0x%x free_heap=%u", -ret,
                (unsigned)qm_free_mem_get());
        goto fail;
    }

    mbedtls_ssl_conf_authmode(&tls->conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
    mbedtls_ssl_conf_rng(&tls->conf, mbedtls_ctr_drbg_random, &tls->ctr_drbg);
    mbedtls_ssl_conf_ca_chain(&tls->conf, &tls->cacert, NULL);
    mbedtls_ssl_conf_read_timeout(&tls->conf, 5000);

    if (cfg != NULL && cfg->client_crt != NULL && cfg->client_key != NULL &&
        cfg->client_crt[0] != '\0' && cfg->client_key[0] != '\0') {
        ret = mbedtls_ssl_conf_own_cert(&tls->conf, &tls->clicert, &tls->pkey);
        if (ret != 0) {
            QM_LOGE(LOG_TAG, "ssl_conf_own_cert -0x%x free_heap=%u", -ret,
                    (unsigned)qm_free_mem_get());
            goto fail;
        }
    }

    ret = mbedtls_ssl_setup(&tls->ssl, &tls->conf);
    if (ret != 0) {
        QM_LOGE(LOG_TAG, "ssl_setup -0x%x free_heap=%u", -ret,
                (unsigned)qm_free_mem_get());
        goto fail;
    }

    ret = mbedtls_ssl_set_hostname(&tls->ssl, host);
    if (ret != 0) {
        QM_LOGE(LOG_TAG, "ssl_set_hostname -0x%x free_heap=%u", -ret,
                (unsigned)qm_free_mem_get());
        goto fail;
    }

    mbedtls_ssl_set_bio(&tls->ssl, tls, tls_bio_send, tls_bio_recv, tls_bio_recv_timeout);

    hs_start_ms = qm_now_ms();
    while ((ret = mbedtls_ssl_handshake(&tls->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            QM_LOGE(LOG_TAG, "handshake -0x%x free_heap=%u", -ret,
                    (unsigned)qm_free_mem_get());
            goto fail;
        }
        if ((qm_now_ms() - hs_start_ms) > QM_TLS_ESTABLISH_TIMEOUT_MS) {
            QM_LOGE(LOG_TAG, "handshake timeout free_heap=%u",
                    (unsigned)qm_free_mem_get());
            goto fail;
        }
    }

    QM_LOGI(LOG_TAG, "tls ok %s:%u cost=%ums free_heap=%u",
            host, (unsigned)port, (unsigned)(qm_now_ms() - start_ms),
            (unsigned)qm_free_mem_get());
    return tls;

fail:
    QM_LOGE(LOG_TAG, "establish fail %s:%u free_heap=%u",
            host, (unsigned)port, (unsigned)qm_free_mem_get());
    tls_ctx_free(tls);
    return NULL;
}

void *qm_tls_accept(int socket, const qm_tls_server_cfg_t *cfg, uint32_t timeout_ms)
{
    (void)socket;
    (void)cfg;
    (void)timeout_ms;
    return NULL;
}

int32_t qm_tls_destroy(void *handle)
{
    if (handle == NULL) {
        return -QM_EINVAL;
    }

    tls_ctx_free((qm_tls_ctx_t *)handle);
    return QM_EOK;
}

int32_t qm_tls_write(void *handle, const char *buf, int len, uint32_t timeout_ms)
{
    qm_tls_ctx_t *tls = (qm_tls_ctx_t *)handle;
    uint32_t start_ms;
    int written = 0;
    int ret;

    (void)timeout_ms;

    if (tls == NULL || buf == NULL || len <= 0) {
        return -QM_EINVAL;
    }

    start_ms = qm_now_ms();
    while (written < len) {
        ret = mbedtls_ssl_write(&tls->ssl, (const unsigned char *)buf + written,
                                (size_t)(len - written));
        if (ret > 0) {
            written += ret;
            continue;
        }
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            return -QM_EIO;
        }
        if ((qm_now_ms() - start_ms) > timeout_ms) {
            break;
        }
    }

    return written;
}

int32_t qm_tls_read(void *handle, char *buf, int len, uint32_t timeout_ms)
{
    qm_tls_ctx_t *tls = (qm_tls_ctx_t *)handle;
    uint32_t start_ms;
    int ret;

    if (tls == NULL || buf == NULL || len <= 0) {
        return -2;
    }

    mbedtls_ssl_conf_read_timeout(&tls->conf, timeout_ms);
    start_ms = qm_now_ms();

    do {
        ret = mbedtls_ssl_read(&tls->ssl, (unsigned char *)buf, (size_t)len);
        if (ret > 0) {
            return ret;
        }
        if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            return -1;
        }
        if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
            ret != MBEDTLS_ERR_SSL_WANT_WRITE &&
            ret != MBEDTLS_ERR_SSL_TIMEOUT) {
            return -2;
        }
    } while ((qm_now_ms() - start_ms) < timeout_ms);

    return 0;
}
