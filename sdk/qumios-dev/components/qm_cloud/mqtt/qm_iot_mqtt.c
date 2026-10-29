#include "qm_cloud_log.h"
#include "qm.h"
#include "qm_iot_mqtt.h"
#include "iot_mqtt.h"
#include "iot_mqtt_client.h"
#include "iot_mqtt_errno.h"
#include "qm_utils_list.h"
#include "qm_kernel.h"
#include "qm_errno.h"
#include "qm_log.h"
#include "qm_utils_timer.h"
#include "qm_iot_config.h"
#include <string.h>

#define LOG_TAG "iot mqtt"

#ifndef CONFIG_QM_IOT_MQTT_READ_MSGLEN
#define CONFIG_QM_IOT_MQTT_READ_MSGLEN   8144
#endif

#ifndef CONFIG_QM_IOT_MQTT_WRITE_MSGLEN
#define CONFIG_QM_IOT_MQTT_WRITE_MSGLEN   2048
#endif

#ifndef CONFIG_QM_IOT_MQTT_YIELD_TIME
#define CONFIG_QM_IOT_MQTT_YIELD_TIME     100
#endif

#ifndef CONFIG_QM_IOT_MQTT_RETRY_INTERVAL_MAX_MS
#define CONFIG_QM_IOT_MQTT_RETRY_INTERVAL_MAX_MS  5000
#endif

#ifndef CONFIG_QM_IOT_MQTT_RETRY_INTERVAL_MIN_MS
#define CONFIG_QM_IOT_MQTT_RETRY_INTERVAL_MIN_MS  500
#endif

#ifndef CONFIG_QM_IOT_MQTT_TOPIC_SUB_MAX_COUNT
#define CONFIG_QM_IOT_MQTT_TOPIC_SUB_MAX_COUNT     8
#endif

#ifndef CONFIG_QM_IOT_MQTT_TOPIC_SUB_RETRY_COUNT
#define CONFIG_QM_IOT_MQTT_TOPIC_SUB_RETRY_COUNT   3
#endif

typedef struct
{
    void *mqtt_handle;
    char *mqtt_host;
    uint16_t mqtt_port;
    char *username;
    char *password;
    char *clientid;
    uint16_t keep_alive_s;
    uint8_t clean_session;
    char *server_crt;
    char *client_crt;
    char *client_privkey;
    char *msg_readbuf;
    char *msg_writebuf;
    /** IOT 会话 句柄，与 @ref QM_IOT_MQTTOPT_USERDATA 一致，为 @ref qm_iot_init 返回值；本模块不直接调用 config API。 */
    void *userdata;
    int interval_ms;
    int interval_max_ms;
    int exit;
    volatile qm_iot_network_state_t network_state;
    /** Wakes the processing task on network state changes or deinit. */
    qm_sem_t network_wakeup;
    qm_mutex_t lock;
    qm_list_t *sub_list;
    qm_list_t *pub_list;
    qm_list_t pre_sub_list;
    qm_iot_mqtt_event_handler_t event_handler;
    qm_iot_mqtt_recv_handler_t recv_handler;
} qm_iot_mqtt_ctx_t;

typedef struct
{
    qm_iot_mqtt_qos_t qos;
    uint16_t topic_len;
    uint16_t payload_len;
    char     data[1];
}qm_iot_mqtt_pub_node_t;


static int topic_match(void *sub_topic, void *topic);
static int sub_topic_info_reset(void *handle);
static void sub_topic_handler_reset(void *handle);
static void sub_topic_info_remove_by_topic(qm_list_t *list, const char *topic);

static void mqtt_wakeup(qm_iot_mqtt_ctx_t *mqtt_ctx)
{
    if (mqtt_ctx != NULL && qm_sem_is_valid(&mqtt_ctx->network_wakeup)) {
        (void)qm_sem_signal(&mqtt_ctx->network_wakeup);
    }
}

void *qm_iot_mqtt_init(void)
{
    qm_err_t ret = QM_EOK;
    qm_iot_mqtt_ctx_t *mqtt_ctx = (qm_iot_mqtt_ctx_t*)qm_malloc(sizeof(qm_iot_mqtt_ctx_t));
    if(mqtt_ctx == NULL){
        return NULL;
    }
    memset(mqtt_ctx, 0, sizeof(qm_iot_mqtt_ctx_t));
    ret = qm_mutex_new(&mqtt_ctx->lock);
    if(ret != QM_EOK){
        qm_free(mqtt_ctx);
        return NULL;
    }

    ret = qm_sem_new(&mqtt_ctx->network_wakeup, 0);
    if (ret != QM_EOK) {
        qm_mutex_free(&mqtt_ctx->lock);
        qm_free(mqtt_ctx);
        return NULL;
    }

    mqtt_ctx->pub_list = qm_list_new();
    if(mqtt_ctx->pub_list == NULL){
        qm_sem_free(&mqtt_ctx->network_wakeup);
        qm_mutex_free(&mqtt_ctx->lock);
        qm_free(mqtt_ctx);
        return NULL;
    }


    mqtt_ctx->sub_list = qm_list_new();
    if(mqtt_ctx->sub_list == NULL){
        qm_list_destroy(mqtt_ctx->pub_list);
        qm_sem_free(&mqtt_ctx->network_wakeup);
        qm_mutex_free(&mqtt_ctx->lock);
        qm_free(mqtt_ctx);
        return NULL;
    }
    mqtt_ctx->clean_session = QM_TRUE;
    mqtt_ctx->network_state = QM_IOT_NETWORK_UP;

    mqtt_ctx->sub_list->match = topic_match;
    return (void*)mqtt_ctx;
}

