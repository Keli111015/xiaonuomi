#include "qm.h"
#include "qm_tls.h"
#include "qm_linux_stub.h"
#include "qm_linux_default_ca.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/crypto.h>

/*
 * SH510 板端链接 OpenSSL 1.1（prebuilt），不要用 sysroot 的 1.0.0 ABI。
 * 阻塞 connect 遇不可达会对端长时间假死；非阻塞 + select 限时失败。
 */
#define LOG_TAG "TLS"
#define TLS_TCP_CONNECT_TIMEOUT_MS 10000
#define TLS_LIVE_MAX 8
/* select 不握 s_tls_mu，避免 MQTT 长读堵住新的 HTTPS 建连 */
#define TLS_LOCK_SLICE_MS 50

typedef struct {
    int server_fd;
    SSL_CTX *ctx;
    SSL *ssl;
} tls_ctx_t;

#define SSL_READ_BUFFER_LEN 8120

/*
 * OpenSSL 1.1 is not safe for SSL_read vs SSL_free on the same SSL*.
 * Serialize read/write/destroy and reject ops on already-destroyed handles
 * without dereferencing a freed tls_ctx_t.
 */
static pthread_mutex_t s_tls_mu = PTHREAD_MUTEX_INITIALIZER;
static void *s_tls_live[TLS_LIVE_MAX];

static int tls_live_add(void *p)
{
    int i;

    for (i = 0; i < TLS_LIVE_MAX; i++) {
        if (s_tls_live[i] == NULL) {
            s_tls_live[i] = p;
            return 0;
        }
    }
    return -1;
}

static int tls_live_has(void *p)
{
    int i;

    if (p == NULL) {
        return 0;
    }
    for (i = 0; i < TLS_LIVE_MAX; i++) {
        if (s_tls_live[i] == p) {
            return 1;
        }
    }
    return 0;
}

static void tls_live_del(void *p)
{
    int i;

    for (i = 0; i < TLS_LIVE_MAX; i++) {
        if (s_tls_live[i] == p) {
            s_tls_live[i] = NULL;
            return;
        }
    }
}

static void tls_release_ssl(tls_ctx_t *tls)
{
    /* Do not SSL_shutdown(): it waits for close_notify and can hang
     * forever (dynreg HTTP close). Socket shutdown is enough to wake
     * a blocked SSL_read; destroy already holds s_tls_mu. */
    if (tls->server_fd >= 0) {
        (void)shutdown(tls->server_fd, SHUT_RDWR);
    }
    if (tls->ssl) {
        SSL_free(tls->ssl);
        tls->ssl = NULL;
    }
    if (tls->ctx) {
        SSL_CTX_free(tls->ctx);
        tls->ctx = NULL;
    }
    if (tls->server_fd >= 0) {
        close(tls->server_fd);
        tls->server_fd = -1;
    }
}

static void qm_tls_openssl_init_once(void)
{
    static int inited;

    if (inited) {
        return;
    }

    if (OPENSSL_init_ssl(0, NULL) != 1) {
        QM_LOGE(LOG_TAG, "OPENSSL_init_ssl failed");
        return;
    }

    inited = 1;
}

static int qm_tls_load_ca_pem(SSL_CTX *ctx, const char *pem, int pem_len);

static int tcp_connect_timeout(int fd, const struct sockaddr *addr, socklen_t addrlen,
                               int timeout_ms)
{
    int flags;
    int ret;
    int so_error = 0;
    socklen_t optlen = sizeof(so_error);
    fd_set wset;
    struct timeval tv;

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }

    ret = connect(fd, addr, addrlen);
    if (ret == 0) {
        goto restore;
    }
    if (errno != EINPROGRESS) {
        ret = -1;
        goto restore;
    }

    FD_ZERO(&wset);
    FD_SET(fd, &wset);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ret = select(fd + 1, NULL, &wset, NULL, &tv);
    if (ret == 0) {
        errno = ETIMEDOUT;
        ret = -1;
        goto restore;
    }
    if (ret < 0) {
        goto restore;
    }

    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &optlen) < 0 ||
        so_error != 0) {
        if (so_error != 0) {
            errno = so_error;
        }
        ret = -1;
        goto restore;
    }
    ret = 0;

restore:
    (void)fcntl(fd, F_SETFL, flags);
    return ret;
}

