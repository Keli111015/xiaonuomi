#ifndef __XJMESH_H_
#define __XJMESH_H_

#include "qm.h"
#include <stdint.h>
#include "qm_eventbus_static.h"

#if PLATFORM_12G || PLATFORM_12S
#  include "cmsis_gcc.h"
#endif

#define XJMESH_NET_ID_KEY_STR ("lmnid")

#define XJ_ADV_HANDLE          (5)
#define XJ_ADV_HANDLE_RETRANS  (6)

#define MAC_MAX_LEN (8)
#define MAC_SEQ_MAX_NUM (4)

#define GAP_ADTYPE_MANUFACTURER_SPECIFIC   (0xFF)
#define XJMESH_MAX_DATA_LEN (9)

#define XJMESH_APP_KEY_INDEX (0x01)
#define XJMESH_NET_KEY_INDEX (0x01)

#define XJMESH_PID          (0xF018)
// #define XJMESH_GET          (0xC1<<16 | XJMESH_PID)
// #define XJMESH_SET          (0xC3<<16 | XJMESH_PID)
// #define XJMESH_SET_NOACK    (0xC4<<16 | XJMESH_PID)
// #define XJMESH_STATUS       (0xC5<<16 | XJMESH_PID)
// #define GROUP_TRANSMITTER_TX_TIMES  (10)
#define XJMESH_ADV_PERIOD (480)
#define XJMESH_ADV_INTERVAL_MIN  (20)  //10ms
#define XJMESH_ADV_INTERVAL_MAX  (20)  //30ms
// #define XJMESH_ADV_WINDOW        (32)  //200ms
// #define XJMESH_ADV_CNT           ()

//缓存已经重发的消息，避免重复转发。当缓冲区满时，丢弃最早的消息
#define XJMESH_MAX_RERANS_NUM        (2)    //每个消息26字节

//model_id
#define XJMESH_MODEL_SERVER     (0x00001234)
#define XJMESH_MODEL_CLIENT     (0x00011234)

//组播地址
#define XJMESH_GROUP_ADDR           (0xC700)
#define XJMESH_GROUP_ADDR_SWITCH    (0xC701)
#define XJMESH_GROUP_ADDR_LIGHT     (0xC702)
#define XJMESH_GROUP_ADDR_CURTAIN   (0xC703)

//opcode
#if USE_SIG_MESH
#define XJMESH_OPCODE_GET          (0xC13412)
#define XJMESH_OPCODE_SET          (0xC33412)
#define XJMESH_OPCODE_SET_NOACK    (0xC43412)
#define XJMESH_OPCODE_STATUS       (0xC53412)
#else
#define XJMESH_OPCODE_GET          (0x01)
#define XJMESH_OPCODE_GET_ACK      (0x02)
#define XJMESH_OPCODE_SET          (0x03)
#define XJMESH_OPCODE_SET_ACK      (0x04)
#define XJMESH_OPCODE_SET_NOACK    (0x05)
#define XJMESH_OPCODE_STATUS       (0x06)
#define XJMESH_OPCODE_GET_SLAVE    (0x07)
#define XJMESH_OPCODE_SET_SLAVE    (0x08)
#endif

//record_id
#define XJMESH_RECORD_ID_BASE       (40)
#define XJMESH_RECORD_ID_NET_ID     (XJMESH_RECORD_ID_BASE + 1)

//attr_id
//net id
#define XJMESH_ATTR_NET_PAIR    (0xF0)
#define XJMESH_ATTR_NET_UNPAIR  (0xF1)
#define XJMESH_ATTR_NET_RENAME  (0xF2)
#define XJMESH_ATTR_QUERY_INFO  (0xF3)

//device info
//  device type
#define XJMESH_DEV_TYPE_SWITCH  (0x01)
#define XJMESH_DEV_TYPE_LIGHT   (0x02)
#define XJMESH_DEV_TYPE_CURTAIN (0x03)
#define XJMESH_DEV_TYPE(type, cnt) ((type&0xF) << 4 | (cnt&0xF))
#define XJMESH_DEV_TYPE_GET(x) ((x>>4)&0xF)
#define XJMESH_DEV_CNT_GET(x)  (x&0xF)
#define XJEMSH_DEV_INFO_LIGHTNTEMP_VALUE(x) ((x/100)&0xFF)
//开关类mesh产品
//组播
#define XJMESH_ATTR_SWITCH_STATE            (0x010)

