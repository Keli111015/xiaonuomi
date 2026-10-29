#include "qm_timer_mgr_cloud.h"

#ifndef CONFIG_QM_TIMER_MGR_CLOUD_METHOD
#define CONFIG_QM_TIMER_MGR_CLOUD_METHOD "timer"
#endif

#ifndef CONFIG_QM_TIMER_MGR_CLOUD_QUEUE_LEN
#define CONFIG_QM_TIMER_MGR_CLOUD_QUEUE_LEN 4
#endif

#ifndef CONFIG_QM_TIMER_MGR_CLOUD_TASK_STACK_SIZE
#define CONFIG_QM_TIMER_MGR_CLOUD_TASK_STACK_SIZE 4096
#endif

#ifndef CONFIG_QM_TIMER_MGR_CLOUD_TASK_PRIORITY
#define CONFIG_QM_TIMER_MGR_CLOUD_TASK_PRIORITY 20
#endif

#if CONFIG_QM_TIMER_MGR_CLOUD_SUPPORT

#include "cJSON.h"
#include "qm_iot_api.h"
#include "qm_kernel.h"
#include "qm_string.h"

#define QM_TIMER_MGR_CLOUD_UPDATE_SUBMETHOD "update"

/* Heap copy of a down payload plus a trailing NUL, so cJSON_ParseWithLength
 * always sees a terminated buffer. Owned by the cloud task; freed after use. */
typedef struct {
    uint32_t len;
    char data[1];
} qm_timer_mgr_cloud_packet_t;

typedef struct {
    qm_timer_mgr_t timer;
    void *iot_handle;
    qm_timer_mgr_cloud_command_cb_t command_cb;
    qm_timer_mgr_cloud_status_payload_cb_t status_payload_cb;
    void *user_data;
    qm_queue_t queue;      /* command packets, bounded by slots_sem */
    qm_sem_t slots_sem;    /* counts free queue slots; drops when full */
    qm_sem_t stopped_sem;  /* task-exit handshake used by deinit() */
    qm_task_t task;        /* serializes command processing and replies */
    qm_bool queue_valid;
    qm_bool slots_sem_valid;
    qm_bool stopped_sem_valid;
    qm_bool registered;
    volatile qm_bool stopping; /* gates new down traffic during teardown */
    volatile uint32_t pending; /* queued/in-flight commands for command_pending() */
} qm_timer_mgr_cloud_t;

static void qm_timer_mgr_cloud_task(void *arg);
static void qm_timer_mgr_cloud_down_cb(void *handle,
                                       const qm_iot_mqtt_recv_t *packet,
                                       void *user_data);

/* Stop accepting new down traffic first: raise the stopping flag and
 * unregister the method so no further frames arrive during teardown. */
static void qm_timer_mgr_cloud_stop_registration(qm_timer_mgr_cloud_t *cloud)
{
    if (cloud == NULL) {
        return;
    }
    cloud->stopping = QM_TRUE;
    if (cloud->registered) {
        (void)qm_iot_down_unregister(cloud->iot_handle,
                                     (char *)CONFIG_QM_TIMER_MGR_CLOUD_METHOD);
        cloud->registered = QM_FALSE;
    }
}

/* Map QM_E* results onto the protocol's numeric code field: 0 ok, 1 invalid/
 * no-mem, 2 unsupported, 4 busy/interrupted, 7 other. */
static int qm_timer_mgr_cloud_result_code(int result)
{
    if (result == QM_EOK) {
        return 0;
    }
    if (result == -QM_EINVAL || result == -QM_ENOMEM) {
        return 1;
    }
    if (result == -QM_ENOSYS) {
        return 2;
    }
    if (result == -QM_EBUSY || result == -QM_EINTR) {
        return 4;
    }
    return 7;
}

