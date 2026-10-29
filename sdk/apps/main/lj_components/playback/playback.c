#include "playback.h"

#include "agent_app.h"

#include "qm.h"
#include "qm_errno.h"
#include "qm_log.h"
#include "qm_string.h"
#include "qm_work.h"

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

#if defined(CONFIG_QM_RES_MGR_SUPPORT) && CONFIG_QM_RES_MGR_SUPPORT
#include "qm_res_mgr.h"
#endif

#define LOG_TAG "playback"

#define PLAYBACK_AUDIO_WAIT_MS    3000U
#define PLAYBACK_MQTT_TIMEOUT_MS  15000U
#define PLAYBACK_PAGE_SIZE        10U
#define PLAYBACK_COMMAND_QUEUE_LEN 4U
#define PLAYBACK_WORKER_STACK      (12 * 1024)
#define PLAYBACK_WORKER_PRIORITY   18
#define PLAYBACK_GUIDE_TIMEOUT_MS  30000U

static qm_audio_control_t *s_control;
static qm_audio_subscription_t s_sub;
static qm_audio_operation_id_t s_op;
static uint8_t s_op_live;
static qm_mutex_t s_lock;
static uint8_t s_lock_ok;
static qm_work_t s_finish_work;
static qm_queue_t s_command_queue;
static StaticTask_t s_command_tcb;
static StackType_t *s_command_stack;
static qm_sem_t s_command_done;
static uint8_t s_command_ready;

static void playback_command_task(void *arg);
int playback_stop(void);

typedef struct {
    char *guide_words;
    char *url;
    char *audio_type;
    char *title;
    uint32_t file_size;
    uint32_t duration_s;
} playback_cloud_command_t;

static int media_is_pcm(const char *audio_type)
{
    if (audio_type == NULL) {
        return 0;
    }
    return qm_strcasecmp(audio_type, "pcm") == 0;
}

static char *copy_text(const char *text)
{
    size_t len;
    char *copy;

    if (text == NULL || text[0] == '\0') {
        return NULL;
    }
    len = qm_strlen(text);
    copy = (char *)qm_malloc(len + 1U);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, len + 1U);
    return copy;
}

static void free_cloud_command(playback_cloud_command_t *command)
{
    if (command == NULL) return;
    qm_free(command->guide_words);
    qm_free(command->url);
    qm_free(command->audio_type);
    qm_free(command->title);
    qm_free(command);
}

static void playback_restore_audio(void)
{
    (void)agent_app_set_resource_mode(QM_FALSE);
}

static int release_current(void)
{
    qm_audio_operation_id_t id;

    if (!s_lock_ok) {
        return -QM_EINIT;
    }
    qm_mutex_lock(&s_lock, QM_WAIT_FOREVER);
    if (!s_op_live || s_control == NULL) {
        qm_mutex_unlock(&s_lock);
        return -QM_ENOENT;
    }
    id = s_op;
    s_op_live = 0;
    qm_mutex_unlock(&s_lock);

    (void)qm_audio_control_release(s_control, id, 1000);
    playback_restore_audio();
    return QM_EOK;
}

static void playback_finish_work(void *arg)
{
    (void)arg;
    (void)release_current();
}

static void playback_event(const qm_audio_event_t *event, void *user_data)
{
    (void)user_data;
    if (event == NULL) {
        return;
    }

    QM_LOGI(LOG_TAG, "event=%d operation=%u result=%d position=%u",
            (int)event->type,
            (unsigned)event->operation_id,
            (int)event->result,
            (unsigned)event->position_ms);
    if (!s_op_live || event->operation_id != s_op) {
        return;
    }
    if (event->type == QM_AUDIO_EVENT_FINISHED ||
        event->type == QM_AUDIO_EVENT_CANCELLED ||
        event->type == QM_AUDIO_EVENT_PREEMPTED ||
        event->type == QM_AUDIO_EVENT_ERROR) {
        qm_post_delayed_action(&s_finish_work, playback_finish_work, NULL, 0);
    }
}

static void destroy_control(void)
{
    if (s_control == NULL) {
        return;
    }
    (void)qm_audio_control_unsubscribe(s_control, s_sub);
    (void)qm_audio_control_shutdown(s_control, 1000);
    (void)qm_audio_control_destroy(s_control);
    s_control = NULL;
    s_op_live = 0;
}

