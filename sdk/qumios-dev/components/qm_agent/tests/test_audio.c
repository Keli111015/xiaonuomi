/** @file test_audio.c @brief Tests for the qm_audio PUSH bridge. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/audio/qm_agent_audio_engine.c"

static qm_audio_event_cb_t audio_event_callback;
static void *audio_event_context;
static qm_audio_capture_config_t capture_config;
static qm_audio_subscription_t fake_subscription = (qm_audio_subscription_t)0x1;
static qm_audio_capture_subscription_t *fake_capture_subscription =
    (qm_audio_capture_subscription_t *)0x2;
static qm_audio_capture_t *fake_capture = (qm_audio_capture_t *)0x3;
static qm_audio_pcm_format_t fake_output_format;
static qm_audio_pcm_format_t fake_input_format;
static qm_audio_stream_format_t fake_output_stream_format;
static qm_audio_stream_format_t fake_input_stream_format;
static qm_audio_event_t posted_audio_event;
static int posted_audio_error;
static uint32_t posted_audio_error_generation;
static unsigned posted_audio_events;
static unsigned posted_audio_errors;
static unsigned emitted_started;
static unsigned emitted_completed;
static unsigned emitted_failed;
static int emitted_failure_code;
static uint32_t emitted_failure_generation;
static unsigned capture_enable_calls;
static unsigned capture_upload_calls;
static unsigned stop_calls;
static unsigned wait_calls;
static unsigned release_calls;
static qm_agent_audio_playback_config_t submitted_policy;
static unsigned scheduled_audio;
static unsigned requested_begins;
static unsigned remote_interrupts;
static int play_result;
static int write_result;
static int finish_result;
static int stop_result;
static int wait_result = -QM_EINTR;
static int release_result;
static int capture_unsubscribe_result;
static qm_audio_operation_id_t submitted_operation = 41U;
static qm_audio_operation_id_t submitted_write_operation;
static uint8_t submitted_bytes[128];
static uint32_t submitted_bytes_len;
static qm_audio_data_t submitted_packet;
static unsigned submitted_packet_writes;
static qm_audio_codec_config_t submitted_codec;
static qm_bool emit_started_from_play;
static qm_bool capture_last_enabled;

void *qm_malloc(unsigned int size) { return malloc(size); }
void *qm_calloc(unsigned int count, unsigned int size) { return calloc(count, size); }
void qm_free(void *data) { free(data); }
void *qm_memcpy(void *to, const void *from, uint32_t size)
{
    return memcpy(to, from, size);
}
void *qm_memset(void *data, int32_t value, uint32_t size)
{
    return memset(data, value, size);
}
uint32_t qm_agent_get_log_flags(void) { return 0U; }

void qm_agent_emit(qm_agent_handle_t handle, const qm_agent_event_t *event)
{
    (void)handle;
    if (event == NULL) return;
    if (event->id == QM_AGENT_EVENT_TTS_PLAYBACK_STARTED) emitted_started++;
    if (event->id == QM_AGENT_EVENT_TTS_PLAYBACK_COMPLETE) emitted_completed++;
    if (event->id == QM_AGENT_EVENT_TTS_PLAYBACK_FAILED) {
        emitted_failed++;
        emitted_failure_code = event->code;
        emitted_failure_generation = event->data.tts_playback.generation;
    }
}

int qm_agent_control_post_audio_event(qm_agent_handle_t handle,
                                      const qm_audio_event_t *event)
{
    (void)handle;
    assert(event != NULL);
    posted_audio_event = *event;
    posted_audio_events++;
    return QM_EOK;
}

int qm_agent_control_post_audio_begin(qm_agent_handle_t handle,
                                      const qm_agent_audio_format_t *format, uint32_t generation)
{
    (void)handle;
    (void)generation;
    (void)format;
    requested_begins++;
    return QM_EOK;
}

int qm_agent_control_post_audio_error(qm_agent_handle_t handle,
                                      int code,
                                      uint32_t generation)
{
    (void)handle;
    posted_audio_error = code;
    posted_audio_error_generation = generation;
    posted_audio_errors++;
    return QM_EOK;
}

int qm_agent_control_schedule(qm_agent_handle_t handle,
                              qm_agent_control_action_t type, uint32_t delay_ms,
                              uint32_t request_id, uint32_t generation)
{
    (void)handle; (void)type; (void)delay_ms; (void)request_id; (void)generation;
    scheduled_audio++;
    return QM_EOK;
}

int qm_agent_transport_send_audio(qm_agent_handle_t handle,
                                  const void *data,
                                  uint32_t len)
{
    assert(handle != NULL && data != NULL);
    if (len == 2U) assert(memcmp(data, "ab", 2U) == 0);
    else assert(len == 80U);
    capture_upload_calls++;
    return QM_EOK;
}

int qm_audio_get_output_stream_format(qm_audio_stream_format_t *format)
{
    assert(format != NULL);
    *format = fake_output_stream_format;
    return QM_EOK;
}

int qm_audio_get_input_stream_format(qm_audio_stream_format_t *format)
{
    assert(format != NULL);
    *format = fake_input_stream_format;
    return QM_EOK;
}

int qm_audio_capture_enable(qm_audio_capture_subscription_t *subscription,
                                   qm_bool enabled)
{
    assert(subscription == fake_capture_subscription);
    capture_last_enabled = enabled;
    capture_enable_calls++;
    return QM_EOK;
}

qm_audio_capture_t *qm_audio_get_capture(void)
{
    return fake_capture;
}

int qm_audio_capture_subscribe(qm_audio_capture_t *capture,
                               const qm_audio_capture_config_t *config,
                               qm_audio_capture_subscription_t **subscription)
{
    assert(capture == fake_capture);
    assert(config != NULL && subscription != NULL);
    capture_config = *config;
    *subscription = fake_capture_subscription;
    return QM_EOK;
}

int qm_audio_capture_unsubscribe(qm_audio_capture_subscription_t *subscription)
{
    assert(subscription == fake_capture_subscription);
    return capture_unsubscribe_result;
}

int qm_audio_control_subscribe(qm_audio_control_t *control,
                               qm_audio_event_cb_t callback,
                               void *ctx,
                               qm_audio_subscription_t *subscription)
{
    (void)control;
    assert(callback != NULL && subscription != NULL);
    audio_event_callback = callback;
    audio_event_context = ctx;
    *subscription = fake_subscription;
    return QM_EOK;
}

int qm_audio_control_unsubscribe(qm_audio_control_t *control,
                                 qm_audio_subscription_t subscription)
{
    (void)control;
    assert(subscription == fake_subscription);
    audio_event_callback = NULL;
    audio_event_context = NULL;
    return QM_EOK;
}

int qm_audio_control_play(qm_audio_control_t *control,
                         const qm_audio_control_play_request_t *request,
                         qm_audio_operation_id_t *operation_id)
{
    qm_audio_event_t event;

    (void)control;
    assert(request != NULL && operation_id != NULL);
    assert(request->source.type == QM_AUDIO_SOURCE_PUSH);
    submitted_policy.priority = request->priority;
    submitted_policy.policy = request->admission;
    submitted_policy.acquire_timeout_ms = request->admission_timeout_ms;
    submitted_codec = request->codec;
    if (play_result != QM_EOK) return play_result;
    *operation_id = submitted_operation;
    if (emit_started_from_play && audio_event_callback != NULL) {
        memset(&event, 0, sizeof(event));
        event.type = QM_AUDIO_EVENT_STARTED;
        event.operation_id = submitted_operation;
        audio_event_callback(&event, audio_event_context);
    }
    return QM_EOK;
}

int qm_audio_control_stream_write(qm_audio_control_t *control,
                                  qm_audio_operation_id_t operation,
                                  const void *data,
                                  uint32_t size,
                                  uint32_t timeout_ms)
{
    (void)control;
    /* 下行写入使用有界背压等待(非 QM_NO_WAIT), 环满时阻塞到排空或超时。 */
    assert(timeout_ms == CONFIG_QM_AGENT_AUDIO_ENQUEUE_WAIT_MS &&
           data != NULL && size != 0U);
    submitted_write_operation = operation;
    if (write_result != QM_EOK) return write_result;
    assert(size <= sizeof(submitted_bytes));
    memcpy(submitted_bytes, data, size);
    submitted_bytes_len = size;
    return QM_EOK;
}

