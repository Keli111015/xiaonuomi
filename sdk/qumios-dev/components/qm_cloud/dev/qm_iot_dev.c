#include "qm_cloud_log.h"
#include "qm.h"
#include "qm_time.h"
#include "qm_iot_dev.h"
#include "qm_iot_common.h"
#include "qm_iot_config.h"
#include "qm_work.h"
#include "cJSON.h"
#include "json_parser.h"
#include "qm_utils_string.h"

#define LOG_TAG  "dev"

    #if CONFIG_QM_IOT_DEVMNT_SUPPORT
#include "qm_iot_mqtt.h"
#include "qm_utils_list.h"

#define QM_IOT_DEV_SUB_RETRY_COUNT                  (10)

#define QM_IOT_DEV_RESET_RETRY_TIMEOUT              (10 * 1000)

#define QM_IOT_DEV_TOPIC_MAX_LEN                     64
#define QM_IOT_DEV_PARAMS_PAYLOAD_MAX_LEN            64
#define QM_IOT_DEV_GENERIC_PAYLOAD_MAX_LEN           256
#define QM_IOT_DEV_TOPIC_METHOD_LEN                  32
#define QM_IOT_DEV_HANDLER_PARAMS_MAX_STORE    (QM_IOT_DEV_TOPIC_METHOD_LEN - 1)


/** MQTT method 名（rodata 各保留一份；Reply 类 Topic/JSON 用 @ref QM_IOT_DEV_TOPIC_REPLY_SUFFIX 拼接）。 */
static const char QM_IOT_DEV_M_BIND_NOTIFY[]   = "bindDevNotify";
static const char QM_IOT_DEV_M_DELDEV[]        = "delDev";
static const char QM_IOT_DEV_M_DELDEVRELY[]    = "delDevReply";
static const char QM_IOT_DEV_M_DELDEV_NOTIFY[] = "delDevNotify";
/** 设备 MQTT 在线绑定准备完成上报 method。 */
static const char QM_IOT_DEV_M_DEVICE_BIND_ONLINE[] = "deviceBindOnline";
/** 设备 MQTT 在线绑定准备完成上报的云端回复 method。 */
static const char QM_IOT_DEV_M_DEVICE_BIND_ONLINE_REPLY[] = "deviceBindOnlineReply";
#if CONFIG_QM_IOT_DEV_IR_SUPPORT
static const char QM_IOT_DEV_M_SEND_IR[]       = "sendIRMessage";
#endif
#if CONFIG_QM_IOT_DEV_INFO_SUPPORT
static const char QM_IOT_DEV_M_DEV_INFO[]         = "devInfo";
static const char QM_IOT_DEV_M_DEV_INFO_REPLY[]   = "devInfoReply";
#endif
#if CONFIG_QM_IOT_LOG_PUSH_SUPPORT
static const char QM_IOT_DEV_M_LOG[]                = QM_IOT_COMMON_LOG_TOPIC_SUFFIX;
#endif

#define QM_IOT_DEV_UP_PARAMS_FMT                    "{\"method\":\"%s\",\"id\":%u}"
#define QM_IOT_DEV_UP_PARAMS_PAYLOAD_FMT            "{\"method\":\"%s\",\"id\":%u,\"code\":%d}"
#define QM_IOT_DEV_UP_PARAMS_WITH_PAYLOAD_FMT       "{\"method\":\"%s\",\"id\":%u,\"params\":%.*s}"
#define QM_IOT_DEV_UP_PARAMS_WITH_PAYLOAD_CODE_FMT  "{\"method\":\"%s\",\"id\":%u,\"params\":%.*s,\"code\":%d}"

/**
 * 通用 Topic / JSON 格式串（method 运行时传入，避免为每个 method 复制整段格式串）。
 */
#define QM_IOT_DEV_TOPIC_FMT                "/%u/%u/device/%s"
#define QM_IOT_DEV_TOPIC_REPLY_FMT          "/%u/%u/device/%s%s"
#define QM_IOT_DEV_TOPIC_REPLY_SUFFIX       "Reply"

#define QM_IOT_DEV_JSON_ID_FMT                  "{\"method\":\"%s\",\"id\":%d}"
#define QM_IOT_DEV_JSON_ID_CODE0_FMT            "{\"method\":\"%s\",\"id\":%d,\"code\":0}"
#define QM_IOT_DEV_JSON_ID_CODE0_REPLY_FMT      "{\"method\":\"%s%s\",\"id\":%d,\"code\":0}"
#define QM_IOT_DEV_JSON_ID_CODE_REPLY_FMT       "{\"method\":\"%s%s\",\"id\":%d,\"code\":%d}"
#define QM_IOT_DEV_JSON_ID_PARAMS_FMT           "{\"method\":\"%s\",\"id\":%d,\"params\":%s}"

#if CONFIG_QM_IOT_DEV_INFO_SUPPORT
/** devInfo params 中 WiFi 网络类型的 type 字段取值。 */
#define QM_IOT_DEV_INFO_WIFI_TYPE           "wifi"
/** devInfo params 中 4G 网络类型的 type 字段取值。 */
#define QM_IOT_DEV_INFO_4G_TYPE             "4g"
#endif

#define QM_IOT_DEV_MSG_ID_MAX          (65535)

#define PARAMS_KEY                  "params"
#define MESSAGE_KEY                 "message"
#define METHOD_KEY                  "method"
#define SUBMETHOD_KEY               "subMethod"
#define CODE_KEY                    "code"
#define MSG_ID_KEY                  "id"
#define TIMESTAMP_KEY               "timestamp"

typedef struct {
    qm_iot_dev_recv_type_t  recv_type;
    char *params;
    qm_iot_mqtt_recv_handler_t recv_handler;
    char topic[QM_IOT_DEV_TOPIC_MAX_LEN];
}dev_handler_t;

typedef struct qm_iot_dev_handler{
    void *user_data;
    qm_iot_mqtt_recv_handler_t recv_handler;
    char params[QM_IOT_DEV_TOPIC_METHOD_LEN];
}qm_iot_dev_handler_t;

/**
 * @brief 供 qm_list_find 使用的键：下行 payload 中 method 片段指针与长度。
 */
typedef struct {
    /** @brief method 指针（不必以 '\\0' 结尾）。 */
    const char *method;
    /** @brief method 字节长度。 */
    int method_eff_len;
} qm_iot_dev_topic_find_key_t;


