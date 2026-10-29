#include "qm.h"

#include "qm_wifi_dev.h"

#include <hal_wifi.h>

#include <aos/kernel.h>
#include <aos/yloop.h>
#include <event_device.h>

#include <lwip/tcpip.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <lwip/tcp.h>
#include <lwip/err.h>

#include "wifi_mgmr_ext.h"
#include "../wifi_mgmr.h"
#include "../wifi_mgmr_api.h"

#include "bl_sys.h"

#define LOG_TAG  "HAL WIFI"

#define QM_WIFI_HAL_SCAN_TIMEOUT_MS         (10 * 1000)
#define QM_WIFI_HAL_CONN_WIFI_TIMEOUT_MS    (30 * 1000)

typedef struct 
{
    uint8_t retry_cnt;
    uint8_t sta_connected;
    uint8_t prev_connectd;        

    qm_sem_t scan_sem;

    qm_sem_t conn_sem;
    qm_task_t conn_task;

    wifi_conf_t conf;
    qm_sta_config_t sta;
    wifi_interface_t wifi_interface;

    qm_wifi_ap_record_t ap_info;
}qm_wifi_hal_dev_t;

extern void ipc_emb_notify(void);

#define WIFI_MGMR wifiMgmr

static qm_wifi_hal_dev_t g_wifi_dev = {
    .conf = {
        .country_code = "CN",
    }
};

static void event_cb_wifi_event(input_event_t *event, void *private_data)
{
#if 0
    static char *ssid;
    static char *password;
#endif
    
    struct sm_connect_tlv_desc* ele = NULL;
    
    qm_wifi_event_info_t event_info = {0};
    qm_wifi_dev_t *wifi = (qm_wifi_dev_t*)private_data;

    qm_ip_info_t *qm_ip_info = &event_info.got_ip.ip_info;
    qm_wifi_event_sta_disconnected_t *sta_disconnected = &event_info.sta_disconnected;

    switch (event->code) {


        case CODE_WIFI_ON_MGMR_DONE:
        {
            wifi->event_handler(QM_WIFI_EVENT_READY, NULL, wifi->arg);
        }
        break;

        case CODE_WIFI_ON_MGMR_DENOISE:
        {
            QM_LOGD("1","[APP] [EVT] Microwave Denoise is ON");
        }
        break;
#if 0
        case CODE_WIFI_ON_SCAN_DONE:
        {
            printf("[APP] [EVT] SCAN Done %lld\r\n", aos_now_ms());
            wifi_mgmr_cli_scanlist();
        }
        break;

        case CODE_WIFI_ON_SCAN_DONE_ONJOIN:
        {
            printf("[APP] [EVT] SCAN On Join %lld\r\n", aos_now_ms());
        }
        break;
#endif
        case CODE_WIFI_ON_DISCONNECT:
        {
            QM_LOGD("1", "[APP] [EVT] disconnect, Reason: %s",
                wifi_mgmr_status_code_str(event->value)
            );

            if(g_wifi_dev.sta_connected == 1)
            {
                g_wifi_dev.sta_connected = 0;
                sta_disconnected->reason = QM_WIFI_REASON_CONNECTION_FAIL;
                wifi->event_handler(QM_WIFI_EVENT_STA_DISCONNECTED, &event_info, wifi->arg);
            }
            else
            {
                qm_sem_signal(&g_wifi_dev.conn_sem);
            }
        }
        break;

        case CODE_WIFI_ON_CONNECTING:
        {
            QM_LOGD("1", "[APP] [EVT] Connecting");
        }
        break;

        case CODE_WIFI_CMD_RECONNECT:
        {
            QM_LOGD("1", "retry connect : %d", g_wifi_dev.retry_cnt);
            if (!g_wifi_dev.prev_connectd)
            {
                if (g_wifi_dev.retry_cnt++ > 3)
                {
                    // wifi_mgmr_sta_autoconnect_disable();
                    wifi_mgmr_api_idle();

                    qm_sem_signal(&g_wifi_dev.conn_sem);
                }
            }
        }
        break;

        case CODE_WIFI_ON_CONNECTED:
        {
            wifi->event_handler(QM_WIFI_EVENT_STA_CONNECTED, NULL, wifi->arg);
        }
        break;

        case CODE_WIFI_ON_PRE_GOT_IP:
        {
            
        }
        break;
        
        case CODE_WIFI_ON_GOT_IP:
        {
            QM_LOGD("1", "CODE_WIFI_ON_GOT_IP");

            g_wifi_dev.prev_connectd = 1;
            g_wifi_dev.sta_connected = 1;
            qm_sem_signal(&g_wifi_dev.conn_sem);
            
            // wifi_mgmr_sta_autoconnect_disable();
            wifi_mgmr_sta_ip_get(&qm_ip_info->ip.addr,&qm_ip_info->gw.addr, &qm_ip_info->netmask.addr);
    
            wifi->event_handler(QM_WIFI_EVENT_STA_GOT_IP, &event_info, wifi->arg);
        }
        break;

        case CODE_WIFI_ON_EMERGENCY_MAC:
        {
            QM_LOGD("1", "[APP] [EVT] EMERGENCY MAC");
            bl_sys_reset_por();//one way of handling emergency is reboot. Maybe we should also consider solutions
        }
        break;

        default:
        {
            /*nothing*/
        }
        break;
    }
}

