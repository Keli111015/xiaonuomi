#ifndef PRODTST_COMMON_H
#define PRODTST_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif


#define CONFIG_PRODTST_BOX_HOPS   1

#ifndef CONFIG_PRODTST_TEST_CHANNEL_MAX_NUM
#define CONFIG_PRODTST_TEST_CHANNEL_MAX_NUM     (6)
#endif
 
enum{ 
    PRODTST_COMM_CODE_SUCCESS,
    PRODTST_COMM_CODE_TIMEOUT, 
    PRODTST_COMM_CODE_PARAM_ERR,
    PRODTST_COMM_CODE_DEVICE_ERR,
};

enum{ 
    PRODTST_RESULT_SUCCESS,
    PRODTST_RESULT_FAIL, 
    PRODTST_RESULT_TEST_ABORTED,
};  

enum{ 
    PRODTST_WIRELESS_CODE_SUCCESS,
    PRODTST_WIRELESS_CODE_ERROR, 
};

typedef enum{
    PRODTST_TEST_TYPE_NONE,
    PRODTST_TEST_TYPE_WIRED,
    PRODTST_TEST_TYPE_WIFI,
    PRODTST_TEST_TYPE_BLE_GATT,
    PRODTST_TEST_TYPE_BLE_ADV,
    PRODTST_TEST_TYPE_BLE_MESH,
    PRODTST_TEST_TYPE_MAX,
}prodtst_test_type_t;

typedef enum{
    PRODTST_COMM_MSG_TYPE_NONE,
    PRODTST_COMM_MSG_TYPE_CONFIG, 
    PRODTST_COMM_MSG_TYPE_CONFIG_REPLY,
    PRODTST_COMM_MSG_TYPE_READY,
    PRODTST_COMM_MSG_TYPE_READY_REPLY,
    PRODTST_COMM_MSG_TYPE_BEFORE_TEST,
    PRODTST_COMM_MSG_TYPE_BEFORE_TEST_REPLY,
    PRODTST_COMM_MSG_TYPE_TEST,
    PRODTST_COMM_MSG_TYPE_TEST_REPLY,
    PRODTST_COMM_MSG_TYPE_SCRIPT_TEST,
    PRODTST_COMM_MSG_TYPE_SCRIPT_TEST_REPLY,
    PRODTST_COMM_MSG_TYPE_AFTER_TEST,
    PRODTST_COMM_MSG_TYPE_AFTER_TEST_REPLY,
    PRODTST_COMM_MSG_TYPE_RESULT, 
    PRODTST_COMM_MSG_TYPE_RESULT_REPLY,
    PRODTST_COMM_MSG_TYPE_REPORT, 
    PRODTST_COMM_MSG_TYPE_REPORT_REPLY,
    PRODTST_COMM_MSG_TYPE_WIRELESS_LIST,
    PRODTST_COMM_MSG_TYPE_WIRELESS_LIST_REPLY,
    PRODTST_COMM_MSG_TYPE_INFORM,
    PRODTST_COMM_MSG_TYPE_PROGRESS,
    PRODTST_COMM_MSG_TYPE_MAX
}prodtst_comm_msg_type_t;

typedef enum {
    PRODTST_HD_UART_NONE,
    PRODTST_HD_UART1,
    PRODTST_HD_UART2,
    PRODTST_HD_UART3,
    PRODTST_HD_UART4,
    PRODTST_HD_UART5,
    PRODTST_HD_UART6,
    prodtst_HD_UART_MAX
}prodtst_hd_uart_id_t;

typedef enum {
    prodtst_HD_IO_NONE,
    PRODTST_HD_IO0,
    PRODTST_HD_IO1,
    PRODTST_HD_IO2,
    PRODTST_HD_IO3,
    PRODTST_HD_IO4,
    PRODTST_HD_IO5,
    PRODTST_HD_IO6,
    PRODTST_HD_IO7,
    prodtst_HD_IO_MAX
}prodtst_hd_io_id_t;

typedef enum{
    PRODTST_DATA_FORMAT_NONE,
    PRODTST_DATA_FORMAT_AT,
    PRODTST_DATA_FORMAT_XJIANG_UART,
    PRODTST_DATA_FORMAT_HEX,
    PRODTST_DATA_FORMAT_STRING,
    PRODTST_DATA_FORMAT_MAX,
}prodtst_data_format_t;

typedef enum{
    PRODTST_HD_TYPE_NONE,
    PRODTST_HD_TYPE_UART,
    PRODTST_HD_TYPE_RS232,
    PRODTST_HD_TYPE_BUTTON,
    PRODTST_HD_TYPE_RELAY,
    PRODTST_HD_TYPE_BUZZER,
    PRODTST_HD_TYPE_IO_PIN,
    PRODTST_HD_TYPE_I_PIN,
    PRODTST_HD_TYPE_O_PIN,
    PRODTST_HD_TYPE_WIFI_AP,
    PRODTST_HD_TYPE_BLE_CLIENT,
    PRODTST_HD_TYPE_BLE_SCAN,
    PRODTST_HD_TYPE_BLE_ADV,
    PRODTST_HD_TYPE_BLE_MESH,
    PRODTST_HD_TYPE_VIRTUAL,
    PRODTST_HD_TYPE_LED,
    PRODTST_HD_TYPE_MAX
}prodtst_hd_type_t; 