typedef struct {
    uint16_t msg_id;
    void *mqtt_handle;
    void *userdata;
    /** 是否已完成基于 pid/did 的 MQTT 预订阅。 */
    uint8_t mqtt_pre_sub_done;
    qm_work_t retry_work;
    /** @brief 动态注册的下行 method 与处理函数，使用 qm_list 管理。 */
    qm_list_t *dev_topic_list;
    qm_iot_dev_recv_handler_t recv_handler;
} dev_handle_t;

/**
 * @brief 从 dev 会话解析 IOT 会话 句柄（@ref QM_IOT_DEVOPT_USERDATA 设置的 @ref qm_iot_init 返回值）。
 */
static void *dev_iot_handle(dev_handle_t *dev)
{
    if (dev == NULL) {
        return NULL;
    }

    return dev->userdata;
}

static int qm_iot_dev_topic_snprintf(void *iot_handle, char *buf, int buf_len, const char *method)
{
    return qm_snprintf(buf, buf_len, QM_IOT_DEV_TOPIC_FMT,
                       qm_iot_pid_get(iot_handle), qm_iot_did_get(iot_handle), method);
}

#if CONFIG_QM_IOT_DEV_IR_SUPPORT
static void mqtt_ir_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata);
#endif

#if CONFIG_QM_IOT_DEV_INFO_SUPPORT
static void mqtt_devinfo_reply_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata);
#endif
/**
 * @brief 处理 deviceBindOnlineReply 下行回复，仅记录云端返回 id/code。
 */
static void mqtt_device_bind_online_reply_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata);

static void qm_iot_dev_unbind_reply(void *handle);
static void mqtt_reset_reply_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata);
static void mqtt_bind_notify_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata);
static void mqtt_unbind_notify_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata);
static void mqtt_reboot_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata);

static void mqtt_down_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata);


/** @copydoc qm_iot_dev_reply_publish（声明见 qm_iot_dev_internal.h） */
int32_t qm_iot_dev_reply_publish(void *handle,
                                 char *param,
                                 char *submethod,
                                 uint32_t msg_id,
                                 int code,
                                 char *payload,
                                 int payload_len);

/**
 * @brief 判断下行 payload 中的 method 片段是否与已注册项的 params 一致（与原 strncmp + 长度上限逻辑一致）。
 *
 * @param[in] h 列表节点上的 qm_iot_dev_handler_t。
 * @param[in] method method 指针（不必以 '\\0' 结尾）。
 * @param[in] method_len method 字节长度。
 * @return 匹配返回 QM_TRUE，否则 QM_FALSE。
 */
/**
 * @brief qm_list 的 match：a 为 qm_iot_dev_topic_find_key_t*，b 为节点 val（qm_iot_dev_handler_t*）。
 *
 * @return 匹配返回 QM_TRUE，否则 QM_FALSE。
 */
static int qm_iot_dev_topic_list_match(void *a, void *b)
{
    qm_iot_dev_topic_find_key_t *key = (qm_iot_dev_topic_find_key_t *)a;
    qm_iot_dev_handler_t *h = (qm_iot_dev_handler_t *)b;
    int reglen = 0;
    int json_eff = 0;

    if (key == NULL || key->method == NULL || key->method_eff_len <= 0 || h == NULL) {
        return QM_FALSE;
    }

    reglen = (int)strlen(h->params);
    if (reglen == 0) {
        return QM_FALSE;
    }

    json_eff = key->method_eff_len;
    if (json_eff > QM_IOT_DEV_HANDLER_PARAMS_MAX_STORE) {
        json_eff = QM_IOT_DEV_HANDLER_PARAMS_MAX_STORE;
    }

    if (reglen == json_eff && strncmp(h->params, key->method, (size_t)reglen) == 0) {
        return QM_TRUE;
    }

    return QM_FALSE;
}


/**
 * @brief 根据 method 在 dev_topic 列表中查找注册项（qm_list_find）。
 */
static qm_iot_dev_handler_t *qm_iot_dev_topic_find(dev_handle_t *owner, char *method, int method_len)
{
    qm_iot_dev_topic_find_key_t key = {0};
    qm_list_node_t *node = NULL;

    if (owner == NULL || method == NULL || method_len == 0 || owner->dev_topic_list == NULL) {
        return NULL;
    }

    key.method = method;
    key.method_eff_len = method_len;

    node = qm_list_find(owner->dev_topic_list, &key);
    if (node == NULL) {
        return NULL;
    }

    return (qm_iot_dev_handler_t *)qm_list_node_val_get(node);
}


void *qm_iot_dev_init(void)
{
    int ret = QM_EOK;
    dev_handle_t *dev_handle = NULL;
    dev_handle = (dev_handle_t*)qm_malloc(sizeof(dev_handle_t));
    if (dev_handle == NULL) {
        return NULL;
    }
    memset(dev_handle, 0, sizeof(dev_handle_t));

    dev_handle->dev_topic_list = qm_list_new();
    if (dev_handle->dev_topic_list == NULL) {
        qm_free(dev_handle);
        return NULL;
    }
    dev_handle->dev_topic_list->match = qm_iot_dev_topic_list_match;

    ret = qm_iot_dev_down_register(dev_handle, (char *)QM_IOT_COMMON_PARAMS_REBOOT_KEY,
                                     mqtt_reboot_recv_handler, NULL);
    if (ret != QM_EOK) {
        qm_list_destroy(dev_handle->dev_topic_list);
        qm_free(dev_handle);
        return NULL;
    }

    ret = qm_iot_dev_down_register(dev_handle, (char *)QM_IOT_DEV_M_DELDEV_NOTIFY,
                                     mqtt_unbind_notify_recv_handler, NULL);
    if (ret != QM_EOK) {
        qm_list_destroy(dev_handle->dev_topic_list);
        qm_free(dev_handle);
        return NULL;
    }

    ret = qm_iot_dev_down_register(dev_handle, (char *)QM_IOT_DEV_M_DELDEVRELY,
                                     mqtt_reset_reply_recv_handler, NULL);
    if (ret != QM_EOK) {
        qm_list_destroy(dev_handle->dev_topic_list);
        qm_free(dev_handle);
        return NULL;
    }

    ret = qm_iot_dev_down_register(dev_handle, (char *)QM_IOT_DEV_M_BIND_NOTIFY,
                                     mqtt_bind_notify_recv_handler, NULL);
    if (ret != QM_EOK) {
        qm_list_destroy(dev_handle->dev_topic_list);
        qm_free(dev_handle);
        return NULL;
    }

    ret = qm_iot_dev_down_register(dev_handle, (char *)QM_IOT_DEV_M_DEVICE_BIND_ONLINE_REPLY,
                                     mqtt_device_bind_online_reply_recv_handler, NULL);
    if (ret != QM_EOK) {
        qm_list_destroy(dev_handle->dev_topic_list);
        qm_free(dev_handle);
        return NULL;
    }

#if CONFIG_QM_IOT_DEV_INFO_SUPPORT
    ret = qm_iot_dev_down_register(dev_handle, (char *)QM_IOT_DEV_M_DEV_INFO_REPLY,
                                     mqtt_devinfo_reply_recv_handler, NULL);
    if (ret != QM_EOK) {
        qm_list_destroy(dev_handle->dev_topic_list);
        qm_free(dev_handle);
        return NULL;
    }
#endif

    return dev_handle;
}

