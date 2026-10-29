#include "qm.h"
#include "qm_log.h"
#include "honor_ble_hal.h"
#include "honor_ble_comm.h"

#include "ls_ble.h"
#include "log.h"
#include "ls_dbg.h"
#include "honor_protocol.h"


typedef struct 
{
    uint8_t pair_mode;
    char device_name[32];
    void (*txdone)(uint8_t res);

    int (*on_char_read)(void *buf, int len);
    int (*on_char_write)(void *buf, int len);
    int (*on_char_write_without_rsp)(void *buf, int len);
    int (*on_char_notify)(void *buf, int len);
    int (*on_char_indicate)(void *buf, int len);

    void (*event_notify_callback)(honor_ble_stack_event_t event);
}honor_ble_hal_ctx_t;

enum uart_svc_att_db_handles
{
    HONOR_SVC_IDX_RX_CHAR,
    HONOR_SVC_IDX_RX_VAL,
    HONOR_SVC_IDX_TX_NTF_CFG,
    HONOR_SVC_ATT_NUM
};

enum svc_att_db_handles
{
    BLE_SVC_IDX_CTRL_CHAR,               // 特征值申明
    BLE_SVC_IDX_CTRL_VAL,                // OTA CTRL 特征值
    BLE_SVC_IDX_CTRL_IND_CFG,            // 特征值描述符
    BLE_SVC_ATT_NUM                    // 
};

#define HONOR_SERVER_MTU_DFT  23

static honor_ble_hal_ctx_t ble_hal_ctx = {0};

static uint8_t connect_id = 0;
static uint8_t adv_obj_hdl;
static uint16_t cccd_config = 0;
static struct svc_decl server_svc = {0};
static struct gatt_svc_env server_svc_env = {0};

// 设备属性UUID读取到的设备名称
// const char *g_deviceName = "HONOR_DEMO";
// const char *g_nvsNamespace = "bleAuthCode";

static uint8_t adv_data_length = 0;
static uint8_t g_curLightBrightness = 1;
static uint8_t g_curLightSwitch = 1;

static uint16_t honor_server_mtu = HONOR_SERVER_MTU_DFT;

static uint8_t g_advData[BLE_ADV_DATA_LEN] = {};
static uint8_t g_scanRspData[31] = { 0x13, 0x09, 'M', 'a', 's', 's', 'a', 'g', 'e', ' ', 'n', 'e', 'c', 'k', ' ', 'p', 'i', 'l', 'l', 'w' };
static uint8_t g_scanRspDataLen = 0;
#define PROFILE_A_APP_ID 0
#define PROFILE_B_APP_ID 1

#define UART_SERVER_WITH_OTA 0

#if UART_SERVER_WITH_OTA == 1
#define UART_SVC_ADV_NAME "LS Uart Server ota prf"
#else
#define UART_SVC_ADV_NAME "Massage neck pillow"
#endif
#define UART_SERVER_MAX_MTU  247
#define UART_SERVER_MTU_DFT  23
#define UART_SERVER_MAX_DATA_LEN (uart_server_mtu - 3)
#define UART_SVC_RX_MAX_LEN (UART_SERVER_MAX_MTU - 3)
#define UART_SVC_TX_MAX_LEN (UART_SERVER_MAX_MTU - 3)
#define UART_SVC_BUFFER_SIZE (128)


static const uint8_t att_decl_char_array[] = {0x03,0x28};
static const uint8_t att_desc_client_char_cfg_array[] = {0x02,0x29};




static struct att_decl honor_server_att_decl[HONOR_SVC_ATT_NUM] =
{
    [HONOR_SVC_IDX_RX_CHAR] = {
        .uuid = att_decl_char_array,
        .s.max_len = 0,
        .s.uuid_len = UUID_LEN_16BIT,
        .s.read_indication = 1,   
        .char_prop.rd_en = 1,
        .char_prop.wr_cmd = 1,
        .char_prop.wr_req = 1,
        .char_prop.ntf_en = 1,
        .char_prop.ind_en = 1,
        
    },
    [HONOR_SVC_IDX_RX_VAL] = {
        .uuid = NULL,
        .s.max_len = UART_SVC_RX_MAX_LEN,
        .s.uuid_len = UUID_LEN_16BIT,
        .s.read_indication = 1,
        .char_prop.wr_cmd = 1,
        .char_prop.wr_req = 1,
        .char_prop.ntf_en = 1,
        .char_prop.ind_en = 1,
        
    },
    [HONOR_SVC_IDX_TX_NTF_CFG] = {
        .uuid = att_desc_client_char_cfg_array,
        .s.max_len = 0,
        .s.uuid_len = UUID_LEN_16BIT,
        .s.read_indication = 1,
        .char_prop.rd_en = 1,
        .char_prop.wr_cmd = 1,
        .char_prop.wr_req = 1,
        .char_prop.ntf_en = 1,
        .char_prop.ind_en = 1,
    },
};