static qm_err_t hal_wifi_init(qm_wifi_dev_t *wifi)
{
    /*wifi fw stack and thread stuff*/
    static uint8_t stack_wifi_init  = 0;

    if (1 == stack_wifi_init) {
        QM_LOGW(LOG_TAG, "Wi-Fi Stack Started already!!!");
        return QM_EOK;
    }
    stack_wifi_init = 1;
    
    aos_register_event_filter(EV_WIFI, event_cb_wifi_event, (void *)wifi);

    tcpip_init(NULL, NULL);

    hal_wifi_start_firmware_task();
    
    wifi_mgmr_start_background(&g_wifi_dev.conf);
    
    ipc_emb_notify();
    wifi_mgmr_scan_filter_hidden_ssid(0);

    return QM_EOK;
}

static qm_err_t hal_wifi_deinit(qm_wifi_dev_t *wifi)
{
    return QM_EOK;
}

static qm_err_t hal_wifi_start(qm_wifi_dev_t *wifi)
{
    char pmk[66] = {0};

    if(wifi->wifi_cfg.mode == QM_WIFI_MODE_STA){

        g_wifi_dev.wifi_interface = wifi_mgmr_sta_enable();
        
        memcpy(&g_wifi_dev.sta, &wifi->wifi_cfg.config.sta, sizeof(qm_sta_config_t));

        wifi->event_handler(QM_WIFI_EVENT_STA_START, NULL, wifi->arg);
    }

    return QM_EOK;
}

static qm_err_t hal_wifi_stop(qm_wifi_dev_t *wifi)
{
    if(wifi->wifi_cfg.mode == QM_WIFI_MODE_STA){
        wifi_mgmr_sta_disable(g_wifi_dev.wifi_interface);
    }
    return QM_EOK;
}


static void scan_complete_indicate(void *data, void *param)
{

    wifi_mgmr_cli_scanlist();

    qm_sem_signal(&g_wifi_dev.scan_sem); 

    return;
}

static qm_wifi_auth_mode_t wifi_mgmr_auth_to_qm(uint8_t auth)
{
    qm_wifi_auth_mode_t mode = QM_WIFI_AUTH_MAX;
    switch (auth) {
        case 6:
        {
            mode = QM_WIFI_AUTH_WPA3_PSK;
        }
        break;
        case 7:
        {
            mode = QM_WIFI_AUTH_WPA2_WPA3_PSK;
        }
        break;
        case 0:
        {
            mode = QM_WIFI_AUTH_OPEN;
        }
        break;
        case 1:
        {
            mode = QM_WIFI_AUTH_WEP;
        }
        break;
        case 2:
        {
            mode = QM_WIFI_AUTH_WPA_PSK;
        }
        break;
        case 3:
        {
            mode = QM_WIFI_AUTH_WPA2_PSK;

        }
        break;
        case 4:
        {
            mode = QM_WIFI_AUTH_WPA_WPA2_PSK;
        }
        break;
        case 5:
        {
            mode = QM_WIFI_AUTH_WPA2_ENTERPRISE;
        }
        break;

        default:
        {

        }
        break;
    }

    return mode;
}