static uint32_t qm_iot_dev_msg_id_get(void *handle)
{
    dev_handle_t *subdev_handle = (dev_handle_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }
    subdev_handle->msg_id = (subdev_handle->msg_id == QM_IOT_DEV_MSG_ID_MAX) ? 1 : subdev_handle->msg_id + 1;
    return subdev_handle->msg_id;
}

/**
 * @brief MQTT 与 session 句柄就绪后注册 dev 下行 topic 预订阅（仅执行一次）。
 */
static void dev_mqtt_sub_if_ready(dev_handle_t *dev_handle)
{
    char topic[QM_IOT_DEV_TOPIC_MAX_LEN] = {0};

    if (dev_handle == NULL || dev_handle->mqtt_handle == NULL || dev_iot_handle(dev_handle) == NULL) {
        return;
    }

    if (dev_handle->mqtt_pre_sub_done) {
        return;
    }

    dev_handle->mqtt_pre_sub_done = QM_TRUE;
    qm_snprintf(topic, QM_IOT_DEV_TOPIC_MAX_LEN, QM_IOT_DEV_DOWN_TOPIC_FMT,
                qm_iot_pid_get(dev_iot_handle(dev_handle)),
                qm_iot_did_get(dev_iot_handle(dev_handle)));
    qm_iot_mqtt_pre_sub(dev_handle->mqtt_handle, topic, mqtt_down_recv_handler, QM_MQTT_QoS0, dev_handle);

#if CONFIG_QM_IOT_DEV_IR_SUPPORT
    memset(topic, 0, QM_IOT_DEV_TOPIC_MAX_LEN);
    qm_iot_dev_topic_snprintf(dev_iot_handle(dev_handle), topic, QM_IOT_DEV_TOPIC_MAX_LEN, QM_IOT_DEV_M_SEND_IR);
    qm_iot_mqtt_pre_sub(dev_handle->mqtt_handle, topic, mqtt_ir_recv_handler, QM_MQTT_QoS0, dev_handle);
#endif

}


#if CONFIG_QM_IOT_DEV_INFO_SUPPORT

/**
 * @brief 将 @ref qm_iot_devinfo_t 序列化为 devInfo params JSON 字符串。
 *
 * @param[in] info 设备信息快照；不可为 NULL。
 * @return 成功返回需由调用方 cJSON_free 释放的 JSON 字符串；失败返回 NULL。
 */
static char *qm_iot_dev_devinfo_params_create(const qm_iot_devinfo_t *info)
{
    cJSON *root = NULL;
    char *params = NULL;

    if (info == NULL) {
        return NULL;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    switch (info->type) {
        case QM_IOT_NETWORK_TYPE_WIFI:
            cJSON_AddStringToObject(root, QM_IOT_COMMON_PARAMS_TYPE_KEY, QM_IOT_DEV_INFO_WIFI_TYPE);
            cJSON_AddStringToObject(root, QM_IOT_COMMON_PARAMS_MAC_TYPE_KEY, info->data.wifi.mac);
            cJSON_AddStringToObject(root, QM_IOT_COMMON_PARAMS_BSSID_TYPE_KEY, info->data.wifi.bssid);
            cJSON_AddStringToObject(root, QM_IOT_COMMON_PARAMS_SSID_TYPE_KEY, info->data.wifi.ssid);
            cJSON_AddStringToObject(root, QM_IOT_COMMON_PARAMS_IP_TYPE_KEY, info->data.wifi.ip);
            cJSON_AddNumberToObject(root, QM_IOT_COMMON_PARAMS_RSSI_TYPE_KEY, (double)info->data.wifi.rssi);
            cJSON_AddNumberToObject(root, QM_IOT_COMMON_PARAMS_CHANNEL_TYPE_KEY, (double)info->data.wifi.channel);
            break;
        case QM_IOT_NETWORK_TYPE_4G:
            cJSON_AddStringToObject(root, QM_IOT_COMMON_PARAMS_TYPE_KEY, QM_IOT_DEV_INFO_4G_TYPE);
            cJSON_AddStringToObject(root, QM_IOT_COMMON_PARAM_ICCID_KEY, info->data.modem_4g.imsi);
            cJSON_AddStringToObject(root, QM_IOT_COMMON_PARAM_LONGITUDE_KEY,
                                    info->data.modem_4g.longitude);
            cJSON_AddStringToObject(root, QM_IOT_COMMON_PARAM_LATITUDE_KEY,
                                    info->data.modem_4g.latitude);
            break;
        default:
            cJSON_Delete(root);
            return NULL;
    }

    params = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return params;
}

/**
 * @brief devInfo 云端回复处理；由统一 device/down 的 method 分发到此处。
 */
static void mqtt_devinfo_reply_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    (void)handle;
    (void)userdata;

    if (packet == NULL || packet->type != QM_IOT_MQTTRECV_PUB) {
        return;
    }

    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "recv devinfo reply payload_len=%lu",
                  (unsigned long)packet->data.pub.payload_len);
}

/**
 * @brief 使用已组好的 params JSON 片段通过统一 device/up 发布 devInfo。
 *
 * @param[in] handle dev 会话句柄。
 * @param[in] params devInfo params JSON 片段；由本函数负责 cJSON_free。
 */
