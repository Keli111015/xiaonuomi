#include "qm_ble_core.h"
#include "qm_ble_transport.h"
#include "qm_ble_export.h"
#include "qm_ble_async.h"
#include "qm_ble_hal_ble.h"
#include "qm_ble_hal_os.h"
#include "qm_ble_bzopt.h"
#include "qm_ble_common.h"
#if QB_ENABLE_OTA
#include "qm_ble_ota.h"
#endif


static qm_ble_dev_status_changed_cb m_status_handler = NULL;
static qm_ble_set_dev_status_cb m_ctrl_handler = NULL;
static qm_ble_get_dev_status_cb m_query_handler = NULL;
static qm_ble_response_cb m_rsp_handler = NULL;
static qm_ble_extinfo_cb m_extinfo_handler = NULL;
#if QB_ENABLE_PRODTST
static qm_ble_prodtst_cb m_prodtst_handler = NULL;
#endif
#if QB_ENABLE_OTA
static qm_ble_ota_cb m_ota_handler = NULL;
#endif

#if QB_ENABLE_COMBO_NET 

#define QM_BLE_SSID_READY                              0x01
#define QM_BLE_PASSWORD_READY                          0x02
#define QM_BLE_BSSID_READY                             0x04
#define QM_BLE_APPTOKEN_READY                          0x08
#define QM_BLE_REGION_ID_READY                         0x10
#define QM_BLE_REGION_MQTT_URL_READY                   0x20
#define QM_BLE_REGION_TYPE_READY                       0x40

#define QM_BLE_BASIC_READY          (QM_BLE_SSID_READY | QM_BLE_PASSWORD_READY)
#define QM_BLE_ALL_READY            (QM_BLE_SSID_READY | QM_BLE_PASSWORD_READY | QM_BLE_BSSID_READY | QM_BLE_APPTOKEN_READY)


#define QM_BLE_UTF8_MAX_SSID                           32
#define QM_BLE_UTF8_MAX_PASSWORD                       64


#define  QM_SSID_ID           0xF5
#define  QM_PWD_ID            0xF4
#define  QM_BSSID_ID          0xF3
#define  QM_TOKEN_ID          0xF2
#define  QM_REGOIN_ID         0xEF
#define  QM_MQTT_URL_ID       0xEE
#define  QM_REGOIN_TYPE_ID    0xED
#define  QM_ENV_TYPE_ID       0xEC

static qm_ble_apinfo_ready_cb m_apinfo_handler;

#endif


struct adv_data_s {
    uint8_t data[MAX_VENDOR_DATA_LEN];
    uint32_t len;
};

static struct adv_data_s user_adv = {{0}, 0};

static void notify_status(qm_ble_event_t event)
{
    if (m_status_handler != NULL) {
        m_status_handler(event);
    }
}