static struct att_decl normal_server_att_decl[BLE_SVC_ATT_NUM] =
{
    [BLE_SVC_IDX_CTRL_CHAR] = {
        .uuid = att_decl_char_array,
        .s.max_len = 0,
        .s.uuid_len = UUID_LEN_16BIT,
        .s.read_indication = 1,   
        .char_prop.rd_en = 1,
        .char_prop.wr_cmd = 1,
        .char_prop.wr_req = 1,
        .char_prop.ntf_en = 1,
        .char_prop.ind_en = 1,
        
    },
    [BLE_SVC_IDX_CTRL_VAL] = {
        .uuid = NULL,
        .s.max_len = UART_SVC_RX_MAX_LEN,
        .s.uuid_len = UUID_LEN_16BIT,
        .s.read_indication = 1,
        .char_prop.wr_cmd = 1,
        .char_prop.wr_req = 1,
        .char_prop.ntf_en = 1,
        .char_prop.ind_en = 1,
        
    },
    [BLE_SVC_IDX_CTRL_IND_CFG] = {
        .uuid = att_desc_client_char_cfg_array,
        .s.max_len = 0,
        .s.uuid_len = UUID_LEN_16BIT,
        .s.read_indication = 1,
        .char_prop.rd_en = 1,
        .char_prop.wr_cmd = 1,
        .char_prop.wr_req = 1,
        .char_prop.ntf_en = 1,
        .char_prop.ind_en = 1,
    },
};

static struct svc_decl honor_server_svc = {0};
static struct gatt_svc_env honor_server_svc_env = {0};

static struct svc_decl normal_server_svc = {0};
static struct gatt_svc_env normal_server_svc_env = {0};

extern int dev_manager_cb_register(void (*cb)(enum dev_evt_type type,union dev_evt_u *evt));

static uint8_t _g_adv_start = 0;

uint8_t adv_status_get(void)
{
    return _g_adv_start;
}

void adv_status_set(uint8_t status)
{
    _g_adv_start = status;
}


int honor_ble_hal_dev_name_set(char *dev_name, int len)
{
    memcpy(ble_hal_ctx.device_name, dev_name, len);
    return QM_EOK;
}

void honor_adv_data_set(uint8_t *data, uint8_t len)
{
    adv_data_length = len;
    memcpy(g_advData, data, len);
}

static void dis_server_get_dev_name(struct gap_dev_info_dev_name *dev_name_ptr, uint8_t con_idx)
{
    dev_name_ptr->value = (uint8_t*)&ble_hal_ctx.device_name[0];
    dev_name_ptr->length = strlen(ble_hal_ctx.device_name);
}

