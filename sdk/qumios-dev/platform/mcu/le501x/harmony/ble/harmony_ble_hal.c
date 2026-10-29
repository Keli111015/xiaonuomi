#include "qm.h"
#include "qm_log.h"
#include "harmony_ble_hal.h"
#include "harmony_ble_comm.h"

#include "ls_ble.h"
#include "log.h"

typedef struct 
{
    uint8_t pair_mode;
    char device_name[32];
    void (*txdone)(uint8_t res);
    void (*ota_txdone)(uint8_t res);
    int (*on_char_read)(void *buf, int len);
    int (*on_char_write)(void *buf, int len);
    int (*on_char_ota_ctrl)(void *buf, int len);
    int (*on_char_ota_data)(void *buf, int len);
    void (*event_notify_callback)(harmony_ble_stack_event_t event);
}harmony_ble_hal_ctx_t;

enum svc_att_db_handles
{
    HARMONY_BLE_SVC_IDX_RX_CHAR,               // 特征值申明
    HARMONY_BLE_SVC_IDX_RX_VAL,                // RX 特征值
    HARMONY_BLE_SVC_IDX_RX_IND_CFG,            // 特征值描述符
    HARMONY_BLE_SVC_IDX_TX_CHAR,               // 特征值申明
    HARMONY_BLE_SVC_IDX_TX_VAL,                // TX 特征值
    HARMONY_BLE_SVC_ATT_NUM                    // 
};

enum svc_att_ota_db_handles
{
    HARMONY_BLE_SVC_OTA_IDX_CTRL_CHAR,               // 特征值申明
    HARMONY_BLE_SVC_OTA_IDX_CTRL_VAL,                // OTA CTRL 特征值
    HARMONY_BLE_SVC_OTA_IDX_CTRL_IND_CFG,            // 特征值描述符
    HARMONY_BLE_SVC_OTA_IDX_DATA_CHAR,               // 特征值申明
    HARMONY_BLE_SVC_OTA_IDX_DATA_VAL,                // OTADATA 特征值
    HARMONY_BLE_SVC_OTA_ATT_NUM                    // 
};

static harmony_ble_hal_ctx_t ble_hal_ctx = {0};

static uint8_t connect_id = 0;

static uint16_t cccd_config = 0;
static struct svc_decl server_svc = {0};
static struct gatt_svc_env server_svc_env = {0};

static uint16_t ota_cccd_config = 0;
static struct svc_decl server_ota_svc = {0};
static struct gatt_svc_env server_ota_svc_env = {0};

static const uint8_t att_decl_char_array[] = {0x03,0x28}; 
static const uint8_t att_desc_client_char_cfg_array[] = {0x02,0x29}; 

static struct att_decl server_att_decl[HARMONY_BLE_SVC_ATT_NUM] =
{
    [HARMONY_BLE_SVC_IDX_RX_CHAR] = {                      // 特征值申明
        .uuid = att_decl_char_array,                // attribute uuid，特征值申明是固定的16-bit uuid
        .s.max_len = 0,                             // 当前属性值支持的通信的最大的字节数
        .s.uuid_len = UUID_LEN_16BIT,               // UUID 的长度,常用的 16-BIT 和 128-BIT
        .s.read_indication = 1,                     // 默认为 1
        .char_prop.rd_en = 1,                       // 支持读属性
    },
    [HARMONY_BLE_SVC_IDX_RX_VAL] = {                       // RX 特征值，128-bit uuid, (characteristic support write req/cmd properties)
        .uuid = NULL,
        .s.max_len = HARMONY_BLE_DEFAULT_MTU_SIZE,
        .s.uuid_len = UUID_LEN_128BIT,
        .s.read_indication = 1,
        .char_prop.rd_en = 1, 
        .char_prop.ind_en = 1,                 
    },
    [HARMONY_BLE_SVC_IDX_RX_IND_CFG] = {                   // TX 特征值描述符 CCCD, 当 Characteristic 具有Notify或者Indicate操作属性时，都要为其添加 CCCD attribute
        .uuid = att_desc_client_char_cfg_array,
        .s.max_len = 0,
        .s.uuid_len = UUID_LEN_16BIT,
        .s.read_indication = 1,
        .char_prop.rd_en = 1,
        .char_prop.wr_req = 1,
    },
    [HARMONY_BLE_SVC_IDX_TX_CHAR] = {                      // 特征值申明
        .uuid = att_decl_char_array,
        .s.max_len = 0,
        .s.uuid_len = UUID_LEN_16BIT,
        .s.read_indication = 1,
        .char_prop.rd_en = 1, 
    },
    [HARMONY_BLE_SVC_IDX_TX_VAL] = {
        .uuid = NULL,           // TX 特征值，128-bit uuid, (characteristic support notify properties)
        .s.max_len = HARMONY_BLE_DEFAULT_MTU_SIZE,
        .s.uuid_len = UUID_LEN_128BIT,
        .s.read_indication = 1,
        .char_prop.wr_req = 1,                      //支持 WRITE 属性
    },

};

