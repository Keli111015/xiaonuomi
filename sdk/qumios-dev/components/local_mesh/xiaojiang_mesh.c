#include "stdint.h"
#include "miio_user_api.h"
#include "qm.h"
#include "qm_log.h"
#include "qm_kv.h"
#include "xiaojiang_mesh.h"
#if PLATFORM_09P
#  include "native_gecko.h"
#elif PLATFORM_05P
#  include "gap_scheduler.h"
#  include "platform_diagnose.h"
#elif PLATFORM_12G || PLATFORM_12S
#  include "bluetooth/bluetooth.h"
#  include "bluetooth/conn.h"
#  include "bluetooth/host/hci_core.h"
#endif
#include "local_mesh_port.h"

//TODO:remove
#include <sys/printk.h>

// #define LOG_TAG "xjmesh"
// #define XJMESH_ENABLE_RELAY 1
//advertise implementation

#define BT_ADV_SCAN_UNIT(_ms) ((_ms) * 8 / 5)

#define INVALID_CHANNEL_ID (0xFF)



static bool _is_group_addr_sub(uint16_t group_addr);
static bool _is_null_net_id(xjmesh_net_id_t *net_id);
static uint32_t _g_pair_last_time = 0;

static int xiaojiang_mesh_start_pair(int channel, int count);
static int xiaojiang_mesh_stop_pair(bool force);
int xiaojiang_mesh_unpair(int channel);

extern uint8_t advertising;

static uint8_t _mac_num = 0; //记录已经保存的mac个数
static uint8_t _pair_flag = 0; //记录是否正在配网中, 只有配网中的设备才会处理配网指令
static uint8_t _pair_channel = INVALID_CHANNEL_ID; //正在配网的channel，0表示无效
static uint8_t _pair_status = 0;
static uint8_t _pair_count = 0;
static uint8_t _send_pair_req_cnt = 0; 
//自己的seq，默认是0
static uint16_t _my_seq = 0;
static int _mesh_dev_type = 0;

static struct bt_le_ext_adv *g_adv = NULL;
// static struct bt_le_ext_adv *g_retran_adv = NULL;
#if 0 //XJMESH_ENABLE_RELAY
static bool _g_retrans_enabled = false;
#endif
static void * _transmit_timer = NULL;
static void * _pair_timer = NULL;
#if PLATFORM_09P
static xjmesh_timer_context_t _timer_context = {0};
#endif
static uint8_t _my_mac[8];
static uint8_t _cur_pair_mac_ch[8]; //记录当前配对的mac地址和channel, 0~5 是mac地址，6是channel
static uint16_t _group_addr_list[XJMESH_MAX_GROUP_ADDR_NUM] = {0};

//缓存已经重发的消息，避免重复转发。当缓冲区满时，丢弃最早的消息
static xjmesh_message_t _adv_msg;


#if 1 //XJMESH_ENABLE_RELAY
static xjmesh_message_t _retrans_msg_list[XJMESH_MAX_RERANS_NUM] = {0};
#endif

static xjmesh_mac_seq_list_t _mac_seq_list;
static xjmesh_net_id_t _net_ids[XJMESH_MAX_NET_ID_NUM] = {0};


static int _net_cmd_process(xjmesh_message_t *msg);
static bool _is_same_net_id(xjmesh_net_id_t *net_id_1, xjmesh_net_id_t *net_id_2);

int xiaojiang_mesh_send_message(int opcode, int attr_id, int flag, xjmesh_addr_t *dst_addr, uint8_t *data, int len, int channel)
{
    xjmesh_message_t msg;
    memset(&msg, 0, sizeof(xjmesh_message_t));
    msg.ttl = XJMESH_DEFAULT_TTL;
    msg.flag = flag & 0x1F;

    msg.opcode = opcode;
    msg.attr_id = attr_id;
    msg.len = len;

    memcpy(&msg.dst_addr, dst_addr, sizeof(xjmesh_addr_t));
    memcpy(&msg.src_addr, &_my_mac, sizeof(xjmesh_addr_t));

    if (data && len > 0 && len < XJMESH_MAX_DATA_LEN)
    {
        //数据长度不能超过XJMESH_MAX_DATA_LEN
        memcpy(msg.data, data, len);
    }

    //如果本地channel中，有相同的net_id, 则上报
    if (!_is_null_net_id(&_net_ids[channel]))
    {
        if (channel != INVALID_CHANNEL_ID && channel < XJMESH_MAX_NET_ID_NUM)
        {
            for (int i = 0; i < XJMESH_MAX_NET_ID_NUM; i++)
            {
                if (i != channel && _is_same_net_id(&_net_ids[i], &msg.dst_addr.net_id))
                {
                    localmesh_post_recv_mutal_msg_event(i, &msg, true, false);
                }
            }
        }
    }

    //post event
    return localmesh_post_send_mutal_msg_event(channel, &msg, true);
}

int xiaojiang_mesh_send_group_messge(int opcode, int attr_id, int group_addr, uint8_t *data, int len)
{
    xjmesh_addr_t dst_addr;
    memset(&dst_addr, 0, sizeof(xjmesh_addr_t));
    dst_addr.group.addr = group_addr;

    return xiaojiang_mesh_send_message(opcode, attr_id, XJMESH_FLAG_GROUP_ADDR, &dst_addr, data, len, INVALID_CHANNEL_ID);
}

int xiaojiang_mesh_send_subnet_message(int opcode, int attr_id, int channel, uint8_t *data, int len)
{
    if (channel < 0 || channel > XJMESH_MAX_NET_ID_NUM)
    {
        return -1;
    }

    //开关类的消息，必须配对后才能发送
    if ((attr_id == XJMESH_ATTR_SWITCH_STATE) && _is_null_net_id(&_net_ids[channel]))
    {
        return -1;
    }

    xjmesh_addr_t dst_addr;
    memcpy(&dst_addr.net_id, &_net_ids[channel], sizeof(xjmesh_net_id_t));

    return xiaojiang_mesh_send_message(opcode, attr_id, XJMESH_FLAG_NET_ADDR, &dst_addr, data, len, channel);
}

int xiaojiang_mesh_send_all_subnet_message(int opcode, int attr_id, uint8_t *data, int len)
{
    for (int i = 0; i < XJMESH_MAX_NET_ID_NUM; i++)
    {
        if (!_is_null_net_id(&_net_ids[i]))
        {
            xjmesh_addr_t dst_addr;
            memcpy(&dst_addr.net_id, &_net_ids[i], sizeof(xjmesh_net_id_t));
            xiaojiang_mesh_send_message(opcode, attr_id, XJMESH_FLAG_NET_ADDR, &dst_addr, data, len, i);
        }
    }
    return 0;
}

//根据mac来过滤
bool _mac_seq_filter(uint8_t *mac, uint16_t seq)
{
    //1. 先判断mac和seq是否存在，如果存在，则返回true，表示需要过滤
    //2. 如果不存在，则保存mac和seq，并返回false，表示不需要过滤
    //3. 如果满了，则丢弃最早的消息，保存新的mac和seq，并返回false，表示不需要过滤。根据index来确定哪个最老
    int cur_seq = 0;
    for (int i = 0; i < MAC_MAX_LEN * MAC_SEQ_MAX_NUM; i++)
    {
        cur_seq = _mac_seq_list.list[i].mac_seq[6] | (_mac_seq_list.list[i].mac_seq[7] << 8);
        if (memcmp(mac, _mac_seq_list.list[i].mac_seq, 6) == 0 && seq == cur_seq)
        {
            return true; //需要过滤
        }
    }
    //save 
    memcpy(_mac_seq_list.list[_mac_seq_list.index].mac_seq, mac, 6);
    _mac_seq_list.list[_mac_seq_list.index].mac_seq[6] = seq & 0xFF;
    _mac_seq_list.list[_mac_seq_list.index].mac_seq[7] = (seq >> 8) & 0xFF;
    _mac_seq_list.index++;
    if (_mac_seq_list.index >= MAC_MAX_LEN * MAC_SEQ_MAX_NUM)
    {
        _mac_seq_list.index = 0; //重置索引
    }
    return false;
}

