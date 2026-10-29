#include "qm.h"
#include "qm_tls.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <sys/select.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define LOG_TAG "mc3303 tls"

#include <openssl/ssl.h>

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

    struct hostent *hostent_content = NULL;
    struct sockaddr_in addr;

    if (NULL == host || NULL == cfg) {
        QM_LOGE(LOG_TAG, "input param is NULL");
        return NULL;
    }

    QM_LOGD(LOG_TAG, "qm_tls_establish start");
    QM_LOGD(LOG_TAG, "host name: %s", host);
    
    // 创建TLS数据结构
    pTlsData = malloc(sizeof(TLSDataParams_t));
    if (NULL == pTlsData) {
        QM_LOGE(LOG_TAG, "malloc failed");
        return NULL;
    }
    memset(pTlsData, 0, sizeof(TLSDataParams_t));

    // 创建SSL上下文
    ctx = SSL_CTX_new(TLS_client_method());
    if (NULL == ctx) {
        QM_LOGE(LOG_TAG, "SSL_CTX_new failed");
        goto err_exit;
    }
    
    // 设置最小和最大协议版本
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
    
    // 如果提供了CA证书，则加载它
    // 如果提供了CA证书，则加载它
    if (cfg->ca_crt && cfg->ca_crt_len > 0) {
        QM_LOGD(LOG_TAG, "Loading CA certificate, length: %d", cfg->ca_crt_len);
        
        // 创建一个BIO来加载证书
        BIO *bio = BIO_new_mem_buf((void*)cfg->ca_crt, cfg->ca_crt_len);
        if (!bio) {
            QM_LOGE(LOG_TAG, "Failed to create BIO for CA certificate");
            goto err_exit;
        }
        
        // 尝试以PEM格式加载
        X509 *cacert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
        if (NULL == cacert) {
            // 如果PEM格式失败，重置BIO指针并尝试DER格式
            BIO_reset(bio);
            cacert = d2i_X509_bio(bio, NULL);
        }
        
        BIO_free(bio);
        
        if (NULL == cacert) {
            QM_LOGE(LOG_TAG, "Failed to load CA certificate, both DER and PEM formats attempted");
            // 打印前100个字符用于调试
            QM_LOGD(LOG_TAG, "CA cert begin: %.100s", cfg->ca_crt);
            goto err_exit;
        }
        
        // 将CA证书添加到证书存储中
        X509_STORE *store = SSL_CTX_get_cert_store(ctx);
        if (X509_STORE_add_cert(store, cacert) != 1) {
            QM_LOGE(LOG_TAG, "Failed to add CA certificate to store");
            X509_free(cacert);
            goto err_exit;
        }
        X509_free(cacert);
    }
    
    // 如果提供了客户端证书，则加载它
    if (cfg->client_crt && cfg->client_crt_len > 0) {
        QM_LOGD(LOG_TAG, "Loading client certificate, length: %d", cfg->client_crt_len);
        
        // 创建一个BIO来加载证书
        BIO *bio = BIO_new_mem_buf((void*)cfg->client_crt, cfg->client_crt_len);
        if (!bio) {
            QM_LOGE(LOG_TAG, "Failed to create BIO for client certificate");
            goto err_exit;
        }
        
        // 尝试以PEM格式加载
        X509 *client_cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
        if (NULL == client_cert) {
            // 如果PEM格式失败，重置BIO指针并尝试DER格式
            BIO_reset(bio);
            client_cert = d2i_X509_bio(bio, NULL);
        }
        
        BIO_free(bio);
        
        if (NULL == client_cert) {
            QM_LOGE(LOG_TAG, "Failed to load client certificate, both DER and PEM formats attempted");
            // 打印前100个字符用于调试
            QM_LOGD(LOG_TAG, "Client cert begin: %.100s", cfg->client_crt);
            goto err_exit;
        }
        
        if (0 == SSL_CTX_use_certificate(ctx, client_cert)) {
            QM_LOGE(LOG_TAG, "SSL_CTX_use_certificate failed");
            X509_free(client_cert);
            goto err_exit;
        }
        X509_free(client_cert);
    }
    
    // 如果提供了客户端私钥，则加载它
    if (cfg->client_key && cfg->client_key_len > 0) {
        QM_LOGD(LOG_TAG, "Loading client private key, length: %d", cfg->client_key_len);
        
        // 创建一个BIO来加载私钥
        BIO *bio = BIO_new_mem_buf((void*)cfg->client_key, cfg->client_key_len);
        if (!bio) {
            QM_LOGE(LOG_TAG, "Failed to create BIO for private key");
            goto err_exit;
        }
        
        // 尝试以PEM格式加载
        EVP_PKEY *client_key = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
        if (NULL == client_key) {
            // 如果PEM格式失败，重置BIO指针并尝试DER格式
            BIO_reset(bio);
            client_key = d2i_PrivateKey_bio(bio, NULL);
        }
        
        BIO_free(bio);
        
        if (NULL == client_key) {
            QM_LOGE(LOG_TAG, "Failed to load private key, both DER and PEM formats attempted");
            // 打印前100个字符用于调试
            QM_LOGD(LOG_TAG, "Client key begin: %.100s", cfg->client_key);
            goto err_exit;
        }
        
        if (0 == SSL_CTX_use_PrivateKey(ctx, client_key)) {
            QM_LOGE(LOG_TAG, "SSL_CTX_use_PrivateKey failed");
            EVP_PKEY_free(client_key);
            goto err_exit;
        }
        EVP_PKEY_free(client_key);
        
        // 验证私钥是否与证书匹配
        if (SSL_CTX_check_private_key(ctx) != 1) {
            QM_LOGE(LOG_TAG, "Private key does not match the certificate");
            goto err_exit;
        }
    }
    
    // 创建socket并连接
    server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd < 0)
    {
        QM_LOGE(LOG_TAG, "create socket error");
        goto err_exit;
    }

    hostent_content = gethostbyname(host);
    if (hostent_content == NULL)
    {
        QM_LOGE(LOG_TAG, "gethostbyname failed, errno: %d", h_errno);
        goto err_exit;
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = *(uint32_t *)(*hostent_content->h_addr_list);
    addr.sin_port = htons(port);

    QM_LOGD(LOG_TAG, "ip %s port %d fd %d", inet_ntoa(addr.sin_addr), port, server_fd);

    ret = connect(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret)
    {
        QM_LOGE(LOG_TAG, "connect failed, errno: %d", errno);
        goto err_exit;
    }

    QM_LOGD(LOG_TAG, "server_fd %d", server_fd);

    // 创建SSL对象
    ssl = SSL_new(ctx);
    if (!ssl)
    {
        QM_LOGE(LOG_TAG, "SSL_new failed");
        goto err_exit;
    }

    // 将socket与SSL关联
    SSL_set_fd(ssl, server_fd);
    SSL_set_connect_state(ssl);

    // 设置读取缓冲区大小
    BIO_set_read_buffer_size(SSL_get_rbio(ssl), SSL_READ_BUFFER_LEN);
    QM_LOGD(LOG_TAG, "set SSL context read buffer size");

    // 保存SSL对象和文件描述符
    pTlsData->server_fd = server_fd;
    pTlsData->ctx = ctx;
    pTlsData->ssl = ssl;

    ret = SSL_connect(ssl);
    if (ret != 1) {
        QM_LOGE(LOG_TAG, "SSL_connect failed, ret: %d", ret);
        int ssl_error = SSL_get_error(ssl, ret);
        QM_LOGE(LOG_TAG, "SSL_get_error returns %d", ssl_error);
        goto err_exit;
    }

    QM_LOGD(LOG_TAG, "ssl connect success");

    // 检查SSL会话状态
    long verify_result = SSL_get_verify_result(ssl);
    if (verify_result != X509_V_OK) {
        QM_LOGW(LOG_TAG, "SSL certificate verification warning: %ld", verify_result);
        // 对于某些证书错误，我们仍然可以继续连接（例如自签名证书）
        switch (verify_result) {
            case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
            case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
                QM_LOGW(LOG_TAG, "Allowing self-signed certificate");
                break;
            case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
                QM_LOGW(LOG_TAG, "Unable to get issuer certificate, but continuing");
                break;
            default:
                QM_LOGE(LOG_TAG, "Certificate verification failed, not continuing");
                goto err_exit;
        }
    }
    
    // 验证SSL连接是否真正可用
    if (SSL_get_peer_certificate(ssl) == NULL) {
        QM_LOGW(LOG_TAG, "No peer certificate received");
    }
    
    // 打印协商的协议版本和密码套件
    QM_LOGD(LOG_TAG, "Negotiated TLS version: %s", SSL_get_version(ssl));
    QM_LOGD(LOG_TAG, "Negotiated cipher: %s", SSL_get_cipher_name(ssl));
    
    // 尝试进行一次测试写入来确认连接是否真正可用
    // 发送一个空的SSL记录来测试连接
    int test_ret = SSL_write(ssl, "", 0);
    if (test_ret < 0) {
        int ssl_error = SSL_get_error(ssl, test_ret);
        if (ssl_error == SSL_ERROR_SYSCALL && errno == 0) {
            QM_LOGE(LOG_TAG, "SSL connection test failed with spurious SSL_ERROR_SYSCALL, errno=0");
            // 这种情况下连接可能不可用，我们应该关闭它
            goto err_exit;
        } else if (ssl_error == SSL_ERROR_SYSCALL && errno == ECONNRESET) {
            QM_LOGE(LOG_TAG, "SSL connection test failed - connection reset by peer immediately after handshake");
            QM_LOGE(LOG_TAG, "This typically indicates the server rejected our connection due to certificate issues");
            goto err_exit;
        }
    }

    QM_LOGD(LOG_TAG, "SSL connection fully established and tested successfully");
    QM_LOGD(LOG_TAG, "SSL_new, ret: %d", (uint32_t)ssl);
    QM_LOGD(LOG_TAG, "platform_ssl_connect end");
    
    // 保存SSL对象和文件描述符
    pTlsData->ssl = ssl;
    pTlsData->server_fd = server_fd;
    pTlsData->ctx = ctx;
    
    return (void*)pTlsData;

err_exit:
    QM_LOGD(LOG_TAG, "platform_ssl_connect end");
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    if (ctx) {
        SSL_CTX_free(ctx);
    }
    if (server_fd >= 0) {
        close(server_fd);
    }
    if (pTlsData) {
        free(pTlsData);
    }
    return NULL;
}

int32_t qm_tls_write(void *handle, const char *buf, int len, uint32_t timeout_ms)
{
    TLSDataParams_t *pTlsData = (TLSDataParams_t *)handle;
    int length = 0, ret = 0;
    if (NULL == handle){
        QM_LOGE(LOG_TAG, "handle is NULL");
        return -QM_EINVAL;
    }
    while (length < len)
    {
        ret = SSL_write(pTlsData->ssl, (char *)(buf + length), (len - length));

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
            // QM_LOGD(LOG_TAG, "recv len %d", rxLen);
            return rxLen;
        }
    }

    return rxLen;
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

void *qm_tls_accept(int socket, const qm_tls_server_cfg_t *cfg, uint32_t timeout_ms)
{
    (void)socket;
    (void)cfg;
    (void)timeout_ms;
    /* Combo IPC does not run a TLS server in the Linux process. */
    QM_LOGW(LOG_TAG, "qm_tls_accept is not implemented on MC3303");
    return NULL;
}
