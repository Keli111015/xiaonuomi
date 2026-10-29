/** @file test_audio_integration.c @brief Agent with real Audio workers and arbitration. */
#include "qm_agent_internal.h"
#include "qm_audio_priv.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define TEST_TIMEOUT_MS 3000U
#define TEST_PCM_BYTES 64U

static qm_agent_transport_event_cb_t transport_event;
static void *transport_user;
static qm_audio_device_config_t device_config;
static qm_sem_t rendered;
static qm_sem_t preempt_entered;
static qm_sem_t preempt_release;
static qm_sem_t tts_terminal;
static qm_agent_tts_event_t terminal_kind;
static unsigned interrupts;
static unsigned rendered_bytes;
static unsigned route_bytes;
static int stop_result;
static int caps_result, close_result;
static const uint8_t pcm[TEST_PCM_BYTES];

void *qm_calloc(unsigned int count, unsigned int size) { return calloc(count, size); }
void *qm_memmove(void *to, const void *from, uint32_t n) { return memmove(to, from, n); }
uint32_t qm_strlen(const char *s) { return (uint32_t)strlen(s); }
int32_t qm_strcmp(const char *a, const char *b) { return strcmp(a, b); }

char *qm_strcpy(char *dst, const char *src) { return strcpy(dst, src); }
const qm_agent_module_desc_t *qm_agent_module_manager_find_desc(const char *name)
{ (void)name; return NULL; }
int qm_agent_module_manager_load(qm_agent_handle_t agent,
                                 const qm_agent_module_desc_t *desc, const void *config)
{ (void)agent; (void)desc; (void)config; return QM_EOK; }

int qm_audio_device_open(void **ctx, const qm_audio_device_config_t *config)
{ device_config = *config; *ctx = &device_config; return QM_EOK; }
int qm_audio_device_close(void *ctx) { (void)ctx; return close_result; }
int qm_audio_device_get_caps(void *ctx, qm_audio_device_caps_t *caps)
{
    (void)ctx;
    memset(caps, 0, sizeof(*caps));
    caps->output_format = device_config.output_format;
    caps->flags = QM_AUDIO_DEVICE_CAP_PLAYBACK | QM_AUDIO_DEVICE_CAP_DRAIN |
                  QM_AUDIO_DEVICE_CAP_ABORT_WRITE | QM_AUDIO_DEVICE_CAP_POSITION;
    return caps_result;
}
int qm_audio_device_playback_start(void *ctx, qm_audio_delivery_t delivery)
{ (void)ctx; (void)delivery; route_bytes = 0U; return QM_EOK; }
int qm_audio_device_playback_write(void *ctx, const qm_audio_data_t *input,
                                 uint32_t timeout)
{
    (void)ctx; (void)timeout;
    assert(input != NULL && input->data != NULL && input->size != 0U);
    assert(input->timestamp_ms == 0U && input->duration_ms == 0U && input->sequence == 0U);
    assert((input->flags & ~QM_AUDIO_DATA_DISCONTINUITY) == 0U);
    rendered_bytes += input->size;
    route_bytes += input->size;
    qm_sem_signal(&rendered);
    return QM_EOK;
}
int qm_audio_device_playback_drain(void *ctx, uint32_t timeout)
{ (void)ctx; (void)timeout; return QM_EOK; }
int qm_audio_device_playback_pause(void *ctx) { (void)ctx; return -QM_ENOSYS; }
int qm_audio_device_playback_resume(void *ctx) { (void)ctx; return -QM_ENOSYS; }
int qm_audio_device_playback_abort(void *ctx) { (void)ctx; return QM_EOK; }
int qm_audio_device_playback_stop(void *ctx) { (void)ctx; return stop_result; }
int qm_audio_device_playback_get_position(void *ctx, uint32_t *position)
{ (void)ctx; *position = route_bytes / 32U; return QM_EOK; }
int qm_audio_device_capture_start(void *ctx)
{ (void)ctx; return -QM_ENOSYS; }
int qm_audio_device_capture_read(void *ctx, qm_audio_data_t *input, uint32_t timeout)
{
    (void)ctx; (void)timeout;
    if (input != NULL) {
        memset(input, 0, sizeof(*input));
    }
    return -QM_ENOSYS;
}
int qm_audio_device_capture_abort(void *ctx) { (void)ctx; return QM_EOK; }
int qm_audio_device_capture_stop(void *ctx) { (void)ctx; return QM_EOK; }
int qm_audio_device_set_volume(void *ctx, uint8_t volume)
{ (void)ctx; (void)volume; return QM_EOK; }
int qm_audio_device_set_mute(void *ctx, qm_bool muted)
{ (void)ctx; (void)muted; return QM_EOK; }