static int32_t qm_iot_dev_send_devinfo_payload(void *handle, char *params)
{
    char topic[QM_IOT_DEV_TOPIC_MAX_LEN] = {0};
    char *payload = NULL;
    dev_handle_t *dev_handle = (dev_handle_t *)handle;

    if (handle == NULL || params == NULL || dev_handle->mqtt_handle == NULL) {
        if (params != NULL) {
            cJSON_free(params);
        }
        return -QM_EINVAL;
    }

    payload = (char *)qm_malloc(QM_IOT_DEV_GENERIC_PAYLOAD_MAX_LEN);
    if (payload == NULL) {
        cJSON_free(params);
        return -QM_ENOMEM;
    }
    memset(payload, 0, QM_IOT_DEV_GENERIC_PAYLOAD_MAX_LEN);

    qm_snprintf(topic, QM_IOT_DEV_TOPIC_MAX_LEN, QM_IOT_DEV_UP_TOPIC_FMT,
                qm_iot_pid_get(dev_iot_handle(dev_handle)),
                qm_iot_did_get(dev_iot_handle(dev_handle)));
    qm_snprintf(payload, QM_IOT_DEV_GENERIC_PAYLOAD_MAX_LEN,
                QM_IOT_DEV_JSON_ID_PARAMS_FMT, QM_IOT_DEV_M_DEV_INFO,
                qm_iot_dev_msg_id_get(dev_handle), params);

    qm_iot_mqtt_pub(dev_handle->mqtt_handle, topic, (uint8_t *)payload, strlen(payload), QM_MQTT_QoS0);
    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_DOWNLINK, "publish devinfo topic_len=%lu payload_len=%lu",
                  (unsigned long)strlen(topic), (unsigned long)strlen(payload));

    cJSON_free(params);
    qm_free(payload);
    return QM_EOK;
}

int32_t qm_iot_dev_send_devinfo(void *handle, qm_iot_devinfo_t *param)
{
    char *params = NULL;

    if (handle == NULL || param == NULL) {
        return -QM_EINVAL;
    }

    params = qm_iot_dev_devinfo_params_create(param);
    if (params == NULL) {
        return -QM_EINVAL;
    }

    return qm_iot_dev_send_devinfo_payload(handle, params);
}

#endif


#if CONFIG_QM_IOT_DEV_IR_SUPPORT
static void qm_iot_dev_ir_reply(void *handle, uint32_t msg_id, int code)
{
    char topic[QM_IOT_DEV_TOPIC_MAX_LEN] = {0};
    char *payload = NULL;
    dev_handle_t *dev_handle = (dev_handle_t*)handle;
    if(handle == NULL){
        return;
    }

    payload = (char*)qm_malloc(QM_IOT_DEV_GENERIC_PAYLOAD_MAX_LEN);
    if(payload == NULL){
        return ;
    }
    memset(payload, 0, QM_IOT_DEV_GENERIC_PAYLOAD_MAX_LEN);

    qm_iot_dev_topic_reply_snprintf(dev_iot_handle(dev_handle), topic, QM_IOT_DEV_TOPIC_MAX_LEN, QM_IOT_DEV_M_SEND_IR);
    qm_snprintf(payload, QM_IOT_DEV_GENERIC_PAYLOAD_MAX_LEN, QM_IOT_DEV_JSON_ID_CODE_REPLY_FMT,
                QM_IOT_DEV_M_SEND_IR, QM_IOT_DEV_TOPIC_REPLY_SUFFIX, (int)msg_id, code);

    qm_iot_mqtt_pub(dev_handle->mqtt_handle, topic, (uint8_t *)payload, strlen(payload), QM_MQTT_QoS0);
    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_DOWNLINK, "publish ir reply topic_len=%lu payload_len=%lu",
                  (unsigned long)strlen(topic), (unsigned long)strlen(payload));
    qm_free(payload);
    payload = NULL;
}

static void mqtt_ir_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    int type = 0;

    char *str_id = NULL;
    int id_len = 0;

    char *str_params = NULL;
    int params_len = 0;

    char *str_message = NULL;
    int message_len = 0;

    qm_iot_dev_recv_t dev_recv = {0};
    dev_handle_t *dev_handle = (dev_handle_t*)userdata;
    if(dev_handle == NULL){
        return;
    }

    if(packet->type != QM_IOT_MQTTRECV_PUB){
        return ;
    }

    if(g_dev_handle == NULL){
        return ;
    }

    dev_recv.type = QM_IOT_DEVRECV_IR_NOTIFY;

    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "recv ir notify payload_len=%lu",
                  (unsigned long)packet->data.pub.payload_len);
    str_id = qm_json_get_value_by_name((char *)packet->data.pub.payload, packet->data.pub.payload_len, MSG_ID_KEY, &id_len, &type);
    dev_recv.data.ir_notify.msg_id = int_str_to_num(str_id, id_len);

    str_params = qm_json_get_value_by_name((char *)packet->data.pub.payload, packet->data.pub.payload_len, PARAMS_KEY, &params_len, &type);
    if(str_params == NULL){
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_DOWNLINK, "ir params failed !!");
        return;
    }

    str_message = qm_json_get_value_by_name(str_params, params_len, MESSAGE_KEY, &message_len, &type);
    if(str_message == NULL){
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_DOWNLINK, "ir str_message failed !!");
        return;
    }

    dev_recv.data.ir_notify.data.msg = str_message;
    dev_recv.data.ir_notify.msg_len = message_len;

    if(g_dev_handle->recv_handler){
        g_dev_handle->recv_handler(g_dev_handle, &dev_recv, g_dev_handle->userdata);
    }

    qm_iot_dev_ir_reply(g_dev_handle, dev_recv.data.ir_notify.msg_id, (int)QM_IOT_CODE_SUCCESS);
}
#endif

static void qm_iot_dev_reset_request_retry(void *arg)
{
    qm_iot_dev_reset_request(arg);
}


static void qm_iot_dev_reboot_reply(void *handle, uint32_t id)
{
    char topic[QM_IOT_DEV_TOPIC_MAX_LEN] = {0};
    char *payload = NULL;
    dev_handle_t *dev_handle = (dev_handle_t*)handle;
    if(handle == NULL){
        return;
    }

    payload = (char*)qm_malloc(QM_IOT_DEV_GENERIC_PAYLOAD_MAX_LEN);
    if(payload == NULL){
        return ;
    }
    memset(payload, 0, QM_IOT_DEV_GENERIC_PAYLOAD_MAX_LEN);

    qm_snprintf(topic, QM_IOT_DEV_TOPIC_MAX_LEN, QM_IOT_DEV_UP_TOPIC_FMT,
                qm_iot_pid_get(dev_iot_handle(dev_handle)),
                qm_iot_did_get(dev_iot_handle(dev_handle)));
    qm_snprintf(payload, QM_IOT_DEV_GENERIC_PAYLOAD_MAX_LEN, QM_IOT_DEV_UP_PARAMS_PAYLOAD_FMT,
                  QM_IOT_COMMON_PARAMS_REBOOTREPLY_KEY, id, 0);

    qm_iot_mqtt_pub(dev_handle->mqtt_handle, topic, (uint8_t *)payload, strlen(payload), QM_MQTT_QoS0);

    qm_free(payload);
    payload = NULL;
}

