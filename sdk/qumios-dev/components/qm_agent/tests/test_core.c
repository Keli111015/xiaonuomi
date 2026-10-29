/** @file test_core.c @brief Agent lifecycle and TTS transaction regressions. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "qm_agent_internal.h"

/* Core uses only the cloud listener contract in these tests. */
#define __QM_IOT_CLOUD_H__
enum { QM_IOT_EVENT_CLOUD_CONN, QM_IOT_EVENT_CLOUD_DISCONN };
typedef struct { int type; } qm_iot_event_t;
typedef void (*qm_iot_event_handler_t)(void *, const qm_iot_event_t *, void *);
int32_t qm_iot_event_register(void *handle, qm_iot_event_handler_t cb, void *ctx);
int32_t qm_iot_event_unregister_one(void *handle, qm_iot_event_handler_t cb, void *ctx);

#include "../src/core/qm_agent_core.c"
#include "../src/core/qm_agent_event.c"

#ifndef QM_AGENT_TEST_REAL_AUDIO
uint32_t qm_strlen(const char *s) { return (uint32_t)strlen(s); }

static qm_sem_t blocked_control_entered;
static qm_sem_t blocked_control_release;
static qm_sem_t mailbox_terminal;
static qm_bool observe_mailbox;
static unsigned mailbox_events;
static unsigned stale_begins;
static qm_audio_event_type_t mailbox_order[2];
static int audio_stop_result;
static int audio_close_result;
static int audio_abort_result;
static unsigned transport_interrupts;
static int transport_stop_result;
static unsigned transport_stops;
static unsigned transport_deinits;
static unsigned audio_starts;
static unsigned audio_stops;
static unsigned terminal_callbacks;
static qm_agent_tts_event_t last_tts_event;
static int last_tts_code;
static qm_sem_t notice_sem;
static qm_sem_t notice_handler_entered;
static qm_sem_t notice_handler_release;
static qm_agent_event_notice_t last_notice;
static int32_t last_notice_id;
static unsigned notice_count;
static qm_bool block_notice_handler;

#endif

void *qm_malloc(unsigned int n) { return malloc(n); }
void qm_free(void *p) { free(p); }
void *qm_memcpy(void *to, const void *from, uint32_t n) { return memcpy(to, from, n); }
void *qm_memset(void *to, int32_t value, uint32_t n) { return memset(to, value, n); }
char *qm_strdup(const char *s) { return strdup(s); }

int32_t qm_iot_event_register(void *handle, qm_iot_event_handler_t cb, void *ctx)
{ (void)handle; (void)cb; (void)ctx; return QM_EOK; }
int32_t qm_iot_event_unregister_one(void *handle, qm_iot_event_handler_t cb, void *ctx)
{ (void)handle; (void)cb; (void)ctx; return QM_EOK; }
int qm_agent_auth_deinit(qm_agent_auth_t *auth) { (void)auth; return QM_EOK; }
qm_agent_auth_t *qm_agent_auth_init(qm_agent_handle_t owner) { (void)owner; return NULL; }
void qm_agent_auth_process_pending(qm_agent_auth_t *auth) { (void)auth; }
void qm_agent_auth_cancel(qm_agent_auth_t *auth) { (void)auth; }
int qm_agent_auth_get_token(qm_agent_auth_t *auth, qm_agent_auth_info_t *info)
{ (void)auth; (void)info; return -QM_ENOENT; }
void qm_agent_auth_info_deinit(qm_agent_auth_info_t *info) { (void)info; }
int qm_agent_auth_request_token(qm_agent_auth_t *auth, uint32_t id)
{ (void)auth; (void)id; return QM_EOK; }
int qm_agent_auth_retry_now(qm_agent_auth_t *auth, uint32_t generation)
{ (void)auth; (void)generation; return QM_EOK; }
const qm_agent_transport_if_t *qm_agent_transport_get_default(qm_agent_transport_type_t type)
{ (void)type; return NULL; }

