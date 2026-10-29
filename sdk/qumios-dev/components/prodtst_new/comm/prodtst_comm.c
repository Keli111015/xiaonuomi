#include "prodtst_comm.h"
#include "qm_log.h"
#include "qm_errno.h"
#include "qm_kernel.h"
#include "qm_utils_list.h"
#include "iot_mqtt.h"
#include "json_parser.h"
#include "prodtst_common.h"
#include "iot_ota.h"
#include "cJSON.h"
#include "qm_cli.h"
#include "qm_kv.h"
#include "iot_mqtt_errno.h"

#define LOG_TAG  "mqtt client"
enum
{
    PRODTST_NORMAL_LOOP_TIME = 200,
    PRODTST_FAST_LOOP_TIME = 50,
};

#ifndef CONFIG_MQTT_CLIENT_TOPIC_SUB_MAX_COUNT
#define CONFIG_MQTT_CLIENT_TOPIC_SUB_MAX_COUNT     8
#endif

#define MQTT_PRODTST_MSGLEN             (1024*5)
#define MQTT_PRODTST_SEND_MSGLEN        (1024*5)
#define MAX_MQTT_PRODTST_TOPIC_LEN      (64)


#define PRODTST_MQTT_CLIENT_SEND_TASK_SIZE (8*1024)
#define PRODTST_MQTT_CLIENT_TASK_SIZE (10*1024)
#define PRODTST_MQTT_CLIENT_TASK_PRIO (20)


#define PRODTST_RETRY_INTERVAL_MAX_MS  (60000)
#define PRODTST_COMM_SEND_QUEUE_LEN    (16)

#define PRODTST_JSON_DATA_MAX_LEN     (96)

#define PRODTST_GENRIC_REPLY_FORMAT    "{\"method\":\"%s\",\"version\":\"2.0\",\"code\":%d}"
#define PRODTST_TEST_REPLY_FORMAT      "{\"method\":\"%s\",\"version\":\"2.0\",\"code\":%d,\"params\":%s}"

#define PRODTST_TEST_ENV_KV_KEY  "test_env"

typedef struct{

    int is_login;
    int test_env;
    uint16_t packet_id;
    void *mqtt_client;
    int recv_exit;
    qm_task_t task;
    prodtst_comm_hooks_t hooks;

    int interval_ms;
    int interval_max_ms;

    char product_id[PRODTST_PRODUCT_ID_MAX_LEN+1];
    char device_id[PRODTST_DEVICE_ID_MAX_LEN+1];
    char device_secret[PRODTST_DEVICE_SECRET_MAX_LEN+1];
    char name[PRODTST_PRODUCT_ID_MAX_LEN + PRODTST_DEVICE_ID_MAX_LEN + 1];
    
    char *msg_readbuf;
    char *msg_recvbuf;

#if CONFIG_PRODTST_OTA_SUPPORT
    void *ota_handle;
    char version[PRODTST_VERSION_MAX_LEN + 1];
#endif

    qm_list_t *list;
    qm_mutex_t lock;
    qm_sem_t sem;
    
}prodtst_comm_ctx_t;

enum{
    PRODTST_TYPE_DATA,
    PRODTST_TYPE_OTA,
    PRODTST_TYPE_CMD,
};

enum{
    PRODTST_TYPE_CMD_EXIT,
};

typedef struct{
    int code;
    int type;
    int sub_type;
    int len;
    char data[1];
}prodtst_comm_send_t;


#define MQTT_TOPIC_PRODTST_CONFIG_FORMAT                    "/%s/%s/prodtst/config"
#define MQTT_TOPIC_PRODTST_CONFIG_REPLY_FORMAT              "/%s/%s/prodtst/config_reply"

#define MQTT_TOPIC_PRODTST_READY_FORMAT                     "/%s/%s/prodtst/ready"
#define MQTT_TOPIC_PRODTST_READY_REPLY_FORMAT               "/%s/%s/prodtst/ready_reply"

#define MQTT_TOPIC_PRODTST_BEFORE_TEST_FORMAT               "/%s/%s/prodtst/before_test"
#define MQTT_TOPIC_PRODTST_BEFORE_TEST_REPLY_FORMAT         "/%s/%s/prodtst/before_test_reply"

#define MQTT_TOPIC_PRODTST_TEST_FORMAT                      "/%s/%s/prodtst/test"
#define MQTT_TOPIC_PRODTST_TEST_REPLY_FORMAT                "/%s/%s/prodtst/test_reply"

#define MQTT_TOPIC_PRODTST_SCRIPT_TEST_FORMAT               "/%s/%s/prodtst/script_test"
#define MQTT_TOPIC_PRODTST_SCRIPT_TEST_REPLY_FORMAT         "/%s/%s/prodtst/script_test_reply"

#define MQTT_TOPIC_PRODTST_AFTER_TEST_FORMAT                "/%s/%s/prodtst/after_test"
#define MQTT_TOPIC_PRODTST_AFTER_TEST_REPLY_FORMAT          "/%s/%s/prodtst/after_test_reply"

#define MQTT_TOPIC_PRODTST_RESULT_FORMAT                    "/%s/%s/prodtst/result"
#define MQTT_TOPIC_PRODTST_RESULT_REPLY_FORMAT              "/%s/%s/prodtst/result_reply"

#define MQTT_TOPIC_PRODTST_REPORT_FORMAT                    "/%s/%s/prodtst/report"
#define MQTT_TOPIC_PRODTST_REPORT_REPLY_FORMAT              "/%s/%s/prodtst/report_reply"

