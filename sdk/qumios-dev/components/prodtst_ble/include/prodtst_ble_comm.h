#ifndef _PRODTST_BLE_COMM_H_
#define _PRODTST_BLE_COMM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"

#if CONFIG_QM_WORK_SUPPORT
#include "qm_event.h"

/** Production-test BLE event base used by event-loop APIs. */
QM_EVENT_DECLARE_BASE(QM_EVENT_PRODTST_BLE);
#endif

#ifndef CONFIG_PRODTST_BLE_SERVER
#define CONFIG_PRODTST_BLE_SERVER           0
#endif

#ifndef CONFIG_PRODTST_BLE_CLIENT
#define CONFIG_PRODTST_BLE_CLIENT           0
#endif

#ifndef CONFIG_PRODTST_BLE_USE_EXT_SCAN
#define CONFIG_PRODTST_BLE_USE_EXT_SCAN     0
#endif

#ifndef CONFIG_PRODTST_BLE_USE_ALLOC
#define CONFIG_PRODTST_BLE_USE_ALLOC        0
#endif

#ifndef CONFIG_PRODTST_BLE_RESENT_COUNT   
#define CONFIG_PRODTST_BLE_RESENT_COUNT         (2)
#endif

#ifndef CONFIG_PRODTST_BLE_CMD_TIMEOUT    
#define CONFIG_PRODTST_BLE_CMD_TIMEOUT          (5000)
#endif

#ifndef CONFIG_PRODTST_BLE_TX_TIMEOUT
#define CONFIG_PRODTST_BLE_TX_TIMEOUT           (200)
#endif

#ifndef CONFIG_PRODTST_BLE_BUFF_LEN
#define CONFIG_PRODTST_BLE_BUFF_LEN             (64)
#endif


#ifndef CONFIG_PRODTST_BLE_MAX_CONN_NUM
#define CONFIG_PRODTST_BLE_MAX_CONN_NUM         (10)
#endif

#ifndef CONFIG_PRODTST_BLE_ADV_INTERVAL
#define CONFIG_PRODTST_BLE_ADV_INTERVAL         (132)
#endif

#ifndef CONFIG_PRODTST_BLE_SCAN_INTERVAL
#define CONFIG_PRODTST_BLE_SCAN_INTERVAL        (160)
#endif

#ifndef CONFIG_PRODTST_BLE_SCAN_WINDOWS
#define CONFIG_PRODTST_BLE_SCAN_WINDOWS         (150)
#endif

#ifndef CONFIG_PRODTST_BLE_SERVER
#define CONFIG_PRODTST_BLE_SERVER   0
#endif

#if CONFIG_PRODTST_BLE_CLIENT

#ifndef CONFIG_PRODTST_BLE_CLIENT_TYPE_ADV
#define CONFIG_PRODTST_BLE_CLIENT_TYPE_ADV        (0)
#endif

#ifndef CONFIG_PRODTST_BLE_CLIENT_TYPE_CONN
#define CONFIG_PRODTST_BLE_CLIENT_TYPE_CONN       (0)
#endif

#ifndef CONFIG_PRODTST_BLE_CLIENT_ADV_REPORT
#define CONFIG_PRODTST_BLE_CLIENT_ADV_REPORT      (0)
#endif

#endif

#define PRODTST_BLE_HEADER_LEN              (4)
#define PRODTST_BLE_MIN_LEN                 (13)
#define PRODTST_BLE_FRAME_MIN_LEN           (14)
#define PRODTST_BLE_CONTENT_MIN_LEN         (10)
#define PRODTST_BLE_TEST_ID_LEN             (1)
#define PRODTST_BLE_PLAYLOAD_MAX_LEN        (31 - PRODTST_BLE_FRAME_MIN_LEN)
#define PRODTST_BLE_RECV_MAX_LEN            (30)

#define PRODTST_BLE_TYPE                    (0xFF)
#define PRODTST_BLE_VER                     (0x03)
#define PRODTST_BLE_SUBTYPE                 (0x04)

#define PRODTST_BLE_CID_1                   (0x18)
#define PRODTST_BLE_CID_2                   (0xF0)

#define PRODTST_BLE_VER_GET(p)              (p>>4)   
#define PRODTST_BLE_SUBTYPE_GET(p)          (p&0x0F)          

#pragma pack(1)

typedef struct
{
    uint8_t     len;
    uint8_t     type;
    uint16_t    cid;
    uint8_t     ver;
}prodtst_ble_head_t;

typedef struct
{
    struct _fmsk_param
    {
        uint8_t  encrypted      : 1; //0：不支持加密 1：支持数据加密 
        uint8_t  channel        : 1; //数据通道  0：下行通道  1: 上行通道
        uint8_t  type           : 1; //协议类型  0：小匠标准AT指令 1：非小匠标准AT指令
        uint8_t  need_ack       : 1; //是否ACK  0：无需ACK  1: 需要ACK 
        uint8_t  invail         : 4; //保留
    }fmsk;
    
    struct _cmd_param
    {
        uint8_t  cmd            : 4; //cmd指令
        uint8_t  invail         : 4; //保留
    }frame_cmd;

    uint16_t     pid; //产品id
    uint8_t      mac[2]; //接收目标的mac地址的后2个字节（无接收目标时，mac地址全0）

    struct _sno_param
    {
        uint8_t  msg_id      : 4; //消息ID
        uint8_t  encrypted   : 1; //数据加密指示
        uint8_t  invail      : 3; //保留
    }sno;

    struct _total_frame_param
    {
        uint8_t  frame_seq : 4; //帧序号
        uint8_t  total_frame : 4; //总拆包帧数
    }total_frame_seq;
    uint8_t length; //帧数据长度
}prodtst_ble_frame_t;