static const cJSON *qm_timer_mgr_cloud_get_any(const cJSON *object,
                                               const char *first,
                                               const char *second,
                                               const char *third)
{
    const cJSON *item = NULL;

    if (object == NULL) {
        return NULL;
    }
    if (first != NULL) {
        item = cJSON_GetObjectItemCaseSensitive((cJSON *)object, first);
    }
    if (item == NULL && second != NULL) {
        item = cJSON_GetObjectItemCaseSensitive((cJSON *)object, second);
    }
    if (item == NULL && third != NULL) {
        item = cJSON_GetObjectItemCaseSensitive((cJSON *)object, third);
    }
    return item;
}

static uint32_t qm_timer_mgr_cloud_get_u32(const cJSON *item)
{
    const char *text;
    uint32_t value = 0U;

    if (cJSON_IsNumber(item)) {
        return item->valuedouble > 0.0 ? (uint32_t)item->valuedouble : 0U;
    }
    if (!cJSON_IsString(item)) {
        return 0U;
    }
    text = cJSON_GetStringValue((cJSON *)item);
    while (text != NULL && *text >= '0' && *text <= '9') {
        value = value * 10U + (uint32_t)(*text - '0');
        text++;
    }
    return value;
}

static qm_timer_mgr_cloud_command_type_t qm_timer_mgr_cloud_command_type(
    const char *submethod)
{
    if (qm_strcmp(submethod, "start") == 0) {
        return QM_TIMER_MGR_CLOUD_COMMAND_START;
    }
    if (qm_strcmp(submethod, "pause") == 0) {
        return QM_TIMER_MGR_CLOUD_COMMAND_PAUSE;
    }
    if (qm_strcmp(submethod, "continue") == 0) {
        return QM_TIMER_MGR_CLOUD_COMMAND_CONTINUE;
    }
    if (qm_strcmp(submethod, "stop") == 0) {
        return QM_TIMER_MGR_CLOUD_COMMAND_STOP;
    }
    if (qm_strcmp(submethod, "restart") == 0) {
        return QM_TIMER_MGR_CLOUD_COMMAND_RESTART;
    }
    return QM_TIMER_MGR_CLOUD_COMMAND_INVALID;
}

static const char *qm_timer_mgr_cloud_ack_submethod(
    qm_timer_mgr_cloud_command_type_t type)
{
    switch (type) {
    case QM_TIMER_MGR_CLOUD_COMMAND_START:
        return "startAck";
    case QM_TIMER_MGR_CLOUD_COMMAND_PAUSE:
        return "pauseAck";
    case QM_TIMER_MGR_CLOUD_COMMAND_CONTINUE:
        return "continueAck";
    case QM_TIMER_MGR_CLOUD_COMMAND_STOP:
        return "stopAck";
    case QM_TIMER_MGR_CLOUD_COMMAND_RESTART:
        return "restartAck";
    default:
        return "unknownAck";
    }
}

/* Build the protocol status array. Reuses the core get_status(), which also
 * advances the time; timerType carries the generic mode name, leaving product
 * specific types to the upper layer. */