int playback_init(void)
{
    int ret;
    uint8_t queue_ready = 0U;
    uint8_t sem_created = 0U;
    uint8_t sem_ready = 0U;

    if (s_control != NULL) {
        return -QM_EBUSY;
    }
    if (!s_lock_ok) {
        if (qm_mutex_new(&s_lock) != QM_EOK) {
            return -QM_ENOMEM;
        }
        s_lock_ok = 1;
    }

    ret = qm_audio_control_create(&s_control);
    if (ret != QM_EOK || s_control == NULL) {
        s_control = NULL;
        return ret != QM_EOK ? ret : -QM_EINIT;
    }
    ret = qm_audio_control_subscribe(s_control, playback_event, NULL, &s_sub);
    if (ret != QM_EOK) {
        (void)qm_audio_control_shutdown(s_control, 1000);
        (void)qm_audio_control_destroy(s_control);
        s_control = NULL;
        return ret;
    }

    s_op_live = 0;
    ret = qm_queue_new(&s_command_queue,
                       PLAYBACK_COMMAND_QUEUE_LEN,
                       sizeof(playback_cloud_command_t *));
    if (ret == QM_EOK) {
        queue_ready = 1U;
        ret = qm_sem_new(&s_command_done, 0);
        if (ret == QM_EOK) {
            sem_created = 1U;
        }
    }
    if (ret == QM_EOK && qm_sem_is_valid(&s_command_done)) {
        sem_ready = 1U;
    }
    if (!queue_ready || !sem_ready) {
        if (sem_created) {
            (void)qm_sem_free(&s_command_done);
        }
        if (queue_ready) {
            (void)qm_queue_free(&s_command_queue);
        }
        destroy_control();
        return -QM_ENOMEM;
    }
    if (s_command_stack == NULL) {
        s_command_stack = (StackType_t *)heap_caps_malloc(
            PLAYBACK_WORKER_STACK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_command_stack == NULL) {
            (void)qm_sem_free(&s_command_done);
            (void)qm_queue_free(&s_command_queue);
            destroy_control();
            return -QM_ENOMEM;
        }
    }
    if (xTaskCreateStatic(playback_command_task, "playback",
                          PLAYBACK_WORKER_STACK / sizeof(StackType_t),
                          NULL, PLAYBACK_WORKER_PRIORITY,
                          s_command_stack, &s_command_tcb) == NULL) {
        (void)qm_sem_free(&s_command_done);
        (void)qm_queue_free(&s_command_queue);
        destroy_control();
        return -QM_ENOMEM;
    }
    s_command_ready = 1U;
    return QM_EOK;
}

static void fill_pcm_codec(qm_audio_codec_config_t *codec)
{
    qm_memset(codec, 0, sizeof(*codec));
    codec->codec = QM_AUDIO_CODEC_PCM;
    codec->framing = QM_AUDIO_CODEC_FRAMING_STREAM;
    codec->pcm.sample_rate_hz = 16000;
    codec->pcm.channels = 1;
    codec->pcm.sample = QM_AUDIO_PCM_S16;
}

static int submit_play(const qm_audio_control_play_request_t *request)
{
    qm_audio_operation_id_t operation = 0;
    int ret;

    if (s_control == NULL || request == NULL) {
        return -QM_EINIT;
    }
    if (s_op_live) {
        (void)playback_stop();
    }

    ret = qm_audio_control_play(s_control, request, &operation);
    if (ret != QM_EOK) {
        QM_LOGW(LOG_TAG, "play submit failed ret=%d", ret);
        return ret;
    }
    qm_mutex_lock(&s_lock, QM_WAIT_FOREVER);
    s_op = operation;
    s_op_live = 1;
    qm_mutex_unlock(&s_lock);
    QM_LOGI(LOG_TAG, "play submitted operation=%u", (unsigned)operation);
    return QM_EOK;
}

static void process_cloud_command(playback_cloud_command_t *command)
{
    int ret;

    if (command == NULL) {
        return;
    }
    if (s_op_live) {
        (void)playback_stop();
    }
    (void)agent_app_abort_output();

    if (command->guide_words != NULL) {
        ret = agent_app_user_corpus(command->guide_words);
        if (ret == QM_EOK) {
            ret = agent_app_wait_tts_complete(PLAYBACK_GUIDE_TIMEOUT_MS);
        }
        if (ret != QM_EOK) {
            QM_LOGW(LOG_TAG, "guide words failed ret=%d", ret);
            (void)agent_app_set_resource_mode(QM_FALSE);
            return;
        }
        QM_LOGI(LOG_TAG, "guide words playback complete");
    }

    if (command->url == NULL || command->url[0] == '\0') {
        (void)agent_app_set_resource_mode(QM_FALSE);
        return;
    }
    (void)agent_app_set_resource_mode(QM_TRUE);
    ret = playback_play_url(command->url,
                            command->file_size,
                            command->duration_s);
    if (ret != QM_EOK) {
        QM_LOGW(LOG_TAG, "resource play failed ret=%d", ret);
        (void)agent_app_set_resource_mode(QM_FALSE);
    }
}

