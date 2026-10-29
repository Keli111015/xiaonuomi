#include "qm.h"
#include "qm_ble_gap.h"
#include "qm_ble_common.h"
#include "qm_ble_hal_ble.h"
#include "qm_ble_hal_os.h"

#include "ls_ble.h"

#define LOG_TAG "qm_ble_hal"

#define BT_ADV_SCAN_UNIT(_ms) ((_ms) * 8 / 5)


enum svc_att_db_handles
{
    QM_BLE_SVC_IDX_RC_CHAR,               // 特征值申明
    QM_BLE_SVC_IDX_RC_VAL,                // Read 特征值

    QM_BLE_SVC_IDX_WC_CHAR,               // 特征值申明
    QM_BLE_SVC_IDX_WC_VAL,                // Write 特征值

    QM_BLE_SVC_IDX_IC_CHAR,               // 特征值申明
    QM_BLE_SVC_IDX_IC_VAL,                // Write 特征值
    QM_BLE_SVC_IDX_IC_IND_CFG,            // 特征值描述符

    QM_BLE_SVC_IDX_WWNRC_CHAR,            // 特征值申明
    QM_BLE_SVC_IDX_WWNRC_VAL,             // WriteWithNoRsp特征值

    QM_BLE_SVC_IDX_NC_CHAR,               // 特征值申明
    QM_BLE_SVC_IDX_NC_VAL,                // Notify 特征值
    QM_BLE_SVC_IDX_NC_NOTIFY_CFG,         // 特征值描述符
    
    /* Add more entry here if necessary */
    QM_BLE_SVC_ATT_NUM                    // 
};

typedef struct 
{
    uint16_t mtu_size;
    uint8_t connect_id;
    uint8_t server_ntf_done;

    uint16_t ntf_cccd_config;
    uint16_t ind_cccd_config;

    struct svc_decl server_svc;
    struct gatt_svc_env server_svc_env;

    void (*txdone)(uint8_t res);

    on_char_read_t rc_read;
    on_char_write_t wc_write;
    on_char_ccc_change_t ic_ccc_change;
    on_char_write_t wwnrc_write;
    on_char_ccc_change_t nc_ccc_change;
    connected_callback_t on_connected;
    disconnected_callback_t on_disconnected;
}qm_ble_hal_ctx_t;

extern char *ble_local_name_get(void);

static uint8_t adv_obj_hdl;
static qm_ble_hal_ctx_t g_ble_hal_ctx = {0};
static const uint8_t att_decl_char_array[] = {0x03,0x28}; 
static const uint8_t att_desc_client_char_cfg_array[] = {0x02,0x29}; 

static struct att_decl server_att_decl[QM_BLE_SVC_ATT_NUM] = {
    [QM_BLE_SVC_IDX_RC_CHAR] = {                      // 特征值申明
        .uuid = att_decl_char_array,                // attribute uuid，特征值申明是固定的16-bit uuid
        .s.max_len = 0,                             // 当前属性值支持的通信的最大的字节数
        .s.uuid_len = UUID_LEN_16BIT,               // UUID 的长度,常用的 16-BIT 和 128-BIT
        .s.read_indication = 1,                     // 默认为 1
        .char_prop.rd_en = 1,                       // 支持读属性
    },

    [QM_BLE_SVC_IDX_WC_CHAR] = {                      // 特征值申明
        .uuid = att_decl_char_array,                // attribute uuid，特征值申明是固定的16-bit uuid
        .s.max_len = 0,                             // 当前属性值支持的通信的最大的字节数
        .s.uuid_len = UUID_LEN_16BIT,               // UUID 的长度,常用的 16-BIT 和 128-BIT
        .s.read_indication = 1,                     // 默认为 1
        .char_prop.rd_en = 1,                       // 支持读属性
    },

