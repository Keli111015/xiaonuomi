#include "qm_cloud_log.h"
#include "qm.h"
#include "json_parser.h"
#include "qm_utils_string.h"

#include "qm_iot_mqtt.h"
#include "qm_iot_shadow.h"
#include "qm_iot_config.h"

#include "qm_utils_list.h"

#define LOG_TAG "SHADOW"

#define SHADOW_DID_MAX_LEN  (16)

#define QM_IOT_SHADOW_TOPIC_SUB_RETRY  (10)

#define SHADOW_UPDATAE_TOPIC_MAX_LEN    (30 + SHADOW_DID_MAX_LEN)

#define SHADOW_GET_TOPIC_SUPPORT    0

#define TOPIC_MAX_STR_LEN   (64)

#define SHADOW_REPORT_FORMAT  "{\"state\":{\"reported\":{\"%u\":%s}}}"

#define SHADOW_STATE_KEY        "state"
#define SHADOW_DESIRED_KEY      "desired"
#define SHADOW_REPORTED_KEY     "reported"
#define SHADOW_WELCOME_KEY      "welcome"
#define SHADOW_VERSION_KEY      "version"
#define SHADOW_TIMESTAMP_KEY    "timestamp"

// 设备report上报的topic
#define SHADOW_TOPIC_UPDATE_FORMAT      "$aws/things/%u/shadow/update"

#if SHADOW_GET_TOPIC_SUPPORT
// 设备get请求的topic
#define SHADOW_TOPIC_GET_FORMAT         "$aws/things/%u/shadow/get"

// 设备监听get的topic
#define SHADOW_TOPIC_ACCEPTED_FORMAT    "$aws/things/%u/shadow/get/accepted"
#endif



typedef struct
{
    uint32_t did;
    char topic[SHADOW_UPDATAE_TOPIC_MAX_LEN];
}update_topic_map_t;

typedef struct
{
    uint32_t did;
    /** 父级 pub 句柄；IOT 会话 见 @ref shadow_iot_handle。 */
    void *userdata;
    void *mqtt_handle;
    /** 是否已完成网关 shadow update topic 预订阅。 */
    uint8_t mqtt_pre_sub_done;
    qm_iot_shadow_recv_handler_t recv_handler;
    qm_list_t *update_topic_map;
    qm_mutex_t topic_lock;
}qm_iot_shadow_handle_t;

static void qm_iot_shadow_set_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata);

/**
 * @brief 从 shadow 会话解析 IOT 会话 句柄（经 pub 的 @ref QM_IOT_PUBOPT_USERDATA）。
 */
static void *shadow_iot_handle(qm_iot_shadow_handle_t *shadow)
{
    void **pub_fields = NULL;

    if (shadow == NULL || shadow->userdata == NULL) {
        return NULL;
    }

    /* userdata 为 pub 句柄；其第三成员为 QM_IOT_PUBOPT_USERDATA（session 句柄） */
    pub_fields = (void **)shadow->userdata;
    return pub_fields[2];
}

/**
 * @brief MQTT 与 session 句柄就绪后注册 shadow 预订阅（仅执行一次）。
 */
static void shadow_mqtt_sub_if_ready(qm_iot_shadow_handle_t *shadow_handle)
{
    char topic[SHADOW_UPDATAE_TOPIC_MAX_LEN] = {0};

    if (shadow_handle == NULL || shadow_handle->mqtt_handle == NULL || shadow_iot_handle(shadow_handle) == NULL) {
        return;
    }

    if (shadow_handle->mqtt_pre_sub_done) {
        return;
    }

    shadow_handle->mqtt_pre_sub_done = QM_TRUE;
    qm_snprintf(topic, SHADOW_UPDATAE_TOPIC_MAX_LEN, SHADOW_TOPIC_UPDATE_FORMAT,
                qm_iot_did_get(shadow_iot_handle(shadow_handle)));
    qm_iot_mqtt_pre_sub(shadow_handle->mqtt_handle, topic, qm_iot_shadow_set_recv_handler, QM_MQTT_QoS0, shadow_handle);
}

/**
 * @brief 设备上报的json消息组包
 *
 * @param[in] msg    消息结构体, 可指定发送设备did, 消息类型, 消息数据等, 更多信息请参考@ref qm_iot_shadow_msg_t
 *
 * @return 设备上报消息的字符串数据, 后续需调用qm_free释放内存
 *
 */
