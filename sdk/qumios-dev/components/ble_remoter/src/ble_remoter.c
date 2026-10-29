#include "qm.h"
#include "ble_remoter.h"

#include "qm_ble_gap.h"
#include "qm_utils_timer.h"
#include "qm_work.h"
#include "qm_kv.h"

#define RECV_PAIR_KV_TAG              "ble_rt"
#define RECV_DATA_MSG_DATA_TIMEOUT    (1 * 1000)
#define RECV_DATA_MSG_QUEUE_TIMEOUT   (10 * 1000)

#if !CONFIG_BLE_REMOTER_USE_ADV_SCAN_UNIT
#define BT_ADV_SCAN_UNIT(_ms) ((_ms) * 8 / 5)
#endif

#define RECV_FILTER_PAIRED_BYTE             (0xF0)
#define RECV_FILTER_IS_PAIRED               (0xF1)

#define RECV_DATA_TYPE_MANU_OFFSET          (1)
#define RECV_DATA_TYPE_MANU                 (0xFF)

#define RECV_DATA_MANU_CID_OFFSET           (2)
#define RECV_DATA_MANU_CID_NUM              (2)

#if CONFIG_BLE_REMOTER_USE_BIGENDIAN_PID
#define RECV_DATA_MANU_CID_1                (0xF0)
#define RECV_DATA_MANU_CID_2                (0x18)
#else
#define RECV_DATA_MANU_CID_1                (0x18)
#define RECV_DATA_MANU_CID_2                (0xF0)
#endif

#define RECV_DATA_MANU_VSR_SUBTYPE_OFFSET   (4)
#define RECV_DATA_MANU_VSR_SUBTYPE_NUM      (1)
#define RECV_DATA_MANU_VSR                  (0x03)

#define RECV_DATA_MANU_FMSK_OFFSET          (5)
#define RECV_DATA_MANU_FMSK_OFFSET_NUM      (1)

#define RECV_DATA_MANU_PID_OFFSET           (6)
#define RECV_DATA_MANU_PID_OFFSET_NUM       (2)

#define RECV_DATA_MANU_OPCODE_OFFSET        (8)
#define RECV_DATA_MANU_OPCODE_OFFSET_NUM    (1)

#define RECV_DATA_MANU_SNO_OFFSET           (9)
#define RECV_DATA_MANU_SNO_OFFSET_NUM       (1)

#define RECV_DATA_MANU_MAC_OFFSET           (10)
#define RECV_DATA_MANU_MAC_OFFSET_NUM       (6)

#define RECV_DATA_MANU_GROUPID_OFFSET       (16)
#define RECV_DATA_MANU_GROUPID_OFFSET_NUM   (1)

#define RECV_DATA_MANU_PARAM_OFFSET         (17)

#if CONFIG_BLE_REMOTER_MULTI_CONTROL_SUPPORT
#define CONFIG_BLE_REMOTER_PAIR_ADDR_NUM    (CONFIG_BLE_REMOTER_FILTER_MAX_NUM)
#else
#define CONFIG_BLE_REMOTER_PAIR_ADDR_NUM    (1)
#endif


typedef struct 
{
    uint8_t is_use;
    uint8_t sno;
    qm_utils_time_t list_timeout;
    qm_utils_time_t data_timeout;
    uint8_t ble_addr[BLE_REMOTER_ADRR_LEN];
    uint8_t recv_len;
    uint8_t recv_data[BLE_REMOTER_RECV_DATA_MAX_NUM];
}ble_remoter_filter_t;

typedef struct 
{
    uint16_t already_pair;
    uint8_t  ble_addr[BLE_REMOTER_ADRR_LEN];
}ble_remoter_pair_addr_handle_t;

typedef struct 
{
    int pair_last_index;
    ble_remoter_pair_addr_handle_t addr[CONFIG_BLE_REMOTER_PAIR_ADDR_NUM];
}ble_remoter_pair_addr_t;

