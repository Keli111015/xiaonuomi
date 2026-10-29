#include "qm_cloud_log.h"
#include "qm.h"
#include "qm_ota.h"
#include "qm_work.h"
#include "qm_time.h"

#include "json_parser.h"
#include "qm_iot_ota.h"
#include "qm_iot_mqtt.h"
#include "qm_iot_api.h"
#include "qm_iot_config.h"
#include "qm_iot_common.h"
#include "qm_iot_crypto.h"
#include "qm_iot_streams.h"
#include "qm_iot_jobs.h"
#include "qm_iot_jobs_profile.h"

#include "qm_utils_base64.h"
#include "qm_utils_string.h"

#define LOG_TAG "ota"

#define NUM_OF_BLOCKS_REQUESTED     ( 1U )

#ifndef CONFIG_QM_IOT_OTA_RETRY_COUNT
#define CONFIG_QM_IOT_OTA_RETRY_COUNT      (10)
#endif

#ifndef CONFIG_QM_IOT_OTA_TIMOUT_MS
#define CONFIG_QM_IOT_OTA_TIMOUT_MS        (5 * 1000)
#endif

#define TIMESTAMP_2000  943891200  /* 1970~2000 in seconds */

#define QM_IOT_OTA_PROGRESS_STR_NUM (10)

#define QM_IOT_OTA_UPDATA_COMPLETE  (100)

#define TOPIC_MAX_STR_LEN   (128)

#define DECODE_MAX_STR_LEN   (512)

#define PAYLOAD_MAX_STR_LEN   (64)


#define VER_MAX_STR_LEN   (384)


#define QM_IOT_OTA_TYPE             (QM_IOT_STREAMS_DATA_TYPE_CBOR)

#define QM_IOT_OTA_VER_RETRY_TIMEOUT    (10 * 1000)

#define OTA_NOTIFY_METHOD                   "notifyOta"

#define OTA_VERSION_REPLY_METHOD            "versionReply"

#define OTA_NOTIFY_REPLY_PAYLOAD  ("{\"devState\":%u}")

#define OTA_VERSION_INFORM  ("moduleVersion")
#define OTA_VERSION_STR_INFORM  ("version")

#if CONFIG_QM_IOT_OTA_MCU_SUPPORT
#define OTA_MCU_VERSION_INFORM_PAYLOAD  ("{\"typeId\":%u,\"version\":\"%s\", \"extra\":{%s}}")
#define OTA_VERSION_INFORM_WITH_MCU_PAYLOAD  ("{\"method\":\"version\",\"id\":%u,\"params\":{\"sdkVersion\":\"%s\",%s,\"resourceList\":[%s],\"silentOTA\":%u}}")
#endif
#define OTA_VERSION_INFORM_PAYLOAD  ("{\"sdkVersion\":\"%s\",%s,\"silentOTA\":%u}}")


#define ID_KEY                      "id"
#define CODE_KEY                    "code"
#define TYPE_KEY                    "type"
#define VESION_KEY                  "version"
#define PARAMS_KEY                  "params"
#define OTA_STATE_KEY               "otaState"
#define START_TIME_KEY              "startTime"
#define END_TIME_KEY                "endTime"

typedef struct
{
    uint32_t msg_id;
    uint32_t notify_msg_id;

    void *mqtt_handle;
    void *crypto_handle;
    void *registered_iot_handle;

    int progress_details;
    int lastReceivedblockId;

    uint32_t currentFileId;
    uint32_t currentBlockOffset;
    uint32_t totalBytesReceived;

    int32_t job_update_pub_notify;

    void *user_data;
    /** 是否已完成 OTA 相关 MQTT 预订阅。 */
    uint8_t mqtt_pre_sub_done;
    uint8_t down_registered;
    char *pucSignature;
    qm_iot_ota_event_handler_t event_handler;

    int retry_count;
    qm_work_t  retry_work;

    qm_work_t  ota_success_work;
    qm_mutex_t ota_lock;  // 添加互斥锁

    uint32_t max_block_size;
    qm_iot_ota_type_t ota_type;

    qm_iot_jobs_file_t documentFields;

    char did[CONFIG_QM_IOT_DID_MAX_LEN + 1];
    char jobs_id[TOPIC_MAX_STR_LEN + 1];

    uint32_t signatureLen;
    uint8_t signature[DECODE_MAX_STR_LEN];

    qm_iot_steams_context_t streams_context;

}qm_iot_ota_handle_t;

static void qm_iot_ota_notify_next_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata);
static void qm_iot_notify_ota_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata);
static void qm_iot_version_reply_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata);
static void ota_down_register_if_ready(qm_iot_ota_handle_t *ota);
static void ota_down_unregister(qm_iot_ota_handle_t *ota);
/**
 * @brief 从 ota 会话解析 IOT 会话 句柄（@ref QM_IOT_OTAOPT_USERDATA）。
 */
static void *ota_iot_handle(qm_iot_ota_handle_t *ota)
{
    if (ota == NULL) {
        return NULL;
    }

    return ota->user_data;
}

/**
 * @brief 用当前 session 配置刷新 ota 会话中的 did 字符串。
 */
static void ota_refresh_did(qm_iot_ota_handle_t *ota, void *iot_handle)
{
    uint32_t did = 0;

    if (ota == NULL || iot_handle == NULL) {
        return;
    }

    did = qm_iot_did_get(iot_handle);
    qm_snprintf(ota->did, CONFIG_QM_IOT_DID_MAX_LEN, "%u", did);
}

/**
 * @brief MQTT、session 句柄与 did 就绪后注册 OTA 相关订阅。
 */
static void ota_mqtt_sub_if_ready(qm_iot_ota_handle_t *ota)
{
    char out_topic[TOPIC_MAX_STR_LEN + 1] = {0};

    if (ota == NULL || ota->mqtt_handle == NULL || ota->did[0] == '\0') {
        return;
    }

    if (ota->mqtt_pre_sub_done) {
        return;
    }

    ota->mqtt_pre_sub_done = QM_TRUE;

    qm_iot_jobs_get_topic(QM_IOT_JOBS_TOPIC_NEX_CHANGED, ota->did, strlen(ota->did),
                          NULL, 0, out_topic, TOPIC_MAX_STR_LEN, NULL);
    qm_iot_mqtt_pre_sub(ota->mqtt_handle, out_topic, qm_iot_ota_notify_next_handler, QM_MQTT_QoS0, ota);
}