#ifndef QM_AGENT_TEST_REAL_AUDIO
int qm_agent_audio_engine_open(qm_agent_handle_t handle) { (void)handle; return QM_EOK; }
int qm_agent_audio_engine_close(qm_agent_handle_t handle)
{
    if (audio_close_result == QM_EOK && handle->audio.runtime_valid) {
        assert(qm_mutex_free(&handle->audio.lock) == QM_EOK);
        handle->audio.runtime_valid = QM_FALSE;
        handle->audio.lock_valid = QM_FALSE;
    }
    return audio_close_result;
}
int qm_agent_audio_engine_start(qm_agent_handle_t handle)
{
    audio_starts++;
    handle->audio.transport_ready = QM_FALSE;
    handle->audio.upload_enabled = QM_FALSE;
    return QM_EOK;
}
int qm_agent_audio_engine_stop(qm_agent_handle_t handle)
{ (void)handle; audio_stops++; return audio_stop_result; }
int qm_agent_audio_engine_abort_stream(qm_agent_handle_t handle, qm_bool ack)
{ (void)ack; handle->audio.stream_generation++; return audio_abort_result; }
int qm_agent_audio_engine_begin_reply(qm_agent_handle_t handle,
                                      const qm_agent_audio_format_t *format)
{ (void)handle; (void)format; return QM_EOK; }
int qm_agent_audio_engine_finish_stream_input(qm_agent_handle_t handle)
{ (void)handle; return QM_EOK; }
uint32_t qm_agent_audio_engine_stream_generation(qm_agent_handle_t handle)
{ return handle->audio.stream_generation; }
void qm_agent_audio_engine_set_transport_ready(qm_agent_handle_t handle, qm_bool ready)
{ handle->audio.transport_ready = ready; handle->audio.upload_enabled = ready; }
int qm_agent_audio_engine_fail_stream(qm_agent_handle_t handle, int code, uint32_t generation)
{ (void)handle; (void)code; (void)generation; return QM_EOK; }
int qm_agent_audio_engine_reconcile(qm_agent_handle_t handle)
{ (void)handle; return QM_EOK; }
void qm_agent_audio_engine_set_dialog_state(qm_agent_handle_t handle,
                                           qm_agent_dialog_state_t state)
{ (void)handle; (void)state; }
qm_bool qm_agent_audio_engine_upload_allowed(qm_agent_handle_t handle)
{ return (handle != NULL && handle->audio.runtime_valid &&
         handle->audio.upload_enabled && handle->audio.transport_ready &&
         !handle->audio.capture_closing) ? QM_TRUE : QM_FALSE; }
qm_bool qm_agent_audio_engine_stream_active(qm_agent_handle_t handle)
{ return (handle != NULL && handle->audio.playback_operation != QM_AUDIO_INVALID_REQUEST_ID) ? QM_TRUE : QM_FALSE; }
int qm_agent_audio_engine_begin_stream(qm_agent_handle_t handle,
                                       const qm_agent_audio_format_t *format)
{ (void)handle; (void)format; stale_begins++; return QM_EOK; }
int qm_agent_audio_engine_request_stream_begin(
    qm_agent_handle_t handle,
    const qm_agent_audio_format_t *format)
{ (void)handle; (void)format; return QM_EOK; }
int qm_agent_audio_engine_handle_event(qm_agent_handle_t handle,
                                       const qm_audio_event_t *event)
{
    (void)handle;
    if (observe_mailbox) {
        assert(mailbox_events < 2U);
        mailbox_order[mailbox_events++] = event->type;
        if (event->type == QM_AUDIO_EVENT_FINISHED) qm_sem_signal(&mailbox_terminal);
    }
    return QM_EOK;
}
int qm_agent_audio_engine_enqueue_transport(qm_agent_handle_t handle,
                                             const void *data, uint32_t len,
                                             const qm_agent_audio_format_t *format)
{ (void)handle; (void)data; (void)len; (void)format; return QM_EOK; }
void qm_agent_set_upload_enabled(qm_agent_handle_t handle, qm_bool enabled)
{ handle->audio.upload_enabled = enabled; }
int qm_agent_audio_engine_resolve_format(qm_agent_audio_format_t *out,
                                         const qm_agent_audio_format_t *in,
                                         const qm_audio_pcm_format_t *hardware)
{ (void)hardware; *out = *in; return QM_EOK; }
int qm_agent_audio_engine_resolve_stream_format(
    qm_agent_audio_format_t *out,
    const qm_agent_audio_format_t *in,
    const qm_audio_stream_format_t *hardware)
{ (void)hardware; *out = *in; return QM_EOK; }

#endif

