#include "ymodem.h"
#include "qm_crc.h"
#include "qm_config.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_IMAGE_CAPACITY 32768U
#define TEST_STREAM_CAPACITY 40960U
#define TEST_TX_CAPACITY 4096U
#define TEST_WRAP_IMAGE_SIZE (255U * 128U + 1U)
#define TEST_PROGRESS_IMAGE_SIZE (1024U + 17U)

#define YMODEM_SOH 0x01U
#define YMODEM_STX 0x02U
#define YMODEM_EOT 0x04U
#define YMODEM_ACK 0x06U
#define YMODEM_NAK 0x15U
#define YMODEM_CAN 0x18U
#define YMODEM_CRC 0x43U

typedef struct {
    uint8_t rx[TEST_STREAM_CAPACITY];
    uint32_t rx_length;
    uint32_t rx_offset;
    uint8_t tx[TEST_TX_CAPACITY];
    uint32_t tx_length;
    uint32_t read_fragment;
    uint32_t write_fragment;
    uint32_t ack_count;
    uint32_t fail_ack_number;
    uint32_t timeout_after_offset;
    uint8_t timeout_once;
} test_transport_t;

typedef struct {
    uint8_t data[TEST_IMAGE_CAPACITY];
    uint32_t size;
    uint32_t begin_calls;
    uint32_t write_calls;
    uint32_t finish_calls;
    uint32_t abort_calls;
    uint32_t fail_write_call;
    qm_err_t finish_error;
    char name[65];
    uint32_t advertised_size;
} test_storage_t;

typedef struct {
    uint32_t calls;
    ymodem_progress_t first;
    ymodem_progress_t last;
    const test_storage_t *storage;
} test_progress_t;

static uint32_t fake_now_ms;

void *qm_malloc(unsigned int size)
{
    return malloc(size);
}

void qm_free(void *memory)
{
    free(memory);
}

void *qm_memcpy(void *destination, const void *source, uint32_t length)
{
    return memcpy(destination, source, length);
}

void *qm_memset(void *memory, int32_t value, uint32_t length)
{
    return memset(memory, value, length);
}

uint32_t qm_now_ms(void)
{
    return fake_now_ms;
}

int qm_msleep(int ms)
{
    if (ms > 0) {
        fake_now_ms += (uint32_t)ms;
    }
    return 0;
}

static void stream_push(test_transport_t *transport, const uint8_t *data,
                        uint32_t length)
{
    assert(transport->rx_length + length <= sizeof(transport->rx));
    memcpy(&transport->rx[transport->rx_length], data, length);
    transport->rx_length += length;
}

static void stream_push_byte(test_transport_t *transport, uint8_t value)
{
    stream_push(transport, &value, 1U);
}

static uint16_t make_packet(uint8_t *packet, uint8_t start, uint8_t sequence,
                            const uint8_t *payload, uint16_t payload_length)
{
    uint16_t crc = 0U;

    assert(packet != NULL);
    assert(payload_length == 128U || payload_length == 1024U);
    packet[0] = start;
    packet[1] = sequence;
    packet[2] = (uint8_t)~sequence;
    memset(&packet[3], 0, payload_length);
    if (payload != NULL) {
        memcpy(&packet[3], payload, payload_length);
    }
    crc = qm_crc16_xmodem(&packet[3], payload_length);
    packet[3U + payload_length] = (uint8_t)(crc >> 8);
    packet[4U + payload_length] = (uint8_t)crc;
    return (uint16_t)(payload_length + 5U);
}

static void push_header_format(test_transport_t *transport, const char *name,
                               uint32_t size, uint8_t start,
                               uint8_t with_metadata)
{
    uint8_t payload[1024] = {0};
    uint8_t packet[1029] = {0};
    int length = 0;
    uint16_t packet_length = 0U;
    uint16_t payload_length = (start == YMODEM_STX) ? 1024U : 128U;

    if (with_metadata != 0U) {
        length = snprintf((char *)payload, sizeof(payload), "%s%c%u 0 0%c",
                          name, '\0', (unsigned int)size, '\0');
    } else {
        length = snprintf((char *)payload, sizeof(payload), "%s%c%u%c", name,
                          '\0', (unsigned int)size, '\0');
    }
    assert(length > 0 && (uint32_t)length < sizeof(payload));
    packet_length = make_packet(packet, start, 0U, payload, payload_length);
    stream_push(transport, packet, packet_length);
}

static void push_header(test_transport_t *transport, const char *name,
                        uint32_t size)
{
    push_header_format(transport, name, size, YMODEM_SOH, 0U);
}

