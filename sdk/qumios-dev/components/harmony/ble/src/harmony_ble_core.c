#include "qm.h"
#include "qm_log.h"
#include "qm_kv.h"
#include "qm_ble_gap.h"
#include "harmony_ble_hal.h"
#include "harmony_ble_comm.h"
#include "harmony_ble_core.h"
#include "harmony_ble_trans.h"
#include "harmony_ble_ota.h"


#define LOG_TAG "harmony"

#define HARMONY_BLE_CORE_KEY    ("core")

typedef struct 
{
	int  registered;		// 是否注册
	uint8_t  authCode[16];  	// authCode
	uint8_t  authCodeId[16];	// authCodeId
}harmony_ble_core_store_t;

typedef struct 
{
    int connect;
    uint8_t is_near;
    uint8_t adv_active;
    uint16_t adv_int_max;
    uint16_t adv_int_min;
    qm_timer_t adv_timerout;
    harmony_ble_comm_param_t *param;
    harmony_ble_core_store_t store;
}harmony_ble_core_handle_t;

static int on_char_read_callback(void *buf, int len);
static int on_char_write_callback(void *buf, int len);
static int on_char_ota_ctrl_callback(void *buf, int len);
static int on_char_ota_data_callback(void *buf, int len);
static void event_notify_callback(harmony_ble_stack_event_t event);

static harmony_ble_init_t ble_info = {
    .service_uuid = HARMONY_BLE_UUID_SERVICE,
    .service_otauuid = HARMONY_BLE_UUID_OTA_SERVICE,
    .rc = {
        .uuid = HARMONY_BLE_UUID_READ,
        .on_char_read_t = on_char_read_callback,
    },
    .wc = {
        .uuid = HARMONY_BLE_UUID_WRITE,
        .on_char_write_t = on_char_write_callback,
    },
    .ota_ctrl = {
        .uuid = HARMONY_BLE_UUID_OTACTRL,
        .ota_ctrl_t = on_char_ota_ctrl_callback,
    },
    .ota_data = {
        .uuid = HARMONY_BLE_UUID_OTADATA,
        .on_data_t = on_char_ota_data_callback,
    },
    .event_notify_callback_t = event_notify_callback,
};


static harmony_ble_core_handle_t harmony_handle = {0};

static int harmony_gap_ble_adv_start(void);
static int harmony_gap_ble_near_adv_stop(void);
static int harmony_gap_ble_1near_adv(uint8_t sub_pid, int8_t adv_power, char *prod_id, char *strmac, uint32_t timeout);
static int harmony_gap_ble_2near_adv(uint8_t sub_pid, int8_t adv_power, char *prod_id, char *strmac, uint32_t timeout);
static int harmony_ble_gap_unregistered_adv(char *productId, char *str_mac, char *name, uint8_t len);
static int harmony_ble_gap_registered_adv(char *productId, char *str_mac, char *name, uint8_t len, 
                                uint16_t interval, uint8_t interval_unit, uint8_t conn_req);

static void qm_gap_ble_callback(qm_gap_ble_cb_event_t event, qm_ble_gap_cb_param_t *param)
{
    qm_ble_adv_params_t adv_param = {
        .adv_int_max = harmony_handle.adv_int_max,
        .adv_int_min = harmony_handle.adv_int_min,
    };

    switch (event)
    {
        case QM_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        case QM_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
            qm_ble_gap_start_advertising(&adv_param);
        break;

        default:
        
        break;
    }
}

static int harmony_gap_ble_adv_start(void)
{
    char str_mac[13] = {0};
    uint8_t xj_mac[6] = {0};
    harmony_ble_comm_info_t *comm_param = &harmony_handle.param->info;
    QM_RETURN_ON_FALSE(comm_param, -QM_EINVAL, LOG_TAG, "comm_param invail");

    qm_ble_gap_get_local_addr(xj_mac);
    qm_snprintf(str_mac, 13, "%02X%02X%02X%02X%02X%02X", xj_mac[0], xj_mac[1], xj_mac[2], xj_mac[3], xj_mac[4], xj_mac[5]);
    
    if(!harmony_handle.store.registered){
        harmony_ble_gap_unregistered_adv(comm_param->productid, str_mac, 
                                        comm_param->adv_name, strlen(comm_param->adv_name));
    }else{
         harmony_ble_gap_registered_adv(comm_param->productid, str_mac, 
                                        comm_param->adv_name, strlen(comm_param->adv_name),
                                        1, HARMONY_BLE_DEFAULT_MMP_INTERVAL_S, 0);
    }

    return QM_EOK;
}

