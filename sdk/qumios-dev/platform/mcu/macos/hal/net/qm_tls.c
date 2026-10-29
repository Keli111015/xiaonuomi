#include "qm_tls.h"
#include "qm_errno.h"
#include "qm_kernel.h"
#include "qm_log.h"

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/pk.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define LOG_TAG "macos tls"
#define QM_MACOS_TLS_CA_FILE_ENV "QM_MACOS_TLS_CA_FILE"
#define QM_MACOS_TLS_DEFAULT_CA_FILE "/etc/ssl/cert.pem"
#define QM_MACOS_TLS_HANDSHAKE_TIMEOUT_MS 15000U

typedef struct {
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt ca;
    mbedtls_x509_crt own_cert;
    mbedtls_pk_context own_key;
} macos_tls_t;

static void log_mbedtls_error(const char *operation, int error)
{
    char message[160];

    mbedtls_strerror(error, message, sizeof(message));
    QM_LOGE(LOG_TAG, "%s failed ret=-0x%04x (%s)", operation,
            (unsigned int)-error, message);
}

static void tls_context_init(macos_tls_t *tls)
{
    mbedtls_net_init(&tls->net);
    mbedtls_ssl_init(&tls->ssl);
    mbedtls_ssl_config_init(&tls->config);
    mbedtls_entropy_init(&tls->entropy);
    mbedtls_ctr_drbg_init(&tls->ctr_drbg);
    mbedtls_x509_crt_init(&tls->ca);
    mbedtls_x509_crt_init(&tls->own_cert);
    mbedtls_pk_init(&tls->own_key);
}

static void tls_context_free(macos_tls_t *tls)
{
    if (tls == NULL) {
        return;
    }
    mbedtls_net_free(&tls->net);
    mbedtls_ssl_free(&tls->ssl);
    mbedtls_ssl_config_free(&tls->config);
    mbedtls_ctr_drbg_free(&tls->ctr_drbg);
    mbedtls_entropy_free(&tls->entropy);
    mbedtls_x509_crt_free(&tls->ca);
    mbedtls_x509_crt_free(&tls->own_cert);
    mbedtls_pk_free(&tls->own_key);
    free(tls);
}

static int seed_random(macos_tls_t *tls, const char *purpose)
{
    return mbedtls_ctr_drbg_seed(&tls->ctr_drbg,
                                 mbedtls_entropy_func,
                                 &tls->entropy,
                                 (const unsigned char *)purpose,
                                 strlen(purpose));
}

static int is_pem(const char *data, size_t len)
{
    static const char marker[] = "-----BEGIN";

    return data != NULL && len >= sizeof(marker) - 1U &&
           memcmp(data, marker, sizeof(marker) - 1U) == 0;
}

static int parse_certificate(mbedtls_x509_crt *certificate,
                             const char *data,
                             int configured_len)
{
    size_t len;
    unsigned char *pem = NULL;
    int ret;

    if (certificate == NULL || data == NULL) {
        return MBEDTLS_ERR_X509_BAD_INPUT_DATA;
    }
    len = configured_len > 0 ? (size_t)configured_len : strlen(data);
    if (len == 0U) {
        return MBEDTLS_ERR_X509_BAD_INPUT_DATA;
    }
    if (!is_pem(data, len)) {
        return mbedtls_x509_crt_parse(certificate,
                                      (const unsigned char *)data,
                                      len);
    }

    pem = (unsigned char *)malloc(len + 1U);
    if (pem == NULL) {
        return MBEDTLS_ERR_X509_ALLOC_FAILED;
    }
    memcpy(pem, data, len);
    pem[len] = '\0';
    ret = mbedtls_x509_crt_parse(certificate, pem, len + 1U);
    free(pem);
    return ret;
}