#if 0
//寻找mac，找到了就返回mac所在位置，未找到就返回-1
int8_t _find_mac(uint8_t *mac)
{
    uint8_t mac_temp[0] = {0};
    if(mac == NULL)
    {
        return (-1);
    }

    for(int i = 0; i <= MAC_MAX_LEN; i++)
    {
        if(memcmp(mac, _mac_seq[i].mac, 6) == 0)
        {
            return i;
        }
    }
    return (-1);
}

//保存mac，
void _save_mac_seq(uint8_t *mac, uint16_t seq)
{
    if(mac == NULL)
    {
        return (-1);
    }

    memcpy(_mac_seq[_mac_num].mac, mac, 6);
    _mac_seq[_mac_num].seq = seq;

    if(_mac_num >= 16)
    {
        _mac_num = 0;
    }
    else
    {
        _mac_num++;
    }
}


//保存了mac才能保存seq
uint16_t _get_seq(uint8_t num)
{
   return _mac_seq[num].seq;
}

void _set_seq(uint8_t num, uint16_t seq)
{
   _mac_seq[num].seq = seq;
}
#endif


#if  1 //XJMESH_ENABLE_RELAY

/**
 * @brief 获取当前重发缓冲区的写入index
 * 
 * @return int  [0, XJMESH_MAX_RERANS_NUM)
 */
static int _get_retrans_list_index(void)
{
    static uint8_t _index = 0;
    int ret = _index;

    _index++;
    if (_index >= XJMESH_MAX_RERANS_NUM)
    {
        _index = 0;
    }
    return ret;
}


/**
 * @brief 判断消息是否已经在重发列表中
 * 
 * @param pmsg 
 * @return true 消息已经重发过，则不会再次重发
 * @return false 消息没重发;或者已经重发但由于缓冲区满而被丢弃,则会导致消息再次被重发
 */
static bool _is_msg_in_retrans_list(xjmesh_message_t *pmsg)
{
    uint8_t null_mac[6] = {0};
    for (int i = 0; i < XJMESH_MAX_RERANS_NUM; i++)
    {
        //判断是否为空消息
        if ((memcmp(_retrans_msg_list[i].src_addr.mac, null_mac, 6) == 0) && (memcmp(_retrans_msg_list[i].dst_addr.mac, null_mac, 6) == 0))
        {
            //空消息，不用判断
            continue;
        }
        if (_retrans_msg_list[i].opcode == pmsg->opcode
            && _retrans_msg_list[i].attr_id == pmsg->attr_id
            && (memcmp(_retrans_msg_list[i].src_addr.mac, pmsg->src_addr.mac, 6) == 0)
            && (memcmp(_retrans_msg_list[i].dst_addr.mac, pmsg->dst_addr.mac, 6) == 0)
            && (memcmp(_retrans_msg_list[i].data, pmsg->data, pmsg->len) == 0))
        {
            if (_retrans_msg_list[i].ttl > 0)
            {
                return true;
            }
            else
            {
                return false;
            }
        }
    }
    return false;
}

/**
 * @brief 从_retrans_msg_list中获取一个待发送的消息，根据ttl是否为0，来判断消息是否有效
 * 
 * @return xjmesh_message_t* 
 */
static xjmesh_message_t * _get_msg_from_retrans_list(void)
{
    for (int i = 0; i < XJMESH_MAX_RERANS_NUM; i++)
    {
        if (_retrans_msg_list[i].ttl > 0)
        {
            syslog(LOG_INFO, "##%d", i);
            return &_retrans_msg_list[i];
        }
    }

    //未找到
    return NULL;
}
#endif

#if PLATFORM_09P || PLATFORM_12G || PLATFORM_12S
static int _xiaojiang_mesh_timer_cb(void * p_context)
{
#if PLATFORM_09P
    //超时关闭广播
    if (_timer_context.adv_time > 0)
    {
        _timer_context.adv_time -= 100;
        if (_timer_context.adv_time == 0)
        {
            _adv_stop(XJ_ADV_HANDLE);
            #ifdef LOG_TAG
            QM_LOGI(LOG_TAG, "stop adv");
            #endif
        }
    }
    if (_timer_context.retrans_time > 0)
    {
        _timer_context.retrans_time -= 100;
        if (_timer_context.retrans_time == 0)
        {
            _adv_stop(XJ_ADV_HANDLE_RETRANS);
            #ifdef LOG_TAG
            QM_LOGD("stop retrans adv\n");
            #endif
        }
    }
#else
    
    //platform 12s
    // 先检测g_adv是否NULL，如果是，则创建；否则需要检查广播是否正在进行，如果正在进行则停止广播
    int err;
    if (g_adv == NULL) 
    {
        struct bt_le_adv_param adv_param = {
            .interval_max = BT_ADV_SCAN_UNIT(XJMESH_ADV_INTERVAL_MAX),
            .interval_min = BT_ADV_SCAN_UNIT(XJMESH_ADV_INTERVAL_MIN),
            .options = BT_LE_ADV_OPT_USE_IDENTITY,
        };
        err = bt_le_ext_adv_create(&adv_param, NULL, &g_adv);
        if (err) {
            #ifdef LOG_TAG
            QM_LOGD(LOG_TAG, "Failed to create adv (err %d)\n", err);
            #endif
            return err;
        }
    }
    else if (atomic_test_bit(g_adv->flags, BT_ADV_ENABLED)) 
    {
        //停止广播
        // bt_le_ext_adv_stop(g_adv); 
        return -EBUSY;
    }
    xjmesh_message_t *pmsg = NULL;
    //检查广播数据和中继数据，如果为空，则退出；广播数据优先级高于中继数据
    if (_adv_msg.ttl != 0)
    {
        //广播数据不为空
        pmsg = &_adv_msg;
    }
    else 
    {
        //广播数据为空，检查中继数据
        pmsg = _get_msg_from_retrans_list();
        if (pmsg == NULL)
        {
            //中继数据也为空
            // miio_timer_stop(_transmit_timer);
            // syslog(LOG_INFO, "!!");
            return 0;
        }
    }

    struct bt_data ad = {0};
    uint8_t pbuffer[32] = {0};

    int msg_len = pmsg->len + MEMBER_OFFSET(xjmesh_message_t, data);
    pbuffer[0] = XJMESH_PID & 0xFF;
    pbuffer[1] = (XJMESH_PID >> 8) & 0xFF;
    pbuffer[2] = 0x77;
    memcpy(&pbuffer[3], pmsg, msg_len);
    ad.type = BT_DATA_MANUFACTURER_DATA;
    ad.data_len = msg_len + 3;
    ad.data = pbuffer;
    static struct bt_le_ext_adv_start_param start = {.num_events = XJMESH_ADV_PERIOD / XJMESH_ADV_INTERVAL_MIN};
    err = bt_le_ext_adv_set_data(g_adv, &ad, 1, NULL, 0);
    if (err) 
    {
        #ifdef LOG_TAG
        QM_LOGD(LOG_TAG, "Failed to set adv data (err %d)\n", err);
        #endif
        return err;
    }
    err = bt_le_ext_adv_start(g_adv, &start);
    if (err) 
    {
        #ifdef LOG_TAG
        QM_LOGD(LOG_TAG, "Failed to start adv (err %d)\n", err);
        #endif
        return err;
    }
    // QM_LOGD(LOG_TAG, "_xiaojiang_mesh_timer_cb end\n");
    pmsg->ttl = 0;
#endif
    return 0;
}
#endif