static void ota_down_register_if_ready(qm_iot_ota_handle_t *ota)
{
    void *iot_handle = NULL;
    int ret = QM_EOK;

    if (ota == NULL) {
        return;
    }

    iot_handle = ota_iot_handle(ota);
    if (iot_handle == NULL) {
        return;
    }

    if (ota->down_registered && ota->registered_iot_handle == iot_handle) {
        return;
    }

    ret = qm_iot_down_register(iot_handle,
                               (char *)OTA_NOTIFY_METHOD,
                               qm_iot_notify_ota_handler,
                               ota);
    if (ret != QM_EOK) {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_OTA, "register ota down handler failed ret=%d", ret);
        return;
    }

    ret = qm_iot_down_register(iot_handle,
                               (char *)OTA_VERSION_REPLY_METHOD,
                               qm_iot_version_reply_handler,
                               ota);
    if (ret != QM_EOK) {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_OTA, "register version reply handler failed ret=%d", ret);
        return;
    }

    /* 两个 method 都注册成功后才置位；失败时不置位，下次调用重试（同 method 重复注册为更新回调）。 */
    ota->down_registered = QM_TRUE;
    ota->registered_iot_handle = iot_handle;
}

static void ota_down_unregister(qm_iot_ota_handle_t *ota)
{
    int ret = QM_EOK;

    if (ota == NULL || !ota->down_registered || ota->registered_iot_handle == NULL) {
        return;
    }

    ret = qm_iot_down_unregister(ota->registered_iot_handle, (char *)OTA_NOTIFY_METHOD);
    if (ret != QM_EOK) {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_OTA, "unregister ota down handler failed ret=%d", ret);
    }

    ret = qm_iot_down_unregister(ota->registered_iot_handle, (char *)OTA_VERSION_REPLY_METHOD);
    if (ret != QM_EOK) {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_OTA, "unregister version reply handler failed ret=%d", ret);
    }

    ota->down_registered = QM_FALSE;
    ota->registered_iot_handle = NULL;
}

static qm_mutex_t g_ota_global_lock = {0};
static qm_iot_ota_handle_t *g_ota_handle = NULL;

static void qm_iot_event_handler(qm_iot_ota_handle_t *ota_handler, qm_iot_ota_event_t *event);

static void qm_iot_ota_retry_timeout(void *arg)
{
    int decodedDataLength;
    qm_iot_ota_event_t event = {0};
    uint8_t decodedData[QM_IOT_STREAMS_REQUEST_BUFFER_SIZE] = {0};
    qm_iot_ota_handle_t *ota_handler = (qm_iot_ota_handle_t *)arg;
    if(ota_handler == NULL){
        return;
    }

    if(ota_handler->retry_count++ >= CONFIG_QM_IOT_OTA_RETRY_COUNT){
        event.type = QM_IOT_OTA_EVENT_FAILED;
        event.ota_type = ota_handler->ota_type;
        qm_iot_event_handler(ota_handler, &event);
        return;
    }

    if(ota_handler->totalBytesReceived < ota_handler->documentFields.fileSize){

        decodedDataLength = qm_iot_steams_create_request(QM_IOT_OTA_TYPE,
                                                        ota_handler->currentFileId, ota_handler->max_block_size,
                                                        ota_handler->currentBlockOffset, NUM_OF_BLOCKS_REQUESTED,
                                                        (char *)decodedData, QM_IOT_STREAMS_REQUEST_BUFFER_SIZE);

        qm_iot_mqtt_pub(ota_handler->mqtt_handle,
                        ota_handler->streams_context.topicGetStream,
                        decodedData, decodedDataLength, QM_MQTT_QoS0);
    }

    qm_post_delayed_action(&ota_handler->retry_work, qm_iot_ota_retry_timeout, ota_handler, CONFIG_QM_IOT_OTA_TIMOUT_MS);
}

static void qm_iot_ota_success_action(void *arg)
{
    qm_iot_ota_event_t event = {0};
    qm_iot_ota_handle_t *ota_handler = (qm_iot_ota_handle_t *)arg;

    if(ota_handler && ota_handler->job_update_pub_notify){
        ota_handler->job_update_pub_notify = QM_FALSE;
        event.type = QM_IOT_OTA_EVENT_SUCCESS;
        event.ota_type = ota_handler->ota_type;
        qm_iot_event_handler(ota_handler, &event);
    }
}

