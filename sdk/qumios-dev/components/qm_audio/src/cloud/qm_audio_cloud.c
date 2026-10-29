/**
 * @file qm_audio_cloud.c
 * @brief Optional qm_cloud to qm_audio_control player adapter. / 可选的 qm_cloud 到 qm_audio_control 播放器适配器。
 */
#include "cloud/qm_audio_cloud.h"
#include "cloud/qm_audio_cloud_protocol.h"

#include "qm.h"
#include "qm_audio_priv.h"

#if CONFIG_QM_AUDIO_CLOUD_SUPPORT

#include "qm_iot_api.h"
#include "qm_kernel.h"
#include "qm_string.h"
#include "qm_utils_timer.h"

#define QM_AUDIO_CLOUD_METHOD "player"
#define QM_AUDIO_CLOUD_REPLY_METHOD "playerReply"
typedef struct qm_audio_cloud_operation qm_audio_cloud_operation_t;

typedef struct {
    uint32_t len;
    char data[1];
} qm_audio_cloud_packet_t;

typedef enum {
    QM_AUDIO_CLOUD_CMD = 0,
} qm_audio_cloud_work_type_t;

typedef struct {
    qm_audio_cloud_work_type_t type;
    qm_audio_cloud_packet_t *packet;
} qm_audio_cloud_work_t;

struct qm_audio_cloud_operation {
    qm_audio_operation_id_t operation_id;
    uint32_t msg_id;
    char *title;
    char *codec;
    uint32_t file_size;
    uint32_t duration_s;
    qm_audio_source_type_t source_type;
    qm_audio_cloud_playback_t playback;
    qm_audio_event_t pending_event;
    qm_bool pending_event_valid;
    qm_bool release_started;
    qm_bool reported_state_valid;
    qm_audio_control_state_t reported_state;
};

struct qm_audio_cloud {
    qm_audio_cloud_config_t config;
    qm_mutex_t lock;
    qm_queue_t queue;
    qm_sem_t command_slots;
    qm_sem_t wake_sem;
    qm_sem_t stopped_sem;
    qm_task_t task;
    qm_bool task_started;
    qm_bool task_stopped;
    qm_bool command_registered;
    qm_bool reply_registered;
    qm_bool stopping;
    qm_bool exit_requested;
    uint32_t pending_commands;
    uint32_t next_report_id;
    uint32_t operation_capacity;
    uint32_t operation_count;
    qm_audio_cloud_operation_t **operations;
    qm_audio_subscription_t event_subscription;
};

static void cloud_task(void *arg);
static void down_cb(void *handle, const qm_iot_mqtt_recv_t *packet, void *user_data);
static void reply_down_cb(void *handle, const qm_iot_mqtt_recv_t *packet, void *user_data);

static qm_bool cloud_stopping(qm_audio_cloud_t *cloud)
{
    qm_bool stopping;

    if (cloud == NULL || !qm_mutex_is_valid(&cloud->lock)) {
        return QM_TRUE;
    }
    (void)qm_mutex_lock(&cloud->lock, QM_WAIT_FOREVER);
    stopping = cloud->stopping;
    (void)qm_mutex_unlock(&cloud->lock);
    return stopping;
}

static qm_audio_cloud_operation_t *latest_operation_locked(qm_audio_cloud_t *cloud)
{
    if (cloud == NULL || cloud->operation_count == 0U) {
        return NULL;
    }
    return cloud->operations[cloud->operation_count - 1U];
}

/**
 * @brief Release one cloud-owned playback operation. / 释放一个云端拥有的播放操作。
 *
 * The product release callback owns borrowed source cleanup and must run exactly
 * once after the Control operation is quiescent. 产品释放回调负责清理借用输入源，
 * 并且只能在 Control 操作静止后执行一次。
 */
static void free_operation(qm_audio_cloud_operation_t *operation)
{
    if (operation == NULL) {
        return;
    }
    if (operation->playback.release != NULL) {
        operation->playback.release(operation->playback.release_ctx);
    }
    qm_free(operation->title);
    qm_free(operation->codec);
    qm_free(operation);
}

static void remove_operation_locked(qm_audio_cloud_t *cloud, qm_audio_cloud_operation_t *operation)
{
    uint32_t i;

    if (cloud == NULL || operation == NULL) {
        return;
    }
    for (i = 0U; i < cloud->operation_count; ++i) {
        if (cloud->operations[i] == operation) {
            /* The final entry is the newest command target. */
            for (uint32_t j = i + 1U; j < cloud->operation_count; ++j) {
                cloud->operations[j - 1U] = cloud->operations[j];
            }
            cloud->operations[--cloud->operation_count] = NULL;
            return;
        }
    }
}

