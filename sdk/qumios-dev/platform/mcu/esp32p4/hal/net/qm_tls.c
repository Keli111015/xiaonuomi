#include "qm.h"
#include "qm_tls.h"

#include "esp_idf_version.h"

#define LOG_TAG "TLS"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)

#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "sys/time.h"
#include "sys/socket.h"
#include "sys/queue.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>

#define QM_TLS_ESTABLISH_TIMEOUT    (60 * 1000)

typedef struct 
{
    esp_tls_t      *tls;
    esp_tls_cfg_t   cfg;
    esp_tls_cfg_server_t server_cfg;
    int             sockfd;
    bool            server_side;
    bool            write_logged;
}qm_tls_hal_handle_t;

static struct timeval* qm_tls_hal_utils_ms_to_timeval(int timeout_ms, struct timeval *tv)
{
    if (timeout_ms == -1) {
        return NULL;
    }
    tv->tv_sec = timeout_ms / 1000;
    tv->tv_usec = (timeout_ms - (tv->tv_sec * 1000)) * 1000;
    return tv;
}

static unsigned int qm_tls_hal_cert_bytes(const char *cert, int cert_len)
{
    if (cert == NULL) {
        return 0;
    }

    if (cert_len > 0) {
        return (unsigned int)cert_len + 1;
    }

    return cert[0] ? (unsigned int)strlen(cert) + 1 : 0;
}

static const char *qm_tls_hal_pem_type(const char *pem)
{
    if (pem == NULL || pem[0] == '\0') {
        return "none";
    }
    if (strstr(pem, "BEGIN CERTIFICATE") != NULL) {
        return "certificate";
    }
    if (strstr(pem, "BEGIN RSA PRIVATE KEY") != NULL) {
        return "rsa-private-key";
    }
    if (strstr(pem, "BEGIN EC PRIVATE KEY") != NULL) {
        return "ec-private-key";
    }
    if (strstr(pem, "BEGIN PRIVATE KEY") != NULL) {
        return "private-key";
    }
    return "unknown";
}

void *qm_tls_establish(const char *host, uint16_t port, const qm_tls_cfg_t *cfg)
{
    unsigned int ca_crt_bytes;
    unsigned int client_crt_bytes;
    unsigned int client_key_bytes;

    if(host == NULL){
        return NULL;
    }

    ca_crt_bytes = cfg ? qm_tls_hal_cert_bytes(cfg->ca_crt, cfg->ca_crt_len) : 0;
    client_crt_bytes = cfg ? qm_tls_hal_cert_bytes(cfg->client_crt, cfg->client_crt_len) : 0;
    client_key_bytes = cfg ? qm_tls_hal_cert_bytes(cfg->client_key, cfg->client_key_len) : 0;

    if ((client_crt_bytes == 0) != (client_key_bytes == 0)) {
        QM_LOGE(LOG_TAG, "Invalid TLS config: client certificate and key must be provided together");
        return NULL;
    }

    QM_LOGI(LOG_TAG,
            "tls establish host=%s port=%u ca_bytes=%u ca_type=%s client_crt_bytes=%u client_crt_type=%s client_key_bytes=%u client_key_type=%s",
            host,
            (unsigned int)port,
            ca_crt_bytes,
            cfg ? qm_tls_hal_pem_type(cfg->ca_crt) : "none",
            client_crt_bytes,
            cfg ? qm_tls_hal_pem_type(cfg->client_crt) : "none",
            client_key_bytes,
            cfg ? qm_tls_hal_pem_type(cfg->client_key) : "none");

    qm_tls_hal_handle_t *tls_handle = (qm_tls_hal_handle_t *)qm_malloc(sizeof(qm_tls_hal_handle_t));
    if(tls_handle == NULL){
        return NULL;
    }
    memset(tls_handle, 0, sizeof(qm_tls_hal_handle_t));

    tls_handle->cfg.skip_common_name = true;

    if(ca_crt_bytes){
        tls_handle->cfg.cacert_pem_buf = (void *)cfg->ca_crt;
        tls_handle->cfg.cacert_pem_bytes = ca_crt_bytes;
    } else {
#ifdef CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY
        QM_LOGW(LOG_TAG, "TLS server certificate verification disabled: no CA certificate configured");
#else
        QM_LOGE(LOG_TAG, "No CA certificate configured, enable CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY to allow no-verify TLS");
        goto exit_failure;
#endif
    }

    if(client_crt_bytes){
        tls_handle->cfg.clientcert_pem_buf = (void *)cfg->client_crt;
        tls_handle->cfg.clientcert_pem_bytes = client_crt_bytes;
    }

    if(client_key_bytes){
        tls_handle->cfg.clientkey_pem_buf = (void *)cfg->client_key;
        tls_handle->cfg.clientkey_pem_bytes = client_key_bytes;
    }

    tls_handle->cfg.timeout_ms = QM_TLS_ESTABLISH_TIMEOUT;
    
    tls_handle->tls = esp_tls_init();
    if (tls_handle->tls  == NULL) {
        QM_LOGE(LOG_TAG, "Failed to initialize new connection object");
        goto exit_failure;
    }

    if (esp_tls_conn_new_sync(host, strlen(host), port, &tls_handle->cfg, tls_handle->tls) <= 0) {
        QM_LOGE(LOG_TAG,
                "Failed to open a new connection host=%s port=%u",
                host,
                (unsigned int)port);
        goto exit_failure;
    }

    if (esp_tls_get_conn_sockfd(tls_handle->tls, &tls_handle->sockfd) != ESP_OK) {
        QM_LOGE(LOG_TAG, "Error in obtaining socket fd for the session");
        goto exit_failure;
    }
    return (void *)tls_handle;

exit_failure:
    QM_LOGE(LOG_TAG, "tls establish failed host=%s port=%u", host, (unsigned int)port);

    if(tls_handle->tls){
        esp_tls_conn_destroy(tls_handle->tls);
        tls_handle->tls = NULL;
    }
    qm_free(tls_handle);
    return NULL;
}