static void qm_iot_event_handler(qm_iot_ota_handle_t *ota_handler, qm_iot_ota_event_t *event)
{
    static uint32_t off_set = 0;
    int ret = QM_EOK;
    int request_len = 0;
    char out_topic[TOPIC_MAX_STR_LEN + 1] = {0};
    char str_progress[QM_IOT_OTA_PROGRESS_STR_NUM + 1] = {0};
    char client_token[32 + 1] = {0};
    char request_data[QM_IOT_STREAMS_REQUEST_BUFFER_SIZE] = {0};
    qm_iot_jobs_status_t status = QM_IOT_JOBS_INPROGRESS;

    static qm_iot_ota_event_type_t  event_type = QM_IOT_OTA_EVENT_NONE;

    if(ota_handler == NULL ){
        return ;
    }

    switch (event->type)
    {

        case QM_IOT_OTA_EVENT_CANCEL:

            if(event_type < QM_IOT_OTA_EVENT_START || event_type > QM_IOT_OTA_EVENT_PROGRESS_UPADTE){
                break;
            }

            qm_cancel_delayed_action(&ota_handler->retry_work);

            if(ota_handler->event_handler){
                ota_handler->event_handler(ota_handler, event, ota_handler->user_data);
            }

        break;

        case QM_IOT_OTA_EVENT_READY:
            if(ota_handler->event_handler){
                ota_handler->event_handler(ota_handler, event, ota_handler->user_data);
            }
        break;

        case QM_IOT_OTA_EVENT_START:
            off_set = 0;
            ota_handler->lastReceivedblockId = -1;
            ota_handler->job_update_pub_notify = QM_FALSE;

            if(ota_handler->ota_type == QM_IOT_OTA_TYPE_MOUDLE)
            {
                qm_ota_start(NULL);
            }

            qm_cancel_delayed_action(&ota_handler->retry_work);


            ota_handler->retry_count = 0;

            if(ota_handler->event_handler){
                ota_handler->event_handler(ota_handler, event, ota_handler->user_data);
            }

            if(!event->data.start.block_size){
                event->data.start.block_size = CONFIG_QM_IOT_STREAMS_BLOCK_SIZE;
            }

            g_ota_handle->max_block_size = event->data.start.block_size;
            QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_OTA, "QM_IOT_OTA_EVENT_START:%d", g_ota_handle->max_block_size);

            qm_iot_crypto_signature_verification_start(ota_handler->crypto_handle,
                                                        ota_handler->documentFields.hash_crypto,
                                                        ota_handler->documentFields.asymmetric_crypto);

            request_len = qm_iot_steams_create_request(QM_IOT_OTA_TYPE,
                                                        ota_handler->currentFileId, ota_handler->max_block_size,
                                                        ota_handler->currentBlockOffset, NUM_OF_BLOCKS_REQUESTED,
                                                        request_data, QM_IOT_STREAMS_REQUEST_BUFFER_SIZE);
            QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD,
                          "streams publish payload_len=%d", request_len);

            qm_iot_mqtt_pub(ota_handler->mqtt_handle,
                            ota_handler->streams_context.topicGetStream,
                            (uint8_t *)request_data, request_len, QM_MQTT_QoS0);

            qm_post_delayed_action(&ota_handler->retry_work, qm_iot_ota_retry_timeout, ota_handler, CONFIG_QM_IOT_OTA_TIMOUT_MS);
        break;

        case QM_IOT_OTA_EVENT_DATA_INSTALLING:

            qm_cancel_delayed_action(&ota_handler->retry_work);
        #if 0
            qm_cancel_delayed_action(&ota_handler->ota_notify_work);//防止多次调用
        #endif

            if(ota_handler->event_handler){
                ota_handler->event_handler(ota_handler, event, ota_handler->user_data);
            }

            if(event->data.data_write.code == QM_IOT_CODE_SUCCESS)
            {
                if(ota_handler->ota_type == QM_IOT_OTA_TYPE_MOUDLE)
                {
                    qm_ota_write(&off_set, event->data.data_write.ota_data, event->data.data_write.data_len);
                }
                qm_iot_crypto_signature_verification_update(ota_handler->crypto_handle,
                                                            event->data.data_write.ota_data, event->data.data_write.data_len);
                ota_handler->retry_count = 0;
                qm_post_delayed_action(&ota_handler->retry_work, qm_iot_ota_retry_timeout, ota_handler, CONFIG_QM_IOT_OTA_TIMOUT_MS);
            }
            else
            {
                ota_handler->retry_count = 0;
                status = QM_IOT_JOBS_FAILED;
                event->type = QM_IOT_OTA_EVENT_FAILED;
                qm_iot_event_handler(ota_handler, event);
                QM_CLOUD_LOGE(LOG_TAG, "ota write failed:%d!!", event->data.data_write.code);
            #if 0
                qm_post_delayed_action(&ota_handler->retry_work, qm_iot_ota_retry_timeout, ota_handler, 0);
            #endif
            }

        break;

        case QM_IOT_OTA_EVENT_PROGRESS_UPADTE:

            if(event->data.progress_update.progress_details == ota_handler->progress_details){
                break;
            }

            if(event->data.progress_update.progress_details != QM_IOT_OTA_UPDATA_COMPLETE){
                status = QM_IOT_JOBS_INPROGRESS;
            }else{
                ret = qm_iot_crypto_verify_finish(ota_handler->crypto_handle,
                                                    ota_handler->signature,
                                                    ota_handler->signatureLen,
                                                    ota_handler->pucSignature,
                                                    strlen(ota_handler->pucSignature));
                if(ret != QM_EOK){
                    status = QM_IOT_JOBS_FAILED;
                    event->type = QM_IOT_OTA_EVENT_FAILED;
                    qm_iot_event_handler(ota_handler, event);
                    QM_CLOUD_LOGE(LOG_TAG, "signature_verification failed:%d!!", ret);
                    return ;
                }
                status = QM_IOT_JOBS_SUCCESSED;

            }

            ota_handler->retry_count = 0;


            if( (event->data.progress_update.progress_details / CONFIG_QM_IOT_OTA_PROGRESS_GRADE) &&
                !(event->data.progress_update.progress_details % CONFIG_QM_IOT_OTA_PROGRESS_GRADE))
            {
                qm_snprintf(client_token, 32, "%u", qm_time(NULL));
                qm_snprintf(str_progress, QM_IOT_OTA_PROGRESS_STR_NUM, "%d%%", event->data.progress_update.progress_details);
                qm_iot_jobs_update(ota_handler->did, strlen(ota_handler->did), ota_handler->jobs_id, strlen(ota_handler->jobs_id),
                                    out_topic, TOPIC_MAX_STR_LEN, NULL);

                request_len = qm_iot_jobs_update_msg(status, str_progress, strlen(str_progress),
                                                    NULL, 0,
                                                    client_token, strlen(client_token),
                                                    request_data, QM_IOT_STREAMS_REQUEST_BUFFER_SIZE);
                if(request_len <= 0){
                    break;
                }

                QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "publish topic_len=%lu payload_len=%d",
                              (unsigned long)strlen(out_topic), request_len);

                ota_handler->progress_details = event->data.progress_update.progress_details;
                qm_iot_mqtt_pub(ota_handler->mqtt_handle, out_topic, (uint8_t *)request_data, request_len, QM_MQTT_QoS0);

                ota_handler->msg_id++;

                (void)qm_iot_up_report(ota_handler->registered_iot_handle,
                                    "otaProgress", ota_handler->notify_msg_id, request_data, strlen(request_data));
            }

            if(ota_handler->event_handler){
                ota_handler->event_handler(ota_handler, event, ota_handler->user_data);
            }

            if(event->data.progress_update.code != QM_IOT_CODE_SUCCESS){
                status = QM_IOT_JOBS_FAILED;
                event->type = QM_IOT_OTA_EVENT_FAILED;
                qm_iot_event_handler(ota_handler, event);
            }else{

                if(status == QM_IOT_JOBS_SUCCESSED){
                    qm_cancel_delayed_action(&ota_handler->ota_success_work);//防止多次调用

                    ota_handler->job_update_pub_notify = QM_TRUE;

                    if(ota_handler->ota_type != QM_IOT_OTA_TYPE_MOUDLE)
                    {
                        qm_iot_ota_success_action((void *)ota_handler);
                    }else{
                        qm_post_delayed_action(&ota_handler->ota_success_work, qm_iot_ota_success_action, ota_handler, 1000);
                    }
                }

            }
        break;

        case QM_IOT_OTA_EVENT_SUCCESS:
            QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_OTA, "qm_ota complete");

            qm_cancel_delayed_action(&ota_handler->retry_work);
            qm_cancel_delayed_action(&ota_handler->ota_success_work);//防止多次调用

            qm_iot_mqtt_unsub(ota_handler->mqtt_handle, ota_handler->streams_context.topicStreamData);

            if(ota_handler->event_handler){
                ota_handler->event_handler(ota_handler, event, ota_handler->user_data);
            }

            if(ota_handler->ota_type == QM_IOT_OTA_TYPE_MOUDLE)
            {
                qm_ota_end(NULL);
            }
            ota_handler->job_update_pub_notify = QM_FALSE;
        break;

        case QM_IOT_OTA_EVENT_FAILED:
            QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_OTA, "qm_ota failed!!");

            ota_handler->job_update_pub_notify = QM_FALSE;
            qm_cancel_delayed_action(&ota_handler->retry_work);
            qm_cancel_delayed_action(&ota_handler->ota_success_work);//防止多次调用

            qm_iot_mqtt_unsub(ota_handler->mqtt_handle, ota_handler->streams_context.topicStreamData);


            qm_snprintf(client_token, 32, "%d", qm_now_ms());
            qm_snprintf(str_progress, QM_IOT_OTA_PROGRESS_STR_NUM, "%d%%", ota_handler->progress_details);
            qm_iot_jobs_update(ota_handler->did, strlen(ota_handler->did), ota_handler->jobs_id, strlen(ota_handler->jobs_id),
                                out_topic, TOPIC_MAX_STR_LEN, NULL);

            status = QM_IOT_JOBS_FAILED;
            request_len = qm_iot_jobs_update_msg(status, str_progress, strlen(str_progress),
                                                NULL, 0,
                                                client_token, strlen(client_token),
                                                request_data, QM_IOT_STREAMS_REQUEST_BUFFER_SIZE);
            if(request_len <= 0){
                break;
            }

            QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "publish topic_len=%lu payload_len=%d",
                          (unsigned long)strlen(out_topic), request_len);

            ota_handler->progress_details = event->data.progress_update.progress_details;
            qm_iot_mqtt_pub(ota_handler->mqtt_handle, out_topic, (uint8_t *)request_data, request_len, QM_MQTT_QoS0);

            if(ota_handler->event_handler){
                ota_handler->event_handler(ota_handler, event, ota_handler->user_data);
            }

        break;

        default:
        break;
    }

    event_type = event->type;
}

