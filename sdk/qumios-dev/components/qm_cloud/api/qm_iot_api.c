#include "qm_cloud_log.h"
#include "qm.h"
#include "qm_work.h"
#include "qm_iot_api.h"
#include "qm_iot_config.h"
#include "qm_iot_dev_internal.h"
#include "qm_iot_session_internal.h"
#include "qm_spec_api.h"
#include "qm_utils_list.h"

#include "qm_kv.h"

#define LOG_TAG "IOT_SESSION"

static volatile uint32_t g_qm_cloud_log_flags = QM_CLOUD_KCONFIG_LOG_FLAGS;

uint32_t qm_cloud_get_log_flags(void)
{
    return g_qm_cloud_log_flags;
}

int qm_cloud_set_log_flags(uint32_t log_flags)
{
    if ((log_flags & ~(uint32_t)QM_CLOUD_LOG_ALL) != 0U) {
        return -QM_EINVAL;
    }
    g_qm_cloud_log_flags = log_flags;
    return QM_EOK;
}

#define CONFIG_QM_IOT_RESET_DELAY_MS            (200)
#define CONFIG_QM_IOT_RESET_FORCE_MS            (5000)
#define CONFIG_QM_IOT_TASK_STOP_WAIT_MS         (2000)

#if CONFIG_QM_IOT_OTA_SUPPORT
#include "qm_iot_ota.h"
#endif

#if CONFIG_QM_IOT_DEVMNT_SUPPORT
#include "qm_iot_dev.h"
#endif

#if CONFIG_QM_IOT_PUB_SUPPORT
#include "qm_iot_pub.h"
#include "qm_iot_shadow.h"
#define CONFIG_QM_IOT_PUB_DELAY_MS              (100)
#endif

#if CONFIG_QM_IOT_MQTT_SUPPORT
#include "qm_iot_mqtt.h"
#define QM_IOT_MQTT_DEFAULT_PORT   (8883)
#define QM_IOT_MQTT_CLIENT_ID_PARAMS       "%ld_%s" //pid_sn

#endif

static void qm_iot_event_post(qm_iot_session_t *session, qm_iot_event_t *event);
static void qm_iot_session_notify_cloud_connected(qm_iot_session_t *session);
static int qm_iot_session_task_start(qm_iot_session_t *session);
static int qm_iot_event_listener_slot_used(const qm_iot_event_listener_t *slot);
static int qm_iot_event_listener_same(const qm_iot_event_listener_t *slot,
                                      qm_iot_event_handler_t handler,
                                      void *userdata);

#if CONFIG_QM_IOT_SERVICE_SUPPORT
static int service_map_match(void *a, void *b);
#endif

#if CONFIG_QM_IOT_PUB_SUPPORT
static void qm_iot_session_pub_process(qm_iot_session_t *session);
#endif
#if CONFIG_QM_IOT_OTA_SUPPORT
static void qm_iot_session_ota_process(qm_iot_session_t *session);
#endif
static void qm_iot_unbind_notify(qm_iot_session_t *session);
#if CONFIG_QM_IOT_DEVMNT_SUPPORT
static void qm_iot_session_dev_process(qm_iot_session_t *session);
#endif
#if CONFIG_QM_IOT_MQTT_SUPPORT
static void qm_iot_session_conn_destroy(qm_iot_session_t *session);
static void qm_iot_session_mqtt_process(qm_iot_session_t *session);
static void qm_iot_session_modules_start(qm_iot_session_t *session);
static void qm_iot_session_network_update(qm_iot_session_t *session, qm_iot_network_state_t state);
#endif

static void qm_iot_session_task(void *arg)
{
    qm_iot_session_t *session = (qm_iot_session_t *)arg;

    if(session == NULL){
        return;
    }

    session->status = QM_IOT_EVENT_CONNING_CLOUD;
    qm_iot_event_notify(session, session->status);


#if CONFIG_QM_IOT_MQTT_SUPPORT
    session->use_mqtt = QM_TRUE;
    while(!session->exit)
    {
        if(!session->use_mqtt){
            (void)qm_sem_wait(&session->task_wakeup, QM_WAIT_FOREVER);
            continue;
        }
        qm_iot_session_mqtt_process(session);
    }
#endif

    session->session_task.hdl = NULL;
    (void)qm_sem_signal(&session->task_exit);
    qm_task_exit(NULL);
}

/**
 * Event base used to notify qm_iot session state changes.
 */
QM_EVENT_DEFINE_BASE(QM_IOT_EVENT);

static void qm_iot_session_event_handler(void *event_handler_arg, qm_event_base_t event_base,
                                         int32_t event_id, void *event_data, size_t event_data_size)
{
    qm_iot_event_t  event = {0};
    qm_iot_session_t *session = (qm_iot_session_t *)event_data;

    QM_UNUSED_ARG(event_base);
    QM_UNUSED_ARG(event_data);
    QM_UNUSED_ARG(event_data_size);

    event.type = (qm_iot_event_type_t)event_id;

    switch (event_id)
    {
        case QM_IOT_EVENT_NETWORK_DOWN:
        #if CONFIG_QM_IOT_MQTT_SUPPORT
            if (session->use_mqtt) {
                session->use_mqtt = QM_FALSE;
                (void)qm_sem_signal(&session->task_wakeup);
            }
            qm_iot_session_network_update(session, QM_IOT_NETWORK_DOWN);
        #endif
            qm_iot_event_post(session, &event);
        break;

        case QM_IOT_EVENT_NETWORK_UP:
        #if CONFIG_QM_IOT_MQTT_SUPPORT
            if (!session->use_mqtt) {
                session->use_mqtt = QM_TRUE;
                (void)qm_sem_signal(&session->task_wakeup);
            }
            qm_iot_session_network_update(session, QM_IOT_NETWORK_UP);
        #endif
            qm_iot_event_post(session, &event);
            (void)qm_iot_session_task_start(session);
        break;

        case QM_IOT_EVENT_CLOUD_CONN:
        case QM_IOT_EVENT_CONNING_CLOUD:
        #if CONFIG_QM_IOT_MQTT_SUPPORT
            session->use_mqtt = QM_TRUE;
        #endif
            qm_iot_event_post(session, &event);
        break;

        case QM_IOT_EVENT_CLOUD_DISCONN:
            qm_iot_event_post(session, &event);
        break;

        default:
            qm_iot_event_post(session, &event);
            break;
    }
}

static int qm_iot_session_task_start(qm_iot_session_t *session)
{
    int ret = QM_EOK;

    if(session == NULL){
        return -QM_EINVAL;
    }

    if(!session->started || session->network_state != QM_IOT_NETWORK_UP){
        return QM_EOK;
    }

    if(!session->session_task.hdl){
        ret = qm_task_new(&session->session_task,
                          "session_task",
                          qm_iot_session_task,
                          (void *)session,
                          CONFIG_QM_IOT_TASK_STACK_SIZE,
                          CONFIG_QM_IOT_TASK_PRIORITY);
        if(ret != QM_EOK){
            QM_CLOUD_LOGE(LOG_TAG, "session task create failed");
        } else {
            session->task_started = QM_TRUE;
        }
    }else{
    #if CONFIG_QM_IOT_MQTT_SUPPORT
        session->use_mqtt = QM_TRUE;
    #endif
    }

    return ret;
}