static int init_transport(void **ctx, const qm_agent_transport_config_t *config,
                          const qm_agent_audio_format_t *input,
                          const qm_agent_audio_format_t *output,
                          const qm_agent_video_format_t *input_video,
                          const qm_agent_video_format_t *output_video,
                          qm_agent_transport_event_cb_t callback, void *user)
{
    (void)config; (void)input; (void)output; (void)input_video; (void)output_video;
    transport_event = callback; transport_user = user; *ctx = user;
    return QM_EOK;
}
static int start_transport(void *ctx)
{
    qm_agent_event_t event = {.id = QM_AGENT_EVENT_CONNECTED};
    (void)ctx; transport_event(&event, transport_user); return QM_EOK;
}
static int stop_transport(void *ctx) { (void)ctx; return QM_EOK; }
static int send_text(void *ctx, const char *text) { (void)ctx; (void)text; return QM_EOK; }
static void send_pcm(void)
{
    qm_agent_event_t event = {.id = QM_AGENT_EVENT_AUDIO};
    event.data.audio.data = pcm;
    event.data.audio.len = sizeof(pcm);
    transport_event(&event, transport_user);
}
static int request_tts(void *ctx, const char *text)
{ (void)ctx; (void)text; send_pcm(); return QM_EOK; }
static int interrupt_transport(void *ctx) { (void)ctx; interrupts++; return QM_EOK; }
static const qm_agent_transport_if_t transport = {
    .init = init_transport, .start = start_transport, .stop = stop_transport,
    .deinit = stop_transport, .send_text = send_text, .request_tts = request_tts,
    .interrupt = interrupt_transport,
};

static void hold_preempt(const qm_audio_event_t *event, void *ctx)
{
    (void)ctx;
    if (event->type != QM_AUDIO_EVENT_PREEMPTED) return;
    qm_sem_signal(&preempt_entered);
    assert(qm_sem_wait(&preempt_release, TEST_TIMEOUT_MS) == QM_EOK);
}
static void on_tts(qm_agent_handle_t handle, uint32_t id,
                    qm_agent_tts_event_t event, int code, void *ctx)
{
    (void)handle; (void)id; (void)code; (void)ctx;
    if (event == QM_AGENT_TTS_EVENT_STARTED) return;
    terminal_kind = event;
    qm_sem_signal(&tts_terminal);
}
static void release_audio(qm_audio_control_t *control, qm_audio_operation_id_t operation)
{
    qm_utils_time_t deadline;
    int ret;
    qm_utils_time_countdown_ms(&deadline, TEST_TIMEOUT_MS);
    do {
        ret = qm_audio_control_release(control, operation, 100U);
        if (ret == QM_EOK) return;
        assert(ret == -QM_EBUSY || ret == -QM_ETIMEOUT);
        qm_msleep(1);
    } while (!qm_utils_time_is_expired(&deadline));
    assert(0 && "Audio operation did not release");
}

static void state_changed(const qm_audio_event_t *event, void *context)
{
    qm_sem_t *changed = context;

    (void)event;
    assert(qm_sem_signal(changed) == QM_EOK);
}

