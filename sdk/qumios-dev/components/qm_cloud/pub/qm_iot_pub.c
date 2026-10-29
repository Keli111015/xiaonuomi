#include "qm_cloud_log.h"
#include "qm.h"
#include "qm_work.h"
#include "json_parser.h"
#include "qm_spec_core.h"
#include "qm_iot_pub.h"
#include "qm_iot_shadow.h"
#include "qm_iot_config.h"
#include "qm_utils_timer.h"
#include "qm_utils_list.h"
#include "qm_utils_string.h"


#include "qm_iot_mqtt.h"
#include "qm_iot_common.h"

#define LOG_TAG     "PUB"

#define QM_IOT_PUB_WORK_RANDOM_MS     (50)

#define QM_IOT_PUB_PROP_EXCEPT_LEN    (64)

#if CONFIG_QM_IOT_SPEC_SUPPORT
#define QM_IOT_COMMON_TOPIC_PROPERTY_GET                "/%u/%u/device/get"
#define QM_IOT_COMMON_TOPIC_PROPERTY_GET_REPLY          "/%u/%u/device/getReply"
#define QM_IOT_COMMON_TOPIC_PROPERTY_REPORT             "/%u/%u/device/report"

#define QM_IOT_COMMON_TOPIC_PROPERTY_CLOUD_SET          "/%u/%u/cloud/set"
#define QM_IOT_COMMON_TOPIC_PROPERTY_CLOUD_GET          "/%u/%u/cloud/get"
#define QM_IOT_COMMON_TOPIC_PROPERTY_CLOUD_GET_REPLY    "/%u/%u/cloud/getReply"


#define QM_IOT_COMMON_PARAMS_PROPERTY_PAYLOAD_FMT  "{\"method\":\"%s\",\"id\":%d,\"params\":%s}"
#endif

typedef enum
{
    QM_IOT_PUB_ID_CLOUD_SET  = 0,
    QM_IOT_PUB_ID_DEVICE_SET,
    QM_IOT_PUB_ID_CLOUD_GET,
    QM_IOT_PUB_ID_DEVICE_GET,
    QM_IOT_PUB_ID_DEVICE_REPORT,
    QM_IOT_PUB_ID_CLOUD_REPORT,
    QM_IOT_PUB_ID_MAX ,
}qm_iot_pub_id_t;


typedef struct
{
    void *mqtt_handle;
    void *shawdow_handle;
    void *userdata;
    /** 是否已完成 spec 相关 MQTT 预订阅。 */
    uint8_t mqtt_pre_sub_done;
    uint32_t delay_pub;
    qm_list_t *pub_list;
    qm_mutex_t pub_lock;
    qm_iot_pub_recv_handler_t recv_handler;
#if CONFIG_QM_IOT_SPEC_SUPPORT
    qm_list_t *prop_pub_list;
    qm_mutex_t prop_pub_lock;
    uint32_t up_id[QM_IOT_PUB_ID_MAX];
    uint32_t down_id[QM_IOT_PUB_ID_MAX];   //做最简单判断
#endif
}qm_iot_pub_handle_t;

/**
 * @brief 从 pub 会话解析 IOT 句柄（@ref QM_IOT_PUBOPT_USERDATA 设置的 @ref qm_iot_init 返回值）。
 */
static void *pub_iot_handle(qm_iot_pub_handle_t *pub)
{
    if (pub == NULL) {
        return NULL;
    }

    return pub->userdata;
}

#if CONFIG_QM_IOT_SPEC_SUPPORT
static void qm_iot_set_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata);
static void qm_iot_get_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata);
static void qm_iot_get_reply_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata);

/**
 * @brief MQTT 与 session 句柄就绪后注册 pub spec 下行 topic 预订阅（仅执行一次）。
 */
static void pub_mqtt_sub_if_ready(qm_iot_pub_handle_t *pub_handle)
{
    char topic[QM_IOT_COMMON_TOPIC_MAX_LEN] = {0};
    void *iot_handle = NULL;

    if (pub_handle == NULL || pub_handle->mqtt_handle == NULL) {
        return;
    }

    iot_handle = pub_iot_handle(pub_handle);
    if (iot_handle == NULL) {
        return;
    }

    if (pub_handle->mqtt_pre_sub_done) {
        return;
    }

    pub_handle->mqtt_pre_sub_done = QM_TRUE;

#if !CONFIG_QM_IOT_SET_CANCEL
    qm_snprintf(topic, QM_IOT_COMMON_TOPIC_MAX_LEN, QM_IOT_COMMON_TOPIC_PROPERTY_CLOUD_SET,
                qm_iot_pid_get(iot_handle), qm_iot_did_get(iot_handle));
    qm_iot_mqtt_pre_sub(pub_handle->mqtt_handle, topic, qm_iot_set_recv_handler, QM_MQTT_QoS1, pub_handle);
#endif

#if !CONFIG_QM_IOT_GET_CANCEL
    memset(topic, 0, QM_IOT_COMMON_TOPIC_MAX_LEN);
    qm_snprintf(topic, QM_IOT_COMMON_TOPIC_MAX_LEN, QM_IOT_COMMON_TOPIC_PROPERTY_GET_REPLY,
                qm_iot_pid_get(iot_handle), qm_iot_did_get(iot_handle));
    qm_iot_mqtt_pre_sub(pub_handle->mqtt_handle, topic, qm_iot_get_reply_recv_handler, QM_MQTT_QoS1, pub_handle);

    memset(topic, 0, QM_IOT_COMMON_TOPIC_MAX_LEN);
    qm_snprintf(topic, QM_IOT_COMMON_TOPIC_MAX_LEN, QM_IOT_COMMON_TOPIC_PROPERTY_CLOUD_GET,
                qm_iot_pid_get(iot_handle), qm_iot_did_get(iot_handle));
    qm_iot_mqtt_pre_sub(pub_handle->mqtt_handle, topic, qm_iot_get_recv_handler, QM_MQTT_QoS1, pub_handle);
#endif
}
#endif