#define MQTT_TOPIC_PRODTST_WIRELESS_LIST_FORMAT             "/%s/%s/prodtst/wireless_list"
#define MQTT_TOPIC_PRODTST_WIRELESS_LIST_REPLY_FORMAT       "/%s/%s/prodtst/wireless_list_reply"

#if CONFIG_PRODTST_OTA_SUPPORT

#define MQTT_TOPIC_PRODTST_OTA_FORMAT                    "/%s/%s/ota/inform"
#define MQTT_TOPIC_PRODTST_OTA_UPGRADE                   "/%s/%s/ota/upgrade"
#define MQTT_TOPIC_PRODTST_OTA_PROGRESS                  "/%s/%s/ota/progress"

#endif

static int32_t mqtt_client_sub_start(void *pcontext);

static void mqtt_client_prodtst_ready_reply(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg);
static void mqtt_client_prodtst_config(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg);
static void mqtt_client_prodtst_before_test(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg);
static void mqtt_client_prodtst_test(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg);
static void mqtt_client_prodtst_script_test(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg);
static void mqtt_client_prodtst_after_test(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg);
static void mqtt_client_prodtst_result(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg);
static void mqtt_client_prodtst_report_reply(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg);
static void mqtt_client_prodtst_wireless_list(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg);


#if CONFIG_PRODTST_OTA_SUPPORT
static void mqtt_client_prodtst_ota_upgrade(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg);
#endif

static int retry_interval_ms_set(void *handle, int interval_ms, int interval_max_ms);
static int retry_interval_ms_get(void *handle);
static int send_task_exit(void *handle);
static int prodtst_comm_generic_send(void *handle, int type, int sub_type, int code, char *data, int len);

typedef struct {
    char *topic_fmt;
    void *callback;
    char topic[MAX_MQTT_PRODTST_TOPIC_LEN];
} mqtt_client_uri_map_t;

static prodtst_comm_ctx_t *g_prodtst_comm_ctx = NULL;

static mqtt_client_uri_map_t g_mqtt_client_uri_map[] = {

    {MQTT_TOPIC_PRODTST_READY_REPLY_FORMAT, mqtt_client_prodtst_ready_reply, {0}},

    {MQTT_TOPIC_PRODTST_CONFIG_FORMAT, mqtt_client_prodtst_config, {0}},    

    {MQTT_TOPIC_PRODTST_BEFORE_TEST_FORMAT, mqtt_client_prodtst_before_test, {0}},
    
    {MQTT_TOPIC_PRODTST_TEST_FORMAT, mqtt_client_prodtst_test, {0}},

    {MQTT_TOPIC_PRODTST_AFTER_TEST_FORMAT, mqtt_client_prodtst_after_test, {0}},

    {MQTT_TOPIC_PRODTST_SCRIPT_TEST_FORMAT, mqtt_client_prodtst_script_test, {0}},

    {MQTT_TOPIC_PRODTST_RESULT_FORMAT, mqtt_client_prodtst_result, {0}},  

    {MQTT_TOPIC_PRODTST_REPORT_REPLY_FORMAT, mqtt_client_prodtst_report_reply, {0}},

    {MQTT_TOPIC_PRODTST_WIRELESS_LIST_FORMAT, mqtt_client_prodtst_wireless_list, {0}},

    #if CONFIG_PRODTST_OTA_SUPPORT
    {MQTT_TOPIC_PRODTST_OTA_UPGRADE, mqtt_client_prodtst_ota_upgrade, {0}},  
    #endif
};

static int32_t mqtt_client_sub_start(void *pcontext)
{
    int ret = 0;
    iot_mqtt_topic_t *topic_list = NULL;
    iot_mqtt_topic_t *m_topic_list = NULL;
    int count = 0,i = 0, j = 0, sum_count = 0;
    int req_count = 0, every_count = 0, remain_count = 0;
    mqtt_client_uri_map_t *uri_map = NULL;
    prodtst_comm_ctx_t *mqtt_ctx = (prodtst_comm_ctx_t*)pcontext;
    if(pcontext == NULL){
        return -QM_EINVAL;
    }

    sum_count = sizeof(g_mqtt_client_uri_map)/sizeof(mqtt_client_uri_map_t);
    if(!sum_count){
        return -QM_EEMPTY;
    }
    
    if(sum_count % CONFIG_MQTT_CLIENT_TOPIC_SUB_MAX_COUNT){
        req_count = sum_count / CONFIG_MQTT_CLIENT_TOPIC_SUB_MAX_COUNT + 1;
    }else{
        req_count = sum_count / CONFIG_MQTT_CLIENT_TOPIC_SUB_MAX_COUNT;
    }

    if(req_count == 1){
        count = sum_count;
    }else{
        count = CONFIG_MQTT_CLIENT_TOPIC_SUB_MAX_COUNT;
    }

    every_count = count;
    remain_count = sum_count;

    topic_list = (iot_mqtt_topic_t*)qm_malloc(sizeof(iot_mqtt_topic_t) * count);
    if(topic_list == NULL){
        return -QM_ENOMEM;
    } 

    for(j = 0; j < req_count; j++){
         for(i = 0; i < every_count; i++){
            count = i + (j * CONFIG_MQTT_CLIENT_TOPIC_SUB_MAX_COUNT);
            uri_map = &g_mqtt_client_uri_map[count];
            memset(uri_map->topic, 0, MAX_MQTT_PRODTST_TOPIC_LEN);
            qm_snprintf(uri_map->topic, MAX_MQTT_PRODTST_TOPIC_LEN, uri_map->topic_fmt, mqtt_ctx->product_id, mqtt_ctx->device_id);
            QM_LOGD(LOG_TAG,"topic: %s", uri_map->topic);
            m_topic_list = topic_list + i; 
            m_topic_list->qos = IOT_MQTT_QOS0;
            m_topic_list->topic_filter = uri_map->topic;
            m_topic_list->topic_handle_func = uri_map->callback;
            m_topic_list->pcontext = pcontext;
        }  
        ret = iot_mqtt_client_multi_subscribe_wait_ack(mqtt_ctx->mqtt_client, topic_list, every_count);
        if(ret != SUCCESS_RETURN){
            ret = -QM_ETIMEOUT;
            break;
        }
        m_topic_list = NULL;
        remain_count = remain_count - every_count;
        if(remain_count >= CONFIG_MQTT_CLIENT_TOPIC_SUB_MAX_COUNT){
            every_count = CONFIG_MQTT_CLIENT_TOPIC_SUB_MAX_COUNT;
        }else{
            every_count = remain_count;
        }
    }
    qm_free(topic_list);
    topic_list = NULL;

    return QM_EOK;
}

