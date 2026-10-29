#include "qm_cloud_log.h"
#include "qm_iot_ntp.h"
#include "qm_iot_api.h"
#include "qm_kernel.h"
#include "qm_errno.h"
#include "qm_iot_mqtt.h"
#include "qm_log.h"
#include "json_parser.h"
#include "qm_utils_string.h"
#include "qm_string.h"
#include "qm_time.h"
#include "qm_iot_config.h"

#define LOG_TAG "ntp"

#define QM_IOT_NTP_METHOD_REQUEST                   "timeSync"
#define QM_IOT_NTP_METHOD_REPLY                     "timeSyncReply"
#define QM_IOT_NTP_REQUEST_PARAMS_MAX_LEN           64
#define QM_IOT_NTP_REQUEST_PARAMS_FMT               "{\"deviceSendTime\":%u}"

#define DEVICE_PARAMS_KEY              "params"
#define DEVICE_SEND_TIME_KEY           "deviceSendTime"
#define SERVER_RECV_TIME_KEY           "serverRecvTime"
#define SERVER_SEND_TIME_KEY           "serverSendTime"

#define QM_IOT_NTP_MSG_ID_MAX          (65535)

typedef struct {
    uint16_t msg_id;
    int8_t time_zone;
    /** IOT core 句柄，由 @ref QM_IOT_NTPOPT_IOT_HANDLE 设置为 @ref qm_iot_init 返回值。 */
    void *iot_handle;
    /** 用户回调上下文，由 @ref QM_IOT_NTPOPT_USERDATA 设置。 */
    void *userdata;
    /** 当前是否已在 device/down 注册 timeSyncReply handler。 */
    uint8_t down_registered;
    /** 记录当前注册所绑定的 IOT 句柄，便于切换/销毁时清理。 */
    void *registered_iot_handle;
    qm_iot_ntp_recv_handler_t recv_handler;
} ntp_handle_t;

static void ntp_down_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata);

/**
 * @brief 从 ntp 会话解析 IOT core 句柄。
 */
static void *ntp_iot_handle(ntp_handle_t *ntp)
{
    if (ntp == NULL) {
        return NULL;
    }

    return ntp->iot_handle;
}

/**
 * @brief 通过 device/down 注册 timeSyncReply method 回调。
 */
static void ntp_down_register_if_ready(ntp_handle_t *ntp)
{
    void *iot_handle = NULL;
    int ret;

    if (ntp == NULL) {
        return;
    }

    iot_handle = ntp_iot_handle(ntp);
    if (iot_handle == NULL) {
        return;
    }

    if (ntp->down_registered && ntp->registered_iot_handle == iot_handle) {
        return;
    }

    ret = qm_iot_down_register(iot_handle,
                               (char *)QM_IOT_NTP_METHOD_REPLY,
                               ntp_down_recv_handler,
                               ntp);
    if (ret != QM_EOK) {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_SERVICE, "register ntp down handler failed ret=%d", ret);
        return;
    }

    ntp->down_registered = QM_TRUE;
    ntp->registered_iot_handle = iot_handle;
}

/**
 * @brief 清理 device/down 上已注册的 timeSyncReply method 回调。
 */
static void ntp_down_unregister(ntp_handle_t *ntp)
{
    int ret;

    if (ntp == NULL || !ntp->down_registered || ntp->registered_iot_handle == NULL) {
        return;
    }

    ret = qm_iot_down_unregister(ntp->registered_iot_handle,
                                 (char *)QM_IOT_NTP_METHOD_REPLY);
    if (ret != QM_EOK) {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_SERVICE, "unregister ntp down handler failed ret=%d", ret);
    }

    ntp->down_registered = QM_FALSE;
    ntp->registered_iot_handle = NULL;
}

void *qm_iot_ntp_init(void)
{
    ntp_handle_t *ntp_handle = NULL;

    ntp_handle = (ntp_handle_t*)qm_malloc(sizeof(ntp_handle_t));
    if (ntp_handle == NULL) {
        return NULL;
    }
    memset(ntp_handle, 0, sizeof(ntp_handle_t));
    return ntp_handle;
}

