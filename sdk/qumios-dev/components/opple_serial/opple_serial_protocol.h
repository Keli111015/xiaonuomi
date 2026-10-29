#ifndef _OPPLE_SERIAL_PROTOCOL_H_
#define _OPPLE_SERIAL_PROTOCOL_H_

#include "qm_config.h"

#if CONFIG_OPPLE_SERIAL_SUPPORT

#include "opple_serial_common.h"
#include "qm_utils_timer.h"
#include "qm_utils_list.h"
#include "qm_tlv.h"

/* ============================================================================================== */
/*                                   MACRO & TYPE DEFINITION REGION                               */
/* ============================================================================================== */


#define OPPLE_SERIAL_NO_ERR              0
#define OPPLE_SERIAL_PARAM_ERR           1
#define OPPLE_SERIAL_ALLOC_MEM_ERR       2
#define OPPLE_SERIAL_CHKSUM_ERR          3
#define OPPLE_SERIAL_MSG_NUM_ERR         4
#define OPPLE_SERIAL_CMD_ERR             5
#define OPPLE_SERIAL_PARAM_LEN_ERR       6
#define OPPLE_SERIAL_PACKET_LEN_ERR      7
#define OPPLE_SERIAL_TAG_ERR             8
#define OPPLE_SERIAL_NO_INIT_ERR         9
#define OPPLE_SERIAL_PACKET_ID_ERR       10
#define OPPLE_SERIAL_DATA_TYPE_ERR       11
#define OPPLE_SERIAL_TYPE_ID_ERR         12
#define OPPLE_SERIAL_PROCESS_ERR         13
#define OPPLE_SERIAL_SEND_BUSY_ERR       14
#define OPPLE_SERIAL_NO_UPDATE_ERR       15

enum{
	OPPLE_SERIAL_COM_OK,
	OPPLE_SERIAL_COM_ERROR,
	OPPLE_SERIAL_COM_OPERATION_NOT_PERMITTED,
	OPPLE_SERIAL_COM_IO_ERROR,
	OPPLE_SERIAL_COM_NO_DEVICE,
	OPPLE_SERIAL_COM_MEM_ERROR,
	OPPLE_SERIAL_COM_DEVICE_BUSY,
	OPPLE_SERIAL_COM_PARAM_ERROR,
	OPPLE_SERIAL_COM_FUNC_NOT_SUPPORT,
	OPPLE_SERIAL_COM_TIMEOUT,
	OPPLE_SERIAL_COM_COMMUNICATION_ERROR,
	OPPLE_SERIAL_COM_CHKSUM_ERROR,
};
/*
 *   -----------------------------------------------------------------------------
 *  |  HEAD  |  LENGTH  |  VER+SEQ   |   CMD |    PAYLOAD    |  CHKSUM  |  TAIL  |
 *   -----------------------------------------------------------------------------
 *    2Bytes     1Byte     1Byte        1Bytes      n Byte      1Byte     2Bytes
 */
#define OPPLE_FRAME_HEAD                  0xBEBE
#define OPPLE_FRAME_TAIL                  0xEDED
#define OPPLE_FRAME_HEAD_PART             0xBE
#define OPPLE_FRAME_HEAD_1_PART           0xBE
#define OPPLE_FRAME_TAIL_PART             0xED
#define OPPLE_FRAME_TAIL_1_PART           0xED
#define OPPLE_FRAME_HEAD_LEN              5  /*HEAD(2bytes) + LENGTH + (VER + SEQ) + CMD*/
#define OPPLE_FRAME_MIN_LEN               8  /*HEAD(2bytes) + LENGTH + (VER + SEQ) + CMD + CHKSUM + TAIL(2bytes)*/
#define OPPLE_PROTOCOL_VER                2
#define OPPLE_FRAME_SNAPSHOT_MIN_LEN      (OPPLE_FRAME_MIN_LEN + 3)
#define OPPLE_FRAME_REPORT_MIN_LEN        (OPPLE_FRAME_MIN_LEN + 3)
#define OPPLE_FRAME_GET_MIN_LEN           (OPPLE_FRAME_MIN_LEN + 1)

