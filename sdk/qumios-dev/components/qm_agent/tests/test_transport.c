/** @file test_transport.c @brief Transport formats and ordered TTS boundaries. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(CONFIG_QM_AGENT_TRANSPORT_JOYINSIDE)
#include "../src/transport/qm_agent_transport_joyinside.c"
#elif defined(CONFIG_QM_AGENT_TRANSPORT_LGEINE)
#include "../src/transport/qm_agent_transport_lgeine.c"
#elif defined(CONFIG_QM_AGENT_TRANSPORT_BAIDU)
#include "../src/transport/qm_agent_transport_baidu_chat_agent.c"
#elif defined(CONFIG_QM_AGENT_TRANSPORT_AGORA)
#include "../src/transport/qm_agent_transport_agora.c"
#endif

void *qm_memcpy(void *to, const void *from, uint32_t size) { return memcpy(to, from, size); }
void *qm_memset(void *data, int32_t value, uint32_t size) { return memset(data, value, size); }
int32_t qm_strcmp(const char *a, const char *b) { return strcmp(a, b); }
uint32_t qm_strlen(const char *data) { return (uint32_t)strlen(data); }
int32_t qm_strncmp(const char *a, const char *b, uint32_t len) { return strncmp(a, b, len); }
char *qm_strstr(const char *data, const char *match) { return strstr(data, match); }
void qm_free(void *data) { free(data); }

#if !defined(CONFIG_QM_AGENT_TRANSPORT_AGORA)
#define MAX_EVENTS 8U
static qm_agent_event_t events[MAX_EVENTS];
static unsigned event_count;

static void on_event(const qm_agent_event_t *event, void *ctx)
{
    (void)ctx;
    assert(event_count < MAX_EVENTS);
    events[event_count++] = *event;
}
#endif

static qm_agent_audio_format_t pcm_format(void)
{
    qm_agent_audio_format_t format = {0};
    format.codec = QM_AGENT_AUDIO_CODEC_PCM;
    format.data_type = QM_AGENT_AUDIO_CODEC_PCM;
    format.sample_rate = 16000U;
    format.channel_num = 1U;
    format.bits_per_sample = 16U;
    format.frame_ms = 20U;
    return format;
}

#if defined(CONFIG_QM_AGENT_TRANSPORT_JOYINSIDE)
static joyinside_client_config_t captured_config;
static joyinside_client_start_params_t captured_start;
static int client_cookie;
void *qm_malloc(unsigned int bytes) { return malloc(bytes); }
joyinside_client_handle_t joyinside_client_init(const joyinside_client_config_t *config)
{
    captured_config = *config;
    return (joyinside_client_handle_t)&client_cookie;
}
int joyinside_client_start(joyinside_client_handle_t client, const joyinside_client_start_params_t *params)
{
    assert(client == (joyinside_client_handle_t)&client_cookie);
    captured_start = *params;
    return QM_EOK;
}
int joyinside_client_destroy(joyinside_client_handle_t client)
{ assert(client == (joyinside_client_handle_t)&client_cookie); return QM_EOK; }
static int test_token(void *context, qm_bool refresh, char *token, uint32_t *length)
{ (void)context; (void)refresh; (void)token; (void)length; return -QM_ENOSYS; }

static void test_transport(void)
{
    qm_agent_audio_format_t format = pcm_format();
    qm_agent_joyinside_ctx_t ctx = {0};
    joyinside_client_event_t event = {0};

    assert(validate_audio(&format, QM_TRUE) == QM_EOK);
    format.codec = QM_AGENT_AUDIO_CODEC_OPUS;
    assert(validate_audio(&format, QM_TRUE) == -QM_ENOSYS);
    format.data_type = QM_AGENT_AUDIO_CODEC_OPUS;
    assert(validate_audio(&format, QM_FALSE) == -QM_ENOSYS);
    format.framing = QM_AGENT_AUDIO_FRAMING_OPUS_CBR;
    format.packet_bytes = 80U;
    assert(validate_audio(&format, QM_TRUE) == QM_EOK);
    assert(validate_audio(&format, QM_FALSE) == QM_EOK);
    format.channel_num = 2U;
    assert(validate_audio(&format, QM_FALSE) == -QM_EINVAL);

    ctx.event_cb = on_event;
    ctx.output_format = pcm_format();
    event.user_context = &ctx;
    event.event_id = JOYINSIDE_CLIENT_EVENT_SERVER_EVENT;
    event.payload.server_event.type = JOYINSIDE_SERVER_EVENT_TYPE_CALL_AGENT_START;
    joyinside_on_event(&event);
    assert(event_count == 1U && events[0].id == QM_AGENT_EVENT_TTS_INPUT_BEGIN);
    assert(events[0].data.audio.format.sample_rate == 16000U &&
           events[0].data.audio.data == NULL && events[0].data.audio.len == 0U);
    event_count = 0U;
    event.payload.server_event.type = JOYINSIDE_SERVER_EVENT_TYPE_TTS_COMPLETE;
    joyinside_on_event(&event);
    assert(event_count == 1U && events[0].id == QM_AGENT_EVENT_TTS_INPUT_COMPLETE);

    event_count = 0U;
    event.event_id = JOYINSIDE_CLIENT_EVENT_TEXT;
    event.payload.text.text_type = JOYINSIDE_CLIENT_TEXT_TYPE_AGENT;
    event.payload.text.content.agent.content = "stream chunk";
    event.payload.text.content.agent.finish_reason = "";
    joyinside_on_event(&event);
    assert(event_count == 1U && events[0].id == QM_AGENT_EVENT_AGENT_TEXT &&
           events[0].data.agent_text.final == QM_FALSE &&
           events[0].data.agent_text.finish_reason == QM_AGENT_TEXT_FINISH_REASON_NONE);
    event_count = 0U;
    event.payload.text.content.agent.finish_reason = "audit";
    joyinside_on_event(&event);
    assert(event_count == 1U && events[0].data.agent_text.final == QM_TRUE &&
           events[0].data.agent_text.finish_reason == QM_AGENT_TEXT_FINISH_REASON_AUDIT);

    event_count = 0U;
    event.payload.text.content.agent.finish_reason = "content_filter";
    joyinside_on_event(&event);
    assert(event_count == 1U && events[0].data.agent_text.final == QM_TRUE &&
           events[0].data.agent_text.finish_reason ==
               QM_AGENT_TEXT_FINISH_REASON_CONTENT_FILTER);

    event_count = 0U;
    event.event_id = JOYINSIDE_CLIENT_EVENT_SERVER_EVENT;
    event.payload.server_event.type = JOYINSIDE_SERVER_EVENT_TYPE_TTS_SENTENCE_START;
    event.payload.server_event.common.text = "subtitle";
    joyinside_on_event(&event);
    assert(event_count == 1U && events[0].id == QM_AGENT_EVENT_TTS &&
           strcmp(events[0].data.tts.text, "subtitle") == 0 &&
           events[0].data.tts.final == QM_FALSE);

    event_count = 0U;
    event.payload.server_event.type = JOYINSIDE_SERVER_EVENT_TYPE_CALL_SKILL;
    event.payload.server_event.content.skill.expression = JOYINSIDE_CLIENT_EXPRESSION_HAPPY;
    event.payload.server_event.content.skill.expression_code = "happy_1";
    event.payload.server_event.content.skill.tts_expression = JOYINSIDE_CLIENT_EXPRESSION_HAPPY;
    event.payload.server_event.content.skill.tts_expression_code = "happy";
    joyinside_on_event(&event);
    assert(event_count == 1U && events[0].id == QM_AGENT_EVENT_EXPRESSION &&
           events[0].data.expression.visual == QM_AGENT_EXPRESSION_HAPPY &&
           events[0].data.expression.gesture == QM_AGENT_GESTURE_NONE &&
           events[0].data.expression.category == QM_AGENT_EXPR_CATEGORY_AFFECT &&
           events[0].data.expression.speech_expression == QM_AGENT_EXPRESSION_HAPPY &&
           events[0].data.expression.speech_category ==
               QM_AGENT_EXPR_CATEGORY_AFFECT &&
           strcmp(events[0].data.expression.provider_code, "happy_1") == 0 &&
           strcmp(events[0].data.expression.provider_speech_code, "happy") == 0);

    event_count = 0U;
    event.payload.server_event.content.skill.expression = JOYINSIDE_CLIENT_EXPRESSION_NOD;
    event.payload.server_event.content.skill.expression_code = "nod_1";
    event.payload.server_event.content.skill.tts_expression = JOYINSIDE_CLIENT_EXPRESSION_UNKNOWN;
    event.payload.server_event.content.skill.tts_expression_code = NULL;
    joyinside_on_event(&event);
    assert(event_count == 1U && events[0].id == QM_AGENT_EVENT_EXPRESSION &&
           events[0].data.expression.visual == QM_AGENT_EXPRESSION_UNKNOWN &&
           events[0].data.expression.gesture == QM_AGENT_GESTURE_NOD &&
           events[0].data.expression.category == QM_AGENT_EXPR_CATEGORY_GESTURE &&
           events[0].data.expression.speech_expression == QM_AGENT_EXPRESSION_UNKNOWN &&
           strcmp(events[0].data.expression.provider_code, "nod_1") == 0 &&
           events[0].data.expression.provider_speech_code == NULL);

    event_count = 0U;
    event.payload.server_event.content.skill.expression = JOYINSIDE_CLIENT_EXPRESSION_UNKNOWN;
    event.payload.server_event.content.skill.expression_code = "future_1";
    joyinside_on_event(&event);
    assert(event_count == 1U && events[0].data.expression.visual ==
               QM_AGENT_EXPRESSION_UNKNOWN &&
           events[0].data.expression.gesture == QM_AGENT_GESTURE_NONE &&
           events[0].data.expression.category == QM_AGENT_EXPR_CATEGORY_UNKNOWN &&
           strcmp(events[0].data.expression.provider_code, "future_1") == 0);

    event_count = 0U;
    event.payload.server_event.content.skill.expression =
        JOYINSIDE_CLIENT_EXPRESSION_DISAGREE;
    event.payload.server_event.content.skill.expression_code = "disagree_1";
    joyinside_on_event(&event);
    assert(event_count == 1U && events[0].id == QM_AGENT_EVENT_EXPRESSION &&
           events[0].data.expression.visual == QM_AGENT_EXPRESSION_UNKNOWN &&
           events[0].data.expression.gesture == QM_AGENT_GESTURE_SHAKE_HEAD &&
           events[0].data.expression.category == QM_AGENT_EXPR_CATEGORY_GESTURE &&
           strcmp(events[0].data.expression.provider_code, "disagree_1") == 0);

    event_count = 0U;
    event.payload.server_event.content.skill.expression = JOYINSIDE_CLIENT_EXPRESSION_SICK;
    event.payload.server_event.content.skill.expression_code = "sick_1";
    joyinside_on_event(&event);
    assert(event_count == 1U && events[0].data.expression.visual == QM_AGENT_EXPRESSION_BORED &&
           events[0].data.expression.category == QM_AGENT_EXPR_CATEGORY_AFFECT);
    event_count = 0U;
    event.payload.server_event.content.skill.expression = JOYINSIDE_CLIENT_EXPRESSION_BLAMED;
    event.payload.server_event.content.skill.expression_code = "blamed_1";
    joyinside_on_event(&event);
    assert(event_count == 1U && events[0].data.expression.visual == QM_AGENT_EXPRESSION_ANGRY);

    /* A command and its expression must both survive the same envelope. */
    event_count = 0U;
    event.payload.server_event.content.skill.domain = "alarm";
    event.payload.server_event.content.skill.instruction_count = 2U;
    event.payload.server_event.chunk.data = "{\"domain\":\"alarm\",\"instructions\":[]}";
    joyinside_on_event(&event);
    assert(event_count == 2U && events[0].id == QM_AGENT_EVENT_COMMAND &&
           events[1].id == QM_AGENT_EVENT_EXPRESSION);
    assert(strcmp(events[0].data.command.domain, "alarm") == 0 &&
           events[0].data.command.json == event.payload.server_event.chunk.data);
    event_count = 0U;
    event.payload.server_event.content.skill.expression_code = NULL;
    joyinside_on_event(&event);
    assert(event_count == 1U && events[0].id == QM_AGENT_EVENT_COMMAND);

    event = (joyinside_client_event_t){0};
    event.user_context = &ctx;
    event.event_id = JOYINSIDE_CLIENT_EVENT_TEXT;
    event.payload.text.text_type = JOYINSIDE_CLIENT_TEXT_TYPE_ASR;
    event.payload.text.content.asr.text = "partial recognition";
    event.payload.text.content.asr.text_type = "IS_START";
    event_count = 0U;
    joyinside_on_event(&event);
    assert(event_count == 1U && events[0].id == QM_AGENT_EVENT_ASR && !events[0].data.asr.final);
    event.payload.text.content.asr.text_type = "IS_FINAL";
    event_count = 0U;
    joyinside_on_event(&event);
    assert(event_count == 1U && events[0].data.asr.final);

    qm_agent_transport_config_t config = {0};
    config.data.joyinside.bot_id = "bot";
    config.data.joyinside.uid = "user";
    config.data.joyinside.session_id = "session";
    config.data.joyinside.feature = "AUDIO_BOOK_V2";
    config.data.joyinside.client_type = "device";
    config.data.joyinside.location = "room";
    config.data.joyinside.need_asr_partial = QM_TRUE;
    config.data.joyinside.output_stream = QM_TRUE;
    config.data.joyinside.token_provider = test_token;
    config.data.joyinside.token_context = &config;
    qm_agent_audio_format_t input = pcm_format(), output = pcm_format();
    input.codec = input.data_type = QM_AGENT_AUDIO_CODEC_OPUS;
    input.framing = QM_AGENT_AUDIO_FRAMING_OPUS_CBR;
    input.frame_ms = 40U;
    input.packet_bytes = 80U;
    output.codec = output.data_type = QM_AGENT_AUDIO_CODEC_MP3;
    void *adapter = NULL;
    assert(joyinside_init(&adapter, &config, &input, &output, NULL, NULL, on_event, NULL) == QM_EOK);
    assert(captured_config.voice.input_codec == JOYINSIDE_CLIENT_AUDIO_CODEC_OPUS &&
           captured_config.voice.input_frame_size == 80U &&
           captured_config.voice.output_stream &&
           captured_config.voice.output_codec == JOYINSIDE_CLIENT_AUDIO_CODEC_MP3);
    assert(joyinside_start(adapter) == QM_EOK);
    assert(!strcmp(captured_start.bot_id, "bot") && !strcmp(captured_start.uid, "user") &&
           !strcmp(captured_start.session_id, "session") && !strcmp(captured_start.feature, "AUDIO_BOOK_V2") &&
           !strcmp(captured_start.client_type, "device") && !strcmp(captured_start.location, "room"));
    assert(captured_start.need_asr_partial && captured_start.token_provider == test_token &&
           captured_start.token_context == &config);
    assert(joyinside_deinit(adapter) == QM_EOK);
}
#elif defined(CONFIG_QM_AGENT_TRANSPORT_LGEINE)
int lgeine_agent_send_listen(lgeine_agent_handle_t handle,
                             lgeine_agent_listen_state_t state,
                             lgeine_agent_listen_mode_t mode,
                             const char *text, uint32_t timeout_ms)
{
    (void)handle; (void)state; (void)mode; (void)text; (void)timeout_ms;
    return QM_EOK;
}