static void ntp_down_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    int type = 0;
    char *params = NULL;
    int params_len = 0;

    char *value = NULL;
    int value_len = 0;
    struct qm_tm tm = {0};
    qm_time_t time = 0;
    qm_time_t pre_time = 0;
    qm_iot_ntp_recv_t ntp_recv = {0};
    ntp_handle_t *ntp_handle = (ntp_handle_t*)userdata; 
    uint64_t device_send_time = 0, server_send_time = 0, server_recv_time = 0;

    QM_UNUSED_ARG(handle);

    if (ntp_handle == NULL) {
        return;
    }

    if(packet->type != QM_IOT_MQTTRECV_PUB){
        return;
    }

    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "ntp payload_len=%lu",
                  (unsigned long)packet->data.pub.payload_len);

    params = qm_json_get_value_by_name((char*)packet->data.pub.payload, (int)packet->data.pub.payload_len, DEVICE_PARAMS_KEY, &params_len, &type);
    if(params == NULL || type != JOBJECT){
        return;
    }

    value = qm_json_get_value_by_name(params, params_len, DEVICE_SEND_TIME_KEY, &value_len, &type);
    if(value == NULL || type != JNUMBER){
        return;
    }
    qm_str2uint64(value, value_len, &device_send_time);

    value = qm_json_get_value_by_name(params, params_len, SERVER_RECV_TIME_KEY, &value_len, &type);
    if(value == NULL || type != JNUMBER){
        return;
    }
    qm_str2uint64(value, value_len, &server_recv_time);

    value = qm_json_get_value_by_name(params, params_len, SERVER_SEND_TIME_KEY, &value_len, &type);
    if(value == NULL || type != JNUMBER){
        return;
    }
    qm_str2uint64(value, value_len, &server_send_time);

    pre_time = ((server_send_time + server_recv_time + qm_now_ms() - device_send_time) / 2 / 1000);

    time = pre_time + ntp_handle->time_zone * 3600;
    
    qm_gmtime_r(&time, &tm);
    
    ntp_recv.type = QM_IOT_NTPRECV_LOCAL_TIME;
    ntp_recv.data.local_time.timestamp = (uint64_t)pre_time;
    ntp_recv.data.local_time.year = tm.tm_year + 1900;
    ntp_recv.data.local_time.mon = tm.tm_mon + 1;
    ntp_recv.data.local_time.day = tm.tm_mday;
    ntp_recv.data.local_time.hour = tm.tm_hour;
    ntp_recv.data.local_time.min = tm.tm_min;
    ntp_recv.data.local_time.sec = tm.tm_sec;
    ntp_recv.data.local_time.msec = (uint16_t)(time % 1000);

    if(ntp_handle->recv_handler){
        ntp_handle->recv_handler(ntp_handle, &ntp_recv, ntp_handle->userdata);
    }
}

int32_t qm_iot_ntp_setopt(void *handle, qm_iot_ntp_option_t option, void *data)
{
    qm_err_t ret = QM_EOK;
    ntp_handle_t *ntp_handle = (ntp_handle_t*)handle; 
    if(handle == NULL){
        return -QM_EINVAL;
    }
    switch(option){

        case QM_IOT_NTPOPT_IOT_HANDLE:
            if (ntp_handle->iot_handle != data) {
                ntp_down_unregister(ntp_handle);
            }
            ntp_handle->iot_handle = data;
            ntp_down_register_if_ready(ntp_handle);
        break;
        case QM_IOT_NTPOPT_TIME_ZONE:
            ntp_handle->time_zone = *((int8_t*)data);

        break;
        case QM_IOT_NTPOPT_RECV_HANDLER:
            ntp_handle->recv_handler = (qm_iot_ntp_recv_handler_t)data;

        break;
        case QM_IOT_NTPOPT_USERDATA:
            ntp_handle->userdata = data;
        break;
        default:
            ret = -QM_EINVAL;
        break;
    }
    return ret;
}

int32_t qm_iot_ntp_deinit(void **handle)
{
    ntp_handle_t *ntp_handle = (ntp_handle_t*)(*handle); 
    if(*handle == NULL){
        return -QM_EINVAL;
    }

    ntp_down_unregister(ntp_handle);

    qm_free(ntp_handle);
    *handle = NULL;

    return QM_EOK;
}

static int qm_iot_ntp_msg_id_get(void *handle)
{
    ntp_handle_t *ntp_handle = (ntp_handle_t*)handle; 
    if(handle == NULL){
        return -QM_EINVAL;
    }
    ntp_handle->msg_id = (ntp_handle->msg_id == QM_IOT_NTP_MSG_ID_MAX) ? 1 : ntp_handle->msg_id + 1;
    return ntp_handle->msg_id;
}

int32_t qm_iot_ntp_request(void *handle)
{
    char *params = NULL;
    int ret;
    uint32_t msg_id;
    ntp_handle_t *ntp_handle = (ntp_handle_t*)handle; 
    void *iot_handle = NULL;

    if(handle == NULL){
        return -QM_EINVAL;
    }

    iot_handle = ntp_iot_handle(ntp_handle);
    if (iot_handle == NULL) {
        return -QM_EINVAL;
    }

    params = (char*)qm_malloc(QM_IOT_NTP_REQUEST_PARAMS_MAX_LEN);
    if(params == NULL){
        return -QM_ENOMEM;
    }
    memset(params, 0, QM_IOT_NTP_REQUEST_PARAMS_MAX_LEN);

    ntp_down_register_if_ready(ntp_handle);

    msg_id = (uint32_t)qm_iot_ntp_msg_id_get(handle);
    qm_snprintf(params,
                QM_IOT_NTP_REQUEST_PARAMS_MAX_LEN,
                QM_IOT_NTP_REQUEST_PARAMS_FMT,
                qm_now_ms());

    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_SERVICE, "ntp request method=%s id=%u params_len=%lu",
            QM_IOT_NTP_METHOD_REQUEST, (unsigned int)msg_id, (unsigned long)strlen(params));
    ret = qm_iot_up_report(iot_handle,
                           (char *)QM_IOT_NTP_METHOD_REQUEST,
                           msg_id,
                           params,
                           (int)strlen(params));

    qm_free(params);
    params = NULL;

    return ret;
}
