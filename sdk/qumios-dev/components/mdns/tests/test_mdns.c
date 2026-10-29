#include "mdns.h"
#include "qm_network.h"
#include "qm_string.h"
#include "qm_udp.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_PACKET_CAPACITY 1500U
#define TEST_PACKET_COUNT 3U
#define TEST_MDNS_PORT 5353U

typedef struct {
    uint8_t data[TEST_PACKET_CAPACITY];
    size_t length;
    uint16_t source_port;
} test_packet_t;

static test_packet_t packets[TEST_PACKET_COUNT];
static unsigned packet_count;
static unsigned receive_index;
static int close_count;
static uint8_t sent_query[TEST_PACKET_CAPACITY];
static uint32_t sent_query_length;
static int send_result;

void *qm_malloc(unsigned int size)
{
    return malloc(size);
}

void qm_free(void *memory)
{
    free(memory);
}

uint32_t qm_strlen(const char *text)
{
    return text ? (uint32_t)strlen(text) : 0U;
}

char *qm_strchr(const char *text, int32_t character)
{
    return (char *)strchr(text, character);
}

int32_t qm_strncasecmp(const char *left, const char *right, uint32_t length)
{
    uint32_t i;
    for (i = 0U; i < length; ++i) {
        uint8_t a = (uint8_t)left[i];
        uint8_t b = (uint8_t)right[i];
        if (a >= 'A' && a <= 'Z')
            a = (uint8_t)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z')
            b = (uint8_t)(b + ('a' - 'A'));
        if (a != b || a == 0U)
            return (int32_t)a - (int32_t)b;
    }
    return 0;
}

void *qm_memcpy(void *destination, const void *source, uint32_t length)
{
    return memcpy(destination, source, length);
}

void *qm_memset(void *memory, int32_t value, uint32_t length)
{
    return memset(memory, value, length);
}

void *qm_memchr(const void *memory, int32_t value, uint32_t length)
{
    return (void *)memchr(memory, value, length);
}

static void put_u16(test_packet_t *packet, uint16_t value)
{
    assert(packet->length + 2U <= sizeof(packet->data));
    packet->data[packet->length++] = (uint8_t)(value >> 8);
    packet->data[packet->length++] = (uint8_t)value;
}

static void put_u32(test_packet_t *packet, uint32_t value)
{
    assert(packet->length + 4U <= sizeof(packet->data));
    packet->data[packet->length++] = (uint8_t)(value >> 24);
    packet->data[packet->length++] = (uint8_t)(value >> 16);
    packet->data[packet->length++] = (uint8_t)(value >> 8);
    packet->data[packet->length++] = (uint8_t)value;
}

static size_t put_name(test_packet_t *packet, const char *name)
{
    const char *label = name;
    size_t offset = packet->length;
    while (*label) {
        const char *dot = strchr(label, '.');
        size_t length = dot ? (size_t)(dot - label) : strlen(label);
        assert(length > 0U && length <= 63U);
        assert(packet->length + length + 1U <= sizeof(packet->data));
        packet->data[packet->length++] = (uint8_t)length;
        memcpy(packet->data + packet->length, label, length);
        packet->length += length;
        if (!dot)
            break;
        label = dot + 1;
    }
    packet->data[packet->length++] = 0U;
    return offset;
}

static void put_pointer(test_packet_t *packet, uint16_t offset)
{
    assert(offset < 0x4000U);
    assert(packet->length + 2U <= sizeof(packet->data));
    packet->data[packet->length++] = (uint8_t)(0xc0U | (offset >> 8));
    packet->data[packet->length++] = (uint8_t)offset;
}

static test_packet_t *begin_packet(uint16_t answer_count)
{
    test_packet_t *packet;
    assert(packet_count < TEST_PACKET_COUNT);
    packet = &packets[packet_count++];
    memset(packet, 0, sizeof(*packet));
    packet->source_port = TEST_MDNS_PORT;
    put_u16(packet, 0U);
    put_u16(packet, 0x8400U);
    put_u16(packet, 0U);
    put_u16(packet, answer_count);
    put_u16(packet, 0U);
    put_u16(packet, 0U);
    return packet;
}