int32_t qm_iot_mqtt_setopt(void *handle, qm_iot_mqtt_option_t option, void *data)
{
    uint16_t port = 0;
    qm_iot_mqtt_network_cred_t *network_cred = NULL;
    qm_iot_mqtt_ctx_t *mqtt_ctx = (qm_iot_mqtt_ctx_t*)handle;
    if(handle == NULL || data == NULL){
        return -QM_EINVAL;
    }

    switch(option){

        case QM_IOT_MQTTOPT_HOST:
            mqtt_ctx->mqtt_host = (char*)data;
        break;

        case QM_IOT_MQTTOPT_PORT:
            port = *(uint16_t*)data;
            if(port == 0) {
                return -QM_EINVAL;
            }
            mqtt_ctx->mqtt_port = port;
        break;

        case QM_IOT_MQTTOPT_USERNAME:
            mqtt_ctx->username = (char*)data;
        break;

        case QM_IOT_MQTTOPT_PASSWORD:
            mqtt_ctx->password = (char*)data;
        break;

        case QM_IOT_MQTTOPT_CLIENTID:
            mqtt_ctx->clientid = (char*)data;
        break;

        case QM_IOT_MQTTOPT_NETWORK_CRED:
            network_cred = (qm_iot_mqtt_network_cred_t*)data;
            mqtt_ctx->server_crt = network_cred->server_crt;
            mqtt_ctx->client_crt = network_cred->client_crt;
            mqtt_ctx->client_privkey = network_cred->client_privkey;
        break;

        case QM_IOT_MQTTOPT_KEEPALIVE_SEC:
            mqtt_ctx->keep_alive_s = *(uint16_t*)data;
        break;

        case QM_IOT_MQTTOPT_CLEAN_SESSION:
            mqtt_ctx->clean_session = *(uint8_t*)data;
        break;

        case QM_IOT_MQTTOPT_USERDATA:
            mqtt_ctx->userdata = data;
        break;

        case QM_IOT_MQTTOPT_RECV_HANDLER:
            mqtt_ctx->recv_handler = (qm_iot_mqtt_recv_handler_t)data;
        break;

        case QM_IOT_MQTTOPT_EVENT_HANDLER:
            mqtt_ctx->event_handler = (qm_iot_mqtt_event_handler_t)data;
        break;

        default:
            return -QM_EINVAL;
        break;
    }

    return QM_EOK;
}