static void playback_command_task(void *arg)
{
    (void)arg;
    for (;;) {
        playback_cloud_command_t *command = NULL;
        unsigned int size = sizeof(command);

        if (qm_queue_recv(&s_command_queue, &command, &size,
                          QM_WAIT_FOREVER) != QM_EOK) {
            continue;
        }
        if (command == NULL) {
            break;
        }
        process_cloud_command(command);
        free_cloud_command(command);
    }
    (void)qm_sem_signal(&s_command_done);
    vTaskDelete(NULL);
}

int playback_submit_cloud_play(const playback_cloud_play_request_t *request)
{
    playback_cloud_command_t *command;

    if (!s_command_ready || request == NULL || request->url == NULL ||
        request->url[0] == '\0') {
        return -QM_EINVAL;
    }
    if (!media_is_pcm(request->audio_type != NULL ? request->audio_type : "pcm")) {
        return -QM_ENOSYS;
    }
    command = (playback_cloud_command_t *)qm_malloc(sizeof(*command));
    if (command == NULL) {
        return -QM_ENOMEM;
    }
    qm_memset(command, 0, sizeof(*command));
    command->guide_words = copy_text(request->guide_words);
    command->url = copy_text(request->url);
    command->audio_type = copy_text(request->audio_type != NULL ?
                                    request->audio_type : "pcm");
    command->title = copy_text(request->title);
    command->file_size = request->file_size;
    command->duration_s = request->duration_s;
    if (command->url == NULL || command->audio_type == NULL ||
        (request->guide_words != NULL && command->guide_words == NULL) ||
        (request->title != NULL && command->title == NULL)) {
        free_cloud_command(command);
        return -QM_ENOMEM;
    }
    QM_LOGI(LOG_TAG, "cloud play queued type=%s size=%u duration=%u guide=%u",
            command->audio_type,
            (unsigned)command->file_size,
            (unsigned)command->duration_s,
            command->guide_words != NULL ? 1U : 0U);
    if (qm_queue_send(&s_command_queue, &command, sizeof(command)) != QM_EOK) {
        free_cloud_command(command);
        return -QM_EBUSY;
    }
    return QM_EOK;
}

int playback_play_url(const char *url,
                      uint32_t file_size,
                      uint32_t duration_s)
{
    qm_audio_control_play_request_t request;

    (void)duration_s;
    if (url == NULL || url[0] == '\0') {
        return -QM_EINVAL;
    }
    if (qm_strncmp(url, "http://", 7U) != 0 &&
        qm_strncmp(url, "https://", 8U) != 0) {
        return -QM_EINVAL;
    }

    qm_memset(&request, 0, sizeof(request));
    request.source.type = QM_AUDIO_SOURCE_URI;
    request.source.value.uri.url = url;
    request.source.value.uri.expected_size = file_size;
    fill_pcm_codec(&request.codec);
    request.priority = QM_AUDIO_PRIORITY_LOW;
    request.admission = QM_AUDIO_ADMISSION_QUEUE;
    request.admission_timeout_ms = PLAYBACK_AUDIO_WAIT_MS;
    return submit_play(&request);
}

int playback_play_file(const char *path,
                       uint32_t file_size,
                       uint32_t duration_s)
{
    qm_audio_control_play_request_t request;

    (void)file_size;
    (void)duration_s;
    if (path == NULL || path[0] == '\0') {
        return -QM_EINVAL;
    }

    qm_memset(&request, 0, sizeof(request));
    request.source.type = QM_AUDIO_SOURCE_FILE;
    request.source.value.file.path = path;
    fill_pcm_codec(&request.codec);
    request.priority = QM_AUDIO_PRIORITY_LOW;
    request.admission = QM_AUDIO_ADMISSION_QUEUE;
    request.admission_timeout_ms = PLAYBACK_AUDIO_WAIT_MS;
    return submit_play(&request);
}

#if defined(CONFIG_QM_RES_MGR_SUPPORT) && CONFIG_QM_RES_MGR_SUPPORT
static int metadata_valid(const playback_metadata_t *metadata)
{
    if (metadata == NULL || metadata->did == NULL || metadata->did[0] == '\0') {
        return 0;
    }
#if defined(CONFIG_QM_RES_MGR_HTTP_SUPPORT) && CONFIG_QM_RES_MGR_HTTP_SUPPORT
    return metadata->token != NULL && metadata->token[0] != '\0';
#elif defined(CONFIG_QM_RES_MGR_MQTT_SUPPORT) && CONFIG_QM_RES_MGR_MQTT_SUPPORT
    return metadata->iot_handle != NULL;
#else
    return 0;
#endif
}