void event_handle(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg)
{
    int code = 0;
    prodtst_comm_ctx_t *comm_ctx = (prodtst_comm_ctx_t*)pcontext;
    uint32_t packet_id = (uint32_t)msg->msg;

    switch (msg->event_type) {
        case IOT_MQTT_EVENT_UNDEF:
            QM_LOGD(LOG_TAG,"undefined event occur.");
            break;

        case IOT_MQTT_EVENT_DISCONNECT:
            QM_LOGD(LOG_TAG,"MQTT disconnect.");

            comm_ctx->is_login = 0;

            if(comm_ctx->hooks.event_notify){
                comm_ctx->hooks.event_notify(PRODTST_COMM_EVENT_LOGOUT);
            }

            break;

        case IOT_MQTT_EVENT_RECONNECT:

            code = (int)msg->msg;
            QM_LOGD(LOG_TAG,"MQTT reconnect, code = %d.", code);
            
            if(code != SUCCESS_RETURN){
                comm_ctx->is_login = 0;
                if(comm_ctx->hooks.event_notify){
                    comm_ctx->hooks.event_notify(PRODTST_COMM_EVENT_LOGOUT);
                }
            }else{
                comm_ctx->is_login = 1;
                mqtt_client_sub_start(comm_ctx);
                if(comm_ctx->hooks.event_notify){
                    comm_ctx->hooks.event_notify(PRODTST_COMM_EVENT_LOGIN);
                }
            }

            retry_interval_ms_set((void*)comm_ctx, 500, PRODTST_RETRY_INTERVAL_MAX_MS);

            break;

        case IOT_MQTT_EVENT_SUBCRIBE_SUCCESS:
            QM_LOGD(LOG_TAG,"subscribe success, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOT_MQTT_EVENT_SUBCRIBE_TIMEOUT:
            QM_LOGD(LOG_TAG,"subscribe wait ack timeout, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOT_MQTT_EVENT_SUBCRIBE_NACK:
            QM_LOGD(LOG_TAG,"subscribe nack, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOT_MQTT_EVENT_UNSUBCRIBE_SUCCESS:
            QM_LOGD(LOG_TAG,"unsubscribe success, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOT_MQTT_EVENT_UNSUBCRIBE_TIMEOUT:
            QM_LOGD(LOG_TAG,"unsubscribe timeout, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOT_MQTT_EVENT_UNSUBCRIBE_NACK:
            QM_LOGD(LOG_TAG,"unsubscribe nack, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOT_MQTT_EVENT_PUBLISH_SUCCESS:
            QM_LOGD(LOG_TAG,"publish success, packet-id=%u", (unsigned int)packet_id);

            break;

        case IOT_MQTT_EVENT_PUBLISH_TIMEOUT:
            QM_LOGD(LOG_TAG,"publish timeout, packet-id=%u", (unsigned int)packet_id);

            break;

        case IOT_MQTT_EVENT_PUBLISH_NACK:
            QM_LOGD(LOG_TAG,"publish nack, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOT_MQTT_EVENT_PUBLISH_RECVEIVED:

            break;

        case IOT_MQTT_EVENT_BUFFER_OVERFLOW:
            QM_LOGD(LOG_TAG,"buffer overflow, %s", (char*)msg->msg);
            break;

        default:
            QM_LOGD(LOG_TAG,"Should NOT arrive here.");
            break;
    }
}

static int mqtt_client_event_callback(void *handle, int msg_type, char *payload, int payload_len)
{
    prodtst_comm_ctx_t *comm_ctx = (prodtst_comm_ctx_t*)handle;
    comm_ctx->hooks.msg_notify(msg_type, payload, payload_len);
    return 0;
}

static void mqtt_client_prodtst_ready_reply(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg)
{
    iot_mqtt_topic_info_pt ptopic_info = (iot_mqtt_topic_info_pt) msg->msg;
    mqtt_client_event_callback(pcontext, PRODTST_COMM_MSG_TYPE_READY_REPLY, ptopic_info->payload, ptopic_info->payload_len);  
}

static void mqtt_client_prodtst_config(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg)
{
    iot_mqtt_topic_info_pt ptopic_info = (iot_mqtt_topic_info_pt) msg->msg;
    mqtt_client_event_callback(pcontext, PRODTST_COMM_MSG_TYPE_CONFIG, ptopic_info->payload, ptopic_info->payload_len);  
}

static void mqtt_client_prodtst_test(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg)
{
    iot_mqtt_topic_info_pt ptopic_info = (iot_mqtt_topic_info_pt) msg->msg;
    mqtt_client_event_callback(pcontext, PRODTST_COMM_MSG_TYPE_TEST, ptopic_info->payload, ptopic_info->payload_len);
}

static void mqtt_client_prodtst_result(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg)
{
    iot_mqtt_topic_info_pt ptopic_info = (iot_mqtt_topic_info_pt) msg->msg;
    mqtt_client_event_callback(pcontext, PRODTST_COMM_MSG_TYPE_RESULT, ptopic_info->payload, ptopic_info->payload_len);
}

static void mqtt_client_prodtst_before_test(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg)
{
    iot_mqtt_topic_info_pt ptopic_info = (iot_mqtt_topic_info_pt) msg->msg;
    mqtt_client_event_callback(pcontext, PRODTST_COMM_MSG_TYPE_BEFORE_TEST, ptopic_info->payload, ptopic_info->payload_len);
}

static void mqtt_client_prodtst_script_test(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg)
{
    iot_mqtt_topic_info_pt ptopic_info = (iot_mqtt_topic_info_pt) msg->msg;
    mqtt_client_event_callback(pcontext, PRODTST_COMM_MSG_TYPE_SCRIPT_TEST, ptopic_info->payload, ptopic_info->payload_len);
}

static void mqtt_client_prodtst_after_test(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg)
{
    iot_mqtt_topic_info_pt ptopic_info = (iot_mqtt_topic_info_pt) msg->msg;
    mqtt_client_event_callback(pcontext, PRODTST_COMM_MSG_TYPE_AFTER_TEST, ptopic_info->payload, ptopic_info->payload_len);
}

static void mqtt_client_prodtst_report_reply(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg)
{
    iot_mqtt_topic_info_pt ptopic_info = (iot_mqtt_topic_info_pt) msg->msg;
    mqtt_client_event_callback(pcontext, PRODTST_COMM_MSG_TYPE_REPORT_REPLY, ptopic_info->payload, ptopic_info->payload_len);
}

static void mqtt_client_prodtst_wireless_list(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg)
{
    iot_mqtt_topic_info_pt ptopic_info = (iot_mqtt_topic_info_pt) msg->msg;
    mqtt_client_event_callback(pcontext, PRODTST_COMM_MSG_TYPE_WIRELESS_LIST, ptopic_info->payload, ptopic_info->payload_len);
}


#if CONFIG_PRODTST_OTA_SUPPORT

int prodtst_version_inform(void *handle, char *version)
{
    char *print = NULL;
    cJSON *root = NULL;
    qm_err_t ret = QM_EOK;
    cJSON *param_object = NULL;
    if(version == NULL){
        return -QM_EINVAL;
    }
    root = cJSON_CreateObject();
    if(root == NULL){
        return -QM_ENOMEM;
    }
    cJSON_AddStringToObject(root, PRODTST_JSON_KEY_METHOD, "inform");
    cJSON_AddStringToObject(root, PRODTST_JSON_KEY_VERSION, PRODTST_PROTOCOL_VERSION);

    param_object = cJSON_AddObjectToObject(root, PRODTST_JSON_KEY_PARAMS);
    if(param_object == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    cJSON_AddStringToObject(param_object, PRODTST_JSON_KEY_VERSION, version);

    print = cJSON_PrintUnformatted(root);
    if(print == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    QM_LOGD(LOG_TAG, "inform: %s", print);

    ret = prodtst_comm_send(handle, PRODTST_COMM_MSG_TYPE_INFORM, print, strlen(print));

__exit:
    if(root){
        cJSON_Delete(root);
        root = NULL;
    }

    if(print){
        cJSON_free(print);
        root = NULL;
    }
    return ret;
}

int prodtst_ota_progress_inform(void *handle, int step)
{
    char str_step[8] = {0};
    char *print = NULL;
    cJSON *root = NULL;
    qm_err_t ret = QM_EOK;
    cJSON *param_object = NULL;

    root = cJSON_CreateObject();
    if(root == NULL){
        return -QM_ENOMEM;
    }
    cJSON_AddStringToObject(root, PRODTST_JSON_KEY_METHOD, "progress");
    cJSON_AddStringToObject(root, PRODTST_JSON_KEY_VERSION, PRODTST_PROTOCOL_VERSION);

    param_object = cJSON_AddObjectToObject(root, PRODTST_JSON_KEY_PARAMS);
    if(param_object == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    qm_snprintf(str_step, 8, "%d", step);

    cJSON_AddStringToObject(param_object, "step", str_step);

    print = cJSON_PrintUnformatted(root);
    if(print == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    QM_LOGD(LOG_TAG, "progress: %s", print);

    ret = prodtst_comm_send(handle, PRODTST_COMM_MSG_TYPE_PROGRESS, print, strlen(print));

__exit:
    if(root){
        cJSON_Delete(root);
        root = NULL;
    }

    if(print){
        cJSON_free(print);
        root = NULL;
    }
    return ret;
}

void ota_event_callback(iot_ota_event_t event, void *arg)
{
    int step = 0;
    switch(event){

        case IOT_OTA_EVENT_PROGRESS:
            step = (int)arg;
            prodtst_ota_progress_inform(g_prodtst_comm_ctx, step);
        break;

        case IOT_OTA_EVENT_SUCCESS:
            if(g_prodtst_comm_ctx->hooks.event_notify){
                g_prodtst_comm_ctx->hooks.event_notify(PRODTST_COMM_EVENT_OTA_SUCCESS);
            }
            
        break;

        case IOT_OTA_EVENT_FAIL:
            if(g_prodtst_comm_ctx->hooks.event_notify){
                g_prodtst_comm_ctx->hooks.event_notify(PRODTST_COMM_EVENT_OTA_FAIL);
            }
        break;

        default:
        break;
    }
}


static void mqtt_client_prodtst_ota_upgrade(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg)
{
    char *version = NULL;
    cJSON *root = NULL;
    cJSON *param_object = NULL;
    qm_err_t ret = QM_EOK;
    iot_ota_param_t param = {0};
    iot_ota_upgrade_info_t upgrade_info = {0};
    prodtst_comm_ctx_t *comm_ctx = (prodtst_comm_ctx_t*)pcontext;
    iot_mqtt_topic_info_pt ptopic_info = (iot_mqtt_topic_info_pt) msg->msg;
    
    QM_LOGD(LOG_TAG, "upgrade info: %.*s", ptopic_info->payload_len, ptopic_info->payload);

    root = cJSON_Parse(ptopic_info->payload);
    if(root == NULL){
        goto __exit;
    }

    param_object = cJSON_GetObjectItem(root, "params");
    if(param_object == NULL){
        goto __exit;
    }

    upgrade_info.file_size = cJSON_GetObjectItem(param_object, "size")->valueint;
    upgrade_info.url = cJSON_GetObjectItem(param_object, "url")->valuestring;
    upgrade_info.md5str = cJSON_GetObjectItem(param_object, "md5")->valuestring;
    version = cJSON_GetObjectItem(param_object, "version")->valuestring;

    QM_LOGD(LOG_TAG, "ota version: %s", version);
    QM_LOGD(LOG_TAG, "file size: %d", upgrade_info.file_size);
    QM_LOGD(LOG_TAG, "url: %s", upgrade_info.url);
    QM_LOGD(LOG_TAG, "md5: %s", upgrade_info.md5str);
    
    param.arg = comm_ctx;
    param.event = ota_event_callback;
    comm_ctx->ota_handle = iot_ota_init(&param);
    if(comm_ctx->ota_handle == NULL){
        return;
    }

    ret = iot_ota_start(comm_ctx->ota_handle, &upgrade_info);
    if(ret != QM_EOK){
        goto __exit;
    }

    cJSON_Delete(root);

    if(comm_ctx->hooks.event_notify){
        comm_ctx->hooks.event_notify(PRODTST_COMM_EVENT_OTA_START);
    }

    return;

__exit:
    if(comm_ctx->ota_handle){
        iot_ota_deinit(comm_ctx->ota_handle); 
    }
    if(root){
        cJSON_Delete(root);
    }
}

#endif


static int retry_interval_ms_set(void *handle, int interval_ms, int interval_max_ms)
{
    prodtst_comm_ctx_t *comm_ctx = (prodtst_comm_ctx_t*)handle;
    comm_ctx->interval_ms = interval_ms;
    comm_ctx->interval_max_ms = interval_max_ms;
    return QM_EOK;
}

static int retry_interval_ms_get(void *handle)
{
    prodtst_comm_ctx_t *comm_ctx = (prodtst_comm_ctx_t*)handle;

    if (comm_ctx->interval_max_ms > comm_ctx->interval_ms) {
        comm_ctx->interval_ms *= 2;
    } else {
        comm_ctx->interval_ms = comm_ctx->interval_max_ms;
    }
    return comm_ctx->interval_ms;
}


int mqtt_client_connect(void *handle)
{
    int rc = 0;
    iot_mqtt_param_t mqtt_params = {0};

    prodtst_comm_ctx_t *comm_ctx = (prodtst_comm_ctx_t*)handle;

    if (NULL == (comm_ctx->msg_recvbuf = (char *)qm_malloc(MQTT_PRODTST_SEND_MSGLEN))) {
        QM_LOGE(LOG_TAG,"not enough memory");
        rc = -1;
        goto __exit;
    }

    if (NULL == (comm_ctx->msg_readbuf = (char *)qm_malloc(MQTT_PRODTST_MSGLEN))) {
        QM_LOGE(LOG_TAG,"not enough memory");
        rc = -1;
        goto __exit;
    }

    /* Initialize MQTT parameter */
    memset(&mqtt_params, 0x0, sizeof(mqtt_params));

    qm_snprintf(comm_ctx->name, PRODTST_PRODUCT_ID_MAX_LEN + PRODTST_DEVICE_ID_MAX_LEN, "%s&%s", comm_ctx->device_id, comm_ctx->product_id);

    
    mqtt_params.host = CONFIG_PRODTST_MQTT_HOST;
    mqtt_params.client_id = comm_ctx->device_id;
    if(comm_ctx->test_env){
        mqtt_params.port = CONFIG_PRODTST_MQTT_TEST_PORT;
    }else{
        mqtt_params.port = CONFIG_PRODTST_MQTT_PORT;
    }
    mqtt_params.username = comm_ctx->name;
    mqtt_params.password = comm_ctx->device_secret;

    mqtt_params.request_timeout_ms = 3000;
    mqtt_params.clean_session = 1;
    mqtt_params.keepalive_interval_ms = 60000;
    mqtt_params.pread_buf = comm_ctx->msg_readbuf;
    mqtt_params.read_buf_size = MQTT_PRODTST_MSGLEN;
    mqtt_params.pwrite_buf = comm_ctx->msg_recvbuf;
    mqtt_params.write_buf_size = MQTT_PRODTST_SEND_MSGLEN;

    mqtt_params.handle_event.h_fp = event_handle;
    mqtt_params.handle_event.pcontext = handle;

    /* Construct a MQTT client with specify parameter */
    comm_ctx->mqtt_client = iot_mqtt_client_construct(&mqtt_params);
    if (NULL == comm_ctx->mqtt_client) {
        QM_LOGE(LOG_TAG,"MQTT construct failed");
        rc = -QM_ERROR;
        goto __exit;
    }
#if 0
    topic_num = sizeof(g_mqtt_client_uri_map)/sizeof(mqtt_client_uri_map_t);

    QM_LOGD(LOG_TAG,"topic num: %d", topic_num);

    for(i = 0; i<topic_num; i++){
        
        memset(g_mqtt_client_uri_map[i].topic, 0, MAX_MQTT_PRODTST_TOPIC_LEN);

        qm_snprintf(g_mqtt_client_uri_map[i].topic, MAX_MQTT_PRODTST_TOPIC_LEN, g_mqtt_client_uri_map[i].topic_fmt, comm_ctx->product_id, comm_ctx->device_id);
        QM_LOGD(LOG_TAG,"topic: %s", g_mqtt_client_uri_map[i].topic);

        rc = iot_mqtt_client_subscribe(comm_ctx->mqtt_client, g_mqtt_client_uri_map[i].topic, IOT_MQTT_QOS0, (iot_mqtt_event_handle_func_fpt)g_mqtt_client_uri_map[i].callback, handle);
        if (rc < 0) {
            iot_mqtt_client_destroy(&comm_ctx->mqtt_client);
            QM_LOGE(LOG_TAG,"IOT_MQTT_Subscribe() failed, rc = %d", rc);
            rc = -QM_ERROR;
            goto __exit;
        }
    }     
#else
    rc =  mqtt_client_sub_start(comm_ctx);
    if (rc < 0) {
        iot_mqtt_client_destroy(&comm_ctx->mqtt_client);
        QM_LOGE(LOG_TAG,"IOT_MQTT_Subscribe() failed, rc = %d", rc);
        rc = -QM_ERROR;
        goto __exit;
    }
#endif

    comm_ctx->is_login = 1;
    if(comm_ctx->hooks.event_notify){
        comm_ctx->hooks.event_notify(PRODTST_COMM_EVENT_LOGIN);
    }

#if CONFIG_PRODTST_OTA_SUPPORT
    prodtst_version_inform(comm_ctx, comm_ctx->version);
#endif

    return QM_EOK;

__exit:
    if (NULL != comm_ctx->msg_recvbuf) {
        qm_free(comm_ctx->msg_recvbuf);
    }

    if (NULL != comm_ctx->msg_readbuf) {
        qm_free(comm_ctx->msg_readbuf);
    }

    if (NULL != comm_ctx->mqtt_client) {
        iot_mqtt_client_destroy(&comm_ctx->mqtt_client);
    }

    return rc;
}

void mqtt_client_recv(void *arg)
{
    int ret = 0;

    prodtst_comm_ctx_t *comm_ctx = (prodtst_comm_ctx_t*)arg;
    if(comm_ctx == NULL){
        goto __exit;
    }

    retry_interval_ms_set((void*)comm_ctx, 500, PRODTST_RETRY_INTERVAL_MAX_MS);

    while(1){

        if(comm_ctx->recv_exit){
            break;
        }
        ret = mqtt_client_connect((void*)comm_ctx);
        if( ret < 0 ){
            qm_msleep(retry_interval_ms_get((void*)comm_ctx));
        }else{
            break;
        }
    }

    retry_interval_ms_set((void*)comm_ctx, 500, PRODTST_RETRY_INTERVAL_MAX_MS);

    while(1){
        if(comm_ctx->recv_exit){
            break;
        }
        iot_mqtt_client_yield(comm_ctx->mqtt_client, PRODTST_NORMAL_LOOP_TIME);
    }

__exit:

    send_task_exit((void*)comm_ctx);
    
    qm_msleep(500);

    if (NULL != comm_ctx->msg_recvbuf) {
        qm_free(comm_ctx->msg_recvbuf);
        comm_ctx->msg_recvbuf = NULL;
    }

    if (NULL != comm_ctx->msg_readbuf) {
        qm_free(comm_ctx->msg_readbuf);
        comm_ctx->msg_readbuf = NULL;
    }

    if(NULL != comm_ctx->list){
        qm_list_destroy(comm_ctx->list);
        comm_ctx->list = NULL;
    }

    if(qm_mutex_is_valid(&comm_ctx->lock)){
        qm_mutex_free(&comm_ctx->lock);
    }

    if(qm_sem_is_valid(&comm_ctx->sem)){
        qm_sem_free(&comm_ctx->sem);
    }

    if (NULL != comm_ctx->mqtt_client) {
        iot_mqtt_client_destroy(comm_ctx->mqtt_client);
        comm_ctx->mqtt_client = NULL;
    }
    qm_free(comm_ctx);
    comm_ctx = NULL;
}

static int topic_creat(prodtst_comm_ctx_t *comm_ctx, int msg_type, char *topic, int topic_len)
{
    switch(msg_type){

        case PRODTST_COMM_MSG_TYPE_TEST_REPLY:
            qm_snprintf(topic, topic_len, MQTT_TOPIC_PRODTST_TEST_REPLY_FORMAT, comm_ctx->product_id, comm_ctx->device_id);
        break;

        case PRODTST_COMM_MSG_TYPE_SCRIPT_TEST_REPLY:
            qm_snprintf(topic, topic_len, MQTT_TOPIC_PRODTST_SCRIPT_TEST_REPLY_FORMAT, comm_ctx->product_id, comm_ctx->device_id);
        break;

        case PRODTST_COMM_MSG_TYPE_BEFORE_TEST_REPLY:
            qm_snprintf(topic, topic_len, MQTT_TOPIC_PRODTST_BEFORE_TEST_REPLY_FORMAT, comm_ctx->product_id, comm_ctx->device_id);
        break;

        case PRODTST_COMM_MSG_TYPE_AFTER_TEST_REPLY:
            qm_snprintf(topic, topic_len, MQTT_TOPIC_PRODTST_AFTER_TEST_REPLY_FORMAT, comm_ctx->product_id, comm_ctx->device_id);
        break;

        case PRODTST_COMM_MSG_TYPE_CONFIG_REPLY:
            qm_snprintf(topic, topic_len, MQTT_TOPIC_PRODTST_CONFIG_REPLY_FORMAT, comm_ctx->product_id, comm_ctx->device_id);
        break;

        case PRODTST_COMM_MSG_TYPE_READY:
            qm_snprintf(topic, topic_len, MQTT_TOPIC_PRODTST_READY_FORMAT, comm_ctx->product_id, comm_ctx->device_id);
        break;

        case PRODTST_COMM_MSG_TYPE_RESULT_REPLY:
            qm_snprintf(topic, topic_len, MQTT_TOPIC_PRODTST_RESULT_REPLY_FORMAT, comm_ctx->product_id, comm_ctx->device_id);
        break;

        case PRODTST_COMM_MSG_TYPE_REPORT:
            qm_snprintf(topic, topic_len, MQTT_TOPIC_PRODTST_REPORT_FORMAT, comm_ctx->product_id, comm_ctx->device_id);
        break;

    
        case PRODTST_COMM_MSG_TYPE_WIRELESS_LIST_REPLY:
            qm_snprintf(topic, topic_len, MQTT_TOPIC_PRODTST_WIRELESS_LIST_REPLY_FORMAT, comm_ctx->product_id, comm_ctx->device_id);
        break;

#if CONFIG_PRODTST_OTA_SUPPORT
        case PRODTST_COMM_MSG_TYPE_INFORM:
            qm_snprintf(topic, topic_len, MQTT_TOPIC_PRODTST_OTA_FORMAT, comm_ctx->product_id, comm_ctx->device_id);
        break;

        case PRODTST_COMM_MSG_TYPE_PROGRESS:
            qm_snprintf(topic, topic_len, MQTT_TOPIC_PRODTST_OTA_PROGRESS, comm_ctx->product_id, comm_ctx->device_id);
        break;

#endif
        default:
            return -QM_EINVAL;
        break;
    }
    return QM_EOK;
}

static int prodtst_comm_data_send(prodtst_comm_ctx_t *comm_ctx, int msg_type, int code, char *data, int len)
{
    int ret = 0;
    char mqtt_topic[MAX_MQTT_PRODTST_TOPIC_LEN] = {0};

    ret = topic_creat(comm_ctx, msg_type, mqtt_topic, MAX_MQTT_PRODTST_TOPIC_LEN);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "msg type error");
        return ret;
    }

    QM_LOGD(LOG_TAG, "topic: %s", mqtt_topic);


    ret = iot_mqtt_client_publish(comm_ctx->mqtt_client, mqtt_topic, data, len, IOT_MQTT_QOS0, 0);
    if (ret < 0) {
        QM_LOGE(LOG_TAG, "error occur when publish");
        ret = -QM_ERROR;
    }

    return ret;
}

void mqtt_client_send(void *arg)
{
    qm_list_node_t *node = NULL;
    prodtst_comm_send_t *comm_send = NULL;
    prodtst_comm_ctx_t *comm_ctx = (prodtst_comm_ctx_t*)arg;

    while(1){
        qm_sem_wait(&comm_ctx->sem, QM_WAIT_FOREVER);

        qm_mutex_lock(&comm_ctx->lock, QM_WAIT_FOREVER);
        if(comm_ctx->list->len == 0){
            qm_mutex_unlock(&comm_ctx->lock);
            continue;
        }

        node = qm_list_lpop(comm_ctx->list);
        if(node == NULL){
            qm_mutex_unlock(&comm_ctx->lock);
            continue;
        }

        qm_mutex_unlock(&comm_ctx->lock);

        comm_send = (prodtst_comm_send_t*)((char*)node + sizeof(qm_list_node_t));
        if(comm_send->type == PRODTST_TYPE_CMD){
            if(comm_send->sub_type == PRODTST_TYPE_CMD_EXIT){
                QM_LOGD(LOG_TAG, "mqtt send task exit");
                break;
            }
        }else if(comm_send->type == PRODTST_TYPE_DATA){
            prodtst_comm_data_send(comm_ctx, comm_send->sub_type, comm_send->code, comm_send->data, comm_send->len);
        }
        qm_free(node);
        node = NULL;
    }
}

static int send_task_exit(void *handle)
{
    return prodtst_comm_generic_send(handle, PRODTST_TYPE_CMD, PRODTST_TYPE_CMD_EXIT, 0, NULL, 0);
}

int prodtst_comm_send(void *handle, prodtst_comm_msg_type_t msg_type, char *data, int len)
{
    return prodtst_comm_generic_send(handle, PRODTST_TYPE_DATA, msg_type, 0, data, len);
}

static int prodtst_comm_generic_send(void *handle, int type, int sub_type, int code, char *data, int len)
{
    qm_list_node_t *node = NULL;
    prodtst_comm_send_t *comm_send = NULL;
    prodtst_comm_ctx_t *comm_ctx = (prodtst_comm_ctx_t*)handle;
    char *buf = qm_malloc(sizeof(qm_list_node_t) + sizeof(prodtst_comm_send_t) - 1 + len);
    if(buf == NULL){
        return -QM_ENOMEM;
    }

    node = (qm_list_node_t*)buf;

    comm_send = (prodtst_comm_send_t*)(buf+sizeof(qm_list_node_t));
    comm_send->code = code;
    comm_send->type = type;
    comm_send->sub_type = sub_type;
    comm_send->len = len;
    if(data){
        memcpy(comm_send->data, data, len);
    }

    qm_mutex_lock(&comm_ctx->lock, QM_WAIT_FOREVER);
    qm_list_node_init(node, (void*)comm_send);
    qm_list_rpush(comm_ctx->list, node);
    qm_mutex_unlock(&comm_ctx->lock);
    qm_sem_signal(&comm_ctx->sem);
    
    return QM_EOK;
}

static void handle_prodtst_test_env(char *pwbuf, int blen, int argc, char **argv);

static struct qm_cli_command prodtst_test_env = {
    .name = "prodtst_test_env",
    .help = "on/off",
    .function = handle_prodtst_test_env
};


static void handle_prodtst_test_env(char *pwbuf, int blen, int argc, char **argv)
{
    int test_env = 0;
    if(argc == 2){
        if(0 == memcmp("on", argv[1], strlen(argv[1]))){
            test_env = 1;
            qm_kv_set(PRODTST_TEST_ENV_KV_KEY, &test_env, sizeof(test_env), 1);

        }else if(0 == memcmp("off", argv[1], strlen(argv[1]))){
            test_env = 0;
            qm_kv_set(PRODTST_TEST_ENV_KV_KEY, &test_env, sizeof(test_env), 1);
        }
        qm_cli_printf("OK\r\n");
    } else {
        qm_cli_printf("Error\r\n");
        return;
    }
}

void *prodtst_comm_init(char *version, char *product_id, char *device_id, char *device_secret, prodtst_comm_hooks_t *hooks)
{
    int kv_len = 0;
    qm_err_t ret = QM_EOK;
    prodtst_comm_ctx_t *comm_ctx = NULL;

    if(version == NULL || product_id == NULL || 
        device_id == NULL || device_secret == NULL || hooks == NULL){
        return NULL;
    }

    if(strlen(version) > PRODTST_VERSION_MAX_LEN ||
       strlen(product_id) > PRODTST_PRODUCT_ID_MAX_LEN || 
       strlen(device_id) > PRODTST_DEVICE_ID_MAX_LEN ||
       strlen(device_secret) > PRODTST_DEVICE_SECRET_MAX_LEN){
        QM_LOGE(LOG_TAG,"prodtst product info error");
        return NULL;
    }

    comm_ctx = (prodtst_comm_ctx_t*)qm_malloc(sizeof(prodtst_comm_ctx_t));
    if(comm_ctx == NULL){
        goto __exit;
    }

    memset(comm_ctx, 0, sizeof(prodtst_comm_ctx_t));

    memcpy(&comm_ctx->hooks, hooks, sizeof(prodtst_comm_hooks_t));
#if CONFIG_PRODTST_OTA_SUPPORT
    memcpy(comm_ctx->version, version, strlen(version));
#endif
    memcpy(comm_ctx->product_id, product_id, strlen(product_id));
    memcpy(comm_ctx->device_id, device_id, strlen(device_id));
    memcpy(comm_ctx->device_secret, device_secret, strlen(device_secret));

    
    kv_len = sizeof(comm_ctx->test_env);
    qm_kv_get(PRODTST_TEST_ENV_KV_KEY, &comm_ctx->test_env, &kv_len);

    ret = qm_mutex_new(&comm_ctx->lock);
    if(ret != QM_EOK){
        goto __exit;
    }

    ret = qm_sem_new(&comm_ctx->sem, 0);
    if(ret != QM_EOK){
        goto __exit;
    }

    comm_ctx->list = qm_list_new();
    if(comm_ctx->list == NULL){
        goto __exit;
    }
    
    ret = qm_task_new(&comm_ctx->task, "mqtt send", mqtt_client_send, (void*)comm_ctx, PRODTST_MQTT_CLIENT_SEND_TASK_SIZE, PRODTST_MQTT_CLIENT_TASK_PRIO);
    if(ret != QM_EOK){
        goto __exit;
    }

    ret = qm_task_new(&comm_ctx->task, "mqtt recv", mqtt_client_recv, (void*)comm_ctx, PRODTST_MQTT_CLIENT_TASK_SIZE, PRODTST_MQTT_CLIENT_TASK_PRIO);
    if(ret != QM_EOK){
        send_task_exit((void*)comm_ctx);
        qm_msleep(500);
        goto __exit;
    }

    qm_cli_register_command(&prodtst_test_env);

    g_prodtst_comm_ctx = comm_ctx;

    return (void*)comm_ctx;

__exit:

    if(qm_mutex_is_valid(&comm_ctx->lock)){
        qm_mutex_free(&comm_ctx->lock);
    }

    if(qm_sem_is_valid(&comm_ctx->sem)){
        qm_sem_free(&comm_ctx->sem);
    }

    if(comm_ctx->list){
        qm_list_destroy(comm_ctx->list);
        comm_ctx->list = NULL;
    }

    if(comm_ctx){
        qm_free(comm_ctx);
        comm_ctx = NULL;
    }

    return NULL;
}


int prodtst_comm_deinit(void *handle)
{
    prodtst_comm_ctx_t *comm_ctx = (prodtst_comm_ctx_t*)handle;
    if(comm_ctx == NULL){
        return -QM_EINVAL;
    }
    comm_ctx->recv_exit = 1;
    return QM_EOK;
}