static char *shadow_report_malloc_and_copy(qm_iot_shadow_msg_t *msg)
{
    int msg_len = 0;
    char *json_msg = NULL;

    if(msg == NULL || !msg->data.update.reported){
        return NULL;
    }

    msg_len = SHADOW_DID_MAX_LEN + strlen(SHADOW_REPORT_FORMAT) + strlen(msg->data.update.reported);
    json_msg = (char *)qm_malloc(msg_len + 1);
    if(json_msg == NULL){
        return NULL;
    }
    memset(json_msg, 0, msg_len + 1);

    qm_snprintf(json_msg, msg_len, SHADOW_REPORT_FORMAT, msg->did, msg->data.update.reported);


    return json_msg;
}

/*
{
    "version": 32,
    "timestamp": 1724298963,
    "state": {
        "100045667": {
            "1": {
                "properties": {
                    "1": 20
                }
            }
        }
    },
    "metadata": {
        "100045667": {
            "1": {
                "properties": {
                    "1": {
                        "timestamp": 1724298963
                    }
                }
            }
        }
    }
}
*/

static void qm_iot_shadow_set_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    int type = 0;
    char *ops = NULL;
    char *value = NULL;
    int value_len = 0;
    char *root = NULL;
    int root_len = 0;
    char *key = NULL;
    int key_len = 0;
    qm_iot_shadow_recv_t shadow_recv = {0};
    qm_iot_shadow_handle_t *shadow_handle = (qm_iot_shadow_handle_t *)userdata;

    if(packet->type != QM_IOT_MQTTRECV_PUB){
        return ;
    }

    shadow_recv.type = QM_IOT_SHADOWRECV_SET;
#if 0
    root = qm_json_get_value_by_name((char *)packet->data.pub.payload, (int)packet->data.pub.payload_len, SHADOW_VERSION_KEY, &root_len, &type);
    if(root == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "version find failed!!");
        return;
    }
    shadow_recv.data.set.version = int_str_to_num(root, root_len);

    root = qm_json_get_value_by_name((char *)packet->data.pub.payload, (int)packet->data.pub.payload_len, SHADOW_TIMESTAMP_KEY, &root_len, &type);
    if(root == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "timestamp find failed!!");
        return;
    }

    shadow_recv.data.set.timestamp = (uint64_t)int_str_to_num(root, root_len);
#endif
    root = qm_json_get_value_by_name((char *)packet->data.pub.payload, (int)packet->data.pub.payload_len, SHADOW_STATE_KEY, &root_len, &type);
    if(root == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "state find failed!!");
        return;
    }

    root = qm_json_get_value_by_name(root, root_len, SHADOW_DESIRED_KEY, &root_len, &type);
    if(root == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "desired find failed!!");
        return;
    }

        QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "set_recv[%d] payload_len=%lu", packet->type,
                      (unsigned long)packet->data.pub.payload_len);

    json_object_for_each_kv(root, ops, key, key_len, value, value_len, type)
    {
        if (!(key && key_len && value && value_len)){
            QM_CLOUD_LOGE(LOG_TAG, "find end!!");
            break;
        }

        shadow_recv.did = int_str_to_num(key, key_len);
        shadow_recv.data.set.payload = value;
        shadow_recv.data.set.payload_len = value_len;
        QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "recv[%d] value_len=%d", shadow_recv.did, value_len);
        if(shadow_handle->recv_handler){
            shadow_handle->recv_handler(shadow_handle, &shadow_recv, shadow_handle->userdata);
        }
    }
}

static int shadow_list_match(void *a, void *b)
{
    update_topic_map_t *a_handle = (update_topic_map_t*)a;
    update_topic_map_t *b_handle = (update_topic_map_t*)b;

    if(a_handle == NULL || b_handle == NULL){
        return QM_FALSE;
    }

    if(a_handle->did == b_handle->did){
        return QM_TRUE;
    }

    return QM_FALSE;
}
/**
 * @brief 创建shadow会话实例, 并以默认值配置会话参数
 *
 * @return void *
 * @retval 非NULL shadow实例的句柄
 * @retval NULL   初始化失败, 一般是内存分配失败导致
 *
 */