static char *qm_timer_mgr_cloud_status_payload(qm_timer_mgr_t timer)
{
    cJSON *root;
    cJSON *item;
    qm_timer_mgr_status_t status;
    char *payload;

    if (timer == NULL ||
        qm_timer_mgr_get_status(timer, &status) != QM_EOK) {
        return NULL;
    }
    root = cJSON_CreateArray();
    if (root == NULL) {
        return NULL;
    }
    item = cJSON_CreateObject();
    if (item == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddItemToArray(root, item);
    cJSON_AddStringToObject(item, "timerType",
                            qm_timer_mgr_mode_str(status.mode));
    cJSON_AddStringToObject(item, "state",
                            qm_timer_mgr_state_str(status.state));
    cJSON_AddNumberToObject(item, "durationSec", (double)status.duration_sec);
    cJSON_AddNumberToObject(item, "remainingSec", (double)status.remaining_sec);
    cJSON_AddNumberToObject(item, "elapsedSec", (double)status.elapsed_sec);
    payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return payload;
}

static char *qm_timer_mgr_cloud_payload(qm_timer_mgr_cloud_t *cloud)
{
    if (cloud != NULL && cloud->status_payload_cb != NULL) {
        char *payload = cloud->status_payload_cb(cloud->timer, cloud->user_data);

        if (payload != NULL) {
            return payload;
        }
    }
    return cloud != NULL ? qm_timer_mgr_cloud_status_payload(cloud->timer) : NULL;
}

static void qm_timer_mgr_cloud_reply(qm_timer_mgr_cloud_t *cloud,
                                     uint32_t msg_id,
                                     qm_timer_mgr_cloud_command_type_t type,
                                     int result,
                                     char *payload)
{
    if (cloud == NULL || cloud->iot_handle == NULL) {
        return;
    }
    (void)qm_iot_down_reply_ex(cloud->iot_handle,
                               (char *)CONFIG_QM_TIMER_MGR_CLOUD_METHOD,
                               (char *)qm_timer_mgr_cloud_ack_submethod(type),
                               msg_id,
                               qm_timer_mgr_cloud_result_code(result),
                               payload,
                               payload != NULL ? (int)qm_strlen(payload) : 0);
}

/* Parse one down frame and produce exactly one reply. "get" is answered here
 * with the current snapshot; other submethods are routed to command_cb, which
 * may return a reply payload that is cJSON-allocated and freed here. */
static void qm_timer_mgr_cloud_process(qm_timer_mgr_cloud_t *cloud,
                                       const char *input,
                                       uint32_t input_len)
{
    cJSON *root;
    const cJSON *submethod_item;
    const cJSON *id_item;
    const cJSON *params;
    const char *submethod = NULL;
    char *params_json = NULL;
    char *reply_payload = NULL;
    qm_timer_mgr_cloud_command_t command;
    qm_timer_mgr_cloud_command_type_t type;
    int result = -QM_EINVAL;
    uint32_t msg_id = 0U;

    root = cJSON_ParseWithLength(input, input_len);
    if (root == NULL) {
        qm_timer_mgr_cloud_reply(cloud, 0U, QM_TIMER_MGR_CLOUD_COMMAND_INVALID,
                                 -QM_EINVAL, NULL);
        return;
    }
    submethod_item = qm_timer_mgr_cloud_get_any(root, "subMethod", "submethod", "cmd");
    if (cJSON_IsString(submethod_item)) {
        submethod = cJSON_GetStringValue((cJSON *)submethod_item);
    }
    id_item = qm_timer_mgr_cloud_get_any(root, "id", "msgId", "msg_id");
    msg_id = qm_timer_mgr_cloud_get_u32(id_item);
    params = qm_timer_mgr_cloud_get_any(root, "params", "data", NULL);

    if (submethod == NULL || submethod[0] == '\0') {
        result = -QM_EINVAL;
    } else if (qm_strcmp(submethod, "get") == 0) {
        reply_payload = qm_timer_mgr_cloud_payload(cloud);
        result = reply_payload != NULL ? QM_EOK : -QM_ENOMEM;
        qm_timer_mgr_cloud_reply(cloud, msg_id, QM_TIMER_MGR_CLOUD_COMMAND_INVALID,
                                 result, reply_payload);
        if (reply_payload != NULL) {
            cJSON_free(reply_payload);
        }
    } else if (cloud->stopping) {
        result = -QM_EINTR;
    } else if (cloud->command_cb != NULL) {
        if (params != NULL) {
            params_json = cJSON_PrintUnformatted((cJSON *)params);
        } else {
            params_json = (char *)qm_malloc(3U);
            if (params_json != NULL) {
                qm_memcpy(params_json, "{}", 3U);
            }
        }
        if (params_json != NULL) {
            type = qm_timer_mgr_cloud_command_type(submethod);
            qm_memset(&command, 0, sizeof(command));
            command.type = type;
            command.params_json = params_json;
            command.params_json_len = (uint32_t)qm_strlen(params_json);
            result = cloud->command_cb(cloud->timer, &command,
                                       &reply_payload, cloud->user_data);
            if (result == QM_EOK && reply_payload == NULL) {
                reply_payload = qm_timer_mgr_cloud_payload(cloud);
            }
            qm_timer_mgr_cloud_reply(cloud, msg_id, type, result, reply_payload);
        } else {
            result = -QM_ENOMEM;
            qm_timer_mgr_cloud_reply(cloud, msg_id,
                                     QM_TIMER_MGR_CLOUD_COMMAND_INVALID,
                                     result, NULL);
        }
    } else {
        result = -QM_ENOSYS;
        qm_timer_mgr_cloud_reply(cloud, msg_id, QM_TIMER_MGR_CLOUD_COMMAND_INVALID,
                                 result, NULL);
    }

    if (reply_payload != NULL) {
        cJSON_free(reply_payload);
    }
    if (params_json != NULL) {
        cJSON_free(params_json);
    }
    cJSON_Delete(root);
}

/* Serialize command processing on a single task. A NULL packet sent by deinit
 * is the exit sentinel; on exit the stopped_sem is signalled to unblock it. */
static void qm_timer_mgr_cloud_task(void *arg)
{
    qm_timer_mgr_cloud_t *cloud = (qm_timer_mgr_cloud_t *)arg;
    qm_timer_mgr_cloud_packet_t *packet = NULL;
    unsigned int size;

    while (cloud != NULL) {
        size = sizeof(packet);
        packet = NULL;
        if (qm_queue_recv(&cloud->queue, &packet, &size, QM_WAIT_FOREVER) != QM_EOK) {
            continue;
        }
        if (packet == NULL) {
            break;
        }
        qm_timer_mgr_cloud_process(cloud, packet->data, packet->len);
        (void)__sync_fetch_and_sub(&cloud->pending, 1U);
        qm_free(packet);
        if (cloud->slots_sem_valid) {
            (void)qm_sem_signal(&cloud->slots_sem);
        }
    }
    if (cloud != NULL && cloud->stopped_sem_valid) {
        (void)qm_sem_signal(&cloud->stopped_sem);
    }
}

/* Copy the payload, gate with a queue slot, then enqueue; drop the packet if
 * the queue is full so a burst cannot block the MQTT callback context. */
static void qm_timer_mgr_cloud_down_cb(void *handle,
                                       const qm_iot_mqtt_recv_t *packet,
                                       void *user_data)
{
    qm_timer_mgr_cloud_t *cloud = (qm_timer_mgr_cloud_t *)user_data;
    qm_timer_mgr_cloud_packet_t *copy;
    uint32_t len;
    int slot_acquired;

    QM_UNUSED_ARG(handle);
    if (cloud == NULL || cloud->stopping || packet == NULL ||
        packet->type != QM_IOT_MQTTRECV_PUB ||
        packet->data.pub.payload == NULL || packet->data.pub.payload_len == 0U) {
        return;
    }
    len = packet->data.pub.payload_len;
    copy = (qm_timer_mgr_cloud_packet_t *)qm_malloc(sizeof(*copy) + len);
    if (copy == NULL) {
        return;
    }
    copy->len = len;
    qm_memcpy(copy->data, packet->data.pub.payload, len);
    copy->data[len] = '\0';
    slot_acquired = cloud->slots_sem_valid &&
                    qm_sem_wait(&cloud->slots_sem, QM_NO_WAIT) == QM_EOK;
    if (!slot_acquired ||
        qm_queue_send(&cloud->queue, &copy, sizeof(copy)) != QM_EOK) {
        if (slot_acquired && cloud->slots_sem_valid) {
            (void)qm_sem_signal(&cloud->slots_sem);
        }
        qm_free(copy);
        return;
    }
    (void)__sync_fetch_and_add(&cloud->pending, 1U);
}

int qm_timer_mgr_cloud_init(qm_timer_mgr_t timer,
                            const qm_timer_mgr_cloud_config_t *config)
{
    qm_timer_mgr_cloud_t *cloud;
    int ret;

    if (timer == NULL || config == NULL || config->iot_handle == NULL) {
        return -QM_EINVAL;
    }
    cloud = (qm_timer_mgr_cloud_t *)qm_malloc(sizeof(*cloud));
    if (cloud == NULL) {
        return -QM_ENOMEM;
    }
    qm_memset(cloud, 0, sizeof(*cloud));
    cloud->timer = timer;
    cloud->iot_handle = config->iot_handle;
    cloud->command_cb = config->command_cb;
    cloud->status_payload_cb = config->status_payload_cb;
    cloud->user_data = config->user_data;
    ret = qm_queue_new(&cloud->queue, CONFIG_QM_TIMER_MGR_CLOUD_QUEUE_LEN,
                       sizeof(qm_timer_mgr_cloud_packet_t *));
    if (ret != QM_EOK) {
        qm_free(cloud);
        return ret;
    }
    cloud->queue_valid = QM_TRUE;
    ret = qm_sem_new(&cloud->slots_sem, CONFIG_QM_TIMER_MGR_CLOUD_QUEUE_LEN);
    if (ret != QM_EOK) {
        qm_queue_free(&cloud->queue);
        qm_free(cloud);
        return ret;
    }
    cloud->slots_sem_valid = QM_TRUE;
    ret = qm_sem_new(&cloud->stopped_sem, 0);
    if (ret != QM_EOK) {
        qm_sem_free(&cloud->slots_sem);
        qm_queue_free(&cloud->queue);
        qm_free(cloud);
        return ret;
    }
    cloud->stopped_sem_valid = QM_TRUE;
    ret = qm_task_new(&cloud->task, "qmtm_cloud", qm_timer_mgr_cloud_task, cloud,
                      CONFIG_QM_TIMER_MGR_CLOUD_TASK_STACK_SIZE,
                      CONFIG_QM_TIMER_MGR_CLOUD_TASK_PRIORITY);
    if (ret != QM_EOK) {
        qm_sem_free(&cloud->stopped_sem);
        qm_sem_free(&cloud->slots_sem);
        qm_queue_free(&cloud->queue);
        qm_free(cloud);
        return ret;
    }
    qm_timer_mgr_cloud_set_context(timer, cloud);
    ret = qm_iot_down_register(cloud->iot_handle,
                               (char *)CONFIG_QM_TIMER_MGR_CLOUD_METHOD,
                               qm_timer_mgr_cloud_down_cb,
                               cloud);
    if (ret == QM_EOK) {
        cloud->registered = QM_TRUE;
        return QM_EOK;
    }
    qm_timer_mgr_cloud_deinit(timer);
    return ret;
}

int qm_timer_mgr_cloud_attach(qm_timer_mgr_t timer,
                              const qm_timer_mgr_cloud_config_t *config)
{
    int ret;

    if (timer == NULL || config == NULL) {
        return -QM_EINVAL;
    }
    if (qm_timer_mgr_cloud_get_context(timer) != NULL) {
        return -QM_EBUSY;
    }
    ret = qm_timer_mgr_cloud_init(timer, config);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_timer_mgr_cloud_set_config(timer, config);
    if (ret != QM_EOK) {
        qm_timer_mgr_cloud_deinit(timer);
    }
    return ret;
}

void qm_timer_mgr_cloud_quiesce(qm_timer_mgr_t timer)
{
    qm_timer_mgr_cloud_t *cloud =
        (qm_timer_mgr_cloud_t *)qm_timer_mgr_cloud_get_context(timer);

    qm_timer_mgr_cloud_stop_registration(cloud);
}

/* Teardown ordering: drain queued commands (wait for every slot to return),
 * post the exit sentinel, wait for the task to exit, then release semaphores
 * and the queue. */
void qm_timer_mgr_cloud_deinit(qm_timer_mgr_t timer)
{
    qm_timer_mgr_cloud_t *cloud =
        (qm_timer_mgr_cloud_t *)qm_timer_mgr_cloud_get_context(timer);
    qm_timer_mgr_cloud_packet_t *exit_packet = NULL;

    if (cloud == NULL) {
        return;
    }
    qm_timer_mgr_cloud_stop_registration(cloud);
    if (cloud->queue_valid) {
        (void)qm_sem_wait(&cloud->slots_sem, QM_WAIT_FOREVER);
        (void)qm_queue_send(&cloud->queue, &exit_packet, sizeof(exit_packet));
    }
    if (cloud->stopped_sem_valid) {
        (void)qm_sem_wait(&cloud->stopped_sem, QM_WAIT_FOREVER);
        qm_sem_free(&cloud->stopped_sem);
        cloud->stopped_sem_valid = QM_FALSE;
    }
    if (cloud->slots_sem_valid) {
        qm_sem_free(&cloud->slots_sem);
        cloud->slots_sem_valid = QM_FALSE;
    }
    if (cloud->queue_valid) {
        qm_queue_free(&cloud->queue);
        cloud->queue_valid = QM_FALSE;
    }
    qm_timer_mgr_cloud_set_context(timer, NULL);
    qm_free(cloud);
}

/* Forward state-change events as "update" reports. TICK is intentionally not
 * reported; per-second progress reaches the product only via the event callback. */
void qm_timer_mgr_cloud_event(qm_timer_mgr_t timer,
                              const qm_timer_mgr_event_data_t *event)
{
    qm_timer_mgr_cloud_t *cloud =
        (qm_timer_mgr_cloud_t *)qm_timer_mgr_cloud_get_context(timer);
    char *payload;

    if (cloud == NULL || cloud->stopping || event == NULL ||
        cloud->iot_handle == NULL) {
        return;
    }
    if (event->event != QM_TIMER_MGR_EVENT_STARTED &&
        event->event != QM_TIMER_MGR_EVENT_PAUSED &&
        event->event != QM_TIMER_MGR_EVENT_RESUMED &&
        event->event != QM_TIMER_MGR_EVENT_RESTARTED &&
        event->event != QM_TIMER_MGR_EVENT_STOPPED &&
        event->event != QM_TIMER_MGR_EVENT_FINISHED) {
        return;
    }
    payload = qm_timer_mgr_cloud_payload(cloud);
    if (payload != NULL) {
        (void)qm_iot_up_report_ex(cloud->iot_handle,
                                  (char *)CONFIG_QM_TIMER_MGR_CLOUD_METHOD,
                                  (char *)QM_TIMER_MGR_CLOUD_UPDATE_SUBMETHOD,
                                  0U,
                                  payload,
                                  (int)qm_strlen(payload));
        cJSON_free(payload);
    }
}

int qm_timer_mgr_cloud_command_pending(qm_timer_mgr_t timer)
{
    qm_timer_mgr_cloud_t *cloud =
        (qm_timer_mgr_cloud_t *)qm_timer_mgr_cloud_get_context(timer);

    return (cloud != NULL &&
            __sync_fetch_and_add(&cloud->pending, 0U) > 0U) ? 1 : 0;
}

#else

int qm_timer_mgr_cloud_init(qm_timer_mgr_t timer,
                            const qm_timer_mgr_cloud_config_t *config)
{
    QM_UNUSED_ARG(timer);
    QM_UNUSED_ARG(config);
    return -QM_ENOSYS;
}

int qm_timer_mgr_cloud_attach(qm_timer_mgr_t timer,
                              const qm_timer_mgr_cloud_config_t *config)
{
    QM_UNUSED_ARG(timer);
    QM_UNUSED_ARG(config);
    return -QM_ENOSYS;
}

void qm_timer_mgr_cloud_quiesce(qm_timer_mgr_t timer)
{
    QM_UNUSED_ARG(timer);
}

void qm_timer_mgr_cloud_deinit(qm_timer_mgr_t timer)
{
    QM_UNUSED_ARG(timer);
}

void qm_timer_mgr_cloud_event(qm_timer_mgr_t timer,
                              const qm_timer_mgr_event_data_t *event)
{
    QM_UNUSED_ARG(timer);
    QM_UNUSED_ARG(event);
}

int qm_timer_mgr_cloud_command_pending(qm_timer_mgr_t timer)
{
    QM_UNUSED_ARG(timer);
    return 0;
}

#endif