static int harmony_ble_set_registered_dev_name(void)
{
    char str_mac[13] = {0};
    uint8_t xj_mac[6] = {0};
    uint8_t adv_rsp_data[31] = {0};
    uint8_t mmp = HARMONY_BLE_DEFAULT_MMP_INTERVAL_S << 1 | 0;
    harmony_ble_comm_info_t *comm_param = &harmony_handle.param->info;


    qm_ble_gap_get_local_addr(xj_mac);
    qm_snprintf(str_mac, 13, "%02X%02X%02X%02X%02X%02X", xj_mac[0], xj_mac[1], xj_mac[2], xj_mac[3], xj_mac[4], xj_mac[5]);
    
    uint8_t rsp_data[] = HARMONY_BLE_DEFAULT_REG_HEAD_DATA(0); 
    uint8_t rsp_payload_data[] = HARMONY_BLE_DEFAULT_REG_PAYLODE_DATA(comm_param->productid, str_mac, mmp, 1);

    memcpy(adv_rsp_data, rsp_data, sizeof(rsp_data));
    memcpy(adv_rsp_data + sizeof(rsp_data), comm_param->adv_name, strlen(comm_param->adv_name));
    memcpy(adv_rsp_data + sizeof(rsp_data) + strlen(comm_param->adv_name), rsp_payload_data, sizeof(rsp_payload_data));

    harmony_ble_hal_dev_name_set(adv_rsp_data + 2, strlen(adv_rsp_data + 2));

    return QM_EOK;
}

static void event_notify_callback(harmony_ble_stack_event_t event)
{
    switch (event)
    {
        case HARMONY_BLE_STACK_EVENT_INTT_SUCCESS:

            harmony_ble_set_registered_dev_name();

            if(harmony_handle.store.registered){
                if(harmony_handle.param->event_notify){
                    harmony_handle.param->event_notify(HARMONY_BLE_EVENT_DEVICE_REGISTERED, NULL);
                }
            }else{
                if(harmony_handle.param->event_notify){
                    harmony_handle.param->event_notify(HARMONY_BLE_EVENT_CONFIG, NULL);
                }
            }
        break;
        case HARMONY_BLE_STACK_EVENT_CONNECT:
            if (harmony_handle.adv_active){
                qm_timer_stop(&harmony_handle.adv_timerout);
                harmony_handle.adv_active = QM_FALSE;
            }
            harmony_handle.connect = QM_TRUE;
        break;
        case HARMONY_BLE_STACK_EVENT_DISCONNECT:

            harmony_handle.connect = QM_FALSE;
            harmony_handle.adv_active = QM_FALSE;

            harmony_ble_trans_flush();

            if(harmony_handle.param->event_notify){
                harmony_handle.param->event_notify(HARMONY_BLE_EVENT_DISCONNECT_APP, NULL);
            }

        break;
        default:

        break;
    }
}

static void harmony_ble_near_adv_timeout(qm_timer_t *timer, void *arg)
{
    QM_LOGD(LOG_TAG, "NEAR ADV TIMEOUT");
    harmony_gap_ble_near_adv_stop();
    harmony_gap_ble_adv_start();

}

static int on_char_read_callback(void *buf, int len)
{
    return QM_EOK;
}

static int on_char_write_callback(void *buf, int len)
{
    harmony_ble_transport_rx(buf, len);
    return QM_EOK;
}

static int on_char_ota_ctrl_callback(void *buf, int len)
{
    harmony_ble_ota_ctrl_read(buf, len);
    return QM_EOK;
}

static int on_char_ota_data_callback(void *buf, int len)
{
    harmony_ble_ota_data_read(buf, len);
    return QM_EOK;
}


