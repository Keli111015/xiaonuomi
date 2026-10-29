#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_support.h"
#include "joyinside_client_auth.h"
#include "joyinside_client_media.h"
#include "joyinside_client_vision.h"
#include <math.h>
#include "../src/joyinside_client.c"

static unsigned agent_messages, audio_bytes, errors, ready, tts_sentences, expression_events, voice_updates;
static joyinside_client_expression_t last_expression, last_tts_expression;
static char last_tts_sentence[64], last_expression_code[64], last_tts_expression_code[64], last_skill_message[64];

static void copy_test_text(char *destination, uint32_t length, const char *source)
{
    snprintf(destination, length, "%s", source ? source : "");
}

static void receive_event(const joyinside_client_event_t *event)
{
    if (event->event_id == JOYINSIDE_CLIENT_EVENT_ERROR) ++errors;
    if (event->event_id == JOYINSIDE_CLIENT_EVENT_CONNECTED) ++ready;
    if (event->event_id == JOYINSIDE_CLIENT_EVENT_VOICE_UPDATED) ++voice_updates;
    if (event->event_id == JOYINSIDE_CLIENT_EVENT_AUDIO) audio_bytes += event->payload.audio.chunk.data_len;
    if (event->event_id == JOYINSIDE_CLIENT_EVENT_TEXT &&
        event->payload.text.text_type == JOYINSIDE_CLIENT_TEXT_TYPE_AGENT) {
        assert(event->payload.text.chunk.current_data_offset == 0U);
        assert(event->payload.text.chunk.total_data_len == event->payload.text.chunk.data_len);
        ++agent_messages;
    }
    if (event->event_id == JOYINSIDE_CLIENT_EVENT_SERVER_EVENT) {
        if (event->payload.server_event.type == JOYINSIDE_SERVER_EVENT_TYPE_TTS_SENTENCE_START) {
            copy_test_text(last_tts_sentence, sizeof(last_tts_sentence),
                           event->payload.server_event.common.text);
            ++tts_sentences;
        } else if (event->payload.server_event.type == JOYINSIDE_SERVER_EVENT_TYPE_CALL_SKILL) {
            last_expression = event->payload.server_event.content.skill.expression;
            copy_test_text(last_expression_code, sizeof(last_expression_code),
                           event->payload.server_event.content.skill.expression_code);
            copy_test_text(last_tts_expression_code, sizeof(last_tts_expression_code),
                           event->payload.server_event.content.skill.tts_expression_code);
            last_tts_expression = event->payload.server_event.content.skill.tts_expression;
            copy_test_text(last_skill_message, sizeof(last_skill_message),
                           event->payload.server_event.content.skill.message);
            ++expression_events;
        }
    }
}

static joyinside_client_handle_t new_voice_client(const joyinside_client_voice_config_t *voice)
{
    joyinside_client_config_t config = {0};
    config.include_uid_in_messages = QM_TRUE;
    config.on_event = receive_event;
    config.buffer.max_text_size = 8192U;
    config.voice.input_codec = JOYINSIDE_CLIENT_AUDIO_CODEC_PCM;
    config.voice.output_codec = JOYINSIDE_CLIENT_AUDIO_CODEC_PCM;
    config.voice.input_sample_rate = 16000U;
    config.voice.output_sample_rate = 16000U;
    config.heartbeat.disabled = QM_TRUE;
    if (voice) config.voice = *voice;
    joyinside_client_handle_t client = joyinside_client_init(&config);
    assert(client);
    assert(client->config.include_uid_in_messages);
    return client;
}

static joyinside_client_handle_t new_client(void)
{ return new_voice_client(NULL); }

static void feed(joyinside_client_handle_t client, const char *data, int len,
                  int offset, int frame_len, qm_bool fin)
{
    qm_websocket_event_data_t event = {0};
    event.opcode = 1;
    event.data_ptr = data;
    event.data_len = len;
    event.payload_offset = offset;
    event.payload_len = frame_len;
    event.fin = fin;
    websocket_event_handler(WEBSOCKET_EVENT_DATA, &event, client);
}

static void feed_json(joyinside_client_handle_t client, const char *json)
{ feed(client, json, (int)strlen(json), 0, (int)strlen(json), QM_TRUE); }

static char *large_message(const char *type, const char *field, unsigned size)
{
    char *text = malloc(size + 1U);
    assert(text);
    memset(text, 'A', size); text[size] = 0;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "code", 200);
    cJSON_AddStringToObject(root, "contentType", type);
    cJSON *content = cJSON_AddObjectToObject(root, "content");
    cJSON_AddStringToObject(content, field, text);
    cJSON_AddStringToObject(content, "audioAue", "pcm");
    char *wire = cJSON_PrintUnformatted(root);
    cJSON_Delete(root); free(text);
    assert(wire);
    return wire;
}