static void push_data(test_transport_t *transport, uint8_t start,
                      uint8_t sequence, const uint8_t *data,
                      uint16_t payload_length, uint16_t actual_length)
{
    uint8_t payload[1024] = {0};
    uint8_t packet[1029] = {0};
    uint16_t packet_length = 0U;

    assert(actual_length <= payload_length);
    memset(payload, 0x1A, payload_length);
    memcpy(payload, data, actual_length);
    packet_length = make_packet(packet, start, sequence, payload,
                                 payload_length);
    stream_push(transport, packet, packet_length);
}

static void push_empty_header(test_transport_t *transport)
{
    uint8_t packet[133] = {0};
    uint16_t packet_length = 0U;

    packet_length = make_packet(packet, YMODEM_SOH, 0U, NULL, 128U);
    stream_push(transport, packet, packet_length);
}

static int32_t transport_write(void *user, const uint8_t *data,
                               uint32_t length, uint32_t timeout_ms)
{
    test_transport_t *transport = (test_transport_t *)user;
    uint32_t count = length;

    (void)timeout_ms;
    if (transport->write_fragment != 0U && count > transport->write_fragment) {
        count = transport->write_fragment;
    }
    if (length == 1U && data[0] == YMODEM_ACK) {
        transport->ack_count++;
        if (transport->fail_ack_number != 0U &&
            transport->ack_count == transport->fail_ack_number) {
            return -QM_EIO;
        }
    }
    assert(transport->tx_length + count <= sizeof(transport->tx));
    memcpy(&transport->tx[transport->tx_length], data, count);
    transport->tx_length += count;
    return (int32_t)count;
}

static int32_t transport_read(void *user, uint8_t *data, uint32_t length,
                              uint32_t timeout_ms)
{
    test_transport_t *transport = (test_transport_t *)user;
    uint32_t available = transport->rx_length - transport->rx_offset;
    uint32_t count = available < length ? available : length;

    if (transport->timeout_once != 0U &&
        transport->rx_offset >= transport->timeout_after_offset) {
        transport->timeout_once = 0U;
        fake_now_ms += timeout_ms;
        return 0;
    }
    if (transport->read_fragment != 0U && count > transport->read_fragment) {
        count = transport->read_fragment;
    }
    if (count == 0U) {
        fake_now_ms += timeout_ms;
        return 0;
    }
    memcpy(data, &transport->rx[transport->rx_offset], count);
    transport->rx_offset += count;
    return (int32_t)count;
}

static qm_err_t storage_begin(const ymodem_file_info_t *file, void *user)
{
    test_storage_t *storage = (test_storage_t *)user;

    assert(file != NULL);
    assert(storage != NULL);
    storage->begin_calls++;
    storage->advertised_size = file->total_size;
    strncpy(storage->name, file->name, sizeof(storage->name) - 1U);
    storage->name[sizeof(storage->name) - 1U] = '\0';
    return QM_EOK;
}

static qm_err_t storage_write(uint32_t offset, const uint8_t *data,
                              uint32_t length, void *user)
{
    test_storage_t *storage = (test_storage_t *)user;

    assert(storage != NULL);
    storage->write_calls++;
    if (storage->fail_write_call != 0U &&
        storage->write_calls == storage->fail_write_call) {
        return -QM_EIO;
    }
    assert(offset == storage->size);
    assert(offset + length <= sizeof(storage->data));
    memcpy(&storage->data[offset], data, length);
    storage->size += length;
    return QM_EOK;
}

static qm_err_t storage_finish(const ymodem_file_info_t *file, void *user)
{
    test_storage_t *storage = (test_storage_t *)user;

    assert(file != NULL);
    assert(storage != NULL);
    storage->finish_calls++;
    return storage->finish_error;
}

static void storage_abort(void *user)
{
    test_storage_t *storage = (test_storage_t *)user;

    assert(storage != NULL);
    storage->abort_calls++;
}

static qm_err_t cancel_progress(const ymodem_progress_t *progress, void *user)
{
    test_progress_t *state = (test_progress_t *)user;

    assert(progress != NULL);
    assert(state != NULL);
    state->calls++;
    state->last = *progress;
    return -QM_EINTR;
}

