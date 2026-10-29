#include "qm_cloud_log.h"
#include "qm_iot_subdev.h"
#include "qm_iot_api.h"
#include "qm_errno.h"
#include "qm_log.h"
#include "qm_iot_mqtt.h"
#include "qm_time.h"
#include "cJSON.h"
#include "qm_iot_common.h"
#include "qm_string.h"
#include "qm_iot_config.h"

#define LOG_TAG  "subdev"

#define QM_IOT_SUBDEV_TOPIC_MAX_LEN                 64
#if 0
#define QM_IOT_SUBDEV_STATE_TOPIC_FMT               "/%u/%u/device/subDevState"
#define QM_IOT_SUBDEV_STATE_REPLY_TOPIC_FMT         "/%u/%u/device/subDevStateReply"
#endif
#define QM_IOT_SUBDEV_GENERIC_PAYLOAD_MAX_LEN        128
#define QM_IOT_SUBDEV_LIST_GET_PAYLOAD_FMT          "{\"method\":\"%s\",\"id\":%d,\"timestamp\":%u}"
#define QM_IOT_SUBDEV_GENERIC_REPLY_PAYLOAD_FMT     "{\"method\":\"%s\",\"id\":%d,\"timestamp\":%u,\"code\":%d}"

#define QM_IOT_SUBDEV_MSG_ID_MAX          (65535)

#define METHOD_KEY                  "method"
#define CODE_KEY                    "code"
#define MSG_ID_KEY                  "id"
#define PARAMS_KEY                  "params"
#define DEV_TYPE_KEY                "devType"
#define COMMON_KEY                  "commonKey"
#define NET_KEY                     "netKey"
#define APP_KEY                     "appKey"
#define GATEWAY_INFO_KEY            "gatewayInfo"
#define UNICAST_ADDR_KEY            "unicastAddr"
#define DEVICES_KEY                 "devices"
#define PID_KEY                     "pid"
#define DID_KEY                     "did"
#define MAC_KEY                     "mac"
#define FLAG_KEY                    "flag"
#define IV_INDEX_KEY                "ivIndex"
#define NET_INDEX_KEY               "netIndex"
#define ELEMENT_KEY                 "elementNum"
#define DEV_KEY                     "devKey"
#define TIMESTAMP_KEY               "timestamp"
#define STATE_KEY                   "state"
#define RSSI_KEY                    "rssi"
#define TTL_KEY                     "ttl"


#define SUBDEV_LIST_METHOD                  "subdevList"
#define SUBDEV_LIST_REPLY_METHOD            "subdevListReply"
#define ADD_SUBDEV_NOTIFY_METHOD            "addSubdevNotify"
#define ADD_SUBDEV_NOTIFY_REPLY_METHOD      "addSubdevNotifyReply"
#define DEL_SUBDEV_NOTIFY_METHOD            "delSubdevNotify"
#define DEL_SUBDEV_NOTIFY_REPLY_METHOD      "delSubdevNotifyReply"
#define SUBDEV_STATE_REQ_METHOD             "subdevStateReq"
#define SUBDEV_STATE_REQ_REPLY_METHOD       "subdevStateReqReply"
#define SUBDEV_STATE_NOTIFY_METHOD          "subdevStateNotify"
#define SUBDEV_STATE_NOTIFY_REPLY_METHOD    "subdevStateNotifyReply"



typedef struct {
    qm_iot_subdev_recv_type_t  recv_type;
    qm_iot_subdev_type_t       subdev_type;
    int (*unpack)(void *json, void *data);
    int (*unpack_free)(void *data);
}subdev_handler_t;

typedef struct {
    uint16_t msg_id;
    void *mqtt_handle;
    /** IOT 会话 句柄，由 @ref QM_IOT_SUBDEVOPT_USERDATA 设置为 @ref qm_iot_init 返回值。 */
    void *userdata;
    /** 当前是否已在 device/down 注册 subdev 相关 method。 */
    uint8_t down_registered;
    /** 记录当前注册绑定的 IOT 句柄，便于切换/销毁时清理。 */
    void *registered_iot_handle;
    qm_iot_subdev_recv_handler_t recv_handler;
} subdev_handle_t;

/**
 * @brief 从 subdev 会话解析 IOT 会话 句柄。
 */
static void *subdev_iot_handle(subdev_handle_t *subdev)
{
    if (subdev == NULL) {
        return NULL;
    }

    return subdev->userdata;
}

static int topo_get_ble_mesh_unpack(void *json, void *data);
static int topo_get_ble_mesh_unpack_free(void *data);

static int topo_add_ble_mesh_unpack(void *json, void *data);
static int topo_add_ble_mesh_free(void *data);

static int topo_del_ble_mesh_unpack(void *json, void *data);
static int topo_del_ble_mesh_free(void *data);

#if 0
static int state_req_reply_unpack(void *json, void *data);
static int state_req_reply_free(void *data);
#endif

static int state_notify_unpack(void *json, void *data);
static int state_notify_free(void *data);

static void subdev_list_get_reply_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata);
static void subdev_add_notify_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata);
static void subdev_del_notify_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata);
static void subdev_state_notify_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata);
static void subdev_state_req_reply_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata);