void *qm_tls_accept(int socket, const qm_tls_server_cfg_t *cfg, uint32_t timeout_ms)
{
    qm_tls_hal_handle_t *tls_handle;
    int ret;

    (void)timeout_ms;

    if (socket < 0 || cfg == NULL || cfg->server_crt == NULL || cfg->server_crt_len <= 0 ||
        cfg->server_key == NULL || cfg->server_key_len <= 0) {
        return NULL;
    }

    tls_handle = (qm_tls_hal_handle_t *)qm_malloc(sizeof(qm_tls_hal_handle_t));
    if (tls_handle == NULL) {
        return NULL;
    }
    memset(tls_handle, 0, sizeof(qm_tls_hal_handle_t));
    tls_handle->sockfd = socket;
    tls_handle->server_side = true;

    tls_handle->server_cfg.servercert_pem_buf = (const unsigned char *)cfg->server_crt;
    tls_handle->server_cfg.servercert_pem_bytes = cfg->server_crt_len + 1;
    tls_handle->server_cfg.serverkey_pem_buf = (const unsigned char *)cfg->server_key;
    tls_handle->server_cfg.serverkey_pem_bytes = cfg->server_key_len + 1;
    tls_handle->server_cfg.alpn_protos = cfg->alpn_protos;

    if (cfg->verify_client && cfg->client_ca && cfg->client_ca_len > 0) {
        tls_handle->server_cfg.cacert_pem_buf = (const unsigned char *)cfg->client_ca;
        tls_handle->server_cfg.cacert_pem_bytes = cfg->client_ca_len + 1;
    }

#ifdef CONFIG_ESP_TLS_SERVER_SESSION_TICKETS
    if (cfg->session_tickets &&
        esp_tls_cfg_server_session_tickets_init(&tls_handle->server_cfg) != ESP_OK) {
        QM_LOGE(LOG_TAG, "Failed to init TLS server session tickets");
        goto exit_failure;
    }
#endif

    tls_handle->tls = esp_tls_init();
    if (tls_handle->tls == NULL) {
        QM_LOGE(LOG_TAG, "Failed to initialize server connection object");
        goto exit_failure;
    }

    ret = esp_tls_server_session_create(&tls_handle->server_cfg, socket, tls_handle->tls);
    if (ret < 0) {
        QM_LOGE(LOG_TAG, "Failed to create server TLS session ret=%d", ret);
        goto exit_failure;
    }

    return (void *)tls_handle;

exit_failure:
    if (tls_handle->tls) {
        esp_tls_server_session_delete(tls_handle->tls);
        tls_handle->tls = NULL;
    }
#ifdef CONFIG_ESP_TLS_SERVER_SESSION_TICKETS
    if (tls_handle->server_cfg.ticket_ctx) {
        esp_tls_cfg_server_session_tickets_free(&tls_handle->server_cfg);
    }
#endif
    qm_free(tls_handle);
    return NULL;
}