static void mqtt_reset_reply_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    int type = 0;
    char *str_id = NULL;
    int id_len = 0;
    int code = 0;
    char *str_code = NULL;
    int code_len = 0;
    qm_iot_dev_recv_t dev_recv = {0};
    dev_handle_t *dev_handle = (dev_handle_t*)handle;
    if(dev_handle == NULL){
        return;
    }

    if(packet->type != QM_IOT_MQTTRECV_PUB){
        return ;
    }

    dev_recv.type = QM_IOT_DEVRECV_RESET_REPLY;

    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "recv reply notify payload_len=%lu",
                  (unsigned long)packet->data.pub.payload_len);
    str_id = qm_json_get_value_by_name((char *)packet->data.pub.payload, packet->data.pub.payload_len, MSG_ID_KEY, &id_len, &type);
    dev_recv.data.reset_reply.msg_id = int_str_to_num(str_id, id_len);

    str_code = qm_json_get_value_by_name((char *)packet->data.pub.payload, packet->data.pub.payload_len, CODE_KEY, &code_len, &type);
    code = int_str_to_num(str_code, code_len);
    if(code != QM_IOT_CODE_SUCCESS){
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_DOWNLINK, "dev unbind failed ,need retry!!");
        return;
    }

    qm_cancel_delayed_action(&dev_handle->retry_work);
    dev_recv.data.reset_reply.code = QM_IOT_CODE_SUCCESS;
    if(dev_handle->recv_handler){
        dev_handle->recv_handler(dev_handle, &dev_recv, dev_handle->userdata);
    }
}

static void mqtt_unbind_notify_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    int type = 0;
    char *str_id = NULL;
    int id_len = 0;
    qm_iot_dev_recv_t dev_recv = {0};
    dev_handle_t *dev_handle = (dev_handle_t*)handle;

    if(dev_handle == NULL){
        return;
    }

    if(packet->type != QM_IOT_MQTTRECV_PUB){
        return ;
    }

    dev_recv.type = QM_IOT_DEVRECV_UNBIND_NOTIFY;
    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "recv unbind notify payload_len=%lu",
                  (unsigned long)packet->data.pub.payload_len);

    str_id = qm_json_get_value_by_name((char *)packet->data.pub.payload, packet->data.pub.payload_len, MSG_ID_KEY, &id_len, &type);
    dev_recv.data.reset_reply.msg_id = int_str_to_num(str_id, id_len);

    qm_iot_dev_reply_publish(dev_handle, 
                            (char *)QM_IOT_DEV_M_DELDEV_NOTIFY, NULL,  
                            dev_recv.data.reset_reply.msg_id, 0, NULL, 0);

    dev_recv.data.reset_reply.code = QM_IOT_CODE_SUCCESS;
    if(dev_handle->recv_handler){
        dev_handle->recv_handler(dev_handle, &dev_recv, dev_handle->userdata);
    }
}

static void mqtt_bind_notify_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    int type = 0;
    char *str_id = NULL;
    int id_len = 0;
    qm_iot_dev_recv_t dev_recv = {0};
    dev_handle_t *dev_handle = (dev_handle_t*)handle;

    if(dev_handle == NULL){
        return;
    }

    if(packet->type != QM_IOT_MQTTRECV_PUB){
        return ;
    }

    dev_recv.type = QM_IOT_DEVRECV_BIND_NOTIFY;
    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "recv bind notify payload_len=%lu",
                  (unsigned long)packet->data.pub.payload_len);

    str_id = qm_json_get_value_by_name((char *)packet->data.pub.payload, packet->data.pub.payload_len, MSG_ID_KEY, &id_len, &type);
    dev_recv.data.bind_notify.msg_id = int_str_to_num(str_id, id_len);

    qm_iot_dev_reply_publish(dev_handle, 
        (char *)QM_IOT_DEV_M_BIND_NOTIFY, NULL,  
        dev_recv.data.bind_notify.msg_id, 0, NULL, 0);

    if(dev_handle->recv_handler){
        dev_handle->recv_handler(dev_handle, &dev_recv, dev_handle->userdata);
    }
}

/**
 * @brief deviceBindOnline 云端回复处理；当前仅解析并记录回复结果，不向应用层释放事件。
 */
static void mqtt_device_bind_online_reply_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    int type = 0;
    char *str_id = NULL;
    int id_len = 0;
    char *str_code = NULL;
    int code_len = 0;
    int code = 0;
    dev_handle_t *dev_handle = (dev_handle_t *)handle;

    (void)userdata;

    if (dev_handle == NULL || packet == NULL || packet->type != QM_IOT_MQTTRECV_PUB) {
        return;
    }

    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "recv device bind online reply payload_len=%lu",
                  (unsigned long)packet->data.pub.payload_len);

    str_id = qm_json_get_value_by_name((char *)packet->data.pub.payload, packet->data.pub.payload_len, MSG_ID_KEY, &id_len, &type);
    str_code = qm_json_get_value_by_name((char *)packet->data.pub.payload, packet->data.pub.payload_len, CODE_KEY, &code_len, &type);
    code = int_str_to_num(str_code, code_len);

    QM_CLOUD_LOGI(LOG_TAG, QM_CLOUD_LOG_DOWNLINK, "device bind online reply id=%d code=%d",
            int_str_to_num(str_id, id_len),
            code);
}


static void mqtt_down_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    int type = 0;
    char *str_method = NULL;
    int method_len = 0;
    qm_iot_dev_handler_t *dev_topic = NULL;
    dev_handle_t *dev_handle = (dev_handle_t*)userdata;

    if (dev_handle == NULL) {
        return;
    }

    if(packet->type != QM_IOT_MQTTRECV_PUB){
        return ;
    }

    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "recv down notify payload_len=%lu",
                  (unsigned long)packet->data.pub.payload_len);

    str_method = qm_json_get_value_by_name((char *)packet->data.pub.payload, packet->data.pub.payload_len, METHOD_KEY, &method_len, &type);
    if (str_method == NULL || method_len <= 0) {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_DOWNLINK, "down ignore: no method");
        return;
    }

    dev_topic = qm_iot_dev_topic_find(dev_handle, str_method, method_len);
    if(dev_topic == NULL){
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_DOWNLINK,
                      "dev_topic: unhandled method=%.*s method_len=%d",
                      method_len, str_method, method_len);
        return ;
    }

    if(dev_topic->recv_handler){
        dev_topic->recv_handler(dev_handle, packet, dev_topic->user_data);
    }
}