static void subdev_down_unregister(subdev_handle_t *subdev_handle);
static int subdev_up_publish_raw(subdev_handle_t *subdev_handle, const char *payload);
static int subdev_generic_reply(void *handle, char *method, int msg_id, qm_iot_code_t code);

static subdev_handler_t g_subdev_handler[] = {
    { QM_IOT_SUBDEVRECV_TOPO_GET_REPLY,     QM_IOT_SUBDEV_TYPE_BLE_MESH, topo_get_ble_mesh_unpack, topo_get_ble_mesh_unpack_free    },
    { QM_IOT_SUBDEVRECV_TOPO_ADD_NOTIFY,    QM_IOT_SUBDEV_TYPE_BLE_MESH, topo_add_ble_mesh_unpack, topo_add_ble_mesh_free           },
    { QM_IOT_SUBDEVRECV_TOPO_DEL_NOTIFY,    QM_IOT_SUBDEV_TYPE_BLE_MESH, topo_del_ble_mesh_unpack, topo_del_ble_mesh_free           },
#if 0
    { QM_IOT_SUBDEVRECV_STATE_REQ_REPLY,    QM_IOT_SUBDEV_TYPE_NONE,     state_reply_unpack,    state_reply_free                    },
#endif
    { QM_IOT_SUBDEVRECV_STATE_NOTIFY,       QM_IOT_SUBDEV_TYPE_NONE,     state_notify_unpack,    state_notify_free                  },

};

static subdev_handler_t *subdev_handler_get(qm_iot_subdev_recv_type_t recv_type, qm_iot_subdev_type_t subdev_type)
{
    int i = 0;
    for(i = 0; i < QM_ARRAY_SIZE(g_subdev_handler); i++){
        if(g_subdev_handler[i].recv_type == recv_type &&
           g_subdev_handler[i].subdev_type == subdev_type){
            return &g_subdev_handler[i];
        }
    }
    return NULL;
}

/**
 * @brief IOT 句柄就绪后在 device/down 注册 subdev 相关 method（仅执行一次）。
 */
static void subdev_down_register_if_ready(subdev_handle_t *subdev_handle)
{
    void *iot_handle = NULL;
    int ret = QM_EOK;

    if (subdev_handle == NULL) {
        return;
    }

    iot_handle = subdev_iot_handle(subdev_handle);
    if (iot_handle == NULL) {
        return;
    }

    if (subdev_handle->down_registered && subdev_handle->registered_iot_handle == iot_handle) {
        return;
    }

    if (subdev_handle->down_registered) {
        subdev_down_unregister(subdev_handle);
    }

    ret = qm_iot_down_register(iot_handle,
                               (char *)SUBDEV_LIST_REPLY_METHOD,
                               subdev_list_get_reply_handler,
                               subdev_handle);
    if (ret != QM_EOK) {
        goto __error;
    }

    ret = qm_iot_down_register(iot_handle,
                               (char *)ADD_SUBDEV_NOTIFY_METHOD,
                               subdev_add_notify_handler,
                               subdev_handle);
    if (ret != QM_EOK) {
        goto __error;
    }

    ret = qm_iot_down_register(iot_handle,
                               (char *)DEL_SUBDEV_NOTIFY_METHOD,
                               subdev_del_notify_handler,
                               subdev_handle);
    if (ret != QM_EOK) {
        goto __error;
    }

    ret = qm_iot_down_register(iot_handle,
                               (char *)SUBDEV_STATE_REQ_REPLY_METHOD,
                               subdev_state_req_reply_handler,
                               subdev_handle);
    if (ret != QM_EOK) {
        goto __error;
    }

    ret = qm_iot_down_register(iot_handle,
                               (char *)SUBDEV_STATE_NOTIFY_METHOD,
                               subdev_state_notify_handler,
                               subdev_handle);
    if (ret != QM_EOK) {
        goto __error;
    }

    subdev_handle->down_registered = QM_TRUE;
    subdev_handle->registered_iot_handle = iot_handle;
    return;

__error:
    qm_iot_down_unregister(iot_handle, (char *)SUBDEV_LIST_REPLY_METHOD);
    qm_iot_down_unregister(iot_handle, (char *)ADD_SUBDEV_NOTIFY_METHOD);
    qm_iot_down_unregister(iot_handle, (char *)DEL_SUBDEV_NOTIFY_METHOD);
    qm_iot_down_unregister(iot_handle, (char *)SUBDEV_STATE_REQ_REPLY_METHOD);
    qm_iot_down_unregister(iot_handle, (char *)SUBDEV_STATE_NOTIFY_METHOD);
    QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_SUBDEV, "register subdev down handlers failed ret=%d", ret);
}

