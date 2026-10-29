/**
 * @file comtrace_discovery.c
 * @brief ComTrace-specific DNS-SD discovery and TXT validation.
 */
#include "comtrace_discovery.h"
#include "mdns.h"
#include "qm_string.h"

typedef struct {
    const char *protocol;
    comtrace_endpoint_cb_t callback;
    void *user;
} discovery_context_t;

static const char *const service_names[] = {
    "_comtrace-log._tcp.local.",
    "_comtrace-log._udp.local.",
    "_comtrace-update._tcp.local.",
};

static const char *const protocol_names[] = {"celp/1", "celp/1", "ceup/1"};

static uint16_t parse_u16(const char *text)
{
    uint32_t value = 0U;
    size_t i;
    size_t length = qm_strlen(text);
    if (length == 0U || length > 5U)
        return 0U;
    for (i = 0U; i < length; ++i) {
        if (text[i] < '0' || text[i] > '9')
            return 0U;
        value = value * 10U + (uint32_t)(text[i] - '0');
    }
    return value == 0U || value > COMTRACE_MAX_FRAME ? 0U : (uint16_t)value;
}

static void copy_string(char *destination, size_t capacity, const char *source)
{
    size_t length = qm_strlen(source);
    if (length >= capacity)
        length = capacity - 1U;
    qm_memcpy(destination, source, (uint32_t)length);
    destination[length] = '\0';
}

static int discovered(const mdns_service_t *service, void *user)
{
    discovery_context_t *context = (discovery_context_t *)user;
    comtrace_endpoint_t endpoint;
    char protocol[16];
    char encoding[16];
    char frame[24];
    char max_frame[8];

    if (!service || !context || !context->callback || service->txt_truncated ||
        service->port == 0U || !service->host[0] ||
        mdns_txt_get(service, "proto", protocol, sizeof(protocol)) != QM_EOK ||
        qm_strcmp(protocol, context->protocol) != 0 ||
        mdns_txt_get(service, "encoding", encoding, sizeof(encoding)) != QM_EOK ||
        qm_strcmp(encoding, "cbor") != 0 ||
        mdns_txt_get(service, "frame", frame, sizeof(frame)) != QM_EOK ||
        qm_strcmp(frame, "u16be+cbor") != 0 ||
        mdns_txt_get(service, "max_frame", max_frame, sizeof(max_frame)) != QM_EOK)
        return QM_EOK;
    qm_memset(&endpoint, 0, sizeof(endpoint));
    endpoint.max_frame = parse_u16(max_frame);
    if (endpoint.max_frame == 0U)
        return QM_EOK;
    copy_string(endpoint.host, sizeof(endpoint.host), service->host);
    endpoint.port = service->port;
    (void)mdns_txt_get(service, "instance_id", endpoint.instance_id, sizeof(endpoint.instance_id));
    return context->callback(&endpoint, context->user);
}

int comtrace_discover(comtrace_service_t service, uint32_t timeout_ms,
                      comtrace_endpoint_cb_t callback, void *user)
{
    discovery_context_t context;
    if (service > COMTRACE_SERVICE_OTA_TCP || timeout_ms == 0U || !callback)
        return -QM_EINVAL;
    context.protocol = protocol_names[service];
    context.callback = callback;
    context.user = user;
    return mdns_query(service_names[service], timeout_ms, discovered, &context);
}