int32_t qm_iot_deinit(void **handle)
{
    qm_iot_session_t *session;
    qm_bool task_running;
    int ret;

    if (handle == NULL || *handle == NULL) {
        return -QM_EINVAL;
    }

    session = (qm_iot_session_t *)(*handle);
    QM_CLOUD_LOGI(LOG_TAG, QM_CLOUD_LOG_LIFECYCLE, "deinit begin");

    /* The session task owns the MQTT processing stack and may still be
     * executing when the caller starts tearing down the session.  Signal it
     * first and do not free the session object until it has acknowledged
     * exit. */
    session->exit = QM_TRUE;
    session->started = QM_FALSE;
    (void)qm_sem_signal(&session->task_wakeup);
    task_running = session->task_started;
#if CONFIG_QM_IOT_MQTT_SUPPORT
    session->use_mqtt = QM_FALSE;
    qm_iot_session_conn_destroy(session);
#endif
    if (task_running) {
        ret = qm_sem_wait(&session->task_exit, CONFIG_QM_IOT_TASK_STOP_WAIT_MS);
        if (ret != QM_EOK) {
            QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_LIFECYCLE,
                    "deinit deferred: session task still running ret=%d", ret);
            return -QM_ETIMEOUT;
        }
    }

#if CONFIG_QM_IOT_OTA_SUPPORT
    if (session->ota_handle != NULL) {
        qm_iot_ota_deinit(&session->ota_handle);
    }
#endif

#if CONFIG_QM_IOT_DEVMNT_SUPPORT
    if (session->dev_handle != NULL) {
        qm_iot_dev_deinit(&session->dev_handle);
    }
#endif

#if CONFIG_QM_IOT_SERVICE_SUPPORT
    if (session->service != NULL) {
        qm_list_destroy(session->service);
        session->service = NULL;
    }
#endif

    if (session->prodtuct_sercet != NULL) {
        qm_free(session->prodtuct_sercet);
        session->prodtuct_sercet = NULL;
    }
    if (session->server_crt != NULL) {
        qm_free(session->server_crt);
        session->server_crt = NULL;
        session->server_crt_len = 0;
    }
    if (session->client_crt != NULL) {
        qm_free(session->client_crt);
        session->client_crt = NULL;
        session->client_crt_len = 0;
    }
    if (session->client_key != NULL) {
        qm_free(session->client_key);
        session->client_key = NULL;
        session->client_key_len = 0;
    }
    if (session->ota_public_key != NULL) {
        qm_free(session->ota_public_key);
        session->ota_public_key = NULL;
        session->ota_public_key_len = 0;
    }

    if (qm_mutex_is_valid(&session->lock)) {
        qm_mutex_free(&session->lock);
    }

    if (qm_sem_is_valid(&session->task_wakeup)) {
        qm_sem_free(&session->task_wakeup);
    }
    if (qm_sem_is_valid(&session->task_exit)) {
        qm_sem_free(&session->task_exit);
    }

    qm_free(session);
    *handle = NULL;
    QM_CLOUD_LOGI(LOG_TAG, QM_CLOUD_LOG_LIFECYCLE, "deinit done");
    return QM_EOK;
}

void *qm_iot_init(void)
{
    int ret = QM_EOK;
    qm_iot_session_t *session = (qm_iot_session_t *)qm_malloc(sizeof(qm_iot_session_t));
    if(session == NULL){
        return NULL;
    }
    memset(session, 0, sizeof(qm_iot_session_t));

    session->status = QM_IOT_EVENT_NONE;

    qm_mutex_new(&session->lock);
    ret = qm_sem_new(&session->task_wakeup, 0);
    if (ret != QM_EOK) {
        goto __error;
    }
    ret = qm_sem_new(&session->task_exit, 0);
    if (ret != QM_EOK) {
        goto __error;
    }

 #if CONFIG_QM_IOT_SERVICE_SUPPORT
    session->service = qm_list_new();
    if(session->service == NULL){
        goto __error;
    }
    session->service->match = service_map_match;
#endif

#if CONFIG_QM_IOT_DEVMNT_SUPPORT
    session->dev_handle = qm_iot_dev_init();
    if(session->dev_handle == NULL){
        goto __error;
    }
#endif

#if CONFIG_QM_IOT_OTA_SUPPORT
    session->ota_handle = qm_iot_ota_init();
    if(session->ota_handle == NULL){
        goto __error;
    }
#endif

    ret = qm_event_handler_register(QM_IOT_EVENT, QM_EVENT_ANY_ID,
                                         qm_iot_session_event_handler, NULL);
    if(ret != QM_EOK){
        goto __error;
    }

    QM_CLOUD_LOGI(LOG_TAG, QM_CLOUD_LOG_LIFECYCLE, "init done");

    return session;

__error:

    if (session != NULL && qm_sem_is_valid(&session->task_exit)) {
        qm_sem_free(&session->task_exit);
    }
    if (session != NULL && qm_sem_is_valid(&session->task_wakeup)) {
        qm_sem_free(&session->task_wakeup);
    }

#if CONFIG_QM_IOT_OTA_SUPPORT
    if(session != NULL && session->ota_handle != NULL){
        qm_iot_ota_deinit(&session->ota_handle);
    }
#endif
#if CONFIG_QM_IOT_DEVMNT_SUPPORT
    if(session != NULL && session->dev_handle != NULL){
        qm_iot_dev_deinit(&session->dev_handle);
    }
#endif

    if(qm_mutex_is_valid(&session->lock)) {
        qm_mutex_free(&session->lock);
    }

 #if CONFIG_QM_IOT_SERVICE_SUPPORT
    if(session != NULL && session->service != NULL){
        qm_list_destroy(session->service);
        session->service = NULL;
    }
#endif

    if(session){
        qm_free(session);
    }

    return NULL;
}

int32_t qm_iot_setopt(void *handle, qm_iot_option_t option, void *data)
{
    int ret = QM_EOK;
    qm_iot_session_t *session = (qm_iot_session_t *)handle;
    if(session == NULL || data == NULL){
        return -QM_EINVAL;
    }

    switch (option)
    {
        case QM_IOT_OPT_VERSION_STR:
            ret = qm_iot_version_str_set(handle, (char *)data);
        break;

        case QM_IOT_OPT_PRODUCT_ID:
            ret = qm_iot_pid_set(handle, *((uint32_t *)data));
        break;

        case QM_IOT_OPT_PRODUCT_SECRET:
            ret = qm_iot_prodtuct_sercet_set(handle, (char *)data, strlen((char *)data));
        break;

        case QM_IOT_OPT_SN:
            ret = qm_iot_sn_set(handle, (char *)data, strlen((char *)data));
        break;

        case QM_IOT_OPT_RECV_HANDLER:
            session->recv_handler = (qm_iot_recv_handler_t)data;
        break;

        case QM_IOT_OPT_EVENT_HANDLER:
            session->event_handler = (qm_iot_event_handler_t)data;
        break;

        case QM_IOT_OPT_DEVINFO:
        {
            qm_iot_devinfo_t *info = (qm_iot_devinfo_t *)data;
            if(info->type <= QM_IOT_NETWORK_TYPE_NONE){
                ret = -QM_EINVAL;
                break;
            }

            memcpy(&session->devinfo, info, sizeof(session->devinfo));
            session->has_devinfo = QM_TRUE;
        }
        break;

        case QM_IOT_OPT_USERDATA:
            session->user_data = (void *)data;
        break;

        case QM_IOT_OPT_CLOUD_CREDENTIAL:
        {
            qm_iot_cloud_auth_t *credential = (qm_iot_cloud_auth_t *)data;
            if(credential->did == 0 ||
               credential->cloud_info.host[0] == '\0' ||
               credential->cloud_info.port == 0 ||
               credential->server_cert == NULL ||
               credential->server_cert_len == 0 ||
               credential->client_crt == NULL ||
               credential->client_crt_len == 0 ||
               credential->client_key == NULL ||
               credential->client_key_len == 0 ||
               credential->ota_public_key == NULL ||
               credential->ota_public_key_len == 0) {
                ret = -QM_EINVAL;
                break;
            }

            ret = qm_iot_did_set(handle, credential->did);
            if(ret != QM_EOK) {
                break;
            }
            ret = qm_iot_cloud_info_set(handle, &credential->cloud_info);
            if(ret != QM_EOK) {
                break;
            }
            ret = qm_iot_server_crt_set(handle,
                                        credential->server_cert,
                                        credential->server_cert_len);
            if(ret != QM_EOK) {
                break;
            }
            ret = qm_iot_client_crt_set(handle,
                                        credential->client_crt,
                                        credential->client_crt_len);
            if(ret != QM_EOK) {
                break;
            }
            ret = qm_iot_client_key_set(handle,
                                       credential->client_key,
                                       credential->client_key_len);
            if(ret != QM_EOK) {
                break;
            }
            ret = qm_iot_ota_public_key_set(handle,
                                            credential->ota_public_key,
                                            credential->ota_public_key_len);
            if(ret != QM_EOK) {
                break;
            }
        }
        break;

        default:
            ret = -QM_EINVAL;
        break;
    }
    return ret;
}