int qm_audio_control_stream_write_packet(qm_audio_control_t *control,
                                         qm_audio_operation_id_t operation,
                                         const qm_audio_data_t *packet,
                                         uint32_t timeout_ms)
{
    (void)control;
    /* PACKET 封装同样使用有界背压等待。 */
    assert(timeout_ms == CONFIG_QM_AGENT_AUDIO_ENQUEUE_WAIT_MS &&
           packet != NULL && packet->data != NULL);
    submitted_write_operation = operation;
    submitted_packet = *packet;
    submitted_packet_writes++;
    return write_result;
}

int qm_audio_control_stream_finish(qm_audio_control_t *control,
                                   qm_audio_operation_id_t operation)
{
    (void)control;
    assert(operation == submitted_operation);
    return finish_result;
}

int qm_audio_control_stop(qm_audio_control_t *control,
                          qm_audio_operation_id_t operation,
                          uint32_t reason)
{
    (void)control;
    (void)reason;
    assert(operation == submitted_operation);
    stop_calls++;
    return stop_result;
}

int qm_audio_control_wait(qm_audio_control_t *control,
                          qm_audio_operation_id_t operation,
                          uint32_t timeout_ms)
{
    (void)control;
    (void)timeout_ms;
    assert(operation == submitted_operation);
    wait_calls++;
    return wait_result;
}

