#include "qm.h"

#include "qm_ble_common.h"
#include "qm_ble_gap.h"
#include "qm_ble_hal_ble.h"
#include "qm_ble_hal_os.h"
#include "qm_log.h"

#include "esp_log.h"

#include "esp_system.h"
#include "esp_bt.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

#if CONFIG_BLE_MESH_SUPPORT_BLE_ADV
#include "esp_ble_mesh_ble_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#endif

#define GATTS_TAG "GATTS"

#define PROFILE_APP_ID 0
#define GATTS_NUM_HANDLE_APP (15)
#define PROFILE_MTU_MAX_SIZE (247)
#define BT_ADV_SCAN_UNIT(_ms) ((_ms) * 8 / 5)

typedef struct {
    uint8_t   *prepare_buf;
    int       prepare_len;
    uint16_t   handle;
} prepare_type_env_t;


typedef struct {
    uint16_t char_handle;
    uint16_t descr_handle;
    esp_bt_uuid_t char_uuid;
    esp_bt_uuid_t descr_uuid;
}qm_ble_hal_handle_t;

typedef struct{
    uint16_t    conn_id;
    uint16_t    gattc_if;
    prepare_type_env_t env;

    uint16_t service_handle;   
    qms_bt_init_t *init_param;
    esp_gatt_srvc_id_t service_id;
    
    qm_ble_hal_handle_t handle_wc;
    qm_ble_hal_handle_t handle_rc;
    qm_ble_hal_handle_t handle_ic;
    qm_ble_hal_handle_t handle_wwnrc;
    qm_ble_hal_handle_t handle_nc;

    void (*txdone)(uint8_t res);
}qm_ble_hal_ctx_t;

static qm_ble_hal_ctx_t g_ble_hal_ctx = {0};

static void prepare_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param){
    
    esp_gatt_status_t status = ESP_GATT_OK;
    /* Ensure that write data is not larger than max attribute length */
    if (param->write.offset > (PROFILE_MTU_MAX_SIZE - 3)) {
        status = ESP_GATT_INVALID_OFFSET;
    } else if ((param->write.offset + param->write.len) > (PROFILE_MTU_MAX_SIZE - 3)) {
        status = ESP_GATT_INVALID_ATTR_LEN;
    } else {
        /* If prepare buffer is not allocated, then allocate it */
        if (prepare_write_env->prepare_buf == NULL) {
            prepare_write_env->prepare_len = 0;
            prepare_write_env->prepare_buf = (uint8_t *) qm_malloc((PROFILE_MTU_MAX_SIZE - 3) * sizeof(uint8_t));
            if (prepare_write_env->prepare_buf == NULL) {
                ESP_LOGE(GATTS_TAG, "%s , failed to allocate prepare buf", __func__);
                status = ESP_GATT_NO_RESOURCES;
            }
        }
    }

    /* If prepare buffer is allocated copy incoming data into it */
    if (status == ESP_GATT_OK) {
        memcpy(prepare_write_env->prepare_buf + param->write.offset,
               param->write.value,
               param->write.len);
        prepare_write_env->prepare_len += param->write.len;
        prepare_write_env->handle = param->write.handle;
    }

    /* Send write response if needed */
    if (param->write.need_rsp) {
        esp_err_t response_err;
        /* If data was successfully appended to prepare buffer
         * only then have it reflected in the response */
        if (status == ESP_GATT_OK) {
            esp_gatt_rsp_t gatt_rsp = {0};
            gatt_rsp.attr_value.len = param->write.len;
            gatt_rsp.attr_value.handle = param->write.handle;
            gatt_rsp.attr_value.offset = param->write.offset;
            gatt_rsp.attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
            if (gatt_rsp.attr_value.len && param->write.value) {
                memcpy(gatt_rsp.attr_value.value, param->write.value, param->write.len);
            }
            response_err = esp_ble_gatts_send_response(gatts_if,
                           param->write.conn_id, param->write.trans_id, status, &gatt_rsp);
        } else {
            response_err = esp_ble_gatts_send_response(gatts_if,
                           param->write.conn_id, param->write.trans_id, status, NULL);
        }
        if (response_err != ESP_OK) {
            ESP_LOGE(GATTS_TAG, "Send response error in prep write");
        }
    }

    if (status != ESP_GATT_OK) {
        if (prepare_write_env->prepare_buf) {
            qm_free(prepare_write_env->prepare_buf);
            prepare_write_env->prepare_buf = NULL;
            prepare_write_env->prepare_len = 0;
        }
    }
}