int harmony_ble_core_store_set(int registered, uint8_t authCode[16], uint8_t authCodeId[16])
{
    harmony_ble_core_store_t *core_store = &harmony_handle.store;
    core_store->registered = registered;
    memcpy(core_store->authCode, authCode, sizeof(core_store->authCode));
    memcpy(core_store->authCodeId, authCodeId, sizeof(core_store->authCodeId));
    return qm_kv_set(HARMONY_BLE_CORE_KEY, core_store, sizeof(harmony_ble_core_store_t), 1);
}

int harmony_ble_core_store_get(int *registered, uint8_t authCode[16], uint8_t authCodeId[16])
{
    harmony_ble_core_store_t *core_store = &harmony_handle.store;

    QM_RETURN_ON_FALSE(registered, -QM_EINVAL, LOG_TAG, "registered invail");
    QM_RETURN_ON_FALSE(authCode, -QM_EINVAL, LOG_TAG, "authCode invail");
    QM_RETURN_ON_FALSE(authCodeId, -QM_EINVAL, LOG_TAG, "authCodeId invail");

    *registered = core_store->registered;
    memcpy(authCode, core_store->authCode, sizeof(core_store->authCode));
    memcpy(authCodeId, core_store->authCodeId, sizeof(core_store->authCodeId));

    return QM_EOK;
}

int harmony_ble_core_init(harmony_ble_comm_param_t *param)
{
    int ret = QM_EOK;
    int store_len = sizeof(harmony_ble_core_store_t);
    QM_RETURN_ON_FALSE(param, -QM_EINVAL, LOG_TAG, "param invail");
    
    qm_kv_get(HARMONY_BLE_CORE_KEY, &harmony_handle.store, &store_len);
    
    QM_LOGD(LOG_TAG, "reg : %d", harmony_handle.store.registered);
    QM_HEX_LOGD(LOG_TAG, "authCode: ", harmony_handle.store.authCode, sizeof(harmony_handle.store.authCode));
    QM_HEX_LOGD(LOG_TAG, "authCodeId: ", harmony_handle.store.authCodeId, sizeof(harmony_handle.store.authCode));
    
    ret = qm_timer_new(&harmony_handle.adv_timerout, harmony_ble_near_adv_timeout, NULL, HARMONY_BLE_NEAR_ADV_TIMEOUT, 0);
    if(ret != QM_EOK){
        return ret;
    }
    
    harmony_handle.param = param;
    ble_info.pair_mode = param->pair_mode;
    harmony_handle.adv_int_max = HARMONY_BLE_COMMON_ADV_INTERVAL * 1000 / 625;
    harmony_handle.adv_int_min = HARMONY_BLE_COMMON_ADV_INTERVAL * 1000 / 625; 

    ret = harmony_ble_hal_gatt_stack_init(&ble_info);
    if(ret != QM_EOK){
        return ret;
    }

    return qm_ble_gap_register_callback(qm_gap_ble_callback);
}

static int harmony_ble_gap_unregistered_adv(char *productId, char *str_mac, char *name, uint8_t len) 
{
    uint8_t adv_rsp_data_len = 0;
    uint8_t adv_rsp_data[31] = {0};

    QM_RETURN_ON_FALSE(len, -QM_EINVAL, LOG_TAG, "len 0");
    QM_RETURN_ON_FALSE(name, -QM_EINVAL, LOG_TAG, "name invail");
    QM_RETURN_ON_FALSE(str_mac, -QM_EINVAL, LOG_TAG, "str_mac invail");
    QM_RETURN_ON_FALSE(productId, -QM_EINVAL, LOG_TAG, "productId invail");

    adv_rsp_data_len = HARMONY_BLE_UNREG_PAYLODE_DATA_LEN + len + HARMONY_BLE_UNREG_HEAD_DATA_LEN;

    uint8_t rsp_data[] = HARMONY_BLE_DEFAULT_UNREG_HEAD_DATA(adv_rsp_data_len - 1); 
    uint8_t rsp_payload_data[] = HARMONY_BLE_DEFAULT_UNREG_PAYLODE_DATA(productId, str_mac);

    memcpy(adv_rsp_data, rsp_data, sizeof(rsp_data));
    memcpy(adv_rsp_data + sizeof(rsp_data), name, len);
    memcpy(adv_rsp_data + sizeof(rsp_data) + len, rsp_payload_data, sizeof(rsp_payload_data));

    if(harmony_handle.adv_active){
        if(harmony_handle.is_near){
            qm_timer_stop(&harmony_handle.adv_timerout);
        }
        qm_ble_gap_stop_advertising();
    }

    harmony_handle.adv_active = QM_TRUE;
    qm_ble_gap_set_adv_data_raw(NULL, 0);
    qm_ble_gap_set_scan_rsp_data_raw(adv_rsp_data, adv_rsp_data_len);

    return QM_EOK;
}