void xiaojiang_mesh_init(int type)
{
    _mesh_dev_type = type;
    xiaojiang_mesh_subscribe(XJMESH_GROUP_ADDR);
#if PLATFORM_09P
    mible_gap_address_get(_my_mac);
    //create timer
    int ret = miio_timer_create(&_retrans_timer, _xiaojiang_mesh_timer_cb, MIBLE_TIMER_REPEATED);
    if (ret != MI_SUCCESS)
    {
        #ifdef LOG_TAG
        QM_LOGD(LOG_TAG, "xiaojiang_mesh_init:create timer failed\n");
        #endif
        return ;
    }
    ret = miio_timer_start(_retrans_timer, 100, NULL);
    if (ret != MI_SUCCESS)
    {
        #ifdef LOG_TAG
        QM_LOGD("xiaojiang_mesh_init:start timer failed\n");
        #endif
        return ;
    }
#elif PLATFORM_05P
    //do nothing
    mible_gap_address_get(_my_mac);
#elif PLATFORM_12G || PLATFORM_12S
    //get mac
    size_t count = {1};
    bt_addr_le_t addrs = {0}; 
    bt_id_get(&addrs, &count);
    memcpy(_my_mac, addrs.a.val, 6);
//     #if SUPPORT_V1 && MESH_DEVICE_TYPE == MESH_DEVICE_TYPE_SWITCH
//     {
//         //为了兼容1.0版本的协议，将1.0版本的绑定信息转换为2.0版本的绑定信息
//         localmesh_param_t_v1 *mutual_param = (localmesh_param_t_v1*)LOCALMESH_STORAGE_GET_VALUE_PTR(STORAGE_ID_MUTUAL_BOUND_INFO);
//         xjmesh_net_id_t *g_net_id = (xjmesh_net_id_t*)LOCALMESH_STORAGE_GET_VALUE_PTR(STORAGE_ID_NET_ID);
//         if (mutual_param != NULL)
//         {
//             //mutual_param->bound_info 都为0，则没有绑定信息
//             uint8_t null_mac[6] = {0};
//             int save_flag = 0;
//             for (int i = 0; i < 4; i++)
//             {
//                 for (int j = 0; j < 8; j++)
//                 {
//                     if (memcmp(mutual_param->bound_info[i][j].mac, null_mac, 6) != 0)
//                     {
//                         save_flag = 1;
//                         //有绑定信息
//                         xjmesh_net_id_t net_id = {0};
//                         net_id.a_id[0] = _my_mac[1];
//                         net_id.a_id[1] = _my_mac[0];
//                         net_id.a_id[2] = i;
//                         net_id.b_id[0] = mutual_param->bound_info[i][j].mac[1];
//                         net_id.b_id[1] = mutual_param->bound_info[i][j].mac[0];
//                         net_id.b_id[2] = mutual_param->bound_info[i][j].key_num;
//                         printk("old bound: %02x:%02x:%02x:%02x:%02x:%02x =>", mutual_param->bound_info[i][j].mac[5], mutual_param->bound_info[i][j].mac[4], mutual_param->bound_info[i][j].mac[3], mutual_param->bound_info[i][j].mac[2], mutual_param->bound_info[i][j].mac[1], mutual_param->bound_info[i][j].mac[0]);
//                         printk("  %02x:%02x:%02x:%02x:%02x:%02x\n", net_id.a_id[0], net_id.a_id[1], net_id.a_id[2], net_id.b_id[0], net_id.b_id[1], net_id.b_id[2]);
//                         //found a empty slot
//                         {
//                             int k = 4;
//                             for (k = 4; k < XJMESH_MAX_NET_ID_NUM; k++)
//                             {
//                                 if (_is_null_net_id(&g_net_id[k]))
//                                 {
//                                     memcpy(&g_net_id[k], &net_id, sizeof(xjmesh_net_id_t));
//                                     break;
//                                 }
//                             }
//                             if (k >= XJMESH_MAX_NET_ID_NUM)
//                             {
//                                 //没有找到空位
//                                 break;
//                             }
//                         }
//                         //clear the bound info
//                         memset(&mutual_param->bound_info[i][j], 0, sizeof(xjmesh_mac_seq_t));
//                     }
//                 }
//             }
//             if (save_flag)
//             {
//                 //save the net id
//                 localmesh_post_storage_save_id_event(STORAGE_ID_MUTUAL_BOUND_INFO, false);
//                 localmesh_post_storage_save_id_event(STORAGE_ID_NET_ID, false);
//             }
//         }
//     }
//     #endif
#endif

    //init _net_ids
    int len = sizeof(_net_ids);
    memset(&_net_ids, 0xFF, sizeof(_net_ids));
    qm_kv_get(XJMESH_NET_ID_KEY_STR, &_net_ids, &len);
    {
        for (int i = 0; i < XJMESH_MAX_NET_ID_NUM; i++)
        {
            syslog(LOG_INFO, "net_id[%d]: %02x:%02x:%02x:%02x:%02x:%02x\n", i, _net_ids[i].a_id[0], _net_ids[i].a_id[1], _net_ids[i].a_id[2], _net_ids[i].b_id[0], _net_ids[i].b_id[1], _net_ids[i].b_id[2]);
        }
    }

    #ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "my mac: %02x:%02x:%02x:%02x:%02x:%02x\n", _my_mac[5], _my_mac[4], _my_mac[3], _my_mac[2], _my_mac[1], _my_mac[0]);
    #endif
    syslog(LOG_INFO, "my mac: %02x:%02x:%02x:%02x:%02x:%02x\n", _my_mac[5], _my_mac[4], _my_mac[3], _my_mac[2], _my_mac[1], _my_mac[0]);
}


#if PLATFORM_09P
/**
 * @brief 开启特定广播
 * 
 * @param handle 
 * @param interval_min: 最小广播时间，单位ms 
 * @param interval_max  最大广播时间，单位ms
 * @return int 
 */
int _my_adv_start(int handle, int interval_min, int interval_max)
{
    int result = gecko_cmd_le_gap_set_advertise_timing(handle, interval_min, interval_max, 0, 0)->result;
    MI_ERR_CHECK(result);
    if (result == bg_err_invalid_param) {
        return MI_ERR_INVALID_PARAM;
    }

    result = gecko_cmd_le_gap_set_advertise_channel_map(handle, 7)->result;
    MI_ERR_CHECK(result);
    if (result == bg_err_invalid_param) {
        return MI_ERR_INVALID_PARAM;
    }
    result = gecko_cmd_le_gap_start_advertising(handle, le_gap_user_data, le_gap_non_connectable)->result;
    MI_ERR_CHECK(result);

    return result;
}

int _adv_stop(int handle)
{
    int result = gecko_cmd_le_gap_stop_advertising(handle)->result;
    MI_ERR_CHECK(result);
    return result;
}


int _my_gap_adv_data_set(uint8_t const * p_data, uint8_t dlen,
        uint8_t const *p_sr_data, uint8_t srdlen)
{
    struct gecko_msg_le_gap_bt5_set_adv_data_rsp_t *ret;

    if (p_data != NULL && dlen <= 31) {
        /* 0 - advertisement, 1 - scan response, set advertisement data here */
        ret = gecko_cmd_le_gap_bt5_set_adv_data(XJ_ADV_HANDLE, 0, dlen, p_data);
        if (ret->result == bg_err_invalid_param) {
            return MI_ERR_INVALID_PARAM;
        }
        // memcpy(last_adv_data.data, p_data, dlen);
        // last_adv_data.len = dlen;
    }

    if (p_sr_data != NULL && srdlen <= 31) {
        /* 0 - advertisement, 1 - scan response, set scan response data here */
        ret = gecko_cmd_le_gap_bt5_set_adv_data(XJ_ADV_HANDLE, 1, srdlen, p_sr_data);
        if (ret->result == bg_err_invalid_param) {
            return MI_ERR_INVALID_PARAM;
        }
        // memcpy(last_scan_rsp.data, p_sr_data, srdlen);
        // last_scan_rsp.len = srdlen;
    }


    return MI_SUCCESS;
}
#endif


