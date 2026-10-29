#ifndef _PRODTST_BLE_SERVER_H_
#define _PRODTST_BLE_SERVER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "prodtst_ble_comm.h"

#if CONFIG_PRODTST_BLE_SERVER

typedef enum{
    PRODTST_BLE_SERVER_STATE_SET,        //被测设备对于产测盒子的产测状态设置的应答数据
    PRODTST_BLE_SERVER_CONNECT,          //被测设备连接请求
    PRODTST_BLE_SERVER_TEST,              //被测设备对于产测盒子的测试用例下发的应答数据
    PRODTST_BLE_SERVER_REPORT,           //被测设备结果上报请求
    PRODTST_BLE_SERVER_EVENT_MAX,
}prodtst_ble_server_event_type_t;

typedef struct 
{
    void *handle;
    uint8_t conn_addr[6];
}prodtst_ble_server_conn_info_t;

typedef struct 
{
    prodtst_ble_data_test_info_t gen_info;
    prodtst_ble_server_conn_info_t conn_info;
}prodtst_ble_server_test_info_t;

typedef union 
{
    prodtst_ble_code_info_t code_info;
    prodtst_ble_prodtst_t prodtst_info;
    prodtst_ble_server_conn_info_t conn_info;
    prodtst_ble_server_test_info_t test_info;
}prodtst_ble_server_event_info_t;

typedef struct 
{
    prodtst_ble_param_t ble_param;
    prodtst_ble_type_t channel_type; //产测类型 0：广播类产测 1：连接类产测
}prodtst_ble_server_param_t;

typedef int32_t (*prodtst_ble_server_event_t)(prodtst_ble_server_event_type_t event_type, prodtst_ble_server_event_info_t *p_event);

int32_t prodtst_ble_server_init(prodtst_ble_server_param_t *param);
int32_t prodtst_ble_server_deinit(void);

int32_t prodtst_ble_server_pid_change(uint16_t pid);
int32_t prodtst_ble_server_event_register(prodtst_ble_server_event_t event_cb);

int32_t prodtst_ble_server_prodtst_status_set(uint8_t conn_id, prodtst_ble_prodtst_t status, uint8_t send_count);
int32_t prodtst_ble_server_test(uint8_t conn_id, uint8_t test_id, uint8_t *data, int len, uint8_t send_count);

int32_t prodtst_ble_server_close(uint8_t conn_id);
int32_t prodtst_ble_server_open(void *handle, uint8_t *conn_id);
int32_t prodtst_ble_server_conn_id_get(uint8_t mac[6], uint8_t *conn_id);
int32_t prodtst_ble_server_conn_mac_get(uint8_t conn_id, uint8_t mac[6]);

#if CONFIG_PRODTST_BLE_USE_EXT_SCAN
int32_t prodtst_ble_server_data_push(uint8_t mac[6], uint8_t *buff, int length);
#endif

#endif

#ifdef __cplusplus
}
#endif

#endif