#define OPPLE_VER_GET(p)                (p>>4)
#define OPPLE_SEQ_GET(p)                (p&0x0F)

#define OPPLE_SEQ_MAX                   0x0F

//define module status
typedef enum
{
    OPPLE_MODULE_CONNECTED                        = 0x00,
    OPPLE_MODULE_CONFIGURD_UNCONNECT_ROUTER       = 0x01,
    OPPLE_MODULE_CONNECT_ROUTER_UNCONNECT_CLOUD   = 0x02,
    OPPLE_MODULE_CONFIGURING_NETWORK              = 0x03,
    OPPLE_MODULE_UNCONFIGURD_NETWORK              = 0x04,
    OPPLE_MODULE_TEST_CONNECTED                   = 0x05,
    OPPLE_MODULE_STATUS_NONE                      = 0xFF,
}opple_module_network_status_t;

enum{
    OPPLE_MODULE_STATUS_KEY_NETWORK         = 0x42,
    OPPLE_MODULE_STATUS_KEY_RESET           = 0x44,
    OPPLE_MODULE_STATUS_KEY_REMOTE_PAIR     = 0x45,
    OPPLE_MCU_STATUS_KEY_REBOOT             = 0x50,
};

typedef enum
{
    OPPLE_MCU_ENTER_LOWPOWER              		= 0x01,
}opple_mcu_status_t;

typedef enum
{
    OPPLE_WIFI_TYPE_NORMAL                      = 0x00,
    OPPLE_WIFI_TYPE_TEST                        = 0x01,
}opple_wifi_type_t;

typedef enum
{
    OPPLE_REMOTE_PAIR_STATUS_FAIL            = 0x00,
    OPPLE_REMOTE_PAIR_STATUS_SUCCESS         = 0x01,
}opple_remote_pair_status_t;

typedef enum
{
    OPPLE_MODULE_PRODTST_SUCCESS                  = 0x00,
    OPPLE_MODULE_PRODTST_START                    = 0x01,
    OPPLE_MODULE_PRODTST_FAILED                   = 0x02,
    OPPLE_MODULE_PRODTST_NFC                      = 0x03,
    OPPLE_MODULE_PRODTST_STATUS_NONE,
}module_prodtst_status_t;

typedef enum{
    OPPLE_MODULE_PRODTST_TYPE_START               = 0,
    OPPLE_MODULE_PRODTST_TYPE_NONE                = 0,
    OPPLE_MODULE_PRODTST_TYPE_COMPLETE_MACHINE    = 1,
    OPPLE_MODULE_PRODTST_TYPE_EXIT                = 2,
    OPPLE_MODULE_PRODTST_TYPE_MODULE              = 3, 
    OPPLE_MODULE_PRODTST_TYPE_END                   
}opple_module_prodtst_type_t;

typedef enum
{
    OPPLE_CMD_GET                 = 0x03,
    OPPLE_CMD_SET                 = 0x01,
    OPPLE_CMD_GET_FLASH           = 0x04,
    OPPLE_CMD_SET_FLASH           = 0x05,
    OPPLE_CMD_NOTIFY_STATUS       = 0x22,
    OPPLE_CMD_GET_STATUS          = 0x27,
    OPPLE_CMD_MCU_NOTIFY_STATUS   = 0x28,
    OPPLE_CMD_REPORT              = 0x10,
    OPPLE_CMD_GET_SNAPSHOT        = 0x24,
    OPPLE_CMD_TIMEGET             = 0x26,
    OPPLE_CMD_NETCONFIG           = 0x23,
    OPPLE_CMD_GETMOD              = 0x50,
    OPPLE_CMD_SETMOD              = 0x51,
    OPPLE_CMD_BEACON              = 0x11,
    OPPLE_CMD_EVENT               = 0x13,
    OPPLE_CMD_ACTION              = 0x14,

} opple_frame_cmd_t;

