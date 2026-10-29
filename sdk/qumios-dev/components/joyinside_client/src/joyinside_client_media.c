/**
 * @file joyinside_client_media.c
 * @brief AUDIO_BOOK media state machine and backend worker bridge.
 */

#include "joyinside_client_media.h"
#include "joyinside_client_internal.h"
#include "cJSON.h"
#include "qm_string.h"

#if CONFIG_JOYINSIDE_CLIENT_MEDIA_SUPPORT

#define LOG_TAG "joyinside_media"
#ifndef CONFIG_JOYINSIDE_CLIENT_MEDIA_TASK_STACK_SIZE
#define CONFIG_JOYINSIDE_CLIENT_MEDIA_TASK_STACK_SIZE 4096U
#endif
#ifndef CONFIG_JOYINSIDE_CLIENT_MEDIA_TASK_PRIORITY
#define CONFIG_JOYINSIDE_CLIENT_MEDIA_TASK_PRIORITY 20U
#endif
#ifndef CONFIG_JOYINSIDE_CLIENT_MEDIA_QUEUE_LEN
#define CONFIG_JOYINSIDE_CLIENT_MEDIA_QUEUE_LEN 8U
#endif

typedef struct {
    char *book_id;
    char *chapter_id;
    char *url;
    uint64_t progress;
    uint64_t total_length;
    qm_bool include_tts;
} media_resource_t;

typedef enum { MEDIA_PLAY, MEDIA_STOP, MEDIA_OBSERVATION } media_command_type_t;
typedef struct {
    media_command_type_t type;
    media_resource_t *resource;
    uint64_t generation;
    uint32_t epoch;
    joyinside_client_media_event_t event;
    uint64_t progress;
} media_command_t;

struct joyinside_client_media {
    joyinside_client_media_config_t config;
    qm_mutex_t lock;
    qm_queue_t queue;
    qm_sem_t wake;
    qm_sem_t exit;
    qm_task_t task;
    qm_bool quitting;
    qm_bool exited;
    qm_bool exit_acknowledged;
    qm_bool interrupt;
    uint32_t epoch;
    joyinside_client_media_state_t state;
    uint64_t generation;
    /* Only the worker owns resource pointers and invokes backend transitions.
     * The lock protects public admission, cancellation, and state snapshots. */
    media_resource_t *active;
    media_resource_t *pending;
    uint64_t progress;
    qm_bool player_active;
    qm_bool stop_sent;
};

static void resource_free(media_resource_t *resource)
{
    if (!resource) return;
    qm_free(resource->book_id);
    qm_free(resource->chapter_id);
    qm_free(resource->url);
    qm_free(resource);
}

static void set_state(joyinside_client_media_t *media, joyinside_client_media_state_t state)
{
    qm_mutex_lock(&media->lock, QM_WAIT_FOREVER);
    media->state = state;
    uint64_t generation = media->generation;
    qm_mutex_unlock(&media->lock);
    if (media->config.on_state) media->config.on_state(state, generation, media->config.context);
}

static int send_status(joyinside_client_media_t *media, qm_bool stop, qm_bool finish)
{
    cJSON *root = cJSON_CreateObject();
    char *json;
    int ret = -QM_ENOMEM;
    /* PLAY is sent after STARTED, while STOP is terminal and includes progress. */
    if (!root) return ret;
    if (!cJSON_AddStringToObject(root, "bookId", media->active->book_id) ||
        !cJSON_AddStringToObject(root, "chapterId", media->active->chapter_id)) goto out;
    if (stop && (!cJSON_AddNumberToObject(root, "progress", (double)media->progress) ||
                 !cJSON_AddBoolToObject(root, "finish", finish))) goto out;
    json = cJSON_PrintUnformatted(root);
    if (!json) goto out;
    ret = joyinside_client_audiobook_send_event(media->config.client,
        stop ? JOYINSIDE_CLIENT_AUDIOBOOK_EVENT_STOP : JOYINSIDE_CLIENT_AUDIOBOOK_EVENT_PLAY, json, 0U);
    cJSON_free(json);
out:
    cJSON_Delete(root);
    if (ret != QM_EOK) JOYINSIDE_CLIENT_LOGW(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL,
                                           "media status send failed ret=%d", ret);
    return ret;
}