int32_t qm_iot_start(void *handle)
{
    uint32_t did = 0;
    int ret = QM_EOK;
    uint32_t len = 0;

    qm_iot_session_t *session = (qm_iot_session_t *)handle;
    if(session == NULL){
        return -QM_EINVAL;
    }
    QM_CLOUD_LOGI(LOG_TAG, QM_CLOUD_LOG_LIFECYCLE, "start begin");

    memset(&session->crt_handle, 0, sizeof(qm_iot_credential_cache_t));
#if CONFIG_QM_IOT_MQTT_SUPPORT
    session->crt_handle.cloud_info.port = QM_IOT_MQTT_DEFAULT_PORT;
#endif

    ret = qm_iot_reset_info_get(handle, &session->reset_info);
    QM_CLOUD_LOGI(LOG_TAG, QM_CLOUD_LOG_CONTROL, "reset_info.is_reset=%d,%d", ret, session->reset_info.is_reset);
    if(ret != QM_EOK) {
        session->reset_info.is_reset = QM_IOT_RESET_UNPROV;
    }

#if CONFIG_QM_IOT_MQTT_SUPPORT
   did = qm_iot_did_get(handle);
    if(did <= 0){
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_AUTH, "DID GET FAILED!!");
        return -QM_EINVAL;
    }

    ret = qm_iot_cloud_info_get(handle, &session->crt_handle.cloud_info);
    if(ret != QM_EOK){
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_AUTH, "CLOUD GET FAILED!!");
        return ret;
    }
    QM_CLOUD_LOGI(LOG_TAG, QM_CLOUD_LOG_LIFECYCLE, "cloud credential summary pid=%u did=%u host=%s host_len=%u port=%u",
            (unsigned int)qm_iot_pid_get(handle),
            (unsigned int)did,
            session->crt_handle.cloud_info.host,
            (unsigned int)strlen(session->crt_handle.cloud_info.host),
            (unsigned int)session->crt_handle.cloud_info.port);

    len = CONFIG_QM_IOT_CRT_MAX_LEN;
    ret = qm_iot_server_crt_get(handle, session->crt_handle.ca_crt, &len);
    if(ret != QM_EOK || len == 0){
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_AUTH, "ROOT CA GET FAILED!!");
        return ret != QM_EOK ? ret : -QM_EINVAL;
    }
    QM_CLOUD_LOGI(LOG_TAG, QM_CLOUD_LOG_AUTH, "cloud credential root ca len=%u", (unsigned int)len);

    len = CONFIG_QM_IOT_CRT_MAX_LEN;
    ret = qm_iot_client_crt_get(handle, session->crt_handle.client_crt, &len);
    if(ret != QM_EOK || len == 0){
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_AUTH, "CLIENT CA GET FAILED!!");

        return ret != QM_EOK ? ret : -QM_EINVAL;
    }

    len = CONFIG_QM_IOT_CRT_MAX_LEN;
    ret = qm_iot_client_key_get(handle, session->crt_handle.client_key, &len);
    if(ret != QM_EOK || len == 0){
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_AUTH, "CLIENT KEY GET FAILED!!");

        return ret != QM_EOK ? ret : -QM_EINVAL;
    }


    len = CONFIG_QM_IOT_OTA_CRT_MAX_LEN;
    ret = qm_iot_ota_public_key_get(handle, session->crt_handle.ota_signatrure, &len);
    if(ret != QM_EOK || len == 0){
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_AUTH, "PUBLIC KEY GET FAILED!!");

        return ret != QM_EOK ? ret : -QM_EINVAL;
    }
    QM_CLOUD_LOGI(LOG_TAG, QM_CLOUD_LOG_AUTH, "cloud credential ota public key len=%u", (unsigned int)len);
#endif

    session->started = QM_TRUE;

    QM_CLOUD_LOGI(LOG_TAG, QM_CLOUD_LOG_LIFECYCLE, "start done");

    return QM_EOK;
}

int32_t qm_iot_stop(void *handle)
{
    qm_iot_session_t *session = (qm_iot_session_t *)handle;
    if(session == NULL){
        return -QM_EINVAL;
    }

    QM_CLOUD_LOGI(LOG_TAG, QM_CLOUD_LOG_LIFECYCLE, "stop begin");

    session->started = QM_FALSE;

#if CONFIG_QM_IOT_MQTT_SUPPORT
    session->use_mqtt = QM_FALSE;
    qm_iot_session_conn_destroy(session);
#endif

    QM_CLOUD_LOGI(LOG_TAG, QM_CLOUD_LOG_LIFECYCLE, "stop done");
    return QM_EOK;
}

int32_t qm_iot_network_update(void *handle, qm_iot_network_state_t state)
{
    qm_iot_session_t *session = (qm_iot_session_t *)handle;
    if(session == NULL){
        return -QM_EINVAL;
    }

    QM_CLOUD_LOGI(LOG_TAG, QM_CLOUD_LOG_LIFECYCLE, "network update state=%d", state);

    switch(state){
        case QM_IOT_NETWORK_UP:
            session->network_state = QM_IOT_NETWORK_UP;
#if CONFIG_QM_IOT_MQTT_SUPPORT
            if (!session->use_mqtt) {
                session->use_mqtt = QM_TRUE;
                (void)qm_sem_signal(&session->task_wakeup);
            }
            qm_iot_session_network_update(session, QM_IOT_NETWORK_UP);
#endif
            session->status = QM_IOT_EVENT_NETWORK_UP;
            qm_iot_event_notify(session, QM_IOT_EVENT_NETWORK_UP);
        break;

        case QM_IOT_NETWORK_DOWN:
            session->network_state = QM_IOT_NETWORK_DOWN;
#if CONFIG_QM_IOT_MQTT_SUPPORT
            if (session->use_mqtt) {
                session->use_mqtt = QM_FALSE;
                (void)qm_sem_signal(&session->task_wakeup);
            }
            qm_iot_session_network_update(session, QM_IOT_NETWORK_DOWN);
#endif
            session->status = QM_IOT_EVENT_NETWORK_DOWN;
            qm_iot_event_notify(session, QM_IOT_EVENT_NETWORK_DOWN);
        break;

        default:
            return -QM_EINVAL;
    }

    return QM_EOK;
}