static void test_receive(void)
{
    joyinside_client_handle_t client = new_client();
    char *wire = large_message("AGENT", "content", 5000U);
    int len = (int)strlen(wire);
    for (int chunk = 1; chunk <= 4096; chunk *= 4) {
        unsigned before = agent_messages;
        for (int offset = 0; offset < len;) {
            int n = len - offset < chunk ? len - offset : chunk;
            feed(client, wire + offset, n, offset, len, offset + n == len);
            offset += n;
        }
        assert(agent_messages == before + 1U);
    }
    unsigned before = agent_messages;
    /* Two WS frames, the second split into RX chunks, then an empty final continuation. */
    feed(client, wire, 1000, 0, 1000, QM_FALSE);
    feed(client, wire + 1000, 2000, 0, len - 1000, QM_FALSE);
    feed(client, wire + 3000, len - 3000, 2000, len - 1000, QM_FALSE);
    assert(agent_messages == before);
    feed(client, NULL, 0, 0, 0, QM_TRUE);
    assert(agent_messages == before + 1U);
    unsigned error_before = errors;
    feed(client, wire, 1000, 0, len, QM_FALSE);
    feed(client, wire + 1500, len - 1500, 1500, len, QM_TRUE);
    assert(errors == error_before + 1U);
    feed_json(client, wire);
    assert(agent_messages == before + 2U);
    feed(client, wire, 1000, 0, len, QM_FALSE);
    websocket_event_handler(WEBSOCKET_EVENT_DISCONNECTED, NULL, client);
    feed_json(client, wire);
    assert(agent_messages == before + 3U);
    cJSON_free(wire);
    wire = large_message("AGENT", "content", 9000U);
    error_before = errors;
    feed_json(client, wire);
    assert(errors == error_before + 1U);
    assert(client->rx_text_len == 0U && client->rx_text_capacity <= 8193U);
    cJSON_free(wire);
#if !CONFIG_JOYINSIDE_CLIENT_AUDIO_BINARY
    client->connected = QM_TRUE;
    client->output_binary = QM_FALSE;
    wire = large_message("TTS", "audioBase64", 5120U);
    len = (int)strlen(wire);
    unsigned bytes_before = audio_bytes;
    feed(client, wire, 4096, 0, len, QM_FALSE);
    feed(client, wire + 4096, len - 4096, 4096, len, QM_TRUE);
    assert(audio_bytes == bytes_before + 3840U);
    cJSON_free(wire);
#endif
    error_before = errors;
    feed_json(client, "[1,2,3]");
    feed_json(client, "{\"code\":200}garbage");
    assert(errors == error_before + 2U);
    assert(joyinside_client_destroy(client) == QM_EOK);
}

static void test_server_event_payloads(void)
{
    joyinside_client_handle_t client = new_client();
    unsigned sentence_before = tts_sentences;
    unsigned expression_before = expression_events;

    feed_json(client, "{\"code\":200,\"contentType\":\"EVENT\",\"content\":{"
              "\"eventType\":\"TTS_SENTENCE_START\",\"eventData\":{\"text\":\"subtitle\"}}}");
    assert(tts_sentences == sentence_before + 1U);
    assert(strcmp(last_tts_sentence, "subtitle") == 0);

    feed_json(client, "{\"code\":200,\"contentType\":\"EVENT\",\"content\":{"
              "\"eventType\":\"CALL_SKILL_EVENT\",\"eventData\":{"
              "\"msg\":\"expression event\",\"data\":{\"intentClassify\":\"chat\","
              "\"args\":{\"EMOTION\":\"happy_1\",\"TTS_EMOTION\":\"happy\"}}}}}");
    assert(expression_events == expression_before + 1U);
    assert(last_expression == JOYINSIDE_CLIENT_EXPRESSION_HAPPY);
    assert(last_tts_expression == JOYINSIDE_CLIENT_EXPRESSION_HAPPY);
    assert(strcmp(last_expression_code, "happy_1") == 0);
    assert(strcmp(last_tts_expression_code, "happy") == 0);
    assert(strcmp(last_skill_message, "expression event") == 0);

    assert(joyinside_client_destroy(client) == QM_EOK);
}

static unsigned token_calls;
static qm_bool force_seen;
static int token_provider(void *ctx, qm_bool force, char *token, uint32_t *len)
{
    (void)ctx;
    force_seen = force;
    assert(*len >= 32U);
    int n = snprintf(token, *len, "token-%u", ++token_calls);
    *len = (uint32_t)n;
    return QM_EOK;
}

static void voice_ack(joyinside_client_handle_t client, qm_bool correct)
{
    char json[512];
    snprintf(json, sizeof(json), "{\"code\":200,\"contentType\":\"EVENT\",\"content\":{"
        "\"eventType\":\"SERVER_VOICE_CHAT_UPDATED\",\"eventData\":{\"audio\":{"
        "\"binary\":%s,\"input\":{\"codec\":\"pcm\",\"sampleRate\":\"16000\"},"
        "\"output\":{\"codec\":\"pcm\",\"sampleRate\":%u}}}}}",
        CONFIG_JOYINSIDE_CLIENT_AUDIO_BINARY ? "true" : "false", correct ? 16000U : 24000U);
    feed_json(client, json);
}

static void confirm_last_voice(joyinside_client_handle_t client);