#if QB_ENABLE_COMBO_NET && (QB_VERSION < 3)
static uint32_t apinfo_handler(uint8_t *data, uint32_t length)
{
    uint8_t type_id = 0;
    uint8_t attr_id = 0;
    uint8_t idx = 0;
    uint16_t len = 0;
    qm_ble_apinfo_t apinfo = {0};
    uint8_t ready_flag = 0;
    int32_t err_code  = 0;

    if(data == NULL || length < 2 ){
        err_code = -1;
        goto end;
    }

    while(idx < length){

        if(length - idx < 2){
            err_code = -1;
            goto end;
        }
        
        type_id = data[idx++];
        attr_id = data[idx++];

        switch(attr_id){

            case QM_SSID_ID:

                if(type_id != 11){
                    err_code = -1;
                    goto end;
                }
                if(length - idx <= 3){
                    err_code = -1;
                    goto end;
                }
                len = (data[idx++]<<8);
len += data[idx++];
                if(len > QM_BLE_UTF8_MAX_SSID){
                    err_code = -1;
                    goto end;
                }
                if(length - idx < len){
                    err_code = -1;
                    goto end;
                }
                memcpy(apinfo.ssid, &data[idx], len);
                ready_flag |= QM_BLE_SSID_READY;
                idx += len;

            break;
            case QM_PWD_ID:

                if(type_id != 11){
                    err_code = -1;
                    goto end;
                }
                if(length - idx <= 3){
                    err_code = -1;
                    goto end;
                }

                len = (data[idx++]<<8);
                len += data[idx++];

                if(len > QM_BLE_UTF8_MAX_PASSWORD){
                    err_code = -1;
                    goto end;
                }
                if(length - idx < len){
                    err_code = -1;
                    goto end;
                }
                memcpy(apinfo.pw, &data[idx], len);
                ready_flag |= QM_BLE_PASSWORD_READY;
                idx += len;

            break;
            case QM_BSSID_ID:
            
                if(type_id != 14){
                    err_code = -1;
                    goto end;
                }
                if(length - idx < 6){
                    err_code = -1;
                    goto end;
                }
                len = (data[idx++]<<8);
                len += data[idx++];
                if(len != 6){
                    err_code = -1;
                    goto end;
                }
                memcpy(apinfo.bssid, &data[idx], 6);
                ready_flag |= QM_BLE_BSSID_READY;
                idx += 6;

            break;
            case QM_TOKEN_ID:
                if(type_id != 14){
                    err_code = -1;
                    goto end;
                }
                if(length - idx < 3){
                    err_code = -1;
                    goto end;
                }

                len = (data[idx++]<<8);
                len += data[idx++];
                if(len > MAX_QM_TOKEN_PARAM_LEN){
                    err_code = -1;
                    goto end;
                }

                apinfo.apptoken_len = len;
                memcpy(apinfo.apptoken, &data[idx], len);
                ready_flag |= QM_BLE_APPTOKEN_READY;
                idx += len;

            break;
            case QM_REGOIN_ID:
                if(type_id != 2){
                    err_code = -1;
                    goto end;
                }
                if(length - idx < 1){
                    err_code = -1;
                    goto end;
                }
                apinfo.region_id = data[idx];
                ready_flag |= QM_BLE_REGION_ID_READY;
                idx += 1;

            break;
            case QM_MQTT_URL_ID:
                if(type_id != 11){
                    err_code = -1;
                    goto end;
                }
                if(length - idx <= 3){
                    err_code = -1;
                    goto end;
                }
                len = (data[idx++]<<8);
                len += data[idx++];
                if(len > MAX_QM_URL_LEN){
                    err_code = -1;
                    goto end;
                }
                if(length - idx < len){
                    err_code = -1;
                    goto end;
                }
                memcpy(apinfo.region_url, &data[idx], len);
                ready_flag |= QM_BLE_REGION_MQTT_URL_READY;
                idx += len;

            break;
            case QM_REGOIN_TYPE_ID:
                if(type_id != 2){
                    err_code = -1;
                    goto end;
                }
                if(length - idx < 1){
                    err_code = -1;
                    goto end;
                }
                apinfo.region_type = data[idx];
                ready_flag |= QM_BLE_REGION_TYPE_READY;
                idx += 1;
            break;
            case QM_ENV_TYPE_ID:
                if(type_id != 2){
                    err_code = -1;
                    goto end;
                }
                if(length - idx < 1){
                    err_code = -1;
                    goto end;
                }
                apinfo.env_type = data[idx];
                apinfo.env_type_valid = 1;
                idx += 1;
            break;
            default:
                err_code = -2;
                goto end;
            break;
        }
    }

end:

    if (ready_flag & QM_BLE_BASIC_READY) {
        qm_ble_core_event_notify(QB_EVENT_APINFO, (uint8_t*)&apinfo, sizeof(apinfo));
    }

    return err_code;
}
#endif