int32_t qm_iot_report(void *handle, qm_spec_property_operation_t *property_operation)
{
#if CONFIG_QM_IOT_PUB_SUPPORT
    qm_iot_pub_msg_t pub_msg = {0};
    qm_iot_session_t *session = (qm_iot_session_t *)handle;
    if(session == NULL){
        return -QM_EINVAL;
    }
    if(property_operation == NULL || session->pub_handle == NULL){
        return -QM_EINVAL;
    }
    pub_msg.did = qm_iot_did_get(handle);
    pub_msg.type =  QM_IOT_PUBMSG_SHADOW_UPDATE;
    pub_msg.data.update.property_operation = property_operation;
    return qm_iot_pub_send(session->pub_handle, &pub_msg, 0);
#else
    return -QM_EINVAL;
#endif
}

#if CONFIG_QM_IOT_SPEC_SUPPORT
int32_t qm_iot_property_report(void *handle, qm_spec_property_operation_t *property_operation)
{
#if CONFIG_QM_IOT_PUB_SUPPORT
    qm_iot_pub_msg_t pub_msg = {0};
    qm_iot_session_t *session = (qm_iot_session_t *)handle;
    if(session == NULL){
        return -QM_EINVAL;
    }
    if(property_operation == NULL || session->pub_handle == NULL){
        return -QM_EINVAL;
    }
    pub_msg.did = qm_iot_did_get(handle);
    pub_msg.type =  QM_IOT_PUBMSG_UPDATE;
    pub_msg.data.update.property_operation = property_operation;
    return qm_iot_pub_send(session->pub_handle, &pub_msg, 0);
#else
    return -QM_EINVAL;
#endif
}

int32_t qm_iot_property_get_request(void *handle, qm_spec_property_operation_t *property_operation)
{
#if CONFIG_QM_IOT_PUB_SUPPORT
    qm_iot_pub_msg_t pub_msg = {0};
    qm_iot_session_t *session = (qm_iot_session_t *)handle;
    if(session == NULL){
        return -QM_EINVAL;
    }
    if(property_operation == NULL || session->pub_handle == NULL){
        return -QM_EINVAL;
    }
    pub_msg.did = qm_iot_did_get(handle);
    pub_msg.type =  QM_IOT_PUBMSG_GET_REQ;
    pub_msg.data.update.property_operation = property_operation;
    return qm_iot_pub_send(session->pub_handle, &pub_msg, 0);
#else
    return -QM_EINVAL;
#endif
}

int32_t qm_iot_property_get_rsponse(void *handle, qm_spec_property_operation_t *property_operation)
{
#if CONFIG_QM_IOT_PUB_SUPPORT
    qm_iot_pub_msg_t pub_msg = {0};
    qm_iot_session_t *session = (qm_iot_session_t *)handle;
    if(session == NULL){
        return -QM_EINVAL;
    }
    if(property_operation == NULL || session->pub_handle == NULL){
        return -QM_EINVAL;
    }
    pub_msg.did = qm_iot_did_get(handle);
    pub_msg.type =  QM_IOT_PUBMSG_GET_RSP;
    pub_msg.data.update.property_operation = property_operation;
    return qm_iot_pub_send(session->pub_handle, &pub_msg, 0);
#else
    return -QM_EINVAL;
#endif
}
#endif

int32_t qm_iot_reset(void *handle)
{
    qm_iot_session_t *session = (qm_iot_session_t *)handle;
    if(session == NULL){
        return -QM_EINVAL;
    }
    qm_iot_unbind_notify(session);
    return QM_EOK;
}

int32_t qm_iot_event_notify(void *handle, qm_iot_event_type_t event)
{
    qm_iot_session_t *session = (qm_iot_session_t *)handle;
    int ret;

    if(session == NULL){
        return -QM_EINVAL;
    }

    ret = qm_event_post(QM_IOT_EVENT, event, session, 0, QM_NO_WAIT);
    if (ret != QM_EOK) {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_LIFECYCLE,
                "event post failed event=%d ret=%d", event, ret);
    }

    return ret;
}

static void qm_iot_session_notify_cloud_connected(qm_iot_session_t *session)
{
    int ret;

    if (session == NULL || session->reset_info.is_reset) {
        return;
    }

    session->status = QM_IOT_EVENT_CLOUD_CONN;
    ret = qm_iot_event_notify(session, QM_IOT_EVENT_CLOUD_CONN);
    if (ret != QM_EOK) {
        QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_LIFECYCLE,
                "cloud connected event notify failed ret=%d", ret);
    }
}

static int qm_iot_event_listener_slot_used(const qm_iot_event_listener_t *slot)
{
    return (slot != NULL && slot->handler != NULL) ? 1 : 0;
}

static int qm_iot_event_listener_same(const qm_iot_event_listener_t *slot,
                                      qm_iot_event_handler_t handler,
                                      void *userdata)
{
    if (slot == NULL || !qm_iot_event_listener_slot_used(slot)) {
        return 0;
    }

    return (slot->handler == handler && slot->userdata == userdata) ? 1 : 0;
}

int32_t qm_iot_event_register(void *handle,
                              qm_iot_event_handler_t handler,
                              void *userdata)
{
    int i = 0;
    int found = 0;
    qm_iot_session_t *session = (qm_iot_session_t *)handle;

    if (session == NULL || handler == NULL) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&session->lock, QM_WAIT_FOREVER);
    for (i = 0; i < CONFIG_QM_IOT_EVENT_LISTENER_NUM; i++) {
        if (qm_iot_event_listener_same(&session->event_listeners[i], handler, userdata)) {
            qm_mutex_unlock(&session->lock);
            return -QM_EINVAL;
        }
    }

    for (i = 0; i < CONFIG_QM_IOT_EVENT_LISTENER_NUM; i++) {
        if (!qm_iot_event_listener_slot_used(&session->event_listeners[i])) {
            session->event_listeners[i].handler = handler;
            session->event_listeners[i].userdata = userdata;
            found = 1;
            break;
        }
    }
    qm_mutex_unlock(&session->lock);

    return found ? QM_EOK : -QM_EFULL;
}

int32_t qm_iot_event_unregister_one(void *handle,
                                    qm_iot_event_handler_t handler,
                                    void *userdata)
{
    int i = 0;
    int removed = 0;
    qm_iot_session_t *session = (qm_iot_session_t *)handle;

    if (session == NULL || handler == NULL) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&session->lock, QM_WAIT_FOREVER);
    for (i = 0; i < CONFIG_QM_IOT_EVENT_LISTENER_NUM; i++) {
        if (qm_iot_event_listener_same(&session->event_listeners[i], handler, userdata)) {
            memset(&session->event_listeners[i], 0, sizeof(session->event_listeners[i]));
            removed = 1;
            break;
        }
    }
    qm_mutex_unlock(&session->lock);

    return removed ? QM_EOK : -QM_EINVAL;
}

int32_t qm_iot_event_unregister(void *handle)
{
    qm_iot_session_t *session = (qm_iot_session_t *)handle;

    if (session == NULL) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&session->lock, QM_WAIT_FOREVER);
    session->event_handler = NULL;
    memset(session->event_listeners, 0, sizeof(session->event_listeners));
    qm_mutex_unlock(&session->lock);

    return QM_EOK;
}

static void qm_iot_event_post(qm_iot_session_t *session, qm_iot_event_t *event)
{
    int i = 0;
    qm_iot_event_handler_t opt_handler = NULL;
    void *opt_userdata = NULL;
    qm_iot_event_listener_t snapshot = {0};

    if (session == NULL || event == NULL) {
        return;
    }

    qm_mutex_lock(&session->lock, QM_WAIT_FOREVER);
    opt_handler = session->event_handler;
    opt_userdata = session->user_data;
    qm_mutex_unlock(&session->lock);

    if (opt_handler != NULL) {
        opt_handler(session, event, opt_userdata);
    }

    for (i = 0; i < CONFIG_QM_IOT_EVENT_LISTENER_NUM; i++) {
        qm_mutex_lock(&session->lock, QM_WAIT_FOREVER);
        if (!qm_iot_event_listener_slot_used(&session->event_listeners[i])) {
            qm_mutex_unlock(&session->lock);
            continue;
        }

        snapshot = session->event_listeners[i];
        qm_mutex_unlock(&session->lock);
        snapshot.handler(session, event, snapshot.userdata);
    }
}