typedef struct
{
    uint32_t did;
    qm_iot_pub_handle_t *pub_handle;
    qm_spec_property_operation_t *property_operation;
    qm_work_t  pub_work;
}qm_iot_pub_msg_handle_t;

#if CONFIG_QM_IOT_SPEC_SUPPORT
static int msg_id_get(qm_iot_pub_handle_t *pub_handle, qm_iot_pub_id_t id_type)
{
    if(pub_handle == NULL){
        return -QM_EINVAL;
    }
    pub_handle->up_id[id_type] = (pub_handle->up_id[id_type] == QM_IOT_COMMON_MSG_ID_MAX_NUM) ? 1 : pub_handle->up_id[id_type] + 1;
    return pub_handle->up_id[id_type];
}

static void payload_malloc_and_copy(qm_iot_pub_handle_t *pub_handle, char *type, qm_iot_pub_id_t id_type, uint8_t *msg, uint32_t msg_len, char **payload)
{
    char *json_msg = NULL;

    if(pub_handle == NULL || msg == NULL || !msg_len){
        return ;
    }

    msg_len = QM_IOT_COMMON_DID_MAX_LEN + strlen(type) + strlen(QM_IOT_COMMON_PARAMS_PROPERTY_PAYLOAD_FMT) + msg_len;
    json_msg = (char *)qm_malloc(msg_len + 1);
    if(json_msg == NULL){
        return ;
    }
    memset(json_msg, 0, msg_len + 1);

    qm_snprintf(json_msg, msg_len, QM_IOT_COMMON_PARAMS_PROPERTY_PAYLOAD_FMT, type, msg_id_get(pub_handle, id_type), msg);

    if(payload){
        *payload = json_msg;
    }
}
#endif

static int list_match(void *a, void *b)
{
    qm_iot_pub_msg_handle_t *a_handle = (qm_iot_pub_msg_handle_t *)a;
    qm_iot_pub_msg_handle_t *b_handle = (qm_iot_pub_msg_handle_t *)b;

    if(a_handle->did == b_handle->did){
        return QM_TRUE;
    }

    return QM_FALSE;
}

static void qm_iot_shadow_recv_handler(void *handle, qm_iot_shadow_recv_t *recv, void *userdata)
{
    qm_iot_pub_recv_t iot_recv = {0};
    qm_spec_data_info_t data_info = {0};
    qm_iot_pub_handle_t *pub_handle = (qm_iot_pub_handle_t *)userdata;
    if(pub_handle == NULL || handle == NULL || recv == NULL){
        return;
    }

    if(recv->type != QM_IOT_SHADOWRECV_SET){
        return ;
    }

    iot_recv.did = recv->did;
    iot_recv.type = QM_IOT_PUBRECV_SHADOW_SET;

    qm_spec_data_unpack(QM_SPEC_DATA_TYPE_AWS_JSON, QM_SPEC_MSG_TYPE_SET, (uint8_t *)recv->data.set.payload, recv->data.set.payload_len, &data_info);
    iot_recv.data.prop_set.operation = data_info.operation.property_operation;
    if(pub_handle->recv_handler){
        pub_handle->recv_handler(pub_handle, &iot_recv, pub_handle->userdata);
    }
    qm_spec_data_destroy(QM_SPEC_DATA_TYPE_AWS_JSON, QM_SPEC_MSG_TYPE_SET, &data_info);
}

#if CONFIG_QM_IOT_SPEC_SUPPORT
static void qm_iot_set_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    int type = 0;

    char *value = NULL;
    int value_len = 0;

    int int_id = 0;
    char *id = NULL;
    int id_len = 0;


    qm_iot_pub_recv_t iot_recv = {0};
    qm_spec_data_info_t data_info = {0};
    qm_iot_pub_handle_t *pub_handle = (qm_iot_pub_handle_t *)userdata;
    if(pub_handle == NULL || handle == NULL || packet == NULL){
        return;
    }

    if(packet->type != QM_IOT_MQTTRECV_PUB){
        return ;
    }

    iot_recv.did = qm_iot_did_get(pub_iot_handle(pub_handle));
    iot_recv.type = QM_IOT_PUBRECV_PROPERTY_SET;


    id = qm_json_get_value_by_name((char *)packet->data.pub.payload, (int)packet->data.pub.payload_len, QM_IOT_COMMON_ID_KEY, &id_len, &type);
    if(id == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "id find failed!!");
        return;
    }
    int_id = int_str_to_num(id, id_len);

    if(int_id == pub_handle->down_id[QM_IOT_PUB_ID_CLOUD_SET]){
        return ;
    }
    pub_handle->down_id[QM_IOT_PUB_ID_CLOUD_SET] = int_id;

    value = qm_json_get_value_by_name((char *)packet->data.pub.payload, (int)packet->data.pub.payload_len, QM_IOT_COMMON_PARAMS_KEY, &value_len, &type);
    if(value == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "id find failed!!");
        return;
    }

    qm_spec_data_unpack(QM_SPEC_DATA_TYPE_JSON, QM_SPEC_MSG_TYPE_SET, (uint8_t *)value, value_len, &data_info);
    iot_recv.data.prop_get.operation = data_info.operation.property_operation;
    if(pub_handle->recv_handler){
        pub_handle->recv_handler(pub_handle, &iot_recv, pub_handle->userdata);
    }
    qm_spec_data_destroy(QM_SPEC_DATA_TYPE_JSON, QM_SPEC_MSG_TYPE_SET, &data_info);
}