static void rx_handler(qm_ble_rx_cmd_post_t *rx_cmd)
{
    uint8_t code =0 ;
    uint8_t cmd = rx_cmd ->cmd;
#if QB_ENABLE_COMBO_NET           
    int ret = 0;
#endif

    switch(cmd){
        case QM_BLE_CMD_SET:
            code = QB_SUCCESS;
            if (m_ctrl_handler != NULL) {
                m_ctrl_handler (rx_cmd->msg_id, cmd, rx_cmd->p_rx_buf, rx_cmd->buf_sz);
            }

#if QB_ENABLE_POST_ASYNC
            qm_ble_ack_async(rx_cmd->msg_id, cmd, code, NULL, 0);
#endif
            break;
        case QM_BLE_CMD_REPORT:

            if(m_rsp_handler != NULL){
                m_rsp_handler(rx_cmd->msg_id, cmd, rx_cmd->p_rx_buf, rx_cmd->buf_sz);
            }
            break;

        case QM_BLE_CMD_GET:
        case QM_BLE_CMD_GET_SNAPSHOT:
            if (m_query_handler != NULL) {
                m_query_handler(rx_cmd->msg_id, cmd, rx_cmd->p_rx_buf, rx_cmd->buf_sz);
            }
            break;
#if QB_ENABLE_COMBO_NET           
        case QM_BLE_CMD_APINFO:
        #if (QB_VERSION < 3)
            ret = apinfo_handler(rx_cmd->p_rx_buf, rx_cmd->buf_sz);
        #else
            if(m_apinfo_handler != NULL){
                m_apinfo_handler(rx_cmd->p_rx_buf, rx_cmd->buf_sz);
            }
        #endif
            if(ret == 0){
#if QB_ENABLE_POST_ASYNC
                qm_ble_ack_async(rx_cmd->msg_id, cmd, 0, NULL, 0);
#endif
            }else{
#if QB_ENABLE_POST_ASYNC
                qm_ble_ack_async(rx_cmd->msg_id, cmd, 4, NULL, 0);
#endif
            }
            

        break;
#endif

        case QM_BLE_CMD_INIT:
        case QM_BLE_CMD_ENTER_BACKGROUD:
        case QM_BLE_CMD_BIND_NOTIFY:
            if(m_extinfo_handler != NULL){
                m_extinfo_handler(rx_cmd->msg_id, cmd, rx_cmd->p_rx_buf, rx_cmd->buf_sz);
            }
            break;
            
#if QB_ENABLE_OTA
        case QM_BLE_CMD_OTA_QUERY_VER:
        case QM_BLE_CMD_OTA_REQUEST:
        case QM_BLE_CMD_OTA_DATA:
        case QM_BLE_CMD_OTA_VERIFY:
            
            qm_ble_ota_dispatcher(rx_cmd->msg_id, cmd, rx_cmd->p_rx_buf, rx_cmd->buf_sz);    
                                  
        break;
#endif
#if QB_ENABLE_PRODTST

    case QM_BLE_CMD_PRODTST:

        if(m_prodtst_handler != NULL){
            m_prodtst_handler(rx_cmd->msg_id, rx_cmd->p_rx_buf, rx_cmd->buf_sz);
        }

    break;
#endif
    default:
        break;

    }
}

static void event_handler(qm_ble_event_info_t *p_event)
{
    switch (p_event->type) {
        case QB_EVENT_CONNECTED:
            notify_status(QM_BLE_CONNECTED);

#if QB_ENABLE_OTA
            qm_ble_ota_relate_event(p_event->type);
#endif
    
            break;

        case QB_EVENT_DISCONNECTED:
            qm_ble_core_reset();
            notify_status(QM_BLE_DISCONNECTED);
        
#if QB_ENABLE_POST_ASYNC
            qm_ble_async_clear();
#endif

#if QB_ENABLE_OTA
            qm_ble_ota_relate_event(p_event->type);
#endif

            break;

        case QB_EVENT_AUTHENTICATED:
            notify_status(QM_BLE_AUTHENTICATED);
            
#if QB_ENABLE_OTA
            qm_ble_ota_relate_event(p_event->type);
#endif
            
            break;

        case QB_EVENT_TX_DONE:
            
             notify_status(QM_BLE_TX_DONE);
#if QB_ENABLE_POST_ASYNC
            qm_ble_async_done();
            qm_ble_async_post_internal();
#endif
        
            break;

         case QB_EVENT_TX_DONE_TIMEOUT:

#if QB_ENABLE_POST_ASYNC
            qm_ble_async_done();
            qm_ble_async_post_internal();
#endif
            
            break;

        case QB_EVENT_RX_INFO:
            if(p_event->rx_data.p_data != NULL){
                qm_ble_rx_cmd_post_t *r_cmd  = (qm_ble_rx_cmd_post_t*) p_event->rx_data.p_data;
                rx_handler(r_cmd);
            }
            break;
        case QB_EVENT_APINFO:
#if QB_ENABLE_COMBO_NET && (QB_VERSION < 3)
            if(m_apinfo_handler != NULL){
                m_apinfo_handler((qm_ble_apinfo_t*)p_event->rx_data.p_data);
            }
#endif
            break;
       
        default:
            break;
    }
    
}



