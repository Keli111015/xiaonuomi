#include "qm.h"

#include "qm_work.h"
#include "qm_wifi.h"
#include "qm_tls.h"

#define LOG_TAG "XJIANG"


#define PRODTST_WIFI_SSID       "hl"
#define PRODTST_WIFI_PASSWORD   "12345678"

static const char *ca_crt = \
{
    \
    "-----BEGIN CERTIFICATE-----\r\n"
    "MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\r\n"
    "ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\r\n"
    "b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\r\n"
    "MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\r\n"
    "b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\r\n"
    "ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\r\n"
    "9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\r\n"
    "IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\r\n"
    "VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\r\n"
    "93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\r\n"
    "jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\r\n"
    "AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\r\n"
    "A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\r\n"
    "U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\r\n"
    "N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\r\n"
    "o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\r\n"
    "5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\r\n"
    "rqXRfboQnoZsG4q5WTP468SQvvG5\r\n"
    "-----END CERTIFICATE-----"
};

static const char *client_ca_crt = \
{
    \
    "-----BEGIN CERTIFICATE-----\r\n"
    "MIIDWTCCAkGgAwIBAgIUMRkx5tAoO0Nk3f+XLN7zWI94MicwDQYJKoZIhvcNAQEL\r\n"
    "BQAwTTFLMEkGA1UECwxCQW1hem9uIFdlYiBTZXJ2aWNlcyBPPUFtYXpvbi5jb20g\r\n"
    "SW5jLiBMPVNlYXR0bGUgU1Q9V2FzaGluZ3RvbiBDPVVTMB4XDTI0MDgyNDE3MTUy\r\n"
    "N1oXDTQ5MTIzMTIzNTk1OVowHjEcMBoGA1UEAwwTQVdTIElvVCBDZXJ0aWZpY2F0\r\n"
    "ZTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAK0U32q4KIkb7JM2jFa3\r\n"
    "kK0k78gztOYB0SeWVSNyFX4SVD4A2mPlLkGvShTxBgyYEwfCszlMjxBYvaWKpEE/\r\n"
    "zjypGOkYTCNEtFv3bLwQx6IVHz7KtoXfYIvwZIA5v4P7r1A8wruH6EfKRioJ0L7N\r\n"
    "1Itqn1KIRZv855b1dVsuIZj0alDeOCg3mOFJwa2svFFli8Ri0j5cTQXuwhzIpyEk\r\n"
    "nN1GQF0hLxREoJ/zviCae54O54P1a/zV9/7j2SGeMcBFw2ierMkZQb3R2sPw2QtJ\r\n"
    "zKyzms14Kg9oloWfz6e8x1cx++MpQZlBAq6zlZ0wJsC+IWSNp8ZxPWW7bvVkooYG\r\n"
    "558CAwEAAaNgMF4wHwYDVR0jBBgwFoAU8wImPz9Og3a5U9zAKTOBemlgooowHQYD\r\n"
    "VR0OBBYEFD+aUGX272Wfqa6kRrq6OL7625vbMAwGA1UdEwEB/wQCMAAwDgYDVR0P\r\n"
    "AQH/BAQDAgeAMA0GCSqGSIb3DQEBCwUAA4IBAQAgvbTUb3wLdGL8bMuZ1WHrb2h9\r\n"
    "juBgeUBZKnNZ2mbKNAoxz9vuZOSbruyehVN63C/sTd/+lynI5Hab7J8B0DzY0hVU\r\n"
    "sNPN8dL/6Lyt4DFUjZXZ/b1J6Qutc0C07nf+nxQ23rwBrNv08GWa8/T3vVxR5LPo\r\n"
    "HTaYPLroByDR8+fNBXs+HhiZo2ekOpGgimxbpjbBrSjmkgTgBim7JIPOUJT3I92F\r\n"
    "Tr4TnKdkI8gFAfLfG1z9tkl1lLGG7w86tRpZ/DpqKPBjL86nZgI+fRKTIjD1M1vA\r\n"
    "yz1cBYSRdXA558d4RWO8Os1WzFNdUSPVBdi36atcpcwqkxYHDZeobA8PMaJG\r\n" 
    "-----END CERTIFICATE-----"
};