static SSL_CTX *qm_tls_create_client_ctx(void)
{
    const SSL_METHOD *method;
    SSL_CTX *ctx;

    method = TLS_client_method();
    if (method == NULL) {
        QM_LOGE(LOG_TAG, "no SSL client method");
        return NULL;
    }

    ctx = SSL_CTX_new(method);
    if (ctx == NULL) {
        unsigned long err = ERR_get_error();
        const char *reason = ERR_reason_error_string(err);

        QM_LOGE(LOG_TAG, "SSL_CTX_new failed: %s", reason ? reason : "unknown");
    }

    return ctx;
}

static SSL_CTX *qm_tls_create_server_ctx(const qm_tls_server_cfg_t *cfg)
{
    const SSL_METHOD *method;
    SSL_CTX *ctx;
    BIO *bio;
    X509 *server_cert;
    EVP_PKEY *server_key;

    if (cfg == NULL || cfg->server_crt == NULL || cfg->server_crt_len <= 0 ||
        cfg->server_key == NULL || cfg->server_key_len <= 0) {
        return NULL;
    }

    method = TLS_server_method();
    if (method == NULL) {
        QM_LOGE(LOG_TAG, "no SSL server method");
        return NULL;
    }

    ctx = SSL_CTX_new(method);
    if (ctx == NULL) {
        return NULL;
    }

    bio = BIO_new_mem_buf((void *)cfg->server_crt, cfg->server_crt_len);
    if (bio == NULL) {
        goto err_exit;
    }

    server_cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (server_cert == NULL || SSL_CTX_use_certificate(ctx, server_cert) != 1) {
        if (server_cert) {
            X509_free(server_cert);
        }
        goto err_exit;
    }
    X509_free(server_cert);

    bio = BIO_new_mem_buf((void *)cfg->server_key, cfg->server_key_len);
    if (bio == NULL) {
        goto err_exit;
    }

    server_key = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (server_key == NULL ||
        SSL_CTX_use_PrivateKey(ctx, server_key) != 1 ||
        SSL_CTX_check_private_key(ctx) != 1) {
        if (server_key) {
            EVP_PKEY_free(server_key);
        }
        goto err_exit;
    }
    EVP_PKEY_free(server_key);

    if (cfg->verify_client && cfg->client_ca != NULL && cfg->client_ca_len > 0) {
        if (qm_tls_load_ca_pem(ctx, cfg->client_ca, cfg->client_ca_len) < 0) {
            goto err_exit;
        }
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
    } else {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    }

    return ctx;

err_exit:
    if (ctx) {
        SSL_CTX_free(ctx);
    }
    return NULL;
}

static int qm_tls_load_ca_pem(SSL_CTX *ctx, const char *pem, int pem_len)
{
    BIO *bio;
    int ca_loaded = 0;

    if (ctx == NULL || pem == NULL || pem_len <= 0) {
        return -1;
    }

    bio = BIO_new_mem_buf((void *)pem, pem_len);
    if (bio == NULL) {
        QM_LOGE(LOG_TAG, "ca bio create failed");
        return -1;
    }

    while (1) {
        X509 *cacert = PEM_read_bio_X509(bio, NULL, NULL, NULL);

        if (cacert == NULL) {
            break;
        }

        if (X509_STORE_add_cert(SSL_CTX_get_cert_store(ctx), cacert) != 1) {
            QM_LOGE(LOG_TAG, "add ca cert failed index=%d", ca_loaded);
            X509_free(cacert);
            BIO_free(bio);
            return -1;
        }

        X509_free(cacert);
        ca_loaded++;
    }

    BIO_free(bio);

    if (ca_loaded == 0) {
        QM_LOGE(LOG_TAG, "load ca cert failed len=%d", pem_len);
        return -1;
    }

    return ca_loaded;
}