static size_t begin_record(test_packet_t *packet, const char *owner, uint16_t type, uint32_t ttl)
{
    size_t length_offset;
    put_name(packet, owner);
    put_u16(packet, type);
    put_u16(packet, 1U);
    put_u32(packet, ttl);
    length_offset = packet->length;
    put_u16(packet, 0U);
    return length_offset;
}

static size_t begin_pointer_record(test_packet_t *packet, uint16_t owner_offset, uint16_t type,
                                   uint32_t ttl)
{
    size_t length_offset;
    put_pointer(packet, owner_offset);
    put_u16(packet, type);
    put_u16(packet, 1U);
    put_u32(packet, ttl);
    length_offset = packet->length;
    put_u16(packet, 0U);
    return length_offset;
}

static void end_record(test_packet_t *packet, size_t length_offset)
{
    uint16_t length = (uint16_t)(packet->length - length_offset - 2U);
    packet->data[length_offset] = (uint8_t)(length >> 8);
    packet->data[length_offset + 1U] = (uint8_t)length;
}

static void add_named_ptr(test_packet_t *packet, const char *instance, uint32_t ttl)
{
    size_t length_offset = begin_record(packet, "_demo._tcp.local", 12U, ttl);
    put_name(packet, instance);
    end_record(packet, length_offset);
}

static void add_ptr(test_packet_t *packet, uint32_t ttl)
{
    add_named_ptr(packet, "Printer._demo._tcp.local", ttl);
}

static void add_named_srv(test_packet_t *packet, const char *owner, const char *target,
                          uint32_t ttl)
{
    size_t length_offset = begin_record(packet, owner, 33U, ttl);
    put_u16(packet, 0U);
    put_u16(packet, 0U);
    put_u16(packet, 9100U);
    put_name(packet, target);
    end_record(packet, length_offset);
}

static void add_srv(test_packet_t *packet, uint32_t ttl)
{
    add_named_srv(packet, "Printer._demo._tcp.local", "printer.local", ttl);
}

static void add_txt(test_packet_t *packet, uint32_t ttl, int oversized)
{
    static const char path[] = "PaTh=/queue";
    static const char flag[] = "flag";
    size_t length_offset = begin_record(packet, "Printer._demo._tcp.local", 16U, ttl);
    if (oversized) {
        unsigned i;
        packet->data[packet->length++] = MDNS_TXT_DATA_LEN;
        for (i = 0U; i < MDNS_TXT_DATA_LEN; ++i)
            packet->data[packet->length++] = 'x';
    } else {
        packet->data[packet->length++] = (uint8_t)(sizeof(path) - 1U);
        memcpy(packet->data + packet->length, path, sizeof(path) - 1U);
        packet->length += sizeof(path) - 1U;
        packet->data[packet->length++] = (uint8_t)(sizeof(flag) - 1U);
        memcpy(packet->data + packet->length, flag, sizeof(flag) - 1U);
        packet->length += sizeof(flag) - 1U;
    }
    end_record(packet, length_offset);
}

static void add_a(test_packet_t *packet, uint32_t ttl)
{
    size_t length_offset = begin_record(packet, "printer.local", 1U, ttl);
    packet->data[packet->length++] = 192U;
    packet->data[packet->length++] = 168U;
    packet->data[packet->length++] = 1U;
    packet->data[packet->length++] = 9U;
    end_record(packet, length_offset);
}

static void reset_transport(void)
{
    memset(packets, 0, sizeof(packets));
    packet_count = 0U;
    receive_index = 0U;
    close_count = 0;
    sent_query_length = 0U;
    send_result = QM_EOK;
    memset(sent_query, 0, sizeof(sent_query));
}

int qm_udp_server_create(char *host, uint16_t port)
{
    (void)host;
    (void)port;
    return 7;
}

int qm_udp_sendto(int socket, const char *buffer, uint32_t length, qm_netaddr_t *address,
                  uint32_t timeout_ms)
{
    (void)address;
    (void)timeout_ms;
    assert(length <= sizeof(sent_query));
    memcpy(sent_query, buffer, length);
    sent_query_length = length;
    if (send_result != QM_EOK)
        return send_result;
    return socket == 7 ? (int)length : -QM_EIO;
}