#pragma pack(1)

typedef struct
{
    uint16_t head;
    uint8_t  len;
    uint8_t  seq:4;
    uint8_t  ver:4;
    uint8_t  cmd;
} opple_frame_header_t;

typedef struct
{
    uint8_t chksum;
    uint16_t tail;
} opple_frame_tail_t;

typedef struct
{
    opple_frame_header_t fixed;
    uint8_t chksum;
    uint16_t tail;
}opple_frame_unit_t;

typedef struct
{
    opple_frame_header_t fixed;
    uint8_t data;
    uint8_t chksum;
    uint16_t tail;
}opple_frame_generic_t;

typedef struct 
{
    int8_t timezone;
    uint8_t year;
    uint8_t month;
    uint8_t day;
    uint8_t wday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
}opple_serial_time_info_t;

typedef struct 
{
    uint8_t code;
    opple_serial_time_info_t info;
}opple_serial_time_packet_t;

typedef struct
{
    uint8_t network_status;
    int8_t rssi;
    uint8_t mac[6];
}opple_serial_network_info_t;

#pragma pack()

typedef uint32_t opple_platform_id_t;

typedef enum{
    OPPLE_NETWORK_CONFIG_START   = 0x01,
    OPPLE_NETWORK_CONFIG_DEFAULT = OPPLE_NETWORK_CONFIG_START,
    OPPLE_NETWORK_CONFIG_ONEKEY  = 0x02,
    OPPLE_NETWORK_CONFIG_AP      = 0x03,
    OPPLE_NETWORK_CONFIG_ZERO    = 0x04,
    OPPLE_NETWORK_CONFIG_BLUE    = 0x05,
    OPPLE_NETWORK_CONFIG_END     = OPPLE_NETWORK_CONFIG_BLUE ,
}opple_network_config_type_t;

typedef enum{

    OPPLE_PARSE_STATE_NONE = 0,
    OPPLE_PARSE_STATE_HEAD,
    OPPLE_PARSE_STATE_HEAD_1,
    OPPLE_PARSE_STATE_VER,
    OPPLE_PARSE_STATE_LEN,
    OPPLE_PARSE_STATE_DATA,
    OPPLE_PARSE_STATE_TAIL,
    OPPLE_PARSE_STATE_TAIL_1
}opple_parse_state_t;

typedef enum{
   OPPLE_SERIAL_STATE_NONE = 0,
   OPPLE_SERIAL_STATE_GETDEVICESNAPSHOT,
   OPPLE_SERIAL_STATE_NORMAL,
}opple_serial_state_t;

/* Minimum timeout interval of usart request in millisecond */
#define OPPLE_SERIAL_REQUEST_TIMEOUT_MIN_MS               (500)
/* Maximum timeout interval of usart request in millisecond */
#define OPPLE_SERIAL_REQUEST_TIMEOUT_MAX_MS               (1500)
#define OPPLE_SERIAL_REQUEST_TIMEOUT_DEFAULT_MS           (1000)

#define OPPLE_SERIAL_PROCESS_TIMEOUT_MS                  (1000)


typedef void (*opple_serial_response_fn)(opple_frame_cmd_t cmd, opple_platform_id_t id, uint8_t *data, uint16_t len, void *arg);
typedef void (*opple_serial_report_fn)(opple_frame_cmd_t cmd, uint8_t *data, uint16_t len, void *arg);
typedef void (*opple_serial_network_config_fn)(opple_network_config_type_t type);
typedef int32_t (*opple_serial_setmode_fn)(void *arg);
typedef int32_t (*opple_serial_getmode_fn)(void *arg);