static void test_connection(void)
{
    joyinside_client_handle_t client = new_client();
    joyinside_client_start_params_t missing_uid = {.bot_id = "bot", .access_token = "token"};
    assert(set_start_state(client, &missing_uid) == -QM_EINVAL);
    joyinside_client_start_params_t params = {.bot_id = "bot id", .uid = "bound-user",
        .session_id = "stable-session", .feature = "AUDIO_BOOK_V2", .token_provider = token_provider};
    assert(set_start_state(client, &params) == QM_EOK);
    assert(prepare_connection(NULL, client) == QM_EOK);
    char first_request[64]; snprintf(first_request, sizeof(first_request), "%s", client->request_id);
    assert(strstr(test_ws_uri, "botId=bot%20id"));
    assert(strstr(test_ws_uri, "feature=AUDIO_BOOK_V2"));
    client->force_token_refresh = QM_TRUE;
    assert(prepare_connection(NULL, client) == QM_EOK);
    assert(strcmp(first_request, client->request_id) != 0 && force_seen);
    assert(strcmp(client->session_id, "stable-session") == 0);
    assert(strstr(test_ws_headers, "Bearer token-2"));

    qm_websocket_client_config_t ws_config = {.disable_auto_reconnect = QM_TRUE};
    client->ws = qm_websocket_client_init(&ws_config);
    qm_websocket_client_start(client->ws);
    assert(update_voice_internal(client, QM_TRUE, 0U) >= QM_EOK);
    assert(strstr(test_ws_sent, "CLIENT_VOICE_CHAT_UPDATE"));
    unsigned before = ready;
    voice_ack(client, QM_FALSE);
    assert(!joyinside_client_is_connected(client));
    voice_ack(client, QM_TRUE);
    assert(joyinside_client_is_connected(client) && ready == before + 1U);
    voice_ack(client, QM_TRUE);
    assert(ready == before + 1U);
    assert(joyinside_client_send_text_input(client, "hello", 0U) == QM_EOK);
    cJSON *root = cJSON_Parse(test_ws_sent);
    assert(strcmp(cJSON_GetObjectItem(root, "uid")->valuestring, "bound-user") == 0);
    cJSON_Delete(root);
    client->heartbeat_pending = QM_TRUE;
    strcpy(client->heartbeat_mid, "client-mid");
    assert(handle_heartbeat_pong(client, "server-generated-mid") == QM_EOK);
    assert(!client->heartbeat_pending);
    client->ack_pending = QM_TRUE;
    int reconnects = test_ws_reconnects;
    assert(handle_ack_timeout(client) == -QM_ETIMEOUT);
    assert(test_ws_reconnects == reconnects + 1);
    assert(joyinside_client_stop(client) == QM_EOK);
    assert(joyinside_client_destroy(client) == QM_EOK);

    /* Exercise the actual core worker: network-down defers token acquisition. */
    client = new_client();
    assert(qm_sem_new(&test_start_sem, 0U) == QM_EOK);
    test_signal_start = QM_TRUE;
    assert(joyinside_client_network_update(client, JOYINSIDE_CLIENT_NETWORK_DOWN) == QM_EOK);
    unsigned calls = token_calls;
    assert(joyinside_client_start(client, &params) == QM_EOK);
    assert(qm_sem_wait(&test_start_sem, 20U) == -QM_ETIMEOUT && token_calls == calls);
    assert(joyinside_client_network_update(client, JOYINSIDE_CLIENT_NETWORK_UP) == QM_EOK);
    assert(qm_sem_wait(&test_start_sem, 1000U) == QM_EOK && token_calls == calls + 1U);
    confirm_last_voice(client);
    assert(joyinside_client_is_connected(client));
    assert(qm_sem_new(&test_send_sem, 0U) == QM_EOK);
    test_signal_sends = QM_TRUE;
    joyinside_client_voice_config_t update = client->config.voice;
    update.voice_speed = 1.1f;
    unsigned updates = voice_updates;
    assert(joyinside_client_update_voice(client, &update) == QM_EOK);
    assert(qm_sem_wait(&test_send_sem, 1000U) == QM_EOK);
    assert(!joyinside_client_is_connected(client));
    confirm_last_voice(client);
    assert(joyinside_client_is_connected(client) && voice_updates == updates + 1U);
    test_signal_sends = QM_FALSE;
    assert(joyinside_client_stop(client) == QM_EOK);
    qm_sem_free(&test_send_sem);
    test_signal_start = QM_FALSE;
    assert(joyinside_client_destroy(client) == QM_EOK);
    qm_sem_free(&test_start_sem);
}

static qm_sem_t token_entered, token_release;
static int blocked_token_provider(void *context, qm_bool force, char *token, uint32_t *length)
{
    qm_sem_signal(&token_entered);
    assert(qm_sem_wait(&token_release, 1000U) == QM_EOK);
    return token_provider(context, force, token, length);
}
static void test_stop_during_token(void)
{
    joyinside_client_handle_t client = new_client();
    assert(qm_sem_new(&token_entered, 0U) == QM_EOK);
    assert(qm_sem_new(&token_release, 0U) == QM_EOK);
    joyinside_client_start_params_t params = {.bot_id = "bot", .uid = "bound-user",
        .token_provider = blocked_token_provider};
    int starts = test_ws_starts;
    assert(joyinside_client_start(client, &params) == QM_EOK);
    assert(qm_sem_wait(&token_entered, 1000U) == QM_EOK);
    assert(joyinside_client_stop(client) == -QM_ETIMEOUT);
    assert(client->ws && client->core_task_valid);
    assert(joyinside_client_send_audio(client, "abc", 3U) == -QM_EBUSY);
    qm_sem_signal(&token_release);
    assert(joyinside_client_stop(client) == QM_EOK);
    assert(test_ws_starts == starts);
    assert(joyinside_client_destroy(client) == QM_EOK);
    qm_sem_free(&token_entered); qm_sem_free(&token_release);
}

static void reset_http(const char *reply)
{ test_http_reply = reply; test_http_calls = 0; test_http_failures = 0; }

