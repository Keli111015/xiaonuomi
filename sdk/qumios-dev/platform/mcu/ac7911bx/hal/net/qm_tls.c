/*
 * qm_tls.c - TLS HAL for JieLi WL82/AC79 platform
 *
 * Uses JL SDK's libmbedtls_3_4_0.a.
 * Header path: fw-AC79_AIoT_SDK/include_lib/net/mbedtls_3_4_0
 *
 * Key mbedtls 3.x differences from 2.x:
 *   - mbedtls_pk_parse_key() takes extra f_rng / p_rng arguments
 *   - Include "mbedtls/net_sockets.h" instead of "mbedtls/net.h"
 *   - "mbedtls/config.h" is now "mbedtls/mbedtls_config.h"
 */

/* Avoid JL cpu.h redefining bool (conflicts with stdbool.h from mbedtls toolchain) */
#define BOOL_DEFINE_CONFLICT

#include "qm.h"
#include "qm_tls.h"
#include "qm_utils_timer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mbedtls/mbedtls_config.h"
#include "mbedtls/platform.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#include "mbedtls/debug.h"
#include "mbedtls/pk.h"

#define LOG_TAG "TLS"

#define QM_TLS_ESTABLISH_TIMEOUT    (10 * 1000)

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

typedef struct {
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       conf;
    uint32_t                 flags;
    mbedtls_x509_crt         cacert;
    mbedtls_x509_crt         clicert;
    mbedtls_pk_context       pkey;
    mbedtls_net_context      server_fd;
} qm_tls_hal_data_t;

typedef struct {
    const char         *host;
    uint16_t            port;
    uint32_t            timeout_ms;
    const char         *ca_crt;
    const char         *client_crt;
    const char         *client_key;
    qm_tls_hal_data_t   data;
} qm_tls_hal_handle_t;