static int parse_private_key(mbedtls_pk_context *key,
                             const char *data,
                             int configured_len)
{
    size_t len;
    unsigned char *input = NULL;
    size_t parse_len;
    int ret;

    if (key == NULL || data == NULL) {
        return MBEDTLS_ERR_PK_BAD_INPUT_DATA;
    }
    len = configured_len > 0 ? (size_t)configured_len : strlen(data);
    if (len == 0U) {
        return MBEDTLS_ERR_PK_BAD_INPUT_DATA;
    }

    parse_len = len;
    if (is_pem(data, len)) {
        input = (unsigned char *)malloc(len + 1U);
        if (input == NULL) {
            return MBEDTLS_ERR_PK_ALLOC_FAILED;
        }
        memcpy(input, data, len);
        input[len] = '\0';
        parse_len++;
    }
    ret = mbedtls_pk_parse_key(key,
                               input != NULL ? input : (const unsigned char *)data,
                               parse_len,
                               NULL,
                               0);
    free(input);
    return ret;
}

static int load_client_ca(macos_tls_t *tls, const qm_tls_cfg_t *cfg)
{
    const char *path;

    if (cfg != NULL && cfg->ca_crt != NULL) {
        return parse_certificate(&tls->ca, cfg->ca_crt, cfg->ca_crt_len);
    }

    path = getenv(QM_MACOS_TLS_CA_FILE_ENV);
    if (path == NULL || path[0] == '\0') {
        path = QM_MACOS_TLS_DEFAULT_CA_FILE;
    }
    return mbedtls_x509_crt_parse_file(&tls->ca, path);
}

static int set_socket_timeout(int socket_fd, uint32_t timeout_ms)
{
    struct timeval timeout;

    if (socket_fd < 0) {
        return -1;
    }
    if (timeout_ms == QM_WAIT_FOREVER) {
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;
    } else {
        timeout.tv_sec = (time_t)(timeout_ms / 1000U);
        timeout.tv_usec = (suseconds_t)(timeout_ms % 1000U) * 1000;
    }
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, sizeof(timeout)) != 0 ||
        setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO,
                   &timeout, sizeof(timeout)) != 0) {
        return -1;
    }
    return 0;
}

static int wait_socket(int socket_fd, int write_ready, uint32_t timeout_ms)
{
    fd_set set;
    struct timeval timeout;
    struct timeval *timeout_ptr = &timeout;
    int ret;

    FD_ZERO(&set);
    FD_SET(socket_fd, &set);
    if (timeout_ms == QM_WAIT_FOREVER) {
        timeout_ptr = NULL;
    } else {
        timeout.tv_sec = (time_t)(timeout_ms / 1000U);
        timeout.tv_usec = (suseconds_t)(timeout_ms % 1000U) * 1000;
    }
    do {
        ret = select(socket_fd + 1,
                     write_ready ? NULL : &set,
                     write_ready ? &set : NULL,
                     NULL,
                     timeout_ptr);
    } while (ret < 0 && errno == EINTR);
    return ret;
}

static int configure_common(macos_tls_t *tls, int endpoint)
{
    int ret;

    ret = seed_random(tls, endpoint == MBEDTLS_SSL_IS_CLIENT ?
                           "qumios macos tls client" : "qumios macos tls server");
    if (ret != 0) {
        return ret;
    }
    ret = mbedtls_ssl_config_defaults(&tls->config,
                                      endpoint,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        return ret;
    }
    mbedtls_ssl_conf_rng(&tls->config, mbedtls_ctr_drbg_random, &tls->ctr_drbg);
    mbedtls_ssl_conf_min_version(&tls->config,
                                 MBEDTLS_SSL_MAJOR_VERSION_3,
                                 MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_max_version(&tls->config,
                                 MBEDTLS_SSL_MAJOR_VERSION_3,
                                 MBEDTLS_SSL_MINOR_VERSION_3);
    return 0;
}

static int finish_setup(macos_tls_t *tls, uint32_t timeout_ms,
                        const char *hostname)
{
    int ret;

    mbedtls_ssl_conf_read_timeout(&tls->config, timeout_ms);
    ret = mbedtls_ssl_setup(&tls->ssl, &tls->config);
    if (ret != 0) {
        return ret;
    }
    if (hostname != NULL) {
        ret = mbedtls_ssl_set_hostname(&tls->ssl, hostname);
        if (ret != 0) {
            return ret;
        }
    }
    mbedtls_ssl_set_bio(&tls->ssl,
                        &tls->net,
                        mbedtls_net_send,
                        mbedtls_net_recv,
                        mbedtls_net_recv_timeout);
    (void)set_socket_timeout(tls->net.fd, timeout_ms);
    do {
        ret = mbedtls_ssl_handshake(&tls->ssl);
    } while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);
    return ret;
}

