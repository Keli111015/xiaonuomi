#include "qm.h"
#include "qm_wifi.h"
#include "dns_server.h"
#include "qm_log.h"
#include "qm_work.h"

#define LOG_TAG "dns server"

#define DNS_DOMAIN "test.com"
#define DNS_IP "192.168.4.1"


#define EXAMPLE_WIFI_SSID       "wells"
#define EXAMPLE_WIFI_PASSWORD   "1234567890"
#define EXAMPLE_MAX_STA_CONN    5

static void qm_event_handler(void *event_handler_arg, qm_event_base_t event_base,
                             int32_t event_id, void *event_data, size_t event_data_size)
{
    qm_wifi_event_info_t *event_info = (qm_wifi_event_info_t*)event_data;

    QM_UNUSED_ARG(event_handler_arg);
    QM_UNUSED_ARG(event_base);
    QM_UNUSED_ARG(event_data_size);

    switch(event_id){

        case QM_WIFI_EVENT_AP_START:

            QM_LOGD(LOG_TAG, "AP Start");
        break;

        case QM_WIFI_EVENT_AP_STOP:

        break;

        case QM_WIFI_EVENT_AP_STACONNECTED:
            QM_LOGD(LOG_TAG, "station join "QM_MACSTR" ", QM_MAC2STR(event_info->ap_staconnected.mac));

        break;

        case QM_WIFI_EVENT_AP_STAIPASSIGNED:
            QM_LOGD(LOG_TAG, "station ip:" QM_IPSTR, QM_IP2STR(&event_info->ap_staipassigned.ip));

        break;

        case QM_WIFI_EVENT_AP_STADISCONNECTED:
            QM_LOGD(LOG_TAG, "station leave "QM_MACSTR" ", QM_MAC2STR(event_info->ap_stadisconnected.mac));

        break;

        default:
        break;
    }
}

static void wifi_start(void)
{
    qm_err_t ret = QM_EOK;

    qm_wifi_config_t config = {
        .ap = {
            .ssid = EXAMPLE_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_WIFI_SSID),
            .password = EXAMPLE_WIFI_PASSWORD,
            .password_len = strlen(EXAMPLE_WIFI_PASSWORD),
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = QM_WIFI_AUTH_WPA_WPA2_PSK,
        },
    };

    qm_event_handler_register(QM_EVENT_WIFI, QM_EVENT_ANY_ID, qm_event_handler, NULL);

    qm_wifi_init();

    qm_wifi_set_mode(QM_WIFI_MODE_AP);
    qm_wifi_set_config(QM_WIFI_IF_AP, &config);
    qm_wifi_start();
}

void qm_application_start(void)
{
    dns_server_ip_register(DNS_DOMAIN, DNS_IP);

    wifi_start();

    dns_server_start();
}