/**
 * @brief Queue one copied inbound cloud packet for the worker. / 为工作任务排队一份已复制的云端入站数据包。
 */
static int enqueue_packet(qm_audio_cloud_t *cloud, qm_audio_cloud_packet_t *packet)
{
    qm_audio_cloud_work_t *work;
    int ret;

    if (cloud == NULL || packet == NULL ||
        !qm_sem_is_valid(&cloud->command_slots) ||
        qm_sem_wait(&cloud->command_slots, QM_NO_WAIT) != QM_EOK) {
        return -QM_EBUSY;
    }
    work = (qm_audio_cloud_work_t *)qm_calloc(1U, sizeof(*work));
    if (work == NULL) {
        (void)qm_sem_signal(&cloud->command_slots);
        return -QM_ENOMEM;
    }
    work->type = QM_AUDIO_CLOUD_CMD;
    work->packet = packet;
    (void)qm_mutex_lock(&cloud->lock, QM_WAIT_FOREVER);
    if (cloud->stopping) {
        (void)qm_mutex_unlock(&cloud->lock);
        qm_free(work);
        (void)qm_sem_signal(&cloud->command_slots);
        return -QM_EINTR;
    }
    ret = qm_queue_send(&cloud->queue, &work, sizeof(work));
    if (ret != QM_EOK) {
        (void)qm_mutex_unlock(&cloud->lock);
        qm_free(work);
        (void)qm_sem_signal(&cloud->command_slots);
        return ret;
    }
    cloud->pending_commands++;
    (void)qm_mutex_unlock(&cloud->lock);
    (void)qm_sem_signal(&cloud->wake_sem);
    return QM_EOK;
}

static void enqueue_event(qm_audio_cloud_t *cloud, const qm_audio_event_t *event)
{
    qm_audio_cloud_operation_t *operation = NULL;
    uint32_t i;

    if (cloud == NULL || event == NULL) {
        return;
    }
    (void)qm_mutex_lock(&cloud->lock, QM_WAIT_FOREVER);
    for (i = 0U; i < cloud->operation_count; ++i) {
        if (cloud->operations[i] != NULL &&
            cloud->operations[i]->operation_id == event->operation_id) {
            operation = cloud->operations[i];
            break;
        }
    }
    if (cloud->stopping || operation == NULL || operation->release_started) {
        (void)qm_mutex_unlock(&cloud->lock);
        return;
    }
    /* Keep only the newest status per operation. This callback runs on the
     * audio event task, so it must not wait on the cloud command queue. */
    operation->pending_event = *event;
    operation->pending_event_valid = QM_TRUE;
    (void)qm_mutex_unlock(&cloud->lock);
    (void)qm_sem_signal(&cloud->wake_sem);
}

/**
 * @brief Stage Control events for cloud-task processing. / 暂存 Control 事件以供云端任务处理。
 *
 * The callback never performs network I/O because it shares the Control event
 * task with application subscribers. 回调与应用订阅者共用 Control 事件任务，
 * 因此不执行网络 I/O。
 */
static void control_event(const qm_audio_event_t *event, void *user_ctx)
{
    enqueue_event((qm_audio_cloud_t *)user_ctx, event);
}

static void stop_registration(qm_audio_cloud_t *cloud)
{
    if (cloud == NULL) {
        return;
    }
    if (cloud->reply_registered) {
        (void)qm_iot_down_unregister(cloud->config.iot_handle,
                                     (char *)QM_AUDIO_CLOUD_REPLY_METHOD);
        cloud->reply_registered = QM_FALSE;
    }
    if (cloud->command_registered) {
        (void)qm_iot_down_unregister(cloud->config.iot_handle,
                                     (char *)QM_AUDIO_CLOUD_METHOD);
        cloud->command_registered = QM_FALSE;
    }
}