static int xiaojiang_mesh_send(xjmesh_message_t *pmsg, xjmesh_send_param_t *param)
{
    if (param == NULL)
    {
        return -XJMESH_ERR_INVALID_PARAM;
    }
    if (param->handle != XJ_ADV_HANDLE && param->handle != XJ_ADV_HANDLE_RETRANS)
    {
        return -XJMESH_ERR_INVALID_PARAM;
    }
    if (param->period <= 0)
    {
        return -XJMESH_ERR_INVALID_PARAM;
    }

    if (pmsg == NULL)
    {
        return -XJMESH_ERR_INVALID_PARAM;
    }
#if PLATFORM_09P 
    uint8_t pbuffer[32];
#elif PLATFORM_05P
    uint8_t *pbuffer = gap_sched_task_get();
    if (pbuffer == NULL)
    {
        return false;
    }
#endif

// #ifdef LOG_TAG
// QM_LOGD(LOG_TAG, "xiajiang_mesh_transmit: opcode:%x, attr:%x (%x-%x)\n", pmsg->opcode, pmsg->attr_id, g_adv, g_retran_adv);
// #endif
#if PLATFORM_05P || PLATFORM_09P
    // pmsg->seq = _get_seq();
    int msg_len = pmsg->len + MEMBER_OFFSET(xjmesh_message_t, data);
    pbuffer[0] = msg_len + 4;
    pbuffer[1] = GAP_ADTYPE_MANUFACTURER_SPECIFIC;
    pbuffer[2] = XJMESH_PID & 0xFF;
    pbuffer[3] = (XJMESH_PID >> 8) & 0xFF;
    pbuffer[4] = 0x77;
    memcpy(&pbuffer[5], pmsg, msg_len);
#endif

#if PLATFORM_09P
    _my_gap_adv_data_set(pbuffer, msg_len + 5, NULL, 0);

    int ret = _my_adv_start(param->handle, param->interval_min, param->interval_max);
    if (ret)
    {
        return ret;
    }
    //开启timer, 定时关闭广播
    if (param->handle == XJ_ADV_HANDLE)
    {
        _timer_context.adv_time = param->period;
    }
    else if (param->handle == XJ_ADV_HANDLE_RETRANS)
    {
        _timer_context.retrans_time = param->period;
    }
#elif PLATFORM_05P
    gap_sched_task_p ptask = CONTAINER_OF(pbuffer, gap_sched_task_t, adv_data);
    ptask->adv_type = GAP_SCHED_ADV_TYPE_IND;
    ptask->adv_len = pbuffer[0] + 1;
    ptask->retrans_count = param->period / param->interval_min; 
    ptask->retrans_interval = param->interval_min;
    gap_sched_try(ptask);
    return true;
#elif PLATFORM_12G || PLATFORM_12S
    // if (param->handle == XJ_ADV_HANDLE)
    {
        memcpy(&_adv_msg, pmsg, sizeof(xjmesh_message_t));
        _adv_msg.seq = _my_seq++;
        // if(_adv_msg.seq == 65535) //_adv_msg.seq = 65535,此时_my_seq = 0
        // {
        //     _my_seq = 1;  //需要跳过_my_seq = 0，=0表示设备第一次上电
        // }
        if (_adv_msg.ttl == 0)
        {
            _adv_msg.ttl = 1;
        }
    }
    
    // if(!_adv_flag)
    // {
    //     _adv_flag = 1;
    //     miio_timer_start(_transmit_timer, XJMESH_ADV_PERIOD, NULL);
    // }
    // miio_timer_start(_transmit_timer, 50, NULL);

    _xiaojiang_mesh_timer_cb(NULL);
    //中继重发命令已经在重发列表中，不需处理
    return 0;
#endif
}

/**
 * @brief  重发消息
 * 
 * @param pmsg 
 * @return int 
 */
static int _xiaojiang_mesh_retransmit(xjmesh_message_t *pmsg)
{
#if 1 //XJMESH_ENABLE_RELAY
    // if (!_g_retrans_enabled)
    // {
    //     return 0;
    // }
    //如果消息已经在重发缓冲区列表中，则不重发
    if (_is_msg_in_retrans_list(pmsg))
    {
        return -XJMESH_ERR_ALREADY_RETRANS;
    }
    if (pmsg->ttl > 0)
    {
        // xjmesh_send_param_t param;
        // param.handle = XJ_ADV_HANDLE_RETRANS;
        // param.interval_min = XJMESH_RETRANS_INTERVAL_MIN;
        // param.interval_max = XJMESH_RETRANS_INTERVAL_MAX;
        // param.period = XJMESH_RETRANS_PERIOD;

        pmsg->ttl--;

        //将消息放入重发缓冲区
        int idx = _get_retrans_list_index();
        memcpy(&_retrans_msg_list[idx], pmsg, sizeof(xjmesh_message_t));
        syslog(LOG_INFO, "retransmit: %d, %d\n", idx, pmsg->attr_id);
        // return xiaojiang_mesh_send(pmsg, &param);
        // return localmesh_post_send_mutal_msg_event(INVALID_CHANNEL_ID, pmsg, true);
        return 0;
    }
    else
#endif
    {
        return -XJMESH_ERR_RETRANS_END;
    }
}

int xiaojiang_mesh_unpair(int channel)
{
    if (channel == 0xFF) 
    {
        //clear all
        for (int i = 0; i < XJMESH_MAX_NET_ID_NUM; i++)
        {
            // xiaojiang_mesh_send_subnet_message(XJMESH_OPCODE_SET, XJMESH_ATTR_NET_UNPAIR, i, NULL, 0);
            memset(&_net_ids[i], 0xFF, sizeof(xjmesh_net_id_t));
        }
    }
    else
    {
        #if SUPPORT_V1 && MESH_DEVICE_TYPE == MESH_DEVICE_TYPE_SWITCH
        //V2的绑定信息，存放在0~3的net id中，V1的绑定信息存放在从4中开始的net id中，遍历所有net id，找到与channel相同的net id，清除
        {
            if (!_is_null_net_id(&_net_ids[channel]))
            {
                memset(&_net_ids[channel], 0xFF, sizeof(xjmesh_net_id_t));
            }
            for (int i = 4; i < XJMESH_MAX_NET_ID_NUM; i++)
            {
                if (!_is_null_net_id(&_net_ids[i]))
                {
                    if (_net_ids[i].a_id[2] == channel)
                    {
                        memset(&_net_ids[i], 0xFF, sizeof(xjmesh_net_id_t));
                    }
                }
            }
            localmesh_post_storage_save_id_event(STORAGE_ID_NET_ID, false);
        }
        #else
        if (channel >= 0 && channel < XJMESH_MAX_NET_ID_NUM)
        {
            //send unpair message
            // xiaojiang_mesh_send_subnet_message(XJMESH_OPCODE_SET, XJMESH_ATTR_NET_UNPAIR, channel, NULL, 0);
            memset(&_net_ids[channel], 0xFF, sizeof(xjmesh_net_id_t));
        }
        #endif
    }
    //send save storage id
    localmesh_post_storage_save_id_event(STORAGE_ID_NET_ID, false);

    return 0;
}