typedef struct 
{
    int table_num;
    ble_remoter_cmd_table_t cmd_table[CONFIG_BLE_REMOTER_CMD_TABLE_MAX_NUM];
    ble_remoter_filter_t filter[CONFIG_BLE_REMOTER_FILTER_MAX_NUM];
    ble_remoter_params_t params;
    qm_utils_time_t pair_timeout;
    ble_remoter_pair_addr_t  pair_addr;
}ble_remoter_handle_t;

typedef int (*ble_remoter_recv_t)(ble_remoter_handle_t *bttr_handle, ble_remoter_filter_t *filter);

static int ble_remoter_pair_recv(ble_remoter_handle_t *bttr_handle, ble_remoter_filter_t *filter);
static int ble_remoter_comm_recv(ble_remoter_handle_t *bttr_handle, ble_remoter_filter_t *filter);
static int ble_remoter_factory_recv(ble_remoter_handle_t *bttr_handle, ble_remoter_filter_t *filter);

static ble_remoter_handle_t  g_bttr_handle = {0};
static ble_remoter_recv_t g_remote_table[BLE_REMOTE_CMD_TYPE_MAX] = 
{
    [BLE_REMOTE_CMD_TYPE_PAIR]      = ble_remoter_pair_recv,
    [BLE_REMOTE_CMD_TYPE_COMM]      = ble_remoter_comm_recv,
    [BLE_REMOTE_CMD_TYPE_FACTORY]   = ble_remoter_factory_recv,
};

static int32_t ble_scan_header_mattch(uint8_t *recv_buff, int recv_length)
{
    ble_remoter_handle_t *bttr_handle = &g_bttr_handle;
    ble_remoter_comm_t *comm_param = &bttr_handle->params.comm_param;
    if(recv_buff == NULL || recv_length < BLE_REMOTER_RECV_DATA_MIN_NUM){
        return -QM_EINVAL;
    }

    if(recv_buff[RECV_DATA_TYPE_MANU_OFFSET] != RECV_DATA_TYPE_MANU){
        return -QM_EINVAL;
    }

    if( recv_buff[RECV_DATA_MANU_CID_OFFSET]   != RECV_DATA_MANU_CID_1 ||
        recv_buff[RECV_DATA_MANU_CID_OFFSET+1] != RECV_DATA_MANU_CID_2 ){
        return -QM_EINVAL;
    }

    if(recv_buff[RECV_DATA_MANU_PID_OFFSET]  != comm_param->pid){
        return -QM_EINVAL;
    }

    if(recv_buff[RECV_DATA_MANU_VSR_SUBTYPE_OFFSET]  != 
        (RECV_DATA_MANU_VSR | (comm_param->subtype << 4))){
        return -QM_EINVAL;
    }

    if(recv_buff[RECV_DATA_MANU_FMSK_OFFSET]  != comm_param->fmsk){
        return -QM_EINVAL;
    }

    return QM_EOK;
}