static void build_status(qm_audio_cloud_t *cloud, qm_audio_cloud_operation_t *operation, qm_audio_cloud_protocol_status_t *status)
{
    qm_audio_control_snapshot_t snapshot;

    qm_memset(status, 0, sizeof(*status));
    status->state = QM_AUDIO_CONTROL_CANCELLED;
    status->result = QM_EOK;
    if (cloud == NULL || operation == NULL) {
        return;
    }
    if (qm_audio_control_get_snapshot(cloud->config.control,
                                      operation->operation_id,
                                      &snapshot) == QM_EOK) {
        status->operation_id = snapshot.operation_id;
        status->state = snapshot.state;
        status->result = snapshot.result;
        status->position_ms = snapshot.position_ms;
    }
    status->title = operation->title;
    status->codec = operation->codec;
    status->file_size = operation->file_size;
    status->duration_s = operation->duration_s;
    status->source_type = operation->source_type;
}

static void send_reply(qm_audio_cloud_t *cloud, const qm_audio_cloud_command_t *command, int result, char *payload)
{
    if (cloud == NULL || command == NULL || cloud->config.iot_handle == NULL) {
        return;
    }
    (void)qm_iot_down_reply_ex(cloud->config.iot_handle,
                               (char *)QM_AUDIO_CLOUD_METHOD,
                               (char *)(command->submethod != NULL ? command->submethod : ""),
                               command->msg_id,
                               qm_audio_cloud_protocol_result_code(result),
                               payload,
                               payload != NULL ? (int)qm_strlen(payload) : 0);
}

static void report_event(qm_audio_cloud_t *cloud, qm_audio_cloud_operation_t *operation, const qm_audio_event_t *event)
{
    qm_audio_cloud_protocol_status_t status;
    char *payload;
    qm_bool report;

    if (cloud == NULL || operation == NULL || event == NULL ||
        operation->release_started || cloud_stopping(cloud)) {
        return;
    }
    build_status(cloud, operation, &status);
    report = !operation->reported_state_valid ||
             operation->reported_state != status.state ||
             status.state == QM_AUDIO_CONTROL_ERROR;
    operation->reported_state = status.state;
    operation->reported_state_valid = QM_TRUE;
    if (!report) {
        return;
    }
    payload = qm_audio_cloud_protocol_encode_status(&status);
    if (payload != NULL) {
        uint32_t report_id = cloud->next_report_id++;

        if (cloud->next_report_id == 0U) {
            cloud->next_report_id = 1U;
        }
        (void)qm_iot_up_report_ex(cloud->config.iot_handle,
                                  (char *)QM_AUDIO_CLOUD_METHOD,
                                  "report",
                                  report_id,
                                  payload,
                                  (int)qm_strlen(payload));
        qm_audio_cloud_protocol_free_payload(payload);
    }
}

/**
 * @brief Resolve and submit one cloud PLAY command. / 解析并提交一条云端 PLAY 命令。
 *
 * On successful Control submission, resource-release ownership transfers from
 * the resolver to the cloud operation. Control 提交成功后，资源释放责任从解析器
 * 转交给云端操作。
 */