static void finish_resource(joyinside_client_media_t *media, qm_bool finish, int error)
{
    if (!media->active) return;
    if (finish) media->progress = media->active->total_length;
    /* Terminal status and callback happen before releasing active resource data. */
    joyinside_client_audiobook_stop_ping(media->config.client);
    int ret = send_status(media, QM_TRUE, finish);
    uint64_t progress = media->progress;
    resource_free(media->active);
    media->active = NULL;
    media->player_active = QM_FALSE;
    media->stop_sent = QM_FALSE;
    set_state(media, JOYINSIDE_CLIENT_MEDIA_IDLE);
    if (media->config.on_finished) {
        media->config.on_finished(progress, finish, error != QM_EOK ? error : ret, media->config.context);
    }
}

static void start_player(joyinside_client_media_t *media)
{
    joyinside_client_media_play_t request = {
        .audio_url = media->active->url,
        .progress_sec = media->active->progress,
        .total_length_sec = media->active->total_length,
    };
    set_state(media, JOYINSIDE_CLIENT_MEDIA_STARTING);
    media->player_active = QM_TRUE;
    int ret = media->config.backend.start(media->config.backend.context, media->generation, &request);
    if (ret != QM_EOK) finish_resource(media, QM_FALSE, ret);
}

/**
 * @brief Promote pending media and start its keepalive before playback admission.
 *
 * @par 中文说明
 * 提升待播放媒体，并在申请播放前启动其保活。
 *
 * The server requires AUDIO_BOOK_PING during bridge TTS as well as audible
 * playback, so keepalive begins before waiting for the bridge drain.
 */
static void activate_pending(joyinside_client_media_t *media)
{
    if (media->active || !media->pending) return;
    media->active = media->pending;
    media->pending = NULL;
    media->progress = media->active->progress;
    qm_mutex_lock(&media->lock, QM_WAIT_FOREVER);
    ++media->generation;
    qm_mutex_unlock(&media->lock);
    /* Keepalive begins before bridge TTS or player admission. */
    int ret = joyinside_client_audiobook_start_ping(media->config.client, 0U, 0U);
    if (ret != QM_EOK) { finish_resource(media, QM_FALSE, ret); return; }
    if (!media->active->include_tts) {
        start_player(media);
        return;
    }
    set_state(media, JOYINSIDE_CLIENT_MEDIA_BRIDGE_WAIT);
    /* Bridge subscriptions are also asynchronous backend work. stop() must
     * cancel them and acknowledge quiescence before this context can die. */
    media->player_active = QM_TRUE;
    ret = media->config.backend.wait_bridge ?
        media->config.backend.wait_bridge(media->config.backend.context, media->generation) : -QM_ENOSYS;
    if (ret != QM_EOK) finish_resource(media, QM_FALSE, ret);
}

static void stop_resource(joyinside_client_media_t *media)
{
    if (!media->active) return;
    if (!media->player_active) { finish_resource(media, QM_FALSE, -QM_EINTR); return; }
    if (media->stop_sent) return;
    set_state(media, JOYINSIDE_CLIENT_MEDIA_STOPPING);
    int ret = media->config.backend.stop(media->config.backend.context, media->generation);
    media->stop_sent = ret == QM_EOK;
    if (ret != QM_EOK) JOYINSIDE_CLIENT_LOGE(LOG_TAG, "player stop not accepted ret=%d", ret);
}

/**
 * @brief Apply a player observation only to its current generation.
 *
 * @par 中文说明
 * 仅将播放器观察事件应用到当前 generation。
 *
 * The generation check prevents delayed callbacks from a preempted resource
 * from changing the current resource state or sending incorrect protocol status.
 */