int qm_audio_control_release(qm_audio_control_t *control,
                             qm_audio_operation_id_t operation,
                             uint32_t timeout_ms)
{
    (void)control;
    (void)timeout_ms;
    assert(operation == submitted_operation);
    release_calls++;
    return release_result;
}

static void reset_fakes(void)
{
    memset(&capture_config, 0, sizeof(capture_config));
    memset(&posted_audio_event, 0, sizeof(posted_audio_event));
    memset(&submitted_codec, 0, sizeof(submitted_codec));
    memset(&submitted_packet, 0, sizeof(submitted_packet));
    memset(submitted_bytes, 0, sizeof(submitted_bytes));
    posted_audio_error = QM_EOK;
    posted_audio_error_generation = QM_AUDIO_INVALID_GENERATION;
    posted_audio_events = 0U;
    posted_audio_errors = 0U;
    scheduled_audio = requested_begins = remote_interrupts = 0U;
    emitted_started = 0U;
    emitted_completed = 0U;
    emitted_failed = 0U;
    emitted_failure_code = QM_EOK;
    emitted_failure_generation = 0U;
    capture_enable_calls = 0U;
    capture_upload_calls = 0U;
    stop_calls = 0U;
    wait_calls = 0U;
    release_calls = 0U;
    submitted_write_operation = QM_AUDIO_INVALID_REQUEST_ID;
    submitted_bytes_len = 0U;
    submitted_packet_writes = 0U;
    play_result = QM_EOK;
    write_result = QM_EOK;
    finish_result = QM_EOK;
    stop_result = QM_EOK;
    wait_result = -QM_EINTR;
    release_result = QM_EOK;
    capture_unsubscribe_result = QM_EOK;
    emit_started_from_play = QM_FALSE;
    capture_last_enabled = QM_FALSE;
    audio_event_callback = NULL;
    audio_event_context = NULL;
    memset(&fake_output_stream_format, 0, sizeof(fake_output_stream_format));
    memset(&fake_input_stream_format, 0, sizeof(fake_input_stream_format));
    fake_output_format.sample_rate_hz = 16000U;
    fake_output_format.channels = 1U;
    fake_output_format.sample = QM_AUDIO_PCM_S16;
    fake_input_format = fake_output_format;
    fake_output_stream_format.pcm = fake_output_format;
    fake_output_stream_format.codec = QM_AUDIO_CODEC_PCM;
    fake_input_stream_format = fake_output_stream_format;
}

static qm_agent_handle_t make_agent(qm_bool capture_enabled)
{
    qm_agent_handle_t handle;
    int ret;

    handle = (qm_agent_handle_t)calloc(1U, sizeof(*handle));
    assert(handle != NULL);
    handle->config.audio.control = (qm_audio_control_t *)0x10;
    handle->config.audio.capture_enabled = capture_enabled;
    ret = qm_agent_audio_engine_open(handle);
    assert(ret == QM_EOK);
    return handle;
}

static void free_agent(qm_agent_handle_t handle)
{
    if (handle == NULL) return;
    assert(qm_agent_audio_engine_close(handle) == QM_EOK);
    free(handle);
}