void *qm_iot_shadow_init(void *iot_handle)
{
    qm_iot_shadow_handle_t *shadow_handle = (qm_iot_shadow_handle_t *)qm_malloc(sizeof(qm_iot_shadow_handle_t));
    if(shadow_handle == NULL){
        return NULL;
    }
    memset(shadow_handle, 0, sizeof(qm_iot_shadow_handle_t));

    qm_mutex_new(&shadow_handle->topic_lock);

    shadow_handle->did = (iot_handle != NULL) ? qm_iot_did_get(iot_handle) : 0;
    shadow_handle->update_topic_map = qm_list_new();
    if(shadow_handle->update_topic_map == NULL){
        qm_free(shadow_handle);
        return NULL;
    }

    shadow_handle->update_topic_map->match = shadow_list_match;
    return (void *)shadow_handle;
}



/**
 * @brief
 *
 * @param[in] handle shadow会话句柄
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_shadow_sub(void *handle, uint32_t did)
{
    update_topic_map_t match_map = {0};
    update_topic_map_t *url_map = NULL;
    qm_list_node_t *node = NULL;
    qm_iot_shadow_handle_t *shadow_handle = (qm_iot_shadow_handle_t *)handle;
    if(shadow_handle == NULL){
        return -QM_EINVAL;
    }

    match_map.did = did;

    qm_mutex_lock(&shadow_handle->topic_lock, QM_WAIT_FOREVER);
    node = qm_list_find(shadow_handle->update_topic_map, &match_map);
    if(node == NULL){
        node = qm_list_node_extra_new(sizeof(update_topic_map_t));
        if(node == NULL){
            qm_mutex_unlock(&shadow_handle->topic_lock);
            return -QM_ENOMEM;
        }
        qm_list_rpush(shadow_handle->update_topic_map, node);
    }

    url_map = (update_topic_map_t *)node->val;

    url_map->did = did;
    memset(url_map->topic, 0, SHADOW_UPDATAE_TOPIC_MAX_LEN + 1);
    qm_snprintf(url_map->topic, SHADOW_UPDATAE_TOPIC_MAX_LEN, SHADOW_TOPIC_UPDATE_FORMAT, did);
    qm_mutex_unlock(&shadow_handle->topic_lock);
    qm_iot_mqtt_sub(shadow_handle->mqtt_handle,url_map->topic, qm_iot_shadow_set_recv_handler, QM_MQTT_QoS1);
    return QM_EOK;
}

/**
 * @brief 取消所有子设备的订阅
 *
 * @param[in] handle shadow会话句柄
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_shadow_unsub_all(void *handle)
{
    qm_list_node_t *node;
    update_topic_map_t  *topic_map = NULL;
    qm_iot_shadow_handle_t *shadow_handle = (qm_iot_shadow_handle_t *)handle;
    if(shadow_handle == NULL){
        return -QM_EINVAL;
    }

    qm_mutex_lock(&shadow_handle->topic_lock, QM_WAIT_FOREVER);
    while (1)
    {
        node = qm_list_at(shadow_handle->update_topic_map, 0);
        if(node == NULL){
            break;
        }
        topic_map = (update_topic_map_t *)qm_list_node_val_get(node);
        qm_iot_mqtt_unsub(shadow_handle->mqtt_handle, topic_map->topic);

        qm_list_remove(shadow_handle->update_topic_map, node);
        qm_list_node_destroy(node);

        node = NULL;
    }
    qm_mutex_unlock(&shadow_handle->topic_lock);

    return QM_EOK;
}

/**
 * @brief
 *
 * @param[in] handle shadow会话句柄
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_shadow_unsub(void *handle, uint32_t did)
{
    int ret = QM_EOK;
    update_topic_map_t match_map = {0};
    update_topic_map_t *url_map = NULL;
    qm_list_node_t *node = NULL;
    qm_iot_shadow_handle_t *shadow_handle = (qm_iot_shadow_handle_t *)handle;
    if(shadow_handle == NULL){
        return -QM_EINVAL;
    }

    match_map.did = did;

    qm_mutex_lock(&shadow_handle->topic_lock, QM_WAIT_FOREVER);
    node = qm_list_find(shadow_handle->update_topic_map, &match_map);
    if(node == NULL){
        qm_mutex_unlock(&shadow_handle->topic_lock);
        return QM_EOK;
    }

    qm_list_remove(shadow_handle->update_topic_map, node);
    qm_mutex_unlock(&shadow_handle->topic_lock);

    url_map = (update_topic_map_t *)node->val;

    ret = qm_iot_mqtt_unsub(shadow_handle->mqtt_handle,url_map->topic);
    if(ret < 0){
        return ret;
    }
    qm_list_node_destroy(node);
    return QM_EOK;
}

/**
 * @brief 配置shadow会话
 *
 * @param[in] handle shadow会话句柄
 * @param[in] option 配置选项, 更多信息请参考@ref qm_iot_shadow_option_t
 * @param[in] data   配置选项数据, 更多信息请参考@ref qm_iot_shadow_option_t
 *
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_shadow_setopt(void *handle, qm_iot_shadow_option_t option, void *data)
{
    qm_iot_shadow_handle_t *shadow_handle = (qm_iot_shadow_handle_t *)handle;
    if(shadow_handle == NULL){
        return -QM_EINVAL;
    }

    switch (option)
    {

        case QM_IOT_SHADOWOPT_USERDATA:
            shadow_handle->userdata = data;
            shadow_mqtt_sub_if_ready(shadow_handle);
        break;

        case QM_IOT_SHADOWOPT_MQTT_HANDLE:
            shadow_handle->mqtt_handle = data;
            shadow_mqtt_sub_if_ready(shadow_handle);
        break;

        case QM_IOT_SHADOWOPT_RECV_HANDLER:
            if(shadow_handle->mqtt_handle == NULL){
                return -QM_EINVAL;
            }
            shadow_handle->recv_handler = (qm_iot_shadow_recv_handler_t)data;
            shadow_mqtt_sub_if_ready(shadow_handle);
        break;

        case QM_IOT_SHADOWOPT_RESET_SUB:
            shadow_handle->mqtt_pre_sub_done = QM_FALSE;
            shadow_handle->mqtt_handle = NULL;
        break;

        default:

        break;
    }

    return QM_EOK;
}

/**
 * @brief 向服务器发送shadow消息请求
 *
 * @param[in] handle shadow会话句柄
 * @param[in] msg    消息结构体, 可指定发送设备did, 消息类型, 消息数据等, 更多信息请参考@ref qm_iot_shadow_msg_t
 *
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_shadow_send(void *handle, qm_iot_shadow_msg_t *msg)
{
    int ret = QM_EOK;
    char *json_msg = NULL;
    char topic[TOPIC_MAX_STR_LEN + 1] = {0};
    qm_iot_shadow_handle_t *shadow_handle = (qm_iot_shadow_handle_t *)handle;

    if(handle == NULL || msg == NULL){
        return -QM_EINVAL;
    }

    switch (msg->type)
    {

        case QM_IOT_SHADOWMSG_UPDATE:
            json_msg = shadow_report_malloc_and_copy(msg);
            qm_snprintf(topic, TOPIC_MAX_STR_LEN, SHADOW_TOPIC_UPDATE_FORMAT, msg->did);
        break;
        default:
        break;
    }

    //todo: 执行消息发送接口，导入 json_msg数据
    if(json_msg){
        QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "shadow publish topic_len=%lu payload_len=%lu",
                      (unsigned long)strlen(topic), (unsigned long)strlen(json_msg));
    }else{
        QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "shadow publish topic_len=%lu",
                      (unsigned long)strlen(topic));
    }

    qm_iot_mqtt_pub(shadow_handle->mqtt_handle, topic, (uint8_t *)json_msg, strlen(json_msg), QM_MQTT_QoS0);

    if(json_msg){
        qm_free(json_msg);
        json_msg = NULL;
    }

    return ret;
}

/**
 * @brief 结束shadow会话, 销毁实例并回收资源
 *
 * @param[in] handle 指向shadow会话句柄的指针
 *
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_shadow_deinit(void **handle)
{
    int ret = QM_EOK;
    qm_iot_shadow_handle_t **shadow_handle = (qm_iot_shadow_handle_t **)handle;

    if(shadow_handle == NULL){
        return -QM_EINVAL;
    }

    qm_list_destroy((*shadow_handle)->update_topic_map);

    qm_free(*shadow_handle);

    *shadow_handle = NULL;

    return ret;
}