static void observe_player(joyinside_client_media_t *media, const media_command_t *command)
{
    if (!media->active || command->generation != media->generation) return;
    if (command->progress >= media->active->progress && command->progress <= media->active->total_length) {
        media->progress = command->progress;
    }
    switch (command->event) {
    case JOYINSIDE_CLIENT_MEDIA_BRIDGE_DRAINED:
        if (media->state == JOYINSIDE_CLIENT_MEDIA_BRIDGE_WAIT) {
            media->player_active = QM_FALSE;
            start_player(media);
        }
        break;
    case JOYINSIDE_CLIENT_MEDIA_STARTED:
        if (media->state == JOYINSIDE_CLIENT_MEDIA_STARTING) {
            set_state(media, JOYINSIDE_CLIENT_MEDIA_PLAYING);
            (void)send_status(media, QM_FALSE, QM_FALSE);
        }
        break;
    case JOYINSIDE_CLIENT_MEDIA_FINISHED:
        if (media->state != JOYINSIDE_CLIENT_MEDIA_BRIDGE_WAIT && media->player_active)
            finish_resource(media, QM_TRUE, QM_EOK);
        break;
    case JOYINSIDE_CLIENT_MEDIA_STOPPED:
        if (media->player_active) finish_resource(media, QM_FALSE, -QM_EINTR);
        break;
    case JOYINSIDE_CLIENT_MEDIA_FAILED:
        finish_resource(media, QM_FALSE, -QM_EIO);
        break;
    default:
        break;
    }
}

/**
 * @brief Serialize media commands and player backend state transitions.
 *
 * @par 中文说明
 * 串行执行媒体命令和播放器后端状态转换。
 *
 * Epoch invalidates queued commands after interruption; generation separately
 * rejects delayed player observations. The worker owns all active/pending
 * resource lifetime transitions.
 */
static void media_worker(void *arg)
{
    joyinside_client_media_t *media = arg;
    media_command_t command;
    for (;;) {
        qm_mutex_lock(&media->lock, QM_WAIT_FOREVER);
        qm_bool quitting = media->quitting;
        qm_bool interrupt = media->interrupt;
        uint32_t epoch = media->epoch;
        media->interrupt = QM_FALSE;
        qm_mutex_unlock(&media->lock);
        if (interrupt || quitting) {
            resource_free(media->pending);
            media->pending = NULL;
            stop_resource(media);
        }
        if (quitting && !media->active) break;
        if (!quitting) activate_pending(media);
        unsigned int size = sizeof(command);
        if (qm_queue_recv(&media->queue, &command, &size, QM_NO_WAIT) != QM_EOK) {
            (void)qm_sem_wait(&media->wake, QM_WAIT_FOREVER);
            continue;
        }
        /* Epoch invalidates queued work after an interrupt; generation filters
         * delayed observations from a superseded backend operation. */
        if (command.type == MEDIA_PLAY) {
            if (quitting || command.epoch != epoch) {
                resource_free(command.resource);
            } else {
                resource_free(media->pending);
                media->pending = command.resource;
                stop_resource(media);
            }
        } else if (command.type == MEDIA_STOP) {
            if (media->pending &&
                qm_strcmp(media->pending->book_id, command.resource->book_id) == 0 &&
                qm_strcmp(media->pending->chapter_id, command.resource->chapter_id) == 0) {
                resource_free(media->pending);
                media->pending = NULL;
            }
            if (media->active &&
                qm_strcmp(media->active->book_id, command.resource->book_id) == 0 &&
                qm_strcmp(media->active->chapter_id, command.resource->chapter_id) == 0)
                stop_resource(media);
            resource_free(command.resource);
        } else {
            observe_player(media, &command);
        }
    }
    unsigned int size = sizeof(command);
    while (qm_queue_recv(&media->queue, &command, &size, QM_NO_WAIT) == QM_EOK) resource_free(command.resource);
    qm_mutex_lock(&media->lock, QM_WAIT_FOREVER);
    media->exited = QM_TRUE;
    qm_mutex_unlock(&media->lock);
    (void)qm_sem_signal(&media->exit);
    qm_task_exit(NULL);
}

static int post_command(joyinside_client_media_t *media, media_command_t *command)
{
    qm_mutex_lock(&media->lock, QM_WAIT_FOREVER);
    int ret = media->exited || (media->quitting && command->type != MEDIA_OBSERVATION) ? -QM_EBUSY : QM_EOK;
    /* Reject an observation before enqueueing when its backend generation can
     * no longer affect the active resource. */
    if (ret == QM_EOK && command->type == MEDIA_OBSERVATION &&
        (command->generation != media->generation || media->state == JOYINSIDE_CLIENT_MEDIA_IDLE)) ret = -QM_ENOENT;
    command->epoch = media->epoch;
    if (ret == QM_EOK) ret = qm_queue_send(&media->queue, command, sizeof(*command));
    if (ret == QM_EOK) (void)qm_sem_signal(&media->wake);
    qm_mutex_unlock(&media->lock);
    return ret;
}