void *qm_tls_establish(const char *host, uint16_t port, const qm_tls_cfg_t *cfg)
{
    int  ret;
    char port_str[6];
    qm_tls_hal_data_t *d;
    int ca_crt_len = 0;

    if (host == NULL || cfg == NULL) {
        return NULL;
    }

    qm_tls_hal_handle_t *h = (qm_tls_hal_handle_t *)qm_malloc(sizeof(qm_tls_hal_handle_t));
    if (h == NULL) {
        return NULL;
    }
    memset(h, 0, sizeof(qm_tls_hal_handle_t));

    h->host       = host;
    h->port       = port;
    h->timeout_ms = QM_TLS_ESTABLISH_TIMEOUT;
    h->ca_crt     = cfg->ca_crt;
    h->client_crt = cfg->client_crt;
    h->client_key = cfg->client_key;

    d = &h->data;
    if (h->ca_crt) {
        ca_crt_len = (int)strlen(h->ca_crt) + 1;
    }

    mbedtls_net_init(&d->server_fd);
    mbedtls_ssl_init(&d->ssl);
    mbedtls_ssl_config_init(&d->conf);
    mbedtls_ctr_drbg_init(&d->ctr_drbg);
    mbedtls_x509_crt_init(&d->cacert);
    mbedtls_x509_crt_init(&d->clicert);
    mbedtls_pk_init(&d->pkey);
    mbedtls_entropy_init(&d->entropy);

    ret = mbedtls_ctr_drbg_seed(&d->ctr_drbg, mbedtls_entropy_func, &d->entropy,
                                 (const unsigned char *)LOG_TAG, strlen(LOG_TAG));
    if (ret != 0) {
        QM_LOGE(LOG_TAG, "ctr_drbg_seed failed -0x%x", -ret);
        goto fail;
    }

    /* ca_crt == NULL is a valid configuration for platform/default trust. */
    if (ca_crt_len > 0) {
        ret = mbedtls_x509_crt_parse(&d->cacert,
                                      (const unsigned char *)h->ca_crt,
                                      (size_t)ca_crt_len);
        if (ret < 0) {
            QM_LOGE(LOG_TAG, "x509_crt_parse CA failed -0x%x", -ret);
            goto fail;
        }
    }

    /* Load client certificate (optional) */
    if (h->client_crt) {
        ret = mbedtls_x509_crt_parse(&d->clicert,
                                      (const unsigned char *)h->client_crt,
                                      strlen(h->client_crt) + 1);
        if (ret != 0) {
            QM_LOGE(LOG_TAG, "x509_crt_parse client cert failed -0x%x", -ret);
            goto fail;
        }
    }

    /* Load client private key (optional, mbedtls 3.x requires f_rng/p_rng) */
    if (h->client_key) {
        ret = mbedtls_pk_parse_key(&d->pkey,
                                    (const unsigned char *)h->client_key,
                                    strlen(h->client_key) + 1,
                                    (const unsigned char *)"", 0,
                                    mbedtls_ctr_drbg_random, &d->ctr_drbg);
        if (ret != 0) {
            QM_LOGE(LOG_TAG, "pk_parse_key failed -0x%x", -ret);
            goto fail;
        }
    }

    /* TCP connect */
    snprintf(port_str, sizeof(port_str), "%u", port);
    ret = mbedtls_net_connect(&d->server_fd, host, port_str, MBEDTLS_NET_PROTO_TCP);
    if (ret != 0) {
        QM_LOGE(LOG_TAG, "net_connect failed -0x%x", -ret);
        goto fail;
    }

    ret = mbedtls_net_set_block(&d->server_fd);
    if (ret != 0) {
        QM_LOGE(LOG_TAG, "net_set_block failed -0x%x", -ret);
        goto fail;
    }

    ret = mbedtls_ssl_config_defaults(&d->conf,
                                       MBEDTLS_SSL_IS_CLIENT,
                                       MBEDTLS_SSL_TRANSPORT_STREAM,
                                       MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        QM_LOGE(LOG_TAG, "ssl_config_defaults failed -0x%x", -ret);
        goto fail;
    }

    mbedtls_ssl_conf_authmode(&d->conf,
                              ca_crt_len > 0 ? MBEDTLS_SSL_VERIFY_REQUIRED
                                             : MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&d->conf, mbedtls_ctr_drbg_random, &d->ctr_drbg);
    if (ca_crt_len > 0) {
        mbedtls_ssl_conf_ca_chain(&d->conf, &d->cacert, NULL);
    }
    mbedtls_ssl_conf_read_timeout(&d->conf, h->timeout_ms);

    if (h->client_crt && h->client_key) {
        ret = mbedtls_ssl_conf_own_cert(&d->conf, &d->clicert, &d->pkey);
        if (ret != 0) {
            QM_LOGE(LOG_TAG, "ssl_conf_own_cert failed %d", ret);
            goto fail;
        }
    }

    ret = mbedtls_ssl_setup(&d->ssl, &d->conf);
    if (ret != 0) {
        QM_LOGE(LOG_TAG, "ssl_setup failed -0x%x", -ret);
        goto fail;
    }

    ret = mbedtls_ssl_set_hostname(&d->ssl, host);
    if (ret != 0) {
        QM_LOGE(LOG_TAG, "ssl_set_hostname failed %d", ret);
        goto fail;
    }

    mbedtls_ssl_set_bio(&d->ssl, &d->server_fd,
                         mbedtls_net_send, NULL, mbedtls_net_recv_timeout);

    /* TLS handshake */
    while ((ret = mbedtls_ssl_handshake(&d->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            QM_LOGE(LOG_TAG, "ssl_handshake failed -0x%x", -ret);
            goto fail;
        }
    }

    d->flags = mbedtls_ssl_get_verify_result(&d->ssl);
    if (ca_crt_len > 0 && d->flags != 0) {
        QM_LOGE(LOG_TAG, "certificate verify failed flags=0x%x", d->flags);
        goto fail;
    }

    QM_LOGI(LOG_TAG, "TLS connected [%s] [%s]",
            mbedtls_ssl_get_version(&d->ssl),
            mbedtls_ssl_get_ciphersuite(&d->ssl));

    return (void *)h;

fail:
    mbedtls_net_free(&d->server_fd);
    mbedtls_x509_crt_free(&d->clicert);
    mbedtls_x509_crt_free(&d->cacert);
    mbedtls_pk_free(&d->pkey);
    mbedtls_ssl_free(&d->ssl);
    mbedtls_ssl_config_free(&d->conf);
    mbedtls_ctr_drbg_free(&d->ctr_drbg);
    mbedtls_entropy_free(&d->entropy);
    qm_free(h);
    return NULL;
}

int32_t qm_tls_write(void *handle, const char *buf, int len, uint32_t timeout_ms)
{
    int ret;
    size_t written = 0;
    bool error = false;
    qm_utils_time_t timer = {0};
    qm_tls_hal_handle_t *h = (qm_tls_hal_handle_t *)handle;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    qm_utils_time_countdown_ms(&timer, timeout_ms);

    while (written < (size_t)len && !qm_utils_time_is_expired(&timer)) {
        while (!qm_utils_time_is_expired(&timer) &&
               (ret = mbedtls_ssl_write(&h->data.ssl,
                                         (const unsigned char *)(buf + written),
                                         len - written)) <= 0) {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                QM_LOGE(LOG_TAG, "ssl_write failed -0x%x", -ret);
                error = true;
                break;
            }
        }
        if (error) break;
        written += ret;
    }

    return (written > 0) ? (int32_t)written : -QM_ERROR;
}