void *qm_tls_establish(const char *host, uint16_t port, const qm_tls_cfg_t *cfg)
{
    tls_ctx_t *tls = NULL;
    SSL_CTX *ctx = NULL;
    SSL *ssl = NULL;
    int server_fd = -1;
    int gai;
    char port_str[8];
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *rp = NULL;

    if (host == NULL || cfg == NULL) {
        QM_LOGE(LOG_TAG, "input param is NULL");
        return NULL;
    }

    tls = (tls_ctx_t *)malloc(sizeof(tls_ctx_t));
    if (tls == NULL) {
        return NULL;
    }
    memset(tls, 0, sizeof(tls_ctx_t));
    tls->server_fd = -1;

    qm_tls_openssl_init_once();

    ctx = qm_tls_create_client_ctx();
    if (ctx == NULL) {
        goto err_exit;
    }

    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

    if (cfg->ca_crt != NULL && cfg->ca_crt_len > 0) {
        if (qm_tls_load_ca_pem(ctx, cfg->ca_crt, cfg->ca_crt_len) < 0) {
            goto err_exit;
        }
    } else if (qm_tls_load_ca_pem(ctx,
                                  qm_linux_default_ca_pem,
                                  (int)(sizeof(qm_linux_default_ca_pem) - 1)) < 0) {
        goto err_exit;
    }

    if (cfg->client_crt && cfg->client_crt_len > 0) {
        BIO *bio = BIO_new_mem_buf((void *)cfg->client_crt, cfg->client_crt_len);
        X509 *client_cert;

        if (bio == NULL) {
            QM_LOGE(LOG_TAG, "client cert bio create failed");
            goto err_exit;
        }

        client_cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
        if (client_cert == NULL) {
            BIO_reset(bio);
            client_cert = d2i_X509_bio(bio, NULL);
        }
        BIO_free(bio);

        if (client_cert == NULL || SSL_CTX_use_certificate(ctx, client_cert) == 0) {
            QM_LOGE(LOG_TAG, "load client cert failed len=%d", cfg->client_crt_len);
            if (client_cert) {
                X509_free(client_cert);
            }
            goto err_exit;
        }
        X509_free(client_cert);
    }

    if (cfg->client_key && cfg->client_key_len > 0) {
        BIO *bio = BIO_new_mem_buf((void *)cfg->client_key, cfg->client_key_len);
        EVP_PKEY *client_key;

        if (bio == NULL) {
            QM_LOGE(LOG_TAG, "client key bio create failed");
            goto err_exit;
        }

        client_key = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
        if (client_key == NULL) {
            BIO_reset(bio);
            client_key = d2i_PrivateKey_bio(bio, NULL);
        }
        BIO_free(bio);

        if (client_key == NULL ||
            SSL_CTX_use_PrivateKey(ctx, client_key) == 0 ||
            SSL_CTX_check_private_key(ctx) != 1) {
            QM_LOGE(LOG_TAG, "load client key failed len=%d", cfg->client_key_len);
            if (client_key) {
                EVP_PKEY_free(client_key);
            }
            goto err_exit;
        }
        EVP_PKEY_free(client_key);
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);

    QM_LOGI(LOG_TAG, "tls dns resolve %s", host);
    gai = getaddrinfo(host, port_str, &hints, &res);
    if (gai != 0 || res == NULL) {
        QM_LOGE(LOG_TAG, "getaddrinfo failed: %s (%s)",
                host, gai_strerror(gai));
        goto err_exit;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        server_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (server_fd < 0) {
            continue;
        }

        QM_LOGI(LOG_TAG, "tls tcp connect %s:%u timeout=%dms",
                host, (unsigned)port, TLS_TCP_CONNECT_TIMEOUT_MS);
        if (tcp_connect_timeout(server_fd, rp->ai_addr, rp->ai_addrlen,
                                TLS_TCP_CONNECT_TIMEOUT_MS) == 0) {
            break;
        }

        QM_LOGE(LOG_TAG, "tcp connect failed: %s:%u errno=%d (%s)",
                host, (unsigned)port, errno, strerror(errno));
        close(server_fd);
        server_fd = -1;
    }
    freeaddrinfo(res);
    res = NULL;

    if (server_fd < 0) {
        goto err_exit;
    }

    ssl = SSL_new(ctx);
    if (ssl == NULL) {
        goto err_exit;
    }

    SSL_set_fd(ssl, server_fd);
    SSL_set_connect_state(ssl);
    SSL_set_tlsext_host_name(ssl, host);

    QM_LOGI(LOG_TAG, "tls SSL_connect begin %s:%u", host, (unsigned)port);
    if (SSL_connect(ssl) != 1) {
        QM_LOGE(LOG_TAG, "SSL_connect failed: %s",
                ERR_reason_error_string(ERR_get_error()));
        goto err_exit;
    }
    QM_LOGI(LOG_TAG, "tls SSL_connect ok %s:%u", host, (unsigned)port);

    tls->server_fd = server_fd;
    tls->ctx = ctx;
    tls->ssl = ssl;

    pthread_mutex_lock(&s_tls_mu);
    if (tls_live_add(tls) != 0) {
        pthread_mutex_unlock(&s_tls_mu);
        QM_LOGE(LOG_TAG, "tls live table full");
        goto err_exit;
    }
    pthread_mutex_unlock(&s_tls_mu);
    return tls;

err_exit:
    QM_LOGE(LOG_TAG, "tls establish failed for %s:%u", host ? host : "-", port);
    if (ssl) {
        SSL_free(ssl);
    }
    if (ctx) {
        SSL_CTX_free(ctx);
    }
    if (server_fd >= 0) {
        close(server_fd);
    }
    if (tls) {
        free(tls);
    }
    return NULL;
}