int qm_agent_module_manager_init(qm_agent_handle_t handle) { (void)handle; return QM_EOK; }
int qm_agent_module_manager_start(qm_agent_handle_t handle) { (void)handle; return QM_EOK; }
int qm_agent_module_manager_stop(qm_agent_handle_t handle) { (void)handle; return QM_EOK; }
void qm_agent_module_manager_deinit(qm_agent_handle_t handle) { (void)handle; }
static qm_agent_event_t last_module_command;
static unsigned module_commands;
int qm_agent_module_manager_emit(qm_agent_handle_t handle, const qm_agent_module_event_t *event)
{
    (void)handle;
    if (event->id == QM_AGENT_MODULE_EVENT_COMMAND) {
        assert(event->data_len == sizeof(qm_agent_event_t));
        const qm_agent_event_t *command = event->data;
        assert(command->id == QM_AGENT_EVENT_COMMAND);
        qm_agent_event_free(&last_module_command);
        assert(qm_agent_event_clone(&last_module_command, command) == QM_EOK);
        module_commands++;
    }
    return QM_EOK;
}
void qm_agent_module_cloud_request_free(qm_agent_module_cloud_request_t *request)
{ (void)request; }
int qm_agent_module_manager_dispatch_cloud_request(
    qm_agent_handle_t handle,
    qm_agent_module_cloud_request_t *request)
{ (void)handle; (void)request; return QM_EOK; }
int qm_agent_module_manager_emit_cloud_event(
    qm_agent_handle_t handle,
    qm_agent_module_event_id_t event)
{ (void)handle; (void)event; return QM_EOK; }

#ifndef QM_AGENT_TEST_REAL_AUDIO
static int test_start(void *ctx)
{
    qm_agent_handle_t handle = ctx;
    qm_agent_event_t event = {0};
    assert(audio_starts != 0U);
    event.id = QM_AGENT_EVENT_CONNECTED;
    /* The transport callback is normally serialized onto qm_agent_ctl.  This
     * test already runs the start path synchronously, so invoke the same
     * normalized handler directly to model that control-task turn. */
    qm_agent_transport_event_now(&event, handle);
    return QM_EOK;
}
static int test_stop(void *ctx)
{
    (void)ctx;
    assert(audio_stops != 0U);
    transport_stops++;
    return transport_stop_result;
}
static int test_deinit(void *ctx) { (void)ctx; transport_deinits++; return QM_EOK; }
static const qm_agent_transport_if_t test_transport = {
    .start = test_start, .stop = test_stop, .deinit = test_deinit,
};

static qm_agent_handle_t new_handle(void)
{
    qm_agent_handle_t handle = calloc(1U, sizeof(*handle));
    assert(handle != NULL);
    assert(qm_mutex_new(&handle->network_lock) == QM_EOK);
    assert(qm_mutex_new(&handle->audio.lock) == QM_EOK);
    handle->audio.lock_valid = QM_TRUE;
    handle->audio.runtime_valid = QM_TRUE;
    handle->audio.stream_generation = 1U;
    assert(qm_agent_control_init(handle) == QM_EOK);
    assert(qm_agent_tts_runtime_init(handle) == QM_EOK);
    handle->config.transport.type = QM_AGENT_TRANSPORT_CUSTOM;
    handle->config.transport_if = &test_transport;
    handle->transport_ctx = handle;
    handle->state = QM_AGENT_STATE_READY;
    return handle;
}

static void test_start_and_retryable_stop(void)
{
    qm_agent_handle_t handle = new_handle();
    audio_starts = audio_stops = transport_stops = 0U;
    assert(qm_agent_start_now(handle) == QM_EOK);
    assert(handle->state == QM_AGENT_STATE_RUNNING);
    assert(handle->audio.transport_ready && handle->audio.upload_enabled);

    audio_stop_result = -QM_EBUSY;
    assert(qm_agent_stop(handle) == -QM_EBUSY);
    assert(handle->started && handle->state == QM_AGENT_STATE_STOPPING);
    assert(transport_stops == 0U);
    audio_stop_result = QM_EOK;
    transport_stop_result = -QM_ETIMEOUT;
    assert(qm_agent_stop(handle) == -QM_ETIMEOUT);
    assert(handle->started && handle->state == QM_AGENT_STATE_STOPPING);
    transport_stop_result = QM_EOK;
    assert(qm_agent_stop(handle) == QM_EOK);
    assert(!handle->started && handle->state == QM_AGENT_STATE_READY);
    assert(qm_agent_destroy(handle) == QM_EOK);
}

static void test_retryable_destroy(void)
{
    qm_agent_handle_t handle = new_handle();
    unsigned before = transport_deinits;
    audio_close_result = -QM_EBUSY;
    assert(qm_agent_destroy(handle) == -QM_EBUSY);
    assert(handle->tts_request.lock_valid);
    /* Transport remains owned while audio callbacks are still attached. */
    assert(handle->transport_ctx != NULL);
    assert(transport_deinits == before);
    audio_close_result = QM_EOK;
    assert(qm_agent_destroy(handle) == QM_EOK);
    assert(transport_deinits == before + 1U);
}

static void on_tts(qm_agent_handle_t handle, uint32_t id,
                    qm_agent_tts_event_t event, int code, void *ctx)
{
    (void)handle; (void)ctx;
    assert(id == 7U);
    terminal_callbacks++;
    last_tts_event = event;
    last_tts_code = code;
}