static const char *client_ca_key = \
{
    \
    "-----BEGIN RSA PRIVATE KEY-----\r\n"
    "MIIEpAIBAAKCAQEArRTfargoiRvskzaMVreQrSTvyDO05gHRJ5ZVI3IVfhJUPgDa\r\n"
    "Y+UuQa9KFPEGDJgTB8KzOUyPEFi9pYqkQT/OPKkY6RhMI0S0W/dsvBDHohUfPsq2\r\n"
    "hd9gi/BkgDm/g/uvUDzCu4foR8pGKgnQvs3Ui2qfUohFm/znlvV1Wy4hmPRqUN44\r\n"
    "KDeY4UnBray8UWWLxGLSPlxNBe7CHMinISSc3UZAXSEvFESgn/O+IJp7ng7ng/Vr\r\n"
    "/NX3/uPZIZ4xwEXDaJ6syRlBvdHaw/DZC0nMrLOazXgqD2iWhZ/Pp7zHVzH74ylB\r\n"
    "mUECrrOVnTAmwL4hZI2nxnE9Zbtu9WSihgbnnwIDAQABAoIBAAzUcUM953NSaJT5\r\n"
    "BCohbU4IYOXcP1qVY+WlVSZNfJ5dyXTXJ1kkUet4jOtEeohQdYirvBFRRYGWsmgl\r\n"
    "AMv9aNJCTqBotqwemaf/jUXbkJDJNAm5iRIfXs7AwRJoNXQZIgO2nVC9YFCCA/V1\r\n"
    "yM18AHddLfY2N8K6WohsJYjewIrP5B3t48LyHHTdLzoTyMEoNz9vEm4+YhqEYUhb\r\n"
    "it8tXZ4mVCAB1qZWIAkx/OkeuUf/ocAo+7paUbdzmMI7oKoWgryC4qsGAV3psS67\r\n"
    "dB4giIVi0NDiiRhuJF7iy0ypa+uWib4FPIZdcW9iKf5aI7/n/B+7lF5iCnyep+Hd\r\n"
    "XdNaKyECgYEA20iG8KyYuOJVbhYpD00wg1B4i7y0ifiK5KomER1d5KQxntuDwTjQ\r\n"
    "DWXfI/a9aG8kuA2amY6kPEtBeOIN+uRntsV3cfFK6l2tgxaLWxidDa6f2zRQwUEy\r\n"
    "06QKu6i0ywYn89Pl4lhvvtcEk8ijDW2LUtkCOVKbBYT7tx0YDo/4/pcCgYEAyg/t\r\n"
    "zAYCL9Uh2gHNgzsr16yZjSfsP8pVpcLmtek0ZFsFH6TscHuctdTm2yH6dV8jAWqe\r\n"
    "EZiPUZU2O0OnP4QeS7bGu6Nj36c5/un2yMyX8w+be/OdjmFGDhPmHZ2cI0JCjrj/\r\n"
    "/0LSVFh+xRi7yGFwxsLJSrnm0xZVdJOfU/MaiDkCgYEAiZGPro/pZBwCUoUujz3y\r\n"
    "0H78kVX6wZAeuuQP667Lx/RGeQ3oM6FLzQv7GJnkLA+GLr3CHtIBMR5ZXdgbwynl\r\n"
    "8yEhFWe2gx/wCgxrsuPXK81A1omUnBkmJOaGIULu4WvkRrDKSN0IheZpJbm6qWLv\r\n"
    "BDPGlGXBgY3zSObEv+YM5NMCgYEAyKgC1F2PJGL5p92scUp8YkPjhFqF8F8EqISg\r\n"
    "yTsZrSL6No93wMfwOl1/F1Npvc7JG2n+KKkggbq0TSwE1T1lPHj4Z3N9BaeyHyPF\r\n"
    "z2fk6RmxpOiqqK6OfAJkvTo7yIPPRp4OkjWQWvQ6h43lRLsG3EqozE1KHIsMN6U1\r\n"
    "To+W5HkCgYBb+s6PgR2rUQ4GVxFx74t9THhMbCtTlsYyB9MZRNIo0RAnklQVJrkr\r\n"
    "B6gooLjdwB5Zc3lGI2sKxzbA0YP5vHxibBRIBH2SlJfD4jICeGXOW+HgTIZLQsBx\r\n"
    "mhw8UaO2XQzifoT66oWJ+NhR0nvlRJ/a1tL3d+uakvlkgOWfW/b2MQ==\r\n"
    "-----END RSA PRIVATE KEY-----"
};

