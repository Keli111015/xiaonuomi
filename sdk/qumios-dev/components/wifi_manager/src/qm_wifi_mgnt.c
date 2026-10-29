#include "qm_wifi_mgnt.h"
#include "qm_wifi_dev.h"
#include "qm_errno.h"
#include "qm_kv.h"
#include "qm_work.h"

/** Wi-Fi event base used by event-loop APIs. */
QM_EVENT_DEFINE_BASE(QM_EVENT_WIFI);

typedef enum {
    QM_WIFI_STATE_NONE,
    QM_WIFI_STATE_STARTING,
    QM_WIFI_STATE_CONNECTING,
    QM_WIFI_STATE_CONNECTED,
    QM_WIFI_STATE_DISCONNECTED,
}qm_wifi_state_t;
typedef struct {

    qm_wifi_dev_t *wifi_dev;
    qm_wifi_state_t wifi_state;
    qm_work_t reconnect_work;
    qm_work_t connect_timeout_work;
    uint8_t auto_reconnect;
    uint8_t reconnect_count;
    qm_wifi_reason_t last_reason;
}wifi_mgnt_ctx_t;

static wifi_mgnt_ctx_t g_wifi_mgnt_ctx = {0};
static int g_wifi_initialized;

#define QM_WIFI_AUTO_RECONNECT_DELAY_MS 3000
#define QM_WIFI_AUTO_RECONNECT_MAX_DELAY_MS 30000
#define QM_WIFI_AUTO_RECONNECT_MAX_RETRY 10
#define QM_WIFI_CONNECT_TIMEOUT_MS 15000

static void wifi_auto_reconnect_action(void *arg);
static void wifi_connect_timeout_action(void *arg);

static uint32_t wifi_reconnect_delay_ms(void)
{
    uint32_t delay = QM_WIFI_AUTO_RECONNECT_DELAY_MS;
    uint8_t i;

    for (i = 1; i < g_wifi_mgnt_ctx.reconnect_count; ++i) {
        if (delay >= QM_WIFI_AUTO_RECONNECT_MAX_DELAY_MS / 2) {
            return QM_WIFI_AUTO_RECONNECT_MAX_DELAY_MS;
        }
        delay *= 2;
    }
    return delay;
}

static void wifi_schedule_reconnect(void)
{
    uint32_t delay;

    if (!g_wifi_mgnt_ctx.auto_reconnect) {
        return;
    }
    if (g_wifi_mgnt_ctx.reconnect_count < QM_WIFI_AUTO_RECONNECT_MAX_RETRY) {
        ++g_wifi_mgnt_ctx.reconnect_count;
    }
    delay = wifi_reconnect_delay_ms();
    QM_LOGW("WIFI", "schedule reconnect count=%u delay=%u reason=%d",
            g_wifi_mgnt_ctx.reconnect_count, delay,
            g_wifi_mgnt_ctx.last_reason);
    (void)qm_cancel_delayed_action(&g_wifi_mgnt_ctx.reconnect_work);
    (void)qm_post_delayed_action(&g_wifi_mgnt_ctx.reconnect_work,
                                 wifi_auto_reconnect_action, NULL, delay);
}

static int wifi_manager_ready(void)
{
    return g_wifi_initialized && g_wifi_mgnt_ctx.wifi_dev != NULL;
}

static void wifi_connect_timeout_action(void *arg)
{
    (void)arg;
    if (!wifi_manager_ready() || !g_wifi_mgnt_ctx.auto_reconnect ||
        g_wifi_mgnt_ctx.wifi_state != QM_WIFI_STATE_CONNECTING) {
        return;
    }

    QM_LOGW("WIFI", "connect timeout after %u ms", QM_WIFI_CONNECT_TIMEOUT_MS);
    g_wifi_mgnt_ctx.wifi_state = QM_WIFI_STATE_DISCONNECTED;
    g_wifi_mgnt_ctx.last_reason = QM_WIFI_REASON_CONNECTION_FAIL;
    wifi_schedule_reconnect();
}