static void test_auth(void)
{
    char signature[JOYINSIDE_CLIENT_SIGNATURE_BUFFER_LEN];
    assert(joyinside_client_generate_signature("test-key", "1789500000000", "test-nonce", "test-secret", signature) == QM_EOK);
    assert(strcmp(signature, "632853cf6b4cc872ebae8cb6d6294639") == 0);
    joyinside_client_token_request_t token = {0};
    token.credentials.access_key_id = "test-key";
    token.credentials.access_key_secret = "test-secret";
    token.credentials.vendor_id = "vendor";
    token.max_retries = 1;
    char output[64]; uint32_t len = sizeof(output);
    reset_http("{\"code\":0,\"accessToken\":\"valid-token\",\"expireIn\":7200,\"refreshToken\":\"refresh\"}");
    test_http_failures = 1;
    joyinside_client_token_extra_t extra = {0};
    assert(joyinside_client_request_token_ex(&token, output, &len, &extra) == QM_EOK);
    assert(test_http_calls == 2 && extra.expire_in_sec == 7200U);
    cJSON *first = cJSON_Parse(test_http_first), *last = cJSON_Parse(test_http_body);
    assert(strcmp(cJSON_GetObjectItem(first, "accessNonce")->valuestring,
                  cJSON_GetObjectItem(last, "accessNonce")->valuestring) != 0);
    assert(strcmp(cJSON_GetObjectItem(first, "accessSign")->valuestring,
                  cJSON_GetObjectItem(last, "accessSign")->valuestring) != 0);
    cJSON_Delete(first); cJSON_Delete(last);

    joyinside_client_register_request_t registration = {0};
    registration.credentials = token.credentials;
    registration.credentials.product_id = "new-product";
    registration.device.type = "PHYSICAL_ROBOT";
    registration.device.name = "test-name";
    registration.device.device_id = "test-sn";
    const char *providers[] = {"WANG_YI_CLOUD"};
    registration.device.resource_authed_list = providers;
    registration.device.resource_authed_count = 1U;
    reset_http("{\"code\":\"0000\",\"state\":\"SUCCESS\",\"accessToken\":\"auth\",\"data\":\"bot\"}");
    len = sizeof(output);
    assert(joyinside_client_register_device(&registration, output, &len) == QM_EOK);
    last = cJSON_Parse(test_http_body);
    assert(strcmp(cJSON_GetObjectItem(last, "productId")->valuestring, "new-product") == 0);
    assert(!cJSON_GetObjectItem(last, "appId"));
    assert(!strcmp(cJSON_GetStringValue(cJSON_GetArrayItem(cJSON_GetObjectItem(last, "resourceAuthedList"), 0)), "WANG_YI_CLOUD"));
    cJSON_Delete(last);

    joyinside_client_user_bind_request_t binding = {.access_token = "test", .bot_id = "bot",
        .request_id = "request", .uid = "user", .delete_memory = QM_TRUE};
    reset_http("{\"state\":\"SUCCESS\",\"code\":\"0000\",\"data\":true}");
    assert(joyinside_client_unbind_user(&binding) == QM_EOK);
    last = cJSON_Parse(test_http_body);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(last, "deleteMemory"))); cJSON_Delete(last);
    reset_http("{\"code\":0,\"data\":false}");
    assert(joyinside_client_unbind_user(&binding) == -QM_EIO);
    reset_http("{\"code\":0,\"data\":\"bound-uid\"}"); len = sizeof(output);
    assert(joyinside_client_query_bot_bind(&binding, output, &len) == QM_EOK);
    assert(strcmp(output, "bound-uid") == 0 && len == 9U);
    len = 2U;
    assert(joyinside_client_query_bot_bind(&binding, output, &len) == -QM_EFULL && len == 9U);
    binding.uid = NULL; test_http_calls = 0;
    assert(joyinside_client_bind_user(&binding) == -QM_EINVAL && test_http_calls == 0);
    assert(joyinside_client_unbind_user(&binding) == -QM_EINVAL && test_http_calls == 0);
    binding.uid = "user"; reset_http("{\"state\":\"FAIL\",\"code\":0,\"data\":true}");
    assert(joyinside_client_unbind_user(&binding) == -QM_EIO);
}

static int print_received;
static int print_command(const joyinside_client_print_request_t *request, void *context)
{
    (void)context;
    assert(request->meta.code == 200 && request->meta.timestamp_ms == 1789500000123ULL);
    assert(strcmp(request->meta.msg, "ok") == 0);
    ++print_received;
    return QM_EOK;
}

static void test_extensions(void)
{
    joyinside_client_handle_t clients[6];
    joyinside_client_print_config_t config = {.on_command = print_command};
    for (unsigned i = 0; i < 6; ++i) {
        clients[i] = new_client();
        assert(joyinside_client_print_register(clients[i], &config) == QM_EOK);
    }
    cJSON *root = cJSON_Parse("{\"code\":\"200\",\"msg\":\"ok\",\"mid\":\"m\",\"uid\":\"u\",\"t\":1789500000123,"
        "\"contentType\":\"PRINT\",\"content\":{\"eventType\":\"PRINT_STOP\",\"eventData\":{\"printTaskId\":\"p\"}}}");
    for (unsigned i = 0; i < 6; ++i) {
        assert(joyinside_client_print_handle_message(clients[i], root) == QM_EOK);
        assert(joyinside_client_stop(clients[i]) == QM_EOK);
        assert(joyinside_client_extensions(clients[i])->print.config.on_command == print_command);
        assert(joyinside_client_destroy(clients[i]) == QM_EOK);
    }
    assert(print_received == 6);
    cJSON_Delete(root);
}


#if CONFIG_JOYINSIDE_CLIENT_MEDIA_SUPPORT
/* A controllable asynchronous player. Semaphores assert ordering without sleeps. */
typedef struct {
    joyinside_client_media_t *media;
    qm_sem_t started, bridge, stopped, finished, playing;
    uint64_t generation, start_progress, final_progress;
    qm_bool final_finish;
    int final_error;
    int start_result;
    unsigned ping_before;
    char url[4096];
} fake_player_t;

