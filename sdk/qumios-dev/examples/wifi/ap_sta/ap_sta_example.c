#include "qm.h"
#include "qm_wifi.h"
#include "qm_work.h"
#include "qm_event.h"
#include "qm_errno.h"
#include "qm_log.h"

#define LOG_TAG  "wifi sta"

#define EXAMPLE_WIFI_SSID       "xjiang"
#define EXAMPLE_WIFI_PASSWORD   "xjwl2015"
#define EXAMPLE_MAX_STA_CONN    5

#define EXAMPLE_WIFI_RETRY_MAX_COUNT   10
static int retry_count = 0;

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

        break;

        case QM_WIFI_EVENT_STA_LOST_IP:

        break;

        case QM_WIFI_EVENT_STA_DISCONNECTED:

                QM_LOGD(LOG_TAG, "sta disconnected reason: %d", event_info->sta_disconnected.reason);
                qm_wifi_connect();
                QM_LOGD(LOG_TAG, "retry to connect to the AP");

        break;

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

void qm_application_start(void)
{
    qm_err_t ret = QM_EOK;

    qm_wifi_config_t sta_config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_WIFI_SSID),
            .password = EXAMPLE_WIFI_PASSWORD,
            .password_len = strlen(EXAMPLE_WIFI_PASSWORD),
        },
    };

    qm_wifi_config_t ap_config = {
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

    qm_wifi_set_mode(QM_WIFI_MODE_APSTA);

    qm_wifi_set_config(QM_WIFI_IF_AP, &sta_config);
    qm_wifi_set_config(QM_WIFI_IF_STA, &ap_config);

    qm_wifi_start();
}