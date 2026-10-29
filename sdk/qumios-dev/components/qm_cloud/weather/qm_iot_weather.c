#include "qm_cloud_log.h"
#include "qm_iot_weather.h"
#include "qm_iot_api.h"
#include "qm_kernel.h"
#include "qm_errno.h"
#include "qm_iot_mqtt.h"
#include "qm_log.h"
#include "cJSON.h"
#include "qm_string.h"
#include "qm_iot_config.h"
#include <limits.h>

#define LOG_TAG "weather"

#define QM_IOT_WEATHER_TOPIC_MAX_LEN                    48              
#define QM_IOT_WEATHER_METHOD_REQUEST                   "weather"
#define QM_IOT_WEATHER_METHOD_REPLY                     "weatherReply"
#define QM_IOT_WEATHER_REQUEST_TOPIC_FMT                QM_IOT_DEV_UP_TOPIC_FMT
#define QM_IOT_WEATHER_REQUEST_PAYLOAD_MAX_LEN          64
#define QM_IOT_WEATHER_REQUEST_PAYLOAD_FMT              "{\"method\":\"" QM_IOT_WEATHER_METHOD_REQUEST "\",\"id\":%u,\"timestamp\":%u}"

#define DEVICE_PARAMS_KEY                        "params"
#define QM_IOT_WEATHER_CONDITION_KEY             "condition"
#define QM_IOT_WEATHER_ICON_KEY                  "icon"
#define QM_IOT_WEATHER_CITY_KEY                  "city"
#define QM_IOT_WEATHER_TEMP_KEY                  "temp"
#define QM_IOT_WEATHER_HUMI_KEY                  "humidity"
#define QM_IOT_WEATHER_SUNSET_KEY                "sunSet"
#define QM_IOT_WEATHER_SUNRISE_KEY               "sunRise"
#define QM_IOT_WEATHER_CONTRY_NAME_KEY           "counname"
#define QM_IOT_WEATHER_PNAME_KEY                 "pname"
#define QM_IOT_WEATHER_SECONDARY_NAME_KEY        "secondaryname"
#define QM_IOT_WEATHER_NAME_KEY                  "name"
#define QM_IOT_WEATHER_CITY_ID_KEY               "cityId"


#define QM_IOT_WEATHER_MSG_ID_MAX          (65535)

typedef struct {
    uint16_t msg_id;
    void *mqtt_handle;
    /** IOT core 句柄，由 @ref QM_IOT_WEATHEROPT_USERDATA 设置为 @ref qm_iot_init 返回值。 */
    void *userdata;
    /** 当前是否已在 device/down 注册 weatherReply handler。 */
    uint8_t down_registered;
    /** 记录当前注册所绑定的 IOT 句柄，便于切换/销毁时清理。 */
    void *registered_iot_handle;
    qm_iot_weather_recv_handler_t recv_handler;
} weather_handle_t;

static void weather_down_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata);

/**
 * @brief 从 weather 会话解析 IOT core 句柄。
 */
static void *weather_iot_handle(weather_handle_t *weather)
{
    if (weather == NULL) {
        return NULL;
    }

    return weather->userdata;
}

/**
 * @brief 通过 device/down 注册 weatherReply method 回调。
 */
static int weather_down_register_if_ready(weather_handle_t *weather)
{
    void *iot_handle = NULL;
    int ret;

    if (weather == NULL) {
        return -QM_EINVAL;
    }

    iot_handle = weather_iot_handle(weather);
    if (iot_handle == NULL) {
        return -QM_EINVAL;
    }

    if (weather->down_registered && weather->registered_iot_handle == iot_handle) {
        return QM_EOK;
    }

    ret = qm_iot_down_register(iot_handle,
                               (char *)QM_IOT_WEATHER_METHOD_REPLY,
                               weather_down_recv_handler,
                               weather);
    if (ret != QM_EOK) {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_SERVICE, "register weather down handler failed ret=%d", ret);
        return ret;
    }

    weather->down_registered = QM_TRUE;
    weather->registered_iot_handle = iot_handle;
    return QM_EOK;
}