static int player_start(void *context, uint64_t generation, const joyinside_client_media_play_t *play)
{
    fake_player_t *player = context;
    assert(test_book_count("AUDIO_BOOK_PING") > player->ping_before);
    player->generation = generation;
    player->start_progress = play->progress_sec;
    snprintf(player->url, sizeof(player->url), "%s", play->audio_url);
    assert(qm_sem_signal(&player->started) == QM_EOK);
    return player->start_result;
}
static int player_bridge(void *context, uint64_t generation)
{
    fake_player_t *player = context;
    assert(test_book_count("AUDIO_BOOK_PING") > player->ping_before);
    player->generation = generation;
    assert(qm_sem_signal(&player->bridge) == QM_EOK);
    return QM_EOK;
}
static int player_stop(void *context, uint64_t generation)
{
    fake_player_t *player = context;
    assert(generation == player->generation);
    assert(qm_sem_signal(&player->stopped) == QM_EOK);
    return QM_EOK;
}
static void player_state(joyinside_client_media_state_t state, uint64_t generation, void *context)
{
    (void)generation;
    fake_player_t *player = context;
    if (state == JOYINSIDE_CLIENT_MEDIA_PLAYING) qm_sem_signal(&player->playing);
}
static void player_finished(uint64_t progress, qm_bool finish, int error, void *context)
{
    fake_player_t *player = context;
    player->final_progress = progress;
    player->final_finish = finish;
    player->final_error = error;
    qm_sem_signal(&player->finished);
}
static void book_play(joyinside_client_handle_t client, const char *chapter, const char *url, qm_bool bridge)
{
    cJSON *root = cJSON_Parse("{\"code\":200,\"msg\":\"ok\",\"requestId\":\"r\",\"mid\":\"m\",\"uid\":\"u\",\"t\":1789500000123,"
        "\"contentType\":\"AUDIO_BOOK\",\"content\":{\"eventType\":\"AUDIO_BOOK_PLAY\","
        "\"roundId\":\"r\",\"eventData\":{\"bookId\":\"book\",\"bookName\":\"Book\","
        "\"chapter\":\"1\",\"chapterName\":\"Chapter\",\"totalLength\":120,\"progress\":37}}}");
    cJSON *data = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "content"), "eventData");
    cJSON_AddStringToObject(data, "chapterId", chapter);
    cJSON_AddStringToObject(data, "audioURL", url);
    cJSON_AddBoolToObject(data, "includeTTS", bridge);
    assert(joyinside_client_audiobook_handle_message(client, root) == QM_EOK);
    cJSON_Delete(root);
}
static void wait_event(qm_sem_t *sem) { assert(qm_sem_wait(sem, 1000U) == QM_EOK); }
static void test_media(void)
{
    joyinside_client_handle_t client = new_client();
    fake_player_t player = {0};
    assert(qm_sem_new(&player.started, 0) == QM_EOK);
    assert(qm_sem_new(&player.bridge, 0) == QM_EOK);
    assert(qm_sem_new(&player.stopped, 0) == QM_EOK);
    assert(qm_sem_new(&player.finished, 0) == QM_EOK);
    assert(qm_sem_new(&player.playing, 0) == QM_EOK);
    joyinside_client_media_config_t config = {.client = client,
        .backend = {.start = player_start, .stop = player_stop, .wait_bridge = player_bridge, .context = &player},
        .on_finished = player_finished, .on_state = player_state, .context = &player};
    assert(joyinside_client_media_create(&config, &player.media) == QM_EOK);
    assert(joyinside_client_destroy(client) == -QM_EBUSY);
    assert(joyinside_client_audiobook_unregister(client) == -QM_EBUSY);
    qm_websocket_client_config_t ws_config = {.disable_auto_reconnect = QM_TRUE};
    client->ws = qm_websocket_client_init(&ws_config);
    qm_websocket_client_start(client->ws);
    client->connected = QM_TRUE;
    assert(start_core(client) == QM_EOK);
    assert(joyinside_client_media_destroy(player.media, 1U) == -QM_EBUSY);
    assert(joyinside_client_audiobook_unregister(client) == -QM_EBUSY);

    assert(joyinside_client_audiobook_start_ping(client, 1000U, 0U) == QM_EOK);
    assert((client->protocol_active &
            (1U << JOYINSIDE_CLIENT_CORE_EVENT_AUDIOBOOK_PING)) != 0U);
    assert(joyinside_client_audiobook_send_event(client,
            JOYINSIDE_CLIENT_AUDIOBOOK_EVENT_STOP,
            "{\"bookId\":\"manual\",\"chapterId\":\"manual\",\"progress\":0,\"finish\":false}",
            0U) == QM_EOK);
    assert((client->protocol_active &
            (1U << JOYINSIDE_CLIENT_CORE_EVENT_AUDIOBOOK_PING)) != 0U);
    joyinside_client_audiobook_stop_ping(client);

    char url[3000]; memset(url, 'x', sizeof(url));
    memcpy(url, "https://example.invalid/", 24); url[sizeof(url) - 1] = 0;
    player.ping_before = test_book_count("AUDIO_BOOK_PING");
    book_play(client, "first", url, QM_TRUE);
    wait_event(&player.bridge);
    assert(qm_sem_wait(&player.started, 10U) == -QM_ETIMEOUT);
    uint64_t first = player.generation;
    assert(joyinside_client_media_notify(player.media, first, JOYINSIDE_CLIENT_MEDIA_BRIDGE_DRAINED, 37U) == QM_EOK);
    wait_event(&player.started);
    assert(player.start_progress == 37U && strcmp(player.url, url) == 0);
    assert(joyinside_client_media_notify(player.media, first, JOYINSIDE_CLIENT_MEDIA_STARTED, 37U) == QM_EOK);
    wait_event(&player.playing);
    assert(joyinside_client_media_notify(player.media, first, JOYINSIDE_CLIENT_MEDIA_PROGRESS, 52U) == QM_EOK);
    cJSON *unrelated = cJSON_Parse("{\"code\":200,\"msg\":\"ok\",\"requestId\":\"r\",\"mid\":\"m\",\"t\":1,"
        "\"contentType\":\"AUDIO_BOOK\",\"content\":{\"eventType\":\"AUDIO_BOOK_STOP\",\"roundId\":\"r\","
        "\"eventData\":{\"bookId\":\"book\",\"chapterId\":\"unrelated\"}}}");
    assert(joyinside_client_audiobook_handle_message(client, unrelated) == QM_EOK);
    cJSON_Delete(unrelated);
    assert(qm_sem_wait(&player.stopped, 10U) == -QM_ETIMEOUT);

    /* A replacement must wait for the old backend to acknowledge stop. */
    book_play(client, "second", "https://example.invalid/second.mp3", QM_FALSE);
    wait_event(&player.stopped);
    assert(qm_sem_wait(&player.started, 10U) == -QM_ETIMEOUT);
    assert(joyinside_client_media_notify(player.media, first, JOYINSIDE_CLIENT_MEDIA_STOPPED, 53U) == QM_EOK);
    wait_event(&player.finished);
    assert(player.final_progress == 53U && !player.final_finish);
    wait_event(&player.started);
    uint64_t second = player.generation;
    assert(second != first);
    assert(joyinside_client_media_notify(player.media, first, JOYINSIDE_CLIENT_MEDIA_FINISHED, 120U) == -QM_ENOENT);
    assert(joyinside_client_media_notify(player.media, second, JOYINSIDE_CLIENT_MEDIA_FINISHED, 119U) == QM_EOK);
    wait_event(&player.finished);
    assert(player.final_progress == 120U && player.final_finish && player.final_error == QM_EOK);
    char wire[1024]; test_book_last("AUDIO_BOOK_STOP", wire, sizeof(wire));
    cJSON *root = cJSON_Parse(wire);
    cJSON *data = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "content"), "eventData");
    assert(cJSON_GetNumberValue(cJSON_GetObjectItem(data, "progress")) == 120);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(data, "finish"))); cJSON_Delete(root);

    player.start_result = -QM_ENOSYS;
    player.ping_before = test_book_count("AUDIO_BOOK_PING");
    book_play(client, "unsupported", url, QM_FALSE);
    wait_event(&player.started); wait_event(&player.finished);
    assert(player.final_error == -QM_ENOSYS && !player.final_finish && player.final_progress == 37U);
    player.start_result = QM_EOK;

    /* Destruction cannot free a pending bridge subscription. */
    player.ping_before = test_book_count("AUDIO_BOOK_PING");
    book_play(client, "third", url, QM_TRUE);
    wait_event(&player.bridge);
    uint64_t third = player.generation;
    assert(joyinside_client_stop(client) == QM_EOK);
    assert(joyinside_client_media_destroy(player.media, 10U) == -QM_ETIMEOUT);
    wait_event(&player.stopped);
    assert(joyinside_client_media_notify(player.media, third, JOYINSIDE_CLIENT_MEDIA_STOPPED, 37U) == QM_EOK);
    assert(joyinside_client_media_destroy(player.media, 1000U) == QM_EOK);
    assert(joyinside_client_destroy(client) == QM_EOK);
    qm_sem_free(&player.started); qm_sem_free(&player.bridge); qm_sem_free(&player.stopped);
    qm_sem_free(&player.finished); qm_sem_free(&player.playing);
}