static qm_err_t record_progress(const ymodem_progress_t *progress, void *user)
{
    test_progress_t *state = (test_progress_t *)user;

    assert(progress != NULL);
    assert(state != NULL);
    assert(state->storage != NULL);
    assert(progress->transferred == state->storage->size);
    assert(progress->total_size == state->storage->advertised_size);
    assert(progress->transferred > state->last.transferred);
    assert(progress->transferred <= progress->total_size);
    assert(state->storage->finish_calls == 0U);
    if (state->calls == 0U) {
        state->first = *progress;
    }
    state->last = *progress;
    state->calls++;
    return QM_EOK;
}

static ymodem_handle_t new_receiver(test_transport_t *transport,
                                    test_storage_t *storage,
                                    uint8_t retry_max)
{
    ymodem_config_t config = {0};

    config.write = transport_write;
    config.read = transport_read;
    config.transport_user = transport;
    config.storage.begin = storage_begin;
    config.storage.write = storage_write;
    config.storage.finish = storage_finish;
    config.storage.abort = storage_abort;
    config.storage.user = storage;
    config.handshake_timeout_ms = 10U;
    config.packet_timeout_ms = 10U;
    config.retry_max = retry_max;
    config.max_file_size = TEST_IMAGE_CAPACITY;
    return ymodem_create(&config);
}

static void prepare_transport(test_transport_t *transport)
{
    memset(transport, 0, sizeof(*transport));
    transport->read_fragment = 7U;
    transport->write_fragment = 3U;
}

static void fill_image(uint8_t *image, uint32_t size)
{
    uint32_t index = 0U;

    for (index = 0U; index < size; index++) {
        image[index] = (uint8_t)(index * 17U + 3U);
    }
}

static uint32_t count_tx(const test_transport_t *transport, uint8_t value)
{
    uint32_t index = 0U;
    uint32_t count = 0U;

    for (index = 0U; index < transport->tx_length; index++) {
        if (transport->tx[index] == value) {
            count++;
        }
    }
    return count;
}

static void append_standard_end(test_transport_t *transport)
{
    stream_push_byte(transport, YMODEM_EOT);
    stream_push_byte(transport, YMODEM_EOT);
    push_empty_header(transport);
}

static void test_mixed_packets_and_padding(void)
{
    uint8_t image[1200] = {0};
    test_transport_t transport = {0};
    test_storage_t storage = {0};
    ymodem_handle_t receiver = NULL;

    fill_image(image, sizeof(image));
    prepare_transport(&transport);
    push_header(&transport, "firmware.bin", sizeof(image));
    push_data(&transport, YMODEM_STX, 1U, image, 1024U, 1024U);
    push_data(&transport, YMODEM_SOH, 2U, &image[1024], 128U, 128U);
    push_data(&transport, YMODEM_SOH, 3U, &image[1152], 128U, 48U);
    append_standard_end(&transport);
    receiver = new_receiver(&transport, &storage, 2U);
    assert(receiver != NULL);
    assert(ymodem_receive(receiver) == QM_EOK);
    assert(storage.begin_calls == 1U);
    assert(storage.write_calls == 3U);
    assert(storage.finish_calls == 1U);
    assert(storage.abort_calls == 0U);
    assert(storage.size == sizeof(image));
    assert(storage.advertised_size == sizeof(image));
    assert(strcmp(storage.name, "firmware.bin") == 0);
    assert(memcmp(storage.data, image, sizeof(image)) == 0);
    assert(count_tx(&transport, YMODEM_CRC) >= 2U);
    assert(count_tx(&transport, YMODEM_ACK) >= 6U);
    ymodem_destroy(receiver);
}

static void test_bad_crc_and_duplicate(void)
{
    uint8_t image[128] = {0};
    uint8_t packet[133] = {0};
    test_transport_t transport = {0};
    test_storage_t storage = {0};
    ymodem_handle_t receiver = NULL;
    uint16_t packet_length = 0U;

    fill_image(image, sizeof(image));
    prepare_transport(&transport);
    push_header(&transport, "retry.bin", sizeof(image));
    packet_length = make_packet(packet, YMODEM_SOH, 1U, image, 128U);
    packet[packet_length - 1U] ^= 0x55U;
    stream_push(&transport, packet, packet_length);
    push_data(&transport, YMODEM_SOH, 1U, image, 128U, 128U);
    push_data(&transport, YMODEM_SOH, 1U, image, 128U, 128U);
    append_standard_end(&transport);
    receiver = new_receiver(&transport, &storage, 2U);
    assert(receiver != NULL);
    assert(ymodem_receive(receiver) == QM_EOK);
    assert(storage.write_calls == 1U);
    assert(storage.size == sizeof(image));
    assert(count_tx(&transport, YMODEM_NAK) >= 1U);
    ymodem_destroy(receiver);
}