    [QM_BLE_SVC_IDX_IC_CHAR] = {                      // 特征值申明
        .uuid = att_decl_char_array,                // attribute uuid，特征值申明是固定的16-bit uuid
        .s.max_len = 0,                             // 当前属性值支持的通信的最大的字节数
        .s.uuid_len = UUID_LEN_16BIT,               // UUID 的长度,常用的 16-BIT 和 128-BIT
        .s.read_indication = 1,                     // 默认为 1
        .char_prop.rd_en = 1,                       // 支持读属性
    },

    [QM_BLE_SVC_IDX_IC_IND_CFG] = {                      // 特征值申明
        .uuid = att_desc_client_char_cfg_array,                // attribute uuid，特征值申明是固定的16-bit uuid
        .s.max_len = 0,                             // 当前属性值支持的通信的最大的字节数
        .s.uuid_len = UUID_LEN_16BIT,               // UUID 的长度,常用的 16-BIT 和 128-BIT
        .s.read_indication = 1,                     // 默认为 1
        .char_prop.rd_en = 1,                       // 支持读属性
        .char_prop.wr_req = 1,
    },

    [QM_BLE_SVC_IDX_WWNRC_CHAR] = {                      // 特征值申明
        .uuid = att_decl_char_array,                // attribute uuid，特征值申明是固定的16-bit uuid
        .s.max_len = 0,                             // 当前属性值支持的通信的最大的字节数
        .s.uuid_len = UUID_LEN_16BIT,               // UUID 的长度,常用的 16-BIT 和 128-BIT
        .s.read_indication = 1,                     // 默认为 1
        .char_prop.rd_en = 1,                       // 支持读属性
    },

    [QM_BLE_SVC_IDX_NC_CHAR] = {                      // 特征值申明
        .uuid = att_decl_char_array,                // attribute uuid，特征值申明是固定的16-bit uuid
        .s.max_len = 0,                             // 当前属性值支持的通信的最大的字节数
        .s.uuid_len = UUID_LEN_16BIT,               // UUID 的长度,常用的 16-BIT 和 128-BIT
        .s.read_indication = 1,                     // 默认为 1
        .char_prop.rd_en = 1,                       // 支持读属性
    },

    [QM_BLE_SVC_IDX_NC_NOTIFY_CFG] = {                      // 特征值申明
        .uuid = att_desc_client_char_cfg_array,                // attribute uuid，特征值申明是固定的16-bit uuid
        .s.max_len = 0,                             // 当前属性值支持的通信的最大的字节数
        .s.uuid_len = UUID_LEN_16BIT,               // UUID 的长度,常用的 16-BIT 和 128-BIT
        .s.read_indication = 1,                     // 默认为 1
        .char_prop.rd_en = 1,                       // 支持读属性
        .char_prop.wr_req = 1,
    },
};


void create_adv_obj(void)
{
    struct legacy_adv_obj_param adv_param = {
        .adv_intv_min =  BT_ADV_SCAN_UNIT(100),
        .adv_intv_max = BT_ADV_SCAN_UNIT(100),
        .own_addr_type = PUBLIC_OR_RANDOM_STATIC_ADDR,
        .filter_policy = 0,
        .ch_map = 0x7,
        .disc_mode = ADV_MODE_GEN_DISC,
        .prop = {
            .connectable = 1,
            .scannable = 1,
            .directed = 0,
            .high_duty_cycle = 0,
        },
    };
    
    dev_manager_create_legacy_adv_object(&adv_param);

    dev_manager_create_scan_object(PUBLIC_OR_RANDOM_STATIC_ADDR);
}

static void dev_manager_callback(enum dev_evt_type type,union dev_evt_u *evt)
{
    switch(type)
    {

        case STACK_READY:
        {
            dev_manager_add_service(&g_ble_hal_ctx.server_svc);
        }
        break;

        case SERVICE_ADDED:
        {
            gatt_manager_svc_register(evt->service_added.start_hdl, QM_BLE_SVC_ATT_NUM, &g_ble_hal_ctx.server_svc_env);
        }
        break;

        case ADV_OBJ_CREATED:
        {
            adv_obj_hdl = evt->obj_created.handle;
        }
        break;

        default:
        break;
    }
}