typedef enum{
    PRODTST_MAIN_TYPE_NONE,
    PRODTST_MAIN_TYPE_BOX,
    PRODTST_MAIN_TYPE_HUB,
    PRODTST_MAIN_TYPE_VOLTAMMETER,
    PRODTST_MAIN_TYPE_IO,
    PRODTST_MAIN_TYPE_RELAY,
    PRODTST_MAIN_TYPE_LED,
    PRODTST_MAIN_TYPE_VOICE,
    PRODTST_MAIN_TYPE_MAX
}prodtst_main_type_t;

typedef enum{
    PRODTST_CHANNEL_TYPE_NONE,
    PRODTST_CHANNEL_TYPE_INNER,
    PRODTST_CHANNEL_TYPE_DEVICE,
    PRODTST_CHANNEL_TYPE_TEST,
    PRODTST_CHANNEL_TYPE_CUSTOM,
    PRODTST_CHANNEL_TYPE_MAX
}prodtst_channel_type_t;


#ifndef CONFIG_PRODTST_BOX_SUPPORT
#define CONFIG_PRODTST_BOX_SUPPORT    1
#endif

#ifndef CONFIG_PRODTST_HUB_SUPPORT
#define CONFIG_PRODTST_HUB_SUPPORT    0
#endif

#ifndef CONFIG_PRODTST_HUB_LOCAL_MAIN_COMM_ID
#define CONFIG_PRODTST_HUB_LOCAL_MAIN_COMM_ID    1
#endif


#define CONFIG_PRODTST_UUID_LEN             (32)

#define PRODTST_PROTOCOL_VERSION            "1.0"

#define PRODTST_BYTEORDER_BIG_ENDIAN        "bigEndian"
#define PRODTST_BYTEORDER_LITTLE_ENDIAN     "littleEndian"
//prodtst method
#define PRODTST_METHOD_CONFIG               "config"
//define json key
#define PRODTST_JSON_KEY_METHOD                     "method"
#define PRODTST_JSON_KEY_VERSION                    "version"
#define PRODTST_JSON_KEY_PARAMS                     "params"
#define PRODTST_JSON_KEY_CODE                       "code"
#define PRODTST_JSON_KEY_DATA                       "data"
#define PRODTST_JSON_KEY_VALUE                      "value"

#define PRODTST_JSON_KEY_CHANNEL_NUM                "channelNum"
#define PRODTST_JSON_KEY_CONFIG                     "config"
#define PRODTST_JSON_KEY_BUTTON_START               "buttonStart"
#define PRODTST_JSON_KEY_TEST_TYPE                  "testType"

#define PRODTST_JSON_KEY_WAIT_TIME                  "waitTime"
#define PRODTST_JSON_KEY_HD_TYPE                    "hdType"
#define PRODTST_JSON_KEY_CASE_ID                    "caseId"
#define PRODTST_JSON_KEY_MAC                        "mac"
#define PRODTST_JSON_KEY_CHANNEL_ID                 "channelId"
#define PRODTST_JSON_KEY_DEST                       "dest"
#define PRODTST_JSON_KEY_CFG                        "cfg"
#define PRODTST_JSON_KEY_RESULT                     "result"

#define PRODTST_JSON_KEY_UUID                       "uuid"
#define PRODTST_JSON_KEY_DEST                       "dest"
#define PRODTST_JSON_KEY_SRC                        "src"
#define PRODTST_JSON_KEY_FORMAT                     "format"
#define PRODTST_JSON_KEY_RECV_RULE                  "recvRule"
#define PRODTST_JSON_KEY_HEAD                       "head"
#define PRODTST_JSON_KEY_TAIL                       "tail"
#define PRODTST_JSON_KEY_LEN_POS                    "lenPos"
#define PRODTST_JSON_KEY_BYTE_ORDER                 "byteOrder"
#define PRODTST_JSON_KEY_LEN_RANGE                  "lenRange"
#define PRODTST_JSON_KEY_FIX_LEN                    "fixLen"
#define PRODTST_JSON_KEY_REMAIN_LEN                 "remainLen"
#define PRODTST_JSON_KEY_MATCH_HEX                   "matchHex"
#define PRODTST_JSON_KEY_MATCH_POS                   "matchPos"

//uart config
#define PRODTST_JSON_KEY_CHANNEL_TYPE               "channelType"
#define PRODTST_JSON_KEY_BRAUDRATE                  "braudrate"
#define PRODTST_JSON_KEY_DATABITS                   "databits"
#define PRODTST_JSON_KEY_PARITY                     "parity"
#define PRODTST_JSON_KEY_STOPBITS                   "stopbits"

//ble mesh
#define PRODTST_JSON_KEY_BLE_MESH_PID               "pid"

//ready
#define PRODTST_CHANNEL_TYPE_WIRELESS      "wireless"
#define PRODTST_CHANNEL_TYPE_WIRED         "wired"
#define PRODTST_JSON_KEY_CONN_TYPE         "connType"
#define PRODTST_JSON_KEY_CHANNEL_INFO      "channelInfo"
#define PRODTST_JSON_KEY_MAC               "mac"
#define PRODTST_JSON_KEY_CHANNEL_ID        "channelId"
#define PRODTST_JSON_KEY_ACK               "ack"
#define PRODTST_JSON_KEY_CONN_CODE         "connCode"

#ifdef __cplusplus
}
#endif

#endif