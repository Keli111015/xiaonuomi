#include "lgeine_agent.h"

#include "qm_kernel.h"
#include "qm_string.h"
#include "qm_websocket_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    qm_websocket_event_handler_t handler;
    void *handler_ctx;
    char headers[512];
    char last_text[1024];
    int text_send_count;
    int binary_send_count;
    int close_count;
    int reconnect_count;
} fake_ws_t;

typedef struct {
    int transport_connected;
    int ready;
    int disconnected;
    int audio;
    int stt;
    int llm;
    int tts;
    int abort;
    int errors;
    qm_bool stt_is_final;
    lgeine_agent_tts_state_t tts_state;
    lgeine_agent_tts_type_t tts_type;
    lgeine_agent_server_abort_reason_t abort_reason;
    lgeine_agent_error_type_t error_type;
    int error_code;
    uint32_t audio_len;
} test_events_t;

static fake_ws_t *g_ws;
static qm_websocket_client_config_t g_ws_config;

#define EXPECT(condition) \
    do { if (!(condition)) { \
        fprintf(stderr, "EXPECT failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        exit(1); \
    } } while (0)

void *qm_websocket_client_init(const qm_websocket_client_config_t *config)
{
    EXPECT(config != NULL);
    EXPECT(g_ws == NULL);
    g_ws = (fake_ws_t *)calloc(1, sizeof(*g_ws));
    EXPECT(g_ws != NULL);
    g_ws_config = *config;
    return g_ws;
}

int qm_websocket_client_append_header(void *client,
                                      const char *key,
                                      const char *value)
{
    fake_ws_t *ws = (fake_ws_t *)client;
    size_t used = strlen(ws->headers);
    int ret = snprintf(ws->headers + used, sizeof(ws->headers) - used,
                       "%s: %s\r\n", key, value);

    return ret > 0 && (size_t)ret < sizeof(ws->headers) - used ?
               QM_EOK : -QM_EFULL;
}

int qm_websocket_register_events(void *client,
                                 qm_websocket_event_id_t event_id,
                                 qm_websocket_event_handler_t handler,
                                 void *user_context)
{
    fake_ws_t *ws = (fake_ws_t *)client;

    EXPECT(event_id == WEBSOCKET_EVENT_MAX);
    ws->handler = handler;
    ws->handler_ctx = user_context;
    return QM_EOK;
}

int qm_websocket_client_start(void *client)
{
    fake_ws_t *ws = (fake_ws_t *)client;

    EXPECT(ws->handler != NULL);
    return QM_EOK;
}

int qm_websocket_client_send_text(void *client,
                                  const char *data,
                                  int len,
                                  uint32_t timeout_ms)
{
    fake_ws_t *ws = (fake_ws_t *)client;

    (void)timeout_ms;
    EXPECT(len >= 0 && (size_t)len < sizeof(ws->last_text));
    memcpy(ws->last_text, data, (size_t)len);
    ws->last_text[len] = '\0';
    ws->text_send_count++;
    return len;
}

int qm_websocket_client_send_bin(void *client,
                                 const char *data,
                                 int len,
                                 uint32_t timeout_ms)
{
    fake_ws_t *ws = (fake_ws_t *)client;

    (void)timeout_ms;
    EXPECT(data != NULL && len > 0);
    ws->binary_send_count++;
    return len;
}

int qm_websocket_client_close(void *client, uint32_t timeout_ms)
{
    fake_ws_t *ws = (fake_ws_t *)client;

    (void)timeout_ms;
    ws->close_count++;
    return QM_EOK;
}

int qm_websocket_client_close_with_code(void *client, int code,
                                        const char *data, int len,
                                        uint32_t timeout_ms)
{
    (void)code;
    (void)data;
    (void)len;
    return qm_websocket_client_close(client, timeout_ms);
}

int qm_websocket_client_reconnect(void *client)
{
    fake_ws_t *ws = (fake_ws_t *)client;

    EXPECT(ws == g_ws);
    ws->reconnect_count++;
    return QM_EOK;
}

int qm_websocket_client_destroy(void *client)
{
    EXPECT(client == g_ws);
    free(g_ws);
    g_ws = NULL;
    return QM_EOK;
}

static void dispatch_ws_ex(qm_websocket_event_id_t event_id,
                           const char *data,
                           int len,
                           int opcode,
                           qm_bool final,
                           int payload_len,
                           int payload_offset)
{
    qm_websocket_event_data_t event;

    EXPECT(g_ws != NULL && g_ws->handler != NULL);
    memset(&event, 0, sizeof(event));
    event.data_ptr = data;
    event.data_len = len;
    event.opcode = opcode;
    event.fin = final;
    event.payload_len = payload_len;
    event.payload_offset = payload_offset;
    g_ws->handler(event_id, &event, g_ws->handler_ctx);
}

static void dispatch_ws(qm_websocket_event_id_t event_id,
                        const char *data,
                        int len,
                        int opcode,
                        qm_bool final)
{
    dispatch_ws_ex(event_id, data, len, opcode, final, len, 0);
}

static void on_event(lgeine_agent_handle_t agent,
                     const lgeine_agent_event_t *event,
                     void *user_ctx)
{
    test_events_t *events = (test_events_t *)user_ctx;

    (void)agent;
    switch (event->id) {
        case LGEINE_AGENT_EVENT_TRANSPORT_CONNECTED:
            events->transport_connected++;
            break;
        case LGEINE_AGENT_EVENT_READY:
            events->ready++;
            EXPECT(strcmp(event->session_id, "550e8400-e29b-41d4-a716-446655440000") == 0);
            EXPECT(event->data.ready.audio_info.rate == 16000U);
            EXPECT(event->data.ready.audio_info.format == LGEINE_AGENT_AUDIO_FORMAT_OPUS);
            break;
        case LGEINE_AGENT_EVENT_DISCONNECTED:
            events->disconnected++;
            break;
        case LGEINE_AGENT_EVENT_AUDIO:
            events->audio++;
            events->audio_len = event->data.audio.data_len;
            break;
        case LGEINE_AGENT_EVENT_STT:
            events->stt++;
            events->stt_is_final = event->data.stt.is_final;
            break;
        case LGEINE_AGENT_EVENT_LLM:
            events->llm++;
            break;
        case LGEINE_AGENT_EVENT_TTS:
            events->tts++;
            events->tts_state = event->data.tts.state;
            events->tts_type = event->data.tts.type;
            break;
        case LGEINE_AGENT_EVENT_ABORT:
            events->abort++;
            events->abort_reason = event->data.abort.reason;
            break;
        case LGEINE_AGENT_EVENT_ERROR:
            events->errors++;
            events->error_type = event->data.error.type;
            events->error_code = event->data.error.code;
            break;
        default:
            break;
    }
}

static void dispatch_text(const char *json)
{
    dispatch_ws(WEBSOCKET_EVENT_DATA, json, (int)strlen(json), 1, QM_TRUE);
}

int main(void)
{
    test_events_t events = {0};
    lgeine_agent_init_config_t init = {
        .config = {
            .hello_timeout_ms = 5000U,
            .max_text_message_size = 2048U,
        },
        .event_cb = on_event,
        .user_ctx = &events,
    };
    lgeine_agent_start_params_t start = {
        .uri = "wss://example.invalid/voice",
        .pid = "11257",
        .sn = "94A990046EF2",
        .access_token = "token123",
        .audio_info = {
            .format = LGEINE_AGENT_AUDIO_FORMAT_OPUS,
            .rate = 16000U,
            .bits = 16U,
            .channel = 1U,
            .frame_duration_ms = 60U,
        },
    };
    const char *hello_reply =
        "{\"method\":\"helloReply\",\"sessionId\":\"550e8400-e29b-41d4-a716-446655440000\","
        "\"code\":0,\"audioInfo\":{\"format\":\"opus\",\"rate\":16000,"
        "\"bits\":16,\"channel\":1,\"frameDuration\":60}}";
    const uint8_t audio[] = {1U, 2U, 3U};
    const uint8_t audio_a[] = {1U, 2U};
    const uint8_t audio_b[] = {3U};
    lgeine_agent_handle_t agent = lgeine_agent_init(&init);

    EXPECT(agent != NULL);
    start.pid = NULL;
    EXPECT(lgeine_agent_start(agent, &start) == -QM_EINVAL);
    start.pid = "11257";
    EXPECT(lgeine_agent_start(agent, &start) == QM_EOK);
    EXPECT(strcmp(g_ws_config.uri, start.uri) == 0);
    EXPECT(strstr(g_ws->headers, "pid: 11257\r\n") != NULL);
    EXPECT(strstr(g_ws->headers, "sn: 94A990046EF2\r\n") != NULL);
    EXPECT(strstr(g_ws->headers, "Authorization: Bearer token123\r\n") != NULL);

    dispatch_ws(WEBSOCKET_EVENT_CONNECTED, NULL, 0, 0, QM_TRUE);
    EXPECT(events.transport_connected == 1);
    EXPECT(strstr(g_ws->last_text, "\"method\":\"hello\"") != NULL);
    EXPECT(strstr(g_ws->last_text, "\"format\":\"opus\"") != NULL);
    EXPECT(lgeine_agent_send_welcome(agent, 100U) == -QM_EBUSY);

    dispatch_text(hello_reply);
    EXPECT(events.ready == 1);
    EXPECT(lgeine_agent_is_ready(agent) == QM_TRUE);

    /* Downlink binary audio is accepted only inside a TTS sentence. */
    dispatch_ws(WEBSOCKET_EVENT_DATA, (const char *)audio, sizeof(audio),
                2, QM_TRUE);
    EXPECT(events.audio == 0);

    EXPECT(lgeine_agent_send_welcome(agent, 100U) == QM_EOK);
    EXPECT(strstr(g_ws->last_text, "\"method\":\"welcome\"") != NULL);
    EXPECT(strstr(g_ws->last_text, "\"sessionId\":\"550e8400-e29b-41d4-a716-446655440000\"") != NULL);

    EXPECT(lgeine_agent_send_listen(agent, LGEINE_AGENT_LISTEN_STATE_START,
                                    LGEINE_AGENT_LISTEN_MODE_REALTIME, NULL,
                                    100U) == QM_EOK);
    EXPECT(strstr(g_ws->last_text, "\"state\":\"start\"") != NULL);
    EXPECT(strstr(g_ws->last_text, "\"mode\":\"realtime\"") != NULL);
    EXPECT(lgeine_agent_send_listen(agent, LGEINE_AGENT_LISTEN_STATE_DETECT,
                                    LGEINE_AGENT_LISTEN_MODE_AUTO, NULL,
                                    100U) == -QM_EINVAL);
    EXPECT(lgeine_agent_send_listen(agent, LGEINE_AGENT_LISTEN_STATE_DETECT,
                                    LGEINE_AGENT_LISTEN_MODE_AUTO, "", 100U) == -QM_EINVAL);
    EXPECT(lgeine_agent_send_listen(agent, LGEINE_AGENT_LISTEN_STATE_START,
                                    LGEINE_AGENT_LISTEN_MODE_AUTO, "wake", 100U) == -QM_EINVAL);
    EXPECT(lgeine_agent_send_audio(agent, audio, sizeof(audio), 100U) == QM_EOK);
    EXPECT(g_ws->binary_send_count == 1);

    EXPECT(lgeine_agent_send_speak(agent, "hello", 100U) == QM_EOK);
    EXPECT(strstr(g_ws->last_text, "\"method\":\"speak\"") != NULL);
    EXPECT(strstr(g_ws->last_text, "\"text\":\"hello\"") != NULL);

    EXPECT(lgeine_agent_send_abort(agent, LGEINE_AGENT_ABORT_REASON_USER_STOP,
                                   100U) == QM_EOK);
    EXPECT(strstr(g_ws->last_text, "\"reason\":\"user_stop\"") != NULL);

    dispatch_text("{\"method\":\"stt\",\"sessionId\":\"550e8400-e29b-41d4-a716-446655440000\","
                  "\"text\":\"hi\",\"isFinal\":true}");
    EXPECT(events.stt == 0);
    EXPECT(lgeine_agent_send_listen(agent, LGEINE_AGENT_LISTEN_STATE_START,
                                    LGEINE_AGENT_LISTEN_MODE_REALTIME, NULL,
                                    100U) == QM_EOK);
    dispatch_text("{\"method\":\"stt\",\"sessionId\":\"550e8400-e29b-41d4-a716-446655440000\","
                  "\"text\":\"hi\",\"isFinal\":true}");
    EXPECT(events.stt == 1);
    EXPECT(events.stt_is_final == QM_TRUE);

    /* detect opens the listen interaction (stt follows) but not the recording gate. */
    EXPECT(lgeine_agent_send_listen(agent, LGEINE_AGENT_LISTEN_STATE_DETECT,
                                    LGEINE_AGENT_LISTEN_MODE_AUTO, "HiLmeine", 100U) == QM_EOK);
    EXPECT(strstr(g_ws->last_text, "\"state\":\"detect\"") != NULL);
    EXPECT(strstr(g_ws->last_text, "\"text\":\"HiLmeine\"") != NULL);
    EXPECT(lgeine_agent_send_audio(agent, audio, sizeof(audio), 100U) == -QM_EBUSY);
    dispatch_text("{\"method\":\"stt\",\"sessionId\":\"550e8400-e29b-41d4-a716-446655440000\","
                  "\"text\":\"wake\",\"isFinal\":true}");
    EXPECT(events.stt == 2);

    /* listen.stop closes the recording gate but keeps the interaction open for
     * the final recognition result. */
    EXPECT(lgeine_agent_send_listen(agent, LGEINE_AGENT_LISTEN_STATE_START,
                                    LGEINE_AGENT_LISTEN_MODE_MANUAL, NULL, 100U) == QM_EOK);
    EXPECT(lgeine_agent_send_listen(agent, LGEINE_AGENT_LISTEN_STATE_STOP,
                                    LGEINE_AGENT_LISTEN_MODE_MANUAL, NULL, 100U) == QM_EOK);
    EXPECT(lgeine_agent_send_audio(agent, audio, sizeof(audio), 100U) == -QM_EBUSY);
    dispatch_text("{\"method\":\"stt\",\"sessionId\":\"550e8400-e29b-41d4-a716-446655440000\","
                  "\"text\":\"final after stop\",\"isFinal\":true}");
    EXPECT(events.stt == 3);

    dispatch_text("{\"method\":\"llm\",\"sessionId\":\"550e8400-e29b-41d4-a716-446655440000\","
                  "\"emotion\":\"happy\"}");
    EXPECT(events.llm == 1);

    dispatch_text("{\"method\":\"tts\",\"sessionId\":\"550e8400-e29b-41d4-a716-446655440000\","
                  "\"state\":\"start\",\"type\":\"chat\"}");
    EXPECT(events.tts == 1);
    EXPECT(events.tts_state == LGEINE_AGENT_TTS_STATE_START);
    EXPECT(events.tts_type == LGEINE_AGENT_TTS_TYPE_CHAT);

    dispatch_text("{\"method\":\"tts\",\"sessionId\":\"550e8400-e29b-41d4-a716-446655440000\","
                  "\"state\":\"sentence_start\",\"type\":\"chat\",\"text\":\"ok\"}");
    EXPECT(events.tts == 2);
    EXPECT(events.tts_state == LGEINE_AGENT_TTS_STATE_SENTENCE_START);

    /* Stale session and invalid business message must be dropped, not errored. */
    dispatch_text("{\"method\":\"stt\",\"sessionId\":\"stale\","
                  "\"text\":\"ignored\",\"isFinal\":true}");
    EXPECT(events.stt == 3);
    EXPECT(events.errors == 0);

    dispatch_ws_ex(WEBSOCKET_EVENT_DATA, (const char *)audio_a, sizeof(audio_a),
                   2, QM_FALSE, sizeof(audio), 0);
    dispatch_ws_ex(WEBSOCKET_EVENT_DATA, (const char *)audio_b, sizeof(audio_b),
                   2, QM_TRUE, sizeof(audio), sizeof(audio_a));
    EXPECT(events.audio == 1 && events.audio_len == sizeof(audio));
    dispatch_text("{\"method\":\"tts\",\"sessionId\":\"550e8400-e29b-41d4-a716-446655440000\","
                  "\"state\":\"sentence_end\",\"type\":\"chat\"}");
    dispatch_text("{\"method\":\"tts\",\"sessionId\":\"550e8400-e29b-41d4-a716-446655440000\","
                  "\"state\":\"stop\",\"type\":\"chat\"}");

    dispatch_text("{\"method\":\"abort\",\"sessionId\":\"550e8400-e29b-41d4-a716-446655440000\","
                  "\"reason\":\"server_policy\"}");
    EXPECT(events.abort == 1);
    EXPECT(events.abort_reason == LGEINE_AGENT_SERVER_ABORT_SERVER_POLICY);

    dispatch_text("{\"method\":\"abort\",\"sessionId\":\"550e8400-e29b-41d4-a716-446655440000\","
                  "\"reason\":\"session_end\"}");
    EXPECT(events.abort == 2);
    EXPECT(events.abort_reason == LGEINE_AGENT_SERVER_ABORT_SESSION_END);
    EXPECT(lgeine_agent_send_audio(agent, audio, sizeof(audio), 100U) == -QM_EBUSY);

    EXPECT(lgeine_agent_destroy(agent) == QM_EOK);
    EXPECT(g_ws == NULL);

    {
        test_events_t reject_events = {0};
        lgeine_agent_init_config_t reject_init = {
            .config = {
                .hello_timeout_ms = 5000U,
                .max_text_message_size = 2048U,
            },
            .event_cb = on_event,
            .user_ctx = &reject_events,
        };
        const char *reject_reply =
            "{\"method\":\"helloReply\",\"code\":4002,"
            "\"message\":\"no compatible audio format\"}";

        agent = lgeine_agent_init(&reject_init);
        EXPECT(agent != NULL);
        EXPECT(lgeine_agent_start(agent, &start) == QM_EOK);
        dispatch_ws(WEBSOCKET_EVENT_CONNECTED, NULL, 0, 0, QM_TRUE);
        dispatch_text(reject_reply);
        EXPECT(reject_events.errors == 1);
        EXPECT(reject_events.error_type == LGEINE_AGENT_ERROR_REJECTED);
        EXPECT(reject_events.error_code == 4002);
        EXPECT(reject_events.ready == 0);
        EXPECT(g_ws->reconnect_count == 1);
        EXPECT(lgeine_agent_destroy(agent) == QM_EOK);
        EXPECT(g_ws == NULL);
    }

    /* Uplink PCM frame length must match rate*bits/8*channel*frameDuration/1000;
     * an off-by-one frame is rejected without touching the transport. */
    {
        test_events_t pcm_events = {0};
        lgeine_agent_init_config_t pcm_init = {
            .config = {
                .hello_timeout_ms = 5000U,
                .max_text_message_size = 2048U,
            },
            .event_cb = on_event,
            .user_ctx = &pcm_events,
        };
        lgeine_agent_start_params_t pcm_start = {
            .uri = "wss://example.invalid/voice",
            .pid = "11257",
            .sn = "94A990046EF2",
            .access_token = "token123",
            .audio_info = {
                .format = LGEINE_AGENT_AUDIO_FORMAT_PCM,
                .rate = 16000U,
                .bits = 16U,
                .channel = 1U,
                .frame_duration_ms = 20U,
            },
        };
        /* 16000 * 16 / 8 * 1 * 20 / 1000 = 640 bytes per frame. */
        uint8_t *pcm = (uint8_t *)malloc(640);

        EXPECT(pcm != NULL);
        agent = lgeine_agent_init(&pcm_init);
        EXPECT(agent != NULL);
        EXPECT(lgeine_agent_start(agent, &pcm_start) == QM_EOK);
        dispatch_ws(WEBSOCKET_EVENT_CONNECTED, NULL, 0, 0, QM_TRUE);
        dispatch_text(hello_reply);
        EXPECT(pcm_events.ready == 1);
        EXPECT(lgeine_agent_send_listen(agent, LGEINE_AGENT_LISTEN_STATE_START,
                                        LGEINE_AGENT_LISTEN_MODE_REALTIME, NULL,
                                        100U) == QM_EOK);
        EXPECT(lgeine_agent_send_audio(agent, pcm, 640, 100U) == QM_EOK);
        EXPECT(g_ws->binary_send_count == 1);
        EXPECT(lgeine_agent_send_audio(agent, pcm, 641, 100U) == -QM_EINVAL);
        EXPECT(g_ws->binary_send_count == 1);
        EXPECT(lgeine_agent_send_audio(agent, pcm, 0, 100U) == -QM_EINVAL);
        free(pcm);
        EXPECT(lgeine_agent_destroy(agent) == QM_EOK);
        EXPECT(g_ws == NULL);
    }

    /* The endpoint is built from start_params.env when start_params.uri is
     * NULL; an explicit uri always takes precedence. */
    {
        test_events_t env_events = {0};
        lgeine_agent_init_config_t env_init = {
            .config = {
                .hello_timeout_ms = 5000U,
                .max_text_message_size = 2048U,
            },
            .event_cb = on_event,
            .user_ctx = &env_events,
        };
        lgeine_agent_start_params_t env_start = {
            .uri = NULL,
            .pid = "11257",
            .sn = "94A990046EF2",
            .access_token = "token123",
            .audio_info = {
                .format = LGEINE_AGENT_AUDIO_FORMAT_OPUS,
                .rate = 16000U,
                .bits = 16U,
                .channel = 1U,
                .frame_duration_ms = 60U,
            },
        };

        env_start.env = LGEINE_AGENT_ENV_DEV;
        agent = lgeine_agent_init(&env_init);
        EXPECT(agent != NULL);
        EXPECT(lgeine_agent_start(agent, &env_start) == QM_EOK);
        EXPECT(strcmp(g_ws_config.uri,
                      "wss://voice-dev.ljyd.cn/agent-voicechat/ljyd/v1") == 0);
        EXPECT(lgeine_agent_destroy(agent) == QM_EOK);
        EXPECT(g_ws == NULL);

        env_start.env = LGEINE_AGENT_ENV_TEST;
        agent = lgeine_agent_init(&env_init);
        EXPECT(agent != NULL);
        EXPECT(lgeine_agent_start(agent, &env_start) == QM_EOK);
        EXPECT(strcmp(g_ws_config.uri,
                      "wss://voice-test.ljyd.cn/agent-voicechat/ljyd/v1") == 0);
        EXPECT(lgeine_agent_destroy(agent) == QM_EOK);
        EXPECT(g_ws == NULL);

        env_start.env = LGEINE_AGENT_ENV_PROD;
        agent = lgeine_agent_init(&env_init);
        EXPECT(agent != NULL);
        EXPECT(lgeine_agent_start(agent, &env_start) == QM_EOK);
        EXPECT(strcmp(g_ws_config.uri,
                      "wss://voice.ljyd.cn/agent-voicechat/ljyd/v1") == 0);
        EXPECT(lgeine_agent_destroy(agent) == QM_EOK);
        EXPECT(g_ws == NULL);

        /* An explicit uri overrides the selected environment. */
        env_start.uri = "wss://example.invalid/override";
        agent = lgeine_agent_init(&env_init);
        EXPECT(agent != NULL);
        EXPECT(lgeine_agent_start(agent, &env_start) == QM_EOK);
        EXPECT(strcmp(g_ws_config.uri, env_start.uri) == 0);
        EXPECT(lgeine_agent_destroy(agent) == QM_EOK);
        EXPECT(g_ws == NULL);
    }

    puts("lgeine_agent_test: PASS");
    return 0;
}