uint32_t qm_ble_init(qm_ble_dev_info_t *info, 
                   qm_ble_dev_status_changed_cb status_change_cb,
                   qm_ble_set_dev_status_cb set_cb,
                   qm_ble_get_dev_status_cb get_cb,
                   qm_ble_response_cb rsp_cb,
                   qm_ble_extinfo_cb ext_cb,
                   qm_ble_apinfo_ready_cb apinfo_rx_cb,
                   qm_ble_ota_cb ota_cb)
{

    qm_ble_dev_conf_t init;

#if QB_ENABLE_OTA
    qm_ble_ota_conf_t ota;
    memset(&ota, 0, sizeof(qm_ble_ota_conf_t));
#endif
    
    memset(&init, 0, sizeof(qm_ble_dev_conf_t));
    init.status_changed_cb = status_change_cb;
    init.set_cb            = set_cb;
    init.get_cb            = get_cb;
    init.rsp_cb            = rsp_cb;
    init.ext_cb            = ext_cb;
    init.apinfo_cb         = apinfo_rx_cb;

    init.ota_cb            = ota_cb;

    init.product_id = info->product_id;
    
    if (info->dev_adv_mac != NULL) {
        memcpy(init.bd_adv_addr, info->dev_adv_mac, BD_ADDR_LEN);
    }

    /* device id may be NULL */
    if (info->device_id != NULL) {
        if(strlen(info->device_id) >= STR_DID_LEN){
            return QB_EDATASIZE;
        }
        init.device_id_len = strlen(info->device_id);
        memcpy(init.device_id, info->device_id, init.device_id_len);
    } else {
        init.device_id_len = 0;
    }

    /* device secret may be NULL */
    if (info->device_secret != NULL && strlen(info->device_secret) < STR_DEV_SEC_LEN) {
        init.device_secret_len = strlen(info->device_secret);
        memcpy(init.device_secret, info->device_secret, init.device_secret_len);
    } else {
        init.device_secret_len = 0;
    }

    /* product secret may be NULL */
    if (info->product_secret != NULL && strlen(info->product_secret) < STR_PROD_SEC_LEN) {
        init.product_secret_len = strlen(info->product_secret);
        memcpy(init.product_secret, info->product_secret, init.product_secret_len);
    } else {
        init.product_secret_len = 0;
    }

#if QB_ENABLE_OTA
    memcpy(&init.ble_ver, &info->ble_ver, sizeof(info->ble_ver));
    memcpy(&init.mcu_ver, &info->mcu_ver, sizeof(info->mcu_ver));
#endif

#if QB_ENABLE_POST_ASYNC
    if(qm_ble_async_init() != 0){
        QM_BLE_LOGE("qm ble async init failed\r\n");
    }
#endif  

    if(qm_ble_start(&init) != 0) {
        QM_BLE_LOGE("qm ble start failed\r\n");
        return QB_EINIT;
    }
    return QB_SUCCESS;
}