//todo:后续需要进行优化调整
static void hal_wifi_conn_task(void *arg)
{
    int i;
    int state;
    int reason_code;
    int ret = 0;
    int index = -1;
    int rssi = -127;
    uint32_t cnt = 0;

    wifi_mgmr_scan_item_t *scan = NULL;
    qm_sem_new(&g_wifi_dev.conn_sem, 0);
    

_retry:

    wifi_mgmr_api_idle();

    g_wifi_dev.retry_cnt = 0;
    g_wifi_dev.prev_connectd = 0;

    qm_sem_new(&g_wifi_dev.scan_sem, 0);
    wifi_mgmr_scan_adv(NULL, scan_complete_indicate, 
                        NULL, 0, 
                        NULL, (char *)g_wifi_dev.sta.ssid, 
                        SCAN_ACTIVE, 0);

    qm_sem_wait(&g_wifi_dev.scan_sem, QM_WIFI_HAL_SCAN_TIMEOUT_MS); 

    qm_sem_free(&g_wifi_dev.scan_sem);

    for (i = 0;
        i < sizeof(WIFI_MGMR.scan_items) / sizeof(WIFI_MGMR.scan_items[0]);
        i++)
    {
        scan = &WIFI_MGMR.scan_items[i];

        if (wifi_mgmr_scan_item_is_timeout(&WIFI_MGMR,
                                            &(WIFI_MGMR.scan_items[i])))
        {
            scan->is_used = 0;
        }
        else if (scan->is_used)
        {
            if (strncmp(scan->ssid,
                        (char *)g_wifi_dev.sta.ssid,
                        sizeof(scan->ssid)) == 0)
            {
                scan->is_used = 1;
            if (scan->rssi > rssi) {
                /*found ap with better rssi*/
                rssi = scan->rssi;
                index = i;
            }
            }
            else
            {
                scan->is_used = 0;
            }
        }
    }

    wifi_mgmr_api_idle();
    // wifi_mgmr_sta_autoconnect_enable();

    memset(&g_wifi_dev.ap_info, 0, sizeof(qm_wifi_ap_record_t));

    g_wifi_dev.ap_info.rssi = rssi;
    g_wifi_dev.ap_info.ssid_len = g_wifi_dev.sta.ssid_len;
    g_wifi_dev.ap_info.channel = WIFI_MGMR.scan_items[index].channel;
    g_wifi_dev.ap_info.authmode = wifi_mgmr_auth_to_qm(WIFI_MGMR.scan_items[index].auth);
    memcpy(g_wifi_dev.ap_info.bssid, WIFI_MGMR.scan_items[index].bssid, 6);
    memcpy(g_wifi_dev.ap_info.ssid, g_wifi_dev.sta.ssid, strlen((char *)g_wifi_dev.sta.ssid));

    if(index >= 0){
        QM_LOGD("1", "wifi index:%d",index );
        //todo:取消链接wifi的指定通道
        wifi_mgmr_sta_connect(g_wifi_dev.wifi_interface, 
            (char *)g_wifi_dev.sta.ssid, 
            (char *)g_wifi_dev.sta.password, 
            NULL, WIFI_MGMR.scan_items[index].bssid, 0, WIFI_MGMR.scan_items[index].channel);
    }else{
        wifi_mgmr_sta_connect(g_wifi_dev.wifi_interface, 
            (char *)g_wifi_dev.sta.ssid, 
            (char *)g_wifi_dev.sta.password, 
            NULL, NULL, 0, 0);
    }

    qm_sem_wait(&g_wifi_dev.conn_sem, QM_WIFI_HAL_CONN_WIFI_TIMEOUT_MS);
    
    index = -1;
    wifi_mgmr_state_get_internal(&state);
    wifi_mgmr_status_code_get_internal(&reason_code);
    QM_LOGD("1", "wifi state:%d, reason_code: %d(%s)\n",
             state,
             reason_code,
             wifi_mgmr_status_code_str(reason_code));

    if (state == WIFI_STATE_CONNECTED_IP_GOT ||
        state == WIFI_STATE_WITH_AP_CONNECTED_IP_GOT ||
        state == WIFI_STATE_CONNECTED_IP_GETTING)
    {
        goto __clean;
    }
    else
    {
        goto _retry;
    }
    
__clean:
    qm_sem_free(&g_wifi_dev.conn_sem);
    qm_task_exit(&g_wifi_dev.conn_task);
}