static void subdev_down_unregister(subdev_handle_t *subdev_handle)
{
    if (subdev_handle == NULL || !subdev_handle->down_registered || subdev_handle->registered_iot_handle == NULL) {
        return;
    }

    qm_iot_down_unregister(subdev_handle->registered_iot_handle, (char *)SUBDEV_LIST_REPLY_METHOD);
    qm_iot_down_unregister(subdev_handle->registered_iot_handle, (char *)ADD_SUBDEV_NOTIFY_METHOD);
    qm_iot_down_unregister(subdev_handle->registered_iot_handle, (char *)DEL_SUBDEV_NOTIFY_METHOD);
    qm_iot_down_unregister(subdev_handle->registered_iot_handle, (char *)SUBDEV_STATE_REQ_REPLY_METHOD);
    qm_iot_down_unregister(subdev_handle->registered_iot_handle, (char *)SUBDEV_STATE_NOTIFY_METHOD);

    subdev_handle->down_registered = QM_FALSE;
    subdev_handle->registered_iot_handle = NULL;
}

static int subdev_up_publish_raw(subdev_handle_t *subdev_handle, const char *payload)
{
    char topic[QM_IOT_SUBDEV_TOPIC_MAX_LEN] = {0};
    void *iot_handle = NULL;

    if (subdev_handle == NULL || payload == NULL || subdev_handle->mqtt_handle == NULL) {
        return -QM_EINVAL;
    }

    iot_handle = subdev_iot_handle(subdev_handle);
    if (iot_handle == NULL) {
        return -QM_EINVAL;
    }

    qm_snprintf(topic, QM_IOT_SUBDEV_TOPIC_MAX_LEN, QM_IOT_DEV_UP_TOPIC_FMT,
                qm_iot_pid_get(iot_handle), qm_iot_did_get(iot_handle));
    qm_iot_mqtt_pub(subdev_handle->mqtt_handle, topic, (uint8_t *)payload, strlen(payload), QM_MQTT_QoS0);
    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "publish topic_len=%lu payload_len=%lu",
                  (unsigned long)strlen(topic), (unsigned long)strlen(payload));

    return QM_EOK;
}

void *qm_iot_subdev_init(void)
{
    subdev_handle_t *subdev_handle = NULL;

    subdev_handle = (subdev_handle_t*)qm_malloc(sizeof(subdev_handle_t));
    if (subdev_handle == NULL) {
        return NULL;
    }
    memset(subdev_handle, 0, sizeof(subdev_handle_t));
    return subdev_handle;
}

static int qm_iot_subdev_msg_id_get(void *handle)
{
    subdev_handle_t *subdev_handle = (subdev_handle_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }
    subdev_handle->msg_id = (subdev_handle->msg_id == QM_IOT_SUBDEV_MSG_ID_MAX) ? 1 : subdev_handle->msg_id + 1;
    return subdev_handle->msg_id;
}

static char *cjson_str_get(cJSON *cjson, char *key)
{
    cJSON *tmp = NULL;
    tmp = cJSON_GetObjectItem(cjson, key);
    if(tmp == NULL){
        return NULL;
    }
    return tmp->valuestring;
}

static int cjson_int_get(cJSON *cjson, char *key, int *data)
{
    cJSON *tmp = NULL;
    tmp = cJSON_GetObjectItem(cjson, key);
    if(tmp == NULL){
        return -QM_EINVAL;
    }
    *data = tmp->valueint;
    return QM_EOK;
}

static int ble_mesh_dev_unpack(cJSON *device_arrary, qm_iot_subdev_ble_mesh_dev_t *mesh_dev, int dev_num)
{
    int i = 0;
    char *string = NULL;
    cJSON *arrary = NULL;
    qm_err_t ret = QM_EOK;
    qm_iot_subdev_ble_mesh_dev_t *ble_mesh_dev = NULL;

    for(i = 0; i < dev_num; i++){

        ble_mesh_dev = &mesh_dev[i];
        arrary = cJSON_GetArrayItem(device_arrary, i);
        if(arrary == NULL){
            ret = QM_EINVAL;
            goto __exit;
        }

        ret = cjson_int_get(arrary, PID_KEY, (int*)&ble_mesh_dev->pid);
        if(ret != QM_EOK){
            goto __exit;
        }
        ret = cjson_int_get(arrary, DID_KEY, (int*)&ble_mesh_dev->did);
        if(ret != QM_EOK){
            goto __exit;
        }
        ret = cjson_int_get(arrary, FLAG_KEY, (int*)&ble_mesh_dev->flag);
        if(ret != QM_EOK){
            goto __exit;
        }
        ret = cjson_int_get(arrary, IV_INDEX_KEY, (int*)&ble_mesh_dev->iv_index);
        if(ret != QM_EOK){
            goto __exit;
        }
        ret = cjson_int_get(arrary, NET_INDEX_KEY, (int*)&ble_mesh_dev->net_index);
        if(ret != QM_EOK){
            goto __exit;
        }
        ret = cjson_int_get(arrary, ELEMENT_KEY, (int*)&ble_mesh_dev->element_num);
        if(ret != QM_EOK){
            goto __exit;
        }
        ret = cjson_int_get(arrary, UNICAST_ADDR_KEY, (int*)&ble_mesh_dev->unicast_addr);
        if(ret != QM_EOK){
            goto __exit;
        }
        string = cjson_str_get(arrary, MAC_KEY);
        if(string == NULL){
            ret = -QM_EINVAL;
            goto __exit;
        }
        if(strlen(string)/2 != sizeof(ble_mesh_dev->mac)){
            ret = -QM_EINVAL;
            goto __exit;
        }
        qm_str2hex(string, strlen(string), ble_mesh_dev->mac);

        string = cjson_str_get(arrary, DEV_KEY);
        if(string == NULL){
            ret = -QM_EINVAL;
            goto __exit;
        }
        if(strlen(string)/2 != sizeof(ble_mesh_dev->dev_key)){
            ret = -QM_EINVAL;
            goto __exit;
        }
        qm_str2hex(string, strlen(string), ble_mesh_dev->dev_key);
    }
__exit:
    return ret;
}