#endif

static void confirm_last_voice(joyinside_client_handle_t client)
{
    cJSON *reply = cJSON_Parse(test_ws_sent);
    assert(reply);
    cJSON *content = cJSON_GetObjectItem(reply, "content");
    cJSON_ReplaceItemInObject(content, "eventType", cJSON_CreateString("SERVER_VOICE_CHAT_UPDATED"));
    cJSON_AddNumberToObject(reply, "code", 200);
    char *json = cJSON_PrintUnformatted(reply);
    assert(json);
    feed_json(client, json);
    cJSON_free(json);
    cJSON_Delete(reply);
}

/* Keep dequeue timing deterministic without substituting the real send path. */
static joyinside_client_handle_t protocol_fixture(const joyinside_client_voice_config_t *voice)
{
    joyinside_client_handle_t client = new_voice_client(voice);
    joyinside_client_start_params_t params = {.bot_id = "test-bot", .uid = "test-user",
        .session_id = "image-session", .access_token = "test-token"};
    assert(set_start_state(client, &params) == QM_EOK);
    qm_websocket_client_config_t ws = {.disable_auto_reconnect = QM_TRUE};
    client->ws = qm_websocket_client_init(&ws);
    assert(qm_websocket_client_start(client->ws) == QM_EOK);
    assert(qm_queue_new(&client->core_queue, JOYINSIDE_CLIENT_CORE_QUEUE_LEN,
                        sizeof(joyinside_client_core_event_t)) == QM_EOK);
    assert(qm_sem_new(&client->core_wake, 0U) == QM_EOK);
    client->audio_slots = qm_calloc(CONFIG_JOYINSIDE_CLIENT_AUDIO_QUEUE_LEN, sizeof(*client->audio_slots));
    assert(client->audio_slots);
    client->core_queue_valid = QM_TRUE;
    assert(update_voice_internal(client, QM_TRUE, 0U) == QM_EOK);
    confirm_last_voice(client);
    assert(joyinside_client_is_connected(client));
    return client;
}

static cJSON *dequeue_audio(joyinside_client_handle_t client)
{
    joyinside_client_core_event_t event;
    unsigned int size = sizeof(event);
    assert(qm_queue_recv(&client->core_queue, &event, &size, QM_NO_WAIT) == QM_EOK);
    --client->core_queue_count;
    process_audio(client, &event);
    cJSON *root = cJSON_Parse(test_ws_sent);
    assert(root);
    return root;
}