static void exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param){
    
    qm_ble_hal_ctx_t *ble_ctx = &g_ble_hal_ctx;
    
    if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC){
        if(ble_ctx->handle_wc.char_handle == prepare_write_env->handle){
            if (ble_ctx->init_param && ble_ctx->init_param->wc.on_write) {
                ble_ctx->init_param->wc.on_write(prepare_write_env->prepare_buf, prepare_write_env->prepare_len);
            }
        }

        if(ble_ctx->handle_wwnrc.char_handle == prepare_write_env->handle){
            if (ble_ctx->init_param && ble_ctx->init_param->wwnrc.on_write) {
                ble_ctx->init_param->wwnrc.on_write(prepare_write_env->prepare_buf, prepare_write_env->prepare_len);
            }
        }
    }else{
        QM_LOGI(GATTS_TAG,"ESP_GATT_PREP_WRITE_CANCEL");
    }

    if (prepare_write_env->prepare_buf) {
        qm_free(prepare_write_env->prepare_buf);
        prepare_write_env->prepare_buf = NULL;
    }
    
    prepare_write_env->handle = 0;
    prepare_write_env->prepare_len = 0;
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    qm_ble_hal_ctx_t *ble_ctx = &g_ble_hal_ctx;

   switch (event) {

        case ESP_GATTS_REG_EVT:

            ble_ctx->gattc_if = gatts_if;

            ble_ctx->service_id.is_primary = true;
            ble_ctx->service_id.id.inst_id = 0x00;
            ble_ctx->service_id.id.uuid.len = ESP_UUID_LEN_16;
            ble_ctx->service_id.id.uuid.uuid.uuid16 = BLE_UUID_QMS_SERVICE;

            esp_ble_gatts_create_service(gatts_if, &ble_ctx->service_id, GATTS_NUM_HANDLE_APP);

        break;

        case ESP_GATTS_READ_EVT: 
        {
        
            esp_gatt_rsp_t rsp = {0};
            uint16_t max_len = PROFILE_MTU_MAX_SIZE;

            if(ble_ctx->handle_rc.char_handle != param->read.handle){
                break;
            }

            if(ble_ctx->init_param && ble_ctx->init_param->rc.on_read){
                rsp.attr_value.len = ble_ctx->init_param->rc.on_read( rsp.attr_value.value, max_len);
            }

            if(rsp.attr_value.len){
                esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                            ESP_GATT_OK, &rsp);
            }
        }
        break;

        case ESP_GATTS_WRITE_EVT: {
            
            
            if (param->write.is_prep){
                prepare_write_event_env(gatts_if, &ble_ctx->env, param);
                break;
            }

            if (param->write.len != 2){

                if(ble_ctx->handle_wc.char_handle == param->write.handle){
                     if (ble_ctx->init_param && ble_ctx->init_param->wc.on_write) {
                        ble_ctx->init_param->wc.on_write(param->write.value, param->write.len);
                    }
                }

                if(ble_ctx->handle_wwnrc.char_handle == param->write.handle){
                    if (ble_ctx->init_param && ble_ctx->init_param->wwnrc.on_write) {
                        ble_ctx->init_param->wwnrc.on_write(param->write.value, param->write.len);
                    }
                }

                if (param->write.need_rsp) {
                    esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
                }
                break;
            }

            uint16_t descr_value = param->write.value[1]<<8 | param->write.value[0];

            if (descr_value == 0x0001){
                if(ble_ctx->handle_nc.descr_handle == param->write.handle){
                    QM_LOGI(GATTS_TAG, "notify enable");
                    if (ble_ctx->init_param && ble_ctx->init_param->nc.on_ccc_change) {
                        ble_ctx->init_param->nc.on_ccc_change(QMS_CCC_VALUE_NOTIFY);
                    }
                }
            }else if (descr_value == 0x0002){
                if(g_ble_hal_ctx.handle_ic.descr_handle == param->write.handle){
                    QM_LOGI(GATTS_TAG, "indicate enable");
                    if (ble_ctx->init_param && ble_ctx->init_param->ic.on_ccc_change) {
                        ble_ctx->init_param->ic.on_ccc_change(QMS_CCC_VALUE_INDICATE);
                    }
                }
            }else if (descr_value == 0x0000){
                if( g_ble_hal_ctx.handle_nc.descr_handle == param->write.handle){
                    QM_LOGI(GATTS_TAG, "notify disable ");
                    if (ble_ctx->init_param && ble_ctx->init_param->nc.on_ccc_change) {
                        ble_ctx->init_param->nc.on_ccc_change(QMS_CCC_VALUE_NONE);
                    }
                }

                if(g_ble_hal_ctx.handle_ic.descr_handle == param->write.handle ){
                    QM_LOGI(GATTS_TAG, "indicate disable ");
                    if (ble_ctx->init_param && ble_ctx->init_param->ic.on_ccc_change) {
                        ble_ctx->init_param->ic.on_ccc_change(QMS_CCC_VALUE_NONE);
                    }
                }
            }else{
                QM_LOGE(GATTS_TAG, "unknown descr value");
                ESP_LOG_BUFFER_HEX(GATTS_TAG, param->write.value, param->write.len);
            }

            if (param->write.need_rsp) {
                /* If data was successfully appended to prepare buffer
                * only then have it reflected in the response */
                esp_gatt_rsp_t gatt_rsp = {0};
                gatt_rsp.attr_value.len = param->write.len;
                gatt_rsp.attr_value.handle = param->write.handle;
                gatt_rsp.attr_value.offset = param->write.offset;
                gatt_rsp.attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
                if (gatt_rsp.attr_value.len && param->write.value) {
                    memcpy(gatt_rsp.attr_value.value, param->write.value, param->write.len);
                }
                esp_ble_gatts_send_response(gatts_if,
                            param->write.conn_id, param->write.trans_id, ESP_GATT_OK, &gatt_rsp);
            }
            break;
        }
        case ESP_GATTS_EXEC_WRITE_EVT:
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
            exec_write_event_env(&ble_ctx->env, param);
            break;
        case ESP_GATTS_MTU_EVT:
            QM_LOGI(GATTS_TAG, "ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
            break;
        case ESP_GATTS_UNREG_EVT:
            break;
        case ESP_GATTS_CREATE_EVT:
            ble_ctx->service_handle = param->create.service_handle;
            esp_ble_gatts_start_service(ble_ctx->service_handle);

            ble_ctx->handle_rc.char_uuid.len = ESP_UUID_LEN_16;
            ble_ctx->handle_rc.char_uuid.uuid.uuid16 = BLE_UUID_QMS_RC;

            esp_ble_gatts_add_char(ble_ctx->service_handle, &ble_ctx->handle_rc.char_uuid,
                                                            ESP_GATT_PERM_READ,
                                                            ESP_GATT_CHAR_PROP_BIT_READ,
                                                            NULL, NULL);

            ble_ctx->handle_wc.char_uuid.len = ESP_UUID_LEN_16;
            ble_ctx->handle_wc.char_uuid.uuid.uuid16 = BLE_UUID_QMS_WC;

            esp_ble_gatts_add_char(ble_ctx->service_handle, &ble_ctx->handle_wc.char_uuid,
                                                            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                                            ESP_GATT_CHAR_PROP_BIT_WRITE,
                                                            NULL, NULL);

            ble_ctx->handle_wwnrc.char_uuid.len = ESP_UUID_LEN_16;
            ble_ctx->handle_wwnrc.char_uuid.uuid.uuid16 = BLE_UUID_QMS_WWNRC;

            esp_ble_gatts_add_char(ble_ctx->service_handle, &ble_ctx->handle_wwnrc.char_uuid,
                                                            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                                            ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE_NR,
                                                            NULL, NULL);

            ble_ctx->handle_nc.char_uuid.len = ESP_UUID_LEN_16;
            ble_ctx->handle_nc.char_uuid.uuid.uuid16 = BLE_UUID_QMS_NC;

            esp_ble_gatts_add_char(ble_ctx->service_handle, &ble_ctx->handle_nc.char_uuid,
                                                            ESP_GATT_PERM_READ,
                                                            ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                                                            NULL, NULL);

            ble_ctx->handle_nc.descr_uuid.len = ESP_UUID_LEN_16;
            ble_ctx->handle_nc.descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
            esp_ble_gatts_add_char_descr(ble_ctx->service_handle, &ble_ctx->handle_nc.descr_uuid,
                                            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL, NULL);

            ble_ctx->handle_ic.char_uuid.len = ESP_UUID_LEN_16;
            ble_ctx->handle_ic.char_uuid.uuid.uuid16 = BLE_UUID_QMS_IC;

            esp_ble_gatts_add_char(ble_ctx->service_handle, &ble_ctx->handle_ic.char_uuid,
                                                            ESP_GATT_PERM_READ,
                                                            ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_INDICATE,
                                                            NULL, NULL);
            ble_ctx->handle_ic.descr_uuid.len = ESP_UUID_LEN_16;
            ble_ctx->handle_ic.descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
            esp_ble_gatts_add_char_descr(ble_ctx->service_handle, &ble_ctx->handle_ic.descr_uuid,
                                            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL, NULL);
            break;
        case ESP_GATTS_ADD_INCL_SRVC_EVT:
            break;
        case ESP_GATTS_ADD_CHAR_EVT: {
            qm_ble_hal_handle_t *handle = NULL;

            if(ble_ctx->handle_ic.char_uuid.uuid.uuid16 == param->add_char.char_uuid.uuid.uuid16){
               handle = &ble_ctx->handle_ic;
            }else if(ble_ctx->handle_nc.char_uuid.uuid.uuid16 == param->add_char.char_uuid.uuid.uuid16){
                handle = &ble_ctx->handle_nc;
            }else if(ble_ctx->handle_wc.char_uuid.uuid.uuid16 == param->add_char.char_uuid.uuid.uuid16){
                handle = &ble_ctx->handle_wc;
            }else if(ble_ctx->handle_rc.char_uuid.uuid.uuid16 == param->add_char.char_uuid.uuid.uuid16){
                handle = &ble_ctx->handle_rc;
            }else if(ble_ctx->handle_wwnrc.char_uuid.uuid.uuid16 == param->add_char.char_uuid.uuid.uuid16){
                handle = &ble_ctx->handle_wwnrc;
            }

            handle->char_handle = param->add_char.attr_handle;

            break;
        }
        case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        {
            static uint8_t is_handle_nc = 1;

            if(is_handle_nc){
                is_handle_nc = 0;
                ble_ctx->handle_nc.descr_handle = param->add_char_descr.attr_handle;
            }else{
                is_handle_nc = 1;
                ble_ctx->handle_ic.descr_handle = param->add_char_descr.attr_handle;
            }
        }
        break;
        case ESP_GATTS_START_EVT:
            QM_LOGI(GATTS_TAG, "SERVICE_START_EVT, status %d, service_handle %d\n",
                    param->start.status, param->start.service_handle);
        break;
        case ESP_GATTS_CONNECT_EVT: {
            esp_ble_conn_update_params_t conn_params = {0};
            memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            /* For the IOS system, please reference the apple official documents about the ble connection parameters restrictions. */
            conn_params.latency = 0;
            conn_params.max_int = 0x20;    // max_int = 0x20*1.25ms = 40ms
            conn_params.min_int = 0x10;    // min_int = 0x10*1.25ms = 20ms
            conn_params.timeout = 400;    // timeout = 400*10ms = 4000ms
            QM_LOGI(GATTS_TAG, "ESP_GATTS_CONNECT_EVT, conn_id %d, remote %02x:%02x:%02x:%02x:%02x:%02x:",
                    param->connect.conn_id,
                    param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2],
                    param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
            ble_ctx->conn_id = param->connect.conn_id;
            //start sent the update connection parameters to the peer device.
            esp_ble_gap_update_conn_params(&conn_params);
            if (ble_ctx->init_param && ble_ctx->init_param->on_connected) {
                ble_ctx->init_param->on_connected();
            }
            break;
        }
        case ESP_GATTS_DISCONNECT_EVT:
            QM_LOGI(GATTS_TAG, "ESP_GATTS_DISCONNECT_EVT, disconnect reason 0x%x", param->disconnect.reason);
            if (ble_ctx->init_param && ble_ctx->init_param->on_disconnected) {
                ble_ctx->init_param->on_disconnected();
            }
            break;
        case ESP_GATTS_CONF_EVT:

            if(g_ble_hal_ctx.handle_ic.char_handle == param->conf.handle && g_ble_hal_ctx.txdone){
                g_ble_hal_ctx.txdone(QB_SUCCESS);
            }

            if (param->conf.status != ESP_GATT_OK){
                ESP_LOG_BUFFER_HEX(GATTS_TAG, param->conf.value, param->conf.len);
            }
        break;
        default:
        break;
    }
}