static int submit_play(qm_audio_cloud_t *cloud, const qm_audio_cloud_command_t *command)
{
    qm_audio_cloud_playback_t playback;
    qm_audio_cloud_operation_t *operation;
    qm_audio_control_play_request_t request;
    int ret;

    if (cloud == NULL || command == NULL || cloud->config.resolve_play == NULL) {
        return -QM_EINVAL;
    }
    /* The protocol ID is a bounded correlation key while this operation is
     * retained.  Reject an in-flight duplicate before resolving a second
     * source, so a retransmitted PLAY cannot create another audio operation. */
    if (command->msg_id != 0U) {
        uint32_t i;

        (void)qm_mutex_lock(&cloud->lock, QM_WAIT_FOREVER);
        for (i = 0U; i < cloud->operation_count; ++i) {
            if (cloud->operations[i] != NULL &&
                cloud->operations[i]->msg_id == command->msg_id &&
                !cloud->operations[i]->release_started) {
                (void)qm_mutex_unlock(&cloud->lock);
                return -QM_EBUSY;
            }
        }
        (void)qm_mutex_unlock(&cloud->lock);
    }
    qm_memset(&playback, 0, sizeof(playback));
    ret = cloud->config.resolve_play(command, &playback, cloud->config.user_ctx);
    if (ret != QM_EOK) {
        return ret;
    }
    /* The resolver may provide a stricter size. Use cloud metadata only when
     * it leaves an URI response size unknown. */
    if (playback.source.type == QM_AUDIO_SOURCE_URI &&
        playback.source.value.uri.expected_size == 0U &&
        command->file_size != 0U) {
        playback.source.value.uri.expected_size = command->file_size;
    }
    operation = (qm_audio_cloud_operation_t *)qm_calloc(1U, sizeof(*operation));
    if (operation == NULL) {
        if (playback.release != NULL) {
            playback.release(playback.release_ctx);
        }
        return -QM_ENOMEM;
    }
    operation->msg_id = command->msg_id;
    operation->playback = playback;
    operation->source_type = playback.source.type;
    operation->file_size = command->file_size;
    operation->duration_s = command->duration_s;
    if (command->title != NULL) {
        operation->title = qm_strdup(command->title);
    }
    if (command->codec != NULL) {
        operation->codec = qm_strdup(command->codec);
    }
    if ((command->title != NULL && operation->title == NULL) ||
        (command->codec != NULL && operation->codec == NULL)) {
        free_operation(operation);
        return -QM_ENOMEM;
    }
    (void)qm_mutex_lock(&cloud->lock, QM_WAIT_FOREVER);
    if (cloud->stopping || cloud->operation_count >= cloud->operation_capacity) {
        (void)qm_mutex_unlock(&cloud->lock);
        free_operation(operation);
        return cloud->stopping ? -QM_EINTR : -QM_EFULL;
    }
    cloud->operations[cloud->operation_count++] = operation;
    (void)qm_mutex_unlock(&cloud->lock);

    qm_memset(&request, 0, sizeof(request));
    request.priority = playback.priority;
    request.admission = playback.admission_policy;
    request.admission_timeout_ms = playback.acquire_timeout_ms;
    request.source = playback.source;
    request.codec = playback.codec;
    ret = qm_audio_control_play(cloud->config.control, &request,
                                &operation->operation_id);
    if (ret != QM_EOK) {
        (void)qm_mutex_lock(&cloud->lock, QM_WAIT_FOREVER);
        /* Control may already have emitted an event into our queue. Keep the
         * operation as a tombstone until those queued events are consumed. */
        operation->release_started = QM_TRUE;
        (void)qm_mutex_unlock(&cloud->lock);
    }
    return ret;
}

static int control_current(qm_audio_cloud_t *cloud, qm_audio_cloud_command_type_t type, const qm_audio_cloud_command_t *command)
{
    qm_audio_cloud_operation_t *operation;
    int ret;

    if (cloud == NULL) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&cloud->lock, QM_WAIT_FOREVER);
    operation = latest_operation_locked(cloud);
    (void)qm_mutex_unlock(&cloud->lock);
    if (operation == NULL) {
        return -QM_EINVAL;
    }
    switch (type) {
        case QM_AUDIO_CLOUD_COMMAND_STOP:
            return qm_audio_control_stop(cloud->config.control,
                                         operation->operation_id, 0U);
        case QM_AUDIO_CLOUD_COMMAND_PAUSE:
            return qm_audio_control_pause(cloud->config.control,
                                          operation->operation_id,
                                          QM_AUDIO_PAUSE_HOLD_OUTPUT);
        case QM_AUDIO_CLOUD_COMMAND_RESUME:
            return qm_audio_control_resume(cloud->config.control,
                                           operation->operation_id);
        case QM_AUDIO_CLOUD_COMMAND_LIMIT:
            ret = qm_audio_control_pause(cloud->config.control,
                                         operation->operation_id,
                                         QM_AUDIO_PAUSE_HOLD_OUTPUT);
            if (ret == QM_EOK && cloud->config.on_limit != NULL) {
                ret = cloud->config.on_limit(command, cloud->config.user_ctx);
            }
            return ret;
        default:
            return -QM_ENOSYS;
    }
}

static int execute_command(const qm_audio_cloud_command_t *command, void *user_ctx)
{
    qm_audio_cloud_t *cloud = (qm_audio_cloud_t *)user_ctx;
    qm_audio_cloud_operation_t *operation;
    qm_audio_cloud_protocol_status_t status;
    char *payload = NULL;
    int ret;

    if (cloud == NULL || command == NULL || cloud_stopping(cloud)) {
        return -QM_EINTR;
    }
    if (cloud->config.authorize != NULL) {
        ret = cloud->config.authorize(command, cloud->config.user_ctx);
        if (ret != QM_EOK) {
            send_reply(cloud, command, ret, NULL);
            return ret;
        }
    }
    switch (command->type) {
        case QM_AUDIO_CLOUD_COMMAND_PLAY:
            ret = submit_play(cloud, command);
            break;
        case QM_AUDIO_CLOUD_COMMAND_STOP:
        case QM_AUDIO_CLOUD_COMMAND_PAUSE:
        case QM_AUDIO_CLOUD_COMMAND_RESUME:
        case QM_AUDIO_CLOUD_COMMAND_LIMIT:
            ret = control_current(cloud, command->type, command);
            break;
        case QM_AUDIO_CLOUD_COMMAND_GET:
            (void)qm_mutex_lock(&cloud->lock, QM_WAIT_FOREVER);
            operation = latest_operation_locked(cloud);
            (void)qm_mutex_unlock(&cloud->lock);
            build_status(cloud, operation, &status);
            payload = qm_audio_cloud_protocol_encode_status(&status);
            ret = payload != NULL ? QM_EOK : -QM_ENOMEM;
            break;
        default:
            ret = -QM_ENOSYS;
            break;
    }
    send_reply(cloud, command, ret, payload);
    qm_audio_cloud_protocol_free_payload(payload);
    return ret;
}

