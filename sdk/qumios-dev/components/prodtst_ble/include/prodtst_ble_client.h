#ifndef _PRODTST_BLE_CLIENT_H_
#define _PRODTST_BLE_CLIENT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "prodtst_ble_comm.h"

#if CONFIG_PRODTST_BLE_CLIENT
typedef enum{
    PRODTST_BLE_CLIENT_STATE_SET,          //产测盒子产测状态设置 
    PRODTST_BLE_CLIENT_CONNECT,            //产测盒子应答被测设备连接请求
    PRODTST_BLE_CLIENT_TEST,                    //产测盒子测试用例下发
    PRODTST_BLE_CLIENT_TEST_REQ,                //产测盒子测试用例下发请求
    PRODTST_BLE_CLIENT_REPORT,             //产测盒子对于被测设备结果上报的应答数据
    PRODTST_BLE_CLIENT_EVENT_REQ,                //产测盒子事件请求
    PRODTST_BLE_CLIENT_EVENT_MAX,
}prodtst_ble_client_event_type_t;

typedef union 
{
    prodtst_ble_code_info_t code_info;
    prodtst_ble_conn_info_t conn_info;
    prodtst_ble_prodtst_t prodtst_info;
    prodtst_ble_data_test_info_t gen_info;
    prodtst_ble_event_info_t event_info;
}prodtst_ble_client_event_info_t;

typedef struct 
{
    prodtst_ble_param_t ble_param;
}prodtst_ble_client_param_t;

typedef int32_t (*prodtst_ble_client_event_t)(prodtst_ble_client_event_type_t event_type, prodtst_ble_client_event_info_t *p_event);

int32_t prodtst_ble_client_init(prodtst_ble_client_param_t *param);
int32_t prodtst_ble_client_deinit(void);

int32_t prodtst_ble_client_pid_change(uint16_t pid);
int32_t prodtst_ble_client_event_register(prodtst_ble_client_event_t event_cb);

#if CONFIG_PRODTST_BLE_CLIENT_TYPE_CONN
int32_t prodtst_ble_client_connect(void); 
int32_t prodtst_ble_client_test(uint8_t test_id, uint8_t *data, int len);
#endif

#if CONFIG_PRODTST_BLE_CLIENT_ADV_REPORT
int32_t prodtst_ble_client_prodtst_report(uint8_t test_id, uint8_t *data, int len);
#endif

#endif

#ifdef __cplusplus
}
#endif

#endif
