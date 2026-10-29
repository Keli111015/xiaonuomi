#include "qm_wifi.h"
#include "qm_kernel.h"

#include "qm_wifi_dev.h"
#include "qm_errno.h"
#include "bl_wifi.h"
#include "bl_mtd.h"

#include "hal_wifi.h"
#include "hosal_flash.h"


#include <lwip/tcpip.h>
#include <aos/kernel.h>
#include <aos/yloop.h>
#include "easyflash.h"
#include <lwip/igmp.h>

// #define STA_INFO_PARTITION "KEY"
// #define MAX_LEN_SSID 33 /* 包含字符串结束符 */
// #define MAX_LEN_PASSWORD 65 /* 包含字符串结束符 */
// #define PMK_LEN 65

// static wifi_conf_t conf = {
//     .country_code = "CN",
// };

static wifi_mgmr_ap_item_t item = {0};
static wifi_mgmr_ap_item_t *ap_ary = NULL;
static uint32_t num = 0;
static scan_item_cb_t callback = NULL;

// static void WifiEventCB(input_event_t *event, void *privateData)
// {
//     switch (event->code) {
//         case CODE_WIFI_ON_GOT_IP: {
//             wifi_mgmr_sta_connect_ind_stat_info_t info;
//             memset(&info, 0, sizeof(info));
//             wifi_mgmr_sta_connect_ind_stat_get(&info);
//             hosal_flash_dev_t *pflash = NULL;
//             bl_mtd_open(STA_INFO_PARTITION, &pflash, HOSAL_FLASH_FLAG_ADDR_0);
//             if (pflash == NULL) {
//                 return;
//             }
//             unsigned int offset = MAX_LEN_SSID + PMK_LEN;
//             bl_mtd_write(pflash, offset, sizeof(info.chan_id), &info.chan_id);
//             bl_mtd_close(pflash);
//             aos_unregister_event_filter(EV_WIFI, WifiEventCB, NULL);
//             break;
//         }
//         default:
//             break;
//     }
//     return;
// }

// static bool WifiReConn(void)
// {
//     wifi_interface_t wifiInterface;
//     bool openBssFlag = 1;
//     unsigned char chanId;
//     char ssid[MAX_LEN_SSID] = {0};
//     char pmk[PMK_LEN] = {0};

//     hosal_flash_dev_t *pflash;
//     bl_mtd_open(STA_INFO_PARTITION, &pflash, HOSAL_FLASH_FLAG_ADDR_0);
//     if (pflash == NULL) {
//         return false;
//     }

//     bl_mtd_read(pflash, 0, MAX_LEN_SSID, (unsigned char *)ssid);
//     bl_mtd_read(pflash, MAX_LEN_SSID, PMK_LEN, (unsigned char *)pmk);
//     if (pmk[0]) {
//         openBssFlag = 0;
//     }
//     bl_mtd_read(pflash, MAX_LEN_SSID + PMK_LEN, sizeof(chanId), &chanId);
//     if (chanId == 255) {
//         return true;
//     }

//     if (aos_register_event_filter(EV_WIFI, WifiEventCB, NULL) != 0) {
//         return false;
//     }
//     wifiInterface = wifi_mgmr_sta_enable();
//     if (wifiInterface == NULL) {
//         return false;
//     }
//     wifi_mgmr_sta_connect_mid(wifiInterface, ssid, NULL, openBssFlag ? NULL : pmk, NULL, 0, chanId, 1, 0);
//     return true;
// }

// static qm_err_t hal_wifi_init(qm_wifi_dev_t *wifi)
// {
//     hal_wifi_start_firmware_task();
//     wifi_mgmr_start_background(&conf);
//     tcpip_init(NULL, NULL);
//     if (!WifiReConn()) {
//         return (-1);
//     }
//     return QM_EOK;
// }








#define STA_INFO_PARTITION "KEY"
#define MAX_LEN_SSID 33 /* 包含字符串结束符 */
#define MAX_LEN_PASSWORD 65 /* 包含字符串结束符 */
#define PMK_LEN 65

extern void ble_stack_start(void);

static wifi_conf_t conf = {
    .country_code = "CN",
};

void IgmpJoinGroup(void)
 {
    return;
 }

static void WifiEventCB(input_event_t *event, void *privateData)
{
    switch (event->code) {
        case CODE_WIFI_ON_GOT_IP: {
            qm_printf("conn suc\r\n");
            wifi_mgmr_sta_connect_ind_stat_info_t info;
            memset(&info, 0, sizeof(info));
            wifi_mgmr_sta_connect_ind_stat_get(&info);
            hosal_flash_dev_t *pflash = NULL;
            bl_mtd_open(STA_INFO_PARTITION, &pflash, HOSAL_FLASH_FLAG_ADDR_0);
            if (pflash == NULL) {
                qm_printf("flash open\r\n");
                return;
            }
            unsigned int offset = MAX_LEN_SSID + PMK_LEN;
            bl_mtd_write(pflash, offset, sizeof(info.chan_id), &info.chan_id);
            bl_mtd_close(pflash);
            struct ip4_addr groupAddr;
            IP4_ADDR(&groupAddr, 238, 238, 238, 238);
            igmp_joingroup(IP_ADDR_ANY, (struct ip_addr*)(&groupAddr));
            // aos_unregister_event_filter(EV_WIFI, WifiEventCB, NULL);
            break;
        }
        default:
            break;
    }
    return;
}