static void qm_iot_mqtt_reset_notify(void *arg)
{
    qm_iot_session_t *session = (qm_iot_session_t *)arg;

    if(session){
    #if CONFIG_QM_IOT_MQTT_SUPPORT
        qm_iot_session_conn_destroy(session);
    #endif
    }

    qm_iot_event_notify(session, QM_IOT_EVENT_RESET);
}

#if CONFIG_QM_IOT_MQTT_SUPPORT

/* Periodic maintenance after the MQTT session is ready.  This is deliberately
 * separate from QM_IOT_EVENT_CLOUD_CONN: a scheduled devInfo report is not a
 * new cloud connection. */
static void qm_iot_devinfo_report_action(void *arg)
{
    qm_iot_session_t *session = (qm_iot_session_t *)arg;
    if(session == NULL){
        return ;
    }
    qm_cancel_delayed_action(&session->request_work);

#if CONFIG_QM_IOT_DEV_INFO_SUPPORT
    if(session->dev_handle && session->has_devinfo){
        qm_iot_dev_send_devinfo(session->dev_handle, &session->devinfo);
    }
#endif

    qm_post_delayed_action(&session->request_work,
                           qm_iot_devinfo_report_action,
                           (void *)session,
                           CONFIG_QM_IOT_REQ_INTERVAL * 1000);
}

static void qm_iot_session_modules_start(qm_iot_session_t *session)
{
    qm_cancel_delayed_action(&session->request_work);

#if CONFIG_QM_IOT_MQTT_SUPPORT
    qm_iot_mqtt_pre_sub_start(session->mqtt_handle);
#endif

#if CONFIG_QM_IOT_OTA_SUPPORT
    qm_iot_ota_start(session->ota_handle);
#endif

    qm_iot_session_notify_cloud_connected(session);
    qm_post_delayed_action(&session->request_work,
                           qm_iot_devinfo_report_action,
                           (void *)session,
                           0);
}

static void qm_iot_mqtt_event_handler(void *handle, const qm_iot_mqtt_event_t *event, void *userdata)
{
    qm_iot_session_t *session = (qm_iot_session_t *)userdata;
    if(session == NULL || handle == NULL || event == NULL){
        return;
    }
    switch (event->type)
    {
        case QM_IOT_MQTTEVT_CONNECT:
        #if CONFIG_QM_IOT_DEVMNT_SUPPORT
            qm_iot_session_dev_process(session);
        #endif

        #if CONFIG_QM_IOT_PUB_SUPPORT
            qm_iot_session_pub_process(session);
        #endif

        #if CONFIG_QM_IOT_OTA_SUPPORT
            qm_iot_session_ota_process(session);
        #endif

            qm_iot_session_modules_start(session);

        break;

        case QM_IOT_MQTTEVT_DISCONNECT:
            qm_cancel_delayed_action(&session->request_work);
            session->status = QM_IOT_EVENT_CLOUD_DISCONN;
            qm_iot_event_notify(session, QM_IOT_EVENT_CLOUD_DISCONN);
        break;

        case QM_IOT_MQTTEVT_RECONNECT:
            qm_iot_session_modules_start(session);
        break;

        default:
        break;
    }
}

static void qm_iot_session_conn_destroy(qm_iot_session_t *session)
{
    void *mqtt_handle;

    if (session == NULL) {
        return;
    }
    qm_mutex_lock(&session->lock, QM_WAIT_FOREVER);
    mqtt_handle = session->mqtt_handle;
    qm_mutex_unlock(&session->lock);
    if (mqtt_handle == NULL) {
        return;
    }
    qm_cancel_delayed_action(&session->request_work);
#if CONFIG_QM_IOT_OTA_SUPPORT
    if (session->ota_handle != NULL) {
        qm_iot_ota_stop(session->ota_handle);
    }
#endif
    session->use_mqtt = QM_FALSE;
    qm_mutex_lock(&session->lock, QM_WAIT_FOREVER);
    if (session->mqtt_handle == mqtt_handle) {
        (void)qm_iot_mqtt_deinit(&session->mqtt_handle);
        session->mqtt_handle = NULL;
    }
    qm_mutex_unlock(&session->lock);
}

static void qm_iot_session_network_update(qm_iot_session_t *session, qm_iot_network_state_t state)
{
    void *mqtt_handle;

    if (session == NULL) {
        return;
    }
    qm_mutex_lock(&session->lock, QM_WAIT_FOREVER);
    mqtt_handle = session->mqtt_handle;
    if (mqtt_handle != NULL) {
        (void)qm_iot_mqtt_network_update(mqtt_handle, state);
    }
    qm_mutex_unlock(&session->lock);
}

static void qm_iot_session_mqtt_process(qm_iot_session_t *session)
{
    qm_iot_mqtt_network_cred_t net_cred = {0};
    void *mqtt_handle;
    uint32_t pid;
    uint8_t clear_session = QM_TRUE;
    uint32_t repub_timeout_ms = CONFIG_QM_IOT_MQTT_REPUB_MS;

    uint16_t keeplive = CONFIG_QM_IOT_MQTT_KEEPALIVE_S;
    char str_did[CONFIG_QM_IOT_MQTT_CLIENT_ID_LEN + 1] = {0};

    mqtt_handle = qm_iot_mqtt_init();
    if(mqtt_handle == NULL){
        return;
    }

    qm_mutex_lock(&session->lock, QM_WAIT_FOREVER);
    if (session->exit) {
        void *stop_handle = mqtt_handle;
        qm_mutex_unlock(&session->lock);
        (void)qm_iot_mqtt_deinit(&stop_handle);
        (void)qm_iot_mqtt_process(mqtt_handle);
        return;
    }
    session->mqtt_handle = mqtt_handle;
    qm_mutex_unlock(&session->lock);

    net_cred.server_crt = session->crt_handle.ca_crt;
    net_cred.client_crt = session->crt_handle.client_crt;
    net_cred.client_privkey = session->crt_handle.client_key;

    pid = qm_iot_pid_get(session);
    snprintf(str_did, CONFIG_QM_IOT_MQTT_CLIENT_ID_LEN, QM_IOT_MQTT_CLIENT_ID_PARAMS, pid, qm_iot_sn_get(session));

    qm_iot_mqtt_setopt(mqtt_handle, QM_IOT_MQTTOPT_HOST, session->crt_handle.cloud_info.host);
    qm_iot_mqtt_setopt(mqtt_handle, QM_IOT_MQTTOPT_PORT, &session->crt_handle.cloud_info.port);
    qm_iot_mqtt_setopt(mqtt_handle, QM_IOT_MQTTOPT_CLIENTID, str_did);
    qm_iot_mqtt_setopt(mqtt_handle, QM_IOT_MQTTOPT_KEEPALIVE_SEC, &keeplive);
    qm_iot_mqtt_setopt(mqtt_handle, QM_IOT_MQTTOPT_NETWORK_CRED, &net_cred);
    qm_iot_mqtt_setopt(mqtt_handle, QM_IOT_MQTTOPT_REPUB_TIMEOUT_MS, &repub_timeout_ms);
    qm_iot_mqtt_setopt(mqtt_handle, QM_IOT_MQTTOPT_USERDATA, (void *)session);
    qm_iot_mqtt_setopt(mqtt_handle, QM_IOT_MQTTOPT_CLEAN_SESSION, &clear_session);
    qm_iot_mqtt_setopt(mqtt_handle, QM_IOT_MQTTOPT_EVENT_HANDLER, qm_iot_mqtt_event_handler);

    qm_iot_mqtt_process(mqtt_handle);

    qm_mutex_lock(&session->lock, QM_WAIT_FOREVER);
    if (session->mqtt_handle == mqtt_handle) {
        session->mqtt_handle = NULL;
    }
    qm_mutex_unlock(&session->lock);
}
#endif