static void dis_server_get_dev_name(struct gap_dev_info_dev_name *dev_name_ptr, uint8_t con_idx)
{
    dev_name_ptr->value = (uint8_t*)ble_local_name_get();
    dev_name_ptr->length = strlen(ble_local_name_get());
}

/*
*   GAP 事件处理函数； 处理连接、断连、配对绑定和连接参数更新等事件；
*   参数说明：
*            enum gap_evt_type type: 当前的 GAP 事件类型；包括 CONNECTED、DISCONNECTED、MATER_PAIR_REQ等。
*            union gap_evt_u *evt: 该GAP事件对应的参数。例如DISCONNECTED事件，断连事件可以通过 evt->disconnected.reason 打印出断连的原因。
*            uint8_t con_idx: GAP事件对应的 connect id。多连接情况下，可以通过这个 id 来区分现在是和哪个Client 主机通信。
*/
static void gap_manager_callback(enum gap_evt_type type,union gap_evt_u *evt,uint8_t con_idx)
{
    switch(type)
    {
        case GET_DEV_INFO_DEV_NAME:
            dis_server_get_dev_name((struct gap_dev_info_dev_name *)evt, con_idx);
        break;

        case CONNECTED:             // CONNECTED 连接建立事件
            QM_LOGD(LOG_TAG, "connected!");
            g_ble_hal_ctx.connect_id = con_idx;   // 记录当前连接的 CONNECT_ID
            g_ble_hal_ctx.on_connected();
        break;
        
        case DISCONNECTED:
            QM_LOGD(LOG_TAG,"disconnected! reason = %x ",evt->disconnected.reason);       // DISCONNECT 断连事件，打印断连原因
            g_ble_hal_ctx.connect_id = 0xff; 
            g_ble_hal_ctx.on_disconnected();
        break;

        case CONN_PARAM_REQ:

        break;
        case CONN_PARAM_UPDATED:        // 连接参数已经更新事件，打印当前的连接参数
            QM_LOGD(LOG_TAG,"conn param updata  interval = %d  latency = %d  sup_to = %d",evt->conn_param_updated.con_interval,evt->conn_param_updated.con_latency,evt->conn_param_updated.sup_to);
        break;
        default:

        break;
    }
}


static void server_read_req_ind(uint8_t att_idx, uint8_t con_idx)
{
    int len = 6;
    uint8_t read_buff[6] = {0};
    uint16_t handle = 0;
    switch (att_idx)
    {
        case QM_BLE_SVC_IDX_IC_IND_CFG:
            len = 2;
            memcpy(read_buff, &g_ble_hal_ctx.ind_cccd_config, 2);
        break;

        case QM_BLE_SVC_IDX_NC_NOTIFY_CFG:
            len = 2;
            memcpy(read_buff, &g_ble_hal_ctx.ntf_cccd_config, 2);
        break;

        case QM_BLE_SVC_IDX_RC_VAL:
            g_ble_hal_ctx.rc_read(read_buff, len);
        break;

        default:

        break;
    }

    handle = gatt_manager_get_svc_att_handle(&g_ble_hal_ctx.server_svc_env, att_idx);
    gatt_manager_server_read_req_reply(con_idx, handle, 0, (void*)read_buff, len);
}

static void server_write_req_ind(uint8_t att_idx, uint8_t con_idx, uint16_t length, uint8_t const *value)
{
    switch (att_idx)
    {
        case QM_BLE_SVC_IDX_IC_IND_CFG:
            memcpy(&g_ble_hal_ctx.ind_cccd_config, value, length);
            g_ble_hal_ctx.ic_ccc_change(g_ble_hal_ctx.ind_cccd_config); //todo ,need modify
        break;

        case QM_BLE_SVC_IDX_NC_NOTIFY_CFG:
            memcpy(&g_ble_hal_ctx.ntf_cccd_config, value, length);
            g_ble_hal_ctx.nc_ccc_change(g_ble_hal_ctx.ntf_cccd_config); //todo ,need modify
        break;

        case QM_BLE_SVC_IDX_WC_VAL:
            g_ble_hal_ctx.wc_write((void *)value, length);
        break;

        case QM_BLE_SVC_IDX_WWNRC_VAL:
            g_ble_hal_ctx.wwnrc_write((void *)value, length);
        break;

        default:

        break;
    }
}