int32_t qm_tls_write(void *handle, const char *buf, int len, uint32_t timeout_ms)
{
    struct timeval t;
    fd_set errset;
    fd_set writeset;
    int length = 0, ret = 0;
    qm_tls_hal_handle_t *tls_handle = (qm_tls_hal_handle_t *)handle;
    
    if (NULL == handle){
        QM_LOGE(LOG_TAG, "handle is NULL");
        return -QM_EINVAL;
    }

    FD_ZERO(&writeset);
    FD_ZERO(&errset);
    FD_SET(tls_handle->sockfd, &writeset);
    FD_SET(tls_handle->sockfd, &errset);
    ret = select(tls_handle->sockfd + 1, NULL, &writeset, &errset, qm_tls_hal_utils_ms_to_timeval(timeout_ms, &t));
    if (ret > 0 && FD_ISSET(tls_handle->sockfd, &errset)) {
        int sock_errno = 0;
        uint32_t optlen = sizeof(sock_errno);
        getsockopt(tls_handle->sockfd, SOL_SOCKET, SO_ERROR, &sock_errno, &optlen);
        QM_LOGE(LOG_TAG, "poll_write select error code=%d errno=%s fd=%d timeout_ms=%u", sock_errno, strerror(sock_errno), tls_handle->sockfd, (unsigned int)timeout_ms);
        return -QM_ERROR;
    }else if (ret == 0) {
        QM_LOGE(LOG_TAG, "poll_write timeout fd=%d timeout_ms=%u", tls_handle->sockfd, (unsigned int)timeout_ms);
        return -QM_ETIMEOUT;
    }

    length = esp_tls_conn_write(tls_handle->tls, (const unsigned char *) buf, len);
    if (length < 0) {
        QM_LOGE(LOG_TAG,
                "esp_tls_conn_write error ret=%d errno=%d(%s) fd=%d len=%d",
                length,
                errno,
                strerror(errno),
                tls_handle->sockfd,
                len);
    } else if (length > 0 && !tls_handle->write_logged) {
        QM_LOGI(LOG_TAG, "tls write ok fd=%d wrote=%d requested=%d", tls_handle->sockfd, length, len);
        tls_handle->write_logged = true;
    }
    
    return (length > 0) ? length : -QM_ERROR;
}

int32_t qm_tls_read(void * handle, char *buf, int len, uint32_t timeout_ms)
{
    int ret = 0;
    int rxLen = 0;
    int remain = 0;
    fd_set readset;
    fd_set errset;
    struct timeval t;
    qm_tls_hal_handle_t *tls_handle = (qm_tls_hal_handle_t *)handle;
    
    if (NULL == handle){
        QM_LOGE(LOG_TAG, "handle is NULL");
        return -QM_EINVAL;
    }

    FD_ZERO(&readset);
    FD_ZERO(&errset);
    FD_SET(tls_handle->sockfd, &readset);
    FD_SET(tls_handle->sockfd, &errset);

    if (tls_handle->tls && (remain = esp_tls_get_bytes_avail(tls_handle->tls) > 0)) {
        goto __next;
    }

    ret = select(tls_handle->sockfd + 1, &readset, NULL, &errset, qm_tls_hal_utils_ms_to_timeval(timeout_ms, &t));
    if (ret > 0 && FD_ISSET(tls_handle->sockfd, &errset)) {
        int sock_errno = 0;
        uint32_t optlen = sizeof(sock_errno);
        getsockopt(tls_handle->sockfd, SOL_SOCKET, SO_ERROR, &sock_errno, &optlen);
        QM_LOGE(LOG_TAG, "poll_read select error code=%d errno=%s fd=%d timeout_ms=%u", sock_errno, strerror(sock_errno), tls_handle->sockfd, (unsigned int)timeout_ms);
       return -QM_EIO;
    } else if (ret == 0) {
        return 0;
    }

__next:
    rxLen = esp_tls_conn_read(tls_handle->tls, (unsigned char *)buf, len);
    if (rxLen < 0) {
        QM_LOGE(LOG_TAG, "esp_tls_conn_read error, errno=%s", strerror(errno));
        return -QM_EIO;
    } 
    return rxLen;
}