#if CONFIG_QM_IOT_PUB_SUPPORT

#if CONFIG_QM_IOT_SERVICE_SUPPORT
static int qm_iot_pub_recv_service_handler(qm_iot_session_t *session, qm_iot_pub_recv_t *recv)
{
    int service_find = QM_FALSE;
    qm_list_iterator_t self = {0};
    qm_list_node_t *node = NULL;
    qm_spec_property_t *spec_prop = NULL;
    qm_iot_service_entry_t *service = NULL;
    qm_iot_service_iid_t *service_iid = NULL;
    qm_spec_property_operation_t *property_operation = NULL;
    qm_iot_service_type_t service_type = QM_IOT_SERVICE_TYPE_NONE;

    if(session == NULL || recv == NULL){
        return -QM_EINVAL;
    }

    switch (recv->type)
    {
        case QM_IOT_PUBRECV_SHADOW_SET:
        case QM_IOT_PUBRECV_PROPERTY_SET:
            service_type = QM_IOT_SERVICE_TYPE_SET;
            property_operation = recv->data.prop_set.operation;
        break;

        case QM_IOT_PUBRECV_PROPERTY_GET_REQ:
            service_type = QM_IOT_SERVICE_TYPE_GET_REQ;
            property_operation = recv->data.prop_get.operation;
        break;

        case QM_IOT_PUBRECV_PROPERTY_GET_RSP:
            service_type = QM_IOT_SERVICE_TYPE_GET_RSP;
            property_operation = recv->data.prop_get.operation;
        break;

        default:
            break;
    }

    if(service_type == QM_IOT_SERVICE_TYPE_NONE) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&session->lock, QM_WAIT_FOREVER);
    self.direction = LIST_HEAD;
    self.next = session->service->head;
    node = qm_list_iterator_next(&self);
    while (node)
    {

        service_find = QM_TRUE;
        service = (qm_iot_service_entry_t *)qm_list_node_val_get(node);
        service_iid = (qm_iot_service_iid_t *)service->raw_iid;

        for (int i = 0; i < service->service_num; i++)
        {
            spec_prop = qm_spec_property_find(property_operation, service_iid[i].siid, service_iid[i].piid);
            if(spec_prop == NULL) {
                service_find = QM_FALSE;
                break;
            }
        }

        if(service_find){
            break;
        }
        node = qm_list_iterator_next(&self);
    }

    if(service_find && service){
        if(service->service_fn) {
            service->service_fn(session, service->service_id,
                                service_type, property_operation, service->userdata);
        }
    }
    qm_mutex_unlock(&session->lock);

    return (service_find ? QM_EOK : -QM_ERROR);
}
#endif

static int qm_iot_pub_recv_comm_handler(qm_iot_session_t *session, qm_iot_pub_recv_t *recv)
{
    qm_iot_recv_t iot_recv = {0};
    qm_spec_property_t *spec_prop = NULL;

    iot_recv.did = recv->did;

    switch (recv->type)
    {
        case QM_IOT_PUBRECV_SHADOW_SET:
    #if CONFIG_QM_IOT_SPEC_SUPPORT
        case QM_IOT_PUBRECV_PROPERTY_SET:
    #endif
            while (1)
            {
                spec_prop = qm_spec_property_next(recv->data.prop_set.operation, spec_prop);
                if(spec_prop == NULL){
                    break;
                }
                iot_recv.type = QM_IOT_RECV_TYPE_PROPERTY_SET;
                iot_recv.did = spec_prop->did;
                iot_recv.siid = spec_prop->siid;
                iot_recv.piid = spec_prop->piid;
                iot_recv.data.set.property = spec_prop;
                if(session->recv_handler){
                    session->recv_handler(session, &iot_recv, session->user_data);
                }
            }
        break;
     #if CONFIG_QM_IOT_SPEC_SUPPORT
        case QM_IOT_PUBRECV_PROPERTY_GET_RSP:
            while (1)
            {
                spec_prop = qm_spec_property_next(recv->data.prop_get.operation, spec_prop);
                if(spec_prop == NULL){
                    break;
                }
                iot_recv.type = QM_IOT_RECV_TYPE_PROPERTY_GET_RSP;
                iot_recv.did = spec_prop->did;
                iot_recv.siid = spec_prop->siid;
                iot_recv.piid = spec_prop->piid;
                iot_recv.data.get_rsp.property = spec_prop;
                if(session->recv_handler){
                    session->recv_handler(session, &iot_recv, session->user_data);
                }
            }
        break;

        case QM_IOT_PUBRECV_PROPERTY_GET_REQ:
            while (1)
            {
                spec_prop = qm_spec_property_next(recv->data.prop_get.operation, spec_prop);
                if(spec_prop == NULL){
                    break;
                }
                iot_recv.type = QM_IOT_RECV_TYPE_PROPERTY_GET_REQ;
                iot_recv.did = spec_prop->did;
                iot_recv.siid = spec_prop->siid;
                iot_recv.piid = spec_prop->piid;
                iot_recv.data.get_req.property = spec_prop;
                if(session->recv_handler){
                    session->recv_handler(session, &iot_recv, session->user_data);
                }
            }
        break;
    #endif
        default:
        break;
    }

    return QM_EOK;
}


static void qm_iot_pub_recv_handler(void *handle, qm_iot_pub_recv_t *recv, void *userdata)
{
    int ret = QM_EOK;
    qm_iot_session_t *session = (qm_iot_session_t *)userdata;
    if(session == NULL || handle == NULL || recv == NULL){
        return;
    }

#if CONFIG_QM_IOT_SERVICE_SUPPORT
    ret = qm_iot_pub_recv_service_handler(session, recv);
    if(ret == QM_EOK) {
        return ;
    }
#endif

    ret = qm_iot_pub_recv_comm_handler(session, recv);
    if(ret == QM_EOK) {
        return ;
    }
    QM_CLOUD_LOGE(LOG_TAG, "NOT DATA RECV!!");
}

static void qm_iot_session_pub_process(qm_iot_session_t *session)
{
    uint32_t delay_pub = CONFIG_QM_IOT_PUB_DELAY_MS;
    if(session == NULL){
        return;
    }

    if(session->pub_handle == NULL){
        session->pub_handle = qm_iot_pub_init();
        if(session->pub_handle == NULL){
            return;
        }
    }

    qm_iot_pub_setopt(session->pub_handle, QM_IOT_PUBOPT_RESET_SUB, NULL);
    qm_iot_pub_setopt(session->pub_handle, QM_IOT_PUBOPT_MQTT_HANDLE, session->mqtt_handle);
    qm_iot_pub_setopt(session->pub_handle, QM_IOT_PUBOPT_USERDATA, session);
    qm_iot_pub_setopt(session->pub_handle, QM_IOT_PUBOPT_DELAY_TIMEOUT, &delay_pub);
    qm_iot_pub_setopt(session->pub_handle, QM_IOT_PUBOPT_RECV_HANDLER, qm_iot_pub_recv_handler);

}