int qm_udp_recvfrom(int socket, char *buffer, uint32_t length, qm_netaddr_t *address,
                    uint32_t timeout_ms)
{
    test_packet_t *packet;
    (void)timeout_ms;
    assert(socket == 7);
    if (receive_index >= packet_count)
        return -QM_ETIMEOUT;
    packet = &packets[receive_index++];
    assert(packet->length <= length);
    memcpy(buffer, packet->data, packet->length);
    address->port = packet->source_port;
    return (int)packet->length;
}

int qm_udp_close(int socket)
{
    assert(socket == 7);
    ++close_count;
    return QM_EOK;
}

static int validate_service(const mdns_service_t *service, void *user)
{
    char value[16];
    int *calls = (int *)user;
    ++*calls;
    assert(strcmp(service->host, "192.168.1.9") == 0);
    assert(strcmp(service->instance, "Printer._demo._tcp.local") == 0);
    assert(service->port == 9100U);
    assert(service->txt_truncated == 0U);
    assert(mdns_txt_get(service, "path", value, sizeof(value)) == QM_EOK);
    assert(strcmp(value, "/queue") == 0);
    assert(mdns_txt_get(service, "FLAG", value, sizeof(value)) == QM_EOK);
    assert(value[0] == '\0');
    return QM_EOK;
}

static int count_service(const mdns_service_t *service, void *user)
{
    int *calls = (int *)user;
    (void)service;
    ++*calls;
    return QM_EOK;
}

static int stop_service(const mdns_service_t *service, void *user)
{
    (void)service;
    (void)user;
    return 7;
}

static int validate_truncated_txt(const mdns_service_t *service, void *user)
{
    char value[4];
    int *calls = (int *)user;
    ++*calls;
    assert(service->txt_truncated != 0U);
    assert(service->txt_len == 0U);
    assert(mdns_txt_get(service, "x", value, sizeof(value)) == -QM_EFULL);
    return QM_EOK;
}

static int validate_slot_priority(const mdns_service_t *service, void *user)
{
    int *calls = (int *)user;
    ++*calls;
    assert(strcmp(service->host, "192.168.1.9") == 0);
    assert(strcmp(service->instance, "Printer._demo._tcp.local") == 0);
    assert(service->port == 9100U);
    return QM_EOK;
}

static int validate_compressed_service(const mdns_service_t *service, void *user)
{
    char value[16];
    int *calls = (int *)user;
    ++*calls;
    assert(strcmp(service->host, "192.168.1.9") == 0);
    assert(strcmp(service->instance, "Printer._demo._tcp.local") == 0);
    assert(service->port == 9100U);
    assert(mdns_txt_get(service, "path", value, sizeof(value)) == QM_EOK);
    assert(strcmp(value, "/queue") == 0);
    return QM_EOK;
}

static int validate_target_host(const mdns_service_t *service, void *user)
{
    int *calls = (int *)user;
    ++*calls;
    assert(strcmp(service->host, "printer.local") == 0);
    assert(strcmp(service->instance, "Printer._demo._tcp.local") == 0);
    assert(service->port == 9100U);
    return QM_EOK;
}

static void make_four_label_name(char *name, size_t capacity, size_t final_label_length)
{
    size_t pos = 0U;
    unsigned i;

    for (i = 0U; i < 4U; ++i) {
        size_t label_length = i == 3U ? final_label_length : 63U;
        assert(pos + label_length + 1U <= capacity);
        memset(name + pos, (int)('a' + i), label_length);
        pos += label_length;
        name[pos++] = i == 3U ? '\0' : '.';
    }
}

static void test_query_name_wire_length_limit(void)
{
    char valid_name[MDNS_NAME_LEN - 2U];
    char oversized_name[MDNS_NAME_LEN - 1U];

    make_four_label_name(valid_name, sizeof(valid_name), 61U);
    reset_transport();
    assert(mdns_query(valid_name, 1000U, count_service, NULL) == -QM_ETIMEOUT);
    assert(sent_query_length > 0U);
    assert(close_count == 1);

    make_four_label_name(oversized_name, sizeof(oversized_name), 62U);
    reset_transport();
    assert(mdns_query(oversized_name, 1000U, count_service, NULL) == -QM_EINVAL);
    assert(sent_query_length == 0U);
    assert(close_count == 0);
}