static void test_tts_failure_generation(void)
{
    qm_agent_handle_t handle = new_handle();
    qm_agent_event_t event = {0};
    handle->tts_request.active = QM_TRUE;
    handle->tts_request.request_id = 7U;
    handle->tts_request.generation = 11U;
    handle->tts_request.callback = on_tts;
    event.id = QM_AGENT_EVENT_TTS_PLAYBACK_FAILED;
    event.code = -QM_ENOSYS;
    event.data.tts_playback.generation = 10U;
    qm_agent_emit(handle, &event);
    assert(terminal_callbacks == 0U && handle->tts_request.active);
    event.data.tts_playback.generation = 11U;
    qm_agent_emit(handle, &event);
    assert(terminal_callbacks == 1U && !handle->tts_request.active);
    assert(last_tts_event == QM_AGENT_TTS_EVENT_FAILED && last_tts_code == -QM_ENOSYS);
    qm_agent_emit(handle, &event);
    assert(terminal_callbacks == 1U);
    assert(qm_agent_destroy(handle) == QM_EOK);
}

static int test_interrupt(void *ctx)
{
    (void)ctx;
    transport_interrupts++;
    return QM_EOK;
}

static void test_cancel_with_pending_audio_cleanup(void)
{
    qm_agent_handle_t handle = new_handle();
    qm_agent_transport_if_t transport = test_transport;
    unsigned callbacks_before = terminal_callbacks;

    transport.interrupt = test_interrupt;
    handle->config.transport_if = &transport;
    handle->started = QM_TRUE;
    handle->state = QM_AGENT_STATE_RUNNING;
    handle->tts_request.active = QM_TRUE;
    handle->tts_request.request_id = 7U;
    handle->tts_request.callback = on_tts;
    audio_abort_result = -QM_EBUSY;
    assert(qm_agent_tts_cancel(handle, 7U) == QM_EOK);
    assert(!handle->tts_request.active && transport_interrupts == 1U &&
           terminal_callbacks == callbacks_before + 1U &&
           last_tts_event == QM_AGENT_TTS_EVENT_CANCELLED);

    handle->tts_request.active = QM_TRUE;
    handle->tts_request.request_id = 7U;
    handle->tts_request.callback = on_tts;
    audio_abort_result = -QM_ETIMEOUT;
    assert(qm_agent_interrupt(handle) == QM_EOK);
    assert(!handle->tts_request.active && transport_interrupts == 2U &&
           terminal_callbacks == callbacks_before + 2U &&
           last_tts_event == QM_AGENT_TTS_EVENT_CANCELLED);
    audio_abort_result = QM_EOK;
    assert(qm_agent_destroy(handle) == QM_EOK);
}

static int complete_tts_inline(void *ctx, const char *text)
{
    qm_agent_handle_t handle = ctx;
    qm_agent_event_t event = {0};

    event.id = QM_AGENT_EVENT_TTS_PLAYBACK_COMPLETE;
    event.data.tts_playback.generation = handle->tts_request.generation;
    qm_agent_emit(handle, &event);
    /* This borrow lasts until request_tts returns, including nested events. */
    assert(strcmp(text, "test text") == 0);
    return QM_EOK;
}

static void test_synchronous_tts_completion(void)
{
    qm_agent_handle_t handle = new_handle();
    qm_agent_transport_if_t transport = test_transport;
    qm_agent_tts_request_t request = {0};
    uint32_t id = 0U;

    transport.request_tts = complete_tts_inline;
    handle->config.transport_if = &transport;
    handle->started = QM_TRUE;
    request.text = "test text";
    request.timeout_ms = 10U;
    assert(qm_agent_tts_request(handle, &request, &id) == QM_EOK);
    assert(id != 0U && !handle->tts_request.active);
    assert(qm_agent_destroy(handle) == QM_EOK);
}