static int harmony_ble_gap_registered_adv(char *productId, char *str_mac, char *name, uint8_t len, 
                                uint16_t interval, uint8_t interval_unit, uint8_t conn_req) 
{
    uint8_t adv_rsp_data_len = 0;
    uint8_t adv_rsp_data[31] = {0};
    uint8_t mmp = interval_unit << 1 | conn_req;

    QM_RETURN_ON_FALSE(len, -QM_EINVAL, LOG_TAG, "len 0");
    QM_RETURN_ON_FALSE(name, -QM_EINVAL, LOG_TAG, "name invail");
    QM_RETURN_ON_FALSE(str_mac, -QM_EINVAL, LOG_TAG, "str_mac invail");
    QM_RETURN_ON_FALSE(productId, -QM_EINVAL, LOG_TAG, "productId invail");

    adv_rsp_data_len = HARMONY_BLE_REG_PAYLODE_DATA_LEN + len + HARMONY_BLE_REG_HEAD_DATA_LEN;

    uint8_t rsp_data[] = HARMONY_BLE_DEFAULT_REG_HEAD_DATA(adv_rsp_data_len - 1); 
    uint8_t rsp_payload_data[] = HARMONY_BLE_DEFAULT_REG_PAYLODE_DATA(productId, str_mac, mmp, interval);

    memcpy(adv_rsp_data, rsp_data, sizeof(rsp_data));
    memcpy(adv_rsp_data + sizeof(rsp_data), name, len);
    memcpy(adv_rsp_data + sizeof(rsp_data) + len, rsp_payload_data, sizeof(rsp_payload_data));

    if(harmony_handle.adv_active){
        qm_ble_gap_stop_advertising();
        if(harmony_handle.is_near){
            qm_timer_stop(&harmony_handle.adv_timerout);
        }
    }

    harmony_handle.adv_active = QM_TRUE;
    qm_ble_gap_set_adv_data_raw(NULL, 0);
    qm_ble_gap_set_scan_rsp_data_raw(adv_rsp_data, adv_rsp_data_len);

    return QM_EOK;
}

static int harmony_gap_ble_1near_adv(uint8_t sub_pid, int8_t adv_power, char *prod_id, char *strmac, uint32_t timeout)
{
    harmony_ble_core_handle_t *handle = &harmony_handle;
    uint8_t adv_data[] = HARMONY_BLE_1NEAR_ADV_DATA(sub_pid, adv_power, prod_id, strmac);

    harmony_handle.adv_int_max = HARMONY_BLE_NEAR_ADV_INTERVAL * 1000 / 625;
    harmony_handle.adv_int_min = HARMONY_BLE_NEAR_ADV_INTERVAL * 1000 / 625; 

    if(harmony_handle.adv_active){
        qm_ble_gap_stop_advertising();
    }

    harmony_handle.is_near = QM_TRUE;
    harmony_handle.adv_active = QM_TRUE;
    qm_ble_gap_set_scan_rsp_data_raw(NULL, 0);
    qm_ble_gap_set_adv_data_raw(adv_data, sizeof(adv_data));

    qm_timer_change(&handle->adv_timerout, timeout);

    return QM_EOK;
}

