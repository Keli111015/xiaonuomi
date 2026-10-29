#ifndef _MCU_SYSTEM_H_
#define _MCU_SYSTEM_H_

#include "mcu_config.h"

#ifndef CONFIG_MCU_MAIN_VER          //用户的软件主版本号,用于MCU固件升级,MCU升级版本需修改
#error "Missing configuration items CONFIG_MCU_MAIN_VER"
#endif

#ifndef CONFIG_MCU_SUB_VER          //用户的软件子版本号,用于MCU固件升级,MCU升级版本需修改
#error "Missing configuration items CONFIG_MCU_SUB_VER"
#endif

#ifndef CONFIG_MCU_FIX_VER          //用户的软件修复版本号,用于MCU固件升级,MCU升级版本需修改
#error "Missing configuration items CONFIG_MCU_FIX_VER"
#endif

#ifndef CONFIG_MCU_RECV_BUF_LMT     //数据接收缓存区大小,如MCU的RAM不够,可缩小
#error "Missing configuration items CONFIG_MCU_RECV_BUF_LMT"
#endif

#ifndef CONFIG_MCU_SEND_BUF_LMT     //数据发送缓存区,必须大于48
#error "Missing configuration items CONFIG_MCU_RECV_BUF_LMT"
#endif

#ifndef CONFIG_MCU_DATA_PROCESS_LMT //数据处理缓存区大小,根据用户数据大小量定,必须大于24
#error "Missing configuration items CONFIG_MCU_DATA_PROCESS_LMT"
#endif

#ifndef CONFIG_MCU_ATTR_TABLE       //产品功能表(必要，将相关属性ID填入该宏，需加{})
#error " Missing configuration items CONFIG_MCU_ATTR_TABLE"
#endif

#ifndef CONFIG_MCU_DEVINFO          //设备扩展信息(必要，将设备扩展信息填入该宏，需加{})
#error "Missing configuration items CONFIG_MCU_DEVINFO"
#endif

#ifndef CONFIG_MCU_EXTERN
#define CONFIG_MCU_EXTERN   extern
#endif

#ifdef  __cplusplus
#define  CONFIG_MCU_NULL    0
#else
#define  CONFIG_MCU_NULL    ((void *)0)
#endif

#define MCU_ARRAY_SIZE(array) \
        (sizeof(array) / sizeof((array)[0]))

#define MCU_SWAB16(x)                                                           \
    ((unsigned short)(                                                            \
    (((unsigned short)(x) & (unsigned short)0x00ffU) << 8) |                            \
    (((unsigned short)(x) & (unsigned short)0xff00U) >> 8) ))

#define MCU_SWAB32(x)                                                           \
    ((unsigned int)(                                                            \
    (((unsigned int)(x) & (unsigned int)0x000000ffUL) << 24) |                      \
    (((unsigned int)(x) & (unsigned int)0x0000ff00UL) <<  8)  |                     \
    (((unsigned int)(x) & (unsigned int)0x00ff0000UL) >>  8)  |                     \
    (((unsigned int)(x) & (unsigned int)0xff000000UL) >> 24) ))


/* ============================================================================================== */
/*                                   MACRO & TYPE DEFINITION REGION                               */
/* ============================================================================================== */
#define MCU_SERIAL_NO_ERR              0
#define MCU_SERIAL_PARAM_ERR           1
#define MCU_SERIAL_ALLOC_MEM_ERR       2
#define MCU_SERIAL_CHKSUM_ERR          3
#define MCU_SERIAL_MSG_NUM_ERR         4
#define MCU_SERIAL_CMD_ERR             5
#define MCU_SERIAL_PARAM_LEN_ERR       6
#define MCU_SERIAL_PACKET_LEN_ERR      7
#define MCU_SERIAL_TAG_ERR             8
#define MCU_SERIAL_NO_INIT_ERR         9
#define MCU_SERIAL_PACKET_ID_ERR       10
#define MCU_SERIAL_DATA_TYPE_ERR       11
#define MCU_SERIAL_TYPE_ID_ERR         12
#define MCU_SERIAL_PROCESS_ERR         13
#define MCU_SERIAL_SEND_BUSY_ERR       14
#define MCU_SERIAL_NO_UPDATE_ERR       15
#define MCU_SERIAL_AGAIN_ERR           16

#define MCU_SERIAL_RX_IDLE             80
#define MCU_SERIAL_RX_BUSY             81
//=============================================================================
//Byte order of the frame
//=============================================================================
#define         MCU_HEAD_FIRST                      0
#define         MCU_HEAD_SECOND                     1        
#define         MCU_PROTOCOL_VERSION                2
#define         MCU_FRAME_TYPE                      3
#define         MCU_LENGTH_HIGH                     4
#define         MCU_LENGTH_LOW                      5
#define         MCU_DATA_START                      6
#define         MCU_MIN_PACKET_ID                   1
#define         MCU_MAX_PACKET_ID                   100
/*
 *   ----------------------------------------------------------------
 *  |  HEAD  |  VER  |  CMD   |   LENGTH |    PAYLOAD    |  CHKSUM  |
 *   ----------------------------------------------------------------
 *    2Bytes   1Byte    1Byte     2Bytes      n Byte        1Byte
 */