static void process_packet(qm_audio_cloud_t *cloud, qm_audio_cloud_packet_t *packet)
{
    if (cloud == NULL || packet == NULL || cloud_stopping(cloud)) {
        return;
    }
    /* Invalid packets are rejected by the protocol adapter and do not enter Control. */
    (void)qm_audio_cloud_protocol_decode(packet->data, packet->len,
                                          execute_command, cloud);
}

/**
 * @brief Report staged Control events in cloud-worker context. / 在云端工作任务上下文中上报已暂存的 Control 事件。
 */
static void process_pending_events(qm_audio_cloud_t *cloud)
{
    uint32_t i;

    for (i = 0U; cloud != NULL; ++i) {
        qm_audio_cloud_operation_t *operation = NULL;
        qm_audio_event_t event;

        (void)qm_mutex_lock(&cloud->lock, QM_WAIT_FOREVER);
        if (i >= cloud->operation_count) {
            (void)qm_mutex_unlock(&cloud->lock);
            break;
        }
        operation = cloud->operations[i];
        if (operation != NULL && operation->pending_event_valid) {
            event = operation->pending_event;
            operation->pending_event_valid = QM_FALSE;
        } else {
            operation = NULL;
        }
        (void)qm_mutex_unlock(&cloud->lock);
        if (operation != NULL) {
            report_event(cloud, operation, &event);
        }
    }
}

/**
 * @brief Retry release of terminal cloud-owned operations. / 重试释放已终止的云端拥有操作。
 */
static void cleanup_terminal_operations(qm_audio_cloud_t *cloud)
{
    uint32_t i = 0U;

    while (cloud != NULL) {
        qm_audio_cloud_operation_t *operation;
        qm_audio_control_snapshot_t snapshot;
        int ret;

        (void)qm_mutex_lock(&cloud->lock, QM_WAIT_FOREVER);
        if (i >= cloud->operation_count) {
            (void)qm_mutex_unlock(&cloud->lock);
            break;
        }
        operation = cloud->operations[i];
        if (operation == NULL) {
            (void)qm_mutex_unlock(&cloud->lock);
            ++i;
            continue;
        }
        if (operation->release_started && !operation->pending_event_valid) {
            remove_operation_locked(cloud, operation);
            (void)qm_mutex_unlock(&cloud->lock);
            free_operation(operation);
            continue;
        }
        (void)qm_mutex_unlock(&cloud->lock);
        if (qm_audio_control_get_snapshot(cloud->config.control,
                                          operation->operation_id,
                                          &snapshot) != QM_EOK ||
            !qm_audio_control_terminal(snapshot.state)) {
            ++i;
            continue;
        }
        (void)qm_mutex_lock(&cloud->lock, QM_WAIT_FOREVER);
        if (operation->release_started || operation->pending_event_valid) {
            (void)qm_mutex_unlock(&cloud->lock);
            ++i;
            continue;
        }
        operation->release_started = QM_TRUE;
        (void)qm_mutex_unlock(&cloud->lock);
        ret = qm_audio_control_release(cloud->config.control,
                                       operation->operation_id,
                                       QM_WAIT_FOREVER);
        (void)qm_mutex_lock(&cloud->lock, QM_WAIT_FOREVER);
        if (ret == QM_EOK) {
            remove_operation_locked(cloud, operation);
            (void)qm_mutex_unlock(&cloud->lock);
            free_operation(operation);
            continue;
        }
        operation->release_started = QM_FALSE;
        (void)qm_mutex_unlock(&cloud->lock);
        /* A terminal release can be temporarily busy while the Control
         * executor drains. Retry without spinning when the system is quiet. */
        qm_msleep(10);
        if (!cloud_stopping(cloud)) {
            (void)qm_sem_signal(&cloud->wake_sem);
        }
        ++i;
    }
}