#endif

#if CONFIG_QM_IOT_OTA_SUPPORT

#if CONFIG_QM_IOT_OTA_MCU_SUPPORT
int qm_iot_version_update(void *handle, char *version, qm_iot_mcu_ver_info_t *ver_info)
{
    qm_iot_session_t *session = (qm_iot_session_t *)handle;
    if(session == NULL || session->ota_handle == NULL){
        return -QM_EINVAL;
    }
    return qm_iot_otav2_version_report(session->ota_handle, version, ver_info);
}

#endif

static void qm_iot_ota_event_handler(void *handle, const qm_iot_ota_event_t *event, void *userdata)
{
    qm_iot_event_t iot_event = {0};
    qm_iot_session_t *session = (qm_iot_session_t *)userdata;
    if(session == NULL || handle == NULL){
        return;
    }

    switch (event->type)
    {
        case QM_IOT_OTA_EVENT_READY:
            iot_event.type = QM_IOT_EVENT_OTA_READY;
            iot_event.data.ota_ready.ota_type = event->ota_type;
            iot_event.data.ota_ready.version_len = event->data.ready.version_len;
            iot_event.data.ota_ready.update_version = event->data.ready.update_version;
            qm_iot_event_post(session, &iot_event);
        break;

        case QM_IOT_OTA_EVENT_START:
            iot_event.type = QM_IOT_EVENT_OTA_START;
            iot_event.data.ota_start.ota_type = event->ota_type;
            iot_event.data.ota_start.file_size = event->data.start.file_size;
            iot_event.data.ota_start.extra = event->data.start.extra;
            iot_event.data.ota_start.extra_len = event->data.start.extra_len;
            iot_event.data.ota_start.version_len = event->data.start.version_len;
            iot_event.data.ota_start.update_version = event->data.start.update_version;
            qm_iot_event_post(session, &iot_event);
            ((qm_iot_ota_event_t *)event)->data.start.code = iot_event.data.ota_start.code;
            ((qm_iot_ota_event_t *)event)->data.start.block_size = iot_event.data.ota_start.rsp_block_size;
        break;

        case QM_IOT_OTA_EVENT_DATA_INSTALLING:
            iot_event.type = QM_IOT_EVENT_OTA_INSTALLING;
            iot_event.data.data_write.ota_type = event->ota_type;
            iot_event.data.data_write.id = event->data.data_write.id;
            iot_event.data.data_write.ota_data = event->data.data_write.ota_data;
            iot_event.data.data_write.data_len = event->data.data_write.data_len;
            qm_iot_event_post(session, &iot_event);
            ((qm_iot_ota_event_t *)event)->data.data_write.code = iot_event.data.data_write.code;
        break;

        case QM_IOT_OTA_EVENT_PROGRESS_UPADTE:
            iot_event.type = QM_IOT_EVENT_OTA_PROGRESS_UPADTE;
            iot_event.data.ota_progress_update.ota_type = event->ota_type;
            iot_event.data.ota_progress_update.progress_details =
                            event->data.progress_update.progress_details;
            qm_iot_event_post(session, &iot_event);
            ((qm_iot_ota_event_t *)event)->data.progress_update.code = iot_event.data.ota_progress_update.code;
        break;

        case QM_IOT_OTA_EVENT_SUCCESS:
            iot_event.type = QM_IOT_EVENT_OTA_SUCCESS;
            iot_event.data.ota_success.ota_type = event->ota_type;
            qm_iot_event_post(session, &iot_event);
        break;

        case QM_IOT_OTA_EVENT_FAILED:
            iot_event.type = QM_IOT_EVENT_OTA_FAILED;
            iot_event.data.ota_failed.ota_type = event->ota_type;
            qm_iot_event_post(session, &iot_event);
        break;

        case QM_IOT_OTA_EVENT_CANCEL:
            iot_event.type = QM_IOT_EVENT_OTA_CANCEL;
            iot_event.data.ota_cancel.ota_type = event->ota_type;
            qm_iot_event_post(session, &iot_event);
        break;

        default:
            break;
    }
}

static void qm_iot_session_ota_process(qm_iot_session_t *session)
{
    if(session == NULL){
        return;
    }

    if(session->ota_handle == NULL){
        return;
    }

    qm_iot_ota_setopt(session->ota_handle, QM_IOT_OTAOPT_RESET_SUB, NULL);
    qm_iot_ota_setopt(session->ota_handle, QM_IOT_OTAOPT_USERDATA, session);
    qm_iot_ota_setopt(session->ota_handle, QM_IOT_OTAOPT_MQTT_HANDLE, session->mqtt_handle);
    qm_iot_ota_setopt(session->ota_handle, QM_IOT_OTAOPT_SINGATURE, session->crt_handle.ota_signatrure);
    qm_iot_ota_setopt(session->ota_handle, QM_IOT_OTAOPT_EVENT_HANDLER, qm_iot_ota_event_handler);

}
#endif

#if CONFIG_QM_IOT_DEVMNT_SUPPORT

static void qm_iot_dev_recv_handler(void *handle, const qm_iot_dev_recv_t *packet, void *userdata)
{
    qm_iot_event_t  event = {0};
    qm_iot_session_t *session = (qm_iot_session_t *)userdata;
    if(packet == NULL || userdata == NULL){
        return ;
    }

    qm_cancel_delayed_action(&session->post_work);
    switch (packet->type)
    {
        case QM_IOT_DEVRECV_RESET_REPLY:
            QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_CONTROL, "QM_IOT_DEVRECV_RESET_REPLY");

            session->reset_info.is_reset = QM_IOT_RESET_UNPROV;
            qm_iot_reset_info_set(session, &session->reset_info);

            qm_post_delayed_action(&session->post_work, qm_iot_mqtt_reset_notify, session , 0);
        break;

        case QM_IOT_DEVRECV_UNBIND_NOTIFY:
            QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_CONTROL, "QM_IOT_DEVRECV_UNBIND_NOTIFY");

            session->reset_info.is_reset = QM_IOT_RESET_UNPROV;
            qm_iot_reset_info_set(session, &session->reset_info);
    
            qm_post_delayed_action(&session->post_work, qm_iot_mqtt_reset_notify,session ,CONFIG_QM_IOT_RESET_DELAY_MS);
        break;

        case QM_IOT_DEVRECV_BIND_NOTIFY:
            QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_CONTROL, "QM_IOT_DEVRECV_BIND_NOTIFY");
       
            session->reset_info.is_reset = QM_IOT_NOT_RESET;
            qm_iot_reset_info_set(session, &session->reset_info);
   
            event.type = QM_IOT_EVENT_BIND_NOTIFY;
            qm_iot_event_post(session, &event);

            qm_iot_session_notify_cloud_connected(session);
        break;

        case QM_IOT_DEVRECV_REBOOT_NOTIFY:
            QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_CONTROL, "QM_IOT_DEVRECV_REBOOT_NOTIFY");
            event.type = QM_IOT_EVENT_REBOOT_NOTIFY;
            qm_iot_event_post(session, &event);
        break;

        default:
            break;
    }
}

static void qm_iot_unbind_notify(qm_iot_session_t *session)
{
    if(session == NULL){
        return ;
    }

    session->reset_info.is_reset = QM_IOT_RESET_UNPROV;
    qm_iot_reset_info_set(session, &session->reset_info);

    qm_cancel_delayed_action(&session->post_work);

    if(session->mqtt_handle == NULL || session->dev_handle == NULL){
        qm_post_delayed_action(&session->post_work, qm_iot_mqtt_reset_notify,session ,CONFIG_QM_IOT_RESET_DELAY_MS);
        return;
    }
    qm_iot_dev_reset_request(session->dev_handle);
    qm_post_delayed_action(&session->post_work, qm_iot_mqtt_reset_notify,session ,CONFIG_QM_IOT_RESET_FORCE_MS);
}