static int topo_get_ble_mesh_unpack(void *json, void *data)
{
    int size = 0;
    char *string = NULL;
    qm_err_t ret = QM_EOK;
    cJSON *root = (cJSON*)json;
    cJSON *devices_object = NULL;
    cJSON *common_key_object = NULL;
    cJSON *gateway_info_object = NULL;
    qm_iot_subdev_topo_get_reply_t *topo_reply = (qm_iot_subdev_topo_get_reply_t*)data;
    qm_iot_subdev_ble_mesh_dev_t *ble_mesh_dev = NULL;

    if(topo_reply->type <= QM_IOT_SUBDEV_TYPE_NONE || topo_reply->type >= QM_IOT_SUBDEV_TYPE_MAX){
        ret = -QM_EINVAL;
        goto __exit;
    }

    common_key_object = cJSON_GetObjectItem(root, COMMON_KEY);
    if(common_key_object == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }

    string = cjson_str_get(common_key_object, APP_KEY);
    if(string == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }
    if(strlen(string)/2 != sizeof(topo_reply->data.ble_mesh.app_key)){
        ret = -QM_EINVAL;
        goto __exit;
    }
    qm_str2hex(string, strlen(string), topo_reply->data.ble_mesh.app_key);

    string = cjson_str_get(common_key_object, NET_KEY);
    if(string == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }
    if(strlen(string)/2 != sizeof(topo_reply->data.ble_mesh.net_key)){
        ret = -QM_EINVAL;
        goto __exit;
    }
    qm_str2hex(string, strlen(string), topo_reply->data.ble_mesh.net_key);

    gateway_info_object = cJSON_GetObjectItem(root, GATEWAY_INFO_KEY);
    if(gateway_info_object == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }

    ret = cjson_int_get(gateway_info_object, UNICAST_ADDR_KEY, (int*)&topo_reply->data.ble_mesh.unicast_addr);
    if(ret != QM_EOK){
        goto __exit;
    }

    devices_object = cJSON_GetObjectItem(root, DEVICES_KEY);
    if(devices_object == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }
    size = cJSON_GetArraySize(devices_object);
    if(size == 0){
        goto __exit;
    }

    ble_mesh_dev = (qm_iot_subdev_ble_mesh_dev_t*)qm_malloc(sizeof(qm_iot_subdev_ble_mesh_dev_t) * size);
    if(ble_mesh_dev == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }
    topo_reply->data.ble_mesh.ble_mesh_dev = ble_mesh_dev;
    topo_reply->data.ble_mesh.dev_num = (uint16_t)size;
    memset(ble_mesh_dev, 0, sizeof(qm_iot_subdev_ble_mesh_dev_t) * size);

    ret = ble_mesh_dev_unpack(devices_object, ble_mesh_dev, size);

__exit:
    return ret;
}

static int topo_get_ble_mesh_unpack_free(void *data)
{
    qm_iot_subdev_topo_get_reply_t *topo_reply = (qm_iot_subdev_topo_get_reply_t*)data;
    if(topo_reply->data.ble_mesh.ble_mesh_dev){
        qm_free(topo_reply->data.ble_mesh.ble_mesh_dev);
        topo_reply->data.ble_mesh.ble_mesh_dev = NULL;
    }
    return QM_EOK;
}

static int subdev_generic_reply(void *handle, char *method, int msg_id, qm_iot_code_t code)
{
    qm_time_t time = 0;
    char *payload = NULL;
    subdev_handle_t *subdev_handle = (subdev_handle_t*)handle;

    payload = (char*)qm_malloc(QM_IOT_SUBDEV_GENERIC_PAYLOAD_MAX_LEN);
    if(payload == NULL){
        return -QM_ENOMEM;
    }
    memset(payload, 0, QM_IOT_SUBDEV_GENERIC_PAYLOAD_MAX_LEN);

    qm_time(&time);
    qm_snprintf(payload, QM_IOT_SUBDEV_GENERIC_PAYLOAD_MAX_LEN, QM_IOT_SUBDEV_GENERIC_REPLY_PAYLOAD_FMT, method, msg_id, (uint32_t)time, (int)code);

    subdev_up_publish_raw(subdev_handle, payload);
    qm_free(payload);
    payload = NULL;

    return QM_EOK;
}