static struct att_decl server_ota_att_decl[HARMONY_BLE_SVC_OTA_ATT_NUM] =
{
    [HARMONY_BLE_SVC_OTA_IDX_CTRL_CHAR] = {                      // 特征值申明
        .uuid = att_decl_char_array,                // attribute uuid，特征值申明是固定的16-bit uuid
        .s.max_len = 0,                             // 当前属性值支持的通信的最大的字节数
        .s.uuid_len = UUID_LEN_16BIT,               // UUID 的长度,常用的 16-BIT 和 128-BIT
        .s.read_indication = 1,                     // 默认为 1
        .char_prop.rd_en = 1,                       // 支持读属性
    },

    [HARMONY_BLE_SVC_OTA_IDX_CTRL_VAL] = {
        .uuid = NULL,
        .s.max_len = HARMONY_BLE_DEFAULT_MTU_SIZE,
        .s.uuid_len = UUID_LEN_128BIT,
        .s.read_indication = 1,
        .char_prop.wr_req = 1, 
        .char_prop.ind_en = 1,                      //支持 WRITE 属性
    },

    [HARMONY_BLE_SVC_OTA_IDX_CTRL_IND_CFG] = {       //特征值描述符 CCCD, 当 Characteristic 具有Notify或者Indicate操作属性时，都要为其添加 CCCD attribute
        .uuid = att_desc_client_char_cfg_array,
        .s.max_len = 0,
        .s.uuid_len = UUID_LEN_16BIT,
        .s.read_indication = 1,
        .char_prop.rd_en = 1,
        .char_prop.wr_req = 1,
    },

    [HARMONY_BLE_SVC_OTA_IDX_DATA_CHAR] = {                      // 特征值申明
        .uuid = att_decl_char_array,                // attribute uuid，特征值申明是固定的16-bit uuid
        .s.max_len = 0,                             // 当前属性值支持的通信的最大的字节数
        .s.uuid_len = UUID_LEN_16BIT,               // UUID 的长度,常用的 16-BIT 和 128-BIT
        .s.read_indication = 1,                     // 默认为 1
        .char_prop.rd_en = 1,                       // 支持读属性
    },
    [HARMONY_BLE_SVC_OTA_IDX_DATA_VAL] = {                       // RX 特征值，128-bit uuid, (characteristic support write req/cmd properties)
        .uuid = NULL,
        .s.max_len = HARMONY_BLE_DEFAULT_MTU_SIZE,
        .s.uuid_len = UUID_LEN_128BIT,
        .s.read_indication = 1,
        .char_prop.wr_cmd = 1,              
    },

};

extern int dev_manager_cb_register(void (*cb)(enum dev_evt_type type,union dev_evt_u *evt));
extern int dev_manager_cb_unregister(void (*cb)(enum dev_evt_type type,union dev_evt_u *evt));

int harmony_ble_hal_dev_name_set(char *dev_name, int len)
{
    memcpy(ble_hal_ctx.device_name, dev_name, len);
    return QM_EOK;
}