/**
 * API to initialize ble stack.
 * @parma[in]  qms_init  Bluetooth stack init parmaters.
 * @return     0 on success, error code if failure.
 */
qms_err_t qm_ble_stack_init(qms_bt_init_t *qms_init)
{
    int ret = QM_EOK;

    g_ble_hal_ctx.init_param = qms_init;
    if(qms_init == NULL){
        return -QM_EINVAL;
    }

    if(esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) {

        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

        esp_bt_controller_init(&bt_cfg);

        esp_bt_controller_enable(ESP_BT_MODE_BLE);

        esp_bluedroid_init();

        esp_bluedroid_enable();
    }

    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret){
        return -QM_ERROR;
    }

    ret = esp_ble_gatts_app_register(PROFILE_APP_ID);
    if (ret){
        ESP_LOGE(GATTS_TAG, "gatts app register error, error code = %x", ret);
        return -QM_ERROR;
    }

    ret = esp_ble_gatt_set_local_mtu(PROFILE_MTU_MAX_SIZE);
    if (ret){
        ESP_LOGE(GATTS_TAG, "set local  MTU failed, error code = %x", ret);
        return -QM_ERROR;
    }

    return QM_EOK;
}

/**
 * API to de-initialize ble stack.
 * @return     0 on success, error code if failure.
 */