void *qm_tls_establish(const char *host, uint16_t port, const qm_tls_cfg_t *cfg)
{
    macos_tls_t *tls;
    char port_string[8];
    int ret;

    if (host == NULL || host[0] == '\0' || port == 0U) {
        return NULL;
    }
    tls = (macos_tls_t *)calloc(1, sizeof(*tls));
    if (tls == NULL) {
        return NULL;
    }
    tls_context_init(tls);

    ret = configure_common(tls, MBEDTLS_SSL_IS_CLIENT);
    if (ret != 0) {
        log_mbedtls_error("client config", ret);
        goto fail;
    }
    ret = load_client_ca(tls, cfg);
    if (ret != 0) {
        log_mbedtls_error("load CA", ret);
        goto fail;
    }
    mbedtls_ssl_conf_authmode(&tls->config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&tls->config, &tls->ca, NULL);

    if (cfg != NULL && (cfg->client_crt != NULL || cfg->client_key != NULL)) {
        if (cfg->client_crt == NULL || cfg->client_key == NULL) {
            QM_LOGE(LOG_TAG, "client certificate and key must be provided together");
            goto fail;
        }
        ret = parse_certificate(&tls->own_cert, cfg->client_crt, cfg->client_crt_len);
        if (ret != 0) {
            log_mbedtls_error("parse client certificate", ret);
            goto fail;
        }
        ret = parse_private_key(&tls->own_key, cfg->client_key, cfg->client_key_len);
        if (ret != 0) {
            log_mbedtls_error("parse client key", ret);
            goto fail;
        }
        ret = mbedtls_ssl_conf_own_cert(&tls->config,
                                        &tls->own_cert,
                                        &tls->own_key);
        if (ret != 0) {
            log_mbedtls_error("configure client certificate", ret);
            goto fail;
        }
    }

    snprintf(port_string, sizeof(port_string), "%u", (unsigned int)port);
    ret = mbedtls_net_connect(&tls->net, host, port_string, MBEDTLS_NET_PROTO_TCP);
    if (ret != 0) {
        log_mbedtls_error("TCP connect", ret);
        goto fail;
    }
    ret = finish_setup(tls, QM_MACOS_TLS_HANDSHAKE_TIMEOUT_MS, host);
    if (ret != 0) {
        log_mbedtls_error("TLS handshake", ret);
        goto fail;
    }
    if (mbedtls_ssl_get_verify_result(&tls->ssl) != 0U) {
        QM_LOGE(LOG_TAG, "server certificate verification failed flags=0x%08x",
                (unsigned int)mbedtls_ssl_get_verify_result(&tls->ssl));
        goto fail;
    }
    return tls;

fail:
    tls_context_free(tls);
    return NULL;
}