static void free_work(qm_audio_cloud_t *cloud, qm_audio_cloud_work_t *work)
{
    if (work == NULL) {
        return;
    }
    if (work->type == QM_AUDIO_CLOUD_CMD) {
        qm_free(work->packet);
        if (cloud != NULL && qm_sem_is_valid(&cloud->command_slots)) {
            (void)qm_sem_signal(&cloud->command_slots);
        }
    }
    qm_free(work);
}

/**
 * @brief Serialize decoded cloud commands, status reports, and cleanup. / 串行处理已解码云端命令、状态上报与资源清理。
 */
static void cloud_task(void *arg)
{
    qm_audio_cloud_t *cloud = (qm_audio_cloud_t *)arg;

    while (cloud != NULL) {
        unsigned int size;
        qm_audio_cloud_work_t *work = NULL;

        if (qm_sem_wait(&cloud->wake_sem, QM_WAIT_FOREVER) != QM_EOK) {
            continue;
        }
        if (cloud_stopping(cloud)) {
            (void)qm_mutex_lock(&cloud->lock, QM_WAIT_FOREVER);
            if (cloud->exit_requested) {
                (void)qm_mutex_unlock(&cloud->lock);
                break;
            }
            (void)qm_mutex_unlock(&cloud->lock);
        }
        for (;;) {
            size = sizeof(work);
            work = NULL;
            if (qm_queue_recv(&cloud->queue, &work, &size, QM_NO_WAIT) != QM_EOK) {
                break;
            }
            if (work != NULL && work->type == QM_AUDIO_CLOUD_CMD) {
                (void)qm_mutex_lock(&cloud->lock, QM_WAIT_FOREVER);
                if (cloud->pending_commands > 0U) {
                    cloud->pending_commands--;
                }
                (void)qm_mutex_unlock(&cloud->lock);
                process_packet(cloud, work->packet);
            }
            free_work(cloud, work);
        }
        process_pending_events(cloud);
        cleanup_terminal_operations(cloud);
    }
    if (cloud != NULL) {
        (void)qm_mutex_lock(&cloud->lock, QM_WAIT_FOREVER);
        cloud->task_stopped = QM_TRUE;
        (void)qm_mutex_unlock(&cloud->lock);
        (void)qm_sem_signal(&cloud->stopped_sem);
    }
}

static void down_cb(void *handle, const qm_iot_mqtt_recv_t *packet, void *user_data)
{
    qm_audio_cloud_t *cloud = (qm_audio_cloud_t *)user_data;
    qm_audio_cloud_packet_t *copy;
    uint32_t len;

    QM_UNUSED_ARG(handle);
    if (cloud == NULL || cloud_stopping(cloud) || packet == NULL ||
        packet->type != QM_IOT_MQTTRECV_PUB || packet->data.pub.payload == NULL ||
        packet->data.pub.payload_len == 0U) {
        return;
    }
    len = packet->data.pub.payload_len;
    copy = (qm_audio_cloud_packet_t *)qm_malloc(sizeof(*copy) + len);
    if (copy == NULL) {
        return;
    }
    copy->len = len;
    qm_memcpy(copy->data, packet->data.pub.payload, len);
    copy->data[len] = '\0';
    if (enqueue_packet(cloud, copy) != QM_EOK) {
        qm_free(copy);
    }
}

static void reply_down_cb(void *handle, const qm_iot_mqtt_recv_t *packet, void *user_data)
{
    QM_UNUSED_ARG(handle);
    QM_UNUSED_ARG(packet);
    QM_UNUSED_ARG(user_data);
    /* playerReply is a cloud acknowledgement.  Replying would form a loop. */
}

static void free_queued_work(qm_audio_cloud_t *cloud)
{
    qm_audio_cloud_work_t *work = NULL;
    unsigned int size;

    if (cloud == NULL || !qm_queue_is_valid(&cloud->queue)) {
        return;
    }
    for (;;) {
        size = sizeof(work);
        work = NULL;
        if (qm_queue_recv(&cloud->queue, &work, &size, QM_NO_WAIT) != QM_EOK) {
            break;
        }
        free_work(cloud, work);
    }
}