/**
 * @brief 清理 device/down 上已注册的 weatherReply method 回调。
 */
static int weather_down_unregister(weather_handle_t *weather)
{
    int ret;

    if (weather == NULL || !weather->down_registered || weather->registered_iot_handle == NULL) {
        return QM_EOK;
    }

    ret = qm_iot_down_unregister(weather->registered_iot_handle,
                                 (char *)QM_IOT_WEATHER_METHOD_REPLY);
    if (ret != QM_EOK) {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_SERVICE, "unregister weather down handler failed ret=%d", ret);
        return ret;
    }

    weather->down_registered = QM_FALSE;
    weather->registered_iot_handle = NULL;
    return QM_EOK;
}

void *qm_iot_weather_init(void)
{
    weather_handle_t *weather_handle = NULL;

    weather_handle = (weather_handle_t*)qm_malloc(sizeof(weather_handle_t));
    if (weather_handle == NULL) {
        return NULL;
    }
    memset(weather_handle, 0, sizeof(weather_handle_t));
    return weather_handle;
}

static int weather_item_to_int(const cJSON *item, int *out)
{
    uint32_t parsed = 0;
    const char *text;
    size_t text_len;
    size_t offset = 0;
    bool_t negative = QM_FALSE;

    if (item == NULL || out == NULL) {
        return -QM_EINVAL;
    }

    if (cJSON_IsNumber(item)) {
        if (item->valuedouble != item->valuedouble ||
            item->valuedouble < (double)INT_MIN ||
            item->valuedouble > (double)INT_MAX) {
            return -QM_EINVAL;
        }
        *out = (int)item->valuedouble;
        return QM_EOK;
    }

    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return -QM_EINVAL;
    }

    text = item->valuestring;
    text_len = strlen(text);
    if (text_len == 0U) {
        return -QM_EINVAL;
    }
    if (text[0] == '-') {
        negative = QM_TRUE;
        offset = 1U;
        if (text_len == 1U) {
            return -QM_EINVAL;
        }
    }

    if (text_len - offset > 255U ||
        qm_str2uint((char *)text + offset,
                    (uint8_t)(text_len - offset),
                    &parsed) != QM_EOK) {
        return -QM_EINVAL;
    }

    if ((!negative && parsed > (uint32_t)INT_MAX) ||
        (negative && parsed > ((uint32_t)INT_MAX + 1U))) {
        return -QM_EINVAL;
    }
    if (negative && parsed == ((uint32_t)INT_MAX + 1U)) {
        *out = INT_MIN;
    } else {
        *out = negative ? -(int)parsed : (int)parsed;
    }
    return QM_EOK;
}

static void weather_item_set_string(const cJSON *item, char **value, int *value_len)
{
    if (value == NULL || value_len == NULL || !cJSON_IsString(item) || item->valuestring == NULL) {
        return;
    }

    *value = item->valuestring;
    *value_len = (int)strlen(item->valuestring);
}