static qm_agent_audio_format_t pcm_format(const qm_agent_handle_t handle)
{
    qm_agent_audio_format_t format;

    fill_pcm_wire_format(&format, &handle->audio.output_format);
    return format;
}

static void test_format_validation(void)
{
    qm_agent_audio_format_t resolved;
    qm_agent_audio_format_t input = {0};
    qm_audio_codec_config_t codec_config;
    qm_agent_handle_t handle;

    reset_fakes();
    handle = make_agent(QM_FALSE);
    assert(qm_agent_audio_engine_resolve_format(&resolved, NULL,
                                                &handle->audio.output_format) == QM_EOK);
    assert(resolved.codec == QM_AGENT_AUDIO_CODEC_PCM &&
           resolved.data_type == QM_AGENT_AUDIO_CODEC_PCM &&
           resolved.sample_rate == 16000U);
    input.codec = QM_AGENT_AUDIO_CODEC_OPUS;
    input.framing = QM_AGENT_AUDIO_FRAMING_OPUS_CBR;
    input.packet_bytes = 40U;
    assert(qm_agent_audio_engine_resolve_format(&resolved, &input,
                                                &handle->audio.output_format) == QM_EOK);
    input.packet_bytes = 0U;
    assert(qm_agent_audio_engine_resolve_format(&resolved, &input,
                                                &handle->audio.output_format) == -QM_EINVAL);

    /* frame_ms is a wire-format field for every stream, but the qm_audio
     * codec contract currently uses packet_duration_ms only for Opus input. */
    input = (qm_agent_audio_format_t){
        .codec = QM_AGENT_AUDIO_CODEC_MP3,
        .data_type = QM_AGENT_AUDIO_CODEC_MP3,
        .sample_rate = 16000U,
        .channel_num = 1U,
        .bits_per_sample = 16U,
        .frame_ms = 20U,
        .framing = QM_AGENT_AUDIO_FRAMING_STREAM,
    };
    assert(build_codec(&input, &codec_config) == QM_EOK);
    assert(codec_config.codec == QM_AUDIO_CODEC_MP3 &&
           codec_config.packet_duration_ms == 0U);
    free_agent(handle);
}

static void test_push_pcm_lifecycle(void)
{
    qm_agent_handle_t handle;
    qm_agent_audio_format_t format;
    qm_audio_event_t event;
    const uint8_t bytes[] = {'a', 'b'};

    reset_fakes();
    handle = make_agent(QM_FALSE);
    assert(qm_agent_audio_engine_start(handle) == QM_EOK);
    format = pcm_format(handle);
    assert(qm_agent_audio_engine_begin_stream(handle, &format) == QM_EOK);
    assert(handle->audio.playback_operation == submitted_operation);
    assert(qm_agent_audio_engine_enqueue_transport(handle, bytes, sizeof(bytes),
                                                    &format) == QM_EOK);
    assert(submitted_write_operation == submitted_operation &&
           submitted_bytes_len == sizeof(bytes) &&
           memcmp(submitted_bytes, bytes, sizeof(bytes)) == 0);
    assert(qm_agent_audio_engine_finish_stream_input(handle) == QM_EOK);
    assert(finish_result == QM_EOK);

    memset(&event, 0, sizeof(event));
    event.type = QM_AUDIO_EVENT_STARTED;
    event.operation_id = submitted_operation;
    assert(qm_agent_audio_engine_handle_event(handle, &event) == QM_EOK);
    assert(emitted_started == 1U);
    memset(&event, 0, sizeof(event));
    event.type = QM_AUDIO_EVENT_FINISHED;
    event.operation_id = submitted_operation;
    assert(qm_agent_audio_engine_handle_event(handle, &event) == QM_EOK);
    assert(wait_calls == 1U && release_calls == 1U && emitted_completed == 1U);
    assert(handle->audio.playback_operation == QM_AUDIO_INVALID_REQUEST_ID);
    free_agent(handle);
}

static void test_first_frame_started_race(void)
{
    qm_agent_handle_t handle;
    qm_agent_audio_format_t format;

    reset_fakes();
    emit_started_from_play = QM_TRUE;
    handle = make_agent(QM_FALSE);
    assert(qm_agent_audio_engine_start(handle) == QM_EOK);
    format = pcm_format(handle);
    assert(qm_agent_audio_engine_begin_stream(handle, &format) == QM_EOK);
    assert(posted_audio_events == 1U);
    assert(qm_agent_audio_engine_handle_event(handle, &posted_audio_event) == QM_EOK);
    assert(emitted_started == 1U);
    free_agent(handle);
}