static void event_handle(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg)
{
    int code  = 0;
    qm_iot_mqtt_event_t mqtt_event;
    qm_iot_mqtt_recv_t mqtt_recv;
    qm_iot_mqtt_ctx_t *mqtt_ctx = (qm_iot_mqtt_ctx_t*)pcontext;
    iot_mqtt_topic_info_pt topic_msg;
    uintptr_t packet_id;

    switch (msg->event_type) {
        case IOT_MQTT_EVENT_UNDEF:
            QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_CONTROL, "undefined event occur.");
            break;

        case IOT_MQTT_EVENT_DISCONNECT:
            QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_LIFECYCLE, "MQTT disconnect.");
            sub_topic_info_reset(mqtt_ctx);
            if(mqtt_ctx->event_handler){
                mqtt_event.type = QM_IOT_MQTTEVT_DISCONNECT;
                mqtt_ctx->event_handler((void*)mqtt_ctx, &mqtt_event, mqtt_ctx->userdata);
            }
            break;

        case IOT_MQTT_EVENT_RECONNECT:
            code = (int)msg->msg;
            QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_LIFECYCLE, "MQTT reconnect,result[%d].", code);
            if(code == SUCCESS_RETURN){
                sub_topic_info_reset(mqtt_ctx);
                if(mqtt_ctx->event_handler){
                    mqtt_event.type = QM_IOT_MQTTEVT_RECONNECT;
                    mqtt_ctx->event_handler((void*)mqtt_ctx, &mqtt_event, mqtt_ctx->userdata);
                }
            }
            break;

        case IOT_MQTT_EVENT_SUBCRIBE_SUCCESS:
            packet_id = (uintptr_t)msg->msg;
            QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_CONTROL, "subscribe success, packet-id=%u", (unsigned int)packet_id);
            mqtt_recv.type = QM_IOT_MQTTRECV_SUB_ACK;
            mqtt_recv.data.sub_ack.res = QM_IOT_MQTT_RES_OK;
            mqtt_recv.data.sub_ack.packet_id = (uint16_t)packet_id;
            if(mqtt_ctx->recv_handler){
                mqtt_ctx->recv_handler((void*)mqtt_ctx, &mqtt_recv, mqtt_ctx->userdata);
            }
            break;

        case IOT_MQTT_EVENT_SUBCRIBE_TIMEOUT:
            packet_id = (uintptr_t)msg->msg;
            QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_CONTROL, "subscribe wait ack timeout, packet-id=%u", (unsigned int)packet_id);
            mqtt_recv.type = QM_IOT_MQTTRECV_SUB_ACK;
            mqtt_recv.data.sub_ack.res = QM_IOT_MQTT_RES_SUB_TIMEOUT;
            mqtt_recv.data.sub_ack.packet_id = (uint16_t)packet_id;
            if(mqtt_ctx->recv_handler){
                mqtt_ctx->recv_handler((void*)mqtt_ctx, &mqtt_recv, mqtt_ctx->userdata);
            }
            break;

        case IOT_MQTT_EVENT_SUBCRIBE_NACK:
            packet_id = (uintptr_t)msg->msg;
            QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_CONTROL, "subscribe nack, packet-id=%u", (unsigned int)packet_id);

            //todo: 订阅失败后,进行重试
            qm_iot_mqtt_pre_sub_start(mqtt_ctx);

            mqtt_recv.type = QM_IOT_MQTTRECV_SUB_ACK;
            mqtt_recv.data.sub_ack.res = QM_IOT_MQTT_RES_SUB_ERR;
            mqtt_recv.data.sub_ack.packet_id = (uint16_t)packet_id;
            if(mqtt_ctx->recv_handler){
                mqtt_ctx->recv_handler((void*)mqtt_ctx, &mqtt_recv, mqtt_ctx->userdata);
            }
            break;

        case IOT_MQTT_EVENT_UNSUBCRIBE_SUCCESS:
            packet_id = (uintptr_t)msg->msg;
            QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_CONTROL, "unsubscribe success, packet-id=%u", (unsigned int)packet_id);
            mqtt_recv.type = QM_IOT_MQTTRECV_UNSUB_ACK;
            mqtt_recv.data.unsub_ack.res = QM_IOT_MQTT_RES_OK;
            mqtt_recv.data.unsub_ack.packet_id = (uint16_t)packet_id;
            if(mqtt_ctx->recv_handler){
                mqtt_ctx->recv_handler((void*)mqtt_ctx, &mqtt_recv, mqtt_ctx->userdata);
            }
            break;

        case IOT_MQTT_EVENT_UNSUBCRIBE_TIMEOUT:
            packet_id = (uintptr_t)msg->msg;
            QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_CONTROL, "unsubscribe timeout, packet-id=%u", (unsigned int)packet_id);
            mqtt_recv.type = QM_IOT_MQTTRECV_UNSUB_ACK;
            mqtt_recv.data.unsub_ack.res = QM_IOT_MQTT_RES_UNSUB_TIMEOUT;
            mqtt_recv.data.unsub_ack.packet_id = (uint16_t)packet_id;
            if(mqtt_ctx->recv_handler){
                mqtt_ctx->recv_handler((void*)mqtt_ctx, &mqtt_recv, mqtt_ctx->userdata);
            }
            break;

        case IOT_MQTT_EVENT_PUBLISH_SUCCESS:
            packet_id = (uintptr_t)msg->msg;
            QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_CONTROL, "publish success, packet-id=%u", (unsigned int)packet_id);
            mqtt_recv.type = QM_IOT_MQTTRECV_PUB_ACK;
            mqtt_recv.data.pub_ack.res = QM_IOT_MQTT_RES_OK;
            mqtt_recv.data.pub_ack.packet_id = (uint16_t)packet_id;
            if(mqtt_ctx->recv_handler){
                mqtt_ctx->recv_handler((void*)mqtt_ctx, &mqtt_recv, mqtt_ctx->userdata);
            }
            break;

        case IOT_MQTT_EVENT_PUBLISH_TIMEOUT:
            packet_id = (uintptr_t)msg->msg;
            QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_CONTROL, "publish timeout, packet-id=%u", (unsigned int)packet_id);
            mqtt_recv.type = QM_IOT_MQTTRECV_PUB_ACK;
            mqtt_recv.data.pub_ack.res = QM_IOT_MQTT_RES_PUB_TIMEOUT;
            mqtt_recv.data.pub_ack.packet_id = (uint16_t)packet_id;
            if(mqtt_ctx->recv_handler){
                mqtt_ctx->recv_handler((void*)mqtt_ctx, &mqtt_recv, mqtt_ctx->userdata);
            }
            break;

        case IOT_MQTT_EVENT_PUBLISH_RECVEIVED:
            topic_msg = (iot_mqtt_topic_info_pt)msg->msg;
            mqtt_recv.type = QM_IOT_MQTTRECV_PUB;
            mqtt_recv.data.pub.qos = topic_msg->qos;
            mqtt_recv.data.pub.topic = topic_msg->ptopic;
            mqtt_recv.data.pub.topic_len = topic_msg->topic_len;
            mqtt_recv.data.pub.payload = (uint8_t*)topic_msg->payload;
            mqtt_recv.data.pub.payload_len = (uint32_t)topic_msg->payload_len;
            if(mqtt_ctx->recv_handler){
                mqtt_ctx->recv_handler((void*)mqtt_ctx, &mqtt_recv, mqtt_ctx->userdata);
            }
            break;

        case IOT_MQTT_EVENT_BUFFER_OVERFLOW:
            QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_CONTROL, "buffer overflow");
            break;

        default:
            QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_CONTROL, "Should NOT arrive here.");
            break;
    }
}