static void test_event_clone_validation(void)
{
    qm_agent_event_t event = {0};
    qm_agent_event_t copy = {0};
    event.id = QM_AGENT_EVENT_AUDIO;
    event.data.audio.len = 1U;
    assert(qm_agent_event_clone(&copy, &event) == -QM_EINVAL);
    assert(qm_agent_event_clone(&event, &event) == -QM_EINVAL);
    assert(event.id == QM_AGENT_EVENT_AUDIO && event.data.audio.len == 1U);

    event = (qm_agent_event_t){0};
    event.id = QM_AGENT_EVENT_EXPRESSION;
    event.data.expression.visual = QM_AGENT_EXPRESSION_HAPPY;
    event.data.expression.category = QM_AGENT_EXPR_CATEGORY_AFFECT;
    event.data.expression.speech_expression = QM_AGENT_EXPRESSION_HAPPY;
    event.data.expression.speech_category = QM_AGENT_EXPR_CATEGORY_AFFECT;
    event.data.expression.provider_code = "happy_1";
    event.data.expression.provider_speech_code = "happy";
    assert(qm_agent_event_clone(&copy, &event) == QM_EOK);
    assert(copy.data.expression.visual == QM_AGENT_EXPRESSION_HAPPY);
    assert(copy.data.expression.category == QM_AGENT_EXPR_CATEGORY_AFFECT);
    assert(copy.data.expression.speech_expression == QM_AGENT_EXPRESSION_HAPPY);
    assert(copy.data.expression.speech_category == QM_AGENT_EXPR_CATEGORY_AFFECT);
    assert(strcmp(copy.data.expression.provider_code, "happy_1") == 0);
    assert(strcmp(copy.data.expression.provider_speech_code, "happy") == 0);
    assert(copy.data.expression.provider_code != event.data.expression.provider_code);
    assert(copy.data.expression.provider_speech_code !=
           event.data.expression.provider_speech_code);
    qm_agent_event_free(&copy);

    event = (qm_agent_event_t){0};
    event.id = QM_AGENT_EVENT_COMMAND;
    event.data.command.domain = "alarm";
    event.data.command.json = "{\"instructions\":[{\"operation\":\"create\"}]}";
    assert(qm_agent_event_clone(&copy, &event) == QM_EOK);
    assert(copy.data.command.domain != event.data.command.domain &&
           strcmp(copy.data.command.domain, event.data.command.domain) == 0);
    assert(copy.data.command.json != event.data.command.json &&
           strcmp(copy.data.command.json, event.data.command.json) == 0);
    qm_agent_event_free(&copy);
}

static void test_expression_classification(void)
{
    assert(qm_agent_expression_classify(QM_AGENT_EXPRESSION_NEUTRAL) ==
           QM_AGENT_EXPR_CATEGORY_FACIAL_STATE);
    assert(qm_agent_expression_classify(QM_AGENT_EXPRESSION_HAPPY) ==
           QM_AGENT_EXPR_CATEGORY_AFFECT);
    assert(qm_agent_expression_classify(QM_AGENT_EXPRESSION_CURIOUS) ==
           QM_AGENT_EXPR_CATEGORY_SOCIAL_COGNITIVE);
    assert(qm_agent_expression_classify(QM_AGENT_EXPRESSION_BORED) ==
           QM_AGENT_EXPR_CATEGORY_AFFECT);
    assert(qm_agent_expression_classify(QM_AGENT_EXPRESSION_UNWELL) ==
           QM_AGENT_EXPR_CATEGORY_PHYSICAL_STATE);
    assert(qm_agent_expression_classify(QM_AGENT_EXPRESSION_UNKNOWN) ==
           QM_AGENT_EXPR_CATEGORY_UNKNOWN);
}

static void on_public_notice(void *arg,
                             qm_event_base_t base,
                             int32_t event_id,
                             void *event_data,
                             size_t event_data_size)
{
    QM_UNUSED_ARG(arg);
    assert(base == QM_AGENT_EVENT_BASE);
    assert(event_data != NULL);
    assert(event_data_size == sizeof(last_notice));
    if (block_notice_handler) {
        block_notice_handler = QM_FALSE;
        assert(qm_sem_signal(&notice_handler_entered) == QM_EOK);
        assert(qm_sem_wait(&notice_handler_release, 1000U) == QM_EOK);
    }
    last_notice = *(const qm_agent_event_notice_t *)event_data;
    last_notice_id = event_id;
    notice_count++;
    assert(qm_sem_signal(&notice_sem) == QM_EOK);
}

static void test_command_notice(void)
{
    qm_agent_handle_t handle = new_handle();
    assert(qm_agent_event_runtime_init(handle) == QM_EOK);
    qm_agent_event_t event = {0};
    qm_agent_event_notice_t notice = {0};
    char json[sizeof(notice.data.command.json.text) + 32U];
    memset(json, 'x', sizeof(json) - 1U);
    json[sizeof(json) - 1U] = '\0';
    event.id = QM_AGENT_EVENT_COMMAND;
    event.data.command.domain = "alarm";
    event.data.command.json = json;
    assert(build_notice(handle, &event, &notice));
    assert(strcmp(notice.data.command.domain.text, "alarm") == 0);
    assert(notice.data.command.json.truncated &&
           notice.data.command.json.text_length == sizeof(notice.data.command.json.text) - 1U);
    assert(notice.data.command.json.text[sizeof(notice.data.command.json.text) - 1U] == '\0');
    json[0] = 'y';
    assert(notice.data.command.json.text[0] == 'x');
    qm_agent_event_runtime_deinit(handle);
    unsigned before = module_commands;
    qm_agent_transport_event_now(&event, handle);
    assert(module_commands == before + 1U);
    assert(strlen(last_module_command.data.command.json) == sizeof(json) - 1U);
    assert(last_module_command.data.command.json != json &&
           strcmp(last_module_command.data.command.json, json) == 0);
    qm_agent_event_free(&last_module_command);
    assert(qm_agent_destroy(handle) == QM_EOK);
}