static void test_vad_and_configuration(void)
{
    joyinside_client_voice_config_t voice = {.input_codec = JOYINSIDE_CLIENT_AUDIO_CODEC_PCM,
        .output_codec = JOYINSIDE_CLIENT_AUDIO_CODEC_PCM, .vad = QM_TRUE,
        .output_transport = JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BINARY};
    joyinside_client_handle_t client = protocol_fixture(&voice);
    cJSON *config = cJSON_Parse(test_ws_sent);
    cJSON *audio = cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(config, "content"), "eventData"), "audio");
    assert(!cJSON_GetObjectItem(audio, "binary"));
    assert(cJSON_IsFalse(cJSON_GetObjectItem(cJSON_GetObjectItem(audio, "input"), "binary")));
    assert(cJSON_IsTrue(cJSON_GetObjectItem(cJSON_GetObjectItem(audio, "output"), "binary")));
    cJSON_Delete(config);

    assert(joyinside_client_vad_round_begin(client) == QM_EOK);
    char round_a[37], round_b[37];
    strcpy(round_a, client->vad_round_mid);
    assert(joyinside_client_send_audio(client, "a1", 2U) == QM_EOK);
    assert(joyinside_client_send_audio(client, "a2", 2U) == QM_EOK);
    assert(joyinside_client_vad_round_begin(client) == QM_EOK);
    strcpy(round_b, client->vad_round_mid);
    assert(strcmp(round_a, round_b));
    assert(joyinside_client_send_audio(client, "b1", 2U) == QM_EOK);
    feed_json(client, "{\"code\":200,\"contentType\":\"EVENT\",\"content\":{\"eventType\":\"COMPLETE\",\"roundId\":\"older-round\"}}");
    assert(!strcmp(round_b, client->vad_round_mid));
    feed_json(client, "{\"code\":200,\"contentType\":\"EVENT\",\"content\":{\"eventType\":\"CALL_AGENT_INTERRUPTED\"}}");
    assert(!strcmp(round_b, client->vad_round_mid));
    for (unsigned i = 0; i < 3U; ++i) {
        cJSON *root = dequeue_audio(client);
        cJSON *content = cJSON_GetObjectItem(root, "content");
        assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(content, "mid")), i < 2 ? round_a : round_b));
        assert(cJSON_GetNumberValue(cJSON_GetObjectItem(content, "index")) == (i == 1 ? 1 : 0));
        cJSON_Delete(root);
    }

    voice.output_codec = JOYINSIDE_CLIENT_AUDIO_CODEC_MP3;
    voice.output_stream = QM_TRUE;
    voice.output_transport = JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BASE64;
    char role[] = "new-role";
    voice.chat_role_code = role;
    voice.voice_speed = 1.1f;
    voice.voice_volume = 5.0f;
    unsigned connects = ready, updates = voice_updates;
    assert(joyinside_client_update_voice(client, &voice) == QM_EOK);
    assert(joyinside_client_update_voice(client, &voice) == -QM_EBUSY);
    assert(joyinside_client_send_audio(client, "x", 1U) == -QM_EBUSY);
    memset(role, 'x', strlen(role));
    assert(update_voice_internal(client, QM_FALSE, 0U) == QM_EOK);
    assert(strstr(test_ws_sent, "new-role"));
    char *requested = strdup(test_ws_sent);
    assert(requested);
    unsigned bytes = audio_bytes;
    feed_json(client, "{\"code\":200,\"contentType\":\"TTS\",\"content\":{\"audioBase64\":\"YWJj\",\"audioAue\":\"pcm\"}}");
    assert(audio_bytes == bytes); /* Old audio is gated while changing formats. */

    config = cJSON_Parse(requested);
    cJSON_AddNumberToObject(config, "code", 200);
    cJSON *content = cJSON_GetObjectItem(config, "content");
    cJSON_ReplaceItemInObject(content, "eventType", cJSON_CreateString("SERVER_VOICE_CHAT_UPDATED"));
    audio = cJSON_GetObjectItem(cJSON_GetObjectItem(content, "eventData"), "audio");
    cJSON_ReplaceItemInObject(audio, "vad", cJSON_CreateBool(0));
    cJSON_ReplaceItemInObject(cJSON_GetObjectItem(audio, "output"), "stream", cJSON_CreateBool(0));
    char *mismatch = cJSON_PrintUnformatted(config);
    unsigned before = errors;
    feed_json(client, mismatch);
    assert(errors == before + 1U && !joyinside_client_is_connected(client));
    cJSON_free(mismatch);
    cJSON_Delete(config);
    snprintf(test_ws_sent, sizeof(test_ws_sent), "%s", requested);
    confirm_last_voice(client);
    free(requested);
    assert(voice_updates == updates + 1U && ready == connects);
    assert(joyinside_client_is_connected(client));
    assert(update_voice_internal(client, QM_FALSE, 0U) == QM_EOK);
    assert(!client->ack_pending); /* A consumed request is never replayed. */
    feed_json(client, "{\"code\":200,\"contentType\":\"TTS\",\"content\":{\"audioBase64\":\"YWJj\",\"audioAue\":\"mp3\"}}");
    assert(audio_bytes == bytes + 3U);

    websocket_event_handler(WEBSOCKET_EVENT_DISCONNECTED, NULL, client);
    assert(update_voice_internal(client, QM_TRUE, 0U) == QM_EOK);
    assert(strstr(test_ws_sent, "new-role") && strstr(test_ws_sent, "\"stream\":true"));
    confirm_last_voice(client);
    assert(joyinside_client_is_connected(client));

    voice.chat_role_code = "valid";
    voice.voice_speed = 2.0f;
    assert(joyinside_client_update_voice(client, &voice) == -QM_EINVAL);
    voice.voice_speed = NAN;
    assert(joyinside_client_update_voice(client, &voice) == -QM_EINVAL);
    voice.voice_speed = 1.0f;
    voice.voice_volume = INFINITY;
    assert(joyinside_client_update_voice(client, &voice) == -QM_EINVAL);
    voice.voice_volume = 11.0f;
    assert(joyinside_client_update_voice(client, &voice) == -QM_EINVAL);
    voice.voice_volume = 1.0f;
    voice.input_transport = JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BINARY;
    assert(joyinside_client_update_voice(client, &voice) == -QM_EINVAL);
    voice.input_transport = JOYINSIDE_CLIENT_AUDIO_TRANSPORT_DEFAULT;
    voice.output_codec = JOYINSIDE_CLIENT_AUDIO_CODEC_PCM;
    assert(joyinside_client_update_voice(client, &voice) == -QM_EINVAL);
    assert(joyinside_client_is_connected(client) && !strcmp(client->config.voice.chat_role_code, "new-role"));
    assert(joyinside_client_destroy(client) == QM_EOK);

    voice.vad = QM_FALSE;
    voice.output_stream = QM_FALSE;
    voice.input_transport = JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BINARY;
    client = protocol_fixture(&voice); /* Reverse mixed direction also works. */
    bytes = audio_bytes;
    feed_json(client, "{\"code\":200,\"contentType\":\"TTS\",\"content\":{\"audioBase64\":\"YWJj\"}}");
    assert(audio_bytes == bytes + 3U);
    assert(joyinside_client_destroy(client) == QM_EOK);
}

