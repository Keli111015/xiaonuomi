#ifndef __QM_IOT_COMMON_H__
#define __QM_IOT_COMMON_H__

#if defined(__cplusplus)
extern "C" {
#endif


#define QM_IOT_COMMON_DID_MAX_LEN       (16)
#define QM_IOT_COMMON_TOPIC_MAX_LEN     (64)
#define QM_IOT_COMMON_MSG_ID_MAX_NUM    (65535)


#define QM_IOT_DEV_UP_TOPIC_FMT                  "/%u/%u/device/up"
#define QM_IOT_DEV_DOWN_TOPIC_FMT                "/%u/%u/device/down"

#define QM_IOT_COMMON_ID_KEY                "id"
#define QM_IOT_COMMON_TYPE_KEY              "type"
#define QM_IOT_COMMON_SOURCE_KEY            "source"
#define QM_IOT_COMMON_PARAMS_KEY            "params"
#define QM_IOT_COMMON_PAYLOAD_KEY           "payload"
#define QM_IOT_COMMON_MESSAGE_KEY           "message"
#define QM_IOT_COMMON_TIMESTAMP_KEY         "timestamp"
#define QM_IOT_COMMON_LOG_LEVEL_KEY         "logLevel"

#define QM_IOT_COMMON_LOG_INFO_METHOD       "logInfo"
#define QM_IOT_COMMON_LOG_TOPIC_SUFFIX      "log"
#define QM_IOT_COMMON_LOG_PAYLOAD_KEY       "payload"

/** logLevel 取值。 */
#define QM_IOT_COMMON_LOG_LEVEL_NONE        "NONE"
#define QM_IOT_COMMON_LOG_LEVEL_ERROR       "ERROR"
#define QM_IOT_COMMON_LOG_LEVEL_WARN        "WARN"
#define QM_IOT_COMMON_LOG_LEVEL_INFO        "INFO"
#define QM_IOT_COMMON_LOG_LEVEL_DEBUG       "DEBUG"

/** 单条日志 type 取值。 */
#define QM_IOT_COMMON_LOG_TYPE_COMM         "Comm"
#define QM_IOT_COMMON_LOG_TYPE_CMD          "Cmd"
#define QM_IOT_COMMON_LOG_TYPE_DATA         "Data"
#define QM_IOT_COMMON_LOG_TYPE_ERROR        "Error"

#define QM_IOT_COMMON_PARAMS_SET_KEY        "set"
#define QM_IOT_COMMON_PARAMS_GET_KEY        "get"
#define QM_IOT_COMMON_PARAMS_REPORT_KEY     "report"


#define QM_IOT_COMMON_PARAMS_TYPE_KEY                    "type"
#define QM_IOT_COMMON_PARAMS_MAC_TYPE_KEY                "mac"
#define QM_IOT_COMMON_PARAMS_SSID_TYPE_KEY               "ssid"
#define QM_IOT_COMMON_PARAMS_BSSID_TYPE_KEY              "bssid"
#define QM_IOT_COMMON_PARAMS_CHANNEL_TYPE_KEY            "channel"
#define QM_IOT_COMMON_PARAMS_RSSI_TYPE_KEY               "rssi"
#define QM_IOT_COMMON_PARAMS_IP_TYPE_KEY                 "ip"
#define QM_IOT_COMMON_PARAMS_MODULE_TYPE_KEY             "moudle"

#define QM_IOT_COMMON_PARAM_LONGITUDE_KEY         "longitude"
#define QM_IOT_COMMON_PARAM_LATITUDE_KEY          "latitude"
#define QM_IOT_COMMON_PARAM_ICCID_KEY             "iccId"
#define QM_IOT_COMMON_PARAMS_REBOOT_KEY           "reboot"
#define QM_IOT_COMMON_PARAMS_REBOOTREPLY_KEY      "rebootReply"

typedef enum {
    QM_IOT_CODE_SUCCESS = 0,
    QM_IOT_CODE_ERROR,
}qm_iot_code_t;

typedef enum {
    QM_IOT_NETWORK_TYPE_NONE = 0,
    QM_IOT_NETWORK_TYPE_WIFI,
    QM_IOT_NETWORK_TYPE_4G,
} qm_iot_network_type_t;


#if defined(__cplusplus)
}
#endif

#endif  /* __QM_IOT_COMMON_H__ */