static qm_err_t hal_wifi_connect(qm_wifi_dev_t *wifi)
{
    if(wifi->wifi_cfg.mode == QM_WIFI_MODE_STA){

        if(!g_wifi_dev.conn_task.hdl){
            qm_task_new(&g_wifi_dev.conn_task, "qm_wifi", hal_wifi_conn_task, NULL, 2048, CONFIG_QM_DRIVER_TASK_PRIO);
        }

    }
    return QM_EOK;
}

static qm_err_t hal_wifi_disconnet(qm_wifi_dev_t *wifi)
{
    if(wifi->wifi_cfg.mode == QM_WIFI_MODE_STA){

        if (g_wifi_dev.sta_connected == 0)
        {
            return QM_EOK;
        }

        g_wifi_dev.sta_connected = 0;
        wifi_mgmr_sta_disconnect();
        qm_msleep(WIFI_MGMR_STA_DISCONNECT_DELAY);
        wifi_mgmr_api_idle();

    }
    return QM_EOK;
}

#if 0
static qm_err_t hal_wifi_scan_start(qm_wifi_dev_t *wifi, qm_scan_config_t *config)
{
    if(g_wifi_dev.mode == QM_WIFI_MODE_STA){
        
    }
    return -QM_EINVAL;
}

static qm_err_t hal_wifi_scan_stop(qm_wifi_dev_t *wifi)
{
    if(g_wifi_dev.mode == QM_WIFI_MODE_STA){
        
    }
    return -QM_EINVAL;
}
#endif


static qm_err_t hal_wifi_set_mac(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, const uint8_t mac[6])
{
    if(ifx == QM_WIFI_IF_STA){
        wifi_mgmr_sta_mac_set((uint8_t *)mac);
    }

    return QM_EOK;
}

static qm_err_t hal_wifi_get_mac(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, uint8_t mac[6])
{
    if(ifx == QM_WIFI_IF_STA){
        wifi_mgmr_sta_mac_get(mac);
    }

    return QM_EOK;
}


static qm_err_t hal_wifi_get_sta_ap_info(qm_wifi_dev_t *wifi, qm_wifi_ap_record_t *ap_info)
{
    int rssi = -127;
    if(ap_info == NULL){
        return -QM_EINVAL;
    }

    wifi_mgmr_rssi_get(&rssi);
    g_wifi_dev.ap_info.rssi = rssi;
    memcpy(ap_info, &g_wifi_dev.ap_info, sizeof(qm_wifi_ap_record_t));

    return QM_EOK;
}

static qm_err_t hal_wifi_get_sta_rssi(qm_wifi_dev_t *wifi, int8_t *rssi)
{
    wifi_mgmr_rssi_get(rssi);
    return QM_EOK;
}


static qm_wifi_dev_ops_t wifi_ops =
{
    .wifi_init              = hal_wifi_init,
    .wifi_deinit            = hal_wifi_deinit,
    .wifi_start             = hal_wifi_start,
    .wifi_stop              = hal_wifi_stop,
    .wifi_connect           = hal_wifi_connect,
    .wifi_disconnect        = hal_wifi_disconnet,
    .wifi_set_mac           = hal_wifi_set_mac,
    .wifi_get_mac           = hal_wifi_get_mac,
    .wifi_get_sta_ap_info   = hal_wifi_get_sta_ap_info,
    .wifi_get_sta_rssi      = hal_wifi_get_sta_rssi,
#if 0
    .wifi_scan_start        = hal_wifi_scan_start,
    .wifi_scan_stop         = hal_wifi_scan_stop,
    .wifi_deauth            = hal_wifi_deauth,
    .wifi_set_ps            = hal_wifi_set_ps,
#endif
};


int qm_hal_wifi_init(void)
{
    return qm_wifi_dev_register(&wifi_ops, NULL);
}