static ble_remoter_filter_t *ble_scan_msg_merge(uint8_t *recv_buff, int recv_length)
{
    int index = 0;
    uint32_t timeout = 0xFFFFFFF;
    ble_remoter_filter_t *data_msg = NULL;
    ble_remoter_handle_t *bttr_handle = &g_bttr_handle;
    ble_remoter_filter_t *last_data_msg = NULL; //最早使用的遥控器列表;

    if(recv_buff == NULL || recv_length < BLE_REMOTER_RECV_DATA_MIN_NUM){
        return NULL;
    }

    //寻找已存在的遥控器列表
    for (index = 0; index < QM_ARRAY_SIZE(bttr_handle->filter); index++)
    {
        data_msg = &bttr_handle->filter[index];
        if(data_msg->is_use == QM_FALSE){
            continue;
        }

        if(timeout > qm_utils_time_left(&data_msg->list_timeout)){
            timeout = qm_utils_time_left(&data_msg->list_timeout);
            last_data_msg = data_msg;
        }

        if(memcmp(&data_msg->ble_addr[0], &recv_buff[RECV_DATA_MANU_MAC_OFFSET], RECV_DATA_MANU_MAC_OFFSET_NUM)){
            continue;
        }

        if(data_msg->sno == recv_buff[RECV_DATA_MANU_SNO_OFFSET]){
            if(qm_utils_time_left(&data_msg->data_timeout)){
                return NULL;
            }
        }

        last_data_msg = data_msg;
        goto __copy;
    }

    //寻找未被使用的遥控器列表
    for (index = 0; index < QM_ARRAY_SIZE(bttr_handle->filter); index++)
    {
        data_msg = &bttr_handle->filter[index];
        if(data_msg->is_use != QM_FALSE){
            continue;
        }
    
        last_data_msg = data_msg;
        data_msg->is_use = QM_TRUE;
    }

__copy:
    if(last_data_msg == NULL){
        return NULL;
    }

    last_data_msg->sno =  recv_buff[RECV_DATA_MANU_SNO_OFFSET];
    qm_utils_time_countdown_ms(&data_msg->data_timeout, RECV_DATA_MSG_DATA_TIMEOUT);
    qm_utils_time_countdown_ms(&data_msg->list_timeout, RECV_DATA_MSG_QUEUE_TIMEOUT);
    memcpy(&data_msg->ble_addr[0], &recv_buff[RECV_DATA_MANU_MAC_OFFSET], RECV_DATA_MANU_MAC_OFFSET_NUM);

    data_msg->recv_len = recv_length;
    memcpy(&data_msg->recv_data[0], &recv_buff[0], recv_length);

    return last_data_msg;
}

static int ble_remoter_pair_recv(ble_remoter_handle_t *bttr_handle, ble_remoter_filter_t *filter)
{
    int index = 0;
    ble_remoter_pair_t *pair_param = NULL;
    ble_remoter_pair_addr_t *pair_addr = NULL;
    ble_remoter_pair_addr_handle_t *pair_handle = NULL;

    if(bttr_handle == NULL || filter == NULL){
        return -QM_EINVAL;
    }

    pair_param = &bttr_handle->params.pair_param;
    if(!pair_param->enable){
        return -QM_EINVAL;
    }

    if(!qm_utils_time_left(&bttr_handle->pair_timeout)){
        pair_param->enable = QM_FALSE;
        return -QM_ETIMEOUT;
    }
    
    pair_addr = &bttr_handle->pair_addr;
    for(index = 0; index < CONFIG_BLE_REMOTER_PAIR_ADDR_NUM; index++){

        if(!pair_addr->addr[index].already_pair){
            pair_handle = &pair_addr->addr[index];
            break;
        }

    }

    if(pair_handle == NULL){
        index = ((pair_addr->pair_last_index + 1) % CONFIG_BLE_REMOTER_PAIR_ADDR_NUM);
        pair_handle = &pair_addr->addr[index];
    }

    pair_addr->pair_last_index = index;
    pair_handle->already_pair = QM_TRUE;
    memcpy(&pair_handle->ble_addr[0], &filter->ble_addr[0], BLE_REMOTER_ADRR_LEN);

    qm_kv_set(RECV_PAIR_KV_TAG, pair_addr, sizeof(ble_remoter_pair_addr_t), 1);
    return QM_EOK;
}

static int ble_remoter_comm_recv(ble_remoter_handle_t *bttr_handle, ble_remoter_filter_t *filter)
{
    int index = 0;
    ble_remoter_pair_addr_t *pair_addr = NULL;
    ble_remoter_pair_addr_handle_t *pair_handle = NULL;

    if(bttr_handle == NULL || filter == NULL){
        return -QM_EINVAL;
    }

    pair_addr = &bttr_handle->pair_addr;

    for(index = 0; index < CONFIG_BLE_REMOTER_PAIR_ADDR_NUM; index++){
        
        if(pair_addr->addr[index].already_pair &&
            !memcmp(&pair_addr->addr[index].ble_addr[0], &filter->ble_addr[0], BLE_REMOTER_ADRR_LEN)){
            pair_handle = &pair_addr->addr[index];
            break;
        }
    }
    
    if(pair_handle == NULL){
        return -QM_EINVAL;
    }

    return QM_EOK;
}