static int32_t qm_iot_mqtt_connect(void *handle)
{
    qm_err_t ret = QM_EOK;
    iot_mqtt_param_t mqtt_params = {0};
    qm_iot_mqtt_ctx_t *mqtt_ctx = (qm_iot_mqtt_ctx_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }

    if (NULL == (mqtt_ctx->msg_readbuf = (char *)qm_malloc(CONFIG_QM_IOT_MQTT_READ_MSGLEN))) {
        QM_CLOUD_LOGE(LOG_TAG,"not enough memory");
        ret = -QM_ENOMEM;
        goto __exit;
    }

    if (NULL == (mqtt_ctx->msg_writebuf = (char *)qm_malloc(CONFIG_QM_IOT_MQTT_WRITE_MSGLEN))) {
        QM_CLOUD_LOGE(LOG_TAG,"not enough memory");
        ret = -QM_ENOMEM;
        goto __exit;
    }

    /* Initialize MQTT parameter */
    memset(&mqtt_params, 0x0, sizeof(mqtt_params));

    mqtt_params.port = mqtt_ctx->mqtt_port;
    mqtt_params.client_id = mqtt_ctx->clientid;
    mqtt_params.host = mqtt_ctx->mqtt_host;
    mqtt_params.username = mqtt_ctx->username;
    mqtt_params.password = mqtt_ctx->password;

    mqtt_params.request_timeout_ms = 3000;
    mqtt_params.clean_session = mqtt_ctx->clean_session;
    mqtt_params.keepalive_interval_ms = mqtt_ctx->keep_alive_s * 1000;
    mqtt_params.pread_buf = mqtt_ctx->msg_readbuf;
    mqtt_params.read_buf_size = CONFIG_QM_IOT_MQTT_READ_MSGLEN;
    mqtt_params.pwrite_buf = mqtt_ctx->msg_writebuf;
    mqtt_params.write_buf_size = CONFIG_QM_IOT_MQTT_WRITE_MSGLEN;

    mqtt_params.server_crt = mqtt_ctx->server_crt;
    mqtt_params.client_crt = mqtt_ctx->client_crt;
    mqtt_params.client_key = mqtt_ctx->client_privkey;

    mqtt_params.handle_event.h_fp = event_handle;
    mqtt_params.handle_event.pcontext = handle;

    mqtt_ctx->mqtt_handle = iot_mqtt_client_construct(&mqtt_params);
    if(mqtt_ctx->mqtt_handle == NULL){
        ret = iot_mqtt_client_last_error();
        if (ret == QM_EOK) {
            ret = -QM_ERROR;
        }
        QM_CLOUD_LOGE(LOG_TAG, "iot_mqtt_client_construct failed ret=%d", ret);
        goto __exit;
    }
    return QM_EOK;

__exit:
    if (NULL != mqtt_ctx->msg_writebuf) {
        qm_free(mqtt_ctx->msg_writebuf);
        mqtt_ctx->msg_writebuf = NULL;
    }

    if (NULL != mqtt_ctx->msg_readbuf) {
        qm_free(mqtt_ctx->msg_readbuf);
        mqtt_ctx->msg_readbuf = NULL;
    }

    if (NULL != mqtt_ctx->mqtt_handle) {
        iot_mqtt_client_destroy(&mqtt_ctx->mqtt_handle);
        mqtt_ctx->mqtt_handle = NULL;
    }
    return ret;
}

static int retry_interval_ms_set(void *handle, int interval_ms, int interval_max_ms)
{
    qm_iot_mqtt_ctx_t *mqtt_ctx = (qm_iot_mqtt_ctx_t*)handle;
    mqtt_ctx->interval_ms = interval_ms;
    mqtt_ctx->interval_max_ms = interval_max_ms;
    return QM_EOK;
}

static int retry_interval_ms_get(void *handle)
{
    qm_iot_mqtt_ctx_t *mqtt_ctx = (qm_iot_mqtt_ctx_t*)handle;

    if (mqtt_ctx->interval_max_ms > mqtt_ctx->interval_ms) {
        mqtt_ctx->interval_ms *= 2;
    } else {
        mqtt_ctx->interval_ms = mqtt_ctx->interval_max_ms;
    }
    return mqtt_ctx->interval_ms;
}

int32_t qm_iot_mqtt_process(void *handle)
{
    int conn_retry = 1;
    qm_err_t ret = QM_EOK;
    qm_utils_time_t conn_timeout = {0};
    qm_iot_mqtt_event_t mqtt_event;
    char *payload = NULL;
    qm_list_iterator_t self = {0};
    qm_list_node_t *node = NULL;
    qm_iot_mqtt_pub_node_t *pub_node = NULL;

    qm_iot_mqtt_ctx_t *mqtt_ctx = (qm_iot_mqtt_ctx_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }

    retry_interval_ms_set((void*)mqtt_ctx, CONFIG_QM_IOT_MQTT_RETRY_INTERVAL_MIN_MS, CONFIG_QM_IOT_MQTT_RETRY_INTERVAL_MAX_MS);

    while(!mqtt_ctx->exit){

        if (mqtt_ctx->network_state == QM_IOT_NETWORK_DOWN) {
            (void)qm_sem_wait(&mqtt_ctx->network_wakeup, QM_WAIT_FOREVER);
            continue;
        }

        if(conn_retry || !qm_utils_time_left(&conn_timeout)){
            ret = qm_iot_mqtt_connect((void*)mqtt_ctx);
            if(ret == QM_EOK){
                break;
            }

            conn_retry = 0;
            QM_CLOUD_LOGE(LOG_TAG, "mqtt connect fail ret=%d", ret);
            qm_utils_time_countdown_ms(&conn_timeout, retry_interval_ms_get(mqtt_ctx));
        }

        if (mqtt_ctx->network_state == QM_IOT_NETWORK_DOWN && qm_sem_is_valid(&mqtt_ctx->network_wakeup)) {
            (void)qm_sem_wait(&mqtt_ctx->network_wakeup, QM_WAIT_FOREVER);
        } else {
            uint32_t wait_ms = qm_utils_time_left(&conn_timeout);
            if (wait_ms > 0U && qm_sem_is_valid(&mqtt_ctx->network_wakeup)) {
                (void)qm_sem_wait(&mqtt_ctx->network_wakeup, wait_ms);
            }
        }
    }

    if(mqtt_ctx->exit){
        goto cleanup;
    }

    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_LIFECYCLE, "mqtt connect success");

    if(mqtt_ctx->event_handler){
        mqtt_event.type = QM_IOT_MQTTEVT_CONNECT;
        mqtt_ctx->event_handler((void*)mqtt_ctx, &mqtt_event, mqtt_ctx->userdata);
    }

    while(!mqtt_ctx->exit){

        if (mqtt_ctx->network_state == QM_IOT_NETWORK_DOWN) {
            (void)qm_sem_wait(&mqtt_ctx->network_wakeup, QM_WAIT_FOREVER);
            continue;
        }

        qm_mutex_lock(&mqtt_ctx->lock, QM_WAIT_FOREVER);
        self.direction = LIST_HEAD;
        self.next = mqtt_ctx->pub_list->head;
        node = qm_list_iterator_next(&self);
        qm_mutex_unlock(&mqtt_ctx->lock);

        if(node){
            pub_node = (qm_iot_mqtt_pub_node_t *)qm_list_node_val_get(node);
            payload = pub_node->data + strlen(pub_node->data) + 1;
            ret = iot_mqtt_client_publish(mqtt_ctx->mqtt_handle, pub_node->data, (char*)payload, (int)pub_node->payload_len, (int)pub_node->qos, 0);
            if (ret < 0) {
                QM_CLOUD_LOGE(LOG_TAG, "error occur when publish topic_len=%lu payload_len=%lu",
                              (unsigned long)strlen(pub_node->data),
                              (unsigned long)pub_node->payload_len);
            }else{

                qm_mutex_lock(&mqtt_ctx->lock, QM_WAIT_FOREVER);
                qm_list_remove(mqtt_ctx->pub_list, node);
                qm_mutex_unlock(&mqtt_ctx->lock);

                qm_list_node_destroy(node);
            }
        }

        iot_mqtt_client_yield(mqtt_ctx->mqtt_handle, CONFIG_QM_IOT_MQTT_YIELD_TIME);
    }