/*
{
    "f": 0,
    "i": 0,
    "l": 8,
    "p": "MTIzMTIzMTI="
}
*/

static qm_iot_streams_status_t ota_progress_data_match(int fileId, int blockId, int blockSize, int lastReceivedblockId)
{
    qm_iot_streams_status_t status = QM_IOT_STREAMS_STATUS_Success;

    // 验证参数范围
    if(fileId < 0 || blockId < 0 || blockSize < 0) {
        QM_CLOUD_LOGE(LOG_TAG, "Invalid parameters: fileId=%d, blockId=%d, blockSize=%d", fileId, blockId, blockSize);
        return QM_IOT_STREAMS_STATUS_DataDecodingFailed;
    }

    if( fileId != g_ota_handle->documentFields.fileId){
        /* Error - the file ID doesn't match with the one we received in the job document. */
        QM_CLOUD_LOGE(LOG_TAG, "ota fileId failed %d, expected %d", fileId, g_ota_handle->documentFields.fileId);
        status = QM_IOT_STREAMS_STATUS_DataDecodingFailed;
    }else if( blockSize > CONFIG_QM_IOT_STREAMS_BLOCK_SIZE ){
        /* Error - the block size doesn't match with what we requested. It can be smaller as
            * the last block may or may not be of exact size. */
        QM_CLOUD_LOGE(LOG_TAG, "ota blockSize failed %d, max allowed %d", blockSize, CONFIG_QM_IOT_STREAMS_BLOCK_SIZE);
        status = QM_IOT_STREAMS_STATUS_DataDecodingFailed;
    }else if( blockId <= lastReceivedblockId ){
        /* Ignore this block. */
        QM_CLOUD_LOGE(LOG_TAG, "ota blockId failed now %d last %d", blockId, lastReceivedblockId);
        status = QM_IOT_STREAMS_STATUS_DataDecodingFailed;
    }

    return status;
}

