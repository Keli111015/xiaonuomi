/** @file test_websocket_queue.c @brief Real WebSocket queue admission regressions. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "../../qm_websocket_client/src/qm_websocket_client.c"

void *qm_malloc(unsigned int n) { return malloc(n); }
void *qm_calloc(unsigned int n, unsigned int size) { return calloc(n, size); }
void qm_free(void *p) { free(p); }
void *qm_memcpy(void *d, const void *s, uint32_t n) { return memcpy(d, s, n); }
void *qm_memset(void *d, int32_t v, uint32_t n) { return memset(d, v, n); }

typedef struct {
    qm_websocket_client_t *client;
    qm_sem_t locked;
    qm_sem_t release;
} contention_t;

static void *hold_admission(void *arg)
{
    contention_t *test = arg;
    assert(qm_mutex_lock(&test->client->tx_mutex, QM_WAIT_FOREVER) == QM_EOK);
    assert(qm_sem_signal(&test->locked) == QM_EOK);
    assert(qm_sem_wait(&test->release, 1000U) == QM_EOK);
    assert(qm_mutex_unlock(&test->client->tx_mutex) == QM_EOK);
    return NULL;
}

int main(void)
{
    qm_websocket_client_t client = {0};
    qm_websocket_message_t *message;
    unsigned int size = sizeof(message);
    char input[] = "abc";

    g_qm_websocket_client_log_flags = 0U;
    assert(qm_mutex_new(&client.mutex) == QM_EOK);
    assert(qm_mutex_new(&client.tx_mutex) == QM_EOK);
    assert(qm_sem_new(&client.tx_space, 0U) == QM_EOK);
    assert(qm_queue_new(&client.send_queue, 1U, sizeof(message)) == QM_EOK);
    client.config.send_queue_size = 1;
    ws_set_state(&client, WEBSOCKET_STATE_CONNECTED);
    contention_t test = {.client = &client};
    pthread_t thread;
    assert(qm_sem_new(&test.locked, 0U) == QM_EOK);
    assert(qm_sem_new(&test.release, 0U) == QM_EOK);
    assert(pthread_create(&thread, NULL, hold_admission, &test) == 0);
    assert(qm_sem_wait(&test.locked, 1000U) == QM_EOK);
    assert(qm_websocket_client_send_bin(&client, input, 3, 0) == -QM_EBUSY);
    assert(qm_sem_signal(&test.release) == QM_EOK);
    assert(pthread_join(thread, NULL) == 0);
    assert(qm_sem_free(&test.locked) == QM_EOK);
    assert(qm_sem_free(&test.release) == QM_EOK);
    assert(qm_websocket_client_send_bin(&client, input, 3, 0) == 3);
    memset(input, 'x', 3);
    assert(client.tx_count == 1U);
    assert(qm_websocket_client_send_bin_partial(&client, input, 3, 0) == -QM_EFULL);
    assert(client.tx_frag_opcode == 0);
    assert(qm_websocket_client_send_bin(&client, input, 3, 5) == -QM_ETIMEOUT);
    assert(qm_queue_recv(&client.send_queue, &message, &size, QM_NO_WAIT) == QM_EOK);
    client.tx_count--;
    assert(memcmp(message->data, "abc", 3) == 0);
    qm_free(message->data);
    qm_free(message);
    assert(qm_websocket_client_send_bin_partial(&client, input, 3, 0) == 3);
    assert(client.tx_frag_opcode == WS_OPCODE_BINARY);
    ws_set_state(&client, WEBSOCKET_STATE_INIT);
    assert(!client.tx_open && client.tx_count == 0U && client.tx_frag_opcode == 0);
    assert(qm_websocket_client_send_bin(&client, input, 3, 0) == -QM_EBUSY);
    ws_set_state(&client, WEBSOCKET_STATE_CONNECTED);
    assert(qm_websocket_client_send_fin(&client, 0) == -QM_EINVAL);
    assert(qm_websocket_client_send_bin(&client, input, 3, 0) == 3);
    ws_set_state(&client, WEBSOCKET_STATE_CLOSED);
    assert(qm_queue_free(&client.send_queue) == QM_EOK);
    assert(qm_sem_free(&client.tx_space) == QM_EOK);
    assert(qm_mutex_free(&client.tx_mutex) == QM_EOK);
    assert(qm_mutex_free(&client.mutex) == QM_EOK);
    puts("PASS: bounded WebSocket admission and connection invalidation");
    return 0;
}
