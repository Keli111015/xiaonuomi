#include "prodtst_client.h"
#include "prodtst_client_common.h"

#include "qm_at.h"
#include "qm_log.h"
#include "qm_types.h"
#include "qm_errno.h"
#include "qm_kernel.h"
#include "qm_config.h"
#include "xjiang_version.h"

#include <k_api.h>
#include "app_entry.h"
#include "iot_export.h"

#include "config.h"
#include "bluetooth.h"
#include "breeze_export.h"

#define QM_LOG_TAG "prodtst_hal"

typedef struct 
{
    uint8_t is_use;
    qm_mutex_t adv_lock;
    qm_timer_t adv_timer;
}prodtst_ble_adv_info_t;

typedef struct 
{
    char test_siid[33] ;
    qm_sem_t test_sem ;
    int test_wifi_rssi ;
    qm_at_result_t wifi_rf_test_result;
}prodtst_rf_wifi_info_t;

static  uint8_t ble_init = 0;
static  prodtst_ble_adv_info_t prodtst_ble_adv_info = {0};
static  prodtst_rf_wifi_info_t prodtst_rf_wifi_info = {0};


int hal_get_fwname(char *data, uint16_t len)
{
    if(data == NULL){
        return -QM_EINVAL;
    }
    char next_ver[4] = {0};
    int man_ver = 0, sub_ver = 0, fix_ver = 0;
    sscanf(XJIANG_FIRMWARE_VERSION, "%d.%d.%d.%s", &man_ver, &sub_ver, &fix_ver, next_ver);
    qm_snprintf(data, len, XJIANG_FIRMWARE_NAME_FORMAT, man_ver, sub_ver, fix_ver, next_ver);
    return QM_EOK;
}

int hal_get_fwversion(char *data, uint16_t len)
{
    if(data == NULL){
        return -QM_EINVAL;
    }
    memcpy(data, XJIANG_FIRMWARE_VERSION, strlen(XJIANG_FIRMWARE_VERSION));
    return QM_EOK;
}