static void wifi_auto_reconnect_action(void *arg)
{
    int ret;

    (void)arg;
    if (!wifi_manager_ready() || !g_wifi_mgnt_ctx.auto_reconnect ||
        g_wifi_mgnt_ctx.wifi_state != QM_WIFI_STATE_DISCONNECTED ||
        g_wifi_mgnt_ctx.wifi_dev->wifi_cfg.mode != QM_WIFI_MODE_STA ||
        g_wifi_mgnt_ctx.wifi_dev->wifi_cfg.config.sta.ssid[0] == '\0') {
        return;
    }

    g_wifi_mgnt_ctx.wifi_state = QM_WIFI_STATE_CONNECTING;
    (void)qm_cancel_delayed_action(&g_wifi_mgnt_ctx.connect_timeout_work);
    (void)qm_post_delayed_action(&g_wifi_mgnt_ctx.connect_timeout_work,
                                 wifi_connect_timeout_action, NULL,
                                 QM_WIFI_CONNECT_TIMEOUT_MS);
    ret = qm_wifi_dev_connect(g_wifi_mgnt_ctx.wifi_dev);
    if (ret != QM_EOK) {
        (void)qm_cancel_delayed_action(&g_wifi_mgnt_ctx.connect_timeout_work);
        g_wifi_mgnt_ctx.wifi_state = QM_WIFI_STATE_DISCONNECTED;
        wifi_schedule_reconnect();
    }
}


static void wifi_event_hander(qm_wifi_event_t event, qm_wifi_event_info_t *event_info, void *arg)
{
    switch(event){

        case QM_WIFI_EVENT_READY:

            qm_event_post(QM_EVENT_WIFI, (uint16_t)event, NULL, 0, QM_NO_WAIT);
        break;

        case QM_WIFI_EVENT_STA_START:
            g_wifi_mgnt_ctx.wifi_state = QM_WIFI_STATE_CONNECTING;
            (void)qm_cancel_delayed_action(&g_wifi_mgnt_ctx.connect_timeout_work);
            (void)qm_post_delayed_action(&g_wifi_mgnt_ctx.connect_timeout_work,
                                         wifi_connect_timeout_action, NULL,
                                         QM_WIFI_CONNECT_TIMEOUT_MS);
            qm_event_post(QM_EVENT_WIFI, (uint16_t)event, NULL, 0, QM_NO_WAIT);
        break;

        case QM_WIFI_EVENT_STA_STOP:
            (void)qm_cancel_delayed_action(&g_wifi_mgnt_ctx.connect_timeout_work);
            g_wifi_mgnt_ctx.wifi_state = QM_WIFI_STATE_DISCONNECTED;
            qm_event_post(QM_EVENT_WIFI, (uint16_t)event, NULL, 0, QM_NO_WAIT);
        break;

        case QM_WIFI_EVENT_STA_CONNECTED:
            (void)qm_cancel_delayed_action(&g_wifi_mgnt_ctx.connect_timeout_work);
            g_wifi_mgnt_ctx.wifi_state = QM_WIFI_STATE_CONNECTED;
            g_wifi_mgnt_ctx.reconnect_count = 0;
            g_wifi_mgnt_ctx.last_reason = QM_WIFI_REASON_UNKNOWN;
            qm_event_post(QM_EVENT_WIFI, (uint16_t)event, NULL, 0, QM_NO_WAIT);

        break;

        case QM_WIFI_EVENT_STA_GOT_IP:
            g_wifi_mgnt_ctx.wifi_state = QM_WIFI_STATE_CONNECTED;
            g_wifi_mgnt_ctx.reconnect_count = 0;
            g_wifi_mgnt_ctx.last_reason = QM_WIFI_REASON_UNKNOWN;

            qm_event_post(QM_EVENT_WIFI, (uint16_t)event, event_info, sizeof(qm_wifi_event_info_t), QM_NO_WAIT);
        break;

        case QM_WIFI_EVENT_STA_LOST_IP:
            (void)qm_cancel_delayed_action(&g_wifi_mgnt_ctx.connect_timeout_work);
            qm_event_post(QM_EVENT_WIFI, (uint16_t)event, NULL, 0, QM_NO_WAIT);
            g_wifi_mgnt_ctx.wifi_state = QM_WIFI_STATE_DISCONNECTED;
            g_wifi_mgnt_ctx.last_reason = QM_WIFI_REASON_DHCP_TIMEOUT;
            if (g_wifi_mgnt_ctx.auto_reconnect) {
                wifi_schedule_reconnect();
            }
        break;

        case QM_WIFI_EVENT_STA_DISCONNECTED:
            (void)qm_cancel_delayed_action(&g_wifi_mgnt_ctx.connect_timeout_work);
            g_wifi_mgnt_ctx.wifi_state = QM_WIFI_STATE_DISCONNECTED;
            g_wifi_mgnt_ctx.last_reason = event_info ?
                (qm_wifi_reason_t)event_info->sta_disconnected.reason :
                QM_WIFI_REASON_UNKNOWN;
            qm_event_post(QM_EVENT_WIFI, (uint16_t)event, event_info, sizeof(qm_wifi_event_info_t), QM_NO_WAIT);
            if (g_wifi_mgnt_ctx.auto_reconnect) {
                wifi_schedule_reconnect();
            }

        break;

        case QM_WIFI_EVENT_AP_START:
        case QM_WIFI_EVENT_AP_STOP:
            qm_event_post(QM_EVENT_WIFI, (uint16_t)event, NULL, 0, QM_NO_WAIT);
        break;

        case QM_WIFI_EVENT_AP_STACONNECTED:
        case QM_WIFI_EVENT_AP_STAIPASSIGNED:
        case QM_WIFI_EVENT_AP_STADISCONNECTED:

            qm_event_post(QM_EVENT_WIFI, (uint16_t)event, event_info, sizeof(qm_wifi_event_info_t), QM_NO_WAIT);
        break;  

        case QM_WIFI_EVENT_SCAN_DONE:
        
            qm_event_post(QM_EVENT_WIFI, (uint16_t)event, event_info, sizeof(qm_wifi_event_info_t), QM_NO_WAIT);
        break;        

        default:
            break;
    }
}