static int qm_tls_example(void)
{
    qm_tls_cfg_t tls_cfg = {0};

    tls_cfg.ca_crt = ca_crt;
    tls_cfg.ca_crt_len = strlen(ca_crt);
    tls_cfg.client_crt = client_ca_crt;
    tls_cfg.client_crt_len = strlen(client_ca_crt);
    tls_cfg.client_key = client_ca_key;
    tls_cfg.client_key_len = strlen(client_ca_key);

    if (NULL != (qm_tls_establish("a3aao8sovulqg6-ats.iot.us-east-1.amazonaws.com", 8883, &tls_cfg))) {
        QM_LOGD(LOG_TAG, "CONNECT COMPLETE!!");

        return QM_EOK;
    } else {
        /* TODO SHOLUD not remove this handle space */
        /* The space will be freed by calling disconnect_ssl() */
        /* util_memory_free((void *)pNetwork->handle); */

        QM_LOGD(LOG_TAG, "CONNECT FAILED!!");
        
        return -QM_EIO;
    }
    return QM_EOK;
}


static void qm_event_handler(void *event_handler_arg, qm_event_base_t event_base,
                             int32_t event_id, void *event_data, size_t event_data_size)
{
    qm_wifi_event_info_t *event_info = (qm_wifi_event_info_t*)event_data;

    QM_UNUSED_ARG(event_handler_arg);
    QM_UNUSED_ARG(event_base);
    QM_UNUSED_ARG(event_data_size);

    switch(event_id){

        case QM_WIFI_EVENT_STA_START:
            QM_LOGD(LOG_TAG, "connect to the AP");
            qm_wifi_connect();
        break;

        case QM_WIFI_EVENT_STA_STOP:

        break;

        case QM_WIFI_EVENT_STA_CONNECTED:
            QM_LOGD(LOG_TAG, "connected to the AP");
        break;

        case QM_WIFI_EVENT_STA_GOT_IP:
            QM_LOGD(LOG_TAG, "sta got ip");

            QM_LOGD(LOG_TAG, "ip:"      QM_IPSTR, QM_IP2STR(&event_info->got_ip.ip_info.ip));
            QM_LOGD(LOG_TAG, "netmask:" QM_IPSTR, QM_IP2STR(&event_info->got_ip.ip_info.netmask));
            QM_LOGD(LOG_TAG, "gw:"      QM_IPSTR, QM_IP2STR(&event_info->got_ip.ip_info.gw));

            qm_tls_example();

        break;

        case QM_WIFI_EVENT_STA_LOST_IP:

        break;

        case QM_WIFI_EVENT_STA_DISCONNECTED:

            QM_LOGD(LOG_TAG, "sta disconnected reason: %d", event_info->sta_disconnected.reason);
            qm_wifi_connect();
            QM_LOGD(LOG_TAG, "retry to connect to the AP");
     
        break;

        default:
        break;
    }
}

void qm_application_start(void)
{
    qm_wifi_config_t config = {
        .sta = {
            .ssid = PRODTST_WIFI_SSID,
            .ssid_len = strlen(PRODTST_WIFI_SSID),
            .password = PRODTST_WIFI_PASSWORD,
            .password_len = strlen(PRODTST_WIFI_PASSWORD),
        },
    };

    qm_event_handler_register(QM_EVENT_WIFI, QM_EVENT_ANY_ID, qm_event_handler, NULL);
    qm_wifi_init();

    qm_wifi_set_mode(QM_WIFI_MODE_STA);
    qm_wifi_set_config(QM_WIFI_IF_STA, &config);
    qm_wifi_start();
}