static int ble_remoter_factory_recv(ble_remoter_handle_t *bttr_handle, ble_remoter_filter_t *filter)
{
    ble_remoter_factory_t *factory_param = NULL;
    if(bttr_handle == NULL || filter == NULL){
        return -QM_EINVAL;
    }

    factory_param = &bttr_handle->params.factory_param;
    if(!factory_param->enable){
        return -QM_EINVAL;
    }

    if(memcmp(&factory_param->fac_addr[0], &filter->ble_addr[0], BLE_REMOTER_ADRR_LEN)){
        return -QM_EIO;
    }

    return QM_EOK;
}

static int32_t ble_scan_read(uint8_t mac[6], uint8_t *recv_buff, int recv_length)
{
    int index = 0;
    int32_t ret = QM_EOK;
    ble_remoter_filter_t *msg_info = NULL;
    ble_remoter_cmd_table_t *cmd_table = NULL;
    ble_remoter_payload_t remote_payload = {0};
    ble_remoter_handle_t *bttr_handle = &g_bttr_handle;

    if(recv_buff == NULL || recv_length < BLE_REMOTER_RECV_DATA_MIN_NUM){
        return -QM_EINVAL;
    }

    ret = ble_scan_header_mattch(recv_buff, recv_length);
    if(ret != QM_EOK){
        return ret;
    }

    msg_info = ble_scan_msg_merge(recv_buff, recv_length);
    if(msg_info == NULL){
        return -QM_EINVAL;
    }

    QM_HEX_LOGD("1", "BEACON ", recv_buff, recv_length);
    
    for(index = 0; index < bttr_handle->table_num; index++){

        cmd_table = &bttr_handle->cmd_table[index];

        if(cmd_table->opcode && 
            cmd_table->opcode != msg_info->recv_data[RECV_DATA_MANU_OPCODE_OFFSET]){
            continue;
        }

        ret = g_remote_table[cmd_table->cmd_type](bttr_handle, msg_info);
        if(ret != QM_EOK || cmd_table->recv == NULL){
            continue;
        }

        remote_payload.group_id = msg_info->recv_data[RECV_DATA_MANU_GROUPID_OFFSET];
        remote_payload.param_len = msg_info->recv_len - BLE_REMOTER_RECV_DATA_MIN_NUM;
        memcpy(&remote_payload.ble_addr[0], &msg_info->ble_addr[0], BLE_REMOTER_ADRR_LEN);
        if(remote_payload.param_len){
            memcpy(&remote_payload.param[0], &msg_info->recv_data[RECV_DATA_MANU_PARAM_OFFSET], remote_payload.param_len);
        }
        
        cmd_table->recv(cmd_table->cmd_type, msg_info->recv_data[RECV_DATA_MANU_OPCODE_OFFSET], &remote_payload);
    }

    return QM_EOK;
}

static void qm_gap_ble_callback(qm_gap_ble_cb_event_t event, qm_ble_gap_cb_param_t *param)
{
    switch (event)
    {

        case QM_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
            qm_ble_gap_start_scanning(0);
        break; 
        case QM_GAP_BLE_SCAN_RESULT_EVT:
        {
            ble_scan_read(param->scan_rst.bda, 
                        param->scan_rst.ble_adv,
                        param->scan_rst.adv_data_len);
        }
        break;

        default:
        
        break;
    }
}

int32_t ble_remoter_deinit(void)
{
    
    return QM_EOK;
}