static void dis_server_get_dev_name(struct gap_dev_info_dev_name *dev_name_ptr, uint8_t con_idx)
{
    dev_name_ptr->value = (uint8_t*)&ble_hal_ctx.device_name[0];
    dev_name_ptr->length = strlen(ble_hal_ctx.device_name);
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
            connect_id = con_idx;   // 记录当前连接的 CONNECT_ID
            LOG_I("connected!");
            
            if(ble_hal_ctx.pair_mode){
                gap_manager_slave_security_req(con_idx, AUTH_SEC_CON);
            }

            if(ble_hal_ctx.event_notify_callback){
                ble_hal_ctx.event_notify_callback(HARMONY_BLE_STACK_EVENT_CONNECT);
            }
        break;
        case DISCONNECTED:
            connect_id = 0xff; 
            if(ble_hal_ctx.event_notify_callback){
                ble_hal_ctx.event_notify_callback(HARMONY_BLE_STACK_EVENT_DISCONNECT);
            }
            LOG_I("disconnected! reason = %x ",evt->disconnected.reason);       // DISCONNECT 断连事件，打印断连原因
        break;
        case MASTER_PAIR_REQ:
        {
            if(!ble_hal_ctx.pair_mode){
                break;
            }
            struct pair_feature feat_param={
                .iocap = BLE_GAP_IO_CAPS_NONE,
                .oob = BLE_GAP_OOB_DISABLE,
                .auth = AUTH_MITM | AUTH_BOND,
                .key_size = 16,
                .ikey_dist = KDIST_ENCKEY|KDIST_IDKEY,
                .rkey_dist = KDIST_ENCKEY|KDIST_IDKEY,
            };
            gap_manager_slave_pair_response_send(con_idx, true, &feat_param);
            LOG_I("JUST_WORKS");
        }
        break;
        case PAIR_DONE:
            LOG_I("PAIR_DONE reason :%d , success :%d", evt->pair_done.u.fail_reason, evt->pair_done.succeed);
        break;
        case ENCRYPT_FAIL:
            LOG_I("ENCRYPT_FAIL");
        break;
        case CONN_PARAM_REQ:

        break;
        case CONN_PARAM_UPDATED:        // 连接参数已经更新事件，打印当前的连接参数
            LOG_I("conn param updata  interval = %d  latency = %d  sup_to = %d",evt->conn_param_updated.con_interval,evt->conn_param_updated.con_latency,evt->conn_param_updated.sup_to);
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
    if(att_idx == HARMONY_BLE_SVC_IDX_RX_IND_CFG)
    {
        len = 2;
        memcpy(read_buff, &cccd_config, 2);
    }else if(att_idx == HARMONY_BLE_SVC_IDX_RX_VAL){
        if(ble_hal_ctx.on_char_read){
            ble_hal_ctx.on_char_read(read_buff, len);
        }
    }
    handle = gatt_manager_get_svc_att_handle(&server_svc_env, att_idx);
    gatt_manager_server_read_req_reply(con_idx, handle, 0, (void*)read_buff, len);
}

static void server_write_req_ind(uint8_t att_idx, uint8_t con_idx, uint16_t length, uint8_t const *value)
{
    if(att_idx == HARMONY_BLE_SVC_IDX_TX_VAL)
    { 
        if(ble_hal_ctx.on_char_write){
            ble_hal_ctx.on_char_write((void *)value, length);
        }
    }else if (att_idx == HARMONY_BLE_SVC_IDX_RX_IND_CFG){
        memcpy(&cccd_config, value, length);
    }
}

static void server_ota_read_req_ind(uint8_t att_idx, uint8_t con_idx)
{
    uint16_t handle = 0;
    uint8_t read_buff[2] = {0};
    
    if(att_idx != HARMONY_BLE_SVC_OTA_IDX_CTRL_IND_CFG){
        return;
    }

    memcpy(read_buff, &ota_cccd_config, 2);
    handle = gatt_manager_get_svc_att_handle(&server_svc_env, att_idx);
    gatt_manager_server_read_req_reply(con_idx, handle, 0, (void*)read_buff, 2);
}

