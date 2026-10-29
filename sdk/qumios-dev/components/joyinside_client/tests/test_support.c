#include "test_support.h"
#include "qm_http_client.h"
#include "qm_time.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include "cJSON.h"

#define WEAK __attribute__((weak))
char test_http_body[TEST_WIRE_CAPACITY];
char test_http_first[TEST_WIRE_CAPACITY];
char test_http_url[512];
const char *test_http_reply;
int test_http_calls;
int test_http_failures;
int test_ws_starts;
int test_ws_reconnects;
int test_send_result;
char test_ws_uri[4096];
char test_ws_headers[8192];
char test_ws_sent[TEST_WIRE_CAPACITY];
qm_bool test_signal_sends;
qm_sem_t test_send_sem;
qm_bool test_signal_start;
qm_sem_t test_start_sem;

static pthread_mutex_t wire_lock = PTHREAD_MUTEX_INITIALIZER;
static const char *book_events[] = {"AUDIO_BOOK_PING", "AUDIO_BOOK_PLAY", "AUDIO_BOOK_STOP"};
static unsigned book_counts[3];
static char book_last[3][1024];
unsigned test_book_count(const char *event)
{
    unsigned count = 0;
    pthread_mutex_lock(&wire_lock);
    for (unsigned i = 0; i < 3; ++i) if (!strcmp(event, book_events[i])) count = book_counts[i];
    pthread_mutex_unlock(&wire_lock);
    return count;
}
void test_book_last(const char *event, char *output, unsigned capacity)
{
    pthread_mutex_lock(&wire_lock);
    for (unsigned i = 0; i < 3; ++i)
        if (!strcmp(event, book_events[i])) snprintf(output, capacity, "%s", book_last[i]);
    pthread_mutex_unlock(&wire_lock);
}

WEAK void *qm_malloc(unsigned int n) { return malloc(n); }
WEAK void *qm_calloc(unsigned int n, unsigned int size) { return calloc(n, size); }
WEAK void qm_free(void *p) { free(p); }
static atomic_uint uuid_counter;
WEAK char *qm_utils_random_uuid(char uuid[37])
{ snprintf(uuid, 37, "00000000-0000-0000-0000-%012u", atomic_fetch_add(&uuid_counter, 1U)); return uuid; }
char *qm_utils_unformatted_uuid(char uuid[33])
{ snprintf(uuid, 33, "%032u", atomic_fetch_add(&uuid_counter, 1U)); return uuid; }
#if CONFIG_QM_TIME_SUPPORT
int qm_gettimeofday(struct qm_timeval *tv, struct qm_timezone *tz)
{ (void)tz; tv->tv_sec = 1789500000; tv->tv_usec = 0; return QM_EOK; }
qm_time_t qm_time(qm_time_t *t)
{ if (t) *t = 1789500000; return 1789500000; }
#endif

char test_http_authorization[512];

int qm_http_client_execute(const qm_http_client_request_t *req, qm_http_client_response_t *resp)
{
    assert(req->body_len < (int)sizeof(test_http_body));
    if (req->body_cb) {
        for (int position = 0; position < req->body_len;) {
            int capacity = position % 17 + 1;
            if (capacity > req->body_len - position) capacity = req->body_len - position;
            int n = req->body_cb(test_http_body + position, capacity, req->user);
            assert(n > 0 && n <= capacity);
            position += n;
        }
        test_http_body[req->body_len] = 0;
    } else {
        snprintf(test_http_body, sizeof(test_http_body), "%s", req->body);
    }
    test_http_authorization[0] = 0;
    for (int i = 0; i < req->header_count; ++i)
        if (!strcmp(req->headers[i].name, "Authorization"))
            snprintf(test_http_authorization, sizeof(test_http_authorization), "%s", req->headers[i].value);
    snprintf(test_http_url, sizeof(test_http_url), "%s", req->url);
    if (test_http_calls++ == 0) snprintf(test_http_first, sizeof(test_http_first), "%s", test_http_body);
    if (test_http_failures-- > 0) { resp->status_code = 500; return QM_EOK; }
    resp->status_code = 200;
    assert(test_http_reply);
    return req->on_body(test_http_reply, (int)strlen(test_http_reply), req->user);
}