static void test_skill_variants(void)
{
    cJSON *content = cJSON_Parse("{\"eventData\":{\"data\":{\"domain\":\"alarm\",\"instructions\":[{\"type\":\"normal_alarm\",\"operation\":\"delete\",\"params\":{\"ids\":[\"a1\"]}},{\"type\":\"interval_reminder\",\"operation\":\"create\",\"params\":{\"interval_minutes\":30}}]}}}");
    joyinside_client_skill_t skill;
    joyinside_client_instruction_t instruction;
    parse_call_skill(content, &skill);
    assert(skill.instruction_count == 2U);
    assert(joyinside_client_skill_instruction(&skill, 0U, &instruction) == QM_EOK);
    assert(!strcmp(instruction.operation, "delete") && !strcmp(instruction.type, "normal_alarm"));
    assert(cJSON_IsArray(cJSON_GetObjectItem(instruction.params, "ids")));
    assert(joyinside_client_skill_instruction(&skill, 1U, &instruction) == QM_EOK);
    assert(!strcmp(instruction.operation, "create") && !strcmp(instruction.type, "interval_reminder"));
    assert(joyinside_client_skill_instruction(&skill, 2U, &instruction) == -QM_ENOENT);
    cJSON_Delete(content);
    content = cJSON_Parse("{\"eventData\":{\"data\":{\"args\":{\"EMOTION\":\"happy_1\",\"TTS_EMOTION\":\"高兴\"},\"instructions\":[{\"action\":\"LIGHT_UP\",\"args\":{}},{}]}}}");
    parse_call_skill(content, &skill);
    assert(skill.expression == JOYINSIDE_CLIENT_EXPRESSION_HAPPY && skill.tts_expression == JOYINSIDE_CLIENT_EXPRESSION_HAPPY);
    assert(!strcmp(skill.tts_expression_code, "高兴"));
    assert(joyinside_client_skill_instruction(&skill, 0U, &instruction) == QM_EOK);
    assert(!strcmp(instruction.operation, "LIGHT_UP") && !instruction.type);
    assert(joyinside_client_skill_instruction(&skill, 1U, &instruction) == -QM_EINVAL);
    cJSON_Delete(content);
}

#if CONFIG_JOYINSIDE_CLIENT_VISION_SUPPORT
static void test_vision_report(void)
{
    joyinside_client_handle_t client = protocol_fixture(NULL);
    client->token_provider = token_provider;
    assert(prepare_connection(client->ws, client) == QM_EOK);
    uint8_t jpeg[67];
    for (unsigned i = 0; i < sizeof(jpeg); ++i) jpeg[i] = (uint8_t)i;
    jpeg[0] = 0xff; jpeg[1] = 0xd8;
    joyinside_client_image_t request = {.app_id = "vision-app", .jpeg = jpeg,
        .timestamp_ms = 1789500000123ULL};
    char previous_request[37] = "";
    for (unsigned length = 64; length <= sizeof(jpeg); ++length) {
        request.jpeg_len = length;
        reset_http("{\"state\":\"SUCCESS\",\"code\":\"0000\"}");
        assert(joyinside_client_report_image(client, &request) == QM_EOK);
        assert(strstr(test_http_url, "/vision/client/image/report"));
        char expected_token[64];
        snprintf(expected_token, sizeof(expected_token), "Bearer %s", client->access_token);
        assert(!strcmp(expected_token, test_http_authorization));
        cJSON *root = cJSON_Parse(test_http_body);
        assert(root);
        assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(root, "sessionId")), "image-session"));
        assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(root, "appId")), "vision-app"));
        const char *request_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "requestId"));
        assert(request_id && strcmp(previous_request, request_id));
        snprintf(previous_request, sizeof(previous_request), "%s", request_id);
        const char *encoded = cJSON_GetStringValue(cJSON_GetObjectItem(root, "imgBase64"));
        uint8_t decoded[sizeof(jpeg)]; uint32_t decoded_len;
        assert(qm_utils_base64decode((const uint8_t *)encoded, (uint32_t)strlen(encoded),
                                     sizeof(decoded), decoded, &decoded_len) == QM_EOK);
        assert(decoded_len == length && !memcmp(decoded, jpeg, length));
        cJSON_Delete(root);
    }
    reset_http("{\"state\":\"FAILURE\",\"code\":\"0000\"}");
    assert(joyinside_client_report_image(client, &request) == -QM_EIO);
    reset_http("{\"state\":\"SUCCESS\",\"code\":\"0000\"} trailing");
    assert(joyinside_client_report_image(client, &request) == -QM_EIO);
    jpeg[0] = 0;
    assert(joyinside_client_report_image(client, &request) == -QM_EINVAL);
    jpeg[0] = 0xff;
    client->connected = QM_FALSE;
    assert(joyinside_client_report_image(client, &request) == -QM_EBUSY);
    assert(joyinside_client_destroy(client) == QM_EOK);
}
#endif

int main(void)
{
    g_joyinside_client_log_flags = 0;
    test_receive();
    test_server_event_payloads();
    test_connection();
    test_stop_during_token();
    test_auth();
    test_extensions();
    test_vad_and_configuration();
    test_skill_variants();
#if CONFIG_JOYINSIDE_CLIENT_VISION_SUPPORT
    test_vision_report();
#endif
#if CONFIG_JOYINSIDE_CLIENT_MEDIA_SUPPORT
    test_media();
#endif
    puts("PASS: message assembly, handshake, token/reconnect identity, HTTP contracts, per-client extensions, media ordering and lifetime");
    return 0;
}