int hal_get_wifi_mac(uint8_t mac[6])
{
    if(mac == NULL){
        return -QM_EINVAL;
    }
    hal_wifi_get_mac_addr(NULL, mac);
    QM_LOGD(QM_LOG_TAG, "WIFI MAC is %02X:%02X:%02X:%02X:%02X:%02X.\r\n",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    return QM_EOK;

}

int hal_get_ble_mac(uint8_t mac[6])
{
    if(mac == NULL){
        return -QM_EINVAL;
    }

    if(!ble_init){
        ble_init = QM_TRUE;
        ble_controller_init(0);
        hci_driver_init();
        bt_enable(NULL);
    }


    uint8_t  ble_mac[6] = {0};
    qm_ble_get_mac(mac);

    QM_LOGD(QM_LOG_TAG, "BT MAC is %02X:%02X:%02X:%02X:%02X:%02X.\r\n",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);

    
    return QM_EOK;
}


static void ble_adv_timer_callback(qm_timer_t *timer, void *arg)
{
    prodtst_ble_adv_info_t *ble_adv_info = (prodtst_ble_adv_info_t *)arg;
    if(arg == NULL){
        return;
    }

    qm_timer_stop(&ble_adv_info->adv_timer);

    qm_mutex_lock(&ble_adv_info->adv_lock, QM_WAIT_FOREVER);
    ble_adv_info->is_use = QM_FALSE;    
    qm_timer_free(&ble_adv_info->adv_timer);
    bt_le_adv_stop();
    qm_mutex_unlock(&ble_adv_info->adv_lock);
}

int hal_ble_adv_set(uint32_t adv_interval, uint32_t adv_duration, uint8_t *adv_data, uint16_t len)
{
    int ret = QM_EOK;
    int adv_index = 0;
    int adv_len = 0;
    int adv_init_len = 5;
    struct bt_data ad[adv_init_len];
    struct bt_le_adv_param param = {0};
    
    if(adv_data == NULL || len == 0 || len > 32){
        return -QM_EINVAL;
    }


    adv_interval = (adv_interval * 1000 / 625);

    param.id = 0;
    param.interval_min = adv_interval;
    param.interval_max = adv_interval;
    param.options = (BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_ONE_TIME);

    QM_HEX_LOGD(QM_LOG_TAG, "adv_data ", adv_data, len);
    
    if(!prodtst_ble_adv_info.is_use){

        prodtst_ble_adv_info.is_use = QM_TRUE;

        if(!qm_mutex_is_valid(&prodtst_ble_adv_info.adv_lock)){
            qm_mutex_new(&prodtst_ble_adv_info.adv_lock);
        }

        qm_timer_new(&prodtst_ble_adv_info.adv_timer, ble_adv_timer_callback, (void *)&prodtst_ble_adv_info, adv_duration, 0);

        if(!ble_init){
            ble_init = QM_TRUE;
            ble_controller_init(0);
            hci_driver_init();
            bt_enable(NULL);
        }
    }

    qm_mutex_lock(&prodtst_ble_adv_info.adv_lock, QM_WAIT_FOREVER);

    while (adv_index < len)
    {
        uint8_t flag = 6;

        ad[adv_len].data_len = adv_data[adv_index] - 1;
        ad[adv_len].type	= adv_data[adv_index + 1];
        ad[adv_len].data	= &adv_data[adv_index + 2];
        
        QM_LOGD(QM_LOG_TAG, "data_len %02X TYPE %02X DATA %02X",  
            ad[adv_len].data_len, 
            ad[adv_len].type,
            *ad[adv_len].data);
        

        adv_index += ad[adv_len].data_len + 2;
        adv_len++;
    }
    
    QM_LOGD(QM_LOG_TAG, "bt_le_adv_start ");
    bt_le_adv_stop();
    ret = bt_le_adv_start(&param, ad, adv_len, NULL, 0);
    qm_mutex_unlock(&prodtst_ble_adv_info.adv_lock);

    if (ret != QM_EOK) {
        QM_LOGD(QM_LOG_TAG, "bt_le_adv_start FAIL");
        return -QM_EINVAL;
    }

    qm_mutex_lock(&prodtst_ble_adv_info.adv_lock, QM_WAIT_FOREVER);
    qm_timer_stop(&prodtst_ble_adv_info.adv_timer);
    qm_timer_change(&prodtst_ble_adv_info.adv_timer, adv_duration);
    qm_timer_start(&prodtst_ble_adv_info.adv_timer);
    qm_mutex_unlock(&prodtst_ble_adv_info.adv_lock);

    return QM_EOK;
}

static int awss_wifi_scan_result_cb(
            const char ssid[HAL_MAX_SSID_LEN],
            const uint8_t bssid[ETH_ALEN],
            enum AWSS_AUTH_TYPE auth,
            enum AWSS_ENC_TYPE encry,
            uint8_t channel, signed char rssi,
            int is_last_ap)
{
    QM_LOGD(QM_LOG_TAG,"ssid: %s,rssi: %d",ssid, rssi);

    if(!strncmp(ssid, prodtst_rf_wifi_info.test_siid, strlen(prodtst_rf_wifi_info.test_siid))){
        prodtst_rf_wifi_info.test_wifi_rssi = rssi;
        qm_sem_signal(&prodtst_rf_wifi_info.test_sem);
    }
    return 0;
}

int hal_wifi_rf_test(char *ssid, int8_t *rssi)
{
    int ret = QM_EOK;
    qm_sem_new(&prodtst_rf_wifi_info.test_sem, 0);
    
    memset(prodtst_rf_wifi_info.test_siid, 0, sizeof(prodtst_rf_wifi_info.test_siid));
    memcpy(prodtst_rf_wifi_info.test_siid, ssid + 1, strlen(ssid) - 4);

    hal_wifi_suspend_station(NULL);
    HAL_Wifi_Scan(awss_wifi_scan_result_cb);

    ret = qm_sem_wait(&prodtst_rf_wifi_info.test_sem, 10 * 1000);
    qm_sem_free(&prodtst_rf_wifi_info.test_sem);
    if(ret != QM_EOK){
        return QM_AT_RESULT_FAILE;
    }   

    *rssi =  prodtst_rf_wifi_info.test_wifi_rssi;
    return ret;
}

int hal_ali_cert_set(char product_id[9], 
                    char product_key[21], 
                    char product_secret[65], 
                    char device_name[33], 
                    char device_secret[65])

{
    int ret = QM_EOK;

    ret = aos_kv_set( "linkkit_product_id", product_id, strlen(product_id)+1, 1);
    if(ret != QM_EOK){
        return -QM_EINVAL;
    }

    ret = aos_kv_set( "linkkit_product_key", product_key, strlen(product_key)+1, 1);
    if(ret != QM_EOK){
        return -QM_EINVAL;
    }

    ret = aos_kv_set( "linkkit_product_secret", product_secret, strlen(product_secret)+1, 1);
    if(ret != QM_EOK){
        return -QM_EINVAL;
    }

    ret = aos_kv_set( "linkkit_device_name", device_name, strlen(device_name)+1, 1);
    if(ret != QM_EOK){
        return -QM_EINVAL;
    }

    ret = aos_kv_set( "linkkit_device_secret", device_secret, strlen(device_secret)+1, 1);
    if(ret != QM_EOK){
        return -QM_EINVAL;
    }

    return QM_EOK;
}

int hal_ali_cert_get(char product_id[9], 
                    char product_key[21], 
                    char product_secret[65], 
                    char device_name[33], 
                    char device_secret[65])

{
    int ret = QM_EOK;
    int read_len = 9;
    ret = aos_kv_get( "linkkit_product_id", product_id, &read_len );
    if(ret != 0){
        goto _error;
    }   

    read_len = 21;
    ret = aos_kv_get("linkkit_product_key", product_key, &read_len);
    if(ret != 0){
        goto _error;
    }   

    read_len = 65;
    ret = aos_kv_get("linkkit_product_secret", product_secret, &read_len);
    if(ret != 0){
        goto _error;
    } 

    read_len = 33;
    ret = aos_kv_get("linkkit_device_name", device_name, &read_len);
    if(ret != 0){
        goto _error;
    } 

    read_len = 65;
    ret = aos_kv_get("linkkit_device_secret", device_secret, &read_len);
    if(ret != 0){
        goto _error;
    } 

    return QM_EOK;

_error:

    memset(product_id, 0, 9);
    memset(product_key, 0, 21);
    memset(product_secret, 0, 65);
    memset(device_name, 0, 33);
    memset(device_secret, 0, 65);

    return QM_EOK;
}

prodtst_client_generic_cmd_t generic_hal =
{
    .fwname_get = hal_get_fwname,
    .fwver_get  = hal_get_fwversion,
#if CONFIG_PRODTST_CLIENT_AT_WIFI
    .wifi_mac_get = hal_get_wifi_mac,
#endif
#if CONFIG_PRODTST_CLIENT_AT_BLE
    .ble_mac_get = hal_get_ble_mac,
    .ble_adv_set = hal_ble_adv_set,
#endif
#if CONFIG_PRODTST_CLIENT_AT_WIFI_RF       
    .wifi_rf_test = hal_wifi_rf_test,
#endif
#if CONFIG_IOT_GENERIC_PLATFORM_ALI_SUPPORT
    .ali_cert_set = hal_ali_cert_set,
    .ali_cert_get = hal_ali_cert_get,
#endif
};

void prodtst_client_generic_hal_init(void)
{
    prodtst_client_generic_cmd_register(&generic_hal);
}