static int harmony_gap_ble_2near_adv(uint8_t sub_pid, int8_t adv_power, char *prod_id, char *strmac, uint32_t timeout)
{
    harmony_ble_core_handle_t *handle = &harmony_handle;
    uint8_t adv_data[] = HARMONY_BLE_2NEAR_ADV_DATA(sub_pid, adv_power, prod_id, strmac); 
    harmony_handle.adv_int_max = HARMONY_BLE_NEAR_ADV_INTERVAL * 1000 / 625;
    harmony_handle.adv_int_min = HARMONY_BLE_NEAR_ADV_INTERVAL * 1000 / 625; 

    if(handle->adv_active){
        qm_ble_gap_stop_advertising();
    }

    handle->is_near = QM_TRUE;
    handle->adv_active = QM_TRUE;

    qm_ble_gap_set_scan_rsp_data_raw(NULL, 0);
    qm_ble_gap_set_adv_data_raw(adv_data, sizeof(adv_data));
    
    qm_timer_change(&handle->adv_timerout, timeout);

    return QM_EOK;
}

static int harmony_gap_ble_near_adv_stop(void)
{
    harmony_ble_core_handle_t *handle = &harmony_handle;
    harmony_handle.adv_int_max = HARMONY_BLE_COMMON_ADV_INTERVAL * 1000 / 625;
    harmony_handle.adv_int_min = HARMONY_BLE_COMMON_ADV_INTERVAL * 1000 / 625; 

    if(!handle->adv_active || !handle->is_near){
        return QM_EOK;
    }

    handle->adv_active = QM_FALSE;
    handle->is_near = QM_FALSE;

    qm_timer_stop(&handle->adv_timerout);

    qm_ble_gap_stop_advertising();
    
    qm_ble_gap_set_adv_data_raw(NULL, 0);
    
    return QM_EOK;
}

int harmony_ble_core_adv_start(uint8_t near_mode)
{
    char str_mac[13] = {0};
    uint8_t xj_mac[6] = {0};
    harmony_ble_comm_info_t *comm_param = &harmony_handle.param->info;
    QM_RETURN_ON_FALSE(comm_param, -QM_EINVAL, LOG_TAG, "comm_param invail");

    qm_ble_gap_get_local_addr(xj_mac);
    qm_snprintf(str_mac, 13, "%02X%02X%02X%02X%02X%02X", xj_mac[0], xj_mac[1], xj_mac[2], xj_mac[3], xj_mac[4], xj_mac[5]);
    
    if(harmony_handle.store.registered){
        if(near_mode){
            harmony_gap_ble_2near_adv(comm_param->sub_pid, comm_param->tx_power, 
                                    comm_param->productid, str_mac, HARMONY_BLE_DEFAULT_ADV_TIMEOUT);
        }else{
            harmony_ble_gap_registered_adv(comm_param->productid, str_mac, 
                                            comm_param->adv_name, strlen(comm_param->adv_name),
                                            1, HARMONY_BLE_DEFAULT_MMP_INTERVAL_S, 0);
        }
    }else{
        if(near_mode){
            harmony_gap_ble_1near_adv(comm_param->sub_pid, comm_param->tx_power, 
                                    comm_param->productid, str_mac, HARMONY_BLE_DEFAULT_ADV_TIMEOUT);
        }else{
            harmony_ble_gap_unregistered_adv(comm_param->productid, str_mac, 
                                                comm_param->adv_name, strlen(comm_param->adv_name));
        }
    }

    harmony_handle.adv_active = QM_TRUE;

    return QM_EOK;
}

int harmony_ble_core_adv_stop(void)
{
    harmony_handle.adv_active = QM_FALSE;

    qm_timer_stop(&harmony_handle.adv_timerout);    

    qm_ble_gap_stop_advertising();
    
    return QM_EOK;
}

int harmony_ble_core_unregisterd(uint8_t near_mode)
{
    harmony_ble_core_store_t *core_store = &harmony_handle.store;

    core_store->registered = QM_FALSE;

    if(harmony_handle.connect){
        harmony_ble_hal_disconnect(0);
    }else{
        harmony_ble_core_adv_start(near_mode);
    }

    return qm_kv_set(HARMONY_BLE_CORE_KEY, core_store, sizeof(harmony_ble_core_store_t), 1);
}

int harmony_ble_core_conn_status_get(void)
{
    return harmony_handle.connect;
}