static void test_cross_packet_record_order(void)
{
    test_packet_t *packet;
    int calls = 0;
    reset_transport();
    packet = begin_packet(3U);
    add_srv(packet, 120U);
    add_txt(packet, 120U, 0);
    add_a(packet, 120U);
    packet = begin_packet(1U);
    add_ptr(packet, 120U);
    assert(mdns_query("_demo._tcp.local", 1000U, validate_service, &calls) == QM_EOK);
    assert(calls == 1);
    assert(close_count == 1);
    assert(sent_query_length >= 4U);
    assert(sent_query[sent_query_length - 4U] == 0U);
    assert(sent_query[sent_query_length - 3U] == 12U);
    assert(sent_query[sent_query_length - 2U] == 0x80U);
    assert(sent_query[sent_query_length - 1U] == 1U);
}

static void test_goodbye_removes_service(void)
{
    test_packet_t *packet;
    int calls = 0;
    reset_transport();
    packet = begin_packet(4U);
    add_ptr(packet, 120U);
    add_srv(packet, 120U);
    add_txt(packet, 120U, 0);
    add_a(packet, 120U);
    packet = begin_packet(1U);
    add_ptr(packet, 0U);
    assert(mdns_query("_demo._tcp.local", 1000U, count_service, &calls) == -QM_ENOENT);
    assert(calls == 0);
}

static void test_invalid_packet_does_not_hide_valid_response(void)
{
    test_packet_t *packet;
    int calls = 0;
    reset_transport();
    packet = begin_packet(0U);
    packet->source_port = 1234U;
    packet = begin_packet(0U);
    packet->data[1] = 1U;
    packet = begin_packet(4U);
    add_ptr(packet, 120U);
    add_srv(packet, 120U);
    add_txt(packet, 120U, 0);
    add_a(packet, 120U);
    assert(mdns_query("_demo._tcp.local", 1000U, validate_service, &calls) == QM_EOK);
    assert(calls == 1);
}

static void test_ptr_replaces_pending_entry(void)
{
    test_packet_t *packet;
    int calls = 0;

    reset_transport();
    packet = begin_packet(7U);
    add_named_srv(packet, "Fake1._demo._tcp.local", "fake1.local", 120U);
    add_named_srv(packet, "Fake2._demo._tcp.local", "fake2.local", 120U);
    add_named_srv(packet, "Fake3._demo._tcp.local", "fake3.local", 120U);
    add_named_srv(packet, "Fake4._demo._tcp.local", "fake4.local", 120U);
    add_ptr(packet, 120U);
    add_srv(packet, 120U);
    add_a(packet, 120U);
    assert(mdns_query("_demo._tcp.local", 1000U, validate_slot_priority, &calls) == QM_EOK);
    assert(calls == 1);
}

static void test_compressed_names(void)
{
    test_packet_t *packet;
    size_t instance_offset;
    size_t target_offset;
    size_t length_offset;
    int calls = 0;

    reset_transport();
    packet = begin_packet(4U);

    length_offset = begin_record(packet, "_demo._tcp.local", 12U, 120U);
    instance_offset = packet->length;
    put_name(packet, "Printer._demo._tcp.local");
    end_record(packet, length_offset);

    length_offset = begin_pointer_record(packet, (uint16_t)instance_offset, 33U, 120U);
    put_u16(packet, 0U);
    put_u16(packet, 0U);
    put_u16(packet, 9100U);
    target_offset = packet->length;
    put_name(packet, "printer.local");
    end_record(packet, length_offset);

    length_offset = begin_pointer_record(packet, (uint16_t)instance_offset, 16U, 120U);
    packet->data[packet->length++] = 11U;
    memcpy(packet->data + packet->length, "PaTh=/queue", 11U);
    packet->length += 11U;
    end_record(packet, length_offset);

    length_offset = begin_pointer_record(packet, (uint16_t)target_offset, 1U, 120U);
    packet->data[packet->length++] = 192U;
    packet->data[packet->length++] = 168U;
    packet->data[packet->length++] = 1U;
    packet->data[packet->length++] = 9U;
    end_record(packet, length_offset);

    assert(mdns_query("_demo._tcp.local", 1000U, validate_compressed_service, &calls) == QM_EOK);
    assert(calls == 1);
}