/*
*   GATT 事件处理函数。
*   参数说明：
*            enum gatt_evt_type type: 当前的 GATT 事件类型；包括 GATT Read、Write、Notification done、MTU Exchange等。
*            union gatt_evt_u *evt: 该事件对应的参数。例如SERVER_WRITE_REQ事件，APP下发的数据就在 evt->server_write_req.value 中，也包括这次write事件对应的服务信息等。
*            uint8_t con_idx: GATT事件对应的 connect id。多连接情况下，可以通过这个 id 来区分现在是和哪个Client 主机通信。
*/
static void gatt_manager_callback(enum gatt_evt_type type,union gatt_evt_u *evt,uint8_t con_idx)
{
    if(g_ble_hal_ctx.connect_id == 0xff){
        QM_LOGD(LOG_TAG,"receive gatt msg when disconnected!");  
        return ;  
    }

    switch (type)
    {
        case SERVER_READ_REQ:       // Read:APP读取Server端的数据
            server_read_req_ind(evt->server_read_req.att_idx, con_idx);
        break;

        case SERVER_WRITE_REQ:      // Write:APP写数据到Server端
        {
            server_write_req_ind(evt->server_write_req.att_idx, con_idx, evt->server_write_req.length, evt->server_write_req.value);
        }
        break;
        case SERVER_NOTIFICATION_DONE:      // NOTIFICATION_DONE,该事件意味当前的Notificate数据包已经交给协议栈处理，可以继续发下一包Notificate数据
            QM_LOGD(LOG_TAG,"notify done");
            g_ble_hal_ctx.server_ntf_done = QM_TRUE;
        break;
        case SERVER_INDICATION_DONE:      // NOTIFICATION_DONE,该事件意味当前的Notificate数据包已经交给协议栈处理，可以继续发下一包Notificate数据
            QM_LOGD(LOG_TAG,"ind done");
            if(g_ble_hal_ctx.txdone){
                g_ble_hal_ctx.txdone(QB_SUCCESS);
            }
        break;
        case MTU_CHANGED_INDICATION:        // MTU Exchange 已经完成指示事件
        {    
            QM_LOGD(LOG_TAG,"mtu: %d", evt->mtu_changed_ind.mtu);     // 打印当前的 MTU
            g_ble_hal_ctx.mtu_size = evt->mtu_changed_ind.mtu;
        }
        break;
        default:
            QM_LOGD(LOG_TAG,"Event not handled! type = %d", type);
        break;
    }
}

static void qm_gap_ble_callback(qm_gap_ble_cb_event_t event, qm_ble_gap_cb_param_t *param)
{
    switch (event)
    {
        case QM_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        case QM_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            
        break;

        default:
        
        break;
    }
}

qms_err_t qm_ble_stack_deinit(void)
{
    qm_ble_gap_unregister_callback(qm_gap_ble_callback);
    dev_manager_cb_unregister(dev_manager_callback);
    return QM_EOK;
}

/**
 * API to initialize ble stack.
 * @parma[in]  qms_init  Bluetooth stack init parmaters.
 * @return     0 on success, error code if failure.
 */