cleanup:
    QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_LIFECYCLE, "mqtt handle destroy");

    // 按顺序清理资源
    if (NULL != mqtt_ctx->mqtt_handle) {
        iot_mqtt_client_destroy(&mqtt_ctx->mqtt_handle);
        mqtt_ctx->mqtt_handle = NULL;
    }

    if (NULL != mqtt_ctx->msg_readbuf) {
        qm_free(mqtt_ctx->msg_readbuf);
        mqtt_ctx->msg_readbuf = NULL;
    }

    if (NULL != mqtt_ctx->msg_writebuf) {
        qm_free(mqtt_ctx->msg_writebuf);
        mqtt_ctx->msg_writebuf = NULL;
    }

    if (NULL != mqtt_ctx->sub_list) {
        qm_list_destroy(mqtt_ctx->sub_list);
        mqtt_ctx->sub_list = NULL;
    }

    if (NULL != mqtt_ctx->pub_list) {
        qm_list_destroy(mqtt_ctx->pub_list);
        mqtt_ctx->pub_list = NULL;
    }

    if (qm_mutex_is_valid(&mqtt_ctx->lock)) {
        qm_mutex_free(&mqtt_ctx->lock);
    }

    if (qm_sem_is_valid(&mqtt_ctx->network_wakeup)) {
        qm_sem_free(&mqtt_ctx->network_wakeup);
    }

    if(NULL != mqtt_ctx){
        qm_free(mqtt_ctx);
        mqtt_ctx = NULL;
    }
    return QM_EOK;
}

int32_t qm_iot_mqtt_pub(void *handle, char *topic, uint8_t *payload, uint32_t payload_len, qm_iot_mqtt_qos_t qos)
{
    qm_err_t ret = QM_EOK;
    qm_iot_mqtt_ctx_t *mqtt_ctx = (qm_iot_mqtt_ctx_t*)handle;
    if(handle == NULL || topic == NULL || payload == NULL){
        return -QM_EINVAL;
    }
    if(mqtt_ctx->mqtt_handle == NULL) {
        QM_CLOUD_LOGE(LOG_TAG, "MQTT handle is NULL");
        return -QM_EINVAL;
    }
    if(mqtt_ctx->pub_list == NULL) {
        QM_CLOUD_LOGE(LOG_TAG, "MQTT pub_list is NULL");
        return -QM_EINVAL;
    }

    ret = iot_mqtt_client_publish(mqtt_ctx->mqtt_handle, topic, (char*)payload, (int)payload_len, (int)qos, 0);
    if (ret < 0) {
        QM_CLOUD_LOGE(LOG_TAG, "error occur when publish ret=%d topic_len=%lu payload_len=%lu",
                      ret, (unsigned long)strlen(topic), (unsigned long)payload_len);
    }

    return ret;
}

int32_t qm_iot_mqtt_pub_wait_ack(void *handle, char *topic, uint8_t *payload, uint32_t payload_len, qm_iot_mqtt_qos_t qos)
{
#if 0
    int rc = SUCCESS_RETURN;
    qm_iot_mqtt_ctx_t *mqtt_ctx = (qm_iot_mqtt_ctx_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }

    rc = iot_mqtt_client_publish_wait_ack(mqtt_ctx->mqtt_handle, topic, (char*)payload, (int)payload_len, (int)qos, 0);
    if(rc == ERROR_NET_TIMEOUT){
        return -QM_ETIMEOUT;
    }else{
        return QM_EOK;
    }
#endif
    //todo :暂不支持
    return -QM_EINVAL;
}

static void iot_mqtt_event_handler(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg)
{
    qm_iot_mqtt_recv_t mqtt_recv;
    iot_mc_client_pt mc_client = (iot_mc_client_pt)pclient;
    qm_iot_mqtt_recv_handler_t recv_handler = (qm_iot_mqtt_recv_handler_t)pcontext;
    uintptr_t packet_id;

    switch (msg->event_type) {
            case IOT_MQTT_EVENT_PUBLISH_SUCCESS:
            packet_id = (uintptr_t)msg->msg;
            QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_CONTROL, "publish success, packet-id=%u", (unsigned int)packet_id);
            mqtt_recv.type = QM_IOT_MQTTRECV_PUB_ACK;
            mqtt_recv.data.pub_ack.res = QM_IOT_MQTT_RES_OK;
            mqtt_recv.data.pub_ack.packet_id = (uint16_t)packet_id;
            if(recv_handler){
                recv_handler(mc_client->handle_event.pcontext, &mqtt_recv, NULL);
            }
            break;

        case IOT_MQTT_EVENT_PUBLISH_TIMEOUT:
            packet_id = (uintptr_t)msg->msg;
            QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_CONTROL, "publish timeout, packet-id=%u", (unsigned int)packet_id);
            mqtt_recv.type = QM_IOT_MQTTRECV_PUB_ACK;
            mqtt_recv.data.pub_ack.res = QM_IOT_MQTT_RES_PUB_TIMEOUT;
            mqtt_recv.data.pub_ack.packet_id = (uint16_t)packet_id;
            if(recv_handler){
                recv_handler(mc_client->handle_event.pcontext, &mqtt_recv, NULL);
            }
            break;
        default:
        break;
    }
}