qms_err_t qm_ble_stack_deinit(void)
{
    esp_ble_gatts_stop_service(g_ble_hal_ctx.service_handle);

    esp_ble_gatts_delete_service(g_ble_hal_ctx.service_handle);

    esp_ble_gatts_app_unregister(g_ble_hal_ctx.gattc_if);

    if(g_ble_hal_ctx.env.prepare_buf){
        qm_free(g_ble_hal_ctx.env.prepare_buf);
        g_ble_hal_ctx.env.prepare_buf = NULL;
    }

#if !CONFIG_QM_IOT_BLE_CANCEL_DEINIT
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
#endif

    return QM_EOK;
}


/**
 * API to send data via QM's Notify Characteristics.
 * @parma[in]  p_data  data buffer.
 * @parma[in]  length  data length.
 * @return     0 on success, error code if failure.
 */
qms_err_t qm_ble_send_notification(uint8_t *p_data, uint16_t length)
{
    int ret = QM_EOK;
    ret = esp_ble_gatts_send_indicate(g_ble_hal_ctx.gattc_if, g_ble_hal_ctx.conn_id, 
                                      g_ble_hal_ctx.handle_nc.char_handle, length, p_data, false);
    return ret;
}


/**
 * API to send data via QM's Indicate Characteristics.
 * @parma[in]  p_data  data buffer.
 * @parma[in]  length  data length.
 * @parma[in]  txdone  txdone callback.
 * @return     0 on success, erro code if failure.
 */