static void test_retry_budget_is_per_block(void)
{
    uint8_t image[256] = {0};
    uint8_t packet[133] = {0};
    test_transport_t transport = {0};
    test_storage_t storage = {0};
    ymodem_handle_t receiver = NULL;
    uint16_t packet_length = 0U;

    fill_image(image, sizeof(image));
    prepare_transport(&transport);
    push_header(&transport, "per-block.bin", sizeof(image));
    packet_length = make_packet(packet, YMODEM_SOH, 1U, image, 128U);
    packet[packet_length - 1U] ^= 0x55U;
    stream_push(&transport, packet, packet_length);
    push_data(&transport, YMODEM_SOH, 1U, image, 128U, 128U);
    packet_length = make_packet(packet, YMODEM_SOH, 2U, &image[128], 128U);
    packet[packet_length - 1U] ^= 0xAAU;
    stream_push(&transport, packet, packet_length);
    push_data(&transport, YMODEM_SOH, 2U, &image[128], 128U, 128U);
    append_standard_end(&transport);
    receiver = new_receiver(&transport, &storage, 1U);
    assert(receiver != NULL);
    assert(ymodem_receive(receiver) == QM_EOK);
    assert(storage.write_calls == 2U);
    assert(storage.size == sizeof(image));
    ymodem_destroy(receiver);
}

static void test_partial_frame_timeout_discards_tail(void)
{
    uint8_t image[128] = {0};
    test_transport_t transport = {0};
    test_storage_t storage = {0};
    ymodem_handle_t receiver = NULL;

    fill_image(image, sizeof(image));
    image[0] = YMODEM_CAN;
    prepare_transport(&transport);
    push_header(&transport, "partial.bin", sizeof(image));
    transport.timeout_after_offset = transport.rx_length + 8U;
    transport.timeout_once = 1U;
    push_data(&transport, YMODEM_SOH, 1U, image, 128U, 128U);
    push_data(&transport, YMODEM_SOH, 1U, image, 128U, 128U);
    append_standard_end(&transport);
    receiver = new_receiver(&transport, &storage, 1U);
    assert(receiver != NULL);
    assert(ymodem_receive(receiver) == QM_EOK);
    assert(storage.write_calls == 1U);
    assert(storage.size == sizeof(image));
    assert(count_tx(&transport, YMODEM_NAK) >= 1U);
    ymodem_destroy(receiver);
}

static void test_stx_header_metadata(void)
{
    uint8_t image[1024] = {0};
    test_transport_t transport = {0};
    test_storage_t storage = {0};
    ymodem_handle_t receiver = NULL;

    fill_image(image, sizeof(image));
    prepare_transport(&transport);
    push_header_format(&transport, "stx.bin", sizeof(image), YMODEM_STX, 1U);
    push_data(&transport, YMODEM_STX, 1U, image, 1024U, 1024U);
    append_standard_end(&transport);
    receiver = new_receiver(&transport, &storage, 2U);
    assert(receiver != NULL);
    assert(ymodem_receive(receiver) == QM_EOK);
    assert(storage.advertised_size == sizeof(image));
    assert(storage.size == sizeof(image));
    assert(memcmp(storage.data, image, sizeof(image)) == 0);
    ymodem_destroy(receiver);
}

static void test_final_header_retry(void)
{
    uint8_t image[1] = {0xA5U};
    uint8_t packet[133] = {0};
    test_transport_t transport = {0};
    test_storage_t storage = {0};
    ymodem_handle_t receiver = NULL;
    uint16_t packet_length = 0U;

    prepare_transport(&transport);
    push_header(&transport, "final.bin", sizeof(image));
    push_data(&transport, YMODEM_SOH, 1U, image, 128U, 1U);
    stream_push_byte(&transport, YMODEM_EOT);
    stream_push_byte(&transport, YMODEM_EOT);
    packet_length = make_packet(packet, YMODEM_SOH, 0U, NULL, 128U);
    packet[packet_length - 2U] ^= 0x10U;
    stream_push(&transport, packet, packet_length);
    push_empty_header(&transport);
    receiver = new_receiver(&transport, &storage, 2U);
    assert(receiver != NULL);
    assert(ymodem_receive(receiver) == QM_EOK);
    assert(storage.finish_calls == 1U);
    assert(count_tx(&transport, YMODEM_NAK) >= 1U);
    ymodem_destroy(receiver);
}