static void create_adv_obj()
{
    struct legacy_adv_obj_param adv_param = {
        .adv_intv_min = 32,
        .adv_intv_max = 32,
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
        // case GET_DEV_INFO_DEV_NAME:
        //     dis_server_get_dev_name((struct gap_dev_info_dev_name *)evt, con_idx);
        // break;

        case CONNECTED:             // CONNECTED 连接建立事件
            connect_id = con_idx;   // 记录当前连接的 CONNECT_ID
            LOG_I("connected!");
            
            if(ble_hal_ctx.pair_mode)
            {
                uint8_t peer_id_cont = gap_manager_get_bonding_peer_id(con_idx);
                LOG_I("CONNECT Peer_id = %d", peer_id_cont);
                gap_manager_slave_security_req(con_idx, AUTH_SEC_CON);
            }           
            LOG_I("connected! new con_idx = %d", con_idx);

            if(ble_hal_ctx.event_notify_callback){
                ble_hal_ctx.event_notify_callback(HONOR_BLE_STACK_EVENT_CONNECTED);
            }
        break;
        case DISCONNECTED:
            connect_id = 0xff; 
            honor_server_mtu = UART_SERVER_MTU_DFT;
            if(ble_hal_ctx.event_notify_callback){
                ble_hal_ctx.event_notify_callback(HONOR_BLE_STACK_EVENT_DISCONNECT);
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
    uint16_t handle = 0;
    if(att_idx == HONOR_SVC_IDX_TX_NTF_CFG)
    {
        handle = gatt_manager_get_svc_att_handle(&honor_server_svc_env, att_idx);
        gatt_manager_server_read_req_reply(con_idx, handle, 0, (void*)&cccd_config, 2);
    }
}

static void ProcessFrameFromApp(uint8_t *data, uint16_t dataLen, uint8_t profileInst)
{
    if (dataLen <= 64) {
        LOG_I( "%c server recv[%d]:", profileInst + 'A', dataLen);
        LOG_HEX(data, dataLen);
    }

    if (dataLen == 2 && (data[0] == 1 && data[1] == 0)) {
        LOG_I( "notify msg return");
        return;
    }

    BLE_SEND_TYPE sendType = (profileInst == 0)
                                 ? BLE_HONOR_SEND
                                 : ((profileInst == 1) ? BLE_NORMAL_SEND : BLE_SEND_BUTT);
    if (sendType == BLE_SEND_BUTT) {
        LOG_I( "profile inst error");
        return;
    }

    // data为NULL，且len为0，则为透传方案的read交互
    ProcessFrame(sendType, data, dataLen);
    return;
}


static void honor_server_write_req_ind(uint8_t att_idx, uint8_t con_idx, uint16_t length, uint8_t const *value)
{
    uint8_t status = 0;
    LOG_I("att_idx = %d", att_idx);
    if(att_idx == HONOR_SVC_IDX_RX_VAL)
    { 
        LOG_I("1-HONOR_SVC_IDX_RX_VAL");
        ProcessFrameFromApp(value, length, PROFILE_B_APP_ID);
    }
    else if (att_idx == HONOR_SVC_IDX_TX_NTF_CFG)
    {
        LOG_I("1-HONOR_SVC_IDX_TX_NTF_CFG");
        LS_ASSERT(length == 2);
        memcpy(&cccd_config, value, length);
    }
}

static void normal_server_write_req_ind(uint8_t att_idx, uint8_t con_idx, uint16_t length, uint8_t const *value)
{
    uint8_t status = 0;
    LOG_I("normal_server_write_req_ind normal_att_idx = %d", att_idx);
    if(att_idx == BLE_SVC_IDX_CTRL_VAL)
    { 
        LOG_I("2-BLE_SVC_IDX_CTRL_VAL");
        ProcessFrameFromApp(value, length, BLE_SVC_IDX_CTRL_VAL);
    }
    else if (att_idx == BLE_SVC_IDX_CTRL_IND_CFG)
    {
        LOG_I("2-BLE_SVC_IDX_CTRL_IND_CFG");
        LS_ASSERT(length == 2);
        memcpy(&cccd_config, value, length);
    }
}

static void honor_server_data_length_update(uint8_t con_idx)
{
    struct gap_set_pkt_size dlu_param = 
    {
        .pkt_size = 251,
    };
    gap_manager_set_pkt_size(con_idx, &dlu_param);
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
     if (gap_manager_get_role(con_idx) == LS_BLE_ROLE_SLAVE)
    {
        connect_id == 0xff;
    }
    if (connect_id != 0xff)
    {
        switch (type)
        {
            case SERVER_READ_REQ:       // Read:APP读取Server端的数据
                QM_LOGD(LOG_TAG, "GEN SERVER READ REQ");
                server_read_req_ind(evt->server_read_req.att_idx, con_idx);
            break;
            case SERVER_WRITE_REQ:      // Write:APP写数据到Server端
            {
                if(honor_server_svc_env.start_hdl == evt->server_write_req.svc->start_hdl){
                    honor_server_write_req_ind(evt->server_write_req.att_idx, con_idx, evt->server_write_req.length, evt->server_write_req.value);
                }else if(normal_server_svc_env.start_hdl == evt->server_write_req.svc->start_hdl){
                    LOG_I("GEN SERVER WRITE REQ");
                    normal_server_write_req_ind(evt->server_write_req.att_idx, con_idx, evt->server_write_req.length, evt->server_write_req.value);
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
                honor_server_mtu = evt->mtu_changed_ind.mtu;
                honor_server_data_length_update(con_idx);
                LOG_I("mtu: %d", evt->mtu_changed_ind.mtu);     // 打印当前的 MTU
            }
            break;

            case CLIENT_WRITE_WITH_RSP_DONE:        /*!<  Write response indication for client*/
            
            
            break;
            case CLIENT_WRITE_NO_RSP_DONE: 

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
    static uint8_t server_num = 0;
    LOG_I("dev_manager_callback type = %d!", type);
    switch(type)
    {
    #if 0
        case STACK_INIT:
        {
            struct ble_stack_cfg cfg = {
                .private_addr = false,
                .controller_privacy = false,
            };
            dev_manager_stack_init(&cfg);
        }
        break;
    #endif
        case STACK_READY:
        {
            uint8_t macAddr[6];
            bool type;
            dev_manager_get_identity_bdaddr(macAddr,&type);
            dev_manager_add_service((struct svc_decl *)&honor_server_svc);
            dev_manager_add_service((struct svc_decl *)&normal_server_svc);
        }
        break;

        case SERVICE_ADDED:
        {
            
            if(0 == server_num)
            {
                server_num = 1;
                gatt_manager_svc_register(evt->service_added.start_hdl, HONOR_SVC_ATT_NUM, &honor_server_svc_env);
            }
            else if(server_num == 1)
            {
                server_num = 2;
                gatt_manager_svc_register(evt->service_added.start_hdl, BLE_SVC_ATT_NUM, &normal_server_svc_env);       // 服务添加步骤4：private server 服务信息注册
            }
            create_adv_obj();
        }
        break;

        case ADV_OBJ_CREATED:
        {
             LOG_I("ADV_OBJ_CREATED");
            LS_ASSERT(evt->obj_created.status == 0);
            adv_obj_hdl = evt->obj_created.handle;
            
            if(ble_hal_ctx.event_notify_callback){
                ble_hal_ctx.event_notify_callback(HONOR_BLE_STACK_EVENT_INTT_SUCCESS);
            }
        }
        break;
        case ADV_STARTED:                                    /*!< Adv started event*/ 
        {
            LOG_I("ADV_STARTED");
            _g_adv_start = 1;
            break;
        }
        case ADV_STOPPED:                                   /*!< Adv stopped event*/
        {
            LOG_I("ADV_STOPPED");
            _g_adv_start = 0;
            break;
        }
        case ADV_UPDATED: 
        {
            LOG_I("ADV_UPDATED");
            // honor_start_advertising();
            break;
        }
        //_g_adv_start


        default:
        break;
    }
}

/**
 * API to initialize ble stack.
 * @parma[in]  init  Bluetooth stack init parmaters.
 * @return     0 on success, error code if failure.
 */
int honor_ble_hal_gatt_stack_init(honor_ble_init_t *ble_init)
{
    // QM_RETURN_ON_FALSE(ble_init, -QM_EINVAL, LOG_TAG, "ble init inval!!");
    if(ble_init == NULL)
    {
        LOG_I("honor_ble_hal_gatt_stack_init error!");
        return (-1);
    }
    honor_server_svc.uuid = ble_init->service_uuid;
    honor_server_svc.att = honor_server_att_decl;
    honor_server_svc.nb_att = HONOR_SVC_ATT_NUM;
    honor_server_svc.uuid_len = UUID_LEN_128BIT;
    honor_server_att_decl[HONOR_SVC_IDX_RX_VAL].uuid = ble_init->honor_chara.uuid;

    normal_server_svc.uuid = ble_init->service_normal_uuid;
    normal_server_svc.att = normal_server_att_decl;
    normal_server_svc.nb_att = BLE_SVC_ATT_NUM;
    normal_server_svc.uuid_len = UUID_LEN_16BIT;
    normal_server_att_decl[BLE_SVC_IDX_CTRL_VAL].uuid = ble_init->normal_chara.uuid;

    ble_hal_ctx.pair_mode = ble_init->pair_mode;
    ble_hal_ctx.event_notify_callback = ble_init->event_notify_callback_t;
    ble_hal_ctx.txdone = ble_init->txdone;


    ble_hal_ctx.on_char_read = ble_init->honor_chara.on_char_read_t;
    ble_hal_ctx.on_char_write = ble_init->honor_chara.on_char_write_t;
    ble_hal_ctx.on_char_indicate = ble_init->honor_chara.on_char_indicate_t;
    ble_hal_ctx.on_char_notify = ble_init->honor_chara.on_char_notify_t;
    ble_hal_ctx.on_char_write_without_rsp = ble_init->honor_chara.on_char_write_without_rsp_t;


    ble_hal_ctx.on_char_read = ble_init->normal_chara.on_char_read_t;
    ble_hal_ctx.on_char_write = ble_init->normal_chara.on_char_write_t;
    ble_hal_ctx.on_char_indicate = ble_init->normal_chara.on_char_indicate_t;
    ble_hal_ctx.on_char_notify = ble_init->normal_chara.on_char_notify_t;
    ble_hal_ctx.on_char_write_without_rsp = ble_init->normal_chara.on_char_write_without_rsp_t;


#if 0
    dev_manager_init(dev_manager_callback);
#else
    dev_manager_cb_register(dev_manager_callback);
#endif
    gap_manager_init(gap_manager_callback); 
    gatt_manager_init(gatt_manager_callback);
    LOG_I("honor_ble_hal_gatt_stack_init ok!");
    return QM_EOK;  
}

/**
 * API to send data via OTA Indicate Characteristics.
 * @parma[in]  p_data  data buffer.
 * @parma[in]  length  data length.
 * @parma[in]  txdone  txdone callback.
 * @return     0 on success, erro code if failure.
 */
int honor_ble_hal_send_indication(uint8_t *p_data, uint16_t length, void (*txdone)(uint8_t res))
{
    // ble_hal_ctx.txdone = txdone;
    uint16_t handle = gatt_manager_get_svc_att_handle(&honor_server_svc_env, HONOR_SVC_IDX_RX_VAL);
    gatt_manager_server_send_indication(connect_id, handle, p_data, length, NULL);
    return QM_EOK;
}


int honor_ble_hal_normal_send_indication(uint8_t *p_data, uint16_t length, void (*txdone)(uint8_t res))
{
    // ble_hal_ctx.txdone = txdone;
    uint16_t handle = gatt_manager_get_svc_att_handle(&honor_server_svc_env, BLE_SVC_IDX_CTRL_VAL);
    gatt_manager_server_send_indication(connect_id, handle, p_data, length, NULL);
    return QM_EOK;
}

/**
 * API to disconnect BLE connection.
 * @param[in]  reason  the reason to disconnect the connection.
 */
void honor_ble_hal_disconnect(uint8_t reason)
{
    if(reason == 0){
        reason = 0x13;
    }
    gap_manager_disconnect(connect_id, reason);
}


void set_dev_name(uint8_t *name, uint8_t name_len)
{
    if(name == NULL || name_len == 0 || name_len >= DEV_NAME_MAX_LEN + 1) return;
    memset(g_scanRspData, 0, sizeof(g_scanRspData));
    g_scanRspData[0] = name_len + 1;
    g_scanRspData[1] = 0x09;
    memcpy(&g_scanRspData[2], name, name_len);
    g_scanRspDataLen = g_scanRspData[0] + 1;
}


void _honor_start_adv(void)
{
    LS_ASSERT(adv_obj_hdl != 0xff);
    if(adv_data_length == 0)
        adv_data_length = BLE_ADV_DATA_LEN;
    dev_manager_start_adv(adv_obj_hdl, g_advData, adv_data_length, g_scanRspData, g_scanRspDataLen);
    LOG_I("adv start");
    LOG_HEX(g_advData, adv_data_length);
    LOG_HEX(g_scanRspData, g_scanRspDataLen);
    LOG_I("adv_data_length = %d", adv_data_length);
}


int honor_start_advertising(void)
{
    LOG_I("_g_adv_start = %d", _g_adv_start);
    if(_g_adv_start)  return QM_EOK;
    if(adv_data_length == 0)
        adv_data_length = BLE_ADV_DATA_LEN;
   
    dev_manager_start_adv(adv_obj_hdl, g_advData, adv_data_length, 
                                    g_scanRspData, g_scanRspDataLen);

    return QM_EOK;
}