int xiaojiang_mesh_ev_handler(int bus_id, qm_eventbus_event_t *ev)
{
    if (ev->module_id == LOCALMESH_MODULE_ID)
    {
        if (EVENT_ID(ev->event_id) == MUTUAL_EV_RECV_MSG)
        {
            localmesh_mutual_msg_ev_t msg_ev;
            qm_eventbus_peek_event_data(0, ev, &msg_ev, sizeof(localmesh_mutual_msg_ev_t));
            if (msg_ev.msg.attr_id == XJMESH_ATTR_NET_PAIR || msg_ev.msg.attr_id == XJMESH_ATTR_NET_RENAME) // || msg_ev.msg.attr_id == XJMESH_ATTR_NET_UNPAIR)
            {
                _net_cmd_process(&msg_ev.msg);
            }
        }
        else if (EVENT_ID(ev->event_id) == MUTUAL_EV_SEND_MSG)
        {
            localmesh_mutual_msg_ev_t msg_ev;
            qm_eventbus_peek_event_data(0, ev, &msg_ev, sizeof(localmesh_mutual_msg_ev_t));
            xjmesh_send_param_t param;
            param.handle = XJ_ADV_HANDLE;
            param.interval_min = XJMESH_ADV_INTERVAL_MIN;
            param.interval_max = XJMESH_ADV_INTERVAL_MAX;
            param.period = XJMESH_ADV_PERIOD;
            xiaojiang_mesh_send(&msg_ev.msg, &param);
        }
        else if ((EVENT_ID(ev->event_id) == MUTUAL_EV_START_PAIR) || (EVENT_ID(ev->event_id) == MUTUAL_EV_STOP_PAIR))
        {
            localmesh_pair_ev_t msg_ev;
            qm_eventbus_peek_event_data(0, ev, &msg_ev, sizeof(localmesh_pair_ev_t));
            if (EVENT_ID(ev->event_id) == MUTUAL_EV_START_PAIR)
            {
                printk("start pair:%d,%d\n", msg_ev.channel, msg_ev.count);
                xiaojiang_mesh_start_pair(msg_ev.channel, msg_ev.count);
            }
            else
            {
                printk("stop pair\n");
                xiaojiang_mesh_stop_pair(1);
            }
        }
        else if (EVENT_ID(ev->event_id) == MUTUAL_EV_UNPAIR)
        {
            localmesh_pair_ev_t msg_ev;
            qm_eventbus_peek_event_data(0, ev, &msg_ev, sizeof(localmesh_pair_ev_t));
            xiaojiang_mesh_unpair(msg_ev.channel);
        }
        else if (EVENT_ID(ev->event_id) == MUTUAL_EV_SAVE_ID)
        {
            qm_kv_set(XJMESH_NET_ID_KEY_STR, &_net_ids, sizeof(_net_ids), 1);
        }
    }

    if (ev->module_id == SYS_MODULE_ID && EVENT_ID(ev->event_id) == SYS_EVENT_TICK_100MS)
    {
        //
        _xiaojiang_mesh_timer_cb(NULL);
    }

    return 0;
}

bool xiaojiang_mesh_header_seq_check(void *data, int len)
{
    uint8_t *pdata = data;
    // uint8_t len = param->report.data_len;
    
    //检测是否是xiaojiang mesh数据
    if (pdata[1] != GAP_ADTYPE_MANUFACTURER_SPECIFIC
        || pdata[2] != (XJMESH_PID & 0xFF) || pdata[3] != ((XJMESH_PID >> 8) & 0xFF)
        || pdata[4] != 0x77)
    {
        return false;
    }

    xjmesh_message_t *pmsg = (xjmesh_message_t *)(pdata + 5);

    //对于src地址是自己的消息，不处理
    if (memcmp(pmsg->src_addr.mac, _my_mac, 6) == 0)
    {
        return false;
    }
    if (_mac_seq_filter(pmsg->src_addr.mac, pmsg->seq) == true)
    {
        //需要过滤
        return false;
    }
    return true;
}

int xiaojiang_mesh_receive(int len, void* data)
{
    int ret = 0;
    // mible_gap_evt_param_t *param = (mible_gap_evt_param_t *)arg;
    // if (evt == MIBLE_GAP_EVT_ADV_REPORT)
    {
        uint8_t *pdata = data;
        if (xiaojiang_mesh_header_seq_check(pdata, len) == false)
        {
            return -1;
        }
        xjmesh_message_t *pmsg = (xjmesh_message_t *)(pdata + 5);
        //判读是否是发给自己的消息
        //1. mac地址相同
        //2. 组播地址的话，判断是否在组播列表中
        //3. 对于组播地址，都需要转发
        if (pmsg->flag & XJMESH_FLAG_GROUP_ADDR)
        {
            //组播地址
            #if XJMESH_ENABLE_RELAY
            ret = _xiaojiang_mesh_retransmit(pmsg);
            #endif
            // MI_LOG_INFO("retrans:ret:%d\n", ret);
            if (_is_group_addr_sub(pmsg->dst_addr.group.addr))
            {
                //已经订阅组播地址，use callback to process the message
                return localmesh_post_recv_mutal_msg_event(INVALID_CHANNEL_ID, pmsg, true, false);
            }
        }
        else if (pmsg->flag & XJMESH_FLAG_NET_ADDR)
        {
            //网络地址
            //1. 判断是否是自己的网络地址
            //2. 如果是自己的网络地址，post event
            //3. 转发, 不论是否是自己的网络数据，都需要转发，以确保网络中的所有设备都能收到消息
            //未绑定时，也不处理心跳数据
            if (xiaojiang_mesh_paired_channels() == 0)
            {
                return -1;
            }
            for (int i = 0; i < XJMESH_MAX_NET_ID_NUM; i++)
            {
                if (_is_same_net_id(&_net_ids[i], &pmsg->dst_addr.net_id))
                {
                    //是自己的网络地址
                    localmesh_post_recv_mutal_msg_event(i, pmsg, true, false);
                }
            }
            //转发
            #if XJMESH_ENABLE_RELAY
            ret = _xiaojiang_mesh_retransmit(pmsg);
            #endif
        }
        else 
        {
            //单播地址
            if ( (memcmp(pmsg->dst_addr.mac, _my_mac, 6) != 0))
            {
                // MI_LOG_INFO("not for me\n");
                #if XJMESH_ENABLE_RELAY
                return _xiaojiang_mesh_retransmit(pmsg);
                #else
                return -XJMESH_ERR_RETRANS_END;
                #endif
            }
            else
            {
                //use callback to process the message 
                return localmesh_post_recv_mutal_msg_event(INVALID_CHANNEL_ID, pmsg, true, false);
            }
        }

        // MI_LOG_INFO("xiajiang_mesh_receive: opcode:0x%x, attr:0x%x\n", pmsg->opcode, pmsg->attr_id);

    }

    return ret;
}


/**
 * @brief 订阅组播地址
 * 
 * @param group_addr 
 * @return int 
 */
int xiaojiang_mesh_subscribe(uint16_t group_addr)
{
    //先判断是否已经订阅
    int idx = -1;
    for (int i = 0; i < XJMESH_MAX_GROUP_ADDR_NUM; i++)
    {
        if (_group_addr_list[i] == group_addr)
        {
            return 0;
        }
        else if (_group_addr_list[i] == 0)
        {
            if (idx < 0)
            {
                //记录第一个空位
                idx = i;
            }
        }
    }
    //here, not found
    if (idx >= 0)
    {
        _group_addr_list[idx] = group_addr;
        return 0;
    }
    else
    {
        return -XJMESH_ERR_SUBSCRIBE_FULL;
    }
    return 0;
}

/**
 * @brief 取消订阅组播地址
 * 
 * @param group_addr 
 * @return int 
 */
int xiaojiang_mesh_unsubscribe(uint16_t group_addr)
{
    for (int i = 0; i < XJMESH_MAX_GROUP_ADDR_NUM; i++)
    {
        if (_group_addr_list[i] == group_addr)
        {
            _group_addr_list[i] = 0;
            return 0;
        }
    }

    //未找到
    return 0;
}


/**
 * @brief 判断组播地址是否被订阅
 * 
 * @param group_addr 
 * @return true 
 * @return false 
 */
static bool _is_group_addr_sub(uint16_t group_addr)
{
    if (group_addr == XJMESH_GROUP_ADDR)
    {
        //broadcast
        return true;
    }

    for (int i = 0; i < XJMESH_MAX_GROUP_ADDR_NUM; i++)
    {
        if (_group_addr_list[i] == group_addr)
        {
            return true;
        }
    }

    return false;
}

int xiaojiang_mesh_fill_src_mac(xjmesh_message_t *msg)
{
    if (msg == NULL)
    {
        return -XJMESH_ERR_INVALID_PARAM;
    }

    memcpy(msg->src_addr.mac, _my_mac, 6);

    return 0;
}