static void test_final_header_timeout_limit(void)
{
    uint8_t image[1] = {0x5AU};
    test_transport_t transport = {0};
    test_storage_t storage = {0};
    ymodem_handle_t receiver = NULL;

    prepare_transport(&transport);
    push_header(&transport, "final-timeout.bin", sizeof(image));
    push_data(&transport, YMODEM_SOH, 1U, image, 128U, 1U);
    stream_push_byte(&transport, YMODEM_EOT);
    stream_push_byte(&transport, YMODEM_EOT);
    receiver = new_receiver(&transport, &storage, 1U);
    assert(receiver != NULL);
    assert(ymodem_receive(receiver) == -QM_ETIMEOUT);
    assert(storage.abort_calls == 1U);
    assert(count_tx(&transport, YMODEM_CRC) == 4U);
    assert(count_tx(&transport, YMODEM_CAN) >= 8U);
    ymodem_destroy(receiver);
}

static void test_repeated_eot_before_final_header(void)
{
    uint8_t image[1] = {0x3CU};
    test_transport_t transport = {0};
    test_storage_t storage = {0};
    ymodem_handle_t receiver = NULL;

    prepare_transport(&transport);
    push_header(&transport, "repeated-eot.bin", sizeof(image));
    push_data(&transport, YMODEM_SOH, 1U, image, 128U, 1U);
    stream_push_byte(&transport, YMODEM_EOT);
    stream_push_byte(&transport, YMODEM_EOT);
    stream_push_byte(&transport, YMODEM_EOT);
    push_empty_header(&transport);
    receiver = new_receiver(&transport, &storage, 2U);
    assert(receiver != NULL);
    assert(ymodem_receive(receiver) == QM_EOK);
    assert(count_tx(&transport, YMODEM_NAK) == 1U);
    assert(count_tx(&transport, YMODEM_CRC) == 4U);
    assert(storage.finish_calls == 1U);
    ymodem_destroy(receiver);
}

static void test_repeated_eot_retry_limit(void)
{
    uint8_t image[1] = {0x7EU};
    test_transport_t transport = {0};
    test_storage_t storage = {0};
    ymodem_handle_t receiver = NULL;

    prepare_transport(&transport);
    push_header(&transport, "eot-limit.bin", sizeof(image));
    push_data(&transport, YMODEM_SOH, 1U, image, 128U, 1U);
    stream_push_byte(&transport, YMODEM_EOT);
    stream_push_byte(&transport, YMODEM_EOT);
    stream_push_byte(&transport, YMODEM_EOT);
    stream_push_byte(&transport, YMODEM_EOT);
    receiver = new_receiver(&transport, &storage, 1U);
    assert(receiver != NULL);
    assert(ymodem_receive(receiver) == -QM_ETIMEOUT);
    assert(storage.finish_calls == 0U);
    assert(storage.abort_calls == 1U);
    assert(count_tx(&transport, YMODEM_CAN) >= 8U);
    ymodem_destroy(receiver);
}

static void test_storage_error_aborts(void)
{
    uint8_t image[128] = {0};
    test_transport_t transport = {0};
    test_storage_t storage = {0};
    ymodem_handle_t receiver = NULL;

    fill_image(image, sizeof(image));
    prepare_transport(&transport);
    push_header(&transport, "broken.bin", sizeof(image));
    push_data(&transport, YMODEM_SOH, 1U, image, 128U, 128U);
    append_standard_end(&transport);
    storage.fail_write_call = 1U;
    receiver = new_receiver(&transport, &storage, 1U);
    assert(receiver != NULL);
    assert(ymodem_receive(receiver) == -QM_EIO);
    assert(storage.abort_calls == 1U);
    assert(count_tx(&transport, YMODEM_CAN) >= 8U);
    ymodem_destroy(receiver);
}

static void test_finish_error_aborts_without_early_empty_ack(void)
{
    uint8_t image[128] = {0};
    test_transport_t transport = {0};
    test_storage_t storage = {0};
    ymodem_handle_t receiver = NULL;

    fill_image(image, sizeof(image));
    prepare_transport(&transport);
    push_header(&transport, "finish-error.bin", sizeof(image));
    push_data(&transport, YMODEM_SOH, 1U, image, 128U, 128U);
    stream_push_byte(&transport, YMODEM_EOT);
    push_empty_header(&transport);
    storage.finish_error = -QM_EIO;
    receiver = new_receiver(&transport, &storage, 1U);
    assert(receiver != NULL);
    assert(ymodem_receive(receiver) == -QM_EIO);
    assert(storage.finish_calls == 1U);
    assert(storage.abort_calls == 1U);
    assert(transport.ack_count == 2U);
    assert(count_tx(&transport, YMODEM_CAN) >= 8U);
    ymodem_destroy(receiver);
}