typedef struct 
{
    uint8_t prodtst_status;
    uint8_t prodtst_type;
}prodtst_ble_prodtst_t;

typedef struct
{
    prodtst_ble_head_t header;
    prodtst_ble_frame_t frame;
}prodtst_ble_frame_generic_t;

typedef struct
{
    prodtst_ble_head_t header;
    prodtst_ble_frame_t frame;
    uint8_t pdata[1];
}prodtst_ble_frame_info_t;

#pragma pack()

typedef enum{
    PRODTST_BLE_PRODTST_EVENT_ACTION_SEQ = 0,     
    PRODTST_BLE_PRODTST_EVENT_ACTION = 1,     
    PRODTST_BLE_PRODTST_EVENT_MAX,
}prodtst_ble_prodtst_event_t;

typedef enum{
    PRODTST_BLE_CMD_TEST = 0,        //产测盒子测试用例下发 
    PRODTST_BLE_CMD_STATE_SET,      //产测盒子产测状态设置 
    PRODTST_BLE_CMD_CONNECT,        //被测设备连接
    PRODTST_BLE_CMD_REPORT,         //被测设备结果上报
    PRODTST_BLE_CMD_MAX,
}prodtst_ble_frame_cmd_t;

typedef enum{
    PRODTST_BLE_CHANNEL_SERVER,    //server通道
    PRODTST_BLE_CHANNEL_CLIENT,     //client通道
    PRODTST_BLE_CHANNEL_MAX,
}prodtst_ble_channel_t;

typedef enum{
    PRODTST_BLE_TYPE_ADV,     //广播类型
    PRODTST_BLE_TYPE_CONN,    //连接类型
    PRODTST_BLE_TYPE_MAX,
}prodtst_ble_type_t;

typedef enum{
    PRODTST_BLE_DATA_TYPE_AT,     //AT指令
    PRODTST_BLE_DATA_TYPE_HEX,    //HEX数据
    PRODTST_BLE_DATA_TYPE_MAX,
}prodtst_ble_data_type_t;


typedef struct 
{
    uint16_t length;
    uint8_t *pdata;
}prodtst_ble_data_generic_info_t;

typedef struct 
{
    uint8_t test_id;
    uint16_t length;
    uint8_t *pdata;
}prodtst_ble_data_test_info_t;

typedef struct 
{
    uint8_t error_code;
}prodtst_ble_code_info_t;

typedef struct 
{
    uint8_t conn_id;
}prodtst_ble_conn_info_t;

typedef struct 
{
    uint8_t event;
}prodtst_ble_event_info_t;

typedef struct 
{
    uint8_t test_id; //测试用例
    uint8_t is_conn; // 是否被连接
    uint8_t src_addr[6]; //发送目标的mac地址

    prodtst_ble_frame_t frame;
    uint8_t pdata[CONFIG_PRODTST_BLE_BUFF_LEN];
    uint16_t bytes_received;

    uint8_t need_del;

    uint8_t raw_data[32];
    uint16_t raw_len;
}prodtst_ble_handle_t;

typedef struct 
{
    uint16_t pid;
    int8_t min_rssi;
    uint8_t encrypted; //是否加密 0：加密 1:非加密 
    prodtst_ble_data_type_t data_type; //数据格式类型 0：AT数据 1:HEX数据    
}prodtst_ble_param_t;

typedef int (*prodtst_ble_event_t)(prodtst_ble_prodtst_event_t event, uint8_t *data, int length);

int32_t prodtst_ble_comm_deinit(void);
int32_t prodtst_ble_comm_init(prodtst_ble_channel_t channel, prodtst_ble_param_t *param);

int32_t prodtst_ble_comm_pid_change(uint16_t pid);
int32_t prodtst_ble_comm_event_register(prodtst_ble_event_t event_cb);

#if CONFIG_PRODTST_BLE_SERVER || CONFIG_PRODTST_BLE_CLIENT_ADV_REPORT ||  CONFIG_PRODTST_BLE_CLIENT_TYPE_CONN
int32_t prodtst_ble_comm_send(uint8_t cmd, uint8_t mac[6], uint8_t need_ack, uint8_t *data, int len);
#endif

#if CONFIG_PRODTST_BLE_USE_EXT_SCAN
int32_t prodtst_ble_comm_data_push(uint8_t mac[6], uint8_t *buff, int length);
#endif

prodtst_ble_handle_t *prodtst_ble_handle_get(uint8_t mac[6]);
int32_t prodtst_ble_handle_delete(prodtst_ble_handle_t *handle);
prodtst_ble_handle_t * prodtst_ble_handle_create(uint8_t mac[6]);

#ifdef __cplusplus
}
#endif

#endif