qms_err_t qm_ble_stack_init(qms_bt_init_t *qms_init)
{
    qms_uuid_16_t *uuid_16 = NULL;
    struct att_decl *decl_handle = NULL;
    if(qms_init == NULL){
        return QMS_ERR_STACK_FAIL;
    }
    g_ble_hal_ctx.server_ntf_done = QM_TRUE;
    g_ble_hal_ctx.on_connected = qms_init->on_connected;
    g_ble_hal_ctx.on_disconnected = qms_init->on_disconnected;

    uuid_16 = (qms_uuid_16_t *)qms_init->uuid_svc;
    g_ble_hal_ctx.server_svc.att = server_att_decl;
    g_ble_hal_ctx.server_svc.nb_att = QM_BLE_SVC_ATT_NUM;
    g_ble_hal_ctx.server_svc.uuid = (const uint8_t *)&uuid_16->val;

     /* rc */
    g_ble_hal_ctx.rc_read = qms_init->rc.on_read;

    uuid_16 = (qms_uuid_16_t *)qms_init->rc.uuid;
    decl_handle = &server_att_decl[QM_BLE_SVC_IDX_RC_VAL];
    decl_handle->uuid = (const uint8_t *)&uuid_16->val;
    decl_handle->s.uuid_len = UUID_LEN_16BIT;
    decl_handle->s.max_len = QB_GATT_MTU_SIZE_MAX;
    decl_handle->s.read_indication = 1;
    decl_handle->char_prop.rd_en = 1;
    
    /* wc */
    g_ble_hal_ctx.wc_write = qms_init->wc.on_write;
    uuid_16 = (qms_uuid_16_t *)qms_init->wc.uuid;
    decl_handle = &server_att_decl[QM_BLE_SVC_IDX_WC_VAL];
    decl_handle->uuid = (const uint8_t *)&uuid_16->val;
    decl_handle->s.uuid_len = UUID_LEN_16BIT;
    decl_handle->s.max_len = QB_GATT_MTU_SIZE_MAX;
    decl_handle->s.read_indication = 1;
    decl_handle->char_prop.wr_req = 1;
    decl_handle->char_prop.rd_en = 1;

    /* ic */
    g_ble_hal_ctx.ic_ccc_change = qms_init->ic.on_ccc_change;
    uuid_16 = (qms_uuid_16_t *)qms_init->ic.uuid;
    decl_handle = &server_att_decl[QM_BLE_SVC_IDX_IC_VAL];
    decl_handle->uuid = (const uint8_t *)&uuid_16->val;
    decl_handle->s.uuid_len = UUID_LEN_16BIT;
    decl_handle->s.max_len = QB_GATT_MTU_SIZE_MAX;
    decl_handle->s.read_indication = 1;
    decl_handle->char_prop.ind_en = 1;
    decl_handle->char_prop.rd_en = 1;

    /* wwnrc */
    g_ble_hal_ctx.wwnrc_write = qms_init->wwnrc.on_write;
    uuid_16 = (qms_uuid_16_t *)qms_init->wwnrc.uuid;
    decl_handle = &server_att_decl[QM_BLE_SVC_IDX_WWNRC_VAL];
    decl_handle->uuid = (const uint8_t *)&uuid_16->val;
    decl_handle->s.uuid_len = UUID_LEN_16BIT;
    decl_handle->s.max_len = QB_GATT_MTU_SIZE_MAX;
    decl_handle->s.read_indication = 1;
    decl_handle->char_prop.rd_en = 1;
    decl_handle->char_prop.wr_cmd = 1;
    
    /* nc */
    g_ble_hal_ctx.nc_ccc_change = qms_init->nc.on_ccc_change;
    uuid_16 = (qms_uuid_16_t *)qms_init->nc.uuid;
    decl_handle = &server_att_decl[QM_BLE_SVC_IDX_NC_VAL];
    decl_handle->uuid = (const uint8_t *)&uuid_16->val;
    decl_handle->s.uuid_len = UUID_LEN_16BIT;
    decl_handle->s.max_len = QB_GATT_MTU_SIZE_MAX;
    decl_handle->s.read_indication = 1;
    decl_handle->char_prop.rd_en = 1;
    decl_handle->char_prop.ntf_en = 1;

//todo 多接口可使用
#if 1
    qm_ble_gap_register_callback(qm_gap_ble_callback);
    dev_manager_cb_register(dev_manager_callback);
    gap_manager_init(gap_manager_callback); 
    gatt_manager_init(gatt_manager_callback);
#endif
//

    return QMS_ERR_SUCCESS;
}

/**
 * API to send data via QM's Notify Characteristics.
 * @parma[in]  p_data  data buffer.
 * @parma[in]  length  data length.
 * @return     0 on success, error code if failure.
 */
