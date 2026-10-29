#include "qm.h"
#include "qm_tls.h"
#include "qm_utils_timer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mbedtls/config.h"
#include "mbedtls/platform.h"
#include "mbedtls/net.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/certs.h"
#include "mbedtls/x509.h"
#include "mbedtls/error.h"
#include "mbedtls/debug.h"
#include "mbedtls/timing.h"
#include "mbedtls/pk.h"
#include "mbedtls/net_sockets.h"

#define TLS_DEBUG_LOG   0

#define LOG_TAG "TLS"


const char *TAG = "qm tls";

#define QM_TLS_ESTABLISH_TIMEOUT    (10 * 1000)  // TLS握手总超时时间

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

// TLS连接配置参数结构体
typedef struct 
{
	const char *pRootCALocation;                ///< 根CA证书路径/内容
	const char *pDeviceCertLocation;            ///< 设备证书路径/内容
	const char *pDevicePrivateKeyLocation;    ///< 设备私钥路径/内容
	const char *pDestinationURL;                ///< 目标服务器地址
	uint16_t DestinationPort;            ///< 目标服务器端口
	uint32_t timeout_ms;                ///< TLS握手超时时间(毫秒)
	bool ServerVerificationFlag;        ///< 服务器证书验证标志
}qm_tls_hal_conn_params_t;

// TLS运行时数据结构体(包含mbedtls上下文)
typedef struct 
{
    mbedtls_entropy_context entropy;         // 熵源上下文
    mbedtls_ctr_drbg_context ctr_drbg;       // 随机数生成器上下文
    mbedtls_ssl_context ssl;                 // TLS会话上下文
    mbedtls_ssl_config conf;                 // TLS配置上下文
    uint32_t flags;                          // 验证标志
    mbedtls_x509_crt cacert;                 // CA证书链
    mbedtls_x509_crt clicert;                // 客户端证书
    mbedtls_pk_context pkey;                 // 客户端私钥
    mbedtls_net_context server_fd;           // 网络套接字上下文
}qm_tls_hal_data_params_t;

// TLS连接句柄结构体
typedef struct 
{
	qm_tls_hal_conn_params_t tlsConnectParams;        ///< TLS连接配置参数
	qm_tls_hal_data_params_t tlsDataParams;            ///< TLS运行时数据
}qm_tls_hal_handle_t;


/*
 * 证书验证回调函数(调试用)
 */
#if TLS_DEBUG_LOG
static int _iot_tls_verify_cert(void *data, mbedtls_x509_crt *crt, int depth, uint32_t *flags) 
{
    char buf[256];
    ((void) data);
    QM_LOGD(TAG, "Verify requested for (Depth %d):", depth);
    mbedtls_x509_crt_info(buf, sizeof(buf) - 1, "", crt);
    QM_LOGD(TAG, "%s", buf);
    
    if((*flags) == 0) {
        QM_LOGD(TAG, "  This certificate has no flags");
    } else {
        QM_LOGD(TAG, "Verify result:%s", buf);
    }
    return 0;
}
#endif

// 设置TLS连接参数
static void _iot_tls_set_connect_params(qm_tls_hal_handle_t *pNetwork, const char *pRootCALocation, const char *pDeviceCertLocation,
    const char *pDevicePrivateKeyLocation, const char *pDestinationURL,
    uint16_t destinationPort, uint32_t timeout_ms, bool ServerVerificationFlag) 
{
    pNetwork->tlsConnectParams.DestinationPort = destinationPort;
    pNetwork->tlsConnectParams.pDestinationURL = pDestinationURL;
    pNetwork->tlsConnectParams.pDeviceCertLocation = pDeviceCertLocation;
    pNetwork->tlsConnectParams.pDevicePrivateKeyLocation = pDevicePrivateKeyLocation;
    pNetwork->tlsConnectParams.pRootCALocation = pRootCALocation;
    pNetwork->tlsConnectParams.timeout_ms = timeout_ms;
    pNetwork->tlsConnectParams.ServerVerificationFlag = ServerVerificationFlag;
}

/**
 * 建立TLS连接
 * 返回值: 成功返回TLS句柄,失败返回NULL
 * 阻塞点: TCP连接、TLS握手过程会阻塞,总超时由QM_TLS_ESTABLISH_TIMEOUT控制
 */