/**
 * @brief Copy a borrowed audiobook request into a media-worker command.
 *
 * @par 中文说明
 * 将借用的有声书请求复制为媒体工作线程命令。
 *
 * This callback is reached from the JoyInside event path and must not retain
 * request pointers; the worker owns the deep-copied resource after enqueue.
 */
static int on_command(const joyinside_client_audiobook_request_t *request, void *context)
{
    joyinside_client_media_t *media = context;
    media_command_t command = {0};
    const char *type = request->content_info.event_type;
    /* The callback receives borrowed JSON-backed fields; deep-copy before the
     * asynchronous worker takes ownership. / 回调字段借用自 JSON，异步工作线程接管前必须深拷贝。 */
    if (!type) return -QM_EINVAL;
    if (qm_strcmp(type, JOYINSIDE_CLIENT_AUDIOBOOK_EVENT_STOP) == 0) {
        if (!request->event_data_info.book_id || !request->event_data_info.chapter_id) return -QM_EINVAL;
        command.resource = qm_calloc(1U, sizeof(*command.resource));
        if (!command.resource) return -QM_ENOMEM;
        command.resource->book_id = qm_strdup(request->event_data_info.book_id);
        command.resource->chapter_id = qm_strdup(request->event_data_info.chapter_id);
        if (!command.resource->book_id || !command.resource->chapter_id) {
            resource_free(command.resource);
            return -QM_ENOMEM;
        }
        command.type = MEDIA_STOP;
        int ret = post_command(media, &command);
        if (ret != QM_EOK) resource_free(command.resource);
        return ret;
    }
    if (qm_strcmp(type, JOYINSIDE_CLIENT_AUDIOBOOK_EVENT_PLAY) != 0) return QM_EOK;
    if (media->config.can_play && !media->config.can_play(request, media->config.context)) return -QM_EBUSY;
    if (!request->event_data_info.book_id || !request->event_data_info.chapter_id ||
        !request->event_data_info.audio_url ||
        request->event_data_info.progress > request->event_data_info.total_length) return -QM_EINVAL;
    media_resource_t *resource = qm_calloc(1U, sizeof(*resource));
    if (!resource) return -QM_ENOMEM;
    resource->book_id = qm_strdup(request->event_data_info.book_id);
    resource->chapter_id = qm_strdup(request->event_data_info.chapter_id);
    resource->url = qm_strdup(request->event_data_info.audio_url);
    resource->progress = request->event_data_info.progress;
    resource->total_length = request->event_data_info.total_length;
    resource->include_tts = request->event_data_info.include_tts;
    if (!resource->book_id || !resource->chapter_id || !resource->url) { resource_free(resource); return -QM_ENOMEM; }
    command.type = MEDIA_PLAY;
    command.resource = resource;
    int ret = post_command(media, &command);
    if (ret != QM_EOK) resource_free(resource);
    return ret;
}

static void on_disconnect(void *context)
{
    (void)joyinside_client_media_interrupt(context);
}