//=============================================================================
#define MCU_FRAME_HEAD                      0xA5A5
#define MCU_FRAME_HEAD_PART                 0xA5
#define MCU_FRAME_HEAD_LEN                  6  /*HEAD + VER + CMD + LENGTH*/
#define MCU_FRAME_MIN_LEN                   7  /*HEAD + VER + CMD + LENGTH + CHKSUM*/
#define MCU_PROTOCOL_VER                    0x03
#define MCU_FRAME_DEVICEINFO_GET_MIN_LEN    (3)
#define MCU_FRAME_VER_LEN                   (3)
#define MCU_FRAME_SNAPSHOT_MIN_LEN          (3)
#define MCU_FRAME_REPORT_MIN_LEN            (3)
#define MCU_FRAME_SETMODE_MIN_LEN           (3)
#define MCU_DATA_MIN_LEN                    (3)
//============================================================================= 

typedef enum
{
    MCU_SYSTEM_MODULE_CONNECTED                        = 0x00,
    MCU_SYSTEM_MODULE_CONFIGURD_UNCONNECT_ROUTER       = 0x01,
    MCU_SYSTEM_MODULE_CONNECT_ROUTER_UNCONNECT_CLOUD   = 0x02,
    MCU_SYSTEM_MODULE_CONFIGURING_NETWORK              = 0x03,
    MCU_SYSTEM_MODULE_UNCONFIGURD_NETWORK              = 0x04,
    MCU_SYSTEM_MODULE_ENTER_LOWPOWER              	   = 0x05,
    MCU_SYSTEM_MODULE_ENTER_OTA_UPDATE                 = 0x06,
}mcu_system_module_status_t;

typedef enum{
    MCU_NETWORK_CONFIG_START                = 0x01,
    MCU_NETWORK_CONFIG_DEFAULT              = MCU_NETWORK_CONFIG_START,
    MCU_NETWORK_CONFIG_ONEKEY               = 0x02,
    MCU_NETWORK_CONFIG_AP                   = 0x03,
    MCU_NETWORK_CONFIG_ZERO                 = 0x04,
    MCU_NETWORK_CONFIG_BLUE                 = 0x05,
    MCU_NETWORK_CONFIG_REMOTE_CONTROL       = 0x06,
    MCU_NETWORK_CONFIG_BLUE_PAIR_CONFIRM    = 0x07,
    MCU_NETWORK_CONFIG_END    ,
}mcu_network_config_type_t;

typedef enum
{
    MCU_CMD_SET                 = 0x01,
    MCU_CMD_SERVICE             = 0x02,
    MCU_CMD_GET                 = 0x03,
    MCU_CMD_NOTIFY_STATUS       = 0x22,
    MCU_CMD_REPORT              = 0x10,
    MCU_CMD_HEARTBEAT           = 0x20,
    MCU_CMD_GET_DEVICEINFO      = 0x21,
    MCU_CMD_GET_SNAPSHOT        = 0x24,
    MCU_CMD_MCU_NOTIFY_STATUS   = 0x25,
    MCU_CMD_TIMEGET             = 0x26,
    MCU_CMD_NETCONFIG           = 0x23,
    MCU_CMD_GETMOD              = 0x50,
    MCU_CMD_SETMOD              = 0x51,
    MCU_CMD_EVENT               = 0x11,
    MCU_CMD_ACTION              = 0x12,
    MCU_CMD_OTA_START		    = 0x30,
    MCU_CMD_OTA_TRANSFER	    = 0x31,
    MCU_CMD_OTA_QUERYCRC        = 0x32,
    MCU_CMD_OTA_VERIFY          = 0x33,
    MCU_CMD_OTA_SWITCH          = 0x34,
}mcu_frame_cmd_t;

typedef enum{
    MCU_FRAME_MSG_TYPE_MIN = 0,
    MCU_FRAME_MSG_TYPE_BOOL = MCU_FRAME_MSG_TYPE_MIN,
    MCU_FRAME_MSG_TYPE_INT8 = 1,
    MCU_FRAME_MSG_TYPE_UINT8 = 2,
    MCU_FRAME_MSG_TYPE_INT16 = 3,
    MCU_FRAME_MSG_TYPE_UINT16 = 4,
    MCU_FRAME_MSG_TYPE_INT32 = 5,
    MCU_FRAME_MSG_TYPE_UINT32 = 6,
    MCU_FRAME_MSG_TYPE_STRING = 11,
    MCU_FRAME_MSG_TYPE_ARRAY = 14,
    MCU_FRAME_MSG_TYPE_FLOAT_ONE_UINT16 = 15,
    MCU_FRAME_MSG_TYPE_FLOAT_TWO_UINT16 = 16,
    MCU_FRAME_MSG_TYPE_FLOAT_ONE_UINT32 = 17,
    MCU_FRAME_MSG_TYPE_FLOAT_TWO_UINT32 = 18,
    MCU_FRAME_MSG_TYPE_FLOAT_ONE_INT16 = 19,
    MCU_FRAME_MSG_TYPE_FLOAT_TWO_INT16 = 20,
    MCU_FRAME_MSG_TYPE_FLOAT_ONE_INT32 = 21,
    MCU_FRAME_MSG_TYPE_FLOAT_TWO_INT32 = 22,
    MCU_FRAME_MSG_TYPE_STRING_ARRAY    = 24,
    MCU_FRAME_MSG_TYPE_MAX,
    MCU_FRAME_MSG_TYPE_NONE = 0xFF,
}mcu_frame_msg_type_t;