//灯具类mesh产品
#define XJMESH_ATTR_LIGHT_STATE   (0x20)
#define XJMESH_ATTR_LIGHT_SETTING (0x21)
#define XJMESH_ATTR_LIGHT_STEP    (0x22)
#define XJMESH_LIGHT_INDEX_TOTAL (0)
#define XJMESH_LIGHT_INDEX_1     (1<<0)
#define XJMESH_LIGHT_INDEX_2     (1<<1)
#define XJMESH_LIGHT_INDEX_3     (1<<2)
#define XJMESH_LIGHT_INDEX_4     (1<<3)
#define XJMESH_LIGHT_INDEX_5     (1<<4)
#define XJMESH_LIGHT_INDEX_6     (1<<5)
#define XJMESH_LIGHT_INDEX_7     (1<<6)
#define XJMESH_LIGHT_INDEX_8     (1<<7)
#define XJMESH_LIGHT_STATE_FLAG_LIGHTNESS (1<<0)
#define XJMESH_LIGHT_STATE_FLAG_TEMP      (1<<1)
#define XJMESH_LIGHT_STATE_FLAG_FADETIME  (1<<2)
#define XJMESH_LIGHT_STATE_FLAG_MODE      (1<<3)
#define XJMESH_LIGHT_STATE_FLAG_RGB       (1<<4)
#define XJMESH_LIGHT_STATE_OFF       (0)
#define XJMESH_LIGHT_STATE_ON        (1) 
#define XJMESH_LIGHT_STATE_TOGGLE    (2)
#define XJMESH_LIGHT_STATE_UNCHANGE  (3)
#define STEP_TYPE_LIGHTNESS (0x01)
#define STEP_TYPE_LIGHTTEMP (0x02)
#define STEP_TYPE_LIGHTNESS_TEMP (0x03)
#define STEP_TYPE_RGB (0x04)
#define STEP_DIR_UP         (0x01)
#define STEP_DIR_DOWN       (0x02)
#define STEP_DIR_CONTI_UP   (0x03)
#define STEP_DIR_CONTI_DOWN (0x04)
#define STEP_DIR_STOP       (0x05)
#define XJMESH_LIGHT_STEP(dir, type) ((dir&0xF) << 4 | ((type)&0xF))
#define XJMESH_LIGHT_STATE_FADETIME(x) ((x) / 100) //单位100ms
#define XJMESH_LIGHT_STATE_FADETIME_GET(x) ((x) * 100) //单位100ms
#define XJMESH_LIGHT_STATE_LIGHTNESS(x) ((x) * 100 / 10000) //单位0~10000
#define XJMESH_LIGHT_STATE_LIGHTNESS_GET(x) ((x) * 10000 / 100) //单位0~10000
#define XJMESH_LIGHT_STATE_TEMP(x) (x)
#define XJMESH_LIGHT_STATE_TEMP_GET(x) (x)
#define XJMESH_LIGHT_STATE_RGB(r,g,b) ((((b)&0xFF)<<16 | ((g)&0xFF)<<8 | ((r)&0xFF)))
#define XJMESH_LIGHT_STEP_DIR_GET(x) ((x>>4)&0xF)
#define XJMESH_LIGHT_STEP_TYPE_GET(x) (x&0xF)

typedef struct 
{
    uint8_t key_idx;
    uint8_t flag;   //参考XJMESH_LIGHT_STATE_FLAG_XXX
    uint8_t index; //灯具索引 
    uint8_t onoff; //开关状态 0:off 1:on 2:toggle 3:unchange
    uint8_t mode; //模式 
    uint16_t lightness; //亮度 0~10000
    uint16_t fade_time; //渐变时间 
    uint32_t temp_rgb; //色温 0~10000
}xjmesh_light_state_t;

typedef struct
{
    uint8_t key_idx;
    uint8_t index; //灯具索引 
    uint8_t step_type; //参考STEP_TYPE_XXX
    uint8_t step_dir; //参考STEP_DIR_XXX
    uint16_t step_time; //渐变时间 
}xjmesh_light_step_info_t;