static void test_transport(void)
{
    qm_agent_audio_format_t format = pcm_format();
    lgeine_agent_audio_info_t audio = {0};
    qm_agent_lgeine_ctx_t ctx = {0};
    lgeine_agent_event_t event = {0};

    assert(to_lgeine_audio(&format, &audio) == QM_EOK);
    format.codec = QM_AGENT_AUDIO_CODEC_OPUS;
    assert(to_lgeine_audio(&format, &audio) == -QM_ENOSYS);
    audio.format = LGEINE_AGENT_AUDIO_FORMAT_OPUS;
    assert(from_lgeine_audio(&audio, &format) == QM_EOK);
    assert(format.framing == QM_AGENT_AUDIO_FRAMING_OPUS_PACKET);
    assert(format.packet_bytes == 0U);

    ctx.event_cb = on_event;
    ctx.output_format = pcm_format();
    event.id = LGEINE_AGENT_EVENT_TTS;
    event.data.tts.state = LGEINE_AGENT_TTS_STATE_START;
    lgeine_on_event(NULL, &event, &ctx);
    assert(event_count == 3U && events[0].id == QM_AGENT_EVENT_TTS_INPUT_BEGIN &&
           events[1].id == QM_AGENT_EVENT_TTS && events[2].id == QM_AGENT_EVENT_AGENT_STATE);
    assert(events[0].data.audio.format.sample_rate == 16000U &&
           events[0].data.audio.data == NULL && events[0].data.audio.len == 0U);
    event_count = 0U;
    event.data.tts.state = LGEINE_AGENT_TTS_STATE_SENTENCE_START;
    lgeine_on_event(NULL, &event, &ctx);
    assert(event_count == 2U && events[1].id == QM_AGENT_EVENT_AGENT_STATE);
    event_count = 0U;
    event.data.tts.state = LGEINE_AGENT_TTS_STATE_STOP;
    lgeine_on_event(NULL, &event, &ctx);
    assert(event_count == 3U);
    assert(events[0].id == QM_AGENT_EVENT_TTS);
    assert(events[1].id == QM_AGENT_EVENT_TTS_INPUT_COMPLETE);
    assert(events[2].id == QM_AGENT_EVENT_AGENT_STATE);
}
#elif defined(CONFIG_QM_AGENT_TRANSPORT_BAIDU)
static int destroy_result;
static unsigned destroy_calls;

