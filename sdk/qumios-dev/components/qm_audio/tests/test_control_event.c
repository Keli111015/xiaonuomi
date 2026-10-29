/*
 * Focused Control/Event regression checks.
 *
 * This source intentionally includes the Agent integration fixture and the
 * Event implementation so the private durable FIFO can be exercised without
 * widening the production API. The parent CMake target supplies the same
 * real qm_audio/qm_agent sources as test_agent_audio_integration, but omits
 * test_audio_integration.c and qm_audio_event.c (both are included here).
 */
#define main qm_audio_existing_integration_main
#include "../../qm_agent/tests/test_audio_integration.c"
#undef main

#include "../src/event/qm_audio_event.c"
#include <sched.h>

static qm_sem_t callback_entered;
static qm_sem_t callback_release;
static qm_sem_t unsubscribe_done;

typedef struct {
    qm_audio_control_t *control;
    qm_audio_t *audio;
    qm_audio_subscription_t subscription;
    qm_bool wake;
    int result;
} subscription_call_t;

static void blocked_callback(const qm_audio_event_t *event, void *context)
{
    (void)event;
    (void)context;
    assert(qm_sem_signal(&callback_entered) == QM_EOK);
    assert(qm_sem_wait(&callback_release, TEST_TIMEOUT_MS) == QM_EOK);
}

static void *dispatch_callback_thread(void *context)
{
    subscription_call_t *call = context;
    qm_audio_event_t event = {0};

    if (call->wake) {
        dispatch_wake_event(call->audio, &event);
    } else {
        qm_audio_event_dispatch(call->control, &event);
    }
    return NULL;
}

static void *unsubscribe_thread(void *context)
{
    subscription_call_t *call = context;

    call->result = call->wake ? qm_audio_wake_unsubscribe(call->subscription) :
        qm_audio_control_unsubscribe(call->control, call->subscription);
    assert(qm_sem_signal(&unsubscribe_done) == QM_EOK);
    return NULL;
}

static void test_unsubscribe_waits_for_callback(qm_bool wake)
{
    qm_audio_t audio = {0};
    qm_audio_control_t control = {0};
    qm_audio_subscription_t subscription = calloc(1U, sizeof(*subscription));
    qm_audio_subscription_t slots[CONFIG_QM_AUDIO_MAX_SUBSCRIBERS] = {0};
    subscription_call_t call = {0};
    pthread_t dispatch_thread;
    pthread_t remove_thread;
    qm_utils_time_t deadline = {0};
    qm_bool active = QM_TRUE;

    assert(subscription != NULL);
    assert(qm_mutex_new(&audio.lock) == QM_EOK);
    assert(qm_mutex_new(&control.lock) == QM_EOK);
    assert(qm_sem_new(&subscription->idle_sem, 1U) == QM_EOK);
    assert(qm_sem_new(&callback_entered, 0U) == QM_EOK);
    assert(qm_sem_new(&callback_release, 0U) == QM_EOK);
    assert(qm_sem_new(&unsubscribe_done, 0U) == QM_EOK);
    subscription->callback = blocked_callback;
    subscription->active = QM_TRUE;
    if (wake) {
        subscription->audio = &audio;
        audio.users = 1U;
        audio.wake_subscriptions[0] = subscription;
        audio.wake_subscription_count = 1U;
    } else {
        subscription->control = &control;
        control.subscriptions = slots;
        control.subscriptions[0] = subscription;
        control.subscription_count = 1U;
    }
    call = (subscription_call_t){&control, &audio, subscription, wake, -QM_EIO};
    assert(pthread_create(&dispatch_thread, NULL, dispatch_callback_thread, &call) == 0);
    assert(qm_sem_wait(&callback_entered, TEST_TIMEOUT_MS) == QM_EOK);
    assert(pthread_create(&remove_thread, NULL, unsubscribe_thread, &call) == 0);
    qm_utils_time_countdown_ms(&deadline, TEST_TIMEOUT_MS);
    while (active) {
        (void)qm_mutex_lock(wake ? &audio.lock : &control.lock, QM_WAIT_FOREVER);
        active = subscription->active;
        (void)qm_mutex_unlock(wake ? &audio.lock : &control.lock);
        assert(!qm_utils_time_is_expired(&deadline));
        sched_yield();
    }
    /* A stale idle token must not release storage borrowed by the callback. */
    assert(qm_sem_wait(&unsubscribe_done, 20U) == -QM_ETIMEOUT);
    assert(qm_sem_signal(&callback_release) == QM_EOK);
    assert(pthread_join(dispatch_thread, NULL) == 0);
    assert(pthread_join(remove_thread, NULL) == 0);
    assert(call.result == QM_EOK);
    assert(qm_sem_wait(&unsubscribe_done, QM_NO_WAIT) == QM_EOK);
    assert(qm_sem_free(&callback_entered) == QM_EOK);
    assert(qm_sem_free(&callback_release) == QM_EOK);
    assert(qm_sem_free(&unsubscribe_done) == QM_EOK);
    assert(qm_mutex_free(&audio.lock) == QM_EOK);
    assert(qm_mutex_free(&control.lock) == QM_EOK);
}

static unsigned int control_event_callback_count;

static void control_event_callback(const qm_audio_event_t *event, void *context)
{
    (void)event;
    (void)context;
    control_event_callback_count++;
}