static void wait_audio_state(qm_audio_control_t *control, qm_audio_operation_id_t operation,
                             qm_audio_control_state_t state)
{
    qm_audio_control_snapshot_t snapshot = {0};
    qm_audio_subscription_t subscription = NULL;
    qm_utils_time_t deadline = {0};
    qm_sem_t changed = {0};
    uint32_t left = 0U;

    assert(qm_sem_new(&changed, 0U) == QM_EOK);
    assert(qm_audio_control_subscribe(control, state_changed, &changed, &subscription) == QM_EOK);
    qm_utils_time_countdown_ms(&deadline, TEST_TIMEOUT_MS);
    for (;;) {
        assert(qm_audio_control_get_snapshot(control, operation, &snapshot) == QM_EOK);
        if (snapshot.state == state) {
            break;
        }
        assert(snapshot.state != QM_AUDIO_CONTROL_ERROR);
        left = qm_utils_time_left(&deadline);
        assert(left != 0U && qm_sem_wait(&changed, left) == QM_EOK);
    }
    assert(qm_audio_control_unsubscribe(control, subscription) == QM_EOK);
    assert(qm_sem_free(&changed) == QM_EOK);
}

static void test_control_range_exhaustion(void)
{
    qm_audio_control_t *last, *rejected;
    qm_audio_t *audio = NULL;

    assert(qm_audio_acquire(&audio) == QM_EOK);
    audio->service->next_request_id_base = UINT16_MAX;
    assert(qm_audio_control_create(&last) == QM_EOK);
    assert(last->request_id_base == 0xffff0000U);
    assert(qm_audio_control_create(&rejected) == -QM_EFULL);
    assert(rejected == NULL);
    assert(qm_audio_control_shutdown(last, TEST_TIMEOUT_MS) == QM_EOK);
    assert(qm_audio_control_destroy(last) == QM_EOK);
    assert(qm_audio_control_create(&rejected) == -QM_EFULL);
    assert(rejected == NULL);
    qm_audio_release(audio);
    puts("PASS: exhausted Control ranges fail cleanly without reassigning a range");
}

static void test_queue_backpressure(void)
{
    qm_queue_t queue = {0};
    uint32_t value = 1U;
    unsigned int size = sizeof(value);

    assert(qm_queue_new(&queue, 1U, sizeof(value)) == QM_EOK);
    assert(qm_queue_send(&queue, &value, sizeof(value)) == QM_EOK);
    assert(qm_queue_send(&queue, &value, sizeof(value)) == -QM_EFULL);
    assert(qm_queue_recv(&queue, &value, &size, QM_NO_WAIT) == QM_EOK);
    assert(value == 1U);
    assert(qm_queue_free(&queue) == QM_EOK);
}

static void test_failed_engine_rollback(const qm_audio_config_t *config)
{
    qm_audio_control_t *control;

    caps_result = -QM_ENOSYS;
    close_result = -QM_EIO;
    assert(qm_audio_init(config) == -QM_ENOSYS);
    assert(qm_audio_control_create(&control) == -QM_EBUSY);
    assert(control == NULL);
    assert(qm_audio_deinit(TEST_TIMEOUT_MS) == -QM_EIO);
    close_result = QM_EOK;
    assert(qm_audio_deinit(TEST_TIMEOUT_MS) == QM_EOK);
    assert(qm_audio_init(config) == -QM_ENOSYS);
    assert(qm_audio_deinit(TEST_TIMEOUT_MS) == QM_EOK);
    caps_result = QM_EOK;
    assert(qm_audio_init(config) == QM_EOK);
    assert(qm_audio_control_create(&control) == QM_EOK);
    assert(qm_audio_control_shutdown(control, TEST_TIMEOUT_MS) == QM_EOK);
    assert(qm_audio_control_destroy(control) == QM_EOK);
    assert(qm_audio_deinit(TEST_TIMEOUT_MS) == QM_EOK);
    puts("PASS: failed Engine creation retains backend cleanup for retry");
}

static void test_operation_id_wrap(qm_audio_control_t *first,
                                    qm_audio_control_t *second)
{
    qm_audio_control_play_request_t request = {
        .admission = QM_AUDIO_ADMISSION_QUEUE,
        .admission_timeout_ms = TEST_TIMEOUT_MS,
        .source = {.type = QM_AUDIO_SOURCE_PUSH},
    };
    qm_audio_operation_id_t first_id, second_id;

    /* Reach the long-running counter boundary without replaying 65535 times. */
    first->next_operation_id = first->request_id_base + UINT16_MAX;
    assert(qm_audio_control_play(first, &request, &first_id) == QM_EOK);
    assert(first_id == first->request_id_base + UINT16_MAX);
    assert(qm_audio_control_stop(first, first_id, 0U) == QM_EOK);
    release_audio(first, first_id);
    assert(qm_audio_control_play(second, &request, &second_id) == QM_EOK);
    assert(qm_audio_control_play(first, &request, &first_id) == QM_EOK);
    assert(first_id == first->request_id_base + 1U && first_id != second_id);
    assert(qm_audio_control_stop(first, first_id, 0U) == QM_EOK);
    release_audio(first, first_id);
    assert(qm_audio_control_stop(second, second_id, 0U) == QM_EOK);
    release_audio(second, second_id);
    puts("PASS: operation ID wraps within its Control while another Control is active");
}