typedef struct {
    qm_websocket_event_handler_t callback;
    void *context;
    qm_bool connected;
} test_websocket_t;

WEAK void *qm_websocket_client_init(const qm_websocket_client_config_t *config)
{
    assert(config->disable_auto_reconnect);
    return calloc(1, sizeof(test_websocket_t));
}
WEAK int qm_websocket_register_events(void *ws, qm_websocket_event_id_t id,
    qm_websocket_event_handler_t callback, void *context)
{ (void)id; test_websocket_t *s = ws; s->callback = callback; s->context = context; return QM_EOK; }
WEAK int qm_websocket_client_start(void *ws)
{
    test_websocket_t *s = ws;
    ++test_ws_starts;
    s->connected = QM_TRUE;
    if (s->callback) s->callback(WEBSOCKET_EVENT_CONNECTED, NULL, s->context);
    if (test_signal_start) qm_sem_signal(&test_start_sem);
    return QM_EOK;
}
WEAK int qm_websocket_client_stop(void *ws)
{
    test_websocket_t *s = ws;
    if (s->connected) {
        s->connected = QM_FALSE;
        if (s->callback) s->callback(WEBSOCKET_EVENT_DISCONNECTED, NULL, s->context);
    }
    return QM_EOK;
}
WEAK int qm_websocket_client_close(void *ws, uint32_t timeout)
{ (void)timeout; return qm_websocket_client_stop(ws); }
WEAK int qm_websocket_client_destroy(void *ws) { free(ws); return QM_EOK; }
WEAK qm_bool qm_websocket_client_is_connected(void *ws)
{ return ws && ((test_websocket_t *)ws)->connected; }
WEAK int qm_websocket_client_reconnect(void *ws)
{ ++test_ws_reconnects; return qm_websocket_client_stop(ws); }
WEAK int qm_websocket_client_network_update(void *ws, qm_websocket_network_state_t state)
{ (void)ws; (void)state; return QM_EOK; }
WEAK int qm_websocket_client_set_uri(void *ws, const char *uri)
{ (void)ws; snprintf(test_ws_uri, sizeof(test_ws_uri), "%s", uri); return QM_EOK; }
WEAK int qm_websocket_client_set_headers(void *ws, const char *headers)
{ (void)ws; snprintf(test_ws_headers, sizeof(test_ws_headers), "%s", headers); return QM_EOK; }
WEAK int qm_websocket_client_send_text(void *ws, const char *data, int len, uint32_t timeout)
{
    (void)ws; (void)timeout;
    assert(len >= 0 && len < (int)sizeof(test_ws_sent));
    pthread_mutex_lock(&wire_lock);
    memcpy(test_ws_sent, data, (size_t)len);
    test_ws_sent[len] = 0;
    cJSON *root = cJSON_Parse(test_ws_sent);
    const char *event = cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetObjectItem(root, "content"), "eventType"));
    for (unsigned i = 0; event && i < 3; ++i) {
        if (!strcmp(event, book_events[i])) {
            ++book_counts[i];
            snprintf(book_last[i], sizeof(book_last[i]), "%s", test_ws_sent);
        }
    }
    cJSON_Delete(root);
    pthread_mutex_unlock(&wire_lock);
    if (test_signal_sends) qm_sem_signal(&test_send_sem);
    return test_send_result < 0 ? test_send_result : len;
}
WEAK int qm_websocket_client_send_bin(void *ws, const char *data, int len, uint32_t timeout)
{ return qm_websocket_client_send_text(ws, data, len, timeout); }