static int print_page(const qm_res_mgr_list_page_t *page, void *user)
{
    uint32_t i;
    int *received = (int *)user;

    if (page == NULL || received == NULL) {
        return -QM_EINVAL;
    }
    if (page->biz_code != 0) {
        return -QM_EIO;
    }

    *received = 1;
    for (i = 0U; i < page->count; i++) {
        const qm_res_mgr_resource_t *item = &page->items[i];
        QM_LOGI(LOG_TAG, "resource id=%u title=%s type=%s audio_type=%s",
                (unsigned int)item->id,
                item->title != NULL ? item->title : "",
                item->type != NULL ? item->type : "",
                item->audio_type != NULL ? item->audio_type : "");
    }

    /* Keep the synchronous example bounded to one page. */
    return -QM_EINTR;
}

int playback_set_resource_env(qm_res_mgr_env_t env)
{
    return qm_res_mgr_set_env(env);
}

int playback_print_first_page(const playback_metadata_t *metadata,
                              const char *bundle)
{
    qm_res_mgr_list_req_t request;
    qm_res_mgr_list_t list;
    int received = 0;
    int ret;

    if (!metadata_valid(metadata) || bundle == NULL || bundle[0] == '\0') {
        return -QM_EINVAL;
    }

    qm_memset(&request, 0, sizeof(request));
    qm_memset(&list, 0, sizeof(list));
    request.did = metadata->did;
    request.resource_bundle_name = bundle;
    request.page_num = 1U;
    request.page_size = PLAYBACK_PAGE_SIZE;
    request.on_page = print_page;
    request.page_user = &received;
#if defined(CONFIG_QM_RES_MGR_HTTP_SUPPORT) && CONFIG_QM_RES_MGR_HTTP_SUPPORT
    request.token = metadata->token;
#elif defined(CONFIG_QM_RES_MGR_MQTT_SUPPORT) && CONFIG_QM_RES_MGR_MQTT_SUPPORT
    request.iot_handle = metadata->iot_handle;
    request.timeout_ms = PLAYBACK_MQTT_TIMEOUT_MS;
#endif

    ret = qm_res_mgr_get_list(&request, &list);
    if (ret == -QM_EINTR && received && list.biz_code == 0) {
        ret = QM_EOK;
    }
    if (ret == QM_EOK && list.biz_code != 0) {
        ret = -QM_EIO;
    }
    qm_res_mgr_list_free(&list);
    return ret;
}

int playback_get_categories(const playback_metadata_t *metadata,
                            qm_res_mgr_category_list_t *out)
{
    qm_res_mgr_category_req_t request;

    if (!metadata_valid(metadata) || out == NULL) {
        return -QM_EINVAL;
    }

    qm_memset(&request, 0, sizeof(request));
    qm_memset(out, 0, sizeof(*out));
    request.did = metadata->did;
#if defined(CONFIG_QM_RES_MGR_HTTP_SUPPORT) && CONFIG_QM_RES_MGR_HTTP_SUPPORT
    request.product_id = metadata->product_id;
    request.token = metadata->token;
#elif defined(CONFIG_QM_RES_MGR_MQTT_SUPPORT) && CONFIG_QM_RES_MGR_MQTT_SUPPORT
    request.iot_handle = metadata->iot_handle;
    request.timeout_ms = PLAYBACK_MQTT_TIMEOUT_MS;
#endif
    return qm_res_mgr_get_category(&request, out);
}

int playback_get_child_categories(const playback_metadata_t *metadata,
                                  uint32_t category_id,
                                  const char *bundle,
                                  qm_res_mgr_child_category_list_t *out)
{
    qm_res_mgr_children_category_req_t request;

    if (!metadata_valid(metadata) || out == NULL) {
        return -QM_EINVAL;
    }

    qm_memset(&request, 0, sizeof(request));
    qm_memset(out, 0, sizeof(*out));
    request.did = metadata->did;
    request.category_id = category_id;
    request.resource_bundle_name = bundle;
#if defined(CONFIG_QM_RES_MGR_HTTP_SUPPORT) && CONFIG_QM_RES_MGR_HTTP_SUPPORT
    request.product_id = metadata->product_id;
    request.token = metadata->token;
#elif defined(CONFIG_QM_RES_MGR_MQTT_SUPPORT) && CONFIG_QM_RES_MGR_MQTT_SUPPORT
    request.iot_handle = metadata->iot_handle;
    request.timeout_ms = PLAYBACK_MQTT_TIMEOUT_MS;
#endif
    return qm_res_mgr_get_children_category(&request, out);
}