static void subdev_list_get_reply_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    qm_err_t ret = QM_EOK;
    cJSON *root = NULL;
    cJSON *params_object = NULL;
    qm_iot_subdev_recv_t subdev_recv;
    subdev_handler_t *subdev_handler = NULL;
    qm_iot_subdev_topo_get_reply_t *topo_reply = &subdev_recv.data.topo_reply;
    subdev_handle_t *subdev_handle = (subdev_handle_t*)userdata;
    if(packet->type != QM_IOT_MQTTRECV_PUB){
        return;
    }

    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "subdev payload_len=%lu",
                  (unsigned long)packet->data.pub.payload_len);
    root = cJSON_ParseWithLength((char*)packet->data.pub.payload, packet->data.pub.payload_len);
    if(root == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "subdev list parse error");
        return;
    }
    memset(&subdev_recv, 0, sizeof(qm_iot_subdev_recv_t));
    subdev_recv.type = QM_IOT_SUBDEVRECV_TOPO_GET_REPLY;

    ret = cjson_int_get(root, MSG_ID_KEY, (int*)&topo_reply->msg_id);
    if(ret != QM_EOK){
        goto __exit;
    }
    ret = cjson_int_get(root, CODE_KEY, (int*)&topo_reply->code);
    if(ret != QM_EOK){
        goto __exit;
    }

    params_object = cJSON_GetObjectItem(root, PARAMS_KEY);
    if(params_object == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }

    ret = cjson_int_get(params_object, DEV_TYPE_KEY, (int*)&topo_reply->type);
    if(ret != QM_EOK){
        goto __exit;
    }

    subdev_handler = subdev_handler_get(subdev_recv.type, topo_reply->type);
    if(subdev_handler == NULL){
        ret = -QM_ERROR;
        goto __exit;
    }

    ret = subdev_handler->unpack(params_object, topo_reply);
    if(ret != QM_EOK){
        goto __exit;
    }

    if(subdev_handle->recv_handler){
        subdev_handle->recv_handler(subdev_handle, &subdev_recv, subdev_handle->userdata);
    }

__exit:
    if(subdev_handler && subdev_handler->unpack_free){
        subdev_handler->unpack_free(topo_reply);
    }
    cJSON_Delete(root);
    root = NULL;
    return;
}

static int topo_add_ble_mesh_unpack(void *json, void *data)
{
    int size = 0;
    qm_err_t ret = QM_EOK;
    cJSON *root = (cJSON*)json;
    cJSON *devices_object = NULL;
    qm_iot_subdev_topo_add_notify_t *add_notify = (qm_iot_subdev_topo_add_notify_t*)data;
    qm_iot_subdev_ble_mesh_dev_t *ble_mesh_dev = NULL;
    devices_object = cJSON_GetObjectItem(root, DEVICES_KEY);
    if(devices_object == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }
    size = cJSON_GetArraySize(devices_object);
    if(size == 0){
        ret = -QM_EINVAL;
        goto __exit;
    }

    ble_mesh_dev = (qm_iot_subdev_ble_mesh_dev_t*)qm_malloc(sizeof(qm_iot_subdev_ble_mesh_dev_t) * size);
    if(ble_mesh_dev == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }
    add_notify->data.ble_mesh_dev = ble_mesh_dev;
    add_notify->dev_num = (uint16_t)size;
    memset(ble_mesh_dev, 0, sizeof(qm_iot_subdev_ble_mesh_dev_t) * size);

    ret = ble_mesh_dev_unpack(devices_object, ble_mesh_dev, size);

__exit:
    return ret;
}

static int topo_add_ble_mesh_free(void *data)
{
    qm_iot_subdev_topo_add_notify_t *add_notify = (qm_iot_subdev_topo_add_notify_t*)data;
    if(add_notify->data.ble_mesh_dev){
        qm_free(add_notify->data.ble_mesh_dev);
        add_notify->data.ble_mesh_dev = NULL;
    }
    return QM_EOK;
}

static void subdev_add_notify_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    qm_err_t ret = QM_EOK;
    cJSON *root = NULL;
    cJSON *params_object = NULL;
    qm_iot_subdev_recv_t subdev_recv;
    subdev_handler_t *subdev_handler = NULL;
    qm_iot_subdev_topo_add_notify_t *add_notify = &subdev_recv.data.add_notify;
    subdev_handle_t *subdev_handle = (subdev_handle_t*)userdata;
    if(packet->type != QM_IOT_MQTTRECV_PUB){
        return;
    }
    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "subdev payload_len=%lu",
                  (unsigned long)packet->data.pub.payload_len);
    root = cJSON_Parse((char*)packet->data.pub.payload);
    if(root == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "subdev add parse error");
        return;
    }

    memset(&subdev_recv, 0, sizeof(qm_iot_subdev_recv_t));

    subdev_recv.type = QM_IOT_SUBDEVRECV_TOPO_ADD_NOTIFY;

    ret = cjson_int_get(root, MSG_ID_KEY, (int*)&add_notify->msg_id);
    if(ret != QM_EOK){
        goto __exit;
    }

    params_object = cJSON_GetObjectItem(root, PARAMS_KEY);
    if(params_object == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }

    ret = cjson_int_get(params_object, DEV_TYPE_KEY, (int*)&add_notify->type);
    if(ret != QM_EOK){
        goto __exit;
    }
    if(add_notify->type <= QM_IOT_SUBDEV_TYPE_NONE || add_notify->type >= QM_IOT_SUBDEV_TYPE_MAX){
        ret = -QM_EINVAL;
        goto __exit;
    }
    subdev_handler = subdev_handler_get(subdev_recv.type, add_notify->type);
    if(subdev_handler == NULL){
        ret = -QM_ERROR;
        goto __exit;
    }
    ret = subdev_handler->unpack(params_object, add_notify);
    if(ret != QM_EOK){
        goto __exit;
    }
    if(subdev_handle->recv_handler){
        subdev_handle->recv_handler(subdev_handle, &subdev_recv, subdev_handle->userdata);
    }

    subdev_generic_reply(subdev_handle, (char *)ADD_SUBDEV_NOTIFY_REPLY_METHOD, (int)add_notify->msg_id, QM_IOT_CODE_SUCCESS);