int xiaojiang_mesh_retran_enable(bool enable)
{
#if XJMESH_ENABLE_RELAY
    //关闭转发功能，默认使能
    _g_retrans_enabled = enable;

    if (enable)
    {
        //打开timer
        miio_timer_start(_transmit_timer, 500, NULL);
    }
    else
    {
        miio_timer_stop(_transmit_timer);
    }
#endif

    return 0;
}

#if 0
int xiaojiang_mesh_recieve_enable(bool enable)
{
    _g_receive_enabled = enable;

    return 0;
}
#endif

//channel: 1-8
int xiaojiang_mesh_get_net_id(int channel, xjmesh_net_id_t *net_id)
{
    if (net_id == NULL)
    {
        return -XJMESH_ERR_INVALID_PARAM;
    }
    if (channel < 0 || channel >= XJMESH_MAX_NET_ID_NUM)
    {
        return -XJMESH_ERR_INVALID_PARAM;
    }
    // channel -= 1;
    //检查net_id是否有效, channel不为0则有效
    if ((_net_ids[channel].a_id[2] == INVALID_CHANNEL_ID) || (_net_ids[channel].b_id[2] == INVALID_CHANNEL_ID))
    {
        return -XJMESH_ERR_INVALID_PARAM;
    }

    memcpy(net_id, &_net_ids[channel], sizeof(xjmesh_net_id_t));

    return 0;
}

static bool _is_null_net_id(xjmesh_net_id_t *net_id)
{
    if (net_id == NULL)
    {
        return true;
    }
    if ((net_id->a_id[2] == INVALID_CHANNEL_ID) || (net_id->b_id[2] == INVALID_CHANNEL_ID))
    {
        return true;
    }
    return false;
}

static bool _is_same_net_id(xjmesh_net_id_t *net_id_1, xjmesh_net_id_t *net_id_2)
{
    //net_id a_id和b_id是无序的
    int aa = memcmp(net_id_1->a_id, net_id_2->a_id, 3);
    int ab = memcmp(net_id_1->a_id, net_id_2->b_id, 3);
    int ba = memcmp(net_id_1->b_id, net_id_2->a_id, 3);
    int bb = memcmp(net_id_1->b_id, net_id_2->b_id, 3);

    if (((aa == 0) && (bb == 0)) || ((ab == 0) && (ba == 0)))
    {
        return true;
    }

    return false;
}

int xiaojiang_mesh_send_pair_request(int channel)
{
    //set data
    // channel + net_id 
    uint8_t data[8];
    data[0] = channel;
    data[1] = _net_ids[channel].a_id[0];
    data[2] = _net_ids[channel].a_id[1];
    data[3] = _net_ids[channel].a_id[2];
    data[4] = _net_ids[channel].b_id[0];
    data[5] = _net_ids[channel].b_id[1];
    data[6] = _net_ids[channel].b_id[2];
    data[7] = _mesh_dev_type & 0xFF; //type


    return xiaojiang_mesh_send_group_messge(XJMESH_OPCODE_SET, XJMESH_ATTR_NET_PAIR, XJMESH_GROUP_ADDR, data, sizeof(data));
}

void _pair_timer_cb(void * p_context)
{
    //stop timer
    if (_pair_count <= 0)
    {
        localmesh_post_stop_pair_event(_pair_channel, false);
        return ;
    }
    _pair_count--;
    if (_pair_channel < XJMESH_MAX_NET_ID_NUM)
    {
        _send_pair_req_cnt++;
        xiaojiang_mesh_send_pair_request(_pair_channel);
    }
}

static int xiaojiang_mesh_stop_pair(bool force)
{
    if (!force && _send_pair_req_cnt < 2)
    {
        return -1;
    }
    _pair_flag = 0;
    //不清除channel，确保退出配网后，还可以正常回复对方的消息
    // _pair_channel = INVALID_CHANNEL_ID;
    _pair_status = 0;
    _pair_count = 0;
    if (_pair_timer)
    {
        miio_timer_delete(_pair_timer);
        _pair_timer = NULL;
    }

    return 0;
}

static int xiaojiang_mesh_start_pair(int channel, int count)
{
    if (channel < 0 || channel > XJMESH_MAX_NET_ID_NUM)
    {
        return -XJMESH_ERR_INVALID_PARAM;
    }
    _pair_flag = 1;
    _pair_channel = channel;
    _pair_count = count;
    _pair_status = 0;
    _send_pair_req_cnt = 0;
    memset(_cur_pair_mac_ch, 0xFF, sizeof(_cur_pair_mac_ch));

    xiaojiang_mesh_send_pair_request(channel);

    //start timer
    if (_pair_timer == NULL)
    {
        miio_timer_create(&_pair_timer, _pair_timer_cb, MIBLE_TIMER_REPEATED);
    }
    else
    {
        //stop timer
        miio_timer_stop(_pair_timer);
    }
    miio_timer_start(_pair_timer, 1000, channel);
	_g_pair_last_time = qm_now_ms(); //mible_mesh_get_exact_systicks();
}

static int _net_send_ack(int errcode, xjmesh_net_id_t *net_id)
{
    printk("_net_send_ack:%x-%x-%x-%x-%x-%x\n", net_id->a_id[0], net_id->a_id[1], net_id->a_id[2], net_id->b_id[0], net_id->b_id[1], net_id->b_id[2]);
    uint8_t data[7] = {0};
    data[0] = errcode;
    data[1] = net_id->a_id[0];
    data[2] = net_id->a_id[1];
    data[3] = net_id->a_id[2];
    data[4] = net_id->b_id[0];
    data[5] = net_id->b_id[1];
    data[6] = net_id->b_id[2];

    return xiaojiang_mesh_send_subnet_message(XJMESH_OPCODE_SET_ACK, XJMESH_ATTR_NET_PAIR, _pair_channel, data, 7);
}

//当下面两个标志都为1时，配网成功. 
#define NET_PAIR_STATUS_RECV_ACK  (1 << 0)  //自己发送的配网请求，收到对方的ack
#define NET_PAIR_STATUS_SEND_ACK  (1 << 1)  //收到对方的配网请求，发送ack

static void _put_rename_msg_to_retrans_list(xjmesh_net_id_t *current_id, xjmesh_net_id_t *new_id)
{
    xjmesh_message_t msg;
    msg.opcode = XJMESH_OPCODE_SET;
    msg.attr_id = XJMESH_ATTR_NET_RENAME;
    msg.flag = 0;
    msg.ttl = XJMESH_DEFAULT_TTL+1;
    msg.seq = _my_seq++;
    msg.flag = XJMESH_FLAG_NET_ADDR;
    memcpy(&msg.src_addr.mac, _my_mac, 6);
    memcpy(&msg.dst_addr.net_id, current_id, sizeof(xjmesh_net_id_t));
    memcpy(&msg.data[0], new_id, sizeof(xjmesh_net_id_t));
    msg.len = sizeof(xjmesh_net_id_t); 

    //put into retrans list
    _xiaojiang_mesh_retransmit(&msg);
}