static void test_public_event_loop(void)
{
    qm_agent_handle_t handle = new_handle();
    qm_agent_event_t event = {0};
    char text[] = "event text";
    uint32_t agent_id;

    notice_count = 0U;
    last_notice_id = 0;
    block_notice_handler = QM_FALSE;
    qm_memset(&last_notice, 0, sizeof(last_notice));
    /* Simulate the system initialization stage that creates the shared loop
     * before any agent uses it. */
    {
        int create_ret = qm_event_default_create();
        assert(create_ret == QM_EOK || create_ret == -QM_EINIT);
    }
    assert(qm_agent_event_runtime_init(handle) == QM_EOK);
    agent_id = qm_agent_get_id(handle);
    assert(agent_id != 0U);
    assert(qm_sem_new(&notice_sem, 0U) == QM_EOK);
    assert(qm_sem_new(&notice_handler_entered, 0U) == QM_EOK);
    assert(qm_sem_new(&notice_handler_release, 0U) == QM_EOK);
    assert(qm_event_handler_register(QM_AGENT_EVENT_BASE,
                                     QM_EVENT_ANY_ID,
                                     on_public_notice,
                                     NULL) == QM_EOK);

    event.id = QM_AGENT_EVENT_AGENT_TEXT;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    event.data.agent_text.text = text;
    event.data.agent_text.final = QM_TRUE;
    event.data.agent_text.finish_reason = QM_AGENT_TEXT_FINISH_REASON_AUDIT;
    qm_agent_emit(handle, &event);
    text[0] = 'X';
    assert(qm_sem_wait(&notice_sem, 1000U) == QM_EOK);
    assert(notice_count == 1U);
    assert(last_notice_id == QM_AGENT_EVENT_AGENT_TEXT);
    assert(last_notice.agent_id == agent_id);
    assert(last_notice.source == QM_AGENT_EVENT_SOURCE_TRANSPORT);
    assert(last_notice.data.text.final == QM_TRUE);
    assert(last_notice.data.text.finish_reason == QM_AGENT_TEXT_FINISH_REASON_AUDIT);
    assert(last_notice.data.text.text_length == strlen("event text"));
    assert(strcmp(last_notice.data.text.text, "event text") == 0);

    event = (qm_agent_event_t){0};
    event.id = QM_AGENT_EVENT_EXPRESSION;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    event.data.expression.visual = QM_AGENT_EXPRESSION_HAPPY;
    event.data.expression.category = QM_AGENT_EXPR_CATEGORY_AFFECT;
    event.data.expression.speech_expression = QM_AGENT_EXPRESSION_HAPPY;
    event.data.expression.speech_category = QM_AGENT_EXPR_CATEGORY_AFFECT;
    event.data.expression.provider_code = "happy_1";
    event.data.expression.provider_speech_code = "happy";
    qm_agent_emit(handle, &event);
    assert(qm_sem_wait(&notice_sem, 1000U) == QM_EOK);
    assert(notice_count == 2U);
    assert(last_notice_id == QM_AGENT_EVENT_EXPRESSION);
    assert(last_notice.data.expression.visual == QM_AGENT_EXPRESSION_HAPPY);
    assert(last_notice.data.expression.gesture == QM_AGENT_GESTURE_NONE);
    assert(last_notice.data.expression.category == QM_AGENT_EXPR_CATEGORY_AFFECT);
    assert(last_notice.data.expression.speech_expression == QM_AGENT_EXPRESSION_HAPPY);
    assert(last_notice.data.expression.speech_category == QM_AGENT_EXPR_CATEGORY_AFFECT);
    assert(strcmp(last_notice.data.expression.provider_code.text, "happy_1") == 0);
    assert(strcmp(last_notice.data.expression.provider_speech_code.text, "happy") == 0);

    event = (qm_agent_event_t){0};
    event.id = QM_AGENT_EVENT_EXPRESSION;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    event.data.expression.category = QM_AGENT_EXPR_CATEGORY_SOCIAL_COGNITIVE;
    event.data.expression.provider_code = "wink";
    qm_agent_emit(handle, &event);
    assert(qm_sem_wait(&notice_sem, 1000U) == QM_EOK);
    assert(notice_count == 3U);
    assert(last_notice_id == QM_AGENT_EVENT_EXPRESSION);
    assert(last_notice.data.expression.visual == QM_AGENT_EXPRESSION_UNKNOWN);
    assert(last_notice.data.expression.category ==
           QM_AGENT_EXPR_CATEGORY_SOCIAL_COGNITIVE);
    assert(strcmp(last_notice.data.expression.provider_code.text, "wink") == 0);

    event.id = QM_AGENT_EVENT_AUDIO;
    event.data.audio.data = (const uint8_t *)"ab";
    event.data.audio.len = 2U;
    qm_agent_emit(handle, &event);
    event.id = QM_AGENT_EVENT_VIDEO;
    event.data.video.data = (const uint8_t *)"cd";
    event.data.video.len = 2U;
    qm_agent_emit(handle, &event);
    assert(qm_sem_wait(&notice_sem, 20U) == -QM_ETIMEOUT);
    assert(notice_count == 3U);

    /* Hold one loop dispatch so the second value notice is provably queued
     * when the session is destroyed. Neither notice contains session memory. */
    event.id = QM_AGENT_EVENT_AGENT_TEXT;
    event.data.agent_text.text = "first queued notice";
    event.data.agent_text.final = QM_TRUE;
    block_notice_handler = QM_TRUE;
    qm_agent_emit(handle, &event);
    assert(qm_sem_wait(&notice_handler_entered, 1000U) == QM_EOK);
    event.data.agent_text.text = "second queued notice";
    qm_agent_emit(handle, &event);
    qm_agent_event_runtime_deinit(handle);
    assert(qm_agent_destroy(handle) == QM_EOK);
    assert(qm_sem_signal(&notice_handler_release) == QM_EOK);
    assert(qm_sem_wait(&notice_sem, 1000U) == QM_EOK);
    assert(qm_sem_wait(&notice_sem, 1000U) == QM_EOK);
    assert(notice_count == 5U);
    assert(last_notice.agent_id == agent_id);
    assert(strcmp(last_notice.data.text.text, "second queued notice") == 0);
    assert(qm_event_default_create() == -QM_EINIT);
    assert(qm_event_handler_unregister(QM_AGENT_EVENT_BASE,
                                       QM_EVENT_ANY_ID,
                                       on_public_notice,
                                       NULL) == QM_EOK);
    assert(qm_sem_free(&notice_handler_entered) == QM_EOK);
    assert(qm_sem_free(&notice_handler_release) == QM_EOK);
    assert(qm_sem_free(&notice_sem) == QM_EOK);
}

