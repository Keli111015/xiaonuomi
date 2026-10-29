#ifndef PRODTST_CLIENT_H
#define PRODTST_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"
#include "qm_config.h"
#include "prodtst_client_common.h"

#if CONFIG_QM_WORK_SUPPORT
#include "qm_event.h"

/** Production-test client event base used by event-loop APIs. */
QM_EVENT_DECLARE_BASE(QM_EVENT_PRODTST_CLIENT);
#endif

typedef struct 
{
    int (*fwname_get)(char *data, uint16_t len);
    int (*fwname_verify)(char *data, uint16_t len);
#if CONFIG_PRODTST_CLIENT_AT_BLE 
    int (*ble_mac_get)(uint8_t mac[6]);
#if CONFIG_PRODTST_CLIENT_AT_BLE_ADV
    int (*ble_adv_set)( uint32_t adv_interval,
                        uint32_t adv_duration,
                        uint8_t *adv_data, 
                        uint16_t len);
#endif
#endif
#if CONFIG_PRODTST_CLIENT_AT_WIFI 
    int (*wifi_mac_get)(uint8_t mac[6]);
#endif
#if CONFIG_PRODTST_CLIENT_AT_WIFI_RF       
    int (*wifi_rf_test)(char *ssid, int8_t *rssi);
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_MI_SUPPORT
    int (*mi_pid_model_set)(uint32_t product_id, char model[65]);
    int (*mi_pid_get)(void);
    int (*mi_is_use_get)(void);
    int (*mi_model_get)(char model[65]);
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_ALI_SUPPORT
    int (*ali_cert_set)(char product_id[9], 
                        char product_key[21], 
                        char product_secret[65], 
                        char device_name[33], 
                        char device_secret[65]);
                        
    int (*ali_cert_get)(char product_id[9], 
                        char product_key[21], 
                        char product_secret[65], 
                        char device_name[33], 
                        char device_secret[65]);
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_BAIDU_SUPPORT
    int (*baidu_cert_set)(uint32_t product_id, 
                          char device_name[33], 
                          char device_secret[65]);

    int (*baidu_cert_get)(uint32_t *product_id, 
                          char device_name[33], 
                          char device_secret[65]);
#endif

}prodtst_client_generic_cmd_t;

typedef enum{ 

    PRODTST_CLIENT_CHANNEL_TYPE_MIN = 0,

#if CONFIG_PRODTST_CLIENT_UART_SUPPORT
    PRODTST_CLIENT_CHANNEL_TYPE_UART,
#endif
#if CONFIG_PRODTST_CLIENT_WIFI_SUPPORT
    PRODTST_CLIENT_CHANNEL_TYPE_WIFI,
#endif
#if CONFIG_PRODTST_CLIENT_BLE_GATT_SUPPORT
    PRODTST_CLIENT_CHANNEL_TYPE_BLE_GATT ,
#endif
#if CONFIG_PRODTST_CLIENT_BLE_MESH_SUPPORT
    PRODTST_CLIENT_CHANNEL_TYPE_BLE_MESH ,
#endif

    PRODTST_CLIENT_CHANNEL_TYPE_MAX,    
}prodtst_client_channel_t;

typedef enum{ 
    PRODTST_CLIENT_ENTER_PRODTST_TIMEOUT   = 0,
    PRODTST_CLIENT_ENTER_PRODTST_SUCCESS ,
    PRODTST_CLIENT_ENTER_PRODTST_FAILED,
    PRODTST_CLINET_PRODTST_TEST_RECV_TIMEOUT,
    PRODTST_CLINET_PRODTST_TEST_FAILED,
    PRODTST_CLINET_PRODTST_RESULT_OK,
    PRODTST_CLINET_PRODTST_RESULT_FAILED,
    PRODTST_CLIENT_EXIT_PRODTST_SUCCESS,
    PRODTST_CLIENT_EXIT_PRODTST_FAILED,
    PRODTST_CLIENT_EVENT_TYPE_MAX,    
}prodtst_client_event_t;

typedef struct {
    void *(*init)(void);
    int  (*write)(void *handle, char *data, int len);
    int  (*read)(void *handle, char *data, int len, int timeout);
    int  (*deinit)(void *handle);
}prodtst_client_link_t;


typedef  void (*prodtst_client_event_cb)(prodtst_client_event_t event);

int prodtst_client_event_notify_register(prodtst_client_event_cb event_fn);
   

#if CONFIG_PRODTST_CLIENT_EXT_LINK_SUPPORT
int prodtst_client_channel_link_register(prodtst_client_channel_t channel, prodtst_client_link_t *link_fn);
#endif

#if CONFIG_PRODTST_CLIENT_GEN_CMD_SUPPORT
int prodtst_client_generic_cmd_register(prodtst_client_generic_cmd_t *generic_cmd); 
#endif

#if CONFIG_PRODTST_CLIENT_BLE_MESH_SUPPORT
int prodtst_client_ble_mesh_pid_set(uint16_t pid, uint8_t scan_time);
#endif

void *prodtst_client_init(prodtst_client_channel_t channel, void *prodtst_func_cmd , uint16_t cmd_num);
int prodtst_client_deinit(void *handle);

int prodtst_client_event_post(prodtst_client_event_t event);


#ifdef __cplusplus
}
#endif


#endif /* PRODTST_CLIENT_H */