__exit:
    if(subdev_handler && subdev_handler->unpack_free){
        subdev_handler->unpack_free(add_notify);
    }
    cJSON_Delete(root);
    root = NULL;
    return;
}

static int topo_del_ble_mesh_unpack(void *json, void *data)
{
    int i = 0;
    int size = 0;
    qm_err_t ret = QM_EOK;
    cJSON *arrary = NULL;
    cJSON *root = (cJSON*)json;
    cJSON *devices_object = NULL;
    qm_iot_subdev_topo_del_notify_t *del_notify = (qm_iot_subdev_topo_del_notify_t*)data;
    qm_iot_subdev_ble_mesh_del_t *ble_mesh_del = NULL;
    devices_object = cJSON_GetObjectItem(root, DEVICES_KEY);
    if(devices_object == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }
    size = cJSON_GetArraySize(devices_object);
    if(size == 0){
        ret = -QM_EINVAL;
        goto __exit;
    }

    ble_mesh_del = (qm_iot_subdev_ble_mesh_del_t*)qm_malloc(sizeof(qm_iot_subdev_ble_mesh_del_t) * size);
    if(ble_mesh_del == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    del_notify->data.ble_mesh_del = ble_mesh_del;
    del_notify->dev_num = (uint16_t)size;
    memset(ble_mesh_del, 0, sizeof(qm_iot_subdev_ble_mesh_del_t) * size);
    for(i = 0; i < size; i++){
        arrary = cJSON_GetArrayItem(devices_object, i);
        if(arrary == NULL){
            ret = QM_EINVAL;
            goto __exit;
        }

        ret = cjson_int_get(arrary, DID_KEY, (int*)&ble_mesh_del[i].did);
        if(ret != QM_EOK){
            goto __exit;
        }
    }

__exit:
    return ret;
}

static int topo_del_ble_mesh_free(void *data)
{
    qm_iot_subdev_topo_del_notify_t *del_notify = (qm_iot_subdev_topo_del_notify_t*)data;
    if(del_notify->data.ble_mesh_del){
        qm_free(del_notify->data.ble_mesh_del);
        del_notify->data.ble_mesh_del = NULL;
    }
    return QM_EOK;
}

static void subdev_del_notify_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    qm_err_t ret = QM_EOK;
    cJSON *root = NULL;
    cJSON *params_object = NULL;
    qm_iot_subdev_recv_t subdev_recv;
    subdev_handler_t *subdev_handler = NULL;
    qm_iot_subdev_topo_del_notify_t *del_notify = &subdev_recv.data.del_notify;
    subdev_handle_t *subdev_handle = (subdev_handle_t*)userdata;
    if(packet->type != QM_IOT_MQTTRECV_PUB){
        return;
    }
    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "subdev payload_len=%lu",
                  (unsigned long)packet->data.pub.payload_len);
    root = cJSON_ParseWithLength((char*)packet->data.pub.payload, packet->data.pub.payload_len);
    if(root == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "subdev del parse error");
        return;
    }

    memset(&subdev_recv, 0, sizeof(qm_iot_subdev_recv_t));

    subdev_recv.type = QM_IOT_SUBDEVRECV_TOPO_DEL_NOTIFY;

    ret = cjson_int_get(root, MSG_ID_KEY, (int*)&del_notify->msg_id);
    if(ret != QM_EOK){
        goto __exit;
    }

    params_object = cJSON_GetObjectItem(root, PARAMS_KEY);
    if(params_object == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "subdev params parse error");
        ret = -QM_EINVAL;
        goto __exit;
    }

    ret = cjson_int_get(params_object, DEV_TYPE_KEY, (int*)&del_notify->type);
    if(ret != QM_EOK){
        goto __exit;
    }
    if(del_notify->type <= QM_IOT_SUBDEV_TYPE_NONE || del_notify->type >= QM_IOT_SUBDEV_TYPE_MAX){
        ret = -QM_EINVAL;
        goto __exit;
    }
    subdev_handler = subdev_handler_get(subdev_recv.type, del_notify->type);
    if(subdev_handler == NULL){
        ret = -QM_ERROR;
        goto __exit;
    }
    ret = subdev_handler->unpack(params_object, del_notify);
    if(ret != QM_EOK){
        goto __exit;
    }

    subdev_generic_reply(subdev_handle, (char *)DEL_SUBDEV_NOTIFY_REPLY_METHOD, (int)del_notify->msg_id, QM_IOT_CODE_SUCCESS);

    if(subdev_handle->recv_handler){
        subdev_handle->recv_handler(subdev_handle, &subdev_recv, subdev_handle->userdata);
    }