int playback_play_id(const playback_metadata_t *metadata, uint32_t resource_id)
{
    qm_res_mgr_detail_req_t request;
    qm_res_mgr_resource_t detail;
    int ret;

    if (s_control == NULL) {
        return -QM_EINIT;
    }
    if (!metadata_valid(metadata) || resource_id == 0U) {
        return -QM_EINVAL;
    }

    qm_memset(&request, 0, sizeof(request));
    qm_memset(&detail, 0, sizeof(detail));
    request.did = metadata->did;
    request.resource_id = resource_id;
#if defined(CONFIG_QM_RES_MGR_HTTP_SUPPORT) && CONFIG_QM_RES_MGR_HTTP_SUPPORT
    request.token = metadata->token;
#elif defined(CONFIG_QM_RES_MGR_MQTT_SUPPORT) && CONFIG_QM_RES_MGR_MQTT_SUPPORT
    request.iot_handle = metadata->iot_handle;
    request.timeout_ms = PLAYBACK_MQTT_TIMEOUT_MS;
#endif

    ret = qm_res_mgr_get_detail(&request, &detail);
    if (ret == QM_EOK) {
        if (!media_is_pcm(detail.audio_type)) {
            ret = -QM_ENOSYS;
        } else {
            ret = playback_play_url(detail.url, detail.file_size, detail.total_duration);
        }
    }
    qm_res_mgr_resource_free(&detail);
    return ret;
}

int playback_download(const qm_res_mgr_resource_t *resource,
                      qm_res_mgr_download_data_cb_t on_data,
                      void *user,
                      qm_res_mgr_download_result_t *result)
{
    qm_res_mgr_download_req_t request;

    if (resource == NULL || resource->url == NULL || resource->url[0] == '\0' ||
        on_data == NULL) {
        return -QM_EINVAL;
    }

    qm_memset(&request, 0, sizeof(request));
    request.resource = *resource;
    request.on_data = on_data;
    request.user = user;
    return qm_res_mgr_download(&request, result);
}
#endif

static int copy_live_op(qm_audio_operation_id_t *id)
{
    if (s_control == NULL || !s_lock_ok) {
        return -QM_EINIT;
    }
    qm_mutex_lock(&s_lock, QM_WAIT_FOREVER);
    if (!s_op_live) {
        qm_mutex_unlock(&s_lock);
        return -QM_ENOENT;
    }
    *id = s_op;
    qm_mutex_unlock(&s_lock);
    return QM_EOK;
}

int playback_pause(void)
{
    qm_audio_operation_id_t id;
    int ret = copy_live_op(&id);

    if (ret != QM_EOK) {
        return ret;
    }
    return qm_audio_control_pause(s_control, id, QM_AUDIO_PAUSE_HOLD_OUTPUT);
}

int playback_resume(void)
{
    qm_audio_operation_id_t id;
    int ret = copy_live_op(&id);

    if (ret != QM_EOK) {
        return ret;
    }
    return qm_audio_control_resume(s_control, id);
}

int playback_stop(void)
{
    qm_audio_operation_id_t id;
    int ret = copy_live_op(&id);

    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_audio_control_stop(s_control, id, 0);
    if (ret == QM_EOK) {
        (void)qm_audio_control_wait(s_control, id, PLAYBACK_AUDIO_WAIT_MS);
    }
    (void)release_current();
    return ret;
}

int playback_get_status(qm_audio_control_snapshot_t *status)
{
    qm_audio_operation_id_t id;
    int ret;

    if (status == NULL) {
        return -QM_EINVAL;
    }
    ret = copy_live_op(&id);
    if (ret != QM_EOK) {
        return ret;
    }
    return qm_audio_control_get_snapshot(s_control, id, status);
}

int playback_deinit(void)
{
    int ret;

    if (s_command_ready) {
        playback_cloud_command_t *stop = NULL;

        ret = qm_queue_send(&s_command_queue, &stop, sizeof(stop));
        if (ret != QM_EOK ||
            qm_sem_wait(&s_command_done, PLAYBACK_GUIDE_TIMEOUT_MS) != QM_EOK) {
            QM_LOGW(LOG_TAG, "command worker stop timeout ret=%d", ret);
            return -QM_ETIMEOUT;
        }
        (void)qm_queue_free(&s_command_queue);
        (void)qm_sem_free(&s_command_done);
        s_command_ready = 0U;
    }
    (void)playback_stop();
    destroy_control();
    return QM_EOK;
}