static void qm_iot_get_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    int type = 0;

    char *value = NULL;
    int value_len = 0;

    int int_id = 0;
    char *id = NULL;
    int id_len = 0;

    qm_iot_pub_recv_t iot_recv = {0};
    qm_spec_data_info_t data_info = {0};
    qm_iot_pub_handle_t *pub_handle = (qm_iot_pub_handle_t *)userdata;
    if(pub_handle == NULL || handle == NULL || packet == NULL){
        return;
    }

    if(packet->type != QM_IOT_MQTTRECV_PUB){
        return ;
    }

    iot_recv.did = qm_iot_did_get(pub_iot_handle(pub_handle));
    iot_recv.type = QM_IOT_PUBRECV_PROPERTY_GET_REQ;

    id = qm_json_get_value_by_name((char *)packet->data.pub.payload, (int)packet->data.pub.payload_len, QM_IOT_COMMON_ID_KEY, &id_len, &type);
    if(id == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "id find failed!!");
        return;
    }
    int_id = int_str_to_num(id, id_len);

    if(int_id == pub_handle->down_id[QM_IOT_PUB_ID_CLOUD_GET]){
        return ;
    }
    pub_handle->down_id[QM_IOT_PUB_ID_CLOUD_GET] = int_id;

    value = qm_json_get_value_by_name((char *)packet->data.pub.payload, (int)packet->data.pub.payload_len, QM_IOT_COMMON_PARAMS_KEY, &value_len, &type);
    if(value == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "id find failed!!");
        return;
    }

    qm_spec_data_unpack(QM_SPEC_DATA_TYPE_JSON, QM_SPEC_MSG_TYPE_CLOUD_GET, (uint8_t *)value, value_len, &data_info);
    iot_recv.data.prop_get.operation = data_info.operation.property_operation;
    if(pub_handle->recv_handler){
        pub_handle->recv_handler(pub_handle, &iot_recv, pub_handle->userdata);
    }
    qm_spec_data_destroy(QM_SPEC_DATA_TYPE_JSON, QM_SPEC_MSG_TYPE_CLOUD_GET, &data_info);
}


static void qm_iot_get_reply_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    int type = 0;

    char *value = NULL;
    int value_len = 0;

    int int_id = 0;
    char *id = NULL;
    int id_len = 0;

    qm_iot_pub_recv_t iot_recv = {0};
    qm_spec_data_info_t data_info = {0};
    qm_iot_pub_handle_t *pub_handle = (qm_iot_pub_handle_t *)userdata;
    if(pub_handle == NULL || handle == NULL || packet == NULL){
        return;
    }

    if(packet->type != QM_IOT_MQTTRECV_PUB){
        return ;
    }

    iot_recv.did = qm_iot_did_get(pub_iot_handle(pub_handle));
    iot_recv.type = QM_IOT_PUBRECV_PROPERTY_GET_RSP;

    id = qm_json_get_value_by_name((char *)packet->data.pub.payload, (int)packet->data.pub.payload_len, QM_IOT_COMMON_ID_KEY, &id_len, &type);
    if(id == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "id find failed!!");
        return;
    }
    int_id = int_str_to_num(id, id_len);

    if(int_id == pub_handle->down_id[QM_IOT_PUB_ID_DEVICE_GET]){
        return ;
    }
    pub_handle->down_id[QM_IOT_PUB_ID_DEVICE_GET] = int_id;

    value = qm_json_get_value_by_name((char *)packet->data.pub.payload, (int)packet->data.pub.payload_len, QM_IOT_COMMON_PARAMS_KEY, &value_len, &type);
    if(value == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "id find failed!!");
        return;
    }

    qm_spec_data_unpack(QM_SPEC_DATA_TYPE_JSON, QM_SPEC_MSG_TYPE_DEVICE_GET, (uint8_t *)value, value_len, &data_info);

    iot_recv.data.prop_get.operation = data_info.operation.property_operation;
    if(pub_handle->recv_handler){
        pub_handle->recv_handler(pub_handle, &iot_recv, pub_handle->userdata);
    }

    qm_spec_data_destroy(QM_SPEC_DATA_TYPE_JSON, QM_SPEC_MSG_TYPE_DEVICE_GET, &data_info);
}
#endif

/**
 * @brief 创建pub会话实例, 并以默认值配置会话参数
 *
 * @return void *
 * @retval 非NULL pub 实例的句柄
 * @retval NULL   初始化失败, 一般是内存分配失败导致
 *
 */
void *qm_iot_pub_init(void)
{
    int ret = QM_EOK;
    qm_iot_pub_handle_t *pub_handle = (qm_iot_pub_handle_t *)qm_malloc(sizeof(qm_iot_pub_handle_t));
    if(pub_handle == NULL){
        return NULL;
    }
    memset(pub_handle, 0, sizeof(qm_iot_pub_handle_t));

    ret = qm_mutex_new(&pub_handle->pub_lock);
    if(ret != QM_EOK){
        qm_free(pub_handle);
        return NULL;
    }
    pub_handle->pub_list = qm_list_new();
    if(pub_handle->pub_list == NULL){
        qm_mutex_free(&pub_handle->pub_lock);
        qm_free(pub_handle);
        return NULL;
    }
    pub_handle->pub_list->match = list_match;
#if CONFIG_QM_IOT_SPEC_SUPPORT
    ret = qm_mutex_new(&pub_handle->prop_pub_lock);
    if(ret != QM_EOK){
        qm_free(pub_handle);
        return NULL;
    }
    pub_handle->prop_pub_list = qm_list_new();
    if(pub_handle->prop_pub_list == NULL){
        qm_mutex_free(&pub_handle->prop_pub_lock);
        qm_free(pub_handle);
        return NULL;
    }
    pub_handle->prop_pub_list->match = list_match;
#endif

    return (void *)pub_handle;
}