void *qm_tls_accept(int socket_fd, const qm_tls_server_cfg_t *cfg,
                    uint32_t timeout_ms)
{
    macos_tls_t *tls;
    int ret;

    if (socket_fd < 0 || cfg == NULL || cfg->server_crt == NULL ||
        cfg->server_key == NULL) {
        return NULL;
    }
    tls = (macos_tls_t *)calloc(1, sizeof(*tls));
    if (tls == NULL) {
        return NULL;
    }
    tls_context_init(tls);
    tls->net.fd = socket_fd;

    ret = configure_common(tls, MBEDTLS_SSL_IS_SERVER);
    if (ret != 0) {
        log_mbedtls_error("server config", ret);
        goto fail;
    }
    ret = parse_certificate(&tls->own_cert, cfg->server_crt, cfg->server_crt_len);
    if (ret != 0) {
        log_mbedtls_error("parse server certificate", ret);
        goto fail;
    }
    ret = parse_private_key(&tls->own_key, cfg->server_key, cfg->server_key_len);
    if (ret != 0) {
        log_mbedtls_error("parse server key", ret);
        goto fail;
    }
    ret = mbedtls_ssl_conf_own_cert(&tls->config, &tls->own_cert, &tls->own_key);
    if (ret != 0) {
        log_mbedtls_error("configure server certificate", ret);
        goto fail;
    }

    if (cfg->client_ca != NULL && cfg->client_ca_len > 0) {
        ret = parse_certificate(&tls->ca, cfg->client_ca, cfg->client_ca_len);
        if (ret != 0) {
            log_mbedtls_error("parse client CA", ret);
            goto fail;
        }
        mbedtls_ssl_conf_ca_chain(&tls->config, &tls->ca, NULL);
        mbedtls_ssl_conf_authmode(&tls->config,
                                  cfg->verify_client ? MBEDTLS_SSL_VERIFY_REQUIRED :
                                                       MBEDTLS_SSL_VERIFY_OPTIONAL);
    } else {
        if (cfg->verify_client) {
            QM_LOGE(LOG_TAG, "verify_client requires client_ca");
            goto fail;
        }
        mbedtls_ssl_conf_authmode(&tls->config, MBEDTLS_SSL_VERIFY_NONE);
    }
    if (cfg->alpn_protos != NULL) {
        ret = mbedtls_ssl_conf_alpn_protocols(&tls->config, cfg->alpn_protos);
        if (ret != 0) {
            log_mbedtls_error("configure ALPN", ret);
            goto fail;
        }
    }
    if (cfg->session_tickets) {
        QM_LOGW(LOG_TAG, "TLS session tickets are not enabled in the macOS host");
    }

    ret = finish_setup(tls, timeout_ms, NULL);
    if (ret != 0) {
        log_mbedtls_error("server TLS handshake", ret);
        goto fail;
    }
    return tls;

fail:
    tls_context_free(tls);
    return NULL;
}

int32_t qm_tls_write(void *handle, const char *buf, int len, uint32_t timeout_ms)
{
    macos_tls_t *tls = (macos_tls_t *)handle;
    int written = 0;

    if (tls == NULL || buf == NULL || len <= 0) {
        return -QM_EINVAL;
    }
    (void)set_socket_timeout(tls->net.fd, timeout_ms);
    while (written < len) {
        int ret;

        if (wait_socket(tls->net.fd, 1, timeout_ms) <= 0) {
            return written > 0 ? written : 0;
        }
        ret = mbedtls_ssl_write(&tls->ssl,
                                (const unsigned char *)buf + written,
                                (size_t)(len - written));
        if (ret > 0) {
            written += ret;
        } else if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
                   ret == MBEDTLS_ERR_SSL_WANT_WRITE ||
                   ret == MBEDTLS_ERR_SSL_TIMEOUT) {
            return written > 0 ? written : 0;
        } else {
            log_mbedtls_error("TLS write", ret);
            return written > 0 ? written : -2;
        }
    }
    return written;
}

int32_t qm_tls_read(void *handle, char *buf, int len, uint32_t timeout_ms)
{
    macos_tls_t *tls = (macos_tls_t *)handle;
    int ret;

    if (tls == NULL || buf == NULL || len <= 0) {
        return -QM_EINVAL;
    }
    if (mbedtls_ssl_get_bytes_avail(&tls->ssl) == 0U &&
        wait_socket(tls->net.fd, 0, timeout_ms) <= 0) {
        return 0;
    }
    mbedtls_ssl_conf_read_timeout(&tls->config, timeout_ms);
    (void)set_socket_timeout(tls->net.fd, timeout_ms);
    ret = mbedtls_ssl_read(&tls->ssl, (unsigned char *)buf, (size_t)len);
    if (ret > 0) {
        return ret;
    }
    if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
        return -1;
    }
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE ||
        ret == MBEDTLS_ERR_SSL_TIMEOUT) {
        return 0;
    }
    log_mbedtls_error("TLS read", ret);
    return -2;
}

int32_t qm_tls_destroy(void *handle)
{
    macos_tls_t *tls = (macos_tls_t *)handle;

    if (tls == NULL) {
        return -QM_EINVAL;
    }
    (void)mbedtls_ssl_close_notify(&tls->ssl);
    tls_context_free(tls);
    return QM_EOK;
}