__exit:
    if(subdev_handler && subdev_handler->unpack_free){
        subdev_handler->unpack_free(del_notify);
    }
    cJSON_Delete(root);
    root = NULL;
    return;
}

static int state_notify_unpack(void *json, void *data)
{
    int i = 0;
    int size = 0;
    qm_err_t ret = QM_EOK;
    cJSON *arrary = NULL;
    cJSON *devices_object = (cJSON*)json;
    qm_iot_subdev_state_notify_t *state_notify = (qm_iot_subdev_state_notify_t *)data;
    qm_iot_subdev_state_t *dev_state = NULL;

    size = cJSON_GetArraySize(devices_object);
    if(size == 0){
        ret = -QM_EINVAL;
        goto __exit;
    }

    dev_state = (qm_iot_subdev_state_t *)qm_malloc(sizeof(qm_iot_subdev_state_t) * size);
    if(dev_state == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    state_notify->dev_state = dev_state;
    state_notify->dev_num = (uint16_t)size;
    memset(dev_state, 0, sizeof(qm_iot_subdev_state_t) * size);
    for(i = 0; i < size; i++){
        arrary = cJSON_GetArrayItem(devices_object, i);
        if(arrary == NULL){
            ret = QM_EINVAL;
            goto __exit;
        }

        ret = cjson_int_get(arrary, DEV_TYPE_KEY, (int*)&dev_state[i].dev_type);
        if(ret != QM_EOK){
            goto __exit;
        }

        ret = cjson_int_get(arrary, DID_KEY, (int*)&dev_state[i].did);
        if(ret != QM_EOK){
            goto __exit;
        }

        ret = cjson_int_get(arrary, STATE_KEY, (int*)&dev_state[i].state);
        if(ret != QM_EOK){
            goto __exit;
        }
    }
__exit:
    return ret;
}

static int state_notify_free(void *data)
{
    qm_iot_subdev_state_notify_t *state_notify = (qm_iot_subdev_state_notify_t*)data;
    if(state_notify->dev_state){
        qm_free(state_notify->dev_state);
        state_notify->dev_state = NULL;
    }
    return QM_EOK;
}

static void subdev_state_notify_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    qm_err_t ret = QM_EOK;
    cJSON *root = NULL;
    cJSON *params_object = NULL;
    qm_iot_subdev_recv_t subdev_recv;
    subdev_handler_t *subdev_handler = NULL;
    qm_iot_subdev_state_notify_t *state_notify = &subdev_recv.data.state_notify;
    subdev_handle_t *subdev_handle = (subdev_handle_t*)userdata;
    if(packet->type != QM_IOT_MQTTRECV_PUB){
        return;
    }
    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "[subdev_state_notify] payload_len=%lu",
                  (unsigned long)packet->data.pub.payload_len);
    root = cJSON_ParseWithLength((char*)packet->data.pub.payload, packet->data.pub.payload_len);
    if(root == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "state reply parse error");
        return;
    }
    memset(&subdev_recv, 0, sizeof(qm_iot_subdev_recv_t));
    subdev_recv.type = QM_IOT_SUBDEVRECV_STATE_NOTIFY;

    ret = cjson_int_get(root, MSG_ID_KEY, (int*)&state_notify->msg_id);
    if(ret != QM_EOK){
        goto __exit;
    }

    params_object = cJSON_GetObjectItem(root, PARAMS_KEY);
    if(params_object == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "subdev params parse error");
        ret = -QM_EINVAL;
        goto __exit;
    }

    subdev_handler = subdev_handler_get(subdev_recv.type, QM_IOT_SUBDEV_TYPE_NONE);
    if(subdev_handler == NULL){
        ret = -QM_ERROR;
        goto __exit;
    }

    ret = subdev_handler->unpack(params_object, state_notify);
    if(ret != QM_EOK){
        goto __exit;
    }

    if(subdev_handle->recv_handler){
        subdev_handle->recv_handler(subdev_handle, &subdev_recv, subdev_handle->userdata);
    }

    subdev_generic_reply(subdev_handle, (char *)SUBDEV_STATE_NOTIFY_REPLY_METHOD, (int)state_notify->msg_id, QM_IOT_CODE_SUCCESS);

__exit:
    if(subdev_handler && subdev_handler->unpack_free){
        subdev_handler->unpack_free(state_notify);
    }
    cJSON_Delete(root);
    root = NULL;
    return;
}