static int _net_cmd_process(xjmesh_message_t *pmsg)
{
    if (pmsg->attr_id == XJMESH_ATTR_NET_PAIR)
    {
        // {
        //     //根据设备类型来决定是否处理当前的配网请求
        //     //1. 设备为开关，则接受所有类型的配网情况
        //     //2. 设备为灯、窗帘， 则只接受开关的配对请求
        //     if (XJMESH_DEV_TYPE_GET(_mesh_dev_type) != XJMESH_DEV_TYPE_SWITCH)
        //     {
        //         if (XJMESH_DEV_TYPE_GET(pmsg->data[7]) != XJMESH_DEV_TYPE_SWITCH)
        //         {
        //             //不是开关的配网请求，直接返回
        //             return -1;
        //         }
        //     }
        // }
        printk("pair(%d,%d,%d)\n", _pair_channel, _pair_flag, _pair_count);
        if (!_pair_flag || (_pair_channel >= XJMESH_MAX_NET_ID_NUM))
        {
            //未在配网状态, 但是刚配网结束，如果未超时，还是需要继续执行后续操作的
            //判断条件，mac和channel相同，并且未超时
            if (_cur_pair_mac_ch[6] != 0xFF)
            {
                if (memcmp(pmsg->src_addr.mac, _cur_pair_mac_ch, 6) == 0 && pmsg->data[0] == _cur_pair_mac_ch[6])
                {
                    //同一个设备的配网请求, 检测是否超时
                    if (_g_pair_last_time != 0 && (qm_now_ms() - _g_pair_last_time > (20*1000)))
                    {
                        //超时了，停止配网
                        printk("88\n");
                        _pair_channel = INVALID_CHANNEL_ID;
                        memset(_cur_pair_mac_ch, 0xFF, sizeof(_cur_pair_mac_ch));
                        return -1;
                    }
                    else
                    {
                        //未超时，继续执行后续操作
                        printk("99\n");
                    }
                }
                else
                {
                    //不是同一个设备的配网请求，直接返回
                    return -1;
                }
            }
            else
            {
                //mac  channel无效，
                return -1;
            }
        }
        if (pmsg->opcode == XJMESH_OPCODE_SET)
        {
            #if 0
            //一对多配对请求处理：
            {
                //只有开关会一对多配对请求，灯和窗帘不支持一对多配对请求
                //如果是开关，已经收到过set请求，则不处理
                if (XJMESH_DEV_TYPE_GET(_mesh_dev_type) == XJMESH_DEV_TYPE_SWITCH)
                {
                    if (_pair_status & NET_PAIR_STATUS_SEND_ACK)
                    {
                        //已经发送过配网请求，直接返回
                        if (memcpy(&pmsg->src_addr.mac, &_cur_pair_addr.mac, 6) != 0)
                        {
                            //不是同一个设备的配网请求，直接返回
                            return -1;
                        }
                    }
                }
            }
            #endif

            //配网请求 
            //1. 如果双方的net_id都为null，则创建net_id
            //2. 如果对方net_id为null，自己的不为null，则回复ok,net_id给对方
            //3. 如果对方net_id不为null，自己的是null，则修改net_id为对方的net_id
            //4. 如果双方都不为null，如果net_id不相同，判断seq，
            //   自己的seq小，则将net_id更改为对方的，并且开启网络更名流程, 回复rename,net_id
            //   对方的seq小，回复ok,net_id给对方即可
            xjmesh_net_id_t other_net_id;
            uint8_t other_channel = pmsg->data[0];
            other_net_id.a_id[0] = pmsg->data[1];
            other_net_id.a_id[1] = pmsg->data[2];
            other_net_id.a_id[2] = pmsg->data[3];
            other_net_id.b_id[0] = pmsg->data[4];
            other_net_id.b_id[1] = pmsg->data[5];
            other_net_id.b_id[2] = pmsg->data[6];

            if (_cur_pair_mac_ch[6] == 0xFF)
            {
                //当前配网的设备，未设置channel， 表示还未收到mac，记录当前的配网设备的mac和channel
                memcpy(&_cur_pair_mac_ch, &pmsg->src_addr.mac, 6);
                _cur_pair_mac_ch[6] = other_channel;
            }

            int other_null = _is_null_net_id(&other_net_id);
            int myself_null = _is_null_net_id(&_net_ids[_pair_channel]);

            if (other_null && myself_null)
            {
                //双方的net_id都为null，创建网络id
                //使用对方的mac最低两字节、channel，以及自己的mac、channel生成net_id
                //默认a_id为自己的mac和channel，b_id为对方的mac和channel
                _net_ids[_pair_channel].a_id[0] = _my_mac[1];
                _net_ids[_pair_channel].a_id[1] = _my_mac[0];
                _net_ids[_pair_channel].a_id[2] = _pair_channel;
                _net_ids[_pair_channel].b_id[0] = pmsg->src_addr.mac[1];
                _net_ids[_pair_channel].b_id[1] = pmsg->src_addr.mac[0];
                _net_ids[_pair_channel].b_id[2] = other_channel;
                localmesh_post_storage_save_id_event(STORAGE_ID_NET_ID, false);
                printk("11\n");
                _net_send_ack(XJMESH_ERR_OK, &_net_ids[_pair_channel]);
            }
            else if (other_null && !myself_null)
            {
                //对方为null，自己不为null
                printk("22\n");
                _net_send_ack(XJMESH_ERR_OK, &_net_ids[_pair_channel]);
            }
            else if (!other_null && myself_null)
            {
                //对方不为null，自己为null
                //保存对方的net_id
                printk("33\n");
                memcpy(&_net_ids[_pair_channel], &other_net_id, sizeof(xjmesh_net_id_t));
                localmesh_post_storage_save_id_event(STORAGE_ID_NET_ID, false);
                _net_send_ack(XJMESH_ERR_OK, &_net_ids[_pair_channel]);
            }
            else
            {
                //双方都不为null
                if (!_is_same_net_id(&other_net_id, &_net_ids[_pair_channel]))
                {
                    //net_id不相同，判断seq，若自己的seq小，则将net_id更改为对方的，并且开启网络更名流程；否则回复ok,net_id给对方即可
                    if (pmsg->seq > _my_seq)
                    {
                        //自己的seq小，将net_id更改为对方的，并且开启网络更名流程
                        printk("44\n");
                        //  先发送网络更名，因为此时自己的网络ID还是之前的。若更名后发送，会使用错误的网络ID
                        // xiaojiang_mesh_send_subnet_message(XJMESH_OPCODE_SET_NOACK, XJMESH_ATTR_NET_RENAME, _pair_channel, &other_net_id, sizeof(xjmesh_net_id_t));
                        _put_rename_msg_to_retrans_list(&_net_ids[_pair_channel], &other_net_id);
                        //  更改网络ID
                        memcpy(&_net_ids[_pair_channel], &other_net_id, sizeof(xjmesh_net_id_t));
                        localmesh_post_storage_save_id_event(STORAGE_ID_NET_ID, false);
                        _net_send_ack(XJMESH_ERR_OK, &_net_ids[_pair_channel]);

                    }
                }
                else
                {
                    //net_id相同
                    printk("55\n");
                    _net_send_ack(XJMESH_ERR_OK, &_net_ids[_pair_channel]);
                }
            }

            _pair_status |= NET_PAIR_STATUS_SEND_ACK;
            printk("pair status:%x\n", _pair_status);
            if (_pair_status & NET_PAIR_STATUS_RECV_ACK)
            {
                //配网成功
                // if (XJMESH_DEV_TYPE_GET(_mesh_dev_type) == XJMESH_DEV_TYPE_SWITCH)
                // {
                //     //开关类设备，配对成功后，清除标志位，继续配对
                //     _pair_status = 0;
                //     printk("continue pair1\n");
                // }
                // else
                {
                    //非开关类设备，一次配对后，直接退出
                    if (!xiaojiang_mesh_stop_pair(0))
                    {
                        localmesh_post_finish_pair_event(_pair_channel, false);
                    }
                    printk("finish pair1\n");
                }
            }
        }
        else if (pmsg->opcode == XJMESH_OPCODE_SET_ACK)
        {
            //配网回复
            // if (memcmp(&pmsg->src_addr.mac, &_cur_pair_addr.mac, 6) != 0)
            // {
            //     //不是当前配网的设备，直接返回
            //     return -1;
            // }
            _pair_status |= NET_PAIR_STATUS_RECV_ACK;
            printk("66 pair status:%x\n", _pair_status);
            if (_pair_status & NET_PAIR_STATUS_SEND_ACK)
            {
                //配网成功
                // if (XJMESH_DEV_TYPE_GET(_mesh_dev_type) == XJMESH_DEV_TYPE_SWITCH)
                // {
                //     //开关类设备，配对成功后，清除标志位，继续配对
                //     _pair_status = 0;
                //     printk("continue pair2\n");
                // }
                // else
                {
                    if (!xiaojiang_mesh_stop_pair(0))
                    {
                        localmesh_post_finish_pair_event(_pair_channel, false);
                    }
                    printk("finish pair2\n");
                }
                
            }
        }

    }
    else if (pmsg->attr_id == XJMESH_ATTR_NET_RENAME)
    {
        //find which channel to rename, compare net_id with the net_id in the _net_ids
        //if the same, rename the net_id
        //if not the same, do nothing
        syslog(LOG_INFO, "--RENAME");
        for (int i = 0; i < XJMESH_MAX_NET_ID_NUM; i++)
        {
            if (_is_same_net_id(&pmsg->dst_addr.net_id, &_net_ids[i]))
            {
                //rename
                memcpy(&_net_ids[i], pmsg->data, sizeof(xjmesh_net_id_t));
                localmesh_post_storage_save_id_event(STORAGE_ID_NET_ID, false);
                break;
            }
        }
    }
    // else if (pmsg->attr_id == XJMESH_ATTR_NET_UNPAIR)
    // {
    //     //unpair, clear net_id
    //     for (int i = 0; i < XJMESH_MAX_NET_ID_NUM; i++)
    //     {
    //         if (_is_same_net_id(&pmsg->dst_addr.net_id, &_net_ids[i]))
    //         {
    //             memset(&_net_ids[i], 0xFF, sizeof(xjmesh_net_id_t));
    //             localmesh_post_storage_save_id_event(STORAGE_ID_NET_ID, false);
    //             break;
    //         }
    //     }
    // }
}

