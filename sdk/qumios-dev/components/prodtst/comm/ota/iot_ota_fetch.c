
#include "iot_ota_fetch.h"
#include "iot_ota.h"
#include "util_httpc.h"
#include "qm_errno.h"
#include "qm_kernel.h"
#include "qm_config.h"


static const char *iot_ca_crt = \
{
    \
    "-----BEGIN CERTIFICATE-----\r\n"
    "MIID7TCCAtWgAwIBAgIUW2hy+Z8GXqsduppLsPQn++BCtPswDQYJKoZIhvcNAQEL\r\n"
    "BQAwgYUxCzAJBgNVBAYTAkNOMREwDwYDVQQIDAhaaGVqaWFuZzERMA8GA1UEBwwI\r\n"
    "SGFuZ3pob3UxDTALBgNVBAoMBEVNUVgxDTALBgNVBAsMBEVNUVgxCzAJBgNVBAMM\r\n"
    "AkNBMSUwIwYJKoZIhvcNAQkBFhZ6aGFvbGlqdW5AeGlhb2ppYW5nLmNjMB4XDTIx\r\n"
    "MDQyMTEyNDQyNFoXDTMxMDQxOTEyNDQyNFowgYUxCzAJBgNVBAYTAkNOMREwDwYD\r\n"
    "VQQIDAhaaGVqaWFuZzERMA8GA1UEBwwISGFuZ3pob3UxDTALBgNVBAoMBEVNUVgx\r\n"
    "DTALBgNVBAsMBEVNUVgxCzAJBgNVBAMMAkNBMSUwIwYJKoZIhvcNAQkBFhZ6aGFv\r\n"
    "bGlqdW5AeGlhb2ppYW5nLmNjMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKC\r\n"
    "AQEAyf2wjINoQ5l1sxjP9LDaJ03gqR9ixdAT555NweJCeE5Hyh0gaNIzI1VJ5V6m\r\n"
    "E9M2lexgGoaheE2mdZJ+XnrZnYpdWTFjs3Lr1lUyxPQs/OlH0aXvYBMrfZvP2+Xi\r\n"
    "RYY1kaEsN2VZhEF9uo7eK7d6vQ41KAHiJFiiYUJVh86ZXaHx7xBYmCuGEDqLkUoz\r\n"
    "UydCvuALp1ruu9mT/iE1k5qFiETk+1EwzkaVFmVFoX/AA9iB53RpDgx6KfNsG/ao\r\n"
    "GCwglYmO7wo4ZmzEGrG252jExu60rCXx3v8JJO+ut91V3mL0kiLZAwhmvUudQBNv\r\n"
    "XKrWHNderoUUXabysIuD3W/GjQIDAQABo1MwUTAdBgNVHQ4EFgQUgvfBWoG4qUok\r\n"
    "gW0f4f1O3TsuFhEwHwYDVR0jBBgwFoAUgvfBWoG4qUokgW0f4f1O3TsuFhEwDwYD\r\n"
    "VR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEAHIq1ZmJYxBvqTwxanKIe\r\n"
    "umDNdku+QsPVwZ3X6uYtek5vrlsJcoRkFve4UL2G6xSkQKSwGQ14zKPooUKOtgVi\r\n"
    "EkgoYM5A/vAk6jFB0PQVr0RKVpyT2sSwhOY4EUmMnStGcErA2KyACjKMNiSiEGBd\r\n"
    "Ns5tLZEvjKrbXh9kRdCjBpb+0Y0UQtDY+JJ3v3+J0r/xARPiteZW7omzg0/OsYR6\r\n"
    "ETLzDB2S1G735jQ0vOlGx9Ri3ljumMLiyGAeGTb12F2GOSzX5ljmvZsBRe3PJLmk\r\n"
    "b3Ja4jUtfQFO5hn15Z40BxYWwPb9g3nB8cQltnnoZsA0CEmlbMlRfocqN1yjPcgI\r\n"
    "lg==\r\n"
    "-----END CERTIFICATE-----"

};

typedef struct {

    char url[128];
    uint16_t port;
    httpclient_t http;              /* http client */
    httpclient_data_t http_data;    /* http client data */

} ota_http_info_t;


void *ota_fetch_init(const char *url, uint16_t port)
{
    ota_http_info_t *http_info = NULL;

    if (NULL == (http_info = qm_malloc(sizeof(ota_http_info_t)))) {
        OTA_LOG_ERROR("allocate for h_odc failed");
        return NULL;
    }

    memset(http_info, 0, sizeof(ota_http_info_t));

    /* set http request-header parameter */
    http_info->http.header = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n" \
                         "Accept-Encoding: gzip, deflate\r\n";

    strncpy(http_info->url, url, strlen(url));

#if CONFIG_QM_NETWORK_TLS_SUPPORT
    if(http_info->port == 0){
        http_info->port = 443;
#else
    if(http_info->port == 0){
        http_info->port = 80;
#endif
    }else{
        http_info->port = port;
    }
    return http_info;
}


int ota_fetch(void *handle, char *buf, uint32_t buf_len, uint32_t timeout)
{
    int diff = 0;
    ota_http_info_t *http_info = (ota_http_info_t*)handle;

    http_info->http_data.response_buf = buf;
    http_info->http_data.response_buf_len = buf_len;
    diff = http_info->http_data.response_content_len - http_info->http_data.retrieve_len;

    if (0 != http_client_common(&http_info->http, http_info->url, http_info->port, NULL, HTTPCLIENT_GET, timeout, &http_info->http_data)){
        OTA_LOG_ERROR("http client failed");
        return -QM_EIO;
    }
    return http_info->http_data.response_content_len - http_info->http_data.retrieve_len - diff;
}


int ota_fetch_deinit(void *handle)
{
    ota_http_info_t *http_info = (ota_http_info_t*)handle;
    if (NULL == handle) {
        return -QM_EINVAL;
    }

    http_client_close(&http_info->http);

    qm_free(handle);
    return QM_EOK;
}