int32_t qm_tls_read(void *handle, char *buf, int len, uint32_t timeout_ms)
{
    int ret;
    size_t rxLen = 0;
    uint32_t read_timeout;
    qm_utils_time_t timer = {0};
    qm_tls_hal_handle_t *h = (qm_tls_hal_handle_t *)handle;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    qm_utils_time_countdown_ms(&timer, timeout_ms);
    read_timeout = h->data.conf.read_timeout;

    while (len > 0) {
        mbedtls_ssl_conf_read_timeout(&h->data.conf,
                                       MAX(1u, MIN(read_timeout,
                                                   (uint32_t)qm_utils_time_left(&timer))));
        ret = mbedtls_ssl_read(&h->data.ssl, (unsigned char *)buf, len);
        mbedtls_ssl_conf_read_timeout(&h->data.conf, read_timeout);

        if (ret > 0) {
            rxLen += ret;
            buf   += ret;
            len   -= ret;
        } else if (ret == 0 ||
                   (ret != MBEDTLS_ERR_SSL_WANT_READ  &&
                    ret != MBEDTLS_ERR_SSL_WANT_WRITE &&
                    ret != MBEDTLS_ERR_SSL_TIMEOUT)) {
            return 0;
        }

        if (qm_utils_time_is_expired(&timer)) {
            break;
        }
    }

    return (int32_t)rxLen;
}

int32_t qm_tls_destroy(void *handle)
{
    int ret, count = 0;
    qm_tls_hal_handle_t *h = (qm_tls_hal_handle_t *)handle;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    do {
        ret = mbedtls_ssl_close_notify(&h->data.ssl);
    } while (ret == MBEDTLS_ERR_SSL_WANT_WRITE && ++count <= 5);

    mbedtls_net_free(&h->data.server_fd);
    mbedtls_x509_crt_free(&h->data.clicert);
    mbedtls_x509_crt_free(&h->data.cacert);
    mbedtls_pk_free(&h->data.pkey);
    mbedtls_ssl_free(&h->data.ssl);
    mbedtls_ssl_config_free(&h->data.conf);
    mbedtls_ctr_drbg_free(&h->data.ctr_drbg);
    mbedtls_entropy_free(&h->data.entropy);

    qm_free(h);
    return QM_EOK;
}