static void test_encoded_input_and_backpressure(void)
{
    qm_agent_handle_t handle;
    qm_agent_audio_format_t format;
    qm_agent_audio_format_t mismatch;
    qm_audio_event_t event;
    const uint8_t bytes[] = {1U, 2U, 3U};

    reset_fakes();
    handle = make_agent(QM_FALSE);
    assert(qm_agent_audio_engine_start(handle) == QM_EOK);
    format = pcm_format(handle);
    assert(qm_agent_audio_engine_begin_stream(handle, &format) == QM_EOK);
    mismatch = format;
    mismatch.sample_rate = 8000U;
    assert(qm_agent_audio_engine_enqueue_transport(handle, bytes, sizeof(bytes),
                                                    &mismatch) == -QM_EINVAL);
    write_result = -QM_EFULL;
    assert(qm_agent_audio_engine_enqueue_transport(handle, bytes, sizeof(bytes),
                                                    &format) == -QM_EFULL);
    /* 环满是可恢复背压：原样返回给传输层丢弃该帧，不升级为终态错误、也不封锁后续帧。 */
    assert(posted_audio_errors == 0U);
    assert(qm_agent_audio_engine_enqueue_transport(handle, bytes, sizeof(bytes),
                                                    &format) == -QM_EFULL);
    assert(posted_audio_errors == 0U);
    /* 流已封口(-QM_EBUSY)同样只是可恢复信号。 */
    write_result = -QM_EBUSY;
    assert(qm_agent_audio_engine_enqueue_transport(handle, bytes, sizeof(bytes),
                                                    &format) == -QM_EBUSY);
    assert(posted_audio_errors == 0U);
    /* 只有真正的流错误才上报，且只上报一次并封锁后续帧。 */
    write_result = -QM_EIO;
    assert(qm_agent_audio_engine_enqueue_transport(handle, bytes, sizeof(bytes),
                                                    &format) == -QM_EIO);
    assert(posted_audio_errors == 1U && posted_audio_error == -QM_EIO &&
           posted_audio_error_generation == handle->audio.stream_generation);
    assert(qm_agent_audio_engine_enqueue_transport(handle, bytes, sizeof(bytes),
                                                    &format) == -QM_EBUSY);
    assert(posted_audio_errors == 1U);

    memset(&event, 0, sizeof(event));
    event.type = QM_AUDIO_EVENT_ERROR;
    event.operation_id = submitted_operation;
    event.result = -QM_EIO;
    assert(qm_agent_audio_engine_handle_event(handle, &event) == QM_EOK);
    assert(emitted_failed == 1U);
    free_agent(handle);
}

static void test_abort_and_capture(void)
{
    qm_agent_handle_t handle;
    qm_agent_audio_format_t format;
    qm_audio_data_t frame = {0};

    reset_fakes();
    handle = make_agent(QM_TRUE);
    assert(qm_agent_audio_engine_start(handle) == QM_EOK);
    assert(capture_config.frame_cb != NULL &&
           capture_config.delivery == QM_AUDIO_CAPTURE_DELIVERY_CALLBACK);
    qm_agent_audio_engine_set_transport_ready(handle, QM_TRUE);
    qm_agent_set_upload_enabled(handle, QM_TRUE);
    frame.data = "ab";
    frame.size = 2U;
    capture_config.frame_cb(&frame, capture_config.user_ctx);
    assert(capture_upload_calls == 1U);

    format = pcm_format(handle);
    assert(qm_agent_audio_engine_begin_stream(handle, &format) == QM_EOK);
    assert(qm_agent_audio_engine_abort_stream(handle, QM_TRUE) == QM_EOK);
    assert(stop_calls == 1U && release_calls == 1U &&
           handle->audio.playback_operation == QM_AUDIO_INVALID_REQUEST_ID &&
           !handle->audio.stream_accepting);
    assert(qm_agent_audio_engine_stop(handle) == QM_EOK);
    assert(handle->audio.capture_sub == NULL && !handle->audio.audio_subscription_valid);
    free_agent(handle);
}