static void qm_iot_ota_update_progress_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    int fileId;
    int blockId;
    int blockSize;
    int progress_details;
    int decodedDataLength = 0;
    qm_iot_ota_event_t event = {0};
    static uint8_t decodedData[ CONFIG_QM_IOT_STREAMS_BLOCK_SIZE ] = {0};
    int result = -1;
    qm_iot_steams_context_t *streams_context = NULL;

    if(g_ota_handle == NULL || handle == NULL || packet == NULL){
        return ;
    }

    streams_context = &g_ota_handle->streams_context;
    memset(decodedData, 0, CONFIG_QM_IOT_STREAMS_BLOCK_SIZE);
    if(g_ota_handle->documentFields.fileSize == 0){
        return ;
    }

    result = (int)qm_iot_streams_recviced_datablock(streams_context,packet->data.pub.payload, packet->data.pub.payload_len,
                                                &fileId, &blockId, &blockSize, decodedData, &decodedDataLength );

    if( result != QM_IOT_STREAMS_STATUS_Success ){
        QM_CLOUD_LOGE(LOG_TAG, "ota status failed %d",result);
        goto __next;
    }

    result = ota_progress_data_match(fileId, blockId, blockSize, g_ota_handle->lastReceivedblockId);
    if( result != QM_IOT_STREAMS_STATUS_Success ){
        QM_CLOUD_LOGE(LOG_TAG, "ota match failed %d",result);
        goto __next;
    }

    memset(&event, 0, sizeof(qm_iot_ota_event_t));
    event.ota_type = g_ota_handle->ota_type;
    event.type = QM_IOT_OTA_EVENT_DATA_INSTALLING;
    event.data.data_write.id = blockId;
    event.data.data_write.ota_data = decodedData;
    event.data.data_write.data_len = (uint16_t)decodedDataLength;
    qm_iot_event_handler(g_ota_handle, &event);

    if(event.data.data_write.code != QM_IOT_CODE_SUCCESS)
    {
        return ;
    }

    g_ota_handle->currentBlockOffset++;
    g_ota_handle->totalBytesReceived += decodedDataLength;
    g_ota_handle->lastReceivedblockId = blockId;

__next:

    if(g_ota_handle->totalBytesReceived < g_ota_handle->documentFields.fileSize){
        memset(decodedData, 0, CONFIG_QM_IOT_STREAMS_BLOCK_SIZE);
        decodedDataLength = qm_iot_steams_create_request(QM_IOT_OTA_TYPE,
                                                        g_ota_handle->currentFileId, g_ota_handle->max_block_size,
                                                        g_ota_handle->currentBlockOffset, NUM_OF_BLOCKS_REQUESTED,
                                                        (char *)decodedData, g_ota_handle->max_block_size);

        QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD,
                      "streams update payload_len=%d", decodedDataLength);
        qm_iot_mqtt_pub(g_ota_handle->mqtt_handle, streams_context->topicGetStream, decodedData, decodedDataLength, QM_MQTT_QoS0);
    }

    progress_details = ((g_ota_handle->totalBytesReceived * 100) / g_ota_handle->documentFields.fileSize);

    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_OTA, "qm_ota write (%d/%d)progress %d", g_ota_handle->totalBytesReceived,g_ota_handle->documentFields.fileSize,progress_details);

    memset(&event, 0, sizeof(qm_iot_ota_event_t));
    event.type = QM_IOT_OTA_EVENT_PROGRESS_UPADTE;
    event.data.progress_update.progress_details = progress_details;
    qm_iot_event_handler(g_ota_handle, &event);
}

static int convertSignatureToDER(qm_iot_ota_handle_t *ota_handle, qm_iot_jobs_file_t * jobFields )
{
    int ret = QM_EOK;

    ota_handle->signatureLen = DECODE_MAX_STR_LEN;
    ret = qm_utils_base64decode(( const uint8_t * ) jobFields->signature, jobFields->signatureLen,
                            DECODE_MAX_STR_LEN, ota_handle->signature, &ota_handle->signatureLen);

    if( ret != QM_EOK ){
        return -QM_ERROR;
    }

    jobFields->signature = ( const char * ) ota_handle->signature;
    jobFields->signatureLen = ota_handle->signatureLen;

    return QM_EOK;
}

/**
 * note: 平台在设备OTA完成时会继续下方一个空josn的 topic通知
 *       此处在接收实际job文件时再进行取消 ota_success_work 和 ota_notify_work 的定时器操作
 * detail:
 *
 * 1]空jobs
 *  {
 *      "timestamp": 1726048263
 *  }
 *
 * 2]
 *{
    "timestamp": 1726047226,
    "execution": {
        "jobId": "AFR_OTA-test-jobs-for-ota-0011",
        "status": "QUEUED",
        "queuedAt": 1726047225,
        "lastUpdatedAt": 1726047225,
        "versionNumber": 1,
        "executionNumber": 1,
        "jobDocument": {
            "afr_ota": {
                "protocols": [
                    "MQTT"
                ],
                "streamname": "AFR_OTA-552bd5ea-ab85-4d37-8dea-d156f679a49c",
                "files": [
                    {
                        "filepath": "/ota/module/1.0.0",
                        "filesize": 8,
                        "fileid": 0,
                        "certfile": "/auth.pem",
                        "sig-sha256-ecdsa": "EYCI0c2isIjNajKhnxeeI0smirLkhcvrNiuvl29hMgkPmm7a0IhAPeLuyDr1MrmeLOccpwrjul1FrbhsasLcej/]2voi]",
                        "fileType": 1,
                        "versionName": "0.0.1",
                        "extra": {
                            "mcuTypeId": 1,
                            "supplierId": 1,
                            "sig-md5": "1234567890abcdef1234567890abcdef"
                        }
                    }
                ]
            }
        }
    }
}
}
 */