static unsigned uploaded;
static int admit_audio(void *ctx, const void *data, uint32_t len)
{
    assert(ctx != NULL && data != NULL && len == 2U);
    uploaded++;
    return -QM_EFULL;
}

static void *contended_upload(void *arg)
{
    assert(qm_agent_transport_send_audio(arg, "ab", 2U) == -QM_EBUSY);
    return NULL;
}

static void test_uplink_admission(void)
{
    qm_agent_handle_t handle = new_handle();
    qm_agent_transport_if_t transport = test_transport;
    pthread_t caller;
    transport.send_audio = admit_audio;
    handle->config.transport_if = &transport;
    handle->started = QM_TRUE;
    handle->network_forwarding = QM_TRUE;
    handle->audio.upload_enabled = QM_TRUE;
    handle->audio.transport_ready = QM_TRUE;
    assert(qm_agent_transport_send_audio(handle, "ab", 2U) == -QM_EFULL);
    assert(uploaded == 1U);
    handle->config.audio.capture_enabled = QM_TRUE;
    assert(qm_agent_send_audio(handle, "ab", 2U) == -QM_EBUSY);
    assert(uploaded == 1U);
    handle->config.audio.capture_enabled = QM_FALSE;
    assert(qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER) == QM_EOK);
    assert(pthread_create(&caller, NULL, contended_upload, handle) == 0);
    assert(pthread_join(caller, NULL) == 0);
    assert(qm_mutex_unlock(&handle->network_lock) == QM_EOK);
    handle->audio.capture_closing = QM_TRUE;
    assert(qm_agent_transport_send_audio(handle, "ab", 2U) == -QM_EBUSY);
    handle->audio.capture_closing = QM_FALSE;
    handle->network_forwarding = QM_FALSE;
    assert(qm_agent_transport_send_audio(handle, "ab", 2U) == -QM_EBUSY);
    assert(uploaded == 1U);
    assert(qm_agent_destroy(handle) == QM_EOK);
}