static void test_native_opus_cbr_bridge(void)
{
    qm_agent_handle_t handle;
    qm_agent_audio_format_t format;
    qm_audio_data_t packet = {0};
    uint8_t bytes[80] = {0x50U};

    reset_fakes();
    fake_output_stream_format.codec = QM_AUDIO_CODEC_OPUS;
    fake_output_stream_format.packet_duration_ms = 40U;
    fake_output_stream_format.packet_bytes = sizeof(bytes);
    fake_output_stream_format.max_packet_bytes = sizeof(bytes);
    fake_input_stream_format = fake_output_stream_format;
    handle = make_agent(QM_TRUE);
    assert(handle->audio.transport_input_format.codec == QM_AGENT_AUDIO_CODEC_OPUS &&
           handle->audio.transport_input_format.framing == QM_AGENT_AUDIO_FRAMING_OPUS_CBR &&
           handle->audio.transport_input_format.packet_bytes == sizeof(bytes));
    assert(qm_agent_audio_engine_start(handle) == QM_EOK);
    assert(capture_config.frame_cb == NULL && capture_config.packet_cb != NULL);
    qm_agent_audio_engine_set_transport_ready(handle, QM_TRUE);
    qm_agent_set_upload_enabled(handle, QM_TRUE);
    packet.data = bytes;
    packet.size = sizeof(bytes);
    packet.duration_ms = 40U;
    packet.flags = QM_AUDIO_DATA_DURATION_VALID;
    capture_config.packet_cb(&packet, capture_config.user_ctx);
    assert(capture_upload_calls == 1U);

    format = handle->audio.transport_output_format;
    assert(qm_agent_audio_engine_begin_stream(handle, &format) == QM_EOK);
    assert(submitted_codec.codec == QM_AUDIO_CODEC_OPUS &&
           submitted_codec.framing == QM_AUDIO_CODEC_FRAMING_OPUS_CBR &&
           submitted_codec.packet_duration_ms == 40U &&
           submitted_codec.packet_bytes == sizeof(bytes));
    assert(qm_agent_audio_engine_enqueue_transport(handle, bytes, 31U, &format) == QM_EOK);
    assert(qm_agent_audio_engine_enqueue_transport(handle, bytes + 31U, 49U, &format) == QM_EOK);
    assert(submitted_packet_writes == 0U && submitted_bytes_len == 49U);
    assert(qm_agent_audio_engine_stop(handle) == QM_EOK);
    free_agent(handle);
}

static int interrupt_remote(void *ctx)
{
    (void)ctx;
    remote_interrupts++;
    return QM_EOK;
}

static void test_preemption_and_retry(void)
{
    qm_agent_handle_t handle;
    qm_agent_audio_format_t format;
    qm_audio_event_t event = {0};
    qm_agent_transport_if_t transport = {.interrupt = interrupt_remote};

    reset_fakes();
    handle = make_agent(QM_FALSE);
    handle->config.transport_if = &transport;
    assert(qm_agent_audio_engine_start(handle) == QM_EOK);
    qm_agent_audio_engine_set_transport_ready(handle, QM_TRUE);
    format = pcm_format(handle);
    assert(qm_agent_audio_engine_begin_stream(handle, &format) == QM_EOK);
    assert(submitted_policy.priority == QM_AUDIO_PRIORITY_HIGH &&
           submitted_policy.policy == QM_AUDIO_ADMISSION_PREEMPT_LOWER &&
           submitted_policy.acquire_timeout_ms == CONFIG_QM_AGENT_AUDIO_ACQUIRE_TIMEOUT_MS);
    event.type = QM_AUDIO_EVENT_PREEMPTED;
    event.operation_id = submitted_operation;
    release_result = -QM_EBUSY;
    assert(qm_agent_audio_engine_handle_event(handle, &event) == -QM_EBUSY);
    assert(remote_interrupts == 1U && emitted_failed == 0U &&
           handle->audio.playback_operation == submitted_operation && scheduled_audio != 0U);
    assert(qm_agent_audio_engine_enqueue_transport(handle, "ab", 2U, &format) == -QM_EBUSY);
    release_result = QM_EOK;
    assert(qm_agent_audio_engine_reconcile(handle) == QM_EOK);
    assert(emitted_failed == 1U && remote_interrupts == 1U);
    assert(qm_agent_audio_engine_request_stream_begin(handle, &format) == QM_EOK);
    assert(requested_begins == 0U); /* Old packets must not resurrect the reply. */
    assert(qm_agent_audio_engine_handle_event(handle, &event) == QM_EOK);
    assert(emitted_failed == 1U); /* Late duplicate terminal is harmless. */
    assert(qm_agent_audio_engine_finish_stream_input(handle) == QM_EOK);
    assert(qm_agent_audio_engine_request_stream_begin(handle, &format) == QM_EOK);
    assert(requested_begins == 1U); /* Ordered EOF opens the next reply boundary. */
    free_agent(handle);
}