static void test_resource_suspend(qm_audio_control_t *resource,
                                   qm_audio_control_t *prompt)
{
    qm_audio_control_play_request_t request = {
        .priority = QM_AUDIO_PRIORITY_LOW,
        .admission = QM_AUDIO_ADMISSION_PREEMPT_LOWER,
        .admission_timeout_ms = TEST_TIMEOUT_MS,
        .source = {.type = QM_AUDIO_SOURCE_PUSH},
    };
    qm_audio_operation_id_t resource_id = QM_AUDIO_INVALID_REQUEST_ID;
    qm_audio_operation_id_t prompt_id = QM_AUDIO_INVALID_REQUEST_ID;
    qm_audio_control_snapshot_t status = {0};
    unsigned before = 0U;

    assert(qm_audio_control_play(resource, &request, &resource_id) == QM_EOK);
    assert(qm_audio_control_stream_write(resource, resource_id, pcm, sizeof(pcm), 0) == QM_EOK);
    assert(qm_sem_wait(&rendered, TEST_TIMEOUT_MS) == QM_EOK);
    /* Control reports execution failure asynchronously; release retries cleanup. */
    stop_result = -QM_EIO;
    assert(qm_audio_control_pause(resource, resource_id,
                                  QM_AUDIO_PAUSE_YIELD_OUTPUT) == QM_EOK);
    assert(qm_audio_control_wait(resource, resource_id, TEST_TIMEOUT_MS) == -QM_EIO);
    stop_result = QM_EOK;
    release_audio(resource, resource_id);
    before = rendered_bytes;
    assert(qm_audio_control_play(resource, &request, &resource_id) == QM_EOK);
    assert(qm_audio_control_stream_write(resource, resource_id, pcm, sizeof(pcm), 0U) == QM_EOK);
    assert(qm_sem_wait(&rendered, TEST_TIMEOUT_MS) == QM_EOK);
    assert(qm_audio_control_pause(resource, resource_id,
                                  QM_AUDIO_PAUSE_YIELD_OUTPUT) == QM_EOK);
    wait_audio_state(resource, resource_id, QM_AUDIO_CONTROL_PAUSED);
    assert(qm_audio_control_pause(resource, resource_id,
                                  QM_AUDIO_PAUSE_YIELD_OUTPUT) == QM_EOK);
    wait_audio_state(resource, resource_id, QM_AUDIO_CONTROL_PAUSED);
    assert(qm_audio_control_get_snapshot(resource, resource_id, &status) == QM_EOK);
    assert(status.state == QM_AUDIO_CONTROL_PAUSED);
    request.priority = QM_AUDIO_PRIORITY_HIGH;
    assert(qm_audio_control_play(prompt, &request, &prompt_id) == QM_EOK);
    assert(qm_audio_control_resume(resource, resource_id) == -QM_EBUSY);
    assert(qm_audio_control_stream_write(prompt, prompt_id, pcm, sizeof(pcm), 0) == QM_EOK);
    assert(qm_sem_wait(&rendered, TEST_TIMEOUT_MS) == QM_EOK);
    assert(qm_audio_control_stream_finish(prompt, prompt_id) == QM_EOK);
    assert(qm_audio_control_wait(prompt, prompt_id, TEST_TIMEOUT_MS) == QM_EOK);
    release_audio(prompt, prompt_id);
    assert(qm_audio_control_resume(resource, resource_id) == QM_EOK);
    wait_audio_state(resource, resource_id, QM_AUDIO_CONTROL_PLAYING);
    assert(qm_audio_control_stream_write(resource, resource_id, pcm, sizeof(pcm), 0) == QM_EOK);
    assert(qm_sem_wait(&rendered, TEST_TIMEOUT_MS) == QM_EOK);
    assert(qm_audio_control_stream_finish(resource, resource_id) == QM_EOK);
    assert(qm_audio_control_wait(resource, resource_id, TEST_TIMEOUT_MS) == QM_EOK);
    assert(qm_audio_control_get_snapshot(resource, resource_id, &status) == QM_EOK);
    assert(status.operation_id == resource_id && status.state == QM_AUDIO_CONTROL_FINISHED);
    assert(rendered_bytes - before == sizeof(pcm) * 3U);
    assert(status.position_ms == sizeof(pcm) * 2U / 32U);
    release_audio(resource, resource_id);
    puts("PASS: retained resource, prompt handoff, busy resume, stop failure retry and no replay");
}