static void qm_iot_ota_notify_next_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    int retry = 0;
    int type = 0;
    int ret = QM_EOK;
    int job_len = 0;
    const char *job_doc = NULL;
    char *root = NULL;
    int root_len = 0;
    char *jobsid = NULL;
    int jobsid_len = 0U;
    qm_iot_ota_event_t event = {0};
    qm_iot_ota_handle_t *ota_handle = (qm_iot_ota_handle_t *)userdata;

    qm_cancel_delayed_action(&ota_handle->retry_work);
    memset(&ota_handle->documentFields, 0, sizeof(qm_iot_jobs_file_t));

    root = qm_json_get_value_by_name((char *)packet->data.pub.payload, packet->data.pub.payload_len, "execution", &root_len, &type);
    if(root == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "execution find failed!!");
        goto __error;
    }

    jobsid = qm_json_get_value_by_name((char *)root, root_len, "jobId", &jobsid_len, &type);
    if(jobsid == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "jobId find failed!!");
        goto __error;
    }

    // 添加边界检查
    if(jobsid_len >= sizeof(ota_handle->jobs_id)) {
        QM_CLOUD_LOGE(LOG_TAG, "jobId too long");
        return;
    }

    memset(ota_handle->jobs_id, 0, sizeof(ota_handle->jobs_id));
    memcpy(ota_handle->jobs_id, jobsid, jobsid_len);

    qm_iot_jobs_get_document((const char *)packet->data.pub.payload, packet->data.pub.payload_len, &job_doc, &job_len);
    if(job_doc == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "GET JOB DOC: Failed");
        goto __error;
    }
    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_OTA, "GET JOB DOC len=%d", job_len);

    ret = qm_iot_job_pop_files(job_doc, job_len, 0, &ota_handle->documentFields);
    if(ret != QM_TRUE){
        QM_CLOUD_LOGE(LOG_TAG, "GET JOB FILED: Failed");
        goto __error;
    }

    ret = convertSignatureToDER(ota_handle, &ota_handle->documentFields);
    if(ret != QM_EOK){
        QM_CLOUD_LOGE(LOG_TAG, "GET JOB signature FILED: Failed");
        goto __error;
    }

    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_OTA, "GET fileId:%d fileSize %d",ota_handle->documentFields.fileId, ota_handle->documentFields.fileSize);

    if(ota_handle->documentFields.version){
        QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_OTA, "GET version len=%d", ota_handle->documentFields.versionLen);
    }

    if(ota_handle->documentFields.signature){
        QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_OTA,
                      "GET signature len=%d", ota_handle->documentFields.signatureLen);
    }

    if(ota_handle->documentFields.filepath){
        QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_OTA, "GET filepath len=%d", ota_handle->documentFields.filepathLen);
    }

    if(ota_handle->documentFields.certfile){
        QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_OTA, "GET certfile present len=%d", ota_handle->documentFields.certfileLen);
    }

    if(ota_handle->documentFields.imageRef){
        QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_OTA, "GET imageRef len=%d", ota_handle->documentFields.imageRefLen);
    }

    if(ota_handle->documentFields.extra){
        QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_OTA, "GET extra len=%d", ota_handle->documentFields.extraLen);
    }

    qm_iot_steams_init(&ota_handle->streams_context,
                        ota_handle->documentFields.imageRef, ota_handle->documentFields.imageRefLen,
                        ota_handle->did, strlen(ota_handle->did),
                        QM_IOT_OTA_TYPE);

    ota_handle->currentBlockOffset = 0;
    ota_handle->totalBytesReceived = 0;
    ota_handle->ota_type = ota_handle->documentFields.fileType;
    ota_handle->currentFileId = ota_handle->documentFields.fileId;
#if 0
    qm_cancel_delayed_action(&ota_handle->ota_notify_work);  //防止多次调用
#endif
    qm_cancel_delayed_action(&ota_handle->ota_success_work); //防止多次调用


    do{
        ret = qm_iot_mqtt_sub_wait_ack(ota_handle->mqtt_handle, ota_handle->streams_context.topicStreamData,
                                        qm_iot_ota_update_progress_handler, QM_MQTT_QoS0);
        if(ret == QM_EOK){
            break;
        }
    }while(retry++ <CONFIG_QM_IOT_OTA_RETRY_COUNT);


    event.type = QM_IOT_OTA_EVENT_START;
    event.ota_type = ota_handle->ota_type;
    event.data.start.file_size = ota_handle->documentFields.fileSize;
    event.data.start.extra  = ota_handle->documentFields.extra;
    event.data.start.extra_len = ota_handle->documentFields.extraLen;
    event.data.start.update_version = ota_handle->documentFields.version;
    event.data.start.version_len = ota_handle->documentFields.versionLen;

    qm_iot_event_handler(ota_handle, &event);

    return;

__error:
    event.type = QM_IOT_OTA_EVENT_CANCEL;
    qm_iot_event_handler(g_ota_handle, &event);
}

/**
 * @brief 结束ota会话, 销毁实例并回收资源
 *
 * @param[in] handle 指向ota会话句柄的指针
 *
 * @return int32_t
 * @retval <QM_EOK  执行失败
 * @retval >=QM_EOK 执行成功
 *
 */
int32_t qm_iot_ota_deinit(void **handle)
{
    if(handle == NULL || *handle == NULL){
        return -QM_EINVAL;
    }

    qm_iot_ota_handle_t *ota_handle = (qm_iot_ota_handle_t *)(*handle);

    qm_mutex_lock(&g_ota_global_lock, QM_WAIT_FOREVER);

    ota_down_unregister(ota_handle);

    qm_iot_crypto_deinit(&ota_handle->crypto_handle);

    // 清理实例锁
    if(qm_mutex_is_valid(&ota_handle->ota_lock)) {
        qm_mutex_free(&ota_handle->ota_lock);
    }

    qm_free(ota_handle);

    if(g_ota_handle == ota_handle) {
        g_ota_handle = NULL;
    }

    *handle = NULL;

    qm_mutex_unlock(&g_ota_global_lock);

    return QM_EOK;
}

/**
 * @brief 创建ota会话实例, 并以默认值配置会话参数
 *
 * @return void *
 * @retval 非NULL ota实例的句柄
 * @retval NULL   初始化失败, 一般是内存分配失败导致
 *
 */
void *qm_iot_ota_init(void)
{
    // 初始化全局锁
    static bool_t global_lock_init = QM_FALSE;
    if(!global_lock_init) {
        qm_mutex_new(&g_ota_global_lock);
        global_lock_init = QM_TRUE;
    }

    qm_mutex_lock(&g_ota_global_lock, QM_WAIT_FOREVER);

    if(g_ota_handle != NULL) {
        qm_mutex_unlock(&g_ota_global_lock);
        return g_ota_handle;  // 避免重复初始化
    }

    qm_iot_ota_handle_t *ota_handle = (qm_iot_ota_handle_t *)qm_malloc(sizeof(qm_iot_ota_handle_t));
    if(ota_handle == NULL){
        qm_mutex_unlock(&g_ota_global_lock);
        return NULL;
    }

    memset(ota_handle, 0, sizeof(qm_iot_ota_handle_t));

    // 初始化实例锁
    if(qm_mutex_new(&ota_handle->ota_lock) != QM_EOK) {
        qm_free(ota_handle);
        qm_mutex_unlock(&g_ota_global_lock);
        return NULL;
    }

    ota_handle->crypto_handle = qm_iot_crypto_init();
    if(ota_handle->crypto_handle == NULL){
        qm_mutex_free(&ota_handle->ota_lock);
        qm_free(ota_handle);
        qm_mutex_unlock(&g_ota_global_lock);
        return NULL;
    }

    memset(ota_handle->did, 0, sizeof(ota_handle->did));

    g_ota_handle = ota_handle;
    qm_mutex_unlock(&g_ota_global_lock);

    return (void *)ota_handle;
}