static void test_user_capture_gate(void)
{
    qm_agent_handle_t handle;
    qm_agent_audio_format_t format;
    qm_audio_event_t event = {0};

    reset_fakes();
    handle = make_agent(QM_TRUE);
    assert(qm_agent_audio_engine_start(handle) == QM_EOK);
    qm_agent_audio_engine_set_transport_ready(handle, QM_TRUE);
    qm_agent_set_upload_enabled(handle, QM_FALSE);
    qm_agent_audio_engine_set_dialog_state(handle, QM_AGENT_DIALOG_STATE_SILENT);
    format = pcm_format(handle);
    assert(qm_agent_audio_engine_begin_stream(handle, &format) == QM_EOK);
    event.operation_id = submitted_operation;
    event.type = QM_AUDIO_EVENT_STARTED;
    assert(qm_agent_audio_engine_handle_event(handle, &event) == QM_EOK);
    event.type = QM_AUDIO_EVENT_FINISHED;
    assert(qm_agent_audio_engine_handle_event(handle, &event) == QM_EOK);
    qm_agent_audio_engine_set_transport_ready(handle, QM_FALSE);
    qm_agent_audio_engine_set_transport_ready(handle, QM_TRUE);
    assert(!qm_agent_audio_engine_upload_allowed(handle));
    assert(qm_agent_audio_engine_reconcile(handle) == QM_EOK && !capture_last_enabled);
    qm_agent_set_upload_enabled(handle, QM_TRUE);
    qm_agent_audio_engine_set_dialog_state(handle, QM_AGENT_DIALOG_STATE_THINKING);
    assert(!qm_agent_audio_engine_upload_allowed(handle));
    qm_agent_audio_engine_set_dialog_state(handle, QM_AGENT_DIALOG_STATE_SILENT);
    assert(qm_agent_audio_engine_upload_allowed(handle));
    qm_agent_set_upload_enabled(handle, QM_FALSE);
    assert(qm_agent_audio_engine_stop(handle) == QM_EOK);
    assert(qm_agent_audio_engine_start(handle) == QM_EOK);
    qm_agent_audio_engine_set_transport_ready(handle, QM_TRUE);
    assert(!qm_agent_audio_engine_upload_allowed(handle));
    free_agent(handle);
}

static void test_input_failure_retry(void)
{
    qm_agent_handle_t handle;
    qm_agent_audio_format_t format;
    qm_agent_transport_if_t transport = {.interrupt = interrupt_remote};
    uint32_t generation;

    reset_fakes();
    handle = make_agent(QM_FALSE);
    handle->config.transport_if = &transport;
    assert(qm_agent_audio_engine_start(handle) == QM_EOK);
    qm_agent_audio_engine_set_transport_ready(handle, QM_TRUE);
    format = pcm_format(handle);
    assert(qm_agent_audio_engine_begin_stream(handle, &format) == QM_EOK);
    generation = handle->audio.stream_generation;
    write_result = -QM_EIO;
    assert(qm_agent_audio_engine_enqueue_transport(handle, "ab", 2U, &format) == -QM_EIO);
    assert(qm_agent_audio_engine_fail_stream(handle, -QM_EIO, generation - 1U) == QM_EOK);
    assert(stop_calls == 0U && emitted_failed == 0U);
    stop_result = -QM_EBUSY;
    assert(qm_agent_audio_engine_fail_stream(handle, posted_audio_error,
                                             posted_audio_error_generation) == -QM_EBUSY);
    assert(remote_interrupts == 1U && stop_calls == 1U && release_calls == 0U);
    assert(emitted_failed == 0U && handle->audio.cleanup_pending);
    stop_result = QM_EOK;
    assert(qm_agent_audio_engine_reconcile(handle) == QM_EOK);
    assert(emitted_failed == 1U && emitted_failure_code == -QM_EIO &&
           emitted_failure_generation == generation && remote_interrupts == 1U);
    assert(!handle->audio.stream_accepting && !qm_agent_audio_engine_stream_active(handle));
    assert(qm_agent_audio_engine_fail_stream(handle, -QM_EIO, generation) == QM_EOK);
    assert(emitted_failed == 1U);
    free_agent(handle);
}