static void qm_iot_session_dev_process(qm_iot_session_t *session)
{
    if(session == NULL){
        return;
    }

    if(session->dev_handle == NULL){
        return;
    }

    qm_iot_dev_setopt(session->dev_handle, QM_IOT_DEVOPT_RESET_SUB, NULL);
    qm_iot_dev_setopt(session->dev_handle, QM_IOT_DEVOPT_MQTT_HANDLE, session->mqtt_handle);
    qm_iot_dev_setopt(session->dev_handle, QM_IOT_DEVOPT_USERDATA, session);
    qm_iot_dev_setopt(session->dev_handle, QM_IOT_DEVOPT_RECV_HANDLER, qm_iot_dev_recv_handler);
}
#else
static void qm_iot_unbind_notify(qm_iot_session_t *session)
{
    if(session == NULL){
        return;
    }

    qm_cancel_delayed_action(&session->post_work);

    session->reset_info.is_reset = QM_IOT_RESET_UNPROV;
    qm_iot_reset_info_set(session, &session->reset_info);

    qm_post_delayed_action(&session->post_work, qm_iot_mqtt_reset_notify, session ,CONFIG_QM_IOT_RESET_DELAY_MS);
}
#endif

#if CONFIG_QM_IOT_SERVICE_SUPPORT

static int service_map_match(void *a, void *b)
{
    qm_iot_service_entry_t *a_handle = (qm_iot_service_entry_t*)a;
    qm_iot_service_entry_t *b_handle = (qm_iot_service_entry_t*)b;

    if(a_handle == NULL || b_handle == NULL){
        return QM_FALSE;
    }

    if(a_handle->service_id == b_handle->service_id){
        return 1;
    }else{
        return 0;
    }
}

int qm_iot_service_register(void *handle, int service_id, qm_iot_service_iid_t *iid, int iid_num,
                                qm_iot_service_handler_t service_fn, void *userdata)
{
    int node_len = 0;
    qm_list_node_t *node = NULL;
    qm_iot_service_iid_t *service_iid = NULL;
    qm_iot_service_entry_t *service = NULL;
    qm_iot_service_entry_t match_service = {0};

    qm_iot_session_t *session = (qm_iot_session_t *)handle;
    if(session == NULL || iid == NULL || iid_num == 0 || service_fn == NULL){
        return -QM_EINVAL;
    }

    node_len = (sizeof(qm_iot_service_entry_t) + (sizeof(qm_iot_service_iid_t) * iid_num) - 1);

    match_service.service_id = service_id;

    qm_mutex_lock(&session->lock, QM_WAIT_FOREVER);
    node = qm_list_find(session->service, &match_service);
    if(node != NULL){
        qm_mutex_unlock(&session->lock);
        QM_CLOUD_LOGE(LOG_TAG, "service is exist!!");
        return -QM_EINVAL;
    }

    node = qm_list_node_extra_new(node_len);
    if(node == NULL){
        qm_mutex_unlock(&session->lock);
        return -QM_ENOMEM;
    }

    service = (qm_iot_service_entry_t *)qm_list_node_val_get(node);

    service->service_id = service_id;
    service->userdata = userdata;
    service->service_fn = service_fn;
    service->service_num = iid_num;

    service_iid = (qm_iot_service_iid_t *)service->raw_iid;
    for (int i = 0; i < iid_num; i++)
    {
        service_iid[i].siid = iid[i].siid;
        service_iid[i].piid = iid[i].piid;
    }

    qm_list_rpush(session->service, node);

    qm_mutex_unlock(&session->lock);

    return QM_EOK;
}

#endif

int32_t qm_iot_up_report_ex(void *handle,
                            char *param,
                            char *submethod,
                            uint32_t msg_id,
                            char *payload,
                            int payload_len)
{
    qm_iot_session_t *session = (qm_iot_session_t *)handle;
    if(session == NULL || session->dev_handle == NULL){
        return -QM_EINVAL;
    }
    return qm_iot_dev_up_report_ex(session->dev_handle,
                                   param,
                                   submethod,
                                   msg_id,
                                   payload,
                                   payload_len);
}

int32_t qm_iot_up_report(void *handle,
                         char *param,
                         uint32_t msg_id,
                         char *payload,
                         int payload_len)
{
    return qm_iot_up_report_ex(handle, param, NULL, msg_id, payload, payload_len);
}

int32_t qm_iot_down_reply_ex(void *handle,
                             char *param,
                             char *submethod,
                             uint32_t msg_id,
                             int code,
                             char *payload,
                             int payload_len)
{
    qm_iot_down_req_t req = {0};

    req.method = param;
    req.submethod = submethod;
    req.msg_id = msg_id;

    return qm_iot_down_reply(handle, &req, code, payload, payload_len);
}

int32_t qm_iot_down_reply(void *handle,
                          const qm_iot_down_req_t *req,
                          int code,
                          char *payload,
                          int payload_len)
{
    qm_iot_session_t *session = (qm_iot_session_t *)handle;
    if(session == NULL || session->dev_handle == NULL || req == NULL || req->method == NULL){
        return -QM_EINVAL;
    }

    return qm_iot_dev_reply_publish(session->dev_handle,
                                    (char *)req->method,
                                    (char *)req->submethod,
                                    req->msg_id,
                                    code,
                                    payload,
                                    payload_len);
}

int32_t qm_iot_down_register(void *handle, char *param, qm_iot_mqtt_recv_handler_t recv_handler, void *user_data)
{
    qm_iot_session_t *session = (qm_iot_session_t *)handle;
    if(session->dev_handle == NULL){
        return -QM_EINVAL;
    }

    return qm_iot_dev_down_register(session->dev_handle, param, recv_handler, user_data);
}

int32_t qm_iot_down_unregister(void *handle, char *param)
{
    qm_iot_session_t *session = (qm_iot_session_t *)handle;
    if(session == NULL || session->dev_handle == NULL){
        return -QM_EINVAL;
    }

    return qm_iot_dev_down_unregister(session->dev_handle, param);
}

void *qm_iot_get_mqtt_handle(void *handle)
{
#if CONFIG_QM_IOT_MQTT_SUPPORT
    qm_iot_session_t *session = (qm_iot_session_t *)handle;

    if(session == NULL){
        return NULL;
    }

    return session->mqtt_handle;
#else
    (void)handle;
    return NULL;
#endif
}

#if CONFIG_QM_IOT_LOG_PUSH_SUPPORT

int32_t qm_iot_log_push(void *handle, const char *log_level,
                        const qm_iot_log_item_t *items, uint8_t item_num,
                        uint32_t msg_id)
{
    qm_iot_session_t *session = (qm_iot_session_t *)handle;

    if (session == NULL || session->dev_handle == NULL) {
        return -QM_EINVAL;
    }

    return qm_iot_dev_log_push(session->dev_handle, log_level, items, item_num, msg_id);
}

int32_t qm_iot_log_push_raw(void *handle, const char *log_level,
                            const char *payload_array_json, uint32_t msg_id)
{
    qm_iot_session_t *session = (qm_iot_session_t *)handle;

    if (session == NULL || session->dev_handle == NULL) {
        return -QM_EINVAL;
    }

    return qm_iot_dev_log_push_raw(session->dev_handle, log_level, payload_array_json, msg_id);
}

#endif /* CONFIG_QM_IOT_LOG_PUSH_SUPPORT */