static void qm_iot_notify_ota_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    int type = 0;
    char *params = NULL;
    int params_len = 0;
    char *payload = NULL;
    int payload_len = 0;
    int msg_id = 0;
    qm_iot_ota_event_t event = {0};
    char post_payload[PAYLOAD_MAX_STR_LEN + 1] = {0};

    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "recv ota notify payload_len=%lu",
                  (unsigned long)packet->data.pub.payload_len);

    payload = qm_json_get_value_by_name((char *)packet->data.pub.payload, packet->data.pub.payload_len, ID_KEY, &payload_len, &type);
    if(payload == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "msg_id find failed!!");
        goto __error;
    }
    msg_id = int_str_to_num(payload, payload_len);
    g_ota_handle->notify_msg_id = (uint32_t)msg_id;

    params = qm_json_get_value_by_name((char *)packet->data.pub.payload, packet->data.pub.payload_len, PARAMS_KEY, &params_len, &type);
    if(params == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "params find failed!!");
        goto __error;
    }

    payload = qm_json_get_value_by_name(params, params_len, TYPE_KEY, &payload_len, &type);
    if(payload == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "type find failed!!");
        goto __error;
    }
    event.ota_type = int_str_to_num(payload, payload_len);

    payload = qm_json_get_value_by_name(params, params_len, VESION_KEY, &payload_len, &type);
    if(payload == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "version find failed!!");
        goto __error;
    }

    event.data.ready.version_len = payload_len;
    event.data.ready.update_version = payload;

    event.type = QM_IOT_OTA_EVENT_READY;
    qm_iot_event_handler(g_ota_handle, &event);

    qm_snprintf(post_payload, PAYLOAD_MAX_STR_LEN, OTA_NOTIFY_REPLY_PAYLOAD, event.data.ready.result);
    (void)qm_iot_down_reply_ex(g_ota_handle->registered_iot_handle,
                                OTA_NOTIFY_METHOD, NULL,
                                (uint32_t)msg_id, 0,
                                post_payload, strlen(post_payload));

    return ;

__error:
    event.type = QM_IOT_OTA_EVENT_CANCEL;
    qm_iot_event_handler(g_ota_handle, &event);
}

/**
 * @brief 处理云对 @ref qm_iot_ota_version_notify 上报的 "versionReply" 应答。
 *
 * 应答为 @c {"code":0,"id":1,...}，@c code 非 0 表示本次版本上报未被云端接受。
 */
static void qm_iot_version_reply_handler(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    int type = 0;
    char *payload = NULL;
    int payload_len = 0;
    uint32_t code = 0;
    uint32_t msg_id = 0;

    (void)handle;
    (void)userdata;

    payload = qm_json_get_value_by_name((char *)packet->data.pub.payload, packet->data.pub.payload_len,
                                        ID_KEY, &payload_len, &type);
    if (payload != NULL && payload_len > 0) {
        msg_id = int_str_to_num(payload, payload_len);
    }

    payload = qm_json_get_value_by_name((char *)packet->data.pub.payload, packet->data.pub.payload_len,
                                        CODE_KEY, &payload_len, &type);
    /* int_str_to_num 不判空且 len 为 0 会越界读，先挡掉字段缺失的情况。 */
    if (payload == NULL || payload_len <= 0) {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_OTA, "version reply without code");
        return;
    }
    code = int_str_to_num(payload, payload_len);

    if (code != 0U) {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_OTA, "version reply rejected id=%u code=%u",
                      (unsigned)msg_id, (unsigned)code);
        return;
    }

    QM_CLOUD_LOGI(LOG_TAG, QM_CLOUD_LOG_OTA, "version reply ok id=%u", (unsigned)msg_id);
}

/**
 * @brief 配置ota会话
 *
 * @param[in] handle ota会话句柄
 * @param[in] option 配置选项, 更多信息请参考@ref qm_iot_ota_option_t
 * @param[in] data   配置选项数据, 更多信息请参考@ref qm_iot_ota_option_t
 *
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_ota_setopt(void *handle, qm_iot_ota_option_t option, void *data)
{
    int ret = QM_EOK;
    qm_iot_ota_handle_t *ota_handle = (qm_iot_ota_handle_t *)handle;
    if(ota_handle == NULL){
        return -QM_EINVAL;
    }
    if(data == NULL && option != QM_IOT_OTAOPT_RESET_SUB){
        return -QM_EINVAL;
    }

    switch (option)
    {
        case QM_IOT_OTAOPT_MQTT_HANDLE:
            ota_handle->mqtt_handle = data;
            ota_mqtt_sub_if_ready(ota_handle);
        break;

        case QM_IOT_OTAOPT_SINGATURE:
            ota_handle->pucSignature = (char *)data;
        break;
        case QM_IOT_OTAOPT_EVENT_HANDLER:
            ota_handle->event_handler = (qm_iot_ota_event_handler_t)data;
        break;

        case QM_IOT_OTAOPT_USERDATA:
            if (ota_handle->user_data != data) {
                ota_down_unregister(ota_handle);
            }
            ota_handle->user_data = data;
            ota_refresh_did(ota_handle, ota_iot_handle(ota_handle));
            ota_mqtt_sub_if_ready(ota_handle);
            ota_down_register_if_ready(ota_handle);
        break;

        case QM_IOT_OTAOPT_RESET_SUB:
            ota_handle->mqtt_pre_sub_done = QM_FALSE;
            ota_handle->mqtt_handle = NULL;
        break;

        default:
            ret = -QM_EINVAL;
        break;
    }

    return ret;
}

/**
 * @brief 暂停ota流程
 *
 * @param handle dynreg会话句柄
 *
 * @return int32_t
 * @retval <QM_EOK  数据接收失败
 * @retval >=QM_EOK 数据接收成功
 */