static void weather_down_recv_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    cJSON *root = NULL;
    cJSON *params = NULL;
    cJSON *condition = NULL;
    cJSON *city = NULL;
    cJSON *item = NULL;
    int weather_type = 0;
    int response_id = 0;
    qm_iot_weather_recv_t weather_recv = {0};
    weather_handle_t *weather_handle = (weather_handle_t *)userdata;

    QM_UNUSED_ARG(handle);
    if (weather_handle == NULL || packet == NULL || packet->type != QM_IOT_MQTTRECV_PUB) {
        return;
    }

    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "weather payload_len=%lu",
                  (unsigned long)packet->data.pub.payload_len);

    root = cJSON_ParseWithLength((const char *)packet->data.pub.payload,
                                 packet->data.pub.payload_len);
    if (root == NULL) {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_SERVICE, "weather payload json parse failed");
        return;
    }

    params = cJSON_GetObjectItemCaseSensitive(root, DEVICE_PARAMS_KEY);
    if (!cJSON_IsObject(params)) {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_SERVICE, "weather params missing");
        goto out;
    }

    /* The response ID is required so a late reply cannot complete a newer request. */
    item = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (item == NULL ||
        weather_item_to_int(item, &response_id) != QM_EOK ||
        response_id <= 0 || response_id > QM_IOT_WEATHER_MSG_ID_MAX ||
        (uint16_t)response_id != weather_handle->msg_id) {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_SERVICE, "weather response id missing or mismatched");
        goto out;
    }

    condition = cJSON_GetObjectItemCaseSensitive(params, QM_IOT_WEATHER_CONDITION_KEY);
    if (!cJSON_IsObject(condition)) {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_SERVICE, "weather condition missing");
        goto out;
    }

    item = cJSON_GetObjectItemCaseSensitive(condition, QM_IOT_WEATHER_ICON_KEY);
    if (weather_item_to_int(item, &weather_type) != QM_EOK) {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_SERVICE, "weather icon invalid");
        goto out;
    }
    weather_recv.weather_type = (qm_iot_weather_type_t)weather_type;
    weather_recv.msg_id = (uint16_t)response_id;

    item = cJSON_GetObjectItemCaseSensitive(condition, QM_IOT_WEATHER_TEMP_KEY);
    if (weather_item_to_int(item, &weather_recv.temp) != QM_EOK) {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_SERVICE, "weather temp invalid");
        goto out;
    }

    item = cJSON_GetObjectItemCaseSensitive(condition, QM_IOT_WEATHER_HUMI_KEY);
    if (item != NULL) {
        (void)weather_item_to_int(item, &weather_recv.humidity);
    }

    item = cJSON_GetObjectItemCaseSensitive(condition, QM_IOT_WEATHER_CONDITION_KEY);
    weather_item_set_string(item, &weather_recv.condition, &weather_recv.condition_len);
    item = cJSON_GetObjectItemCaseSensitive(condition, QM_IOT_WEATHER_SUNSET_KEY);
    weather_item_set_string(item, &weather_recv.sunset, &weather_recv.sunset_len);
    item = cJSON_GetObjectItemCaseSensitive(condition, QM_IOT_WEATHER_SUNRISE_KEY);
    weather_item_set_string(item, &weather_recv.sunrise, &weather_recv.sunrise_len);

    city = cJSON_GetObjectItemCaseSensitive(params, QM_IOT_WEATHER_CITY_KEY);
    if (cJSON_IsObject(city)) {
        item = cJSON_GetObjectItemCaseSensitive(city, QM_IOT_WEATHER_CONTRY_NAME_KEY);
        weather_item_set_string(item, &weather_recv.country_name, &weather_recv.country_name_len);
        item = cJSON_GetObjectItemCaseSensitive(city, QM_IOT_WEATHER_PNAME_KEY);
        weather_item_set_string(item, &weather_recv.pname, &weather_recv.pname_len);
        item = cJSON_GetObjectItemCaseSensitive(city, QM_IOT_WEATHER_SECONDARY_NAME_KEY);
        weather_item_set_string(item, &weather_recv.secondaryname, &weather_recv.secondaryname_len);
        item = cJSON_GetObjectItemCaseSensitive(city, QM_IOT_WEATHER_NAME_KEY);
        weather_item_set_string(item, &weather_recv.name, &weather_recv.name_len);
        item = cJSON_GetObjectItemCaseSensitive(city, QM_IOT_WEATHER_CITY_ID_KEY);
        (void)weather_item_to_int(item, &weather_recv.city_id);
    }

    if (weather_handle->recv_handler != NULL) {
        weather_handle->recv_handler(weather_handle, &weather_recv, weather_handle->userdata);
    }

out:
    cJSON_Delete(root);
}