int32_t qm_tls_write(void *handle, const char *buf, int len, uint32_t timeout_ms)
{
    tls_ctx_t *tls;
    fd_set readfds;
    fd_set writefds;
    struct timeval tv;
    int ret;
    int fd;
    int sent = 0;
    uint32_t left_ms;
    uint32_t slice_ms;

    if (handle == NULL || buf == NULL || len <= 0) {
        return -QM_EINVAL;
    }

    /* 与 read 一样：超时必须生效，select 不握锁，避免大包 SSL_write 卡死 */
    left_ms = (timeout_ms > 0) ? timeout_ms : 10;

    while (sent < len) {
        pthread_mutex_lock(&s_tls_mu);
        if (!tls_live_has(handle)) {
            pthread_mutex_unlock(&s_tls_mu);
            return -QM_EINVAL;
        }
        tls = (tls_ctx_t *)handle;
        fd = tls->server_fd;
        pthread_mutex_unlock(&s_tls_mu);
        if (fd < 0) {
            return -QM_EIO;
        }

        slice_ms = (left_ms < TLS_LOCK_SLICE_MS) ? left_ms : TLS_LOCK_SLICE_MS;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_SET(fd, &readfds);
        FD_SET(fd, &writefds);
        tv.tv_sec = slice_ms / 1000;
        tv.tv_usec = (slice_ms % 1000) * 1000;
        ret = select(fd + 1, &readfds, &writefds, NULL, &tv);
        if (ret < 0 && errno != EINTR) {
            return -QM_EIO;
        }
        if (ret == 0) {
            if (left_ms <= slice_ms) {
                return 0;
            }
            left_ms -= slice_ms;
            continue;
        }

        pthread_mutex_lock(&s_tls_mu);
        if (!tls_live_has(handle)) {
            pthread_mutex_unlock(&s_tls_mu);
            return -QM_EINVAL;
        }
        tls = (tls_ctx_t *)handle;

        /* 对端已关 / close_notify：马上失败，上层重连，别堵在 SSL_write */
        if (FD_ISSET(fd, &readfds)) {
            char peek[8];

            (void)SSL_read(tls->ssl, peek, (int)sizeof(peek));
            pthread_mutex_unlock(&s_tls_mu);
            return -QM_EIO;
        }
        if (!FD_ISSET(fd, &writefds)) {
            pthread_mutex_unlock(&s_tls_mu);
            if (left_ms <= slice_ms) {
                return 0;
            }
            left_ms -= slice_ms;
            continue;
        }

        {
            int chunk = len - sent;

            if (chunk > 4096) {
                chunk = 4096;
            }
            tv.tv_sec = slice_ms / 1000;
            tv.tv_usec = (slice_ms % 1000) * 1000;
            (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            ret = SSL_write(tls->ssl, buf + sent, chunk);
        }
        pthread_mutex_unlock(&s_tls_mu);
        if (ret <= 0) {
            return -QM_EIO;
        }
        sent += ret;
    }

    return sent;
}

int32_t qm_tls_read(void *handle, char *buf, int len, uint32_t timeout_ms)
{
    tls_ctx_t *tls;
    fd_set readfds;
    struct timeval tv;
    int ret;
    int fd;
    uint32_t left_ms;
    uint32_t slice_ms;

    if (handle == NULL || buf == NULL || len <= 0) {
        return -QM_EINVAL;
    }

    /* timeout 0 曾直接堵在 SSL_read 上，destroy 拿不到锁。 */
    left_ms = (timeout_ms > 0) ? timeout_ms : 10;

    for (;;) {
        pthread_mutex_lock(&s_tls_mu);
        if (!tls_live_has(handle)) {
            pthread_mutex_unlock(&s_tls_mu);
            return -QM_EINVAL;
        }
        tls = (tls_ctx_t *)handle;

        if (SSL_pending(tls->ssl) > 0) {
            ret = SSL_read(tls->ssl, buf, len);
            pthread_mutex_unlock(&s_tls_mu);
            return (ret <= 0) ? -QM_EIO : ret;
        }

        fd = tls->server_fd;
        pthread_mutex_unlock(&s_tls_mu);

        slice_ms = (left_ms < TLS_LOCK_SLICE_MS) ? left_ms : TLS_LOCK_SLICE_MS;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        tv.tv_sec = slice_ms / 1000;
        tv.tv_usec = (slice_ms % 1000) * 1000;
        ret = select(fd + 1, &readfds, NULL, NULL, &tv);
        if (ret > 0) {
            pthread_mutex_lock(&s_tls_mu);
            if (!tls_live_has(handle)) {
                pthread_mutex_unlock(&s_tls_mu);
                return -QM_EINVAL;
            }
            tls = (tls_ctx_t *)handle;
            ret = SSL_read(tls->ssl, buf, len);
            pthread_mutex_unlock(&s_tls_mu);
            return (ret <= 0) ? -QM_EIO : ret;
        }
        if (ret < 0 && errno != EINTR) {
            return -QM_EIO;
        }
        if (left_ms <= slice_ms) {
            return 0;
        }
        left_ms -= slice_ms;
    }
}

int32_t qm_tls_destroy(void *handle)
{
    tls_ctx_t *tls;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    pthread_mutex_lock(&s_tls_mu);
    if (!tls_live_has(handle)) {
        pthread_mutex_unlock(&s_tls_mu);
        return QM_EOK;
    }
    tls = (tls_ctx_t *)handle;
    tls_live_del(handle);
    tls_release_ssl(tls);
    pthread_mutex_unlock(&s_tls_mu);
    free(tls);
    return QM_EOK;
}

void *qm_tls_accept(int socket, const qm_tls_server_cfg_t *cfg, uint32_t timeout_ms)
{
    tls_ctx_t *tls = NULL;
    SSL_CTX *ctx = NULL;
    SSL *ssl = NULL;
    fd_set readfds;
    struct timeval tv;
    int ret;

    if (socket < 0 || cfg == NULL) {
        return NULL;
    }

    tls = (tls_ctx_t *)malloc(sizeof(tls_ctx_t));
    if (tls == NULL) {
        return NULL;
    }
    memset(tls, 0, sizeof(tls_ctx_t));
    tls->server_fd = -1;

    qm_tls_openssl_init_once();

    ctx = qm_tls_create_server_ctx(cfg);
    if (ctx == NULL) {
        goto err_exit;
    }

    ssl = SSL_new(ctx);
    if (ssl == NULL) {
        goto err_exit;
    }

    SSL_set_fd(ssl, socket);
    SSL_set_accept_state(ssl);

    if (timeout_ms > 0) {
        FD_ZERO(&readfds);
        FD_SET(socket, &readfds);
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ret = select(socket + 1, &readfds, NULL, NULL, &tv);
        if (ret <= 0) {
            QM_LOGE(LOG_TAG, "accept wait timeout ret=%d errno=%d", ret, errno);
            goto err_exit;
        }
    }

    if (SSL_accept(ssl) != 1) {
        QM_LOGE(LOG_TAG, "SSL_accept failed: %s",
                ERR_reason_error_string(ERR_get_error()));
        goto err_exit;
    }

    tls->server_fd = socket;
    tls->ctx = ctx;
    tls->ssl = ssl;

    pthread_mutex_lock(&s_tls_mu);
    if (tls_live_add(tls) != 0) {
        pthread_mutex_unlock(&s_tls_mu);
        QM_LOGE(LOG_TAG, "tls live table full");
        goto err_exit;
    }
    pthread_mutex_unlock(&s_tls_mu);
    return tls;

err_exit:
    if (ssl) {
        SSL_free(ssl);
    }
    if (ctx) {
        SSL_CTX_free(ctx);
    }
    if (tls) {
        free(tls);
    }
    return NULL;
}