static void subdev_state_req_reply_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    (void)handle;
    (void)userdata;

    if(packet->type != QM_IOT_MQTTRECV_PUB){
        return;
    }

    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "[state_req_reply] payload_len=%lu",
                  (unsigned long)packet->data.pub.payload_len);
}


int32_t qm_iot_subdev_setopt(void *handle, qm_iot_subdev_option_t option, void *data)
{
    qm_err_t ret = QM_EOK;
    subdev_handle_t *subdev_handle = (subdev_handle_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }
    switch(option){

        case QM_IOT_SUBDEVOPT_MQTT_HANDLE:
            subdev_handle->mqtt_handle = data;
            subdev_down_register_if_ready(subdev_handle);
        break;
        case QM_IOT_SUBDEVOPT_RECV_HANDLER:
            subdev_handle->recv_handler = (qm_iot_subdev_recv_handler_t)data;

        break;
        case QM_IOT_SUBDEVOPT_USERDATA:
            subdev_handle->userdata = data;
            subdev_down_register_if_ready(subdev_handle);
        break;
        default:
            ret = -QM_EINVAL;
        break;
    }
    return ret;
}


int32_t qm_iot_subdev_deinit(void **handle)
{
    if(handle == NULL){
        return -QM_EINVAL;
    }
    subdev_handle_t *subdev_handle = (subdev_handle_t*)(*handle);

    subdev_down_unregister(subdev_handle);

    qm_free(subdev_handle);
    *handle = NULL;

    return QM_EOK;
}

int32_t qm_iot_subdev_topo_get(void *handle)
{
    int msg_id = 0;
    qm_time_t time = 0;
    char *payload = NULL;
    subdev_handle_t *subdev_handle = (subdev_handle_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }
    payload = (char*)qm_malloc(QM_IOT_SUBDEV_GENERIC_PAYLOAD_MAX_LEN);
    if(payload == NULL){
        return -QM_ENOMEM;
    }
    memset(payload, 0, QM_IOT_SUBDEV_GENERIC_PAYLOAD_MAX_LEN);

    qm_time(&time);
    msg_id = qm_iot_subdev_msg_id_get(handle);
    qm_snprintf(payload, QM_IOT_SUBDEV_GENERIC_PAYLOAD_MAX_LEN, QM_IOT_SUBDEV_LIST_GET_PAYLOAD_FMT,
                SUBDEV_LIST_METHOD, msg_id, (uint32_t)time);
    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_SUBDEV, "subdev topo get payload_len=%lu",
                  (unsigned long)strlen(payload));

    subdev_up_publish_raw(subdev_handle, payload);

    qm_free(payload);
    payload = NULL;

    return msg_id;
}

int32_t qm_iot_subdev_state_report(void *handle, qm_iot_subdev_state_t *subdev_state, int dev_num)
{
    int i = 0;
    int msg_id = 0;
    cJSON *root = NULL;
    cJSON *array = NULL;
    cJSON *arrayobj = NULL;
    qm_err_t ret = QM_EOK;
    qm_time_t time = 0;
    char *payload = NULL;
    subdev_handle_t *subdev_handle = (subdev_handle_t*)handle;
    if(handle == NULL || subdev_state == NULL || dev_num == 0){
        return -QM_EINVAL;
    }
    qm_time(&time);
    msg_id = qm_iot_subdev_msg_id_get(handle);
    root = cJSON_CreateObject();
    if(root == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }
    cJSON_AddStringToObject(root, METHOD_KEY, SUBDEV_STATE_REQ_METHOD);
    cJSON_AddNumberToObject(root, MSG_ID_KEY, msg_id);
    cJSON_AddNumberToObject(root, TIMESTAMP_KEY, time);

    array = cJSON_CreateArray();
    if(array == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }
    cJSON_AddItemToObject(root, PARAMS_KEY, array);

    for(i = 0; i < dev_num; i++){
        qm_iot_subdev_state_t *state_item = &subdev_state[i];

        arrayobj = cJSON_CreateObject();
        if(arrayobj == NULL){
            ret = -QM_ENOMEM;
            goto __exit;
        }
        cJSON_AddItemToArray(array, arrayobj);
        cJSON_AddNumberToObject(arrayobj, DEV_TYPE_KEY, state_item->dev_type);
        cJSON_AddNumberToObject(arrayobj, DID_KEY, state_item->did);
        cJSON_AddNumberToObject(arrayobj, STATE_KEY, state_item->state);
        switch (state_item->dev_type)
        {
            case QM_IOT_SUBDEV_TYPE_BLE_MESH:
                if(state_item->state){
                    cJSON_AddNumberToObject(arrayobj, TTL_KEY, state_item->data.ble_mesh.ttl);
                    cJSON_AddNumberToObject(arrayobj, RSSI_KEY, state_item->data.ble_mesh.rssi);
                }
            break;

            default:
            break;
        }
    }
    payload = cJSON_PrintUnformatted(root);
    if(payload == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }
    subdev_up_publish_raw(subdev_handle, payload);

    cJSON_free(payload);
    payload = NULL;
    ret = msg_id;
__exit:
    if(root){
        cJSON_Delete(root);
        root = NULL;
    }
    return ret;
}