int joyinside_client_media_create(const joyinside_client_media_config_t *config, joyinside_client_media_t **out)
{
    joyinside_client_media_t *media;
    int ret;
    if (!out) return -QM_EINVAL;
    *out = NULL;
    if (!config || !config->client || !config->backend.start || !config->backend.stop) return -QM_EINVAL;
    joyinside_client_extensions_t *ext = joyinside_client_extensions(config->client);
    if (joyinside_client_running(config->client) || ext->audiobook.handle || ext->media_context) return -QM_EBUSY;
    media = qm_calloc(1U, sizeof(*media));
    if (!media) return -QM_ENOMEM;
    media->config = *config;
    ret = qm_mutex_new(&media->lock);
    if (ret != QM_EOK) goto fail_alloc;
    ret = qm_queue_new(&media->queue, config->queue_len ? config->queue_len : CONFIG_JOYINSIDE_CLIENT_MEDIA_QUEUE_LEN, sizeof(media_command_t));
    if (ret != QM_EOK) goto fail_lock;
    ret = qm_sem_new(&media->wake, 0U);
    if (ret != QM_EOK) goto fail_queue;
    ret = qm_sem_new(&media->exit, 0U);
    if (ret != QM_EOK) goto fail_wake;
    joyinside_client_audiobook_config_t audiobook = {0};
    audiobook.on_command = on_command;
    audiobook.user_context = media;
    ret = joyinside_client_audiobook_register(config->client, &audiobook);
    if (ret != QM_EOK) goto fail_exit;
    ret = qm_task_new(&media->task, "joyinside_media", media_worker, media,
        config->task_stack_size ? config->task_stack_size : CONFIG_JOYINSIDE_CLIENT_MEDIA_TASK_STACK_SIZE,
        config->task_priority ? config->task_priority : CONFIG_JOYINSIDE_CLIENT_MEDIA_TASK_PRIORITY);
    if (ret != QM_EOK) { joyinside_client_audiobook_unregister(config->client); goto fail_exit; }
    ext->media_context = media;
    ext->on_disconnect = on_disconnect;
    *out = media;
    return QM_EOK;
fail_exit:
    qm_sem_free(&media->exit);
fail_wake:
    qm_sem_free(&media->wake);
fail_queue:
    qm_queue_free(&media->queue);
fail_lock:
    qm_mutex_free(&media->lock);
fail_alloc:
    qm_free(media);
    return ret;
}

int joyinside_client_media_interrupt(joyinside_client_media_t *media)
{
    if (!media) return -QM_EINVAL;
    qm_mutex_lock(&media->lock, QM_WAIT_FOREVER);
    media->interrupt = QM_TRUE;
    ++media->epoch;
    (void)qm_sem_signal(&media->wake);
    qm_mutex_unlock(&media->lock);
    return QM_EOK;
}

int joyinside_client_media_notify(joyinside_client_media_t *media, uint64_t generation,
                                  joyinside_client_media_event_t event, uint64_t progress_sec)
{
    if (!media || !generation || event > JOYINSIDE_CLIENT_MEDIA_FAILED || event < 0) return -QM_EINVAL;
    media_command_t command = {.type = MEDIA_OBSERVATION, .generation = generation, .event = event, .progress = progress_sec};
    return post_command(media, &command);
}

joyinside_client_media_state_t joyinside_client_media_get_state(joyinside_client_media_t *media, uint64_t *generation)
{
    if (!media) return JOYINSIDE_CLIENT_MEDIA_IDLE;
    qm_mutex_lock(&media->lock, QM_WAIT_FOREVER);
    joyinside_client_media_state_t state = media->state;
    if (generation) *generation = media->generation;
    qm_mutex_unlock(&media->lock);
    return state;
}

int joyinside_client_media_destroy(joyinside_client_media_t *media, uint32_t timeout_ms)
{
    if (!media) return -QM_EINVAL;
    if (joyinside_client_running(media->config.client)) return -QM_EBUSY;
    qm_mutex_lock(&media->lock, QM_WAIT_FOREVER);
    media->quitting = QM_TRUE;
    (void)qm_sem_signal(&media->wake);
    qm_mutex_unlock(&media->lock);
    /* Always consume the exit acknowledgement before freeing its semaphore. */
    int ret;
    if (!media->exit_acknowledged) {
        ret = qm_sem_wait(&media->exit, timeout_ms);
        if (ret != QM_EOK) return ret;
        media->exit_acknowledged = QM_TRUE;
    }
    ret = qm_task_exit(&media->task);
    if (ret != QM_EOK) return ret;
    joyinside_client_extensions_t *ext = joyinside_client_extensions(media->config.client);
    ext->on_disconnect = NULL;
    ext->media_context = NULL;
    joyinside_client_audiobook_unregister(media->config.client);
    qm_sem_free(&media->exit);
    qm_sem_free(&media->wake);
    qm_queue_free(&media->queue);
    qm_mutex_free(&media->lock);
    qm_free(media);
    return QM_EOK;
}
#endif