static bool WifiReConn(void)
{
    wifi_interface_t wifiInterface;
    bool openBssFlag = 1;
    unsigned char chanId;
    char ssid[MAX_LEN_SSID] = {0};
    char pmk[PMK_LEN] = {0};

    hosal_flash_dev_t *pflash;
    bl_mtd_open(STA_INFO_PARTITION, &pflash, HOSAL_FLASH_FLAG_ADDR_0);
    if (pflash == NULL) {
        qm_printf("open fs\r\n");
        return false;
    }

    bl_mtd_read(pflash, 0, MAX_LEN_SSID, (unsigned char *)ssid);
    bl_mtd_read(pflash, MAX_LEN_SSID, PMK_LEN, (unsigned char *)pmk);
    if (pmk[0]) {
        openBssFlag = 0;
    }
    bl_mtd_read(pflash, MAX_LEN_SSID + PMK_LEN, sizeof(chanId), &chanId);
    if (chanId == 255) {
        qm_printf("no sta info\r\n");
        return true;
    }

    if (aos_register_event_filter(EV_WIFI, WifiEventCB, NULL) != 0) {
        qm_printf("aos reg\r\n");
        return false;
    }
    wifiInterface = wifi_mgmr_sta_enable();
    if (wifiInterface == NULL) {
        qm_printf("wifi mgmr\r\n");
        return false;
    }
    wifi_mgmr_sta_connect_mid(wifiInterface, ssid, NULL, openBssFlag ? NULL : pmk, NULL, 0, chanId, 1, 0);
    return true;
}

static qm_err_t hal_wifi_init(qm_wifi_dev_t *wifi)
{
    hal_wifi_start_firmware_task();
    wifi_mgmr_start_background(&conf);
    tcpip_init(NULL, NULL);


//      if (aos_register_event_filter(EV_WIFI, WifiEventCB, NULL) != 0) {
//         qm_printf("aos reg\r\n");
//         return false;
//    } 

    if (!WifiReConn()) {
        return (-1);
    }
    return QM_EOK;
}
















static qm_err_t hal_wifi_deinit(qm_wifi_dev_t *wifi)
{
    return QM_EOK;
}

static qm_err_t hal_wifi_start(qm_wifi_dev_t *wifi)
{
    return QM_EOK;
}

static qm_err_t hal_wifi_stop(qm_wifi_dev_t *wifi)
{
    return QM_EOK;
}

static qm_err_t hal_wifi_connect(qm_wifi_dev_t *wifi)
{
    return QM_EOK;
}

static qm_err_t hal_wifi_disconnet(qm_wifi_dev_t *wifi)
{
    return QM_EOK;
}


void qm_wifi_scan_item_register_callback(scan_item_cb_t cb)
{
    callback = cb;
}


static uint8_t g_scan_finish = 1;
void scan_complete_cb(void *data, void *param)
{
    g_scan_finish = 1;
}

wifi_interface_t interface = NULL;
static qm_err_t hal_wifi_scan_start(qm_wifi_dev_t *wifi, qm_scan_config_t *config)
{
    qm_err_t err = -QM_ERROR;


    interface = wifi_mgmr_sta_enable();
    if(interface == NULL){
        return -QM_ERR_WIFI_IF;
    }
    wifi_scan_item_cb_register(callback);
    wifi_scan_complete_cb_register(scan_complete_cb);

    if(g_scan_finish)
    {
        g_scan_finish = 0;
        if(wifi_mgmr_all_ap_scan(&ap_ary, &num) == 0)
        {
            qm_free(ap_ary);
        }
    }

    return QM_EOK;
}

static qm_err_t hal_wifi_scan_stop(qm_wifi_dev_t *wifi)
{
    if(interface != NULL)
    {
        if(wifi_mgmr_sta_disable(interface) != 0)
        {
            qm_printf("wifi_mgmr_sta_disable failed\r\n");
            return -QM_ERR_WIFI_IF;
        }
        qm_printf("wifi_mgmr_sta_disable success\r\n");
    }
    return QM_EOK;
}

static qm_err_t hal_wifi_deauth(qm_wifi_dev_t *wifi, const uint8_t mac[6])
{   
    return QM_EOK;
}

static qm_err_t hal_wifi_set_ps(qm_wifi_dev_t *wifi, qm_wifi_ps_type_t type)
{
    return QM_EOK;
}

static qm_err_t hal_wifi_set_mac(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, const uint8_t mac[6])
{
    qm_err_t err = -QM_ERROR; 
    if(ifx == QM_WIFI_IF_STA){
        err = wifi_mgmr_sta_mac_set(mac);
    }else if(ifx == QM_WIFI_IF_AP){
        err = wifi_mgmr_ap_mac_set(mac);
    }else{
        return -QM_ERR_WIFI_IF;
    }
    return QM_EOK;
}

static qm_err_t hal_wifi_get_mac(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, uint8_t mac[6])
{
    qm_err_t err = -QM_ERROR; 
     if(ifx == QM_WIFI_IF_STA){
        err = wifi_mgmr_sta_mac_get(mac);
    }else if(ifx == QM_WIFI_IF_AP){
        err = wifi_mgmr_ap_mac_get(mac);
    }else{
        return -QM_ERR_WIFI_IF;
    }
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
    .wifi_scan_start        = hal_wifi_scan_start,
    .wifi_scan_stop         = hal_wifi_scan_stop,
    .wifi_deauth            = hal_wifi_deauth,
    .wifi_set_ps            = hal_wifi_set_ps,
    .wifi_set_mac           = hal_wifi_set_mac,
    .wifi_get_mac           = hal_wifi_get_mac,
};


int qm_hal_wifi_init(void)
{
    return qm_wifi_dev_register(&wifi_ops, NULL);
    
}