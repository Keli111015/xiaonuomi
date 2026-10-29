#include "qm_http_client.h"
#define QM_HTTP_CLIENT_INTERNAL
#include "qm_http_client_transport.h"
#include "qm_kernel.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *response;
    size_t offset, cancel_offset;
    unsigned connects, closes, reads, writes, data_events;
    qm_bool cancelled, cancel_connect, cancel_write;
} transport_test_t;
static transport_test_t test;

void *qm_malloc(unsigned int n) { return malloc(n); }
void *qm_calloc(unsigned int n, unsigned int size) { return calloc(n, size); }
void *qm_realloc(void *p, unsigned int n) { return realloc(p, n); }
void qm_free(void *p) { free(p); }
uint32_t qm_now_ms(void) { return 1U; }
void qm_srandom(unsigned int seed) { srand(seed); }
uint32_t qm_random_get(uint32_t region) { return region == 0U ? 0U : (uint32_t)rand() % region; }
int qm_snprintf(char *out, const int size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(out, (size_t)size, format, args);
    va_end(args);
    return ret;
}

static qm_bool cancelled(void *user)
{
    assert(user == &test);
    return test.cancelled;
}

static int event(const qm_http_client_event_t *event)
{
    if (event->event_id == QM_HTTP_CLIENT_EVENT_DATA) ++test.data_events;
    return QM_EOK;
}

static void *connect_mock(const char *host, uint16_t port, void *config)
{
    (void)config;
    assert(strcmp(host, "test.local") == 0 && port == 80U);
    ++test.connects;
    if (test.cancel_connect) test.cancelled = QM_TRUE;
    return &test;
}

static int read_mock(void *handle, char *buffer, int size, uint32_t timeout_ms)
{
    assert(handle == &test && size > 0 && timeout_ms == 20U);
    ++test.reads;
    if (test.response[test.offset] == '\0') return -QM_EIO;
    /* One byte per transfer forces header/chunk loops through checkpoints. */
    buffer[0] = test.response[test.offset++];
    if (test.cancel_offset != 0U && test.offset >= test.cancel_offset)
        test.cancelled = QM_TRUE;
    return 1;
}

static int write_mock(void *handle, const char *buffer, int size, uint32_t timeout_ms)
{
    (void)buffer;
    assert(handle == &test && size > 0 && timeout_ms == 20U);
    ++test.writes;
    if (test.cancel_write) test.cancelled = QM_TRUE;
    return 1;
}

static int close_mock(void *handle)
{
    assert(handle == &test);
    ++test.closes;
    return QM_EOK;
}

void qm_http_client_transport_tcp_init(qm_http_client_transport_t *transport)
{
    memset(transport, 0, sizeof(*transport));
    transport->connect = connect_mock;
    transport->read = read_mock;
    transport->write = write_mock;
    transport->close = close_mock;
}

void qm_http_client_transport_tls_init(qm_http_client_transport_t *transport,
                                      const qm_http_tls_config_t *tls)
{ (void)tls; qm_http_client_transport_tcp_init(transport); }

static qm_http_client_handle_t create(const char *response)
{
    qm_http_client_config_t config = {0};
    memset(&test, 0, sizeof(test));
    test.response = response;
    config.url = "http://test.local/test";
    config.timeout_ms = 20U;
    config.user = &test;
    config.on_event = event;
    config.interrupted = cancelled;
    qm_http_client_handle_t client = qm_http_client_init(&config);
    assert(client != NULL);
    return client;
}

int main(void)
{
    const char *fixed = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc";
    const char *chunked = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\na\r\n2\r\nbc\r\n0\r\n\r\n";
    char buffer[8];
    qm_http_client_handle_t client = create(fixed);
    test.cancelled = QM_TRUE;
    assert(qm_http_client_open(client, 0) == -QM_EINTR && test.connects == 0U);
    assert(qm_http_client_cleanup(client) == QM_EOK);

    client = create(fixed);
    test.cancel_connect = QM_TRUE;
    assert(qm_http_client_open(client, 0) == -QM_EINTR && test.closes == 1U);
    assert(test.writes == 0U);
    assert(qm_http_client_cleanup(client) == QM_EOK);

    client = create(fixed);
    test.cancel_write = QM_TRUE;
    assert(qm_http_client_open(client, 0) == -QM_EINTR && test.writes == 1U);
    assert(qm_http_client_cleanup(client) == QM_EOK && test.closes == 1U);

    client = create(fixed);
    assert(qm_http_client_open(client, 0) == QM_EOK);
    test.cancel_offset = 5U;
    assert(qm_http_client_fetch_headers(client) == -QM_EINTR && test.reads == 5U);
    assert(qm_http_client_cleanup(client) == QM_EOK && test.closes == 1U);

    for (unsigned mode = 0U; mode < 3U; ++mode) {
        client = create(mode == 1U ? chunked : fixed);
        assert(qm_http_client_open(client, 0) == QM_EOK);
        assert(qm_http_client_fetch_headers(client) == QM_EOK);
        test.cancel_offset = test.offset + (mode == 1U ? 7U : 1U);
        int ret = mode == 2U ? qm_http_client_drain_response_body(client) :
            qm_http_client_read(client, buffer, sizeof(buffer));
        assert(ret == -QM_EINTR && test.data_events == 0U);
        assert(qm_http_client_cleanup(client) == QM_EOK && test.closes == 1U);
    }

    client = create(fixed);
    assert(qm_http_client_open(client, 0) == QM_EOK);
    assert(qm_http_client_fetch_headers(client) == QM_EOK);
    assert(qm_http_client_read(client, buffer, sizeof(buffer)) == 1 && buffer[0] == 'a');
    assert(qm_http_client_cleanup(client) == QM_EOK && test.closes == 1U);

    qm_http_client_request_t request = {0};
    qm_http_client_response_t response;
    memset(&test, 0, sizeof(test));
    test.cancelled = QM_TRUE;
    request.url = "http://test.local/test";
    request.interrupted = cancelled;
    request.user = &test;
    assert(qm_http_client_execute(&request, &response) == -QM_EINTR);
    assert(test.connects == 0U);
    puts("PASS: cancellation before/during connection, writes, headers, body, chunks and drain");
    return 0;
}