int32_t qm_iot_ota_stop(void *handle)
{
    qm_iot_ota_handle_t *ota_handle = (qm_iot_ota_handle_t *)handle;
    if(ota_handle == NULL){
        return -QM_EINVAL;
    }

    ota_handle->mqtt_pre_sub_done = QM_FALSE;
    qm_cancel_delayed_action(&g_ota_handle->retry_work);
    qm_iot_mqtt_unsub(g_ota_handle->mqtt_handle, g_ota_handle->streams_context.topicStreamData);
    return QM_EOK;
}

/**
 * @brief 发送最新ota_verison
 *
 * @param handle dynreg会话句柄
 *
 * @return int32_t
 * @retval <QM_EOK  数据接收失败
 * @retval >=QM_EOK 数据接收成功
 */
int32_t qm_iot_ota_version_notify(qm_iot_ota_handle_t *ota_handle)
{
    // decode + mcu_ver + str_ver
    static char payload[DECODE_MAX_STR_LEN + VER_MAX_STR_LEN + VER_MAX_STR_LEN + 3] = {0};

    char topic[TOPIC_MAX_STR_LEN + 1] = {0};
    char *str_ver_param = &payload[DECODE_MAX_STR_LEN + VER_MAX_STR_LEN + 2];
    char *str_version = qm_iot_version_str_get(ota_iot_handle(ota_handle));

#if CONFIG_QM_IOT_OTA_MCU_SUPPORT
    int index = 0;
    char *mcu_ver_param = &payload[DECODE_MAX_STR_LEN + 1];
    qm_iot_mcu_ver_info_t *mcu_ver_info = qm_iot_mcu_version_get();
#endif
    if(ota_handle == NULL){
        return -QM_EINVAL;
    }

    if(ota_handle->msg_id){
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_OTA, "OTA Ver retry!!");
    }

    ota_handle->msg_id++;

    memset(payload, 0, DECODE_MAX_STR_LEN + VER_MAX_STR_LEN + VER_MAX_STR_LEN + 3);


    if(!str_version)
    {
        QM_CLOUD_LOGE(LOG_TAG, "OTA Ver None!!");
        return -QM_EINVAL;
    }

    qm_snprintf(str_ver_param, VER_MAX_STR_LEN, "\"%s\":\"%s\"", OTA_VERSION_STR_INFORM, str_version);

#if CONFIG_QM_IOT_OTA_MCU_SUPPORT
    if(mcu_ver_info == NULL || !mcu_ver_info->num)
    {
        qm_snprintf(payload, DECODE_MAX_STR_LEN, OTA_VERSION_INFORM_PAYLOAD, ota_handle->msg_id, CONFIG_QM_IOT_SDK_VESRION, str_ver_param, CONFIG_QM_IOT_SLIENT_OTA_SUPPORT);
    }
    else
    {
        for (int i = 0; i < mcu_ver_info->num; i++)
        {
            if(index)
            {
                index += qm_snprintf(mcu_ver_param + index, VER_MAX_STR_LEN - index, ",");
            }

            index += qm_snprintf(mcu_ver_param + index, VER_MAX_STR_LEN - index, OTA_MCU_VERSION_INFORM_PAYLOAD,
                                    mcu_ver_info->version[i].type_id, mcu_ver_info->version[i].version, mcu_ver_info->version[i].extra);
        }

        qm_snprintf(payload, DECODE_MAX_STR_LEN, OTA_VERSION_INFORM_WITH_MCU_PAYLOAD, ota_handle->msg_id, CONFIG_QM_IOT_SDK_VESRION, str_ver_param, mcu_ver_param,CONFIG_QM_IOT_SLIENT_OTA_SUPPORT);
    }
#else
    qm_snprintf(payload, DECODE_MAX_STR_LEN, OTA_VERSION_INFORM_PAYLOAD, CONFIG_QM_IOT_SDK_VESRION, str_ver_param, CONFIG_QM_IOT_SLIENT_OTA_SUPPORT);
#endif

    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_PAYLOAD, "OTA version publish topic_len=%lu payload_len=%lu",
                  (unsigned long)strlen(OTA_VERSION_STR_INFORM), (unsigned long)strlen(payload));
    (void)qm_iot_up_report(g_ota_handle->registered_iot_handle,
                            OTA_VERSION_STR_INFORM,
                            (uint32_t)ota_handle->msg_id, payload, strlen(payload));

    return QM_EOK;
}

/**
 * @brief 启动ota流程
 *
 * @param handle dynreg会话句柄
 *
 * @return int32_t
 * @retval <QM_EOK  数据接收失败
 * @retval >=QM_EOK 数据接收成功
 */
int32_t qm_iot_ota_start(void *handle)
{
    qm_iot_ota_handle_t *ota_handle = (qm_iot_ota_handle_t *)handle;
    if(ota_handle == NULL){
        return -QM_EINVAL;
    }

    return qm_iot_ota_version_notify(handle);
}

#if CONFIG_QM_IOT_OTA_MCU_SUPPORT
int qm_iot_ota_version_report(void *handle, char *version, qm_iot_mcu_ver_info_t *ver_info)
{
    uint32_t ver_num =  0;
    qm_iot_ota_handle_t *ota_handle = (qm_iot_ota_handle_t *)handle;
    if(handle == NULL || version == NULL || ver_info == NULL){
        return -QM_EINVAL;
    }

//简单判断 "%d", "%d.%d.%d"
    qm_iot_version_str_set(version);
    qm_iot_mcu_version_set(ver_info->version, ver_info->num);

    if(ota_handle->mqtt_handle == NULL){
        return -QM_ERROR;
    }

    return qm_iot_ota_version_notify(handle);
}
#endif