static void server_ota_write_req_ind(uint8_t att_idx, uint8_t con_idx, uint16_t length, uint8_t const *value)
{
    switch (att_idx)
    {
        case HARMONY_BLE_SVC_OTA_IDX_CTRL_IND_CFG:
            QM_HEX_LOGD(LOG_TAG, "IND_CFG ", value, length);
            memcpy(&ota_cccd_config, value, length);
        break;
        
        case HARMONY_BLE_SVC_OTA_IDX_CTRL_VAL:
            QM_HEX_LOGD(LOG_TAG, "OTA_CTRL ", value, length);

            if(ble_hal_ctx.on_char_ota_ctrl){
                ble_hal_ctx.on_char_ota_ctrl((void *)value, length);
            }
        break;

        case HARMONY_BLE_SVC_OTA_IDX_DATA_VAL:
            QM_HEX_LOGD(LOG_TAG, "OTA_DATA ", value, length);

            if(ble_hal_ctx.on_char_ota_data){
                ble_hal_ctx.on_char_ota_data((void *)value, length);
            }
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
    if (connect_id != 0xff)
    {
        switch (type)
        {
            case SERVER_READ_REQ:       // Read:APP读取Server端的数据
                if(server_ota_svc_env.start_hdl == evt->server_read_req.svc->start_hdl){
                    QM_LOGD(LOG_TAG, "OTA SERVER READ REQ");
                    server_ota_read_req_ind(evt->server_read_req.att_idx, con_idx);
                }else if(server_svc_env.start_hdl == evt->server_read_req.svc->start_hdl){
                    QM_LOGD(LOG_TAG, "GEN SERVER READ REQ");
                    server_read_req_ind(evt->server_read_req.att_idx, con_idx);
                }
            break;
            case SERVER_WRITE_REQ:      // Write:APP写数据到Server端
            {
                if(server_ota_svc_env.start_hdl == evt->server_write_req.svc->start_hdl){
                    server_ota_write_req_ind(evt->server_write_req.att_idx, con_idx, evt->server_write_req.length, evt->server_write_req.value);
                }else if(server_svc_env.start_hdl == evt->server_write_req.svc->start_hdl){
                    QM_HEX_LOGD(LOG_TAG, "GEN SERVER WRITE REQ", evt->server_write_req.value, evt->server_write_req.length);
                    server_write_req_ind(evt->server_write_req.att_idx, con_idx, evt->server_write_req.length, evt->server_write_req.value);
                }
            }
            break;
            case SERVER_NOTIFICATION_DONE:      // NOTIFICATION_DONE,该事件意味当前的Notificate数据包已经交给协议栈处理，可以继续发下一包Notificate数据

            break;
            case SERVER_INDICATION_DONE:      // NOTIFICATION_DONE,该事件意味当前的Notificate数据包已经交给协议栈处理，可以继续发下一包Notificate数据
                LOG_I("ind done");
                if(ble_hal_ctx.txdone){
                    ble_hal_ctx.txdone(1);
                }
            break;
            case MTU_CHANGED_INDICATION:        // MTU Exchange 已经完成指示事件
            {    
                LOG_I("mtu: %d", evt->mtu_changed_ind.mtu);     // 打印当前的 MTU
            }
            break;
            default:
                LOG_I("Event not handled! type = %d", type);
            break;
        }
    }
    else
    {
        LOG_I("receive gatt msg when disconnected!");
    }
}

static void dev_manager_callback(enum dev_evt_type type,union dev_evt_u *evt)
{
    switch(type)
    {

        case STACK_READY:
        {
            dev_manager_add_service(&server_svc);
        }
        break;

        case SERVICE_ADDED:
        {
            static uint8_t server_num = 0;
            if(0 == server_num)
            {
                server_num = 1;
                gatt_manager_svc_register(evt->service_added.start_hdl, HARMONY_BLE_SVC_ATT_NUM, &server_svc_env);

                dev_manager_add_service(&server_ota_svc); // 服务添加步骤3：添加第二个服务 ota server
            }
            else if(server_num == 1)
            {
                server_num = 2;
                gatt_manager_svc_register(evt->service_added.start_hdl, HARMONY_BLE_SVC_OTA_ATT_NUM, &server_ota_svc_env);       // 服务添加步骤4：private server 服务信息注册
            }
        }
        break;

        case ADV_OBJ_CREATED:
        {
            
            if(ble_hal_ctx.event_notify_callback){
                ble_hal_ctx.event_notify_callback(HARMONY_BLE_STACK_EVENT_INTT_SUCCESS);
            }
        }
        break;

        default:
        break;
    }
}

/**
 * API to initialize ble stack.
 * @parma[in]  init  Bluetooth stack init parmaters.
 * @return     0 on success, error code if failure.
 */
int harmony_ble_hal_gatt_stack_init(harmony_ble_init_t *ble_init)
{
    QM_RETURN_ON_FALSE(ble_init, -QM_EINVAL, LOG_TAG, "ble init inval!!");

    server_svc.uuid = ble_init->service_uuid;
    server_svc.att = server_att_decl;
    server_svc.nb_att = HARMONY_BLE_SVC_ATT_NUM;
    server_svc.uuid_len = UUID_LEN_128BIT;
    server_att_decl[HARMONY_BLE_SVC_IDX_RX_VAL].uuid = ble_init->rc.uuid;
    server_att_decl[HARMONY_BLE_SVC_IDX_TX_VAL].uuid = ble_init->wc.uuid;

    server_ota_svc.uuid = ble_init->service_otauuid;
    server_ota_svc.att = server_ota_att_decl;
    server_ota_svc.nb_att = HARMONY_BLE_SVC_ATT_NUM;
    server_ota_svc.uuid_len = UUID_LEN_128BIT;
    server_ota_att_decl[HARMONY_BLE_SVC_OTA_IDX_CTRL_VAL].uuid = ble_init->ota_ctrl.uuid;
    server_ota_att_decl[HARMONY_BLE_SVC_OTA_IDX_DATA_VAL].uuid = ble_init->ota_data.uuid;

    ble_hal_ctx.pair_mode = ble_init->pair_mode;
    ble_hal_ctx.event_notify_callback = ble_init->event_notify_callback_t;
    ble_hal_ctx.on_char_read = ble_init->rc.on_char_read_t;
    ble_hal_ctx.on_char_write = ble_init->wc.on_char_write_t;

    ble_hal_ctx.on_char_ota_ctrl = ble_init->ota_ctrl.ota_ctrl_t;
    ble_hal_ctx.on_char_ota_data = ble_init->ota_data.on_data_t;

    dev_manager_cb_register(dev_manager_callback);
    gap_manager_init(gap_manager_callback); 
    gatt_manager_init(gatt_manager_callback);

    return QM_EOK;  
}

/**
 * API to send data via OTA Indicate Characteristics.
 * @parma[in]  p_data  data buffer.
 * @parma[in]  length  data length.
 * @parma[in]  txdone  txdone callback.
 * @return     0 on success, erro code if failure.
 */
int harmony_ble_hal_send_indication(uint8_t *p_data, uint16_t length, void (*txdone)(uint8_t res))
{
    ble_hal_ctx.txdone = txdone;
    uint16_t handle = gatt_manager_get_svc_att_handle(&server_svc_env, HARMONY_BLE_SVC_IDX_RX_VAL);
    gatt_manager_server_send_indication(connect_id, handle, p_data, length, NULL);
    return QM_EOK;
}


/**
 * API to send data via QM's Indicate Characteristics.
 * @parma[in]  p_data  data buffer.
 * @parma[in]  length  data length.
 * @parma[in]  txdone  txdone callback.
 * @return     0 on success, erro code if failure.
 */
int harmony_ble_hal_ota_send_indication(uint8_t *p_data, uint16_t length, void (*txdone)(uint8_t res))
{
    ble_hal_ctx.ota_txdone = txdone;
    uint16_t handle = gatt_manager_get_svc_att_handle(&server_ota_svc_env, HARMONY_BLE_SVC_OTA_IDX_CTRL_VAL);
    gatt_manager_server_send_indication(connect_id, handle, p_data, length, NULL);
    return QM_EOK;
}

/**
 * API to disconnect BLE connection.
 * @param[in]  reason  the reason to disconnect the connection.
 */
void harmony_ble_hal_disconnect(uint8_t reason)
{
    if(reason == 0){
        reason = 0x13;
    }
    gap_manager_disconnect(connect_id, reason);
}