static int block_control(void *ctx, const char *text)
{
    (void)ctx; (void)text;
    qm_sem_signal(&blocked_control_entered);
    assert(qm_sem_wait(&blocked_control_release, 2000U) == QM_EOK);
    return QM_EOK;
}

static void *send_blocking_text(void *ctx)
{
    assert(qm_agent_send_text(ctx, "hold") == QM_EOK);
    return NULL;
}

static void test_audio_mailbox_under_backpressure(void)
{
    qm_agent_handle_t handle = new_handle();
    qm_agent_transport_if_t transport = test_transport;
    qm_audio_event_t event = {0};
    qm_agent_control_message_t command = {0};
    pthread_t caller;
    unsigned queued = 0U;
    const unsigned queue_limit = 128U;

    qm_sem_new(&blocked_control_entered, 0);
    qm_sem_new(&blocked_control_release, 0);
    qm_sem_new(&mailbox_terminal, 0);
    transport.send_text = block_control;
    handle->config.transport_if = &transport;
    handle->started = QM_TRUE;
    handle->state = QM_AGENT_STATE_RUNNING;
    observe_mailbox = QM_TRUE;
    mailbox_events = 0U;
    assert(pthread_create(&caller, NULL, send_blocking_text, handle) == 0);
    assert(qm_sem_wait(&blocked_control_entered, 2000U) == QM_EOK);
    while (qm_agent_control_post_module_event(handle,
               QM_AGENT_MODULE_EVENT_CLOUD_CONNECTED) == QM_EOK) {
        assert(++queued < queue_limit);
    }
    assert(queued != 0U);
    event.operation_id = 41U;
    event.type = QM_AUDIO_EVENT_STARTED;
    assert(qm_agent_control_post_audio_event(handle, &event) == QM_EOK);
    event.type = QM_AUDIO_EVENT_FINISHED;
    assert(qm_agent_control_post_audio_event(handle, &event) == QM_EOK);
    assert(qm_agent_control_schedule(handle, QM_AGENT_CONTROL_AUDIO_RECONCILE,
                                    0U, 0U, 0U) == QM_EOK);
    qm_sem_signal(&blocked_control_release);
    assert(qm_sem_wait(&mailbox_terminal, 2000U) == QM_EOK);
    pthread_join(caller, NULL);
    assert(mailbox_events == 2U && mailbox_order[0] == QM_AUDIO_EVENT_STARTED &&
           mailbox_order[1] == QM_AUDIO_EVENT_FINISHED);
    observe_mailbox = QM_FALSE;

    stale_begins = 0U;
    command.type = QM_AGENT_CONTROL_AUDIO_BEGIN;
    command.data.audio_begin.generation = handle->audio.stream_generation - 1U;
    assert(qm_agent_control_call(handle, &command) == QM_EOK);
    assert(stale_begins == 0U);
    assert(qm_agent_destroy(handle) == QM_EOK);
    qm_sem_free(&blocked_control_entered);
    qm_sem_free(&blocked_control_release);
    qm_sem_free(&mailbox_terminal);
}

static void test_resource_token_copy(void)
{
    struct qm_agent agent = {0};
    qm_agent_control_message_t command = {0};
    char copy[16] = "";

    command.type = QM_AGENT_CONTROL_COPY_RESOURCE_TOKEN;
    command.data.resource_token.buffer = copy;
    command.data.resource_token.capacity = sizeof(copy);
    assert(qm_agent_control_execute(&agent, &command) == -QM_ENOENT);
    agent.auth.has_auth = QM_TRUE;
    agent.auth.info.ljyd.token = "resource-token";
    assert(qm_agent_control_execute(&agent, &command) == QM_EOK);
    assert(strcmp(copy, "resource-token") == 0);
    command.data.resource_token.capacity = 2U;
    assert(qm_agent_control_execute(&agent, &command) == -QM_EFULL);
    assert(qm_agent_copy_resource_token(NULL, copy, sizeof(copy)) == -QM_EINVAL);
}

int main(void)
{
    test_resource_token_copy();
    test_start_and_retryable_stop();
    test_retryable_destroy();
    test_audio_mailbox_under_backpressure();
    test_tts_failure_generation();
    test_cancel_with_pending_audio_cleanup();
    test_synchronous_tts_completion();
    test_event_clone_validation();
    test_expression_classification();
    test_command_notice();
    test_public_event_loop();
    test_uplink_admission();
    puts("Agent Core lifecycle tests passed");
    return 0;
}

#endif /* QM_AGENT_TEST_REAL_AUDIO */