int qm_wifi_init(void)
{
    qm_err_t ret = QM_EOK;

    if(g_wifi_initialized){
        return QM_EOK;
    }

    g_wifi_mgnt_ctx.wifi_dev = qm_wifi_dev_get();
    if(g_wifi_mgnt_ctx.wifi_dev == NULL){
        return -QM_EINVAL;
    }

    ret = qm_wifi_dev_register_event_handler(g_wifi_mgnt_ctx.wifi_dev, wifi_event_hander, NULL);
    if(ret != QM_EOK){
        return ret;
    }

    ret = qm_wifi_dev_init(g_wifi_mgnt_ctx.wifi_dev);
    if(ret != QM_EOK){
        return ret;
    }

    g_wifi_initialized = 1;
    return ret;
}

int qm_wifi_deinit(void)
{
    int ret;

    if (!wifi_manager_ready()) {
        return -QM_EINVAL;
    }
    g_wifi_mgnt_ctx.auto_reconnect = 0;
    (void)qm_cancel_delayed_action(&g_wifi_mgnt_ctx.reconnect_work);
    (void)qm_cancel_delayed_action(&g_wifi_mgnt_ctx.connect_timeout_work);
    ret = qm_wifi_dev_deinit(g_wifi_mgnt_ctx.wifi_dev);
    if (ret == QM_EOK) {
        g_wifi_mgnt_ctx.wifi_dev->event_handler = NULL;
        g_wifi_mgnt_ctx.wifi_dev->arg = NULL;
        g_wifi_mgnt_ctx.wifi_state = QM_WIFI_STATE_NONE;
        g_wifi_initialized = 0;
    }
    return ret;
}

int qm_wifi_set_mode(qm_wifi_mode_t mode)
{
    if (!wifi_manager_ready()) {
        return -QM_EINVAL;
    }
    return qm_wifi_dev_set_mode(g_wifi_mgnt_ctx.wifi_dev, mode);
}

int qm_wifi_get_mode(qm_wifi_mode_t *mode)
{
    if (!wifi_manager_ready()) {
        return -QM_EINVAL;
    }
    return qm_wifi_dev_get_mode(g_wifi_mgnt_ctx.wifi_dev, mode);
}

int qm_wifi_set_config(qm_wifi_interface_t ifx, qm_wifi_config_t *config)
{
    if (!wifi_manager_ready()) {
        return -QM_EINVAL;
    }
    return qm_wifi_dev_set_config(g_wifi_mgnt_ctx.wifi_dev, ifx, config);
}

int qm_wifi_get_config(qm_wifi_interface_t ifx, qm_wifi_config_t *config)
{
    if (!wifi_manager_ready()) {
        return -QM_EINVAL;
    }
    return qm_wifi_dev_get_config(g_wifi_mgnt_ctx.wifi_dev, ifx, config);
}

int qm_wifi_clear_config(void)
{
    if (!wifi_manager_ready()) {
        return -QM_EINVAL;
    }

    memset(&g_wifi_mgnt_ctx.wifi_dev->wifi_cfg, 0, sizeof(g_wifi_mgnt_ctx.wifi_dev->wifi_cfg));

    return qm_kv_del("wifi_cfg");
}