uint32_t qm_ble_start(qm_ble_dev_conf_t *dev_conf)
{
    uint32_t err_code;
    qm_ble_init_t init;

#if QB_ENABLE_OTA
    qm_ble_ota_conf_t ota;
    memset(&ota, 0, sizeof(qm_ble_ota_conf_t));
#endif

    if ((dev_conf == NULL) || (dev_conf->status_changed_cb == NULL) ||
        (dev_conf->set_cb == NULL) || (dev_conf->get_cb == NULL) || (dev_conf->rsp_cb == NULL)) {
        return QB_EINVALIDPARAM;
    }
        
    m_status_handler = dev_conf->status_changed_cb;
    m_ctrl_handler   = dev_conf->set_cb;
    m_query_handler  = dev_conf->get_cb;
    m_rsp_handler    = dev_conf->rsp_cb;
    m_extinfo_handler= dev_conf->ext_cb;
#if QB_ENABLE_OTA
    m_ota_handler    = dev_conf->ota_cb;
#endif
    
#if QB_ENABLE_COMBO_NET
    if (dev_conf->apinfo_cb != NULL) {
        m_apinfo_handler = dev_conf->apinfo_cb;
    } else {
        return QB_EINVALIDPARAM;
    }
#endif

    memset(&init, 0, sizeof(qm_ble_init_t));
    init.event_handler         = event_handler;
    init.product_id              = dev_conf->product_id;
    init.product_secret.p_data = (uint8_t *)dev_conf->product_secret;
    init.product_secret.length = dev_conf->product_secret_len;
    init.device_id.p_data    = (uint8_t *)dev_conf->device_id;
    init.device_id.length    = dev_conf->device_id_len;
    init.device_secret.p_data  = (uint8_t *)dev_conf->device_secret;
    init.device_secret.length  = dev_conf->device_secret_len;
    init.adv_mac               = dev_conf->bd_adv_addr;
    init.transport_timeout     = QB_TRANSPORT_TIMEOUT;
    init.max_mtu               = QB_MAX_SUPPORTED_MTU;
    init.user_adv_data         = user_adv.data;
    init.user_adv_len          = user_adv.len;

#if QB_ENABLE_OTA
    memcpy(&ota.ble_ver, &dev_conf->ble_ver, sizeof(ota.ble_ver));
    memcpy(&ota.mcu_ver, &dev_conf->mcu_ver, sizeof(ota.ble_ver));
    ota.ota_cb = dev_conf->ota_cb;
    if(qm_ble_ota_init(&ota) != 0){
        QM_BLE_LOGE("qm ble ota init failed\r\n");
    }
#endif

    err_code = qm_ble_core_init(&init);
    return ((err_code == QB_SUCCESS) ? 0 : -1);
}

uint32_t qm_ble_end(void)
{
    int ret = 0;

    if (qm_ble_stack_deinit() != QMS_ERR_SUCCESS) {
        ret = -1;
    }

    qm_ble_async_deinit();
    qm_ble_transport_deinit();
#if QB_ENABLE_AUTH
    qm_ble_auth_deinit();
#endif
    return ret;
}


uint32_t qm_ble_post(uint8_t cmd, uint8_t *msg_id, uint8_t *buffer, uint32_t length)
{
    QM_BLE_LOGD(QM_BLE_LOG_UPLINK, "ble post indication");
    if (length == 0 || length > QB_MAX_PAYLOAD_SIZE) {
        return QB_EDATASIZE;
    }

    return qm_ble_transport_tx(QB_TX_INDICATION, msg_id, cmd, buffer, length);
}

uint32_t qm_ble_post_fast(uint8_t cmd, uint8_t *msg_id, uint8_t *buffer, uint32_t length)
{
    QM_BLE_LOGD(QM_BLE_LOG_UPLINK, "ble post notification");
    if (length == 0 || length > QB_MAX_PAYLOAD_SIZE) {
        return QB_EDATASIZE;
    }
    
    return qm_ble_transport_tx(QB_TX_NOTIFICATION, msg_id, cmd, buffer, length);
}

void qm_ble_append_adv_data(uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0 || len > MAX_VENDOR_DATA_LEN) {
        QM_BLE_LOGE("invalid adv data");
        return;
    }

    memcpy(user_adv.data, data, len);
    user_adv.len = len;
}