static void mqtt_reboot_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    int type = 0;
    char *str_id = NULL;
    int id_len = 0;
    qm_iot_dev_recv_t dev_recv = {0};
    dev_handle_t *dev_handle = (dev_handle_t *)handle;

    (void)userdata;

    if (dev_handle == NULL) {
        return;
    }

    if(packet->type != QM_IOT_MQTTRECV_PUB){
        return ;
    }

    dev_recv.type = QM_IOT_DEVRECV_REBOOT_NOTIFY;

    str_id = qm_json_get_value_by_name((char *)packet->data.pub.payload, packet->data.pub.payload_len, MSG_ID_KEY, &id_len, &type);
    dev_recv.data.reboot_notify.msg_id = int_str_to_num(str_id, id_len);

    qm_iot_dev_reboot_reply(dev_handle, dev_recv.data.reboot_notify.msg_id);

    if(dev_handle->recv_handler){
        dev_handle->recv_handler(dev_handle, &dev_recv, dev_handle->userdata);
    }
}


int32_t qm_iot_dev_setopt(void *handle, qm_iot_dev_option_t option, void *data)
{
    qm_err_t ret = QM_EOK;
    dev_handle_t *dev_handle = (dev_handle_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }
    switch(option){

        case QM_IOT_DEVOPT_MQTT_HANDLE:
            dev_handle->mqtt_handle = data;
            dev_mqtt_sub_if_ready(dev_handle);
        break;
        case QM_IOT_DEVOPT_RECV_HANDLER:
            dev_handle->recv_handler = (qm_iot_dev_recv_handler_t)data;
        break;
        case QM_IOT_DEVOPT_USERDATA:
            dev_handle->userdata = data;
            dev_mqtt_sub_if_ready(dev_handle);
        break;
        case QM_IOT_DEVOPT_RESET_SUB:
            dev_handle->mqtt_pre_sub_done = QM_FALSE;
            dev_handle->mqtt_handle = NULL;
        break;
        default:
            ret = -QM_EINVAL;
        break;
    }
    return ret;
}

int32_t qm_iot_dev_deinit(void **handle)
{
    dev_handle_t *dev_handle = (dev_handle_t*)(*handle);
    if(handle == NULL){
        return -QM_EINVAL;
    }

#if 0
    qm_iot_dev_unsub_topic(dev_handle->mqtt_handle);
#endif
    qm_cancel_delayed_action(&dev_handle->retry_work);

    if (dev_handle->dev_topic_list != NULL) {
        qm_list_destroy(dev_handle->dev_topic_list);
        dev_handle->dev_topic_list = NULL;
    }

    qm_free(dev_handle);
    *handle = NULL;

    return QM_EOK;
}

int32_t qm_iot_dev_reset_request(void *handle)
{
    dev_handle_t *dev_handle = (dev_handle_t*)handle;
    int ret = QM_EOK;
    if(handle == NULL){
        return -QM_EINVAL;
    }

    qm_cancel_delayed_action(&dev_handle->retry_work);
    ret = qm_iot_dev_up_report(dev_handle, (char *)QM_IOT_DEV_M_DELDEV,
                               qm_iot_dev_msg_id_get(dev_handle), NULL, 0);
    if (ret != QM_EOK) {
        return ret;
    }

    qm_post_delayed_action(&dev_handle->retry_work, qm_iot_dev_reset_request_retry, dev_handle, QM_IOT_DEV_RESET_RETRY_TIMEOUT);
    return QM_EOK;
}

int32_t qm_iot_dev_send_device_bind_online(void *handle)
{
    if (handle == NULL) {
        return -QM_EINVAL;
    }

    return qm_iot_dev_up_report(handle, (char *)QM_IOT_DEV_M_DEVICE_BIND_ONLINE, 0, NULL, 0);
}

/**
 * @brief Build a device/up JSON payload with optional top-level subMethod.
 */