/**
 * @brief 配置 pub 会话
 *
 * @param[in] handle pub 会话句柄
 * @param[in] option 配置选项, 更多信息请参考 @ref qm_iot_pub_option_t
 * @param[in] data   配置选项数据, 更多信息请参考 @ref qm_iot_pub_option_t
 *
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_pub_setopt(void *handle, qm_iot_pub_option_t option, void *data)
{
    qm_iot_pub_handle_t *pub_handle = (qm_iot_pub_handle_t *)handle;
    if(pub_handle == NULL){
        return -QM_EINVAL;
    }

    switch (option)
    {
        case QM_IOT_PUBOPT_USERDATA:
            pub_handle->userdata = data;
#if CONFIG_QM_IOT_SPEC_SUPPORT
            pub_mqtt_sub_if_ready(pub_handle);
#endif
        break;

        case QM_IOT_PUBOPT_DELAY_TIMEOUT:
            pub_handle->delay_pub = *((uint32_t *)data);
        break;

        case QM_IOT_PUBOPT_MQTT_HANDLE:
            pub_handle->mqtt_handle = data;
#if CONFIG_QM_IOT_SPEC_SUPPORT
            pub_mqtt_sub_if_ready(pub_handle);
#endif
        break;

        case QM_IOT_PUBOPT_RECV_HANDLER:
            if( pub_handle->mqtt_handle == NULL){
                return -QM_EINVAL;
            }

            pub_handle->recv_handler = (qm_iot_pub_recv_handler_t)data;

            if(pub_handle->shawdow_handle == NULL){
                pub_handle->shawdow_handle = qm_iot_shadow_init(pub_iot_handle(pub_handle));
                if(pub_handle->shawdow_handle ==  NULL){
                    return -QM_ENOMEM;
                }
            }

            qm_iot_shadow_setopt(pub_handle->shawdow_handle, QM_IOT_SHADOWOPT_MQTT_HANDLE, pub_handle->mqtt_handle);
            qm_iot_shadow_setopt(pub_handle->shawdow_handle, QM_IOT_SHADOWOPT_USERDATA, pub_handle);
            qm_iot_shadow_setopt(pub_handle->shawdow_handle, QM_IOT_SHADOWOPT_RECV_HANDLER, qm_iot_shadow_recv_handler);

#if CONFIG_QM_IOT_SPEC_SUPPORT
            pub_mqtt_sub_if_ready(pub_handle);
#endif

        break;

        case QM_IOT_PUBOPT_RESET_SUB:
            pub_handle->mqtt_pre_sub_done = QM_FALSE;
            pub_handle->mqtt_handle = NULL;
            if (pub_handle->shawdow_handle != NULL) {
                qm_iot_shadow_setopt(pub_handle->shawdow_handle, QM_IOT_SHADOWOPT_RESET_SUB, NULL);
            }
        break;
        default:
        break;
    }

    return QM_EOK;
}

static void qm_iot_pub_action(void *arg)
{
    int ret = QM_EOK;
    qm_list_node_t *node = NULL;
    qm_spec_data_info_t data_info = {0};
    qm_iot_shadow_msg_t shadow_msg = {0};
    qm_iot_pub_handle_t *pub_handle = NULL;
    qm_iot_pub_msg_handle_t  *pub_msg = NULL;
    qm_iot_pub_msg_handle_t  match_msg = {0};

    qm_iot_pub_msg_handle_t *msg_handle = (qm_iot_pub_msg_handle_t *)arg;
    if(msg_handle == NULL){
        return ;
    }

    pub_handle = msg_handle->pub_handle;

    match_msg.did = msg_handle->did;
    qm_mutex_lock(&pub_handle->pub_lock, QM_WAIT_FOREVER);
    node = qm_list_find(pub_handle->pub_list, &match_msg);
    if(node == NULL){
        qm_mutex_unlock(&pub_handle->pub_lock);
        return ;
    }
    qm_list_remove(pub_handle->pub_list, node);
    qm_mutex_unlock(&pub_handle->pub_lock);
    pub_msg = (qm_iot_pub_msg_handle_t *)node->val;

    shadow_msg.did = pub_msg->did;
    shadow_msg.type = QM_IOT_SHADOWMSG_UPDATE;
    data_info.operation.property_operation = pub_msg->property_operation;
    ret = qm_spec_data_pack(QM_SPEC_DATA_TYPE_AWS_JSON, QM_SPEC_MSG_TYPE_REPORT, &data_info);
    if(ret != QM_EOK){
        goto __exit;
    }
    shadow_msg.data.update.reported = (char *)data_info.updata;
    ret = qm_iot_shadow_send(pub_handle->shawdow_handle, &shadow_msg);
    if(ret != QM_EOK){
        QM_CLOUD_LOGE(LOG_TAG, "qm_iot_shadow_send failed:%d!!", ret);
    }

__exit:
    qm_spec_data_destroy(QM_SPEC_DATA_TYPE_AWS_JSON, QM_SPEC_MSG_TYPE_REPORT, &data_info);

    qm_list_node_destroy(node);

}

static qm_iot_pub_msg_handle_t *qm_iot_pub_msg_creat(qm_iot_pub_handle_t *pub_handle)
{
    qm_list_node_t *node = NULL;
    qm_iot_pub_msg_handle_t  *pub_msg = NULL;
    node = qm_list_node_extra_new(sizeof(qm_iot_pub_msg_handle_t));
    if(node == NULL){
        return NULL;
    }

    pub_msg = (qm_iot_pub_msg_handle_t *)node->val;

    pub_msg->property_operation = qm_spec_property_operation_creat();
    if(pub_msg->property_operation == NULL){
        qm_list_node_destroy(node);
        return NULL;
    }

    pub_msg->pub_handle = pub_handle;

    qm_list_rpush(pub_handle->pub_list, node);

    return pub_msg;
}

static int qm_iot_pub_msg_del(qm_iot_pub_msg_handle_t *msg_handle, qm_iot_pub_msg_t *msg)
{
    int prop_num;
    qm_spec_property_t *src_prop = NULL;
    qm_spec_property_t *spec_prop = NULL;
    qm_spec_property_operation_t *property_operation = msg->data.update.property_operation;

    while (1)
    {
        src_prop = qm_spec_property_next(property_operation, src_prop);
        if(src_prop == NULL){
            break;
        }

        spec_prop = qm_spec_property_find(msg_handle->property_operation, src_prop->siid, src_prop->piid);
        if(spec_prop == NULL){
            continue;
        }

        qm_spec_property_remove(msg_handle->property_operation, spec_prop);
        qm_spec_property_delete(spec_prop);
    }

    prop_num = msg_handle->property_operation->element_num;
    if(prop_num == 0){
        qm_cancel_delayed_action(&msg_handle->pub_work);
        qm_spec_property_operation_delete(msg_handle->property_operation);
        msg_handle->property_operation = NULL;
    }

    return prop_num;
}

static int qm_iot_pub_msg_push(qm_iot_pub_handle_t *pub_handle, qm_iot_pub_msg_t *msg)
{
    uint32_t random;
    qm_list_node_t *node = NULL;
    qm_iot_pub_msg_handle_t  *pub_msg = NULL;
    qm_iot_pub_msg_handle_t  match_msg = {0};

    match_msg.did = msg->did;

    qm_mutex_lock(&pub_handle->pub_lock, QM_WAIT_FOREVER);
    node = qm_list_find(pub_handle->pub_list, &match_msg);
    if(node){
        pub_msg = (qm_iot_pub_msg_handle_t *)node->val;
    }else{
        pub_msg = qm_iot_pub_msg_creat(pub_handle);
        if(pub_msg == NULL){
            qm_mutex_unlock(&pub_handle->pub_lock);
            return -QM_ENOMEM;
        }
    }
    qm_mutex_unlock(&pub_handle->pub_lock);

    qm_cancel_delayed_action(&pub_msg->pub_work);

    qm_mutex_lock(&pub_handle->pub_lock, QM_WAIT_FOREVER);

    qm_spec_property_operation_merge(pub_msg->property_operation, msg->data.update.property_operation);
    qm_spec_property_operation_delete(msg->data.update.property_operation);
    msg->data.update.property_operation = NULL;

    pub_msg->did = msg->did;
    qm_srandom(qm_now_ms());
    random = qm_random_get(QM_IOT_PUB_WORK_RANDOM_MS);

    qm_mutex_unlock(&pub_handle->pub_lock);

    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_UPLINK, "[%d]delay_pub %d!!", pub_msg->did, random + pub_handle->delay_pub);
    qm_post_delayed_action(&pub_msg->pub_work, qm_iot_pub_action, pub_msg, random + pub_handle->delay_pub);

    return QM_EOK;
}

static int qm_iot_pub_msg_send(qm_iot_pub_handle_t *pub_handle, qm_iot_pub_msg_t *msg)
{
    int prop_num;
    int ret = QM_EOK;
    qm_list_node_t *node = NULL;
    qm_spec_data_info_t data_info = {0};
    qm_iot_shadow_msg_t shadow_msg = {0};
    qm_iot_pub_msg_handle_t  match_msg = {0};

    match_msg.did = msg->did;
    qm_mutex_lock(&pub_handle->pub_lock, QM_WAIT_FOREVER);
    node = qm_list_find(pub_handle->pub_list, &match_msg);
    if(node){
        prop_num = qm_iot_pub_msg_del((qm_iot_pub_msg_handle_t *)node->val, msg);
        if(prop_num == 0){
            qm_list_remove(pub_handle->pub_list, node);
            qm_list_node_destroy(node);
        }
    }
    qm_mutex_unlock(&pub_handle->pub_lock);

    shadow_msg.did = msg->did;
    shadow_msg.type = QM_IOT_SHADOWMSG_UPDATE;
    data_info.operation.property_operation = msg->data.update.property_operation;

    qm_spec_data_pack(QM_SPEC_DATA_TYPE_AWS_JSON, QM_SPEC_MSG_TYPE_REPORT, &data_info);

    shadow_msg.data.update.reported = (char *)data_info.updata;
    ret = qm_iot_shadow_send(pub_handle->shawdow_handle, &shadow_msg);
    if(ret != QM_EOK){
        QM_CLOUD_LOGE(LOG_TAG, "qm_iot_shadow_send failed:%d!!", ret);
    }
    qm_spec_data_destroy(QM_SPEC_DATA_TYPE_AWS_JSON, QM_SPEC_MSG_TYPE_REPORT, &data_info);

    return QM_EOK;
}

#if CONFIG_QM_IOT_SPEC_SUPPORT
static void qm_iot_prop_pub_action(void *arg)
{
    int ret = QM_EOK;
    char *payload = NULL;
    qm_list_node_t *node = NULL;
    qm_spec_data_info_t data_info = {0};
    qm_iot_pub_handle_t *pub_handle = NULL;
    qm_iot_pub_msg_handle_t  *pub_msg = NULL;
    qm_iot_pub_msg_handle_t  match_msg = {0};
    char topic[QM_IOT_COMMON_TOPIC_MAX_LEN] = {0};


    qm_iot_pub_msg_handle_t *msg_handle = (qm_iot_pub_msg_handle_t *)arg;
    if(msg_handle == NULL){
        return ;
    }

    pub_handle = msg_handle->pub_handle;

    match_msg.did = msg_handle->did;
    qm_mutex_lock(&pub_handle->prop_pub_lock, QM_WAIT_FOREVER);
    node = qm_list_find(pub_handle->prop_pub_list, &match_msg);
    if(node == NULL){
        qm_mutex_unlock(&pub_handle->prop_pub_lock);
        return ;
    }
    qm_list_remove(pub_handle->prop_pub_list, node);
    qm_mutex_unlock(&pub_handle->prop_pub_lock);
    pub_msg = (qm_iot_pub_msg_handle_t *)node->val;

    data_info.operation.property_operation = pub_msg->property_operation;
    ret = qm_spec_data_pack(QM_SPEC_DATA_TYPE_JSON, QM_SPEC_MSG_TYPE_REPORT, &data_info);
    if(ret != QM_EOK){
        goto __exit;
    }

    qm_snprintf(topic, QM_IOT_COMMON_TOPIC_MAX_LEN, QM_IOT_COMMON_TOPIC_PROPERTY_REPORT,
                qm_iot_pid_get(pub_iot_handle(pub_handle)),
                qm_iot_did_get(pub_iot_handle(pub_handle)));

    payload_malloc_and_copy(pub_handle, QM_IOT_COMMON_PARAMS_REPORT_KEY, QM_IOT_PUB_ID_CLOUD_REPORT, data_info.updata, data_info.updata_len, &payload);

    qm_spec_data_destroy(QM_SPEC_DATA_TYPE_JSON, QM_SPEC_MSG_TYPE_REPORT, &data_info);

    if(payload == NULL){
        goto __exit;
    }

    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "publish topic_len=%lu payload_len=%lu",
                  (unsigned long)strlen(topic), (unsigned long)strlen(payload));
    qm_iot_mqtt_pub(pub_handle->mqtt_handle, topic, (uint8_t *)payload, strlen(payload), QM_MQTT_QoS1);
    qm_free(payload);

__exit:

    qm_list_node_destroy(node);

}

static qm_iot_pub_msg_handle_t *qm_iot_prop_pub_msg_creat(qm_iot_pub_handle_t *pub_handle)
{
    qm_list_node_t *node = NULL;
    qm_iot_pub_msg_handle_t  *pub_msg = NULL;
    node = qm_list_node_extra_new(sizeof(qm_iot_pub_msg_handle_t));
    if(node == NULL){
        return NULL;
    }

    pub_msg = (qm_iot_pub_msg_handle_t *)node->val;

    pub_msg->property_operation = qm_spec_property_operation_creat();
    if(pub_msg->property_operation == NULL){
        qm_list_node_destroy(node);
        return NULL;
    }

    pub_msg->pub_handle = pub_handle;

    qm_list_rpush(pub_handle->prop_pub_list, node);

    return pub_msg;
}

static int qm_iot_prop_pub_msg_del(qm_iot_pub_msg_handle_t *msg_handle, qm_iot_pub_msg_t *msg)
{
    int prop_num;
    qm_spec_property_t *src_prop = NULL;
    qm_spec_property_t *spec_prop = NULL;
    qm_spec_property_operation_t *property_operation = msg->data.update.property_operation;

    while (1)
    {
        src_prop = qm_spec_property_next(property_operation, src_prop);
        if(src_prop == NULL){
            break;
        }

        spec_prop = qm_spec_property_find(msg_handle->property_operation, src_prop->siid, src_prop->piid);
        if(spec_prop == NULL){
            continue;
        }

        qm_spec_property_remove(msg_handle->property_operation, spec_prop);
        qm_spec_property_delete(spec_prop);
    }

    prop_num = msg_handle->property_operation->element_num;
    if(prop_num == 0){
        qm_cancel_delayed_action(&msg_handle->pub_work);
        qm_spec_property_operation_delete(msg_handle->property_operation);
        msg_handle->property_operation = NULL;
    }

    return prop_num;
}

static int qm_iot_prop_pub_msg_push(qm_iot_pub_handle_t *pub_handle, qm_iot_pub_msg_t *msg)
{
    uint32_t random;
    qm_list_node_t *node = NULL;
    qm_iot_pub_msg_handle_t  *pub_msg = NULL;
    qm_iot_pub_msg_handle_t  match_msg = {0};

    match_msg.did = msg->did;

    qm_mutex_lock(&pub_handle->prop_pub_lock, QM_WAIT_FOREVER);
    node = qm_list_find(pub_handle->prop_pub_list, &match_msg);
    if(node){
        pub_msg = (qm_iot_pub_msg_handle_t *)node->val;
    }else{
        pub_msg = qm_iot_prop_pub_msg_creat(pub_handle);
        if(pub_msg == NULL){
            qm_mutex_unlock(&pub_handle->prop_pub_lock);
            return -QM_ENOMEM;
        }
    }
    qm_mutex_unlock(&pub_handle->prop_pub_lock);

    qm_cancel_delayed_action(&pub_msg->pub_work);

    qm_mutex_lock(&pub_handle->prop_pub_lock, QM_WAIT_FOREVER);

    qm_spec_property_operation_merge(pub_msg->property_operation, msg->data.update.property_operation);
    qm_spec_property_operation_delete(msg->data.update.property_operation);
    msg->data.update.property_operation = NULL;

    pub_msg->did = msg->did;
    qm_srandom(qm_now_ms());
    random = qm_random_get(QM_IOT_PUB_WORK_RANDOM_MS);

    qm_mutex_unlock(&pub_handle->prop_pub_lock);

    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_UPLINK, "[%d]delay_prop_pub %d!!", pub_msg->did, random + pub_handle->delay_pub);
    qm_post_delayed_action(&pub_msg->pub_work, qm_iot_prop_pub_action, pub_msg, random + pub_handle->delay_pub);

    return QM_EOK;
}

static int qm_iot_prop_pub_msg_send(qm_iot_pub_handle_t *pub_handle, qm_iot_pub_msg_t *msg)
{
    int prop_num;
    char *payload = NULL;
    qm_list_node_t *node = NULL;
    qm_spec_data_info_t data_info = {0};
    qm_iot_pub_msg_handle_t  match_msg = {0};
    char topic[QM_IOT_COMMON_TOPIC_MAX_LEN] = {0};

    match_msg.did = msg->did;
    qm_mutex_lock(&pub_handle->prop_pub_lock, QM_WAIT_FOREVER);
    node = qm_list_find(pub_handle->prop_pub_list, &match_msg);
    if(node){
        prop_num = qm_iot_prop_pub_msg_del((qm_iot_pub_msg_handle_t *)node->val, msg);
        if(prop_num == 0){
            qm_list_remove(pub_handle->prop_pub_list, node);
            qm_list_node_destroy(node);
        }
    }
    qm_mutex_unlock(&pub_handle->prop_pub_lock);

    data_info.operation.property_operation = msg->data.update.property_operation;
    qm_spec_data_pack(QM_SPEC_DATA_TYPE_JSON, QM_SPEC_MSG_TYPE_REPORT, &data_info);
    qm_snprintf(topic, QM_IOT_COMMON_TOPIC_MAX_LEN, QM_IOT_COMMON_TOPIC_PROPERTY_REPORT,
                qm_iot_pid_get(pub_iot_handle(pub_handle)),
                qm_iot_did_get(pub_iot_handle(pub_handle)));
    payload_malloc_and_copy(pub_handle, QM_IOT_COMMON_PARAMS_REPORT_KEY, QM_IOT_PUB_ID_CLOUD_REPORT, data_info.updata, data_info.updata_len, &payload);
    qm_spec_data_destroy(QM_SPEC_DATA_TYPE_JSON, QM_SPEC_MSG_TYPE_REPORT, &data_info);

    if(payload == NULL){
        goto __exit;
    }
    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "publish topic_len=%lu payload_len=%lu",
                  (unsigned long)strlen(topic), (unsigned long)strlen(payload));
    qm_iot_mqtt_pub(pub_handle->mqtt_handle, topic, (uint8_t *)payload, strlen(payload), QM_MQTT_QoS1);

    qm_free(payload);
__exit:

    return QM_EOK;
}


static int qm_iot_prop_pub_msg_get_req(qm_iot_pub_handle_t *pub_handle, qm_iot_pub_msg_t *msg)
{
    char *payload = NULL;
    qm_spec_property_t *prop = NULL;
    qm_spec_data_info_t data_info = {0};
    char topic[QM_IOT_COMMON_TOPIC_MAX_LEN] = {0};

    data_info.operation.property_operation = msg->data.update.property_operation;

    while (1)
    {
        prop = qm_spec_property_next(data_info.operation.property_operation, prop);
        if(prop == NULL){
            break;
        }

        //防止应用层带参数
        prop->value.len = 0;
    }

    qm_spec_data_pack(QM_SPEC_DATA_TYPE_JSON, QM_SPEC_MSG_TYPE_DEVICE_GET, &data_info);

    qm_snprintf(topic, QM_IOT_COMMON_TOPIC_MAX_LEN, QM_IOT_COMMON_TOPIC_PROPERTY_GET,
                qm_iot_pid_get(pub_iot_handle(pub_handle)),
                qm_iot_did_get(pub_iot_handle(pub_handle)));

    payload_malloc_and_copy(pub_handle, QM_IOT_COMMON_PARAMS_GET_KEY, QM_IOT_PUB_ID_DEVICE_GET, data_info.updata, data_info.updata_len, &payload);

    qm_spec_data_destroy(QM_SPEC_DATA_TYPE_JSON, QM_SPEC_MSG_TYPE_DEVICE_GET, &data_info);

    if(payload == NULL){
        goto __exit;
    }
    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "publish topic_len=%lu payload_len=%lu",
                  (unsigned long)strlen(topic), (unsigned long)strlen(payload));
    qm_iot_mqtt_pub(pub_handle->mqtt_handle, topic, (uint8_t *)payload, strlen(payload), QM_MQTT_QoS1);

    qm_free(payload);
__exit:

    return QM_EOK;
}


static int qm_iot_prop_pub_msg_get_rsp(qm_iot_pub_handle_t *pub_handle, qm_iot_pub_msg_t *msg)
{
    char *payload = NULL;
    qm_spec_data_info_t data_info = {0};
    char topic[QM_IOT_COMMON_TOPIC_MAX_LEN] = {0};

    data_info.operation.property_operation = msg->data.update.property_operation;
    qm_spec_data_pack(QM_SPEC_DATA_TYPE_JSON, QM_SPEC_MSG_TYPE_CLOUD_GET, &data_info);

    qm_snprintf(topic, QM_IOT_COMMON_TOPIC_MAX_LEN, QM_IOT_COMMON_TOPIC_PROPERTY_CLOUD_GET_REPLY,
                qm_iot_pid_get(pub_iot_handle(pub_handle)),
                qm_iot_did_get(pub_iot_handle(pub_handle)));

    payload_malloc_and_copy(pub_handle, QM_IOT_COMMON_PARAMS_GET_KEY, QM_IOT_PUB_ID_CLOUD_GET, data_info.updata, data_info.updata_len, &payload);

    qm_spec_data_destroy(QM_SPEC_DATA_TYPE_JSON, QM_SPEC_MSG_TYPE_CLOUD_GET, &data_info);

    if(payload == NULL){
        goto __exit;
    }
    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "publish topic_len=%lu payload_len=%lu",
                  (unsigned long)strlen(topic), (unsigned long)strlen(payload));
    qm_iot_mqtt_pub(pub_handle->mqtt_handle, topic, (uint8_t *)payload, strlen(payload), QM_MQTT_QoS1);
    qm_free(payload);

__exit:

    return QM_EOK;
}
#endif

/**
 * @brief 向服务器发送 pub 消息
 *
 * @param[in] handle pub 会话句柄
 * @param[in] msg    消息结构体, 可指定发送设备did, 消息类型, 消息数据等, 更多信息请参考@ref qm_iot_pub_msg_t
 * @param[in] force  置1为强制上报
 *
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_pub_send(void *handle, qm_iot_pub_msg_t *msg, int force)
{
    qm_iot_pub_handle_t *pub_handle = (qm_iot_pub_handle_t *)handle;
    if(pub_handle == NULL || msg == NULL){
        return -QM_EINVAL;
    }

    switch (msg->type)
    {

        case QM_IOT_PUBMSG_SHADOW_UPDATE:

            if(force){
                qm_iot_pub_msg_send(pub_handle, msg);
            }else{
                qm_iot_pub_msg_push(pub_handle, msg);
            }

        break;
    #if CONFIG_QM_IOT_SPEC_SUPPORT
        case QM_IOT_PUBMSG_UPDATE:
            if(force){
                qm_iot_prop_pub_msg_send(pub_handle, msg);
            }else{
                qm_iot_prop_pub_msg_push(pub_handle, msg);
            }
        break;

        case QM_IOT_PUBMSG_GET_REQ:
            qm_iot_prop_pub_msg_get_req(pub_handle,msg);
        break;

        case QM_IOT_PUBMSG_GET_RSP:
            qm_iot_prop_pub_msg_get_rsp(pub_handle,msg);
        break;
    #endif
        default:

        break;
    }

    return QM_EOK;
}

/**
 * @brief 向服务器发送shadow订阅请求
 *
 * @param[in] handle shadow会话句柄
 * @param[in] msg    消息结构体, 可指定发送设备did, 消息类型, 消息数据等, 更多信息请参考@ref qm_iot_pub_msg_t
 *
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_pub_sub(void *handle, qm_iot_pub_type_t pub_type, uint32_t did)
{
    qm_iot_pub_handle_t *pub_handle = (qm_iot_pub_handle_t *)handle;
    if(pub_handle == NULL || pub_type != QM_IOT_PUB_TYPE_SHADOW){
        return -QM_EINVAL;
    }
    return qm_iot_shadow_sub(pub_handle->shawdow_handle, did);
}

/**
 * @brief 向服务器发送shadow订阅请求
 *
 * @param[in] handle shadow会话句柄
 * @param[in] msg    消息结构体, 可指定发送设备did, 消息类型, 消息数据等, 更多信息请参考@ref qm_iot_pub_msg_t
 *
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_pub_unsub(void *handle, qm_iot_pub_type_t pub_type, uint32_t did)
{
    qm_iot_pub_handle_t *pub_handle = (qm_iot_pub_handle_t *)handle;
    if(pub_handle == NULL || pub_type != QM_IOT_PUB_TYPE_SHADOW){
        return -QM_EINVAL;
    }
    return qm_iot_shadow_unsub(pub_handle->shawdow_handle, did);
}

/**
 * @brief 向服务器发送shadow订阅请求
 *
 * @param[in] handle shadow会话句柄
 * @param[in] msg    消息结构体, 可指定发送设备did, 消息类型, 消息数据等, 更多信息请参考@ref qm_iot_pub_msg_t
 *
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_pub_all_unsub(void *handle, qm_iot_pub_type_t pub_type)
{
    qm_iot_pub_handle_t *pub_handle = (qm_iot_pub_handle_t *)handle;
    if(pub_handle == NULL || pub_type != QM_IOT_PUB_TYPE_SHADOW){
        return -QM_EINVAL;
    }
    return qm_iot_shadow_unsub_all(pub_handle->shawdow_handle);
}

/**
 * @brief 结束shadow会话, 销毁实例并回收资源
 *
 * @param[in] handle 指向 pub 会话句柄的指针
 *
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_pub_deinit(void **handle)
{
    qm_iot_pub_handle_t **pub_handle = (qm_iot_pub_handle_t **)handle;

    if(pub_handle == NULL || *pub_handle == NULL){
        return -QM_EINVAL;
    }

#if CONFIG_QM_IOT_SPEC_SUPPORT
    qm_mutex_lock(&(*pub_handle)->prop_pub_lock, QM_WAIT_FOREVER);
    qm_list_destroy((*pub_handle)->prop_pub_list);
    qm_mutex_unlock(&(*pub_handle)->prop_pub_lock);
    qm_mutex_free(&(*pub_handle)->prop_pub_lock);
#endif

    qm_free(*pub_handle);
    *pub_handle = NULL;

    return QM_EOK;
}