int32_t qm_iot_mqtt_pub_with_callback(void *handle, char *topic, uint8_t *payload, uint32_t payload_len, qm_iot_mqtt_qos_t qos, qm_iot_mqtt_recv_handler_t handler)
{
    qm_err_t ret = QM_EOK;
    qm_iot_mqtt_ctx_t *mqtt_ctx = (qm_iot_mqtt_ctx_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }

    ret = iot_mqtt_client_publish_with_callback(mqtt_ctx->mqtt_handle, topic, (char*)payload, (int)payload_len, (int)qos, 0, iot_mqtt_event_handler, (void*)handler);
    if (ret < 0) {
        QM_CLOUD_LOGE(LOG_TAG, "error occur when publish");
        ret = -QM_ERROR;
    }
    return ret;
}

static void topic_handler(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg)
{
    qm_iot_mqtt_recv_t mqtt_recv;
    iot_mc_client_pt mc_client = (iot_mc_client_pt)pclient;
    iot_mqtt_topic_info_pt ptopic_info = (iot_mqtt_topic_info_pt) msg->msg;
    qm_iot_mqtt_recv_handler_t handler = (qm_iot_mqtt_recv_handler_t)pcontext;
    qm_iot_mqtt_ctx_t *mqtt_ctx = (qm_iot_mqtt_ctx_t*)mc_client->handle_event.pcontext;

    mqtt_recv.type = QM_IOT_MQTTRECV_PUB;
    mqtt_recv.data.pub.qos = ptopic_info->qos;
    mqtt_recv.data.pub.topic = ptopic_info->ptopic;
    mqtt_recv.data.pub.topic_len = ptopic_info->topic_len;
    mqtt_recv.data.pub.payload = (uint8_t*)ptopic_info->payload;
    mqtt_recv.data.pub.payload_len = (uint32_t)ptopic_info->payload_len;
    if(handler){
        handler((void*)mqtt_ctx, &mqtt_recv, mqtt_ctx->userdata);
    }
}

int32_t qm_iot_mqtt_sub(void *handle, char *topic, qm_iot_mqtt_recv_handler_t handler, qm_iot_mqtt_qos_t qos)
{
    int topic_len = 0;
    qm_iot_mqtt_ctx_t *mqtt_ctx = (qm_iot_mqtt_ctx_t*)handle;
    if(handle == NULL || topic == NULL || handler == NULL){
        return -QM_EINVAL;
    }

    if(mqtt_ctx->mqtt_handle == NULL) {
        return -QM_EINVAL;
    }

    // 验证topic长度
    topic_len = strlen(topic);
    if(topic_len <= 0) {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_CONTROL, "ignore empty sub topic");
        return -QM_EINVAL;
    }
    return iot_mqtt_client_subscribe(mqtt_ctx->mqtt_handle, topic, (iot_mqtt_qos_t)qos, topic_handler, (void*)handler);
}

int32_t qm_iot_mqtt_auto_sub(void *handle, char *topic, qm_iot_mqtt_recv_handler_t handler)
{
    qm_iot_mqtt_ctx_t *mqtt_ctx = (qm_iot_mqtt_ctx_t*)handle;
    if(handle == NULL || topic == NULL || handler == NULL){
        return -QM_EINVAL;
    }
    if (topic[0] == '\0') {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_CONTROL, "ignore empty auto-sub topic");
        return -QM_EINVAL;
    }
    return iot_mqtt_client_auto_subscribe(mqtt_ctx->mqtt_handle, topic, topic_handler, (void*)handler);
}

int32_t qm_iot_mqtt_auto_unsub(void *handle, char *topic)
{
    qm_iot_mqtt_ctx_t *mqtt_ctx = (qm_iot_mqtt_ctx_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }

    return iot_mqtt_client_auto_unsubscribe(mqtt_ctx->mqtt_handle, topic);
}

int32_t qm_iot_mqtt_sub_wait_ack(void *handle, char *topic, qm_iot_mqtt_recv_handler_t handler, qm_iot_mqtt_qos_t qos)
{
    int rc = SUCCESS_RETURN;
    qm_iot_mqtt_ctx_t *mqtt_ctx = (qm_iot_mqtt_ctx_t*)handle;
    if(handle == NULL || topic == NULL || handler == NULL){
        return -QM_EINVAL;
    }
    if (topic[0] == '\0') {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_CONTROL, "ignore empty sub-wait-ack topic");
        return -QM_EINVAL;
    }
    rc = iot_mqtt_client_subscribe_wait_ack(mqtt_ctx->mqtt_handle, topic, (iot_mqtt_qos_t)qos, topic_handler, (void*)handler);
    if(rc == ERROR_NET_TIMEOUT){
        return -QM_ETIMEOUT;
    }else{
        return QM_EOK;
    }
}