void *qm_tls_establish(const char *host, uint16_t port, const qm_tls_cfg_t *cfg)
{
    if(host == NULL || cfg == NULL){
        return NULL;
    }

    int ret = 0;
    int conn = 0;
    int count = 0;
    char portBuffer[6] = {0};
    char info_buf[256] = {0};
    qm_tls_hal_data_params_t *tlsDataParams = NULL;
    // 分配TLS句柄内存
    qm_tls_hal_handle_t *tls_handle = (qm_tls_hal_handle_t *)qm_malloc(sizeof(qm_tls_hal_handle_t));
    if(tls_handle == NULL){
        return NULL;
    }
    memset(tls_handle, 0, sizeof(qm_tls_hal_handle_t));
    // 初始化连接参数
    _iot_tls_set_connect_params(tls_handle, cfg->ca_crt,cfg->client_crt,
        cfg->client_key,
        host,
        port,
        QM_TLS_ESTABLISH_TIMEOUT,
        false
    );
    
    tlsDataParams = &(tls_handle->tlsDataParams);

    // 初始化mbedtls组件
    mbedtls_net_init(&(tlsDataParams->server_fd));
    mbedtls_ssl_init(&(tlsDataParams->ssl));
    mbedtls_ssl_config_init(&(tlsDataParams->conf));
    
    mbedtls_ctr_drbg_init(&(tlsDataParams->ctr_drbg));
    mbedtls_x509_crt_init(&(tlsDataParams->cacert));
    mbedtls_x509_crt_init(&(tlsDataParams->clicert));
    mbedtls_pk_init(&(tlsDataParams->pkey));

    mbedtls_entropy_init(&(tlsDataParams->entropy));
    // 初始化随机数生成器(非阻塞,仅初始化)
    if ((ret = mbedtls_ctr_drbg_seed(&(tlsDataParams->ctr_drbg), mbedtls_entropy_func, &(tlsDataParams->entropy), (const unsigned char *) TAG, strlen(TAG))) != 0) {
        QM_LOGE(TAG, "failed! mbedtls_ctr_drbg_seed returned -0x%x", -ret);
        goto exit_failure;
    }

    // 加载根CA证书(纯CPU操作,非阻塞)
#if TLS_DEBUG_LOG
    QM_LOGD(TAG, "Loading embedded CA root certificate ...");
#endif
    ret = mbedtls_x509_crt_parse(&(tlsDataParams->cacert), (const unsigned char *)tls_handle->tlsConnectParams.pRootCALocation, strlen(tls_handle->tlsConnectParams.pRootCALocation)+1);
    if (ret < 0) {
        QM_LOGE(TAG, "failed!  mbedtls_x509_crt_parse returned -0x%x while parsing root cert", -ret);
        goto exit_failure;
    }

    // 加载客户端证书(纯CPU操作,非阻塞)
#if TLS_DEBUG_LOG
    QM_LOGD(TAG, "Loading embedded client certificate...");
#endif
    ret = mbedtls_x509_crt_parse(&(tlsDataParams->clicert), (const unsigned char *)tls_handle->tlsConnectParams.pDeviceCertLocation, strlen(tls_handle->tlsConnectParams.pDeviceCertLocation)+1);
    if(ret != 0) {
        QM_LOGE(TAG, "failed!  mbedtls_x509_crt_parse returned -0x%x while parsing device cert", -ret);
        goto exit_failure;
    }

    // 加载客户端客户端私钥(纯CPU操作,非阻塞)
#if TLS_DEBUG_LOG
    QM_LOGD(TAG, "Loading embedded client private key...");
#endif
    ret = mbedtls_pk_parse_key(&(tlsDataParams->pkey),
                                (const unsigned char *)tls_handle->tlsConnectParams.pDevicePrivateKeyLocation,
                                strlen(tls_handle->tlsConnectParams.pDevicePrivateKeyLocation)+1,
                                (const unsigned char *)"", 0);
    if (ret != 0) {
        QM_LOGE(TAG, "failed!  mbedtls_pk_parse_key returned -0x%x while parsing private key", -ret);
        goto exit_failure;
    }

    // 准备端口字符串
    snprintf(portBuffer, 6, "%d", tls_handle->tlsConnectParams.DestinationPort);
#if TLS_DEBUG_LOG
    QM_LOGD(TAG, "Connecting to %s/%s...", tls_handle->tlsConnectParams.pDestinationURL, portBuffer);
#endif
    // 建立TCP连接[阻塞操作],超时由系统底层控制(通常依赖操作系统默认超时)
    if ((ret = mbedtls_net_connect(&(tlsDataParams->server_fd), tls_handle->tlsConnectParams.pDestinationURL, portBuffer, MBEDTLS_NET_PROTO_TCP)) != 0) {
        QM_LOGE(TAG, "failed! mbedtls_net_connect returned -0x%x", -ret);
        goto exit_failure;
    }

    // 设置套接字为阻塞模式[关键],后续I/O操作将阻塞等待
    ret = mbedtls_net_set_block(&(tlsDataParams->server_fd));
    if (ret != 0) {
        QM_LOGE(TAG, "failed! net_set_(non)block() returned -0x%x", -ret);
        goto exit_failure;
    }
#if TLS_DEBUG_LOG
    QM_LOGD(TAG, "Setting up the SSL/TLS structure...");
#endif
    // 初始化TLS配置(非阻塞)
    if((ret = mbedtls_ssl_config_defaults(&(tlsDataParams->conf), MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
        QM_LOGE(TAG, "failed! mbedtls_ssl_config_defaults returned -0x%x", -ret);
        goto exit_failure;
    }

#if TLS_DEBUG_LOG
    mbedtls_ssl_conf_verify(&(tlsDataParams->conf), _iot_tls_verify_cert, NULL);
#endif
    // 配置证书验证模式(非阻塞)
    if (tls_handle->tlsConnectParams.ServerVerificationFlag == true) {
        mbedtls_ssl_conf_authmode(&(tlsDataParams->conf), MBEDTLS_SSL_VERIFY_REQUIRED);
    } else {
        mbedtls_ssl_conf_authmode(&(tlsDataParams->conf), MBEDTLS_SSL_VERIFY_OPTIONAL);
    }
    mbedtls_ssl_conf_rng(&(tlsDataParams->conf), mbedtls_ctr_drbg_random, &(tlsDataParams->ctr_drbg));

    // 配置CA证书链(非阻塞)
    mbedtls_ssl_conf_ca_chain(&(tlsDataParams->conf), &(tlsDataParams->cacert), NULL);
    // 配置客户端证书和私钥(非阻塞)
    ret = mbedtls_ssl_conf_own_cert(&(tlsDataParams->conf), &(tlsDataParams->clicert), &(tlsDataParams->pkey));
    if (ret != 0) {
        QM_LOGE(TAG, "failed! mbedtls_ssl_conf_own_cert returned %d", ret);
        goto exit_failure;
    }

    // 设置TLS读取超时(非阻塞,仅配置)
    mbedtls_ssl_conf_read_timeout(&(tlsDataParams->conf), tls_handle->tlsConnectParams.timeout_ms);
    // 初始化TLS会话(非阻塞)
    if ((ret = mbedtls_ssl_setup(&(tlsDataParams->ssl), &(tlsDataParams->conf))) != 0) {
        QM_LOGE(TAG, "failed! mbedtls_ssl_setup returned -0x%x", -ret);
        goto exit_failure;
    }
    // 设置服务器域名(非阻塞)
    if ((ret = mbedtls_ssl_set_hostname(&(tlsDataParams->ssl), tls_handle->tlsConnectParams.pDestinationURL)) != 0) {
        QM_LOGE(TAG, "failed! mbedtls_ssl_set_hostname returned %d", ret);
        goto exit_failure;
    }
#if TLS_DEBUG_LOG
    QM_LOGD(TAG, "SSL state connect : %d ", tlsDataParams->ssl.state);
#endif
    // 绑定网络I/O函数到TLS会话(非阻塞)
    mbedtls_ssl_set_bio(&(tlsDataParams->ssl), &(tlsDataParams->server_fd), mbedtls_net_send, NULL, mbedtls_net_recv_timeout);
#if TLS_DEBUG_LOG
    QM_LOGD(TAG, "SSL state connect : %d ", tlsDataParams->ssl.state);
    QM_LOGD(TAG, "Performing the SSL/TLS handshake...");
#endif
    // 执行TLS握手[核心阻塞操作],总耗时受QM_TLS_ESTABLISH_TIMEOUT控制
    // 循环处理握手状态:可能多次阻塞等待读写操作
    while ((ret = mbedtls_ssl_handshake(&(tlsDataParams->ssl))) != 0) {
        // WANT_READ/WANT_WRITE表示需要等待I/O,继续循环重试
        if(ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            QM_LOGE(TAG, "failed! mbedtls_ssl_handshake returned -0x%x", -ret);
            if(ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
                QM_LOGE(TAG, "    Unable to verify the server's certificate. ");
            }
            goto exit_failure;
        }
        // 此处为阻塞点:当返回WANT_READ/WANT_WRITE时,下一次调用mbedtls_ssl_handshake会阻塞等待数据
    }
#if TLS_DEBUG_LOG
    QM_LOGD(TAG, "ok [ Protocol is %s ]    [ Ciphersuite is %s ]", mbedtls_ssl_get_version(&(tlsDataParams->ssl)), mbedtls_ssl_get_ciphersuite(&(tlsDataParams->ssl)));

    if ((ret = mbedtls_ssl_get_record_expansion(&(tlsDataParams->ssl))) >= 0) {
        QM_LOGD(TAG, " [ Record expansion is %d ]", ret);
    } else {
        QM_LOGD(TAG, " [ Record expansion is unknown (compression) ]");
    }
    QM_LOGD(TAG, "Verifying peer X.509 certificate...");
#endif

    // 验证服务器证书(非阻塞,仅CPU计算)
    if (tls_handle->tlsConnectParams.ServerVerificationFlag == true) {
        if ((tlsDataParams->flags = mbedtls_ssl_get_verify_result(&(tlsDataParams->ssl))) != 0) {
            QM_LOGE(TAG, "failed");
            mbedtls_x509_crt_verify_info(info_buf, sizeof(info_buf), "  ! ", tlsDataParams->flags);
            QM_LOGE(TAG, "%s", info_buf);
            goto exit_failure;
        } else {
            ret = QM_EOK;
        }
    } else {
        QM_LOGW(TAG, " Server Verification skipped");
        ret = QM_EOK;
    }

    // 获取并打印对端证书信息(非阻塞)
    if (mbedtls_ssl_get_peer_cert(&(tlsDataParams->ssl)) != NULL) {
#if TLS_DEBUG_LOG
        QM_LOGD(TAG, "Peer certificate information:");
#endif
        mbedtls_x509_crt_info((char *) info_buf, sizeof(info_buf) - 1, "      ", mbedtls_ssl_get_peer_cert(&(tlsDataParams->ssl)));
    #if TLS_DEBUG_LOG
        QM_LOGD(TAG, "%s", info_buf);
    #endif
    }

    return (void *)tls_handle;

exit_failure:

    if(conn){
        // 发送TLS关闭通知[可能阻塞],最多重试5次
        do {
            ret = mbedtls_ssl_close_notify(&tlsDataParams->ssl);
        } while(ret == MBEDTLS_ERR_SSL_WANT_WRITE && ++count <= 5);
    }

    // 释放资源(非阻塞)
    mbedtls_net_free(&(tlsDataParams->server_fd));
    mbedtls_x509_crt_free(&(tlsDataParams->clicert));
    mbedtls_x509_crt_free(&(tlsDataParams->cacert));
    mbedtls_pk_free(&(tlsDataParams->pkey));
    mbedtls_ssl_free(&(tlsDataParams->ssl));
    mbedtls_ssl_config_free(&(tlsDataParams->conf));
    mbedtls_ctr_drbg_free(&(tlsDataParams->ctr_drbg));
    mbedtls_entropy_free(&(tlsDataParams->entropy));


    qm_free(tls_handle);
    return NULL;
}

/**
 * 通过TLS连接发送数据
 * 返回值: 成功返回发送字节数,失败返回负数
 * 阻塞点: mbedtls_ssl_write会阻塞等待I/O,总超时由timeout_ms控制
 */
int32_t qm_tls_write(void *handle, const char *buf, int len, uint32_t timeout_ms)
{
    size_t written_so_far;
    bool isErrorFlag = false;
    int frags, ret = 0;
    qm_utils_time_t timer = {0};
    qm_tls_hal_handle_t *tls_handle = (qm_tls_hal_handle_t *)handle;
    if (NULL == handle){
        QM_LOGE(LOG_TAG, "write handle is NULL");
        return -QM_EINVAL;
    }

    qm_tls_hal_data_params_t *tlsDataParams = &(tls_handle->tlsDataParams);

    // 初始化超时计时器
    qm_utils_time_countdown_ms(&timer, timeout_ms);
    
    // 循环发送数据直到完成或超时
    for (written_so_far = 0, frags = 0; written_so_far < len && !qm_utils_time_is_expired(&timer); written_so_far += ret, frags++) {
        // 循环调用发送函数,处理阻塞状态
        while (!qm_utils_time_is_expired(&timer) && (ret = mbedtls_ssl_write(&(tlsDataParams->ssl), (const unsigned char *)(buf + written_so_far), len - written_so_far)) <= 0) {
            // WANT_READ/WANT_WRITE表示需要等待I/O,继续重试
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                QM_LOGE(TAG, "failed! mbedtls_ssl_write returned -0x%x", -ret);
                isErrorFlag = true;
                break;
            }
            // 此处为阻塞点:mbedtls_ssl_write会阻塞直到数据发送或超时
        }
        if (isErrorFlag) {
            break;
        }
    }

    return (written_so_far > 0) ? written_so_far : -QM_ERROR;
}

/**
 * 通过TLS连接接收数据
 * 返回值: 成功返回接收字节数,超时/失败返回0
 * 阻塞点: mbedtls_ssl_read会阻塞等待数据,每次读取超时动态调整(不超过总timeout_ms)
 */
int32_t qm_tls_read(void * handle, char *buf, int len, uint32_t timeout_ms)
{
    int ret;
    size_t rxLen = 0;
    qm_utils_time_t timer = {0};
    uint32_t read_timeout;
    qm_tls_hal_handle_t *tls_handle = (qm_tls_hal_handle_t *)handle;
    
    if (NULL == handle){
        QM_LOGE(LOG_TAG, "read handle is NULL");
        return -QM_EINVAL;
    }

    qm_tls_hal_data_params_t *tlsDataParams = &(tls_handle->tlsDataParams);
    mbedtls_ssl_context *ssl = &(tlsDataParams->ssl);
    mbedtls_ssl_config *ssl_conf = &(tlsDataParams->conf);

    // 初始化超时计时器
    qm_utils_time_countdown_ms(&timer, timeout_ms);
    read_timeout = ssl_conf->read_timeout;

    // 循环接收数据直到达到长度或超时
    while (len > 0) {
        // 动态调整单次读取超时(不超过剩余总超时)
        mbedtls_ssl_conf_read_timeout(ssl_conf, MAX(1, MIN(read_timeout, qm_utils_time_left(&timer))));

        // 读取数据[阻塞操作],超时由上面设置的read_timeout控制
        ret = mbedtls_ssl_read(ssl, (unsigned char *)buf, len);
        // 恢复原始超时配置
        mbedtls_ssl_conf_read_timeout(ssl_conf, read_timeout);

        if (ret > 0) {  // 成功读取数据
            rxLen += ret;
            buf += ret;
            len -= ret;
        } else if (ret == 0 || (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret != MBEDTLS_ERR_SSL_TIMEOUT)) {
            // 连接关闭或错误
            return 0;
        }

        // 检查总超时
        if (qm_utils_time_is_expired(&timer)) {
            break;
        }
        // 此处为阻塞点:mbedtls_ssl_read会阻塞直到有数据或超时
    }

    return rxLen;
}

/**
 * 销毁TLS连接
 * 返回值: 成功返回QM_EOK,失败返回错误码
 * 阻塞点: 发送关闭通知可能阻塞(最多重试5次)
 */
int32_t qm_tls_destroy(void *handle)
{
    int count = 0;
    int ret = QM_EOK;
    qm_tls_hal_handle_t *tls_handle = (qm_tls_hal_handle_t *)handle;
    if (NULL == handle){
        QM_LOGE(LOG_TAG, "destory handle is NULL");
        return -QM_EINVAL;
    }
    
    qm_tls_hal_data_params_t *tlsDataParams = &(tls_handle->tlsDataParams);

    // 发送TLS关闭通知[可能阻塞],确保优雅断开连接
    do {
        ret = mbedtls_ssl_close_notify(&tlsDataParams->ssl);
    } while(ret == MBEDTLS_ERR_SSL_WANT_WRITE && ++count <= 5);

    // 释放所有资源(非阻塞)
    mbedtls_net_free(&(tlsDataParams->server_fd));
    mbedtls_x509_crt_free(&(tlsDataParams->clicert));
    mbedtls_x509_crt_free(&(tlsDataParams->cacert));
    mbedtls_pk_free(&(tlsDataParams->pkey));
    mbedtls_ssl_free(&(tlsDataParams->ssl));
    mbedtls_ssl_config_free(&(tlsDataParams->conf));
    mbedtls_ctr_drbg_free(&(tlsDataParams->ctr_drbg));
    mbedtls_entropy_free(&(tlsDataParams->entropy));

    qm_free(tls_handle);
    return QM_EOK;
}
    