static void test_final_ack_error_does_not_abort_committed_storage(void)
{
    uint8_t image[128] = {0};
    test_transport_t transport = {0};
    test_storage_t storage = {0};
    ymodem_handle_t receiver = NULL;

    fill_image(image, sizeof(image));
    prepare_transport(&transport);
    push_header(&transport, "ack-error.bin", sizeof(image));
    push_data(&transport, YMODEM_SOH, 1U, image, 128U, 128U);
    append_standard_end(&transport);
    transport.fail_ack_number = 4U;
    receiver = new_receiver(&transport, &storage, 1U);
    assert(receiver != NULL);
    assert(ymodem_receive(receiver) == -QM_EIO);
    assert(storage.finish_calls == 1U);
    assert(storage.abort_calls == 0U);
    assert(transport.ack_count == 4U);
    assert(count_tx(&transport, YMODEM_CAN) == 0U);
    ymodem_destroy(receiver);
}

static void test_sequence_wrap_and_duplicate(void)
{
    uint8_t image[TEST_WRAP_IMAGE_SIZE] = {0};
    test_transport_t transport = {0};
    test_storage_t storage = {0};
    ymodem_handle_t receiver = NULL;
    uint32_t block = 0U;
    uint32_t offset = 0U;

    fill_image(image, sizeof(image));
    prepare_transport(&transport);
    push_header(&transport, "wrap.bin", sizeof(image));
    for (block = 1U; block <= 255U; block++) {
        offset = (block - 1U) * 128U;
        push_data(&transport, YMODEM_SOH, (uint8_t)block, &image[offset],
                  128U, 128U);
    }
    push_data(&transport, YMODEM_SOH, 0U, &image[255U * 128U], 128U, 1U);
    push_data(&transport, YMODEM_SOH, 0U, &image[255U * 128U], 128U, 1U);
    append_standard_end(&transport);
    receiver = new_receiver(&transport, &storage, 2U);
    assert(receiver != NULL);
    assert(ymodem_receive(receiver) == QM_EOK);
    assert(storage.size == sizeof(image));
    assert(storage.write_calls == 256U);
    assert(memcmp(storage.data, image, sizeof(image)) == 0);
    assert(count_tx(&transport, YMODEM_CRC) == 3U);
    ymodem_destroy(receiver);
}

static void test_unexpected_sequence_is_retried(void)
{
    uint8_t image[128] = {0};
    test_transport_t transport = {0};
    test_storage_t storage = {0};
    ymodem_handle_t receiver = NULL;

    fill_image(image, sizeof(image));
    prepare_transport(&transport);
    push_header(&transport, "sequence.bin", sizeof(image));
    push_data(&transport, YMODEM_SOH, 2U, image, 128U, 128U);
    push_data(&transport, YMODEM_SOH, 1U, image, 128U, 128U);
    append_standard_end(&transport);
    receiver = new_receiver(&transport, &storage, 2U);
    assert(receiver != NULL);
    assert(ymodem_receive(receiver) == QM_EOK);
    assert(storage.write_calls == 1U);
    assert(count_tx(&transport, YMODEM_NAK) >= 1U);
    ymodem_destroy(receiver);
}

static void test_progress_cancel_aborts(void)
{
    uint8_t image[128] = {0};
    test_transport_t transport = {0};
    test_storage_t storage = {0};
    test_progress_t progress = {0};
    ymodem_config_t config = {0};
    ymodem_handle_t receiver = NULL;

    fill_image(image, sizeof(image));
    prepare_transport(&transport);
    push_header(&transport, "cancel.bin", sizeof(image));
    push_data(&transport, YMODEM_SOH, 1U, image, 128U, 128U);
    config.write = transport_write;
    config.read = transport_read;
    config.transport_user = &transport;
    config.storage.begin = storage_begin;
    config.storage.write = storage_write;
    config.storage.finish = storage_finish;
    config.storage.abort = storage_abort;
    config.storage.user = &storage;
    config.handshake_timeout_ms = 10U;
    config.packet_timeout_ms = 10U;
    config.retry_max = 1U;
    config.max_file_size = TEST_IMAGE_CAPACITY;
    config.progress = cancel_progress;
    config.progress_user = &progress;
    receiver = ymodem_create(&config);
    assert(receiver != NULL);
    assert(ymodem_receive(receiver) == -QM_EINTR);
    assert(progress.calls == 1U);
    assert(progress.last.transferred == sizeof(image));
    assert(progress.last.total_size == sizeof(image));
    assert(storage.finish_calls == 0U);
    assert(storage.abort_calls == 1U);
    assert(count_tx(&transport, YMODEM_CAN) >= 8U);
    ymodem_destroy(receiver);
}