int32_t ble_remoter_init(ble_remoter_params_t *params, ble_remoter_cmd_table_t *cmd_params, int table_num)
{
    int kv_len = sizeof(ble_remoter_pair_addr_t);
    ble_remoter_pair_t *pair_param = NULL;
    ble_remoter_handle_t *bttr_handle = &g_bttr_handle;
    if(params == NULL || cmd_params == NULL || table_num == 0){
        return -QM_EINVAL;
    }

    qm_ble_scan_params_t scan_param = {
        .scan_type = QM_BLE_SCAN_TYPE_PASSIVE,
        .scan_window = CONFIG_BLE_REMOTER_SCAN_WINDOW,
        .scan_interval = CONFIG_BLE_REMOTER_SCAN_INTERVAL,
    };

    if(table_num > CONFIG_BLE_REMOTER_CMD_TABLE_MAX_NUM){
        return -QM_EINVAL;
    }

#if !CONFIG_BLE_REMOTER_USE_ADV_SCAN_UNIT
    scan_param.scan_window = BT_ADV_SCAN_UNIT(scan_param.scan_window);
    scan_param.scan_interval = BT_ADV_SCAN_UNIT(scan_param.scan_interval);
#endif

    memset(bttr_handle, 0, sizeof(ble_remoter_handle_t));
    memcpy(&bttr_handle->params, params, sizeof(ble_remoter_params_t));
    bttr_handle->table_num = table_num;
    memcpy(&bttr_handle->cmd_table, cmd_params, sizeof(ble_remoter_cmd_table_t) * table_num);

    pair_param = &bttr_handle->params.pair_param;
    qm_utils_time_countdown_ms(&bttr_handle->pair_timeout, (uint32_t)pair_param->pair_window);

    qm_kv_get(RECV_PAIR_KV_TAG, &bttr_handle->pair_addr, &kv_len);

    qm_ble_gap_register_callback(qm_gap_ble_callback);
    qm_ble_gap_set_scan_params(&scan_param);

    return QM_EOK;
}

int32_t ble_remoter_reset(void)
{
    ble_remoter_handle_t *bttr_handle = &g_bttr_handle;
    ble_remoter_pair_addr_t *pair_addr = &bttr_handle->pair_addr;
    ble_remoter_pair_t *pair_param = &bttr_handle->params.pair_param;

    memset(pair_addr, 0, sizeof(ble_remoter_pair_addr_t));
    qm_kv_set(RECV_PAIR_KV_TAG, pair_addr, sizeof(ble_remoter_pair_addr_t), 1);

    pair_param->enable = QM_TRUE;
    qm_utils_time_countdown_ms(&bttr_handle->pair_timeout, (uint32_t)pair_param->pair_window);

    return QM_EOK;
}

int32_t ble_remoter_pair_enable(void)
{
    ble_remoter_handle_t *bttr_handle = &g_bttr_handle;
    ble_remoter_pair_t *pair_param = &bttr_handle->params.pair_param;

    pair_param->enable = QM_TRUE;
    qm_utils_time_countdown_ms(&bttr_handle->pair_timeout, (uint32_t)pair_param->pair_window);
    return QM_EOK;
}

int32_t ble_remoter_pair_disable(void)
{
    ble_remoter_handle_t *bttr_handle = &g_bttr_handle;
    ble_remoter_pair_t *pair_param = &bttr_handle->params.pair_param;

    pair_param->enable = QM_FALSE;

    return QM_EOK;
}

int32_t ble_remoter_factory_enable(void)
{
    ble_remoter_handle_t *bttr_handle = &g_bttr_handle;
    ble_remoter_factory_t *factory_param = &bttr_handle->params.factory_param;

    factory_param->enable = QM_TRUE;
    return QM_EOK;
}

int32_t ble_remoter_factory_disable(void)
{
    ble_remoter_handle_t *bttr_handle = &g_bttr_handle;
    ble_remoter_factory_t *factory_param = &bttr_handle->params.factory_param;

    factory_param->enable = QM_FALSE;

    return QM_EOK;
}