int qm_audio_cloud_init(const qm_audio_cloud_config_t *config, qm_audio_cloud_t **out_cloud)
{
    qm_audio_cloud_t *cloud;
    uint32_t queue_slots;
    int ret;

    if (out_cloud == NULL) {
        return -QM_EINVAL;
    }
    *out_cloud = NULL;
    if (config == NULL || config->iot_handle == NULL || config->control == NULL ||
        config->resolve_play == NULL) {
        return -QM_EINVAL;
    }
    cloud = (qm_audio_cloud_t *)qm_calloc(1U, sizeof(*cloud));
    if (cloud == NULL) {
        return -QM_ENOMEM;
    }
    cloud->config = *config;
    cloud->operation_capacity = CONFIG_QM_AUDIO_CLOUD_MAX_OPERATIONS;
    cloud->next_report_id = 1U;
    cloud->operations = (qm_audio_cloud_operation_t **)qm_calloc(
        cloud->operation_capacity, sizeof(*cloud->operations));
    if (cloud->operations == NULL) {
        ret = -QM_ENOMEM;
        goto fail_uninit;
    }
    ret = qm_mutex_new(&cloud->lock);
    if (ret != QM_EOK) {
        goto fail_uninit;
    }
    ret = qm_queue_new(&cloud->queue, CONFIG_QM_AUDIO_CLOUD_QUEUE_LEN,
                       sizeof(qm_audio_cloud_work_t *));
    if (ret != QM_EOK) {
        goto fail;
    }
    queue_slots = CONFIG_QM_AUDIO_CLOUD_QUEUE_LEN > 1U ?
                  CONFIG_QM_AUDIO_CLOUD_QUEUE_LEN - 1U : 1U;
    ret = qm_sem_new(&cloud->command_slots, queue_slots);
    if (ret != QM_EOK) {
        goto fail;
    }
    ret = qm_sem_new(&cloud->wake_sem, 0U);
    if (ret != QM_EOK) {
        goto fail;
    }
    ret = qm_sem_new(&cloud->stopped_sem, 0U);
    if (ret != QM_EOK) {
        goto fail;
    }
    ret = qm_task_new(&cloud->task, "audio_cloud", cloud_task, cloud,
                      CONFIG_QM_AUDIO_CLOUD_TASK_STACK_SIZE,
                      CONFIG_QM_AUDIO_CLOUD_TASK_PRIORITY);
    if (ret != QM_EOK) {
        goto fail;
    }
    cloud->task_started = QM_TRUE;
    ret = qm_iot_down_register(cloud->config.iot_handle,
                               (char *)QM_AUDIO_CLOUD_METHOD,
                               down_cb, cloud);
    if (ret != QM_EOK) {
        goto fail;
    }
    cloud->command_registered = QM_TRUE;
    ret = qm_iot_down_register(cloud->config.iot_handle,
                               (char *)QM_AUDIO_CLOUD_REPLY_METHOD,
                               reply_down_cb, cloud);
    if (ret != QM_EOK) {
        goto fail;
    }
    cloud->reply_registered = QM_TRUE;
    ret = qm_audio_control_subscribe(cloud->config.control, control_event, cloud,
                                     &cloud->event_subscription);
    if (ret != QM_EOK) {
        goto fail;
    }
    *out_cloud = cloud;
    return QM_EOK;

fail:
    (void)qm_audio_cloud_shutdown(cloud, QM_WAIT_FOREVER);
    (void)qm_audio_cloud_destroy(cloud);
    return ret;

fail_uninit:
    qm_free(cloud->operations);
    qm_free(cloud);
    return ret;
}

/**
 * @brief Stop ingress before releasing cloud-owned operations. / 在释放云端拥有操作前停止入站命令。
 *
 * Unregistering first prevents a new packet from creating an operation during
 * shutdown. 先注销方法，避免关闭期间新数据包创建操作。
 */