qms_err_t qm_ble_send_indication(uint8_t *p_data, uint16_t length, void (*txdone)(uint8_t res))
{
    int ret = QM_EOK;
    if(g_ble_hal_ctx.txdone == NULL){
        g_ble_hal_ctx.txdone = txdone;
    }

    ret = esp_ble_gatts_send_indicate(g_ble_hal_ctx.gattc_if, g_ble_hal_ctx.conn_id, 
                                      g_ble_hal_ctx.handle_ic.char_handle, length, p_data, true);
    return ret;
}

/**
 * API to disconnect BLE connection.
 * @param[in]  reason  the reason to disconnect the connection.
 */
void qm_ble_disconnect(uint8_t reason)
{
    esp_ble_gatts_close(g_ble_hal_ctx.gattc_if, g_ble_hal_ctx.conn_id);
}

/**
 * API to start bluetooth advertising.
 * @return     0 on success, erro code if failure.
 */
qms_err_t qm_ble_advertising_start(qms_adv_init_t *adv)
{
    uint8_t flag = 0;
    uint16_t index = 0;
    uint8_t scan_index = 0;
    uint8_t srv[] = {0xB7, 0xFE};
#if CONFIG_BLE_MESH_SUPPORT_BLE_ADV
    esp_ble_mesh_ble_adv_param_t ble_mesh_params = {
        .adv_type = BLE_MESH_ADV_IND,
        .own_addr_type   = BLE_MESH_ADDR_PUBLIC,
        .priority = BLE_MESH_BLE_ADV_PRIO_HIGH,
        .count = 0xFFFF,
    };

    esp_ble_mesh_ble_adv_data_t ble_mesh_adv_data = {0};
    
    if(esp_ble_mesh_node_is_provisioned()){
        ble_mesh_params.interval = 500;
        ble_mesh_params.duration = 500;
        ble_mesh_params.period = 500;
    }else{
        ble_mesh_params.interval = 100;
        ble_mesh_params.duration = 100;
        ble_mesh_params.period = 100;
    }
#else
    qm_ble_adv_params_t adv_params = {
        .adv_int_min = BT_ADV_SCAN_UNIT(50),
        .adv_int_max = BT_ADV_SCAN_UNIT(100),
    };
#endif
    uint32_t malloc_len = adv->vdata.len + 2 + strlen(adv->name.name) + 2 + 3 + 4;

    uint8_t *adv_data = (uint8_t *)qm_malloc(malloc_len);
    if(adv_data == NULL){
        return -QMS_ERR_MEM_FAIL;
    }
    memset(adv_data, 0, malloc_len);

    if (adv->flag & QMS_AD_GENERAL) {
        flag |= ESP_BLE_ADV_FLAG_GEN_DISC;
    }

    if (adv->flag & QMS_AD_NO_BREDR) {
        flag |= ESP_BLE_ADV_FLAG_BREDR_NOT_SPT;
    }

    if(adv->flag & QMS_AD_LIMITED){
        flag |= ESP_BLE_ADV_FLAG_LIMIT_DISC;
    }    

    adv_data[index++] = 2;
    adv_data[index++]=  ESP_BLE_AD_TYPE_FLAG;
    adv_data[index++] = flag;

    adv_data[index++] = sizeof(uint16_t) + 1;
    adv_data[index++] = ESP_BLE_AD_TYPE_16SRV_CMPL; //BT_DATA_UUID16_ALL
    adv_data[index++] = srv[0];
    adv_data[index++] = srv[1];


    adv_data[index++] = strlen(adv->name.name) + 1;
    switch (adv->name.ntype) {
        case QMS_ADV_NAME_SHORT:
            adv_data[index++]  = ESP_BLE_AD_TYPE_NAME_SHORT;
            break;
        case QMS_ADV_NAME_FULL:
            adv_data[index++]  = ESP_BLE_AD_TYPE_NAME_CMPL;
            break;
        default:

        break;
    }
    memcpy(&adv_data[index], adv->name.name, strlen(adv->name.name));
 
    esp_ble_gap_set_device_name(adv->name.name);

#if CONFIG_BLE_MESH_SUPPORT_BLE_ADV
    esp_ble_mesh_set_unprovisioned_device_name(adv->name.name);
    index += strlen(adv->name.name);
    ble_mesh_adv_data.adv_data_len = index;
    memcpy(ble_mesh_adv_data.adv_data, adv_data, index);
#else

    index += strlen(adv->name.name);
    qm_ble_gap_set_adv_data_raw(adv_data, index);
#endif
    scan_index = index;

    if (adv->vdata.len != 0) {
        adv_data[index++]     = adv->vdata.len + 1;
        adv_data[index++] = ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE; // BT_DATA_MANUFACTURER_DATA
        memcpy(&adv_data[index], adv->vdata.data, adv->vdata.len);
    } 
#if CONFIG_BLE_MESH_SUPPORT_BLE_ADV
    ble_mesh_adv_data.scan_rsp_data_len = malloc_len - scan_index;
    memcpy(ble_mesh_adv_data.scan_rsp_data, adv_data + scan_index, malloc_len - scan_index);
    esp_ble_mesh_start_ble_advertising(&ble_mesh_params, &ble_mesh_adv_data);
#else
    esp_ble_gap_config_scan_rsp_data_raw(adv_data + scan_index, malloc_len - scan_index);
    qm_ble_gap_start_advertising(&adv_params);
#endif
    qm_free(adv_data);
    adv_data = NULL;
    return QMS_ERR_SUCCESS;
}