int32_t qm_iot_mqtt_unsub(void *handle, char *topic)
{
    qm_iot_mqtt_ctx_t *mqtt_ctx = (qm_iot_mqtt_ctx_t*)handle;
    if(handle == NULL || topic == NULL){
        return -QM_EINVAL;
    }
    if (topic[0] == '\0') {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_CONTROL, "ignore empty unsub topic");
        return -QM_EINVAL;
    }
    return iot_mqtt_client_unsubscribe(mqtt_ctx->mqtt_handle, topic);
}

int32_t qm_iot_mqtt_deinit(void **handle)
{
    if(handle == NULL || *handle == NULL){
        return -QM_EINVAL;
    }

    qm_iot_mqtt_ctx_t *mqtt_ctx = (qm_iot_mqtt_ctx_t*)(*handle);

    // 设置退出标志
    mqtt_ctx->exit = 1;
    mqtt_wakeup(mqtt_ctx);

    *handle = NULL;

    return QM_EOK;
}

int32_t qm_iot_mqtt_network_update(void *handle, qm_iot_network_state_t state)
{
    qm_iot_mqtt_ctx_t *mqtt_ctx = (qm_iot_mqtt_ctx_t *)handle;

    if (mqtt_ctx == NULL) {
        return -QM_EINVAL;
    }
    if (state != QM_IOT_NETWORK_DOWN && state != QM_IOT_NETWORK_UP) {
        return -QM_EINVAL;
    }
    if (mqtt_ctx->network_state == state) {
        return QM_EOK;
    }
    mqtt_ctx->network_state = state;
    mqtt_wakeup(mqtt_ctx);
    return QM_EOK;
}

int32_t qm_iot_mqtt_pre_sub(void *handle, char *topic, qm_iot_mqtt_recv_handler_t handler, qm_iot_mqtt_qos_t qos, void *userdata)
{
    int len = 0;
    qm_list_node_t *node = NULL;
    qm_iot_mqtt_sub_info_t *sub_info = NULL;

    qm_iot_mqtt_ctx_t *mqtt_ctx = (qm_iot_mqtt_ctx_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }
    if (topic == NULL || topic[0] == '\0') {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_CONTROL, "ignore empty pre-sub topic");
        return -QM_EINVAL;
    }
    len = sizeof(qm_iot_mqtt_sub_info_t) + strlen(topic) + 1;
    node = qm_list_node_extra_new(len);
    if(node == NULL){
        return -QM_ENOMEM;
    }
    sub_info = (qm_iot_mqtt_sub_info_t*)qm_list_node_val_get(node);

    sub_info->topic = (char*)sub_info + sizeof(qm_iot_mqtt_sub_info_t);
    memset(sub_info->topic, 0, strlen(topic) + 1);
    memcpy(sub_info->topic, topic, strlen(topic));
    sub_info->handler = handler;
    sub_info->qos = qos;
    sub_info->userdata = userdata;
    qm_mutex_lock(&mqtt_ctx->lock, QM_WAIT_FOREVER);
    qm_list_lpush(&mqtt_ctx->pre_sub_list, node);
    qm_mutex_unlock(&mqtt_ctx->lock);

    return QM_EOK;
}

static void pre_topic_handler(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg)
{
    qm_iot_mqtt_recv_t mqtt_recv;
    iot_mc_client_pt mc_client = (iot_mc_client_pt)pclient;
    iot_mqtt_topic_info_pt ptopic_info = (iot_mqtt_topic_info_pt) msg->msg;
    qm_iot_mqtt_sub_info_t *sub_info = (qm_iot_mqtt_sub_info_t*)pcontext;
    qm_iot_mqtt_ctx_t *mqtt_ctx = (qm_iot_mqtt_ctx_t*)mc_client->handle_event.pcontext;

    mqtt_recv.type = QM_IOT_MQTTRECV_PUB;
    mqtt_recv.data.pub.qos = ptopic_info->qos;
    mqtt_recv.data.pub.topic = ptopic_info->ptopic;
    mqtt_recv.data.pub.topic_len = ptopic_info->topic_len;
    mqtt_recv.data.pub.payload = (uint8_t*)ptopic_info->payload;
    mqtt_recv.data.pub.payload_len = (uint32_t)ptopic_info->payload_len;
    if(sub_info && sub_info->handler){
        sub_info->handler((void*)mqtt_ctx, &mqtt_recv, sub_info->userdata);
    }
}

static int sub_topic_info_move(qm_list_t *sub_list, qm_list_t *pre_sub_list, int count)
{
    qm_list_node_t *node = NULL;
    while(count--){
        node = qm_list_rpop(pre_sub_list);
        qm_list_lpush(sub_list, node);
    }
    return QM_EOK;
}

static int sub_topic_info_reset(void *handle)
{
    qm_list_node_t *node = NULL;
    qm_iot_mqtt_ctx_t *mqtt_ctx = (qm_iot_mqtt_ctx_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }
    qm_mutex_lock(&mqtt_ctx->lock, QM_WAIT_FOREVER);
    while(1){
        node = qm_list_rpop(mqtt_ctx->sub_list);
        if(node == NULL){
            break;
        }
        qm_list_lpush(&mqtt_ctx->pre_sub_list, node);
    }
    qm_mutex_unlock(&mqtt_ctx->lock);
    sub_topic_handler_reset(mqtt_ctx->mqtt_handle);
    return QM_EOK;
}

static void sub_topic_handler_reset(void *handle)
{
    iot_mc_client_t *mqtt_handle = (iot_mc_client_t *)handle;

    if (mqtt_handle == NULL) {
        return;
    }

    qm_mutex_lock(&mqtt_handle->lock_generic, QM_WAIT_FOREVER);
    memset(mqtt_handle->sub_handle, 0, sizeof(mqtt_handle->sub_handle));
    qm_mutex_unlock(&mqtt_handle->lock_generic);
}