int qm_wifi_start(void)
{
    int ret;

    if (!wifi_manager_ready()) {
        return -QM_EINVAL;
    }
    g_wifi_mgnt_ctx.reconnect_count = 0;
    g_wifi_mgnt_ctx.last_reason = QM_WIFI_REASON_UNKNOWN;
    g_wifi_mgnt_ctx.auto_reconnect = 1;
    ret = qm_wifi_dev_start(g_wifi_mgnt_ctx.wifi_dev);
    if (ret != QM_EOK) {
        g_wifi_mgnt_ctx.auto_reconnect = 0;
    }
    return ret;
}

int qm_wifi_stop(void)
{
    if (!wifi_manager_ready()) {
        return -QM_EINVAL;
    }
    g_wifi_mgnt_ctx.auto_reconnect = 0;
    g_wifi_mgnt_ctx.reconnect_count = 0;
    g_wifi_mgnt_ctx.last_reason = QM_WIFI_REASON_UNKNOWN;
    (void)qm_cancel_delayed_action(&g_wifi_mgnt_ctx.connect_timeout_work);
    (void)qm_cancel_delayed_action(&g_wifi_mgnt_ctx.reconnect_work);
    return qm_wifi_dev_stop(g_wifi_mgnt_ctx.wifi_dev);
}

int qm_wifi_connect(void)
{
    if (!wifi_manager_ready()) {
        return -QM_EINVAL;
    }
    return qm_wifi_dev_connect(g_wifi_mgnt_ctx.wifi_dev);
}

int qm_wifi_disconnect(void)
{
    if (!wifi_manager_ready()) {
        return -QM_EINVAL;
    }
    g_wifi_mgnt_ctx.auto_reconnect = 0;
    (void)qm_cancel_delayed_action(&g_wifi_mgnt_ctx.reconnect_work);
    return qm_wifi_dev_disconnect(g_wifi_mgnt_ctx.wifi_dev);
}

int qm_wifi_deauth(const uint8_t mac[6])
{
    if (!wifi_manager_ready()) {
        return -QM_EINVAL;
    }
    return qm_wifi_dev_deauth(g_wifi_mgnt_ctx.wifi_dev, mac);
}

int qm_wifi_scan_start(qm_scan_config_t *config)
{
    if (!wifi_manager_ready()) {
        return -QM_EINVAL;
    }
    return qm_wifi_dev_scan_start(g_wifi_mgnt_ctx.wifi_dev, config);
}

int qm_wifi_scan_get_ap_records(uint16_t *number, qm_wifi_ap_record_t *ap_records)
{
    if (!wifi_manager_ready()) {
        return -QM_EINVAL;
    }
    return qm_wifi_dev_scan_get_ap_records(g_wifi_mgnt_ctx.wifi_dev,
                                           number,
                                           ap_records);
}

int qm_wifi_scan_stop(void)
{
    if (!wifi_manager_ready()) {
        return -QM_EINVAL;
    }
    return qm_wifi_dev_scan_stop(g_wifi_mgnt_ctx.wifi_dev);
}

int qm_wifi_set_mac(qm_wifi_interface_t ifx, const uint8_t mac[6])
{
    if (!wifi_manager_ready()) {
        return -QM_EINVAL;
    }
    return qm_wifi_dev_set_mac(g_wifi_mgnt_ctx.wifi_dev, ifx, mac);
}

int qm_wifi_get_mac(qm_wifi_interface_t ifx, uint8_t mac[6])
{
    if (!wifi_manager_ready()) {
        return -QM_EINVAL;
    }
    return qm_wifi_dev_get_mac(g_wifi_mgnt_ctx.wifi_dev, ifx, mac);
}

int qm_wifi_sta_get_rssi(int8_t *rssi)
{
    if (!wifi_manager_ready()) {
        return -QM_EINVAL;
    }
    return qm_wifi_dev_sta_get_rssi(g_wifi_mgnt_ctx.wifi_dev, rssi);
}

int qm_wifi_sta_get_ap_info(qm_wifi_ap_record_t *ap_info)
{
    if (!wifi_manager_ready()) {
        return -QM_EINVAL;
    }
    return qm_wifi_dev_sta_get_ap_info(g_wifi_mgnt_ctx.wifi_dev, ap_info);
}