int32_t qm_tls_destroy(void *handle)
{
    qm_tls_hal_handle_t *tls_handle = (qm_tls_hal_handle_t *)handle;
    if (NULL == handle){
        QM_LOGE(LOG_TAG, "handle is NULL");
        return -QM_EINVAL;
    }

    if(tls_handle->tls){
        if (tls_handle->server_side) {
            esp_tls_server_session_delete(tls_handle->tls);
            if (tls_handle->sockfd >= 0) {
                close(tls_handle->sockfd);
                tls_handle->sockfd = -1;
            }
        } else {
            esp_tls_conn_destroy(tls_handle->tls);
        }
        tls_handle->tls = NULL;
    }
#ifdef CONFIG_ESP_TLS_SERVER_SESSION_TICKETS
    if (tls_handle->server_cfg.ticket_ctx) {
        esp_tls_cfg_server_session_tickets_free(&tls_handle->server_cfg);
    }
#endif
    qm_free(tls_handle);
    return QM_EOK;
}

#else
#include "openssl/ssl.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/err.h"
#include "esp_system.h"

#define LOG_TAG "hal tls"

typedef struct _TLSDataParams
{
    int server_fd;
    SSL_CTX *ctx;
    SSL *ssl;
} TLSDataParams_t, *TLSDataParams_pt;

#define SSL_READ_BUFFER_LEN 8120

void *qm_tls_establish(const char *host, uint16_t port, const qm_tls_cfg_t *cfg)
{
    TLSDataParams_pt pTlsData = NULL;
    SSL_CTX *ctx = NULL;
    SSL *ssl = NULL;
    int ret = -1;
    int server_fd = -1;

    struct sockaddr_in addr;
    struct hostent *hostent_content = NULL;

    (void)cfg;

    if (host == NULL) {
        return NULL;
    }

    QM_LOGD(LOG_TAG, "host name: %s", host);
    hostent_content = gethostbyname(host);
    if (hostent_content == NULL)
    {
        QM_LOGE(LOG_TAG, "gethostbyname err");
        return NULL;
    }

    server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd < 0)
    {
        QM_LOGE(LOG_TAG, "socket creat err");
        return NULL;
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = *(uint32_t *)(*hostent_content->h_addr_list);
    addr.sin_port = htons(port);

    QM_LOGD(LOG_TAG, "ip %s port %d fd %d\n", inet_ntoa(addr.sin_addr), port, server_fd);

    pTlsData = malloc(sizeof(TLSDataParams_t));

    if (NULL == pTlsData)
    {
        return NULL;
    }

    ret = connect(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret)
    {
        QM_LOGE(LOG_TAG, "connect failed");
        goto err_exit;
    }

    QM_LOGD(LOG_TAG, "server_fd %d", server_fd);

    ctx = SSL_CTX_new(TLSv1_1_client_method());
    if (!ctx)
    {
        QM_LOGE(LOG_TAG, "SSL_CTX_new, ret: %d, free_heap :%u", (uint32_t)ctx, esp_get_free_heap_size());
        goto err_exit;
    }
    QM_LOGD(LOG_TAG, "set SSL context read buffer size free_heap: %d", esp_get_free_heap_size());
    SSL_CTX_set_default_read_buffer_len(ctx, SSL_READ_BUFFER_LEN);
    ssl = SSL_new(ctx);
    if (!ssl)
    {
        QM_LOGE(LOG_TAG, "SSL_new failed free_heap :%u", esp_get_free_heap_size());
        goto err_exit;
    }

    SSL_set_fd(ssl, server_fd);

    pTlsData->server_fd = server_fd;
    pTlsData->ctx = ctx;
    pTlsData->ssl = ssl;

    ret = SSL_connect(ssl);
    if (!ret)
    {
        QM_LOGE(LOG_TAG, "SSL_connect, ret: %d,free_heap :%u", ret, esp_get_free_heap_size());
        goto err_exit;
    }

    QM_LOGD(LOG_TAG, "ssl connect success");

    QM_LOGD(LOG_TAG, "SSL_new, ret: %d, free_heap :%u", (uint32_t)ssl, esp_get_free_heap_size());

    return (void*)pTlsData;

err_exit:

    if (pTlsData){
        free(pTlsData);
        pTlsData = NULL;
    }

    if (ctx){
        SSL_CTX_free(ctx);
        ctx = NULL;
    }
    if (ssl){
        SSL_free(ssl);
        ssl = NULL;
    }

    if (server_fd >= 0){
        close(server_fd);
    }

    QM_LOGD(LOG_TAG, "platform_ssl_connect end free_heap :%u", esp_get_free_heap_size());
    return NULL;
}

