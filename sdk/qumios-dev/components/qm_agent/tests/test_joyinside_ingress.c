/** @file test_joyinside_ingress.c @brief Provider-owned audio copying and ordering. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../joyinside_client/src/joyinside_client.c"

static unsigned sends;
static int send_result;
static char sent[8192];
static qm_sem_t sent_sem;
static qm_bool signal_sent;
static qm_bool hold_send;
static qm_sem_t release_send;
void *qm_malloc(unsigned int n) { return malloc(n); }
void *qm_calloc(unsigned int n, unsigned int size) { return calloc(n, size); }
void qm_free(void *p) { free(p); }
char *qm_utils_random_uuid(char uuid[37])
{ memcpy(uuid, "00000000-0000-0000-0000-000000000001", 37); return uuid; }
int qm_websocket_client_send_bin(void *ws, const char *data, int len, uint32_t timeout)
{
    (void)ws;
    assert(timeout == 0U && len >= 0 && (size_t)len < sizeof(sent));
    memcpy(sent, data, (size_t)len);
    sent[len] = 0;
    sends++;
    if (signal_sent) (void)qm_sem_signal(&sent_sem);
    if (hold_send) assert(qm_sem_wait(&release_send, 1000U) == QM_EOK);
    return send_result < 0 ? send_result : len;
}
int qm_websocket_client_send_text(void *ws, const char *data, int len, uint32_t timeout)
{ return qm_websocket_client_send_bin(ws, data, len, timeout); }
qm_bool qm_websocket_client_is_connected(void *ws) { (void)ws; return QM_TRUE; }
int qm_websocket_client_reconnect(void *ws) { (void)ws; return QM_EOK; }
int joyinside_client_audiobook_handle_message(joyinside_client_handle_t handle, const cJSON *root)
{ (void)handle; (void)root; return QM_EOK; }
int joyinside_client_print_handle_message(joyinside_client_handle_t handle, const cJSON *root)
{ (void)handle; (void)root; return QM_EOK; }

static void consume(joyinside_client_handle_t handle)
{
    joyinside_client_core_event_t event = {0};
    unsigned int size = sizeof(event);
    assert(qm_queue_recv(&handle->core_queue, &event, &size, QM_NO_WAIT) == QM_EOK);
    handle->core_queue_count--;
    process_audio(handle, &event);
}

static void test_existing_worker(void)
{
    struct joyinside_client *client = calloc(1U, sizeof(*client));
    assert(client != NULL);
    assert(qm_mutex_new(&client->core_lock) == QM_EOK);
    assert(qm_mutex_new(&client->audio_send_lock) == QM_EOK);
    assert(qm_mutex_new(&client->tx_lock) == QM_EOK);
    assert(qm_sem_new(&sent_sem, 0U) == QM_EOK);
    assert(qm_sem_new(&release_send, 0U) == QM_EOK);
    client->connected = QM_TRUE;
    client->ws = client;
    client->config.voice.input_transport = CONFIG_JOYINSIDE_CLIENT_AUDIO_BINARY ?
        JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BINARY : JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BASE64;
    client->config.heartbeat.disabled = QM_TRUE;
    send_result = QM_EOK;
    signal_sent = QM_TRUE;
    for (unsigned i = 0; i < 3U; ++i) {
        assert(start_core(client) == QM_EOK);
        audio_admission(client, QM_TRUE);
        assert(joyinside_client_send_audio(client, "abc", 3U) == QM_EOK);
        assert(qm_sem_wait(&sent_sem, 1000U) == QM_EOK);
        assert(stop_core(client) == QM_EOK);
        assert(!client->core_task_valid && !client->core_queue_valid && !client->audio_slots);
        assert(stop_core(client) == QM_EOK);
    }
    hold_send = QM_TRUE;
    assert(start_core(client) == QM_EOK);
    audio_admission(client, QM_TRUE);
    assert(joyinside_client_send_audio(client, "abc", 3U) == QM_EOK);
    assert(qm_sem_wait(&sent_sem, 1000U) == QM_EOK);
    assert(stop_core(client) == -QM_ETIMEOUT);
    assert(client->core_task_valid && client->core_queue_valid && client->audio_slots);
    assert(joyinside_client_send_audio(client, "abc", 3U) == -QM_EBUSY);
    assert(qm_sem_signal(&release_send) == QM_EOK);
    assert(stop_core(client) == QM_EOK);
    hold_send = QM_FALSE;
    signal_sent = QM_FALSE;
    assert(qm_sem_free(&sent_sem) == QM_EOK);
    assert(qm_sem_free(&release_send) == QM_EOK);
    assert(qm_mutex_free(&client->tx_lock) == QM_EOK);
    assert(qm_mutex_free(&client->audio_send_lock) == QM_EOK);
    assert(qm_mutex_free(&client->core_lock) == QM_EOK);
    free(client);
}

int main(void)
{
    struct joyinside_client client = {0};
    char input[] = "abc";
    g_joyinside_client_log_flags = 0U;
    assert(qm_mutex_new(&client.core_lock) == QM_EOK);
    assert(qm_mutex_new(&client.audio_send_lock) == QM_EOK);
    assert(qm_mutex_new(&client.tx_lock) == QM_EOK);
    assert(qm_sem_new(&client.core_wake, 0U) == QM_EOK);
    assert(qm_queue_new(&client.core_queue, JOYINSIDE_CLIENT_CORE_QUEUE_LEN,
                       sizeof(joyinside_client_core_event_t)) == QM_EOK);
    client.audio_slots = calloc(CONFIG_JOYINSIDE_CLIENT_AUDIO_QUEUE_LEN, sizeof(*client.audio_slots));
    assert(client.audio_slots != NULL);
    client.core_queue_valid = QM_TRUE;
    client.connected = QM_TRUE;
    client.ws = &client;
    client.config.voice.input_transport = CONFIG_JOYINSIDE_CLIENT_AUDIO_BINARY ?
        JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BINARY : JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BASE64;
    audio_admission(&client, QM_TRUE);
    for (uint32_t i = 0; i < CONFIG_JOYINSIDE_CLIENT_AUDIO_QUEUE_LEN; ++i) {
        assert(joyinside_client_send_audio(&client, input, 3U) == QM_EOK);
    }
    assert(sends == 0U);
    memset(input, 'x', 3U);
    assert(joyinside_client_send_audio(&client, input, 3U) == -QM_EFULL);
#if CONFIG_JOYINSIDE_CLIENT_MANUAL_CALL
    assert(joyinside_client_send_audio_finish(&client, 0U) == QM_EOK);
#endif
    consume(&client);
#if CONFIG_JOYINSIDE_CLIENT_MANUAL_CALL
    assert(client.audio_index == 0U);
#else
    assert(client.audio_index == CONFIG_JOYINSIDE_CLIENT_AUDIO_QUEUE_LEN);
#endif
#if CONFIG_JOYINSIDE_CLIENT_AUDIO_BINARY
    assert(strcmp(sent, "abc") == 0);
#else
    cJSON *root = cJSON_Parse(sent);
    cJSON *content = cJSON_GetObjectItem(root, "content");
    assert(strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(content, "audioBase64")), "YWJj") == 0);
    assert(cJSON_GetNumberValue(cJSON_GetObjectItem(content, "index")) == 0);
    cJSON_Delete(root);
#endif
    while (client.core_queue_count != 0U) consume(&client);
#if CONFIG_JOYINSIDE_CLIENT_MANUAL_CALL
    assert(strstr(sent, "CLIENT_AUDIO_FINISH") != NULL);
    assert(client.audio_index == 0U);
#endif
    assert(joyinside_client_send_audio(&client, input, 3U) == QM_EOK);
    audio_admission(&client, QM_FALSE);
    audio_admission(&client, QM_TRUE);
    unsigned before = sends;
    consume(&client);
    assert(sends == before);
    send_result = -QM_EFULL;
    assert(joyinside_client_send_audio(&client, input, 3U) == QM_EOK);
    consume(&client);
    assert(client.audio_index == 1U);
    assert(qm_queue_free(&client.core_queue) == QM_EOK);
    assert(qm_sem_free(&client.core_wake) == QM_EOK);
    assert(qm_mutex_free(&client.tx_lock) == QM_EOK);
    assert(qm_mutex_free(&client.audio_send_lock) == QM_EOK);
    assert(qm_mutex_free(&client.core_lock) == QM_EOK);
    free(client.audio_slots);
    test_existing_worker();
    puts("PASS: JoyInside audio ownership, protocol sequencing and stale frames");
    return 0;
}