int baidu_chat_agent_destroy(baidu_chat_agent_handle_t handle)
{
    (void)handle;
    destroy_calls++;
    return destroy_result;
}

static void test_transport(void)
{
    qm_agent_audio_format_t input = pcm_format();
    qm_agent_audio_format_t output = pcm_format();
    qm_agent_baidu_chat_agent_ctx_t ctx = {0};
    baidu_chat_agent_text_t event = {0};

    assert(validate_audio(&input, &output) == QM_EOK);
    input.codec = QM_AGENT_AUDIO_CODEC_OPUS;
    assert(validate_audio(&input, &output) == -QM_ENOSYS);
    input = pcm_format();
    output.sample_rate = 8000U;
    assert(validate_audio(&input, &output) == -QM_EINVAL);

    ctx.event_cb = on_event;
    ctx.output_format = pcm_format();
    event.semantic = BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_TTS_BEGIN_SPEAKING;
    event.text = "[TTS_BEGIN_SPEAKING]";
    handle_event_text(&ctx, &event);
    assert(event_count == 2U && events[0].id == QM_AGENT_EVENT_TTS_INPUT_BEGIN &&
           events[1].id == QM_AGENT_EVENT_AGENT_STATE);
    assert(events[0].data.audio.format.sample_rate == 16000U &&
           events[0].data.audio.data == NULL && events[0].data.audio.len == 0U);
    event_count = 0U;
    event.semantic = 0;
    handle_event_text(&ctx, &event);
    assert(event_count == 2U && events[0].id == QM_AGENT_EVENT_TTS_INPUT_BEGIN);
    event_count = 0U;
    event.semantic = BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_TTS_END_SPEAKING;
    event.text = "[TTS_END_SPEAKING]";
    handle_event_text(&ctx, &event);
    assert(event_count == 2U && events[0].id == QM_AGENT_EVENT_TTS_INPUT_COMPLETE);
    event_count = 0U;
    event.semantic = 0;
    handle_event_text(&ctx, &event);
    assert(event_count == 2U && events[0].id == QM_AGENT_EVENT_TTS_INPUT_COMPLETE);
    event_count = 0U;
    event.semantic = BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_VOICE_DISAPPEAR;
    handle_event_text(&ctx, &event);
    assert(event_count == 1U && events[0].id == QM_AGENT_EVENT_AGENT_STATE);

    qm_agent_baidu_chat_agent_ctx_t *owned = calloc(1U, sizeof(*owned));
    assert(owned != NULL);
    owned->agent = (baidu_chat_agent_handle_t)owned;
    destroy_result = -QM_EBUSY;
    assert(baidu_chat_agent_transport_deinit(owned) == -QM_EBUSY);
    assert(owned->agent != NULL && destroy_calls == 1U);
    destroy_result = QM_EOK;
    assert(baidu_chat_agent_transport_deinit(owned) == QM_EOK);
    assert(destroy_calls == 2U);
}
#elif defined(CONFIG_QM_AGENT_TRANSPORT_AGORA)
static void test_transport(void)
{
    qm_agent_audio_format_t input = pcm_format();
    qm_agent_audio_format_t output;

    input.codec = QM_AGENT_AUDIO_CODEC_OPUS;
    assert(validate_uplink(&input) == QM_EOK);
    input.data_type = QM_AGENT_AUDIO_CODEC_OPUS;
    assert(validate_uplink(&input) == -QM_ENOSYS);
    input.data_type = QM_AGENT_AUDIO_CODEC_PCM;
    input.bits_per_sample = 8U;
    assert(validate_uplink(&input) == -QM_EINVAL);
    output = agora_audio_format_from_data_type(AUDIO_DATA_TYPE_OPUS, NULL);
    assert(output.framing == QM_AGENT_AUDIO_FRAMING_OPUS_PACKET);
    output = agora_audio_format_from_data_type(AUDIO_DATA_TYPE_PCM, &output);
    assert(output.framing == QM_AGENT_AUDIO_FRAMING_STREAM && output.packet_bytes == 0U);
}
#endif

int main(void)
{
    test_transport();
    puts("transport contracts passed");
    return 0;
}