void *qm_tls_accept(int socket, const qm_tls_server_cfg_t *cfg, uint32_t timeout_ms)
{
    (void)socket;
    (void)cfg;
    (void)timeout_ms;

    QM_LOGE(LOG_TAG, "server-side TLS accept is not supported on this platform");
    return NULL;
}

int32_t qm_tls_destroy(void *handle)
{
    TLSDataParams_pt pTlsData = NULL;
    if (NULL == handle){
        QM_LOGE(LOG_TAG, "handle is NULL");
        return -QM_EINVAL;
    }
    pTlsData = (TLSDataParams_t *)handle;

    close(pTlsData->server_fd);

    if (pTlsData->ssl){
        SSL_free(pTlsData->ssl);
    }
    if (pTlsData->ctx){
        SSL_CTX_free(pTlsData->ctx);
    }
    if (pTlsData){
        free(pTlsData);
    }

    QM_LOGD(LOG_TAG, "ssl_disconnect");
    return QM_EOK;
}

int32_t qm_tls_write(void *handle, const char *buf, int len, uint32_t timeout_ms)
{
    TLSDataParams_t *pTlsData = (TLSDataParams_t *)handle;
    int length = 0, ret = 0;
    (void)timeout_ms;

    if (NULL == handle){
        QM_LOGE(LOG_TAG, "handle is NULL");
        return -QM_EINVAL;
    }

    while (length < len)
    {
        ret = SSL_write(pTlsData->ssl, (char *)(buf + length), (len - length));
        QM_LOGD(LOG_TAG, "ssl send =%d", ret);
        if (ret < 0){
            length = ret;
            break;
        }
        else if (ret > 0){
            length += ret;
        }
        else{
            return length;
        }
    }

    return (length > 0) ? length : -1;
}

int32_t qm_tls_read(void * handle, char *buf, int len, uint32_t timeout_ms)
{
    TLSDataParams_t *pTlsData = (TLSDataParams_t *)handle;
    int rxLen = 0;
    int ret = 0;
    int fd = pTlsData->server_fd;
    fd_set readfds;
    struct timeval t;

    if (NULL == handle){
        QM_LOGE(LOG_TAG, "handle is NULL");
        return -QM_EINVAL;
    }

    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);

    t.tv_sec = timeout_ms / 1000;
    t.tv_usec = (timeout_ms % 1000) * 1000;

    while (len > 0)
    {
        if (SSL_pending(pTlsData->ssl)){
            ret = SSL_read(pTlsData->ssl, buf, len);
        }
        else{

            if (timeout_ms == 0){
                break;
            }
            //platform_log("timeout_ms %u, cur %u", timeout_ms, HAL_UptimeMs());

            ret = select(fd + 1, &readfds, NULL, NULL, &t);
            if (ret > 0)
            {
                if (FD_ISSET(fd, &readfds))
                {
                    ret = SSL_read(pTlsData->ssl, buf, len);
                    if (ret < 0)
                    {
                        QM_LOGE(LOG_TAG, "select ret %d", ret);
                    }
                }
            }
            else if (ret == 0)
            {
                break;
            }
        }

        if (ret >= 0)
        {
            rxLen += ret;
            buf += ret;
            len -= ret;
        }

        if (ret < 0)
        {
            QM_LOGE(LOG_TAG, "socket read err");
            return -QM_EIO;
        }

        if (len == 0)
        {
            QM_LOGD(LOG_TAG, "recv len %d", rxLen);
            return rxLen;
        }
    }

    return rxLen;
}
#endif