typedef struct {
    qm_audio_control_t *control;
    qm_audio_control_play_request_t *request;
    qm_audio_operation_id_t *operation;
    int result;
} async_play_t;

static void *play_during_callback(void *ctx)
{
    async_play_t *play = ctx;
    play->result = qm_audio_control_play(play->control, play->request, play->operation);
    return NULL;
}

int main(void)
{
    qm_audio_control_t *voice, *alarm;
    qm_audio_subscription_t observer;
    qm_audio_operation_id_t alarm_id;
    qm_audio_control_snapshot_t snapshot;
    qm_agent_handle_t agent;
    qm_agent_config_t config = {0};
    qm_agent_tts_request_t tts = {.text = "test", .timeout_ms = TEST_TIMEOUT_MS,
                                 .callback = on_tts};
    qm_audio_config_t audio_config = {
        .output_format = {.pcm = {.sample_rate_hz = 16000U, .channels = 1U,
                                  .sample = QM_AUDIO_PCM_S16},
                          .codec = QM_AUDIO_CODEC_PCM},
    };
    qm_audio_control_play_request_t alarm_request = {
        .priority = QM_AUDIO_PRIORITY_CRITICAL,
        .admission = QM_AUDIO_ADMISSION_PREEMPT_LOWER,
        .admission_timeout_ms = TEST_TIMEOUT_MS,
        .source = {.type = QM_AUDIO_SOURCE_PUSH},
    };
    uint32_t request_id;
    pthread_t preempt_thread;
    async_play_t preempt = {0};

    qm_sem_new(&rendered, 0); qm_sem_new(&preempt_entered, 0);
    qm_sem_new(&preempt_release, 0); qm_sem_new(&tts_terminal, 0);
    test_queue_backpressure();
    test_failed_engine_rollback(&audio_config);
    assert(qm_audio_control_create(&voice) == -QM_EINIT);
    assert(voice == NULL);
    assert(qm_audio_init(&audio_config) == QM_EOK);
    assert(qm_audio_control_create(&voice) == QM_EOK);
    assert(qm_audio_control_create(&alarm) == QM_EOK);
    assert(qm_audio_deinit(TEST_TIMEOUT_MS) == -QM_EBUSY);
    test_operation_id_wrap(voice, alarm);
    test_resource_suspend(voice, alarm);
    test_control_range_exhaustion();
    config.audio.control = voice;
    config.transport.type = QM_AGENT_TRANSPORT_CUSTOM; config.transport_if = &transport;
    agent = qm_agent_create(&config);
    assert(agent != NULL && qm_agent_start(agent) == QM_EOK);
    assert(qm_audio_control_subscribe(voice, hold_preempt, NULL, &observer) == QM_EOK);
    assert(qm_agent_tts_request(agent, &tts, &request_id) == QM_EOK);
    assert(qm_sem_wait(&rendered, TEST_TIMEOUT_MS) == QM_EOK);
    preempt = (async_play_t){alarm, &alarm_request, &alarm_id, -QM_EIO};
    /* Both businesses now share the event task. A deliberately blocked test
     * callback also holds the preempting command until the test releases it. */
    assert(pthread_create(&preempt_thread, NULL, play_during_callback, &preempt) == 0);
    assert(qm_sem_wait(&preempt_entered, TEST_TIMEOUT_MS) == QM_EOK);
    /* The Agent owns a copied terminal fact. Another observer does not borrow
     * operation storage, so cleanup may finish while that observer is blocked. */
    assert(qm_agent_send_text(agent, "cleanup barrier") == QM_EOK);
    qm_mutex_lock(&agent->audio.lock, QM_WAIT_FOREVER);
    assert(!agent->audio.stream_accepting && !agent->audio.playback_started);
    assert((agent->audio.playback_operation != QM_AUDIO_INVALID_REQUEST_ID) ==
           agent->audio.cleanup_pending);
    qm_mutex_unlock(&agent->audio.lock);
    qm_sem_signal(&preempt_release);
    assert(pthread_join(preempt_thread, NULL) == 0 && preempt.result == QM_EOK);
    assert(qm_sem_wait(&tts_terminal, TEST_TIMEOUT_MS) == QM_EOK);
    assert(terminal_kind == QM_AGENT_TTS_EVENT_FAILED && interrupts == 1U);
    send_pcm();
    assert(qm_agent_send_text(agent, "barrier") == QM_EOK);
    assert(!qm_agent_audio_engine_stream_active(agent));
    assert(qm_audio_control_get_snapshot(alarm, alarm_id, &snapshot) == QM_EOK);
    assert(snapshot.state == QM_AUDIO_CONTROL_PLAYING || snapshot.state == QM_AUDIO_CONTROL_WAITING);

    /* Explicitly start a new reply while the higher-priority alarm owns the
     * Service. The default Agent policy must leave the alarm active. */
    assert(qm_agent_tts_request(agent, &tts, &request_id) == QM_EOK);
    assert(qm_audio_control_get_snapshot(alarm, alarm_id, &snapshot) == QM_EOK);
    assert(snapshot.state == QM_AUDIO_CONTROL_PLAYING || snapshot.state == QM_AUDIO_CONTROL_WAITING);
    assert(qm_agent_tts_cancel(agent, request_id) == QM_EOK);
    assert(qm_audio_control_unsubscribe(voice, observer) == QM_EOK);
    assert(qm_agent_stop(agent) == QM_EOK && qm_agent_destroy(agent) == QM_EOK);
    assert(qm_audio_control_get_snapshot(alarm, alarm_id, &snapshot) == QM_EOK);
    assert(snapshot.state != QM_AUDIO_CONTROL_PREEMPTED);
    assert(qm_audio_control_stop(alarm, alarm_id, 0U) == QM_EOK);
    release_audio(alarm, alarm_id);
    assert(qm_audio_control_shutdown(voice, TEST_TIMEOUT_MS) == QM_EOK);
    assert(qm_audio_control_destroy(voice) == QM_EOK);
    assert(qm_audio_deinit(TEST_TIMEOUT_MS) == -QM_EBUSY);
    /* Removing one business cannot shut down the surviving business's route. */
    assert(qm_audio_control_play(alarm, &alarm_request, &alarm_id) == QM_EOK);
    assert(qm_audio_control_stream_write(alarm, alarm_id, pcm, sizeof(pcm),
                                          TEST_TIMEOUT_MS) == QM_EOK);
    assert(qm_audio_control_stream_finish(alarm, alarm_id) == QM_EOK);
    assert(qm_audio_control_wait(alarm, alarm_id, TEST_TIMEOUT_MS) == QM_EOK);
    release_audio(alarm, alarm_id);
    assert(qm_audio_control_shutdown(alarm, TEST_TIMEOUT_MS) == QM_EOK);
    assert(qm_audio_control_destroy(alarm) == QM_EOK);
    assert(qm_audio_deinit(TEST_TIMEOUT_MS) == QM_EOK);
    assert(qm_audio_control_create(&voice) == -QM_EINIT);
    assert(voice == NULL);
    qm_event_default_delete();
    qm_sem_free(&rendered); qm_sem_free(&preempt_entered);
    qm_sem_free(&preempt_release); qm_sem_free(&tts_terminal);
    puts("PASS: real multi-Control arbitration, preemption cleanup and Agent ownership");
    return 0;
}
