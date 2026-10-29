#include "qm.h"
#include "ntpdate.h"
#include "weekly_timer.h"
#include "qm_log.h"
#include "qm_work.h"
#include "qm_wifi.h"
#include "qm_event.h"

#define LOG_TAG "weekly example"

#define EXAMPLE_WIFI_SSID       "xjiang"
#define EXAMPLE_WIFI_PASSWORD   "xjwl2015"

#define EXAMPLE_WIFI_RETRY_MAX_COUNT   10
static int retry_count = 0;

static weekly_timer_handle_t weekly_timer_handle = NULL;

static void ntpdate_complete_callback(ntpdate_res_t res)
{
    if(res == NTPDATE_RES_SUCCESS){
        QM_LOGD(LOG_TAG, "time update success");
        QM_LOGD(LOG_TAG, "%s", qm_time_print());

        QM_LOGD(LOG_TAG, "weekly timer start");
        weekly_timer_start(weekly_timer_handle);
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


static void weekly_timer_callback(void *arg)
{
    QM_LOGD(LOG_TAG, "weekly timer trigger");
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

    event_time_t event_time = {
        .hour = 11,
        .minute = 47,
        .second = 0,
        .en = 0,
        .tm_cb = weekly_timer_callback,
        .arg = NULL
    };

    weekday_mask_t weekday_mask = {
        .en = 0
    };

    weekly_timer_handle = weekly_timer_add(0, weekday_mask, 1, &event_time);
}