static char *qm_iot_dev_method_payload_create(const char *method,
                                              const char *submethod,
                                              uint32_t msg_id,
                                              qm_bool include_code,
                                              int code,
                                              const char *payload,
                                              int payload_len)
{
    cJSON *root = NULL;
    char *params_raw = NULL;
    char *json = NULL;

    if (method == NULL || payload_len < 0) {
        return NULL;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    if (cJSON_AddStringToObject(root, METHOD_KEY, method) == NULL ||
        cJSON_AddNumberToObject(root, MSG_ID_KEY, msg_id) == NULL) {
        goto out;
    }

    if (submethod != NULL && submethod[0] != '\0' &&
        cJSON_AddStringToObject(root, SUBMETHOD_KEY, submethod) == NULL) {
        goto out;
    }

    if (payload != NULL && payload_len > 0) {
        params_raw = (char *)qm_malloc((uint32_t)payload_len + 1U);
        if (params_raw == NULL) {
            goto out;
        }
        memcpy(params_raw, payload, (size_t)payload_len);
        params_raw[payload_len] = '\0';

        if (cJSON_AddRawToObject(root, PARAMS_KEY, params_raw) == NULL) {
            goto out;
        }
    }

    if (include_code &&
        cJSON_AddNumberToObject(root, CODE_KEY, code) == NULL) {
        goto out;
    }

    json = cJSON_PrintUnformatted(root);

out:
    if (params_raw != NULL) {
        qm_free(params_raw);
    }
    cJSON_Delete(root);
    return json;
}

int32_t qm_iot_dev_up_report_ex(void *handle,
                                char *param,
                                char *submethod,
                                uint32_t msg_id,
                                char *payload,
                                int payload_len)
{
    int ret;
    char topic[QM_IOT_DEV_TOPIC_MAX_LEN] = {0};
    char *up_payload = NULL;
    dev_handle_t *dev_handle = (dev_handle_t *)handle;

    if (dev_handle == NULL || param == NULL || payload_len < 0 ||
        dev_handle->mqtt_handle == NULL || dev_iot_handle(dev_handle) == NULL) {
        return -QM_EINVAL;
    }

    if(!msg_id){
        msg_id = qm_iot_dev_msg_id_get(dev_handle);
    }

    qm_snprintf(topic, QM_IOT_DEV_TOPIC_MAX_LEN, QM_IOT_DEV_UP_TOPIC_FMT,
                qm_iot_pid_get(dev_iot_handle(dev_handle)),
                qm_iot_did_get(dev_iot_handle(dev_handle)));

    up_payload = qm_iot_dev_method_payload_create(param,
                                                  submethod,
                                                  msg_id,
                                                  QM_FALSE,
                                                  0,
                                                  payload,
                                                  payload_len);
    if (up_payload == NULL) {
        return -QM_ENOMEM;
    }

    ret = qm_iot_mqtt_pub(dev_handle->mqtt_handle, topic, (uint8_t *)up_payload, strlen(up_payload), QM_MQTT_QoS0);
    QM_CLOUD_LOGI(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "dev up report topic_len=%lu payload_len=%lu ret=%d",
            (unsigned long)strlen(topic), (unsigned long)strlen(up_payload), ret);
    if (ret != QM_EOK) {
        QM_CLOUD_LOGE(LOG_TAG, "dev up report failed ret=%d topic_len=%lu", ret,
                      (unsigned long)strlen(topic));
    }

    cJSON_free(up_payload);
    up_payload = NULL;

    return ret;
}

int32_t qm_iot_dev_up_report(void *handle,
                             char *param,
                             uint32_t msg_id,
                             char *payload,
                             int payload_len)
{
    return qm_iot_dev_up_report_ex(handle, param, NULL, msg_id, payload, payload_len);
}

/** @copydoc qm_iot_dev_reply_publish */
int32_t qm_iot_dev_reply_publish(void *handle,
                                 char *param,
                                 char *submethod,
                                 uint32_t msg_id,
                                 int code,
                                 char *payload,
                                 int payload_len)
{
    int ret;
    char topic[QM_IOT_DEV_TOPIC_MAX_LEN] = {0};
    /** 回复 JSON 的 method 缓冲；param 未带 Reply 后缀时在此拼接。 */
    char reply_method[QM_IOT_DEV_TOPIC_METHOD_LEN] = {0};
    const char *method_for_payload = param;
    char *up_payload = NULL;
    dev_handle_t *dev_handle = (dev_handle_t *)handle;
    size_t param_len = 0;
    size_t reply_suffix_len = 0;

    if (dev_handle == NULL || param == NULL || payload_len < 0 ||
        dev_handle->mqtt_handle == NULL || dev_iot_handle(dev_handle) == NULL) {
        return -QM_EINVAL;
    }

    param_len = strlen(param);
    reply_suffix_len = strlen(QM_IOT_DEV_TOPIC_REPLY_SUFFIX);
    if (param_len >= reply_suffix_len &&
        strcmp(param + param_len - reply_suffix_len, QM_IOT_DEV_TOPIC_REPLY_SUFFIX) == 0) {
        method_for_payload = param;
    } else {
        if (param_len + reply_suffix_len + 1U > sizeof(reply_method)) {
            return -QM_EINVAL;
        }
        qm_snprintf(reply_method,
                    sizeof(reply_method),
                    "%s%s",
                    param,
                    QM_IOT_DEV_TOPIC_REPLY_SUFFIX);
        method_for_payload = reply_method;
    }

    if(!msg_id){
        msg_id = qm_iot_dev_msg_id_get(dev_handle);
    }

    qm_snprintf(topic, QM_IOT_DEV_TOPIC_MAX_LEN, QM_IOT_DEV_UP_TOPIC_FMT,
                qm_iot_pid_get(dev_iot_handle(dev_handle)),
                qm_iot_did_get(dev_iot_handle(dev_handle)));

    up_payload = qm_iot_dev_method_payload_create(method_for_payload,
                                                  submethod,
                                                  msg_id,
                                                  QM_TRUE,
                                                  code,
                                                  payload,
                                                  payload_len);
    if (up_payload == NULL) {
        return -QM_ENOMEM;
    }

    ret = qm_iot_mqtt_pub(dev_handle->mqtt_handle,
                          topic,
                          (uint8_t *)up_payload,
                          strlen(up_payload),
                          QM_MQTT_QoS0);

    QM_CLOUD_LOGI(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "dev up reply topic_len=%lu payload_len=%lu ret=%d",
            (unsigned long)strlen(topic), (unsigned long)strlen(up_payload), ret);
    if (ret != QM_EOK) {
        QM_CLOUD_LOGE(LOG_TAG, "dev up reply failed ret=%d topic_len=%lu", ret,
                      (unsigned long)strlen(topic));
    }

    cJSON_free(up_payload);
    up_payload = NULL;

    return ret;
}

/** @copydoc qm_iot_dev_up_report_reply */
int32_t qm_iot_dev_up_report_reply(void *handle,
                                   char *param,
                                   uint32_t msg_id,
                                   int code,
                                   char *payload,
                                   int payload_len)
{
    return qm_iot_dev_reply_publish(handle,
                                    param,
                                    NULL,
                                    msg_id,
                                    code,
                                    payload,
                                    payload_len);
}

int32_t qm_iot_dev_down_register(void *handle, char *param, qm_iot_mqtt_recv_handler_t recv_handler, void *user_data)
{
    qm_iot_dev_handler_t *dev_topic = NULL;
    qm_list_node_t *topic_node = NULL;
    dev_handle_t *dev_handle = (dev_handle_t*)handle;
    size_t plen;

    if(handle == NULL || param == NULL){
        return -QM_EINVAL;
    }

    plen = strlen(param);
    if (plen > QM_IOT_DEV_HANDLER_PARAMS_MAX_STORE) {
        plen = QM_IOT_DEV_HANDLER_PARAMS_MAX_STORE;
    }

    dev_topic = qm_iot_dev_topic_find(dev_handle, param, (int)plen);
    if(dev_topic == NULL){
        topic_node = qm_list_node_extra_new(sizeof(qm_iot_dev_handler_t));
        if(topic_node == NULL){
            return -QM_ENOMEM;
        }
        dev_topic = (qm_iot_dev_handler_t *)qm_list_node_val_get(topic_node);
        qm_list_rpush(dev_handle->dev_topic_list, topic_node);
    }

    memset(dev_topic->params, 0, sizeof(dev_topic->params));
    memcpy(dev_topic->params, param, plen);
    dev_topic->params[plen] = '\0';
    dev_topic->user_data = user_data;
    dev_topic->recv_handler = recv_handler;

    return QM_EOK;
}

int32_t qm_iot_dev_down_unregister(void *handle, char *param)
{
    qm_iot_dev_handler_t *dev_topic = NULL;
    qm_list_node_t *topic_node = NULL;
    dev_handle_t *dev_handle = (dev_handle_t*)handle;
    size_t plen;

    if(handle == NULL || param == NULL || dev_handle->dev_topic_list == NULL){
        return -QM_EINVAL;
    }

    plen = strlen(param);
    if (plen > QM_IOT_DEV_HANDLER_PARAMS_MAX_STORE) {
        plen = QM_IOT_DEV_HANDLER_PARAMS_MAX_STORE;
    }

    dev_topic = qm_iot_dev_topic_find(dev_handle, param, (int)plen);
    if(dev_topic == NULL){
        return QM_EOK;
    }

    topic_node = (qm_list_node_t *)((uint8_t *)dev_topic - sizeof(qm_list_node_t));
    qm_list_remove(dev_handle->dev_topic_list, topic_node);
    qm_list_node_destroy(topic_node);

    return QM_EOK;
}

#if CONFIG_QM_IOT_LOG_PUSH_SUPPORT

/**
 * @brief 发布 logInfo 到 /{pid}/{did}/device/log；成功或失败后释放 @p payload_array 。
 */
static int32_t qm_iot_dev_log_push_publish(dev_handle_t *dev_handle, cJSON *payload_array,
                                           const char *log_level, uint32_t msg_id)
{
    cJSON *root = NULL;
    cJSON *params = NULL;
    char *json_str = NULL;
    char topic[QM_IOT_DEV_TOPIC_MAX_LEN] = {0};
    int32_t ret = QM_EOK;
    int json_len = 0;

    if (dev_handle == NULL || dev_handle->mqtt_handle == NULL || log_level == NULL || payload_array == NULL) {
        cJSON_Delete(payload_array);
        return -QM_EINVAL;
    }

    if (!msg_id) {
        msg_id = qm_iot_dev_msg_id_get(dev_handle);
    }

    root = cJSON_CreateObject();
    params = cJSON_CreateObject();
    if (root == NULL || params == NULL) {
        ret = -QM_ENOMEM;
        goto exit;
    }

    cJSON_AddStringToObject(root, METHOD_KEY, QM_IOT_COMMON_LOG_INFO_METHOD);
    cJSON_AddNumberToObject(root, MSG_ID_KEY, msg_id);
    cJSON_AddStringToObject(params, QM_IOT_COMMON_LOG_LEVEL_KEY, log_level);
    cJSON_AddItemToObject(params, QM_IOT_COMMON_LOG_PAYLOAD_KEY, payload_array);
    payload_array = NULL;
    cJSON_AddItemToObject(root, PARAMS_KEY, params);
    params = NULL;

    json_str = cJSON_PrintUnformatted(root);
    if (json_str == NULL) {
        ret = -QM_ENOMEM;
        goto exit;
    }

    json_len = (int)strlen(json_str);
    if (json_len <= 0 || json_len > CONFIG_QM_IOT_LOG_PUSH_MAX_LEN) {
        ret = -QM_EINVAL;
        goto exit;
    }

    qm_iot_dev_topic_snprintf(dev_iot_handle(dev_handle), topic, QM_IOT_DEV_TOPIC_MAX_LEN, QM_IOT_DEV_M_LOG);
    qm_iot_mqtt_pub(dev_handle->mqtt_handle, topic, (uint8_t *)json_str, (uint32_t)json_len, QM_MQTT_QoS0);
    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_DOWNLINK, "publish log topic_len=%lu payload_len=%lu",
                  (unsigned long)strlen(topic), (unsigned long)strlen(json_str));

exit:
    if (json_str != NULL) {
        cJSON_free(json_str);
    }
    if (payload_array != NULL) {
        cJSON_Delete(payload_array);
    }
    if (params != NULL) {
        cJSON_Delete(params);
    }
    if (root != NULL) {
        cJSON_Delete(root);
    }
    return ret;
}