int qm_audio_cloud_shutdown(qm_audio_cloud_t *cloud, uint32_t timeout_ms)
{
    qm_utils_time_t deadline;
    uint32_t i = 0U;
    int ret = QM_EOK;

    if (cloud == NULL || !qm_mutex_is_valid(&cloud->lock)) {
        return -QM_EINVAL;
    }
    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    (void)qm_mutex_lock(&cloud->lock, QM_WAIT_FOREVER);
    cloud->stopping = QM_TRUE;
    cloud->exit_requested = QM_TRUE;
    (void)qm_mutex_unlock(&cloud->lock);
    stop_registration(cloud);
    if (cloud->event_subscription != NULL) {
        (void)qm_audio_control_unsubscribe(cloud->config.control,
                                           cloud->event_subscription);
        cloud->event_subscription = NULL;
    }
    if (cloud->task_started && !cloud->task_stopped) {
        uint32_t left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
                        qm_utils_time_left(&deadline);

        (void)qm_sem_signal(&cloud->wake_sem);
        if (qm_sem_wait(&cloud->stopped_sem, left) != QM_EOK) {
            return -QM_ETIMEOUT;
        }
    }
    free_queued_work(cloud);
    while (i < cloud->operation_count) {
        qm_audio_cloud_operation_t *operation = cloud->operations[i];
        qm_audio_control_snapshot_t snapshot;
        uint32_t left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
                        qm_utils_time_left(&deadline);

        if (operation == NULL) {
            ++i;
            continue;
        }
        (void)qm_audio_control_stop(cloud->config.control, operation->operation_id, 0U);
        if (qm_audio_control_wait(cloud->config.control, operation->operation_id, left) ==
            -QM_ETIMEOUT) {
            return -QM_ETIMEOUT;
        }
        if (qm_audio_control_get_snapshot(cloud->config.control, operation->operation_id,
                                          &snapshot) != QM_EOK ||
            !qm_audio_control_terminal(snapshot.state)) {
            ret = -QM_EBUSY;
            ++i;
            continue;
        }
        left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
               qm_utils_time_left(&deadline);
        if (qm_audio_control_release(cloud->config.control, operation->operation_id,
                                     left) != QM_EOK) {
            ret = -QM_EBUSY;
            ++i;
            continue;
        }
        (void)qm_mutex_lock(&cloud->lock, QM_WAIT_FOREVER);
        remove_operation_locked(cloud, operation);
        (void)qm_mutex_unlock(&cloud->lock);
        free_operation(operation);
    }
    return ret;
}

int qm_audio_cloud_destroy(qm_audio_cloud_t *cloud)
{
    if (cloud == NULL || !qm_mutex_is_valid(&cloud->lock)) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&cloud->lock, QM_WAIT_FOREVER);
    if (!cloud->stopping || (cloud->task_started && !cloud->task_stopped) ||
        cloud->operation_count != 0U) {
        (void)qm_mutex_unlock(&cloud->lock);
        return -QM_EBUSY;
    }
    (void)qm_mutex_unlock(&cloud->lock);
    free_queued_work(cloud);
    if (qm_sem_is_valid(&cloud->stopped_sem)) {
        (void)qm_sem_free(&cloud->stopped_sem);
    }
    if (qm_sem_is_valid(&cloud->wake_sem)) {
        (void)qm_sem_free(&cloud->wake_sem);
    }
    if (qm_sem_is_valid(&cloud->command_slots)) {
        (void)qm_sem_free(&cloud->command_slots);
    }
    if (qm_queue_is_valid(&cloud->queue)) {
        (void)qm_queue_free(&cloud->queue);
    }
    (void)qm_mutex_free(&cloud->lock);
    qm_free(cloud->operations);
    qm_free(cloud);
    return QM_EOK;
}

int qm_audio_cloud_command_pending(const qm_audio_cloud_t *cloud)
{
    qm_audio_cloud_t *mutable_cloud = (qm_audio_cloud_t *)cloud;
    uint32_t pending = 0U;

    if (mutable_cloud == NULL || !qm_mutex_is_valid(&mutable_cloud->lock)) {
        return 0;
    }
    (void)qm_mutex_lock(&mutable_cloud->lock, QM_WAIT_FOREVER);
    pending = mutable_cloud->pending_commands;
    (void)qm_mutex_unlock(&mutable_cloud->lock);
    return pending != 0U ? 1 : 0;
}

#else

int qm_audio_cloud_init(const qm_audio_cloud_config_t *config, qm_audio_cloud_t **out_cloud)
{
    QM_UNUSED_ARG(config);
    if (out_cloud != NULL) {
        *out_cloud = NULL;
    }
    return -QM_ENOSYS;
}

int qm_audio_cloud_shutdown(qm_audio_cloud_t *cloud, uint32_t timeout_ms)
{
    QM_UNUSED_ARG(cloud);
    QM_UNUSED_ARG(timeout_ms);
    return -QM_ENOSYS;
}

int qm_audio_cloud_destroy(qm_audio_cloud_t *cloud)
{
    QM_UNUSED_ARG(cloud);
    return -QM_ENOSYS;
}

int qm_audio_cloud_command_pending(const qm_audio_cloud_t *cloud)
{
    QM_UNUSED_ARG(cloud);
    return 0;
}

#endif
