#include "qm.h"
#include "ntpdate.h"
#include "qm_time.h"
#include "qm_log.h"
#include "qm_work.h"
#include "qm_wifi.h"
#include "qm_event.h"
#include "qm_rtc.h"

#define LOG_TAG "time example"

#define EXAMPLE_WIFI_SSID       "xjiang"
#define EXAMPLE_WIFI_PASSWORD   "xjwl2015"

#define EXAMPLE_WIFI_RETRY_MAX_COUNT   10
static int retry_count = 0;

static void ntpdate_complete_callback(ntpdate_res_t res)
{   
    struct qm_tm *tm = NULL;
    qm_time_t now = {0};
    struct qm_timezone tz = {0};
    if(res == NTPDATE_RES_SUCCESS){
        QM_LOGD(LOG_TAG, "time update success");
        tz.minuteswest = 8*60;
        qm_settimezone(&tz);

        QM_LOGD(LOG_TAG, "%s", qm_time_print());

        now = qm_time(NULL);
        QM_LOGD(LOG_TAG, "UTC time: %llu", now);

        tm = qm_localtime(&now);
        QM_LOGD(LOG_TAG, "time: %04d-%02d-%02d %02d:%02d:%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);

        QM_LOGD(LOG_TAG, "UTC time: %llu", qm_mktime(tm));
    }
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

            ntpdate_start(ntpdate_complete_callback);

        break;

        case QM_WIFI_EVENT_STA_LOST_IP:

        break;

        case QM_WIFI_EVENT_STA_DISCONNECTED:

            QM_LOGD(LOG_TAG, "sta disconnected reason: %d", event_info->sta_disconnected.reason);

            if (retry_count < EXAMPLE_WIFI_RETRY_MAX_COUNT) {
                qm_wifi_connect();
                retry_count++;
                QM_LOGD(LOG_TAG, "retry to connect to the AP");
            }

        break;

        default:
        break;
    }
}

void qm_application_start(void)
{
    qm_wifi_config_t config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_WIFI_SSID),
            .password = EXAMPLE_WIFI_PASSWORD,
            .password_len = strlen(EXAMPLE_WIFI_PASSWORD),
        },
    };

    qm_event_handler_register(QM_EVENT_WIFI, QM_EVENT_ANY_ID, qm_event_handler, NULL);

    qm_wifi_init();

    qm_wifi_set_mode(QM_WIFI_MODE_STA);
    qm_wifi_set_config(QM_WIFI_IF_STA, &config);
    qm_wifi_start();
}