qms_err_t qm_ble_send_notification(uint8_t *p_data, uint16_t length)
{
    uint16_t handle = gatt_manager_get_svc_att_handle(&g_ble_hal_ctx.server_svc_env, QM_BLE_SVC_IDX_NC_VAL);

    if(!g_ble_hal_ctx.server_ntf_done){
        return QMS_ERR_GATT_NOTIFY_FAIL;
    } 

    g_ble_hal_ctx.server_ntf_done = QM_FALSE;

    QM_HEX_LOGD("1", "notify ", p_data, length);
    
    gatt_manager_server_send_notification(g_ble_hal_ctx.connect_id, handle, p_data, length, NULL);
    return QMS_ERR_SUCCESS;
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
    uint16_t handle = gatt_manager_get_svc_att_handle(&g_ble_hal_ctx.server_svc_env, QM_BLE_SVC_IDX_IC_VAL);

    g_ble_hal_ctx.txdone = txdone;
    gatt_manager_server_send_indication(g_ble_hal_ctx.connect_id, handle, p_data, length, NULL);
    return QMS_ERR_SUCCESS;
}

/**
 * API to disconnect BLE connection.
 * @param[in]  reason  the reason to disconnect the connection.
 */
void qm_ble_disconnect(uint8_t reason)
{
    gap_manager_disconnect(g_ble_hal_ctx.connect_id, 0x13);
}

/**
 * API to start bluetooth advertising.
 * @return     0 on success, erro code if failure.
 */
qms_err_t qm_ble_advertising_start(qms_adv_init_t *adv)
{
    uint16_t index = 0;
    uint8_t adv_data[32] = {0};
    uint8_t srv[] = {0xB7, 0xFE};
    qm_ble_adv_params_t adv_param = {
        .adv_int_max = 100,
        .adv_int_min = 100,
    };

    // adv_data[index++] = 2;
    // adv_data[index++]=  GAP_ADV_TYPE_FLAGS;
    // adv_data[index++] = adv->flag;

    adv_data[index++] = sizeof(uint16_t) + 1;
    adv_data[index++] = GAP_ADV_TYPE_COMPLETE_LIST_16_BIT_UUID; //BT_DATA_UUID16_ALL
    adv_data[index++] = srv[0];
    adv_data[index++] = srv[1];

    if (adv->vdata.len != 0) {
        adv_data[index++]     = adv->vdata.len + 1;
        adv_data[index++] = GAP_ADV_TYPE_MANU_SPECIFIC_DATA; // BT_DATA_MANUFACTURER_DATA
        memcpy(&adv_data[index], adv->vdata.data, adv->vdata.len);
    } 
    index += adv->vdata.len;
    qm_ble_gap_set_adv_data_raw(adv_data, index);
    
    index = 0;
    adv_data[index++] = strlen(adv->name.name) + 1;
    switch (adv->name.ntype) {
        case QMS_ADV_NAME_SHORT:
            adv_data[index++]  = GAP_ADV_TYPE_SHORTENED_NAME;
            break;
        case QMS_ADV_NAME_FULL:
            adv_data[index++]  = GAP_ADV_TYPE_COMPLETE_NAME;
            break;
        default:
            return QMS_ERR_INVALID_ADV_DATA;
    }
    memcpy(&adv_data[index], adv->name.name, strlen(adv->name.name));
    index += strlen(adv->name.name);
    qm_ble_gap_set_scan_rsp_data_raw(adv_data, index);

    return qm_ble_gap_start_advertising(&adv_param);
}

/**
 * API to stop bluetooth advertising.
 * @return     0 on success, erro code if failure.
 */
qms_err_t qm_ble_advertising_stop(void)
{
    qm_ble_gap_set_adv_data_raw(NULL, 0);
    qm_ble_gap_set_scan_rsp_data_raw(NULL, 0);

    return qm_ble_gap_stop_advertising();
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
    *att_mtu = QB_GATT_MTU_SIZE_MAX;
    return QM_EOK;
}
#endif