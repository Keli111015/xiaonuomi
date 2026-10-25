#include "morphcube_study_runtime.h"

#include "mc_study.h"
#include "qm.h"
#include "qm_errno.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define LOG_TAG "morphcube_study"
#define MORPHCUBE_QUEUE_LENGTH 12
#define MORPHCUBE_TASK_STACK 4096
#define MORPHCUBE_TASK_PRIORITY 5

typedef enum {
    MORPHCUBE_MESSAGE_POMODORO,
    MORPHCUBE_MESSAGE_DISTRACTION,
    MORPHCUBE_MESSAGE_POSE,
} morphcube_message_type_t;

typedef struct {
    morphcube_message_type_t type;
    qm_pomodoro_event_data_t pomodoro;
    morphcube_pose_t pose;
} morphcube_message_t;

static qm_pomodoro_t s_timer;
static QueueHandle_t s_queue;
static SemaphoreHandle_t s_status_lock;
static morphcube_study_status_t s_public_status;

static uint64_t monotonic_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static bool convert_pose(morphcube_pose_t input, mc_pose *output)
{
    if (output == NULL) return false;
    switch (input) {
    case MORPHCUBE_POSE_UNKNOWN:      *output = MC_UNKNOWN; return true;
    case MORPHCUBE_POSE_NORMAL:       *output = MC_NORMAL; return true;
    case MORPHCUBE_POSE_TILTED:       *output = MC_TILTED; return true;
    case MORPHCUBE_POSE_HEAD_LOWERED: *output = MC_HEAD_LOWERED; return true;
    default: return false;
    }
}

static void publish_status(const mc_study *study,
                           const qm_pomodoro_status_t *pomodoro)
{
    if (xSemaphoreTake(s_status_lock, portMAX_DELAY) != pdTRUE) return;
    s_public_status.hp = study->hp;
    s_public_status.active = study->active;
    s_public_status.exhausted = study->exhausted;
    s_public_status.penalties = study->penalties;
    s_public_status.posture_reminders = study->reminders;
    if (pomodoro != NULL) s_public_status.pomodoro = *pomodoro;
    xSemaphoreGive(s_status_lock);
}

static void handle_pomodoro(mc_study *study,
                            qm_pomodoro_status_t *pomodoro,
                            const qm_pomodoro_event_data_t *event)
{
    *pomodoro = event->status;

    switch (event->event) {
    case QM_POMODORO_EVENT_STATE_CHANGED:
        if (event->status.state == QM_POMODORO_STATE_FOCUS &&
            (event->previous_state == QM_POMODORO_STATE_IDLE ||
             event->previous_state == QM_POMODORO_STATE_FINISHED)) {
            mc_study_reset(study);
        }
        mc_study_set_active(study,
                            event->status.state == QM_POMODORO_STATE_FOCUS &&
                            !event->status.paused);
        break;
    case QM_POMODORO_EVENT_PAUSED:
        mc_study_set_active(study, false);
        break;
    case QM_POMODORO_EVENT_RESUMED:
        mc_study_set_active(study,
                            event->status.state == QM_POMODORO_STATE_FOCUS);
        break;
    case QM_POMODORO_EVENT_STOPPED:
    case QM_POMODORO_EVENT_FINISHED:
        mc_study_set_active(study, false);
        break;
    case QM_POMODORO_EVENT_TICK:
    case QM_POMODORO_EVENT_PHASE_DONE:
    default:
        break;
    }
}

static void morphcube_study_task(void *arg)
{
    mc_study study;
    qm_pomodoro_status_t pomodoro = {0};
    morphcube_message_t message;

    (void)arg;
    mc_study_reset(&study);
    (void)qm_pomodoro_get_status(s_timer, &pomodoro);
    publish_status(&study, &pomodoro);

    for (;;) {
        if (xQueueReceive(s_queue, &message, portMAX_DELAY) != pdTRUE) continue;

        if (message.type == MORPHCUBE_MESSAGE_POMODORO) {
            handle_pomodoro(&study, &pomodoro, &message.pomodoro);
        } else if (message.type == MORPHCUBE_MESSAGE_DISTRACTION) {
            bool changed = mc_study_distraction(&study, true, monotonic_ms());
            if (changed) {
                QM_LOGI(LOG_TAG, "distraction accepted hp=%u penalties=%u",
                        (unsigned)study.hp, (unsigned)study.penalties);
                if (study.exhausted) {
                    int ret = qm_pomodoro_pause(s_timer);
                    if (ret != QM_EOK) {
                        QM_LOGW(LOG_TAG, "pause at zero hp failed ret=%d", ret);
                    }
                }
            } else {
                QM_LOGI(LOG_TAG, "distraction ignored active=%d hp=%u",
                        study.active, (unsigned)study.hp);
            }
        } else if (message.type == MORPHCUBE_MESSAGE_POSE) {
            mc_pose pose;
            bool remind = convert_pose(message.pose, &pose) &&
                          mc_study_pose(&study, pose, monotonic_ms());
            if (remind) {
                QM_LOGI(LOG_TAG, "posture reminder count=%u pose=%d",
                        (unsigned)study.reminders, (int)message.pose);
            }
        }

        publish_status(&study, &pomodoro);
    }
}

int morphcube_study_runtime_init(qm_pomodoro_t timer)
{
    BaseType_t task_result;

    if (timer == NULL) return -QM_EINVAL;
    if (s_queue != NULL) return -QM_EBUSY;

    s_status_lock = xSemaphoreCreateMutex();
    if (s_status_lock == NULL) return -QM_ENOMEM;

    s_queue = xQueueCreate(MORPHCUBE_QUEUE_LENGTH, sizeof(morphcube_message_t));
    if (s_queue == NULL) {
        vSemaphoreDelete(s_status_lock);
        s_status_lock = NULL;
        return -QM_ENOMEM;
    }

    s_timer = timer;
    task_result = xTaskCreate(morphcube_study_task, "morphcube_study",
                              MORPHCUBE_TASK_STACK, NULL,
                              MORPHCUBE_TASK_PRIORITY, NULL);
    if (task_result != pdPASS) {
        vQueueDelete(s_queue);
        vSemaphoreDelete(s_status_lock);
        s_queue = NULL;
        s_status_lock = NULL;
        s_timer = NULL;
        return -QM_ENOMEM;
    }

    QM_LOGI(LOG_TAG, "runtime ready hp=100");
    return QM_EOK;
}

int morphcube_study_runtime_on_pomodoro(const qm_pomodoro_event_data_t *event)
{
    morphcube_message_t message = {0};

    if (event == NULL) return -QM_EINVAL;
    if (s_queue == NULL) return -QM_EINIT;
    message.type = MORPHCUBE_MESSAGE_POMODORO;
    message.pomodoro = *event;
    return xQueueSend(s_queue, &message, 0) == pdTRUE ? QM_EOK : -QM_EFULL;
}

int morphcube_study_runtime_confirm_distraction(void)
{
    morphcube_message_t message = {0};

    if (s_queue == NULL) return -QM_EINIT;
    message.type = MORPHCUBE_MESSAGE_DISTRACTION;
    return xQueueSend(s_queue, &message, 0) == pdTRUE ? QM_EOK : -QM_EFULL;
}

int morphcube_study_runtime_submit_pose(morphcube_pose_t pose)
{
    morphcube_message_t message = {0};
    mc_pose unused;

    if (!convert_pose(pose, &unused)) return -QM_EINVAL;
    if (s_queue == NULL) return -QM_EINIT;
    message.type = MORPHCUBE_MESSAGE_POSE;
    message.pose = pose;
    return xQueueSend(s_queue, &message, 0) == pdTRUE ? QM_EOK : -QM_EFULL;
}

int morphcube_study_runtime_get_status(morphcube_study_status_t *out)
{
    if (out == NULL) return -QM_EINVAL;
    if (s_status_lock == NULL) return -QM_EINIT;
    if (xSemaphoreTake(s_status_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return -QM_ETIMEOUT;
    }
    *out = s_public_status;
    xSemaphoreGive(s_status_lock);
    return QM_EOK;
}