static void test_reply_boundary_during_cleanup(void)
{
    qm_agent_handle_t handle;
    qm_agent_audio_format_t format;
    qm_audio_event_t event = {.type = QM_AUDIO_EVENT_PREEMPTED};
    uint32_t generation;

    reset_fakes();
    handle = make_agent(QM_FALSE);
    assert(qm_agent_audio_engine_start(handle) == QM_EOK);
    format = pcm_format(handle);
    assert(qm_agent_audio_engine_begin_stream(handle, &format) == QM_EOK);
    generation = handle->audio.stream_generation;
    assert(qm_agent_audio_engine_begin_reply(handle, &format) == QM_EOK);
    assert(handle->audio.stream_generation == generation && stop_calls == 0U);
    event.operation_id = submitted_operation;
    wait_result = -QM_EBUSY;
    assert(qm_agent_audio_engine_handle_event(handle, &event) == -QM_EBUSY);
    assert(qm_agent_audio_engine_begin_reply(handle, &format) == -QM_EBUSY);
    assert(handle->audio.stream_accepting && handle->audio.cleanup_pending);
    assert(qm_agent_audio_engine_enqueue_transport(handle, "ab", 2U, &format) == -QM_EBUSY);
    wait_result = QM_EOK;
    assert(qm_agent_audio_engine_reconcile(handle) == QM_EOK);
    assert(emitted_failed == 1U && handle->audio.stream_accepting);
    assert(qm_agent_audio_engine_request_stream_begin(handle, &format) == QM_EOK);
    assert(requested_begins == 1U);
    free_agent(handle);
}

static void test_policy_copy_and_busy_wait(void)
{
    qm_agent_audio_playback_config_t policy = {
        .priority = QM_AUDIO_PRIORITY_LOW,
        .policy = QM_AUDIO_ADMISSION_REJECT_IF_BUSY,
        .acquire_timeout_ms = 25U,
    };
    qm_agent_handle_t handle = calloc(1, sizeof(*handle));
    qm_agent_audio_format_t format;
    qm_audio_event_t event = {0};

    reset_fakes();
    handle->config.audio.control = (qm_audio_control_t *)0x10;
    handle->config.audio.playback = &policy;
    assert(qm_agent_audio_engine_open(handle) == QM_EOK);
    memset(&policy, 0, sizeof(policy));
    assert(qm_agent_audio_engine_start(handle) == QM_EOK);
    format = pcm_format(handle);
    assert(qm_agent_audio_engine_begin_stream(handle, &format) == QM_EOK);
    assert(submitted_policy.priority == QM_AUDIO_PRIORITY_LOW &&
           submitted_policy.policy == QM_AUDIO_ADMISSION_REJECT_IF_BUSY &&
           submitted_policy.acquire_timeout_ms == 25U);
    wait_result = -QM_EBUSY;
    event.operation_id = submitted_operation;
    event.type = QM_AUDIO_EVENT_FINISHED;
    assert(qm_agent_audio_engine_handle_event(handle, &event) == -QM_EBUSY);
    assert(release_calls == 0U && emitted_completed == 0U);
    wait_result = QM_EOK;
    assert(qm_agent_audio_engine_reconcile(handle) == QM_EOK);
    assert(release_calls == 1U && emitted_completed == 1U);
    free_agent(handle);
}

int main(void)
{
    test_format_validation();
    test_push_pcm_lifecycle();
    test_first_frame_started_race();
    test_encoded_input_and_backpressure();
    test_abort_and_capture();
    test_native_opus_cbr_bridge();
    test_preemption_and_retry();
    test_user_capture_gate();
    test_input_failure_retry();
    test_reply_boundary_during_cleanup();
    test_policy_copy_and_busy_wait();
    puts("PASS: qm_audio PUSH bridge, lifecycle, format, backpressure, and capture");
    return 0;
}