/**
 * API to stop bluetooth advertising.
 * @return     0 on success, erro code if failure.
 */
qms_err_t qm_ble_advertising_stop(void)
{
#if CONFIG_BLE_MESH_SUPPORT_BLE_ADV
    esp_ble_mesh_stop_ble_advertising(0);
#else
    esp_ble_gap_config_scan_rsp_data_raw(NULL, 0);
    qm_ble_gap_stop_advertising();
#endif

    return QM_EOK;
}

/**
 * API to start bluetooth advertising.
 * @parma[out]  mac  the uint8_t[BD_ADDR_LEN] space the save the mac address.
 * @return     0 on success, erro code if failure.
 */
qms_err_t qm_ble_get_mac(uint8_t *mac)
{
    return qm_ble_gap_get_local_addr(mac);
}

#ifdef EN_LONG_MTU
/**
 * API to obtain ble link MTU, if ble stack didn't provide, use DEFAULT_ATT_MTU .
 * @parma[out]  mac  the uint8_t[BD_ADDR_LEN] space the save the mac address.
 * @return     0 on success, erro code if failure.
 */

int qm_ble_get_att_mtu(uint16_t *att_mtu)
{
    *att_mtu = PROFILE_MTU_MAX_SIZE;
    return QM_EOK;
}
#endif
