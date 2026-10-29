#include "qm.h"
#include "qm_tls.h"
#include "qm_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LOG_TAG "tls"

#include "qm_utils_timer.h"

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


const char *TAG = "qm tls";

#define QM_TLS_ESTABLISH_TIMEOUT    (60 * 1000)
#define QM_TLS_FINSOLVE_TIMEOUT     (500)

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

typedef struct 
{
	const char *pRootCALocation;                ///< Pointer to string containing the filename (including path) of the root CA file.
	const char *pDeviceCertLocation;            ///< Pointer to string containing the filename (including path) of the device certificate.
	const char *pDevicePrivateKeyLocation;    ///< Pointer to string containing the filename (including path) of the device private key file.
	const char *pDestinationURL;                ///< Pointer to string containing the endpoint of the MQTT service.
	uint16_t DestinationPort;            ///< Integer defining the connection port of the MQTT service.
	uint32_t timeout_ms;                ///< Unsigned integer defining the TLS handshake timeout value in milliseconds.
	bool ServerVerificationFlag;        ///< Boolean.  True = perform server certificate hostname validation.  False = skip validation \b NOT recommended.
}qm_tls_hal_conn_params_t;

typedef struct 
{
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    uint32_t flags;
    mbedtls_x509_crt cacert;
    mbedtls_x509_crt clicert;
    mbedtls_pk_context pkey;
    mbedtls_net_context server_fd;
}qm_tls_hal_data_params_t;


typedef struct 
{
	qm_tls_hal_conn_params_t tlsConnectParams;        ///< TLSConnect params structure containing the common connection parameters
	qm_tls_hal_data_params_t tlsDataParams;            ///< TLSData params structure containing the connection data parameters that are specific to the library being used
}qm_tls_hal_handle_t;


/*
 * This is a function to do further verification if needed on the cert received.
 */
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

void *qm_tls_establish(const char *host, uint16_t port, const qm_tls_cfg_t *cfg)
{
    if(host == NULL || cfg == NULL){
        return NULL;
    }

    int ret = 0;
    char portBuffer[6] = {0};
    char info_buf[256] = {0};
    qm_tls_hal_data_params_t *tlsDataParams = NULL;
    qm_tls_hal_handle_t *tls_handle = (qm_tls_hal_handle_t *)qm_malloc(sizeof(qm_tls_hal_handle_t));
    if(tls_handle == NULL){
        return NULL;
    }
    memset(tls_handle, 0, sizeof(qm_tls_hal_handle_t));

    _iot_tls_set_connect_params(tls_handle, cfg->ca_crt,cfg->client_crt,
        cfg->client_key,
        host,
        port,
        QM_TLS_ESTABLISH_TIMEOUT,
        false
    );
    
    tlsDataParams = &(tls_handle->tlsDataParams);

    mbedtls_net_init(&(tlsDataParams->server_fd));
    mbedtls_ssl_init(&(tlsDataParams->ssl));
    mbedtls_ssl_config_init(&(tlsDataParams->conf));
    
    mbedtls_ctr_drbg_init(&(tlsDataParams->ctr_drbg));
    mbedtls_x509_crt_init(&(tlsDataParams->cacert));
    mbedtls_x509_crt_init(&(tlsDataParams->clicert));
    mbedtls_pk_init(&(tlsDataParams->pkey));

    mbedtls_entropy_init(&(tlsDataParams->entropy));
    if ((ret = mbedtls_ctr_drbg_seed(&(tlsDataParams->ctr_drbg), mbedtls_entropy_func, &(tlsDataParams->entropy), (const unsigned char *) TAG, strlen(TAG))) != 0) {
        QM_LOGE(TAG, "failed! mbedtls_ctr_drbg_seed returned -0x%x", -ret);
        goto exit_failure;
    }

    /*  Load root CA...
        Certs/keys can be paths or they can be raw data. These use a
        very basic heuristic: if the cert starts with '/' then it's a
        path, if it's longer than this then it's raw cert data (PEM or DER,
        neither of which can start with a slash.
    */
    QM_LOGD(TAG, "Loading embedded CA root certificate ...");
    ret = mbedtls_x509_crt_parse(&(tlsDataParams->cacert), (const unsigned char *)tls_handle->tlsConnectParams.pRootCALocation, strlen(tls_handle->tlsConnectParams.pRootCALocation)+1);
    if (ret < 0) {
        QM_LOGE(TAG, "failed!  mbedtls_x509_crt_parse returned -0x%x while parsing root cert", -ret);
        goto exit_failure;
    }

    /* Load client certificate... */
    QM_LOGD(TAG, "Loading embedded client certificate...");
    ret = mbedtls_x509_crt_parse(&(tlsDataParams->clicert), (const unsigned char *)tls_handle->tlsConnectParams.pDeviceCertLocation, strlen(tls_handle->tlsConnectParams.pDeviceCertLocation)+1);
    if(ret != 0) {
        QM_LOGE(TAG, "failed!  mbedtls_x509_crt_parse returned -0x%x while parsing device cert", -ret);
        goto exit_failure;
    }

    /* Parse client private key... */
    QM_LOGD(TAG, "Loading embedded client private key...");
    ret = mbedtls_pk_parse_key(&(tlsDataParams->pkey),
                                (const unsigned char *)tls_handle->tlsConnectParams.pDevicePrivateKeyLocation,
                                strlen(tls_handle->tlsConnectParams.pDevicePrivateKeyLocation)+1,
                                (const unsigned char *)"", 0);
    if (ret != 0) {
        QM_LOGE(TAG, "failed!  mbedtls_pk_parse_key returned -0x%x while parsing private key", -ret);
        goto exit_failure;
    }

    /* Done parsing certs */
    snprintf(portBuffer, 6, "%d", tls_handle->tlsConnectParams.DestinationPort);
    QM_LOGD(TAG, "Connecting to %s/%s...", tls_handle->tlsConnectParams.pDestinationURL, portBuffer);
    if ((ret = mbedtls_net_connect(&(tlsDataParams->server_fd), tls_handle->tlsConnectParams.pDestinationURL, portBuffer, MBEDTLS_NET_PROTO_TCP, 1)) != 0) {
        QM_LOGE(TAG, "failed! mbedtls_net_connect returned -0x%x", -ret);
        goto exit_failure;
    }

    ret = mbedtls_net_set_block(&(tlsDataParams->server_fd));
    if (ret != 0) {
        QM_LOGE(TAG, "failed! net_set_(non)block() returned -0x%x", -ret);
        goto exit_failure;
    }
    QM_LOGD(TAG, "Setting up the SSL/TLS structure...");
    if((ret = mbedtls_ssl_config_defaults(&(tlsDataParams->conf), MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
        QM_LOGE(TAG, "failed! mbedtls_ssl_config_defaults returned -0x%x", -ret);
        goto exit_failure;
    }

    mbedtls_ssl_conf_verify(&(tlsDataParams->conf), _iot_tls_verify_cert, NULL);

    if (tls_handle->tlsConnectParams.ServerVerificationFlag == true) {
        mbedtls_ssl_conf_authmode(&(tlsDataParams->conf), MBEDTLS_SSL_VERIFY_REQUIRED);
    } else {
        mbedtls_ssl_conf_authmode(&(tlsDataParams->conf), MBEDTLS_SSL_VERIFY_OPTIONAL);
    }
    mbedtls_ssl_conf_rng(&(tlsDataParams->conf), mbedtls_ctr_drbg_random, &(tlsDataParams->ctr_drbg));

    mbedtls_ssl_conf_ca_chain(&(tlsDataParams->conf), &(tlsDataParams->cacert), NULL);
    ret = mbedtls_ssl_conf_own_cert(&(tlsDataParams->conf), &(tlsDataParams->clicert), &(tlsDataParams->pkey));
    if (ret != 0) {
        QM_LOGE(TAG, "failed! mbedtls_ssl_conf_own_cert returned %d", ret);
        goto exit_failure;
    }

    mbedtls_ssl_conf_read_timeout(&(tlsDataParams->conf), tls_handle->tlsConnectParams.timeout_ms);
    //mbedtls_ssl_conf_dbg(&(tlsDataParams->conf), aws_debug, stdout );
    //mbedtls_debug_set_threshold(20);
    if ((ret = mbedtls_ssl_setup(&(tlsDataParams->ssl), &(tlsDataParams->conf))) != 0) {
        QM_LOGE(TAG, "failed! mbedtls_ssl_setup returned -0x%x", -ret);
        goto exit_failure;
    }
    if ((ret = mbedtls_ssl_set_hostname(&(tlsDataParams->ssl), tls_handle->tlsConnectParams.pDestinationURL)) != 0) {
        QM_LOGE(TAG, "failed! mbedtls_ssl_set_hostname returned %d", ret);
        goto exit_failure;
    }
    QM_LOGD(TAG, "SSL state connect : %d ", tlsDataParams->ssl.state);
    mbedtls_ssl_set_bio(&(tlsDataParams->ssl), &(tlsDataParams->server_fd), mbedtls_net_send, NULL, mbedtls_net_recv_timeout);

    QM_LOGD(TAG, "SSL state connect : %d ", tlsDataParams->ssl.state);
    QM_LOGD(TAG, "Performing the SSL/TLS handshake...");
    while ((ret = mbedtls_ssl_handshake(&(tlsDataParams->ssl))) != 0) {
        if(ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            QM_LOGE(TAG, "failed! mbedtls_ssl_handshake returned -0x%x", -ret);
            if(ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
                QM_LOGE(TAG, "    Unable to verify the server's certificate. ");
            }
            goto exit_failure;
        }
    }

    QM_LOGD(TAG, "ok [ Protocol is %s ]    [ Ciphersuite is %s ]", mbedtls_ssl_get_version(&(tlsDataParams->ssl)), mbedtls_ssl_get_ciphersuite(&(tlsDataParams->ssl)));
    if ((ret = mbedtls_ssl_get_record_expansion(&(tlsDataParams->ssl))) >= 0) {
        QM_LOGD(TAG, " [ Record expansion is %d ]", ret);
    } else {
        QM_LOGD(TAG, " [ Record expansion is unknown (compression) ]");
    }

    QM_LOGD(TAG, "Verifying peer X.509 certificate...");
    if (tls_handle->tlsConnectParams.ServerVerificationFlag == true) {
        if ((tlsDataParams->flags = mbedtls_ssl_get_verify_result(&(tlsDataParams->ssl))) != 0) {
            QM_LOGE(TAG, "failed");
            mbedtls_x509_crt_verify_info(info_buf, sizeof(info_buf), "  ! ", tlsDataParams->flags);
            QM_LOGE(TAG, "%s", info_buf);
            goto exit_failure;
        } else {
            QM_LOGD(TAG, "ok");
            ret = QM_EOK;
        }
    } else {
        QM_LOGW(TAG, " Server Verification skipped");
        ret = QM_EOK;
    }

    if (mbedtls_ssl_get_peer_cert(&(tlsDataParams->ssl)) != NULL) {
        QM_LOGD(TAG, "Peer certificate information:");
        mbedtls_x509_crt_info((char *) info_buf, sizeof(info_buf) - 1, "      ", mbedtls_ssl_get_peer_cert(&(tlsDataParams->ssl)));
        QM_LOGD(TAG, "%s", info_buf);
    }

    return (void *)tls_handle;

exit_failure:


    qm_free(tls_handle);
    return NULL;
}

int32_t qm_tls_write(void *handle, const char *buf, int len, uint32_t timeout_ms)
{
    size_t written_so_far;
    bool isErrorFlag = false;
    int frags, ret = 0;
    qm_utils_time_t timer = {0};
    qm_tls_hal_handle_t *tls_handle = (qm_tls_hal_handle_t *)handle;
    
    if (NULL == handle){
        QM_LOGE(LOG_TAG, "handle is NULL");
        return -QM_EINVAL;
    }

    qm_tls_hal_data_params_t *tlsDataParams = &(tls_handle->tlsDataParams);

    qm_utils_time_countdown_ms(&timer, timeout_ms);
    
    for (written_so_far = 0, frags = 0; written_so_far < len && !qm_utils_time_is_expired(&timer); written_so_far += ret, frags++) {
        while (!qm_utils_time_is_expired(&timer) && (ret = mbedtls_ssl_write(&(tlsDataParams->ssl), (const unsigned char *)(buf + written_so_far), len - written_so_far)) <= 0) {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                QM_LOGE(TAG, "failed! mbedtls_ssl_write returned -0x%x", -ret);
                /* All other negative return values indicate connection needs to be reset.
                * Will be caught in ping request so ignored here */
                isErrorFlag = true;
                break;
            }
        }
        if (isErrorFlag) {
            break;
        }
    }

    return (written_so_far > 0) ? written_so_far : -QM_ERROR;
}

int32_t qm_tls_read(void * handle, char *buf, int len, uint32_t timeout_ms)
{
    int ret;
    size_t rxLen = 0;
    qm_utils_time_t timer = {0};
    uint32_t read_timeout;
    static qm_utils_time_t timeOutCheck = {0};
    qm_utils_time_t latestTimer ={0};
    int timeDiff =0;
    qm_tls_hal_handle_t *tls_handle = (qm_tls_hal_handle_t *)handle;
    
    if (NULL == handle){
        QM_LOGE(LOG_TAG, "handle is NULL");
        return -QM_EINVAL;
    }

    qm_tls_hal_data_params_t *tlsDataParams = &(tls_handle->tlsDataParams);
    mbedtls_ssl_context *ssl = &(tlsDataParams->ssl);
    mbedtls_ssl_config *ssl_conf = &(tlsDataParams->conf);

    qm_utils_time_countdown_ms(&timer, timeout_ms);
    read_timeout = ssl_conf->read_timeout;

    while (len > 0) {
        /* Make sure we never block on read for longer than timer has left,
         but also that we don't block indefinitely (ie read_timeout > 0) */
        mbedtls_ssl_conf_read_timeout(ssl_conf, MAX(1, MIN(read_timeout, qm_utils_time_left(&timer))));

        ret = mbedtls_ssl_read(ssl, (unsigned char *)buf, len);
        /* Restore the old timeout */
        mbedtls_ssl_conf_read_timeout(ssl_conf, read_timeout);
        
        if(ret == MBEDTLS_ERR_SSL_TIMEOUT) {  
            qm_utils_time_start(&timeOutCheck);
        }
        else{
            qm_utils_time_start(&latestTimer);
            timeDiff=latestTimer.time-timeOutCheck.time;
            if(timeDiff>QM_TLS_FINSOLVE_TIMEOUT){
                return -QM_ETIMEOUT;
            }
        }

        if (ret > 0) {
            rxLen += ret;
            buf += ret;
            len -= ret;
        } else if (ret == 0 || (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret != MBEDTLS_ERR_SSL_TIMEOUT)) {
            return 0;
        }

        // Evaluate timeout after the read to make sure read is done at least once
        if (qm_utils_time_is_expired(&timer)) {
            break;
        }
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
    
    qm_tls_hal_data_params_t *tlsDataParams = &(tls_handle->tlsDataParams);

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