typedef struct
{
    opple_serial_report_fn                m_reportcb;
    opple_serial_response_fn              m_responsecb;
    opple_serial_network_config_fn        m_network_configcb;

}opple_serial_callbacks_t;

typedef struct
{
    opple_serial_setmode_fn  m_setmode;
    opple_serial_getmode_fn  m_getmode;

}opple_serial_mode_callbacks_t;

typedef struct
{
    qm_list_node_t node;
    uint8_t len;
    uint8_t resend;
    uint8_t data[1];
}opple_frame_info_t;

typedef struct{
    uint8_t *data;
    uint16_t len;
}opple_serial_data_info_t;

typedef struct
{
    opple_platform_id_t platform_id;
    uint32_t time;
    uint8_t is_update;
    uint8_t local_id;
    uint8_t retry_count;
    uint8_t *data;
    int is_send;
}opple_serial_id_info_t;

typedef struct
{
    uint8_t network_status;
    uint8_t packet_id;
    uint8_t *pwrite_buf;
    uint32_t write_buf_size; /* Specify size of write-buffer in byte */

    uint8_t *pread_buf;
    uint32_t read_pos;
    uint32_t read_buf_size;  /* Specify size of read-buffer in byte */
    void *serial_mutex;
    void *send_sem;
    uint8_t  is_start_recv;
    uint8_t  recv_timeout_count;


    opple_parse_state_t parse_state;
    uint8_t packet_len;
    uint8_t need_len;
    opple_serial_state_t state;

    qm_utils_time_t process_get_time;
    uint16_t process_get_interval; //unit ms

    qm_utils_time_t send_interval_time;
    uint32_t send_interval; //unit ms

    qm_utils_time_t id_check_time;
    uint32_t id_check_interval; //unit ms

    qm_list_t *send_queue;
    qm_list_t *id_queue;
#ifdef CONFIG_OPPLE_SERIAL_RESEND_SUPPORT
    qm_list_t *down_queue;
#endif

    qm_tlv_ctx_t *snapshot_tlv_ctx;
    qm_tlv_ctx_t *tlv_ctx;

    void *handle;

    opple_serial_callbacks_t callbacks;

}opple_serial_context_t;


typedef struct
{
    uint8_t *pread_buf;
    uint32_t read_buf_size;  /* Specify size of read-buffer in byte */
    opple_serial_callbacks_t callbacks;

}opple_serial_param_t;

int32_t opple_serial_init( opple_serial_param_t *serial_param);
int32_t opple_serial_recv_loop(void);
int32_t opple_serial_send_loop(void);
int32_t opple_serial_cloud_set(opple_platform_id_t id, uint8_t *data, uint8_t len);
int32_t opple_serial_cloud_get(opple_platform_id_t id, uint8_t *data, uint8_t len);
int32_t opple_serial_cloud_action(opple_platform_id_t id, uint8_t *data, uint8_t len);
int32_t opple_serial_beacon_set(opple_platform_id_t id, uint8_t *data, uint8_t len);

opple_module_network_status_t opple_serial_module_network_status_get(void);
int32_t opple_serial_module_network_status_set(opple_module_network_status_t network_status);
int32_t opple_serial_module_remote_pair_status_set(opple_remote_pair_status_t pair_status);
int32_t opple_serial_module_reset(void);

int32_t opple_serial_mode_register(uint8_t key, opple_serial_setmode_fn setmode_fn, opple_serial_getmode_fn getmode_fn);

opple_serial_context_t *opple_serial_context_get(void);
int32_t opple_serial_state_set(opple_serial_state_t state);
int32_t opple_serial_state_get(opple_serial_state_t *state);

int32_t opple_serial_updated_snapshot_get(qm_tlv_ctx_t *tlv_ctx, uint8_t *count, bool_t copy);
int32_t opple_serial_snapshot_get(qm_tlv_ctx_t *tlv_ctx);
qm_tlv_ctx_t *opple_serial_snapshot_ctx_get(void);

#endif

#endif