typedef struct 
{
    mcu_frame_msg_type_t data_type;
    unsigned char attr_id;
    union{
        unsigned char boolean;         
        signed char int8_Value;     
        unsigned char uint8_Value;
        signed short int16_Value;    
        unsigned short uint16_Value;    
        int int32_Value;     
        unsigned int uint32_Value;
        struct {            
            const unsigned char *str; 
            unsigned int length;   
        }string;
        struct {            
            const unsigned char *byte; 
            unsigned int length;   
        }array;
    }data;
}mcu_system_spec_msg_t;

void mcu_system_spec_msg_new_boolean(mcu_system_spec_msg_t *value, unsigned char new_value);
void mcu_system_spec_msg_new_int8(mcu_system_spec_msg_t *value, signed char new_value);
void mcu_system_spec_msg_new_uint8(mcu_system_spec_msg_t *value, unsigned char new_value);
void mcu_system_spec_msg_new_int16(mcu_system_spec_msg_t *value, signed short new_value);
void mcu_system_spec_msg_new_uint16(mcu_system_spec_msg_t *value, unsigned short new_value);
void mcu_system_spec_msg_new_int32(mcu_system_spec_msg_t *value, int new_value);
void mcu_system_spec_msg_new_uint32(mcu_system_spec_msg_t *value, unsigned int new_value);
void mcu_system_spec_msg_new_float_one_int16(mcu_system_spec_msg_t *value, signed short new_value);
void mcu_system_spec_msg_new_float_two_int16(mcu_system_spec_msg_t *value, signed short new_value);
void mcu_system_spec_msg_new_float_one_int32(mcu_system_spec_msg_t *value, int new_value);
void mcu_system_spec_msg_new_float_two_int32(mcu_system_spec_msg_t *value, int new_value);
void mcu_system_spec_msg_new_float_one_uint16(mcu_system_spec_msg_t *value, unsigned short new_value);
void mcu_system_spec_msg_new_float_two_uint16(mcu_system_spec_msg_t *value, unsigned short new_value);
void mcu_system_spec_msg_new_float_one_uint32(mcu_system_spec_msg_t *value, unsigned int new_value);
void mcu_system_spec_msg_new_float_two_uint32(mcu_system_spec_msg_t *value, unsigned int new_value);
void mcu_system_spec_msg_new_array(mcu_system_spec_msg_t *value, const unsigned char *new_value, unsigned int length);
void mcu_system_spec_msg_new_nstring(mcu_system_spec_msg_t *value, const unsigned char *new_value, unsigned int length);
void mcu_system_spec_msg_new_stringarray(mcu_system_spec_msg_t *value, const unsigned char *new_value, unsigned int length);

/**
 * @brief  协议初始化函数
 * @param  Null
 * @return Null
 * @note   在MCU初始化代码中调用该函数
 */
void  mcu_system_init(void);

/**
 * @brief  协议数据处理进程
 * @param  Null
 * @return Null
 * @note   在MCU主函数while循环中调用该函数
 */
void  mcu_system_yield(void);

/**
 * @brief  配网通知
 * @param  Null
 * @return Null
 * @note   在MCU主函数while循环中调用该函数
 */
void mcu_system_network_config_notify(mcu_network_config_type_t config_type);

/**
 * @brief  时间获取请求
 * @param  Null
 * @return Null
 * @note   Null
 */
int mcu_system_gettime_request(unsigned char timezone);

/**
 * @brief  服务设置接口
 * @param[in] {msg} 上报数据格式，详见 mcu_system_spec_msg_t
 * @param[in] {msg_num} 上报数据数量
 * @return Null
 * @note   Null
 */
int mcu_system_set_service(const mcu_system_spec_msg_t *msg, int msg_num);

/**
 * @brief  属性上报
 * @param[in] {msg} 上报数据格式，详见 mcu_system_spec_msg_t
 * @param[in] {msg_num} 上报数据数量
 * @return Null
 * @note   Null
 */
int mcu_system_report(const mcu_system_spec_msg_t *msg, int msg_num);

#endif