int32_t qm_iot_dev_log_push_raw(void *handle, const char *log_level,
                                const char *payload_array_json, uint32_t msg_id)
{
    cJSON *payload_array = NULL;
    dev_handle_t *dev_handle = (dev_handle_t *)handle;

    if (handle == NULL || log_level == NULL || payload_array_json == NULL || payload_array_json[0] == '\0') {
        return -QM_EINVAL;
    }

    payload_array = cJSON_Parse(payload_array_json);
    if (payload_array == NULL || !cJSON_IsArray(payload_array)) {
        cJSON_Delete(payload_array);
        return -QM_EINVAL;
    }

    return qm_iot_dev_log_push_publish(dev_handle, payload_array, log_level, msg_id);
}

int32_t qm_iot_dev_log_push(void *handle, const char *log_level,
                            const qm_iot_log_item_t *items, uint8_t item_num,
                            uint32_t msg_id)
{
    cJSON *payload_array = NULL;
    cJSON *item = NULL;
    uint8_t i = 0;
    dev_handle_t *dev_handle = (dev_handle_t *)handle;

    if (handle == NULL || log_level == NULL || items == NULL || item_num == 0) {
        return -QM_EINVAL;
    }

    payload_array = cJSON_CreateArray();
    if (payload_array == NULL) {
        return -QM_ENOMEM;
    }

    for (i = 0; i < item_num; i++) {
        if (items[i].type == NULL || items[i].message == NULL) {
            cJSON_Delete(payload_array);
            return -QM_EINVAL;
        }

        item = cJSON_CreateObject();
        if (item == NULL) {
            cJSON_Delete(payload_array);
            return -QM_ENOMEM;
        }

        cJSON_AddStringToObject(item, QM_IOT_COMMON_TYPE_KEY, items[i].type);
        cJSON_AddNumberToObject(item, QM_IOT_COMMON_TIMESTAMP_KEY, (double)items[i].timestamp);
        cJSON_AddStringToObject(item, QM_IOT_COMMON_MESSAGE_KEY, items[i].message);
        if (items[i].source != NULL && items[i].source[0] != '\0') {
            cJSON_AddStringToObject(item, QM_IOT_COMMON_SOURCE_KEY, items[i].source);
        }
        cJSON_AddItemToArray(payload_array, item);
    }

    return qm_iot_dev_log_push_publish(dev_handle, payload_array, log_level, msg_id);
}

#endif /* CONFIG_QM_IOT_LOG_PUSH_SUPPORT */

#endif