void qm_ble_restart_advertising()
{
    qms_err_t err;
    uint32_t size;
    char *m_name = ble_local_name_get();

    qms_adv_init_t adv_data = {
        .flag = QMS_AD_GENERAL | QMS_AD_NO_BREDR,
        .name = { .ntype = QMS_ADV_NAME_FULL, .name = m_name },
    };

    err = qm_ble_advertising_stop();
    if (err != QMS_ERR_SUCCESS) {
        QM_BLE_LOGE("Failed to stop previous adv");
        return;
    }

    adv_data.vdata.len = sizeof(adv_data.vdata.data);
    err = qm_ble_core_get_bz_adv_data(adv_data.vdata.data, &(adv_data.vdata.len));
    if (err) {
        QM_BLE_LOGE("%s %d fail", __func__, __LINE__);
        return;
    }

    if (user_adv.len > 0) {
        size = sizeof(adv_data.vdata.data) - adv_data.vdata.len;
        if (size < user_adv.len) {
            QM_BLE_LOGE("no space for user adv data (expected %d but"
                   " only %d left)", user_adv.len, size);
        } else {
            memcpy(adv_data.vdata.data + adv_data.vdata.len,
                   user_adv.data, user_adv.len);
            adv_data.vdata.len += user_adv.len;
        }
    }

    if (qm_ble_advertising_start(&adv_data) != QMS_ERR_SUCCESS) {
        QM_BLE_LOGE("%s %d adv fail", __func__, __LINE__);
        return;
    }
}

uint32_t qm_ble_start_advertising(uint8_t sub_type, uint8_t sec_type, uint8_t bind_state, char *local_name)
{
    uint32_t size;
    char m_name[QB_MAX_LOCAL_NAME_LEN+1]={0};
    qms_adv_init_t adv_data = {
        .flag = QMS_AD_GENERAL | QMS_AD_NO_BREDR,
        .name = { .ntype = QMS_ADV_NAME_FULL, .name = m_name },
    };
        
    if(local_name){
        if(strlen(local_name) > QB_MAX_LOCAL_NAME_LEN){
            return QB_EINVALIDLEN;
        }else{
            memcpy(m_name, local_name, strlen(local_name));
        }

    }else{
        memcpy(m_name, QB_DEFAULT_LOCAL_NAME, strlen(QB_DEFAULT_LOCAL_NAME));
    }

    ble_local_name_set(m_name);
    
    qm_ble_core_create_bz_adv_data(sub_type, sec_type, bind_state);

    adv_data.vdata.len = sizeof(adv_data.vdata.data);
    if (qm_ble_core_get_bz_adv_data(adv_data.vdata.data, &(adv_data.vdata.len))) {
        QM_BLE_LOGE("%s %d fail", __func__, __LINE__);
        return -1;
    }

    /* append user adv data if any. */
    if (user_adv.len > 0) {
        size = sizeof(adv_data.vdata.data) - adv_data.vdata.len;
        if (size < user_adv.len) {
            QM_BLE_LOGE("no space for user adv data (expected %d but"
                   " only %d left)", user_adv.len, size);
        } else {
            memcpy(adv_data.vdata.data + adv_data.vdata.len,
                   user_adv.data, user_adv.len);
            adv_data.vdata.len += user_adv.len;
        }
    }

    if (qm_ble_advertising_start(&adv_data) != QMS_ERR_SUCCESS) {
        QM_BLE_LOGE("%s %d adv fail", __func__, __LINE__);
        return -1;
    }
    return 0;
}

uint32_t qm_ble_stop_advertising(void)
{
    if (qm_ble_advertising_stop() != QMS_ERR_SUCCESS) {
        QM_BLE_LOGE("stop adv fail");
        return -1;
    }
    return 0;
}

void qm_ble_disconnect_ble(void)
{
    qm_ble_disconnect(QMS_BT_REASON_REMOTE_USER_TERM_CONN);
}

uint8_t qm_ble_get_bind_state(void)
{
    return 0;
}

uint32_t qm_ble_clear_bind_info(void)
{
    return 0;
}

#if QB_ENABLE_PRODTST

uint32_t qm_ble_prodtst_register_callback(qm_ble_prodtst_cb prodtst_cb)
{
    m_prodtst_handler = prodtst_cb;
    return QMS_ERR_SUCCESS;
}

uint32_t qm_ble_prodtst_post(uint8_t msg_id, uint8_t *buffer, uint32_t length)
{
    return qm_ble_ack_async_fast(msg_id, QM_BLE_CMD_PRODTST, QM_BLE_COM_NO_ERR, buffer, length);
}

#endif