int xiaojiang_mesh_paired_channels(void)
{
    //return number of paired channels
    int ret = 0;
    for (int i = 0; i < XJMESH_MAX_NET_ID_NUM; i++)
    {
        if (!_is_null_net_id(&_net_ids[i]))
        {
            ret++;
        }
    }
    return ret;
}

int xiaojiang_mesh_light_state_to_msg(xjmesh_light_state_t *state, uint8_t *msg, int len)  
{
    if (state == NULL || msg == NULL)
    {
        return -XJMESH_ERR_INVALID_PARAM;
    }
    if (len < 3)
    {
        return -XJMESH_ERR_INVALID_PARAM;
    }
    //XJMESH_LIGHT_STATE_FLAG_TEMP 与 XJMESH_LIGHT_STATE_FLAG_RGB互斥
    if ((state->flag & XJMESH_LIGHT_STATE_FLAG_TEMP) && (state->flag & XJMESH_LIGHT_STATE_FLAG_RGB))
    {
        return -XJMESH_ERR_INVALID_PARAM;
    }

    msg[0] = state->key_idx;
    //flag, index, lightness, temperature, fade_time, mode, rgb
    msg[1] = state->flag;
    msg[2] = state->index;
    int offset = 3;
    if (state->flag & XJMESH_LIGHT_STATE_FLAG_MODE)
    {
        //模式与其他的状态互斥，并且模式优先级高
        //payload: mode, fade_time
        msg[offset++] = state->mode;
        if (state->flag & XJMESH_LIGHT_STATE_FLAG_FADETIME)
        {
            if (len < 4)
            {
                return -XJMESH_ERR_INVALID_PARAM;
            }
            msg[offset++] = XJMESH_LIGHT_STATE_FADETIME(state->fade_time);
        }
    }
    else
    {
        msg[offset++] = state->onoff;
        if (state->flag & XJMESH_LIGHT_STATE_FLAG_LIGHTNESS)
        {
            //亮度
            msg[offset++] = XJMESH_LIGHT_STATE_LIGHTNESS(state->lightness);
        }
        if (state->flag & XJMESH_LIGHT_STATE_FLAG_TEMP)
        {
            //色温, 大端
            msg[offset++] = (state->temp_rgb >> 8) & 0xFF; //色温
            msg[offset++] = (state->temp_rgb) & 0xFF; //色温
        }
        if (state->flag & XJMESH_LIGHT_STATE_FLAG_RGB)
        {
            //RGB
            msg[offset++] = state->temp_rgb & 0xFF; //red
            msg[offset++] = (state->temp_rgb >> 8) & 0xFF; //green
            msg[offset++] = (state->temp_rgb >> 16) & 0xFF; //blue
        }
        if (state->flag & XJMESH_LIGHT_STATE_FLAG_FADETIME)
        {
            //渐变时间
            msg[offset++] = XJMESH_LIGHT_STATE_FADETIME(state->fade_time);
        }
    }
    return offset;
}

int xiaojiang_mesh_light_msg_to_state(xjmesh_light_state_t *state, uint8_t *msg, int len)  
{
    if (state == NULL || msg == NULL)
    {
        return -XJMESH_ERR_INVALID_PARAM;
    }
    if (len < 3)
    {
        return -XJMESH_ERR_INVALID_PARAM;
    }
    //flag, index, lightness, temperature, fade_time, mode, rgb
    memset(state, 0, sizeof(xjmesh_light_state_t));
    state->key_idx = msg[0];
    state->flag = msg[1];
    state->index = msg[2];

    int offset = 3;
    if (state->flag & XJMESH_LIGHT_STATE_FLAG_MODE)
    {
        //模式与其他的状态互斥，并且模式优先级高
        //payload: mode, fade_time
        state->mode = msg[offset++];
        if (state->flag & XJMESH_LIGHT_STATE_FLAG_FADETIME)
        {
            if (len < 4)
            {
                return -XJMESH_ERR_INVALID_PARAM;
            }
            state->fade_time = XJMESH_LIGHT_STATE_FADETIME_GET(msg[offset++]);
        }
    }
    else
    {
        state->onoff = msg[offset++];
        if (state->flag & XJMESH_LIGHT_STATE_FLAG_LIGHTNESS)
        {
            //亮度
            state->lightness = XJMESH_LIGHT_STATE_LIGHTNESS_GET(msg[offset++]);
        }
        if (state->flag & XJMESH_LIGHT_STATE_FLAG_TEMP)
        {
            //色温, 大端
            state->temp_rgb = msg[offset++] << 8 | msg[offset++]; //色温
        }
        if (state->flag & XJMESH_LIGHT_STATE_FLAG_RGB)
        {
            //RGB
            state->temp_rgb = msg[offset++] | ((uint32_t)msg[offset++] << 8) | ((uint32_t)msg[offset++] << 16);
        }
        if (state->flag & XJMESH_LIGHT_STATE_FLAG_FADETIME)
        {
            //渐变时间
            state->fade_time = XJMESH_LIGHT_STATE_FADETIME_GET(msg[offset++]);
        }
    }

    return offset;
}

int xiaojiang_mesh_light_step_to_msg(xjmesh_light_step_info_t *step, uint8_t *msg, int len)
{
    if (step == NULL || msg == NULL)
    {
        return -XJMESH_ERR_INVALID_PARAM;
    }
    if (len < 3)
    {
        return -XJMESH_ERR_INVALID_PARAM;
    }
    msg[0] = step->key_idx;
    msg[1] = step->index;
    msg[2] = XJMESH_LIGHT_STEP(step->step_dir, step->step_type);

    return 3;
}

int xiaojiang_mesh_light_msg_to_step(xjmesh_light_step_info_t *step, uint8_t *msg, int len)
{
    if (step == NULL || msg == NULL)
    {
        return -XJMESH_ERR_INVALID_PARAM;
    }
    if (len < 2)
    {
        return -XJMESH_ERR_INVALID_PARAM;
    }
    step->key_idx = msg[0];
    step->index = msg[1];
    step->step_dir = XJMESH_LIGHT_STEP_DIR_GET(msg[2]);
    step->step_type = XJMESH_LIGHT_STEP_TYPE_GET(msg[2]);

    return 3;
}