static void test_unsubscribe_does_not_consume_a_stale_idle_signal(void)
{
    qm_audio_control_t control = {0};
    qm_audio_subscription_t *subscriptions = NULL;
    qm_audio_subscription_t subscription = NULL;
    qm_audio_event_t event = {0};
    int result = QM_EOK;

    assert(qm_mutex_new(&control.lock) == QM_EOK);
    subscriptions = calloc(CONFIG_QM_AUDIO_MAX_SUBSCRIBERS,
                            sizeof(*subscriptions));
    assert(subscriptions != NULL);
    control.subscriptions = subscriptions;
    control.event_dispatch_active = QM_TRUE;
    assert(qm_audio_control_subscribe(&control, control_event_callback, NULL,
                                      &subscription) == QM_EOK);

    control_event_callback_count = 0U;
    qm_audio_event_dispatch(&control, &event);
    assert(control_event_callback_count == 1U);
    /* An active subscription must not accumulate a wake token. */
    assert(qm_sem_wait(&subscription->idle_sem, QM_NO_WAIT) != QM_EOK);

    result = qm_audio_control_unsubscribe(&control, subscription);
    assert(result == QM_EOK);
    free(subscriptions);
    assert(qm_mutex_free(&control.lock) == QM_EOK);
}

static void test_public_event_is_durable_when_wake_queue_is_full(void)
{
    qm_audio_t audio = {0};
    qm_audio_control_t control = {0};
    qm_audio_subscription_t *subscriptions = NULL;
    qm_audio_subscription_t subscription = NULL;
    qm_audio_event_msg_t queued = {0};
    qm_audio_event_msg_t dispatch = {0};
    qm_audio_event_t event = {0};
    unsigned int bytes = sizeof(queued);

    assert(qm_mutex_new(&audio.lock) == QM_EOK);
    assert(qm_mutex_new(&control.lock) == QM_EOK);
    subscriptions = calloc(CONFIG_QM_AUDIO_MAX_SUBSCRIBERS,
                            sizeof(*subscriptions));
    assert(subscriptions != NULL);
    assert(qm_queue_new(&audio.event_queue, 1U, sizeof(queued)) == QM_EOK);
    assert(qm_sem_new(&control.event_pending_space_sem, 0U) == QM_EOK);
    control.audio = &audio;
    control.subscriptions = subscriptions;
    control.event_dispatch_active = QM_TRUE;
    assert(qm_audio_control_subscribe(&control, control_event_callback, NULL,
                                      &subscription) == QM_EOK);
    control_event_callback_count = 0U;

    queued.type = QM_AUDIO_EVENT_MSG_WAKE;
    assert(qm_audio_event_post(&audio, &queued) == QM_EOK);
    dispatch.type = QM_AUDIO_EVENT_MSG_DISPATCH;
    dispatch.control = &control;
    dispatch.u.event = event;
    /* The public event is accepted into the durable FIFO even though the
     * coalescing wake cannot be queued while the instance queue is full. */
    assert(qm_audio_event_post(&audio, &dispatch) == QM_EOK);
    assert(control.event_pending_count == 1U);

    assert(qm_queue_recv(&audio.event_queue, &queued, &bytes, QM_NO_WAIT) == QM_EOK);
    assert(queued.type == QM_AUDIO_EVENT_MSG_WAKE);
    assert(dispatch_pending(&control, QM_FALSE));
    assert(control_event_callback_count == 1U);

    assert(qm_audio_control_unsubscribe(&control, subscription) == QM_EOK);
    assert(qm_sem_free(&control.event_pending_space_sem) == QM_EOK);
    assert(qm_queue_free(&audio.event_queue) == QM_EOK);
    assert(qm_mutex_free(&control.lock) == QM_EOK);
    assert(qm_mutex_free(&audio.lock) == QM_EOK);
    free(subscriptions);
}

static void test_shutdown_accepts_terminal_timeout_result(void)
{
    qm_audio_config_t config = {
        .output_format = {.codec = QM_AUDIO_CODEC_PCM,
            .pcm = {.sample_rate_hz = 16000U, .channels = 1U,
                    .sample = QM_AUDIO_PCM_S16}},
    };
    qm_audio_control_t *control = NULL;
    qm_audio_control_operation_t *operation = calloc(1U, sizeof(*operation));

    assert(operation != NULL);
    assert(qm_audio_init(&config) == QM_EOK);
    assert(qm_audio_control_create(&control) == QM_EOK);
    operation->control = control;
    operation->id = control->request_id_base + 1U;
    operation->state = QM_AUDIO_CONTROL_ERROR;
    operation->result = -QM_ETIMEOUT;
    assert(qm_sem_new(&operation->state_sem, 0U) == QM_EOK);
    control->operations[0] = operation;
    control->operation_count = 1U;
    assert(qm_audio_control_wait(control, operation->id, QM_NO_WAIT) == -QM_ETIMEOUT);
    assert(qm_audio_control_shutdown(control, TEST_TIMEOUT_MS) == QM_EOK);
    assert(!control->event_attached);
    assert(qm_audio_control_release(control, operation->id, TEST_TIMEOUT_MS) == QM_EOK);
    assert(qm_audio_control_destroy(control) == QM_EOK);
    assert(qm_audio_deinit(TEST_TIMEOUT_MS) == QM_EOK);
}

int main(void)
{
    test_unsubscribe_does_not_consume_a_stale_idle_signal();
    test_unsubscribe_waits_for_callback(QM_FALSE);
    test_unsubscribe_waits_for_callback(QM_TRUE);
    test_public_event_is_durable_when_wake_queue_is_full();
    test_shutdown_accepts_terminal_timeout_result();
    puts("PASS: callback quiescence, durable public events and terminal timeout shutdown");
    return 0;
}