static void sub_topic_info_remove_by_topic(qm_list_t *list, const char *topic)
{
    qm_list_iterator_t self = {0};
    qm_list_node_t *node = NULL;

    if (list == NULL || topic == NULL || topic[0] == '\0') {
        return;
    }

    qm_list_iterator_init(list, LIST_HEAD, &self);
    node = qm_list_iterator_next(&self);
    while (node != NULL) {
        qm_iot_mqtt_sub_info_t *sub_info = (qm_iot_mqtt_sub_info_t*)qm_list_node_val_get(node);
        qm_list_node_t *next = self.next;

        if (sub_info != NULL &&
            sub_info->topic != NULL &&
            strlen(sub_info->topic) == strlen(topic) &&
            strcmp(sub_info->topic, topic) == 0) {
            qm_list_remove(list, node);
            qm_list_node_destroy(node);
        }

        node = next;
    }
}

int32_t qm_iot_mqtt_pre_sub_start(void *handle)
{
    int ret = 0;
    int retry = CONFIG_QM_IOT_MQTT_TOPIC_SUB_RETRY_COUNT;
    qm_list_iterator_t self;
    int req_count = 0, every_count = 0, remain_count = 0;
    int count = 0, i = 0, j = 0, sum_count = 0;
    qm_list_node_t *node = NULL;
    iot_mqtt_topic_t *m_topic_list = NULL;
    iot_mqtt_topic_t *topic_list = NULL;
    qm_iot_mqtt_sub_info_t *sub_info = NULL;
    qm_iot_mqtt_ctx_t *mqtt_ctx = (qm_iot_mqtt_ctx_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }

    sum_count = qm_list_len_get(&mqtt_ctx->pre_sub_list);
    if(!sum_count){
        return -QM_EEMPTY;
    }

    if(sum_count % CONFIG_QM_IOT_MQTT_TOPIC_SUB_MAX_COUNT){
        req_count = sum_count / CONFIG_QM_IOT_MQTT_TOPIC_SUB_MAX_COUNT + 1;
    }else{
        req_count = sum_count / CONFIG_QM_IOT_MQTT_TOPIC_SUB_MAX_COUNT;
    }

    if(req_count == 1){
        count = sum_count;
    }else{
        count = CONFIG_QM_IOT_MQTT_TOPIC_SUB_MAX_COUNT;
    }

    topic_list = (iot_mqtt_topic_t*)qm_malloc(sizeof(iot_mqtt_topic_t) * count);
    if(topic_list == NULL){
        return -QM_ENOMEM;
    }
    every_count = count;
    remain_count = sum_count;
    for(j = 0; j < req_count; j++){
        retry = CONFIG_QM_IOT_MQTT_TOPIC_SUB_RETRY_COUNT;
        self.next = mqtt_ctx->pre_sub_list.tail;
        self.direction = LIST_TAIL;
        qm_mutex_lock(&mqtt_ctx->lock, QM_WAIT_FOREVER);
        node = qm_list_iterator_next(&self);
         for(i = 0; i < every_count; i++){
            m_topic_list = topic_list + i;
            sub_info = (qm_iot_mqtt_sub_info_t*)qm_list_node_val_get(node);
            m_topic_list->qos = (iot_mqtt_qos_t)sub_info->qos;
            m_topic_list->topic_filter = sub_info->topic;
            m_topic_list->topic_handle_func = pre_topic_handler;
            m_topic_list->pcontext = sub_info;
            node = qm_list_iterator_next(&self);
        }
        qm_mutex_unlock(&mqtt_ctx->lock);
        while (retry--)
        {
            ret = iot_mqtt_client_multi_subscribe_wait_ack(mqtt_ctx->mqtt_handle, topic_list, every_count);
            if(ret != SUCCESS_RETURN){
                QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_CONTROL, "mqtt sub wait ack error:%d", ret);
                ret = -QM_ETIMEOUT;
            }else{
                qm_mutex_lock(&mqtt_ctx->lock, QM_WAIT_FOREVER);
                sub_topic_info_move(mqtt_ctx->sub_list, &mqtt_ctx->pre_sub_list, every_count);
                qm_mutex_unlock(&mqtt_ctx->lock);
                ret = QM_EOK;
                break;
            }
        }

        if(ret != QM_EOK) {
            break;
        }

        m_topic_list = NULL;
        remain_count = remain_count - every_count;
        if(remain_count >= CONFIG_QM_IOT_MQTT_TOPIC_SUB_MAX_COUNT){
            every_count = CONFIG_QM_IOT_MQTT_TOPIC_SUB_MAX_COUNT;
        }else{
            every_count = remain_count;
        }
    }
    qm_free(topic_list);
    topic_list = NULL;
    return ret;
}

static int topic_match(void *sub_topic, void *topic)
{
    return QM_EOK;
}

int32_t qm_iot_mqtt_pre_unsub(void *handle, char *topic)
{
    qm_iot_mqtt_ctx_t *mqtt_ctx = (qm_iot_mqtt_ctx_t*)handle;

    if (handle == NULL || topic == NULL) {
        return -QM_EINVAL;
    }
    if (topic[0] == '\0') {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_CONTROL, "ignore empty pre-unsub topic");
        return -QM_EINVAL;
    }

    qm_mutex_lock(&mqtt_ctx->lock, QM_WAIT_FOREVER);
    sub_topic_info_remove_by_topic(&mqtt_ctx->pre_sub_list, topic);
    sub_topic_info_remove_by_topic(mqtt_ctx->sub_list, topic);

    if (mqtt_ctx->mqtt_handle != NULL) {
        (void)iot_mqtt_client_auto_unsubscribe(mqtt_ctx->mqtt_handle, topic);
    }

    qm_mutex_unlock(&mqtt_ctx->lock);
    return QM_EOK;
}