#define XJMESH_ATTR_SCENE_SETTING  (0x40)
#define XJMESH_ATTR_SCENE_TOGGLE   (0x41)


#if PLATFORM_05P
__PACKED typedef struct 
#elif PLATFORM_09P || PLATFORM_12G || PLATFORM_12S
typedef struct __PACKED
#endif
{
    uint8_t group;
    uint8_t cmd;
    uint16_t arg;
}xjmesh_rc_msg_t;

#if PLATFORM_05P
__PACKED typedef struct 
#elif PLATFORM_09P || PLATFORM_12G || PLATFORM_12S
typedef struct __PACKED
#endif
{
    uint8_t total_packet:4;
    uint8_t current_packet:4;
    uint8_t key_local:4; //本地按键index 0， 1， 2， 3
    uint8_t key_remote:4;  //配对方按键index 0， 1，2， 3
}xjmesh_sync_friend_msg_t;

#if PLATFORM_05P
__PACKED typedef struct 
#elif PLATFORM_09P || PLATFORM_12G  || PLATFORM_12S
typedef struct __PACKED
#endif
{
    uint8_t result;
    uint8_t data[7];
}xjmesh_result_t;

#ifndef MEMBER_OFFSET
#  define MEMBER_OFFSET(type, member) ((int) &((type *)0)->member)
#endif

enum {
    XJMESH_ERR_OK = 0,
    XJMESH_ERR_INVALID_MSG = 1,
    XJMESH_ERR_SUBSCRIBE_FULL = 2,
    XJMESH_ERR_ALREADY_RETRANS = 3,
    XJMESH_ERR_RETRANS_END = 4,
    XJMESH_ERR_INVALID_PARAM = 5,
    XJMESH_ERR_MEMORY_FULL = 6,
    XJMESH_ERR_NET_RENAME = 7,
};

typedef struct {
    uint32_t adv_time;
    uint32_t retrans_time;
}xjmesh_timer_context_t;

#define XJMESH_DEFAULT_TTL (1)

//dst_mac是否为组播地址
#define XJMESH_FLAG_GROUP_ADDR   (1<<0)  
#define XJMESH_FLAG_NET_ADDR     (1<<1)

typedef struct {
    //[0~1] lowwer mac 2 bytes, [2] channel
    uint8_t a_id[3];
    uint8_t b_id[3];
}xjmesh_net_id_t;