static void test_progress_reports_written_bytes(void)
{
    uint8_t image[TEST_PROGRESS_IMAGE_SIZE] = {0};
    static test_transport_t transport = {0};
    static test_storage_t storage = {0};
    test_progress_t progress = {0};
    ymodem_config_t config = {0};
    ymodem_handle_t receiver = NULL;

    fill_image(image, sizeof(image));
    prepare_transport(&transport);
    push_header(&transport, "progress.bin", sizeof(image));
    push_data(&transport, YMODEM_STX, 1U, image, 1024U, 1024U);
    transport.rx[transport.rx_length - 1U] ^= 0x55U;
    push_data(&transport, YMODEM_STX, 1U, image, 1024U, 1024U);
    push_data(&transport, YMODEM_STX, 1U, image, 1024U, 1024U);
    push_data(&transport, YMODEM_SOH, 2U, &image[1024], 128U, 17U);
    append_standard_end(&transport);
    storage.finish_error = -QM_EIO;
    progress.storage = &storage;
    config.write = transport_write;
    config.read = transport_read;
    config.transport_user = &transport;
    config.storage.begin = storage_begin;
    config.storage.write = storage_write;
    config.storage.finish = storage_finish;
    config.storage.abort = storage_abort;
    config.storage.user = &storage;
    config.progress = record_progress;
    config.progress_user = &progress;
    receiver = ymodem_create(&config);
    assert(receiver != NULL);
    assert(ymodem_receive(receiver) == -QM_EIO);
    assert(progress.calls == 2U);
    assert(progress.first.transferred == 1024U);
    assert(progress.first.total_size == sizeof(image));
    assert(progress.last.transferred == sizeof(image));
    assert(progress.last.total_size == sizeof(image));
    assert(storage.write_calls == 2U);
    assert(storage.finish_calls == 1U);
    assert(storage.abort_calls == 1U);
    assert(memcmp(storage.data, image, sizeof(image)) == 0);

    prepare_transport(&transport);
    memset(&storage, 0, sizeof(storage));
    memset(&progress, 0, sizeof(progress));
    progress.storage = &storage;
    push_header(&transport, "empty.bin", 0U);
    append_standard_end(&transport);
    assert(ymodem_receive(receiver) == QM_EOK);
    assert(progress.calls == 0U);
    assert(storage.write_calls == 0U);
    assert(storage.finish_calls == 1U);
    ymodem_destroy(receiver);
}

static void test_invalid_header(void)
{
    test_transport_t transport = {0};
    test_storage_t storage = {0};
    ymodem_handle_t receiver = NULL;

    prepare_transport(&transport);
    push_header(&transport, "bad/name.bin", 16U);
    receiver = new_receiver(&transport, &storage, 1U);
    assert(receiver != NULL);
    assert(ymodem_receive(receiver) == -QM_EINVAL);
    assert(storage.begin_calls == 0U);
    assert(count_tx(&transport, YMODEM_CAN) >= 8U);
    ymodem_destroy(receiver);
}

static void test_oversized_header(void)
{
    test_transport_t transport = {0};
    test_storage_t storage = {0};
    ymodem_handle_t receiver = NULL;

    prepare_transport(&transport);
    push_header(&transport, "too-large.bin", TEST_IMAGE_CAPACITY + 1U);
    receiver = new_receiver(&transport, &storage, 1U);
    assert(receiver != NULL);
    assert(ymodem_receive(receiver) == -QM_EINVAL);
    assert(storage.begin_calls == 0U);
    assert(count_tx(&transport, YMODEM_CAN) >= 8U);
    ymodem_destroy(receiver);
}

static void test_remote_cancel(void)
{
    test_transport_t transport = {0};
    test_storage_t storage = {0};
    ymodem_handle_t receiver = NULL;

    prepare_transport(&transport);
    stream_push_byte(&transport, YMODEM_CAN);
    receiver = new_receiver(&transport, &storage, 1U);
    assert(receiver != NULL);
    assert(ymodem_receive(receiver) == -QM_EINTR);
    assert(storage.begin_calls == 0U);
    assert(count_tx(&transport, YMODEM_CAN) == 0U);
    ymodem_destroy(receiver);
}