static void test_host_falls_back_to_srv_target(void)
{
    test_packet_t *packet;
    int calls = 0;

    reset_transport();
    packet = begin_packet(2U);
    add_ptr(packet, 120U);
    add_srv(packet, 120U);
    assert(mdns_query("_demo._tcp.local", 1000U, validate_target_host, &calls) == QM_EOK);
    assert(calls == 1);
}

static void test_send_error_is_propagated(void)
{
    reset_transport();
    send_result = -QM_ETIMEOUT;
    assert(mdns_query("_demo._tcp.local", 1000U, count_service, NULL) == -QM_ETIMEOUT);
    assert(close_count == 1);
}

static void test_malformed_packet_does_not_leak_state(void)
{
    test_packet_t *packet;
    size_t length_offset;
    int calls = 0;

    reset_transport();
    packet = begin_packet(2U);
    add_ptr(packet, 120U);
    length_offset = begin_record(packet, "Printer._demo._tcp.local", 16U, 120U);
    packet->data[packet->length++] = 3U;
    packet->data[packet->length++] = 'x';
    end_record(packet, length_offset);

    packet = begin_packet(2U);
    add_srv(packet, 120U);
    add_a(packet, 120U);
    assert(mdns_query("_demo._tcp.local", 1000U, count_service, &calls) == -QM_ENOENT);
    assert(calls == 0);
}

static void test_forward_compression_pointer_is_rejected(void)
{
    test_packet_t *packet;
    size_t owner_offset;
    size_t length_offset;
    int calls = 0;

    reset_transport();
    packet = begin_packet(1U);
    owner_offset = packet->length;
    length_offset = begin_pointer_record(packet, (uint16_t)owner_offset, 1U, 120U);
    packet->data[packet->length++] = 192U;
    packet->data[packet->length++] = 168U;
    packet->data[packet->length++] = 1U;
    packet->data[packet->length++] = 9U;
    end_record(packet, length_offset);

    packet = begin_packet(3U);
    add_ptr(packet, 120U);
    add_srv(packet, 120U);
    add_a(packet, 120U);
    assert(mdns_query("_demo._tcp.local", 1000U, validate_slot_priority, &calls) == QM_EOK);
    assert(calls == 1);
}

static void test_truncated_txt_and_callback_result(void)
{
    test_packet_t *packet;
    int calls = 0;
    reset_transport();
    packet = begin_packet(4U);
    add_ptr(packet, 120U);
    add_srv(packet, 120U);
    add_txt(packet, 120U, 1);
    add_a(packet, 120U);
    assert(mdns_query("_demo._tcp.local", 1000U, validate_truncated_txt, &calls) == QM_EOK);
    assert(calls == 1);

    reset_transport();
    packet = begin_packet(3U);
    add_ptr(packet, 120U);
    add_srv(packet, 120U);
    add_a(packet, 120U);
    assert(mdns_query("_demo._tcp.local", 1000U, stop_service, NULL) == 7);
}

static void test_txt_validation(void)
{
    mdns_service_t service;
    char value[8];
    memset(&service, 0, sizeof(service));
    service.txt_len = 2U;
    service.txt[0] = 3U;
    service.txt[1] = 'x';
    assert(mdns_txt_get(&service, "x", value, sizeof(value)) == -QM_EINVAL);
    service.txt_len = MDNS_TXT_DATA_LEN + 1U;
    assert(mdns_txt_get(&service, "x", value, sizeof(value)) == -QM_EINVAL);
}

int main(void)
{
    test_query_name_wire_length_limit();
    test_cross_packet_record_order();
    test_goodbye_removes_service();
    test_invalid_packet_does_not_hide_valid_response();
    test_ptr_replaces_pending_entry();
    test_compressed_names();
    test_host_falls_back_to_srv_target();
    test_send_error_is_propagated();
    test_malformed_packet_does_not_leak_state();
    test_forward_compression_pointer_is_rejected();
    test_truncated_txt_and_callback_result();
    test_txt_validation();
    puts("mdns tests passed");
    return 0;
}