#if PLATFORM_05P
__PACKED typedef union {
#elif PLATFORM_09P || PLATFORM_12G  || PLATFORM_12S
typedef union __PACKED {
#endif
    uint8_t mac[6];
#if PLATFORM_05P
    __PACKED struct {
#else
    struct {
#endif 
        uint16_t addr;
    } group;
    xjmesh_net_id_t net_id;
} xjmesh_addr_t;

#if PLATFORM_05P
__PACKED typedef struct {
#elif PLATFORM_09P || PLATFORM_12G || PLATFORM_12S
typedef struct __PACKED {
#endif
    //中转规则：当dst_mac不为自己的mac && ttl不为0, 会转发消息，并且ttl减1
    uint8_t flag:5; //
    uint8_t ttl:3; //跳数
    uint8_t opcode:4;
    uint8_t len:4; //data数据的长度
    
    uint8_t attr_id;
    uint16_t seq;

    xjmesh_addr_t src_addr;
    xjmesh_addr_t dst_addr;

    uint8_t data[XJMESH_MAX_DATA_LEN];
} xjmesh_message_t;

typedef struct 
{
    uint8_t mac_seq[8]; //mac:6 + seq:2
}xjmesh_mac_seq_t;

typedef struct
{
    xjmesh_mac_seq_t list[MAC_MAX_LEN*MAC_SEQ_MAX_NUM]; //最多支持8个mac地址
    int index; //当前使用的索引
}xjmesh_mac_seq_list_t;

typedef struct {
    //interval 决定每次广播的时长，period决定一共广播多少秒。比如interval=100ms, period=10s, 每次广播100ms,持续10秒
    uint16_t handle;
    uint16_t interval_min;  //广播最小时长，单位ms
    uint16_t interval_max;  //广播最大时长，单位ms
    int      period;        //广播持续时间，单位ms。 
}xjmesh_send_param_t;

#define XJMESH_MAX_MAC_SEQ_INFO_NUM  (8)
typedef int (*xjmesh_recv_cb)(xjmesh_message_t *msg);

void xiaojiang_mesh_init(int type);
// int xiaojiang_mesh_send(xjmesh_message_t *msg, xjmesh_send_param_t *param);
int xiaojiang_mesh_ev_handler(int bus_id, qm_eventbus_event_t *ev);
int xiaojiang_mesh_receive(int len, void* data);
int xiaojiang_mesh_send_message(int opcode, int attr_id, int flag, xjmesh_addr_t *dst_addr, uint8_t *data, int len, int channel);
int xiaojiang_mesh_send_group_messge(int opcode, int attr_id, int group_addr, uint8_t *data, int len);
int xiaojiang_mesh_send_subnet_message(int opcode, int attr_id, int channel, uint8_t *data, int len);
int xiaojiang_mesh_send_all_subnet_message(int opcode, int attr_id, uint8_t *data, int len);
int xiaojiang_mesh_paired_channels(void);
// void xiaojiang_mesh_register_recv_cb(xjmesh_recv_cb cb);

#if SUPPORT_V1 
typedef struct 
{
	//绑定的设备mac地址以及按键
	// key_num为0或者0xFF时，表示该数据未使用
	char key_num;
	// char resvd;
	char mac[6];
}localmesh_bound_info_t_v1;
typedef struct 
{
    localmesh_bound_info_t_v1 bound_info[4][8];
}localmesh_param_t_v1;
#endif

#define XJMESH_MAX_GROUP_ADDR_NUM   4
// #if SUPPORT_V1 && MESH_DEVICE_TYPE == MESH_DEVICE_TYPE_SWITCH
// #  define XJMESH_MAX_NET_ID_NUM       (8 + 32)
// #else
#  define XJMESH_MAX_NET_ID_NUM       (8)
// #endif
#define XJMESH_PAIR_TIMEOUT_MS      (60 * 1000)

/**
 * @brief  增加订阅地址，最多支持8个地址
 * 
 * @param group_addr 
 * @return int  0:成功， -1 失败
 */
int xiaojiang_mesh_subscribe(uint16_t group_addr);

/**
 * @brief  删除订阅地址
 * 
 * @param group_addr 
 * @return int 
 */
int xiaojiang_mesh_unsubscribe(uint16_t group_addr);

/**
 * @brief 将msg的src_mac填充为自己的mac地址
 * 
 * @param msg 
 * @return int 
 */
int xiaojiang_mesh_fill_src_mac(xjmesh_message_t *msg);

/**
 * @brief 打开或者关闭转发功能
 * 
 * @param enable 
 * @return int 
 */
int xiaojiang_mesh_retran_enable(bool enable);

/**
 * @brief 退网
 * 
 * @return int 
 */

int xiaojiang_mesh_get_net_id(int channel, xjmesh_net_id_t *net_id);

/**
 * @brief 将灯具的状态打包成消息
 * 
 * @param state 
 * @param msg 
 * @return int 消息长度，-1表示失败
 */
int xiaojiang_mesh_light_state_to_msg(xjmesh_light_state_t *state, uint8_t *msg, int len);  

/**
 * @brief 将消息打包成灯具的状态
 * 
 * @param msg 
 * @param state 
 * @return int 消息长度，-1表示失败
 */
int xiaojiang_mesh_light_msg_to_state(xjmesh_light_state_t *state, uint8_t *msg, int len);  

/**
 * @brief 将灯具的渐变信息打包成消息
 * 
 * @param step 
 * @param msg 
 * @return int 消息长度，-1表示失败
 */
int xiaojiang_mesh_light_step_to_msg(xjmesh_light_step_info_t *step, uint8_t *msg, int len);

/**
 * @brief 将消息打包成灯具的渐变信息
 * 
 * @param msg 
 * @param step 
 * @return int 消息长度，-1表示失败
 */
int xiaojiang_mesh_light_msg_to_step(xjmesh_light_step_info_t *step, uint8_t *msg, int len);

bool xiaojiang_mesh_header_seq_check(void *data, int len);
#endif