static void test_timeout_and_config(void)
{
    test_transport_t transport = {0};
    test_storage_t storage = {0};
    ymodem_config_t config = {0};
    ymodem_handle_t receiver = NULL;
    uint32_t started_at = 0U;

    config.write = transport_write;
    config.read = transport_read;
    config.transport_user = &transport;
    config.storage.begin = storage_begin;
    config.storage.write = storage_write;
    config.storage.finish = storage_finish;
    config.storage.abort = storage_abort;
    config.storage.user = &storage;
    config.handshake_timeout_ms = 10U;
    config.packet_timeout_ms = 10U;
    config.retry_max = 1U;
    config.max_file_size = TEST_IMAGE_CAPACITY;
    receiver = ymodem_create(&config);
    assert(receiver != NULL);
    assert(ymodem_receive(receiver) == -QM_ETIMEOUT);
    assert(count_tx(&transport, YMODEM_CRC) == 2U);
    assert(count_tx(&transport, YMODEM_CAN) >= 8U);
    ymodem_destroy(receiver);

    prepare_transport(&transport);
    config.handshake_timeout_ms = 0U;
    config.packet_timeout_ms = 0U;
    config.retry_max = 0U;
    config.max_file_size = 0U;
    receiver = ymodem_create(&config);
    assert(receiver != NULL);
    assert(config.handshake_timeout_ms == 0U);
    assert(config.packet_timeout_ms == 0U);
    assert(config.retry_max == 0U);
    assert(config.max_file_size == 0U);
    config.handshake_timeout_ms = 1U;
    config.retry_max = 1U;
    config.max_file_size = UINT32_MAX;
    started_at = fake_now_ms;
    assert(ymodem_receive(receiver) == -QM_ETIMEOUT);
    assert(count_tx(&transport, YMODEM_CRC) == CONFIG_YMODEM_RETRY_MAX + 1U);
    assert(fake_now_ms - started_at ==
           CONFIG_YMODEM_HANDSHAKE_TIMEOUT_MS * (CONFIG_YMODEM_RETRY_MAX + 1U));
    prepare_transport(&transport);
    push_header(&transport, "default-limit.bin", CONFIG_YMODEM_MAX_FILE_SIZE + 1U);
    assert(ymodem_receive(receiver) == -QM_EINVAL);
    assert(storage.begin_calls == 0U);
    ymodem_destroy(receiver);
}

static void test_invalid_create(void)
{
    test_transport_t transport = {0};
    test_storage_t storage = {0};
    ymodem_config_t config = {0};
    ymodem_handle_t receiver = NULL;

    assert(ymodem_create(NULL) == NULL);
    assert(ymodem_create(&config) == NULL);
    config.write = transport_write;
    config.read = transport_read;
    config.transport_user = &transport;
    config.storage.begin = storage_begin;
    config.storage.write = storage_write;
    config.storage.finish = storage_finish;
    config.storage.user = &storage;
    assert(ymodem_create(&config) == NULL);
    config.storage.abort = storage_abort;
    config.handshake_timeout_ms = UINT32_MAX;
    assert(ymodem_create(&config) == NULL);
    config.handshake_timeout_ms = 0U;
    config.packet_timeout_ms = UINT32_MAX / 2U + 1U;
    assert(ymodem_create(&config) == NULL);
    config.handshake_timeout_ms = UINT32_MAX / 2U;
    config.packet_timeout_ms = UINT32_MAX / 2U;
    receiver = ymodem_create(&config);
    assert(receiver != NULL);
    ymodem_destroy(receiver);
}

int main(void)
{
    fake_now_ms = 0U;
    test_mixed_packets_and_padding();
    test_bad_crc_and_duplicate();
    test_retry_budget_is_per_block();
    test_partial_frame_timeout_discards_tail();
    test_stx_header_metadata();
    test_final_header_retry();
    test_final_header_timeout_limit();
    test_repeated_eot_before_final_header();
    test_repeated_eot_retry_limit();
    test_storage_error_aborts();
    test_finish_error_aborts_without_early_empty_ack();
    test_final_ack_error_does_not_abort_committed_storage();
    test_sequence_wrap_and_duplicate();
    test_unexpected_sequence_is_retried();
    test_progress_cancel_aborts();
    test_progress_reports_written_bytes();
    test_invalid_header();
    test_oversized_header();
    test_remote_cancel();
    test_timeout_and_config();
    test_invalid_create();
    return 0;
}