int32_t qm_iot_weather_setopt(void *handle, qm_iot_weather_option_t option, void *data)
{
    qm_err_t ret = QM_EOK;
    weather_handle_t *weather_handle = (weather_handle_t*)handle; 
    if(handle == NULL){
        return -QM_EINVAL;
    }
    switch(option){

        case QM_IOT_WEATHEROPT_MQTT_HANDLE:
            weather_handle->mqtt_handle = data;
        break;
        case QM_IOT_WEATHEROPT_RECV_HANDLER:
            weather_handle->recv_handler = (qm_iot_weather_recv_handler_t)data;

        break;
        case QM_IOT_WEATHEROPT_USERDATA:
            if (weather_handle->userdata != data) {
                ret = weather_down_unregister(weather_handle);
                if (ret != QM_EOK) {
                    break;
                }
            }
            weather_handle->userdata = data;
            if (data != NULL) {
                ret = weather_down_register_if_ready(weather_handle);
            }
        break;
        default:
            ret = -QM_EINVAL;
        break;
    }
    return ret;
}

int32_t qm_iot_weather_deinit(void **handle)
{
    weather_handle_t *weather_handle;
    int ret;

    if (handle == NULL || *handle == NULL) {
        return -QM_EINVAL;
    }

    weather_handle = (weather_handle_t *)(*handle);
    ret = weather_down_unregister(weather_handle);
    if (ret != QM_EOK) {
        return ret;
    }

    qm_free(weather_handle);
    *handle = NULL;

    return QM_EOK;
}

static int qm_iot_weather_msg_id_get(void *handle)
{
    weather_handle_t *weather_handle = (weather_handle_t*)handle; 
    if(handle == NULL){
        return -QM_EINVAL;
    }
    weather_handle->msg_id = (weather_handle->msg_id == QM_IOT_WEATHER_MSG_ID_MAX) ? 1 : weather_handle->msg_id + 1;
    return weather_handle->msg_id;
}

int32_t qm_iot_weather_request(void *handle)
{
    qm_err_t ret = QM_EOK;
    char *payload = NULL;
    char topic[QM_IOT_WEATHER_TOPIC_MAX_LEN] = {0};
    void *iot_handle = NULL;
    weather_handle_t *weather_handle = (weather_handle_t*)handle; 
    if(handle == NULL){
        return -QM_EINVAL;
    }

    iot_handle = weather_iot_handle(weather_handle);
    if (iot_handle == NULL || weather_handle->mqtt_handle == NULL) {
        return -QM_EINVAL;
    }

    payload = (char*)qm_malloc(QM_IOT_WEATHER_REQUEST_PAYLOAD_MAX_LEN);
    if(payload == NULL){
        return -QM_ENOMEM;
    }
    memset(payload, 0, QM_IOT_WEATHER_REQUEST_PAYLOAD_MAX_LEN);

    qm_snprintf(payload, QM_IOT_WEATHER_REQUEST_PAYLOAD_MAX_LEN, QM_IOT_WEATHER_REQUEST_PAYLOAD_FMT, qm_iot_weather_msg_id_get(handle), qm_now_ms());

    ret = weather_down_register_if_ready(weather_handle);
    if (ret != QM_EOK) {
        qm_free(payload);
        return ret;
    }

    qm_snprintf(topic, QM_IOT_WEATHER_TOPIC_MAX_LEN, QM_IOT_WEATHER_REQUEST_TOPIC_FMT,
                qm_iot_pid_get(iot_handle),
                qm_iot_did_get(iot_handle));

    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "weather request topic_len=%lu payload_len=%lu",
                  (unsigned long)strlen(topic), (unsigned long)strlen(payload));
    ret = qm_iot_mqtt_pub(weather_handle->mqtt_handle,
                          topic,
                          (uint8_t *)payload,
                          strlen(payload),
                          QM_MQTT_QoS0);
    if (ret != QM_EOK) {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_UPLINK, "weather request publish failed ret=%d", ret);
    }
    
    qm_free(payload);
    payload = NULL;

    return ret;
}
