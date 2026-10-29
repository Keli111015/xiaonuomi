#include "qm_display_client.h"
#include "qm_display_server.h"
#include "qm_crc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIPE_CAPACITY 16384U
#define BACKEND_ASSET_CAPACITY 2048U
#define MULTI_CHUNK_ASSET_SIZE 1145U

typedef struct {
    uint8_t bytes[PIPE_CAPACITY];
    uint32_t read_offset;
    uint32_t write_offset;
} byte_pipe_t;

typedef struct {
    byte_pipe_t client_to_server;
    byte_pipe_t server_to_client;
    qm_display_server_handle_t server;
    uint32_t read_fragment;
    uint32_t drop_server_responses;
} test_link_t;

typedef struct {
    uint8_t asset[BACKEND_ASSET_CAPACITY];
    uint32_t asset_size;
    uint32_t total_size;
    uint16_t asset_id;
    uint16_t asset_crc;
    qm_display_server_screen_mask_t screen_mask;
    qm_display_server_asset_mode_t mode;
    uint32_t begin_count;
    uint32_t data_count;
    uint32_t end_count;
    uint32_t cancel_count;
    uint32_t display_count;
    uint32_t power_count;
    uint32_t brightness_count;
    qm_display_server_screen_mask_t power_screen_mask;
    qm_display_server_screen_mask_t brightness_screen_mask;
    qm_display_server_power_state_t power_state;
    uint8_t brightness;
} test_backend_t;

typedef struct {
    const uint8_t *data;
    uint32_t length;
    uint32_t read_count;
    uint16_t chunk_length;
} asset_source_t;

static uint32_t fake_now_ms;

static void put_u16_be(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void put_u32_be(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static uint16_t build_request(uint8_t *frame, uint16_t sequence,
                              uint8_t command, const uint8_t *payload,
                              uint16_t payload_length)
{
    uint16_t crc;

    frame[0] = 0x5AU;
    frame[1] = 0xA5U;
    frame[2] = 0x10U;
    put_u16_be(&frame[3], sequence);
    frame[5] = 0x02U;
    frame[6] = command;
    put_u16_be(&frame[7], payload_length);
    memcpy(&frame[9], payload, payload_length);
    crc = qm_crc16_ccitt_false(&frame[2], (uint16_t)(7U + payload_length));
    put_u16_be(&frame[9U + payload_length], crc);
    return (uint16_t)(payload_length + 11U);
}

void *qm_malloc(unsigned int size)
{
    return malloc(size);
}

void *qm_calloc(unsigned int nitems, unsigned int size)
{
    return calloc(nitems, size);
}

void qm_free(void *memory)
{
    free(memory);
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

static int32_t pipe_write(byte_pipe_t *pipe, const uint8_t *data, uint32_t length)
{
    assert(pipe->write_offset + length <= sizeof(pipe->bytes));
    memcpy(&pipe->bytes[pipe->write_offset], data, length);
    pipe->write_offset += length;
    return (int32_t)length;
}

static int32_t pipe_read(byte_pipe_t *pipe, uint8_t *data, uint32_t length,
                         uint32_t fragment)
{
    uint32_t available = pipe->write_offset - pipe->read_offset;
    uint32_t count;

    if (available == 0U) {
        return 0;
    }
    count = length < available ? length : available;
    if (fragment > 0U && count > fragment) {
        count = fragment;
    }
    memcpy(data, &pipe->bytes[pipe->read_offset], count);
    pipe->read_offset += count;
    if (pipe->read_offset == pipe->write_offset) {
        pipe->read_offset = 0U;
        pipe->write_offset = 0U;
    }
    return (int32_t)count;
}

static int32_t server_read(void *user, uint8_t *data,
                           uint32_t length, uint32_t timeout_ms)
{
    test_link_t *link = (test_link_t *)user;
    int32_t count = pipe_read(&link->client_to_server, data, length,
                              link->read_fragment);

    if (count == 0) {
        fake_now_ms += timeout_ms;
    }
    return count;
}

static int32_t server_write(void *user, const uint8_t *data,
                            uint32_t length, uint32_t timeout_ms)
{
    test_link_t *link = (test_link_t *)user;

    (void)timeout_ms;
    if (link->drop_server_responses > 0U) {
        link->drop_server_responses--;
        return (int32_t)length;
    }
    return pipe_write(&link->server_to_client, data, length);
}

static int32_t client_write(void *user, const uint8_t *data,
                            uint32_t length, uint32_t timeout_ms)
{
    test_link_t *link = (test_link_t *)user;
    qm_err_t ret;

    pipe_write(&link->client_to_server, data, length);
    ret = qm_display_server_process(link->server, timeout_ms);
    assert(ret == QM_EOK);
    return (int32_t)length;
}

static int32_t client_read(void *user, uint8_t *data,
                           uint32_t length, uint32_t timeout_ms)
{
    test_link_t *link = (test_link_t *)user;
    int32_t count = pipe_read(&link->server_to_client, data, length,
                              link->read_fragment);

    if (count == 0) {
        fake_now_ms += timeout_ms;
    }
    return count;
}

static qm_display_server_status_t backend_begin(
    void *user, qm_display_server_screen_mask_t screen_mask, uint16_t asset_id,
    qm_display_server_asset_mode_t mode, uint32_t total_size)
{
    test_backend_t *backend = (test_backend_t *)user;

    assert(total_size <= sizeof(backend->asset));
    backend->begin_count++;
    backend->asset_size = 0U;
    backend->total_size = total_size;
    backend->asset_id = asset_id;
    backend->asset_crc = 0U;
    backend->screen_mask = screen_mask;
    backend->mode = mode;
    return QM_DISPLAY_SERVER_STATUS_OK;
}

static qm_display_server_status_t backend_data(
    void *user, qm_display_server_screen_mask_t screen_mask, uint16_t asset_id,
    uint32_t offset, const uint8_t *data, uint16_t data_length)
{
    test_backend_t *backend = (test_backend_t *)user;

    assert(screen_mask == backend->screen_mask);
    assert(asset_id == backend->asset_id);
    assert(offset == backend->asset_size);
    assert(offset + data_length <= sizeof(backend->asset));
    memcpy(&backend->asset[offset], data, data_length);
    backend->asset_size += data_length;
    backend->data_count++;
    return QM_DISPLAY_SERVER_STATUS_OK;
}

static qm_display_server_status_t backend_end(
    void *user, qm_display_server_screen_mask_t screen_mask, uint16_t asset_id,
    qm_display_server_asset_mode_t mode, uint16_t asset_crc16)
{
    test_backend_t *backend = (test_backend_t *)user;

    assert(screen_mask == backend->screen_mask);
    assert(asset_id == backend->asset_id);
    assert(mode == backend->mode);
    assert(backend->asset_size == backend->total_size);
    assert(asset_crc16 == qm_crc16_ccitt_false(backend->asset,
                                               backend->asset_size));
    backend->asset_crc = asset_crc16;
    backend->end_count++;
    return QM_DISPLAY_SERVER_STATUS_OK;
}

static void backend_cancel(void *user,
                           qm_display_server_screen_mask_t screen_mask,
                           uint16_t asset_id)
{
    test_backend_t *backend = (test_backend_t *)user;

    assert(screen_mask == backend->screen_mask);
    assert(asset_id == backend->asset_id);
    backend->cancel_count++;
    backend->asset_size = 0U;
}

static qm_display_server_status_t backend_display(
    void *user, const qm_display_server_item_t *items, uint8_t item_count,
    uint16_t activate_delay_ms)
{
    test_backend_t *backend = (test_backend_t *)user;

    assert(item_count == 2U);
    assert(items[0].screen_id == 1U);
    assert(items[1].screen_id == 2U);
    assert(items[0].asset_id == backend->asset_id);
    assert(items[1].asset_id == backend->asset_id);
    assert(activate_delay_ms == 20U);
    backend->display_count++;
    return QM_DISPLAY_SERVER_STATUS_OK;
}

static qm_display_server_status_t backend_set_screen_power(
    void *user, qm_display_server_screen_mask_t screen_mask,
    qm_display_server_power_state_t power_state)
{
    test_backend_t *backend = (test_backend_t *)user;

    backend->power_screen_mask = screen_mask;
    backend->power_state = power_state;
    backend->power_count++;
    return QM_DISPLAY_SERVER_STATUS_OK;
}

static qm_display_server_status_t backend_set_brightness(
    void *user, qm_display_server_screen_mask_t screen_mask, uint8_t brightness)
{
    test_backend_t *backend = (test_backend_t *)user;

    backend->brightness_screen_mask = screen_mask;
    backend->brightness = brightness;
    backend->brightness_count++;
    return QM_DISPLAY_SERVER_STATUS_OK;
}

static void create_pair(test_link_t *link, test_backend_t *backend,
                        qm_display_client_handle_t *client)
{
    qm_display_server_config_t server_config = {0};
    qm_display_client_config_t client_config = {0};

    server_config.write = server_write;
    server_config.read = server_read;
    server_config.transport_user = link;
    server_config.backend_user = backend;
    server_config.firmware_version.major = 1U;
    server_config.firmware_version.minor = 2U;
    server_config.firmware_version.patch = 3U;
    server_config.firmware_state = QM_DISPLAY_SERVER_FW_APPLICATION;
    server_config.asset_begin = backend_begin;
    server_config.asset_data = backend_data;
    server_config.asset_end = backend_end;
    server_config.asset_cancel = backend_cancel;
    server_config.display = backend_display;
    server_config.set_screen_power = backend_set_screen_power;
    server_config.set_brightness = backend_set_brightness;
    link->server = qm_display_server_create(&server_config);
    assert(link->server != NULL);

    client_config.write = client_write;
    client_config.read = client_read;
    client_config.user = link;
    *client = qm_display_client_create(&client_config);
    assert(*client != NULL);
}

static qm_err_t asset_source_read(void *user, uint32_t offset, uint8_t *buffer,
                                  uint16_t max_length, uint16_t *out_length)
{
    asset_source_t *source = (asset_source_t *)user;
    uint32_t remaining;
    uint16_t length;

    assert(offset < source->length);
    remaining = source->length - offset;
    length = remaining > max_length ? max_length : (uint16_t)remaining;
    if (source->chunk_length > 0U && length > source->chunk_length) {
        length = source->chunk_length;
    }
    memcpy(buffer, &source->data[offset], length);
    source->read_count++;
    *out_length = length;
    return QM_EOK;
}

static void test_end_to_end(void)
{
    uint8_t asset[300];
    test_link_t link = {0};
    test_backend_t backend = {0};
    asset_source_t source;
    qm_display_client_handle_t client = NULL;
    qm_display_fw_info_t firmware;
    qm_display_asset_upload_t upload;
    qm_display_result_t result;
    qm_display_item_t items[2];
    qm_display_server_screen_info_t server_screen;
    uint32_t i;

    fake_now_ms = 0U;
    link.read_fragment = 7U;
    for (i = 0U; i < sizeof(asset); ++i) {
        asset[i] = (uint8_t)(i * 13U + 5U);
    }
    create_pair(&link, &backend, &client);

    assert(qm_display_client_get_fw_version(client, &firmware, &result) == QM_EOK);
    assert(result.status == QM_DISPLAY_STATUS_OK);
    assert(firmware.major == 1U);
    assert(firmware.minor == 2U);
    assert(firmware.patch == 3U);
    assert(firmware.state == QM_DISPLAY_FW_APPLICATION);

    assert(qm_display_client_set_screen_power(
               client, QM_DISPLAY_SCREEN_1,
               (qm_display_power_state_t)2U, &result) == -QM_EINVAL);
    assert(qm_display_client_set_brightness(
               client, QM_DISPLAY_SCREEN_1, 101U, &result) == -QM_EINVAL);

    assert(qm_display_client_set_screen_power(
               client, QM_DISPLAY_SCREEN_BOTH, QM_DISPLAY_POWER_OFF,
               &result) == QM_EOK);
    assert(result.status == QM_DISPLAY_STATUS_OK);
    assert(backend.power_count == 1U);
    assert(backend.power_screen_mask == QM_DISPLAY_SERVER_SCREEN_BOTH);
    assert(backend.power_state == QM_DISPLAY_SERVER_POWER_OFF);
    assert(qm_display_client_set_brightness(
               client, QM_DISPLAY_SCREEN_BOTH, 42U, &result) == QM_EOK);
    assert(result.status == QM_DISPLAY_STATUS_OK);
    assert(backend.brightness_count == 1U);
    assert(backend.brightness_screen_mask == QM_DISPLAY_SERVER_SCREEN_BOTH);
    assert(backend.brightness == 42U);

    source.data = asset;
    source.length = sizeof(asset);
    source.read_count = 0U;
    source.chunk_length = 128U;
    upload.screen_mask = QM_DISPLAY_SCREEN_BOTH;
    upload.asset_id = 20U;
    upload.mode = QM_DISPLAY_ASSET_RAM_DEFERRED;
    upload.total_size = sizeof(asset);
    upload.read = asset_source_read;
    upload.read_user = &source;
    link.drop_server_responses = 1U;
    assert(qm_display_client_upload_asset(client, &upload, &result) == QM_EOK);
    assert(result.status == QM_DISPLAY_STATUS_OK);
    assert(source.read_count == 3U);
    assert(backend.begin_count == 1U);
    assert(backend.data_count == 1U);
    assert(memcmp(backend.asset, asset, sizeof(asset)) == 0);
    assert(backend.end_count == 1U);

    items[0].screen_id = 1U;
    items[0].source = QM_DISPLAY_SOURCE_RAM_READY;
    items[0].asset_id = 20U;
    items[1].screen_id = 2U;
    items[1].source = QM_DISPLAY_SOURCE_RAM_READY;
    items[1].asset_id = 20U;
    assert(qm_display_client_display(client, items, 2U, 20U, &result) == QM_EOK);
    assert(result.status == QM_DISPLAY_STATUS_OK);
    assert(backend.display_count == 1U);
    assert(qm_display_server_get_screen_info(link.server, 1U, &server_screen) == QM_EOK);
    assert(server_screen.has_current_display);
    assert(server_screen.current_asset_id == 20U);
    assert(server_screen.has_power_state);
    assert(server_screen.power_state == QM_DISPLAY_SERVER_POWER_OFF);
    assert(server_screen.has_brightness);
    assert(server_screen.brightness == 42U);

    qm_display_client_destroy(client);
    qm_display_server_destroy(link.server);
}

static void test_server_rejects_bad_asset_crc(void)
{
    static const uint8_t asset[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t begin_payload[8] = {0};
    uint8_t data_payload[11] = {0};
    uint8_t end_payload[5] = {0};
    uint8_t frame[22];
    uint8_t response[12];
    test_link_t link = {0};
    test_backend_t backend = {0};
    qm_display_client_handle_t client = NULL;
    uint16_t frame_length;
    int32_t response_length;

    fake_now_ms = 0U;
    create_pair(&link, &backend, &client);

    begin_payload[0] = QM_DISPLAY_SERVER_SCREEN_1;
    put_u16_be(&begin_payload[1], 21U);
    begin_payload[3] = QM_DISPLAY_SERVER_ASSET_RAM_DEFERRED;
    put_u32_be(&begin_payload[4], sizeof(asset));
    frame_length = build_request(frame, 200U, 0x20U,
                                 begin_payload, sizeof(begin_payload));
    pipe_write(&link.client_to_server, frame, frame_length);
    assert(qm_display_server_process(link.server, 1000U) == QM_EOK);
    response_length = pipe_read(&link.server_to_client, response,
                                sizeof(response), 0U);
    assert(response_length == 12);
    assert(response[9] == QM_DISPLAY_SERVER_STATUS_OK);

    data_payload[0] = QM_DISPLAY_SERVER_SCREEN_1;
    put_u16_be(&data_payload[1], 21U);
    put_u32_be(&data_payload[3], 0U);
    memcpy(&data_payload[7], asset, sizeof(asset));
    frame_length = build_request(frame, 201U, 0x21U,
                                 data_payload, sizeof(data_payload));
    pipe_write(&link.client_to_server, frame, frame_length);
    assert(qm_display_server_process(link.server, 1000U) == QM_EOK);
    response_length = pipe_read(&link.server_to_client, response,
                                sizeof(response), 0U);
    assert(response_length == 12);
    assert(response[9] == QM_DISPLAY_SERVER_STATUS_OK);

    end_payload[0] = QM_DISPLAY_SERVER_SCREEN_1;
    put_u16_be(&end_payload[1], 21U);
    put_u16_be(&end_payload[3], 0x1234U);
    frame_length = build_request(frame, 202U, 0x22U,
                                 end_payload, sizeof(end_payload));
    pipe_write(&link.client_to_server, frame, frame_length);
    assert(qm_display_server_process(link.server, 1000U) == QM_EOK);
    response_length = pipe_read(&link.server_to_client, response,
                                sizeof(response), 0U);
    assert(response_length == 12);
    assert(response[5] == 0x01U);
    assert(response[9] == QM_DISPLAY_SERVER_STATUS_VERIFY_FAILED);
    assert(backend.end_count == 0U);
    assert(backend.cancel_count == 1U);

    qm_display_client_destroy(client);
    qm_display_server_destroy(link.server);
}

static void test_multi_chunk_upload(void)
{
    uint8_t asset[MULTI_CHUNK_ASSET_SIZE];
    test_link_t link = {0};
    test_backend_t backend = {0};
    asset_source_t source;
    qm_display_client_handle_t client = NULL;
    qm_display_asset_upload_t upload;
    qm_display_result_t result;
    uint32_t i;

    for (i = 0U; i < sizeof(asset); ++i) {
        asset[i] = (uint8_t)(i * 17U + 9U);
    }
    source.data = asset;
    source.length = sizeof(asset);
    source.read_count = 0U;
    source.chunk_length = 0U;
    upload.screen_mask = QM_DISPLAY_SCREEN_1;
    upload.asset_id = 22U;
    upload.mode = QM_DISPLAY_ASSET_RAM_DEFERRED;
    upload.total_size = sizeof(asset);
    upload.read = asset_source_read;
    upload.read_user = &source;

    fake_now_ms = 0U;
    link.read_fragment = 13U;
    create_pair(&link, &backend, &client);
    assert(qm_display_client_upload_asset(client, &upload, &result) == QM_EOK);
    assert(result.status == QM_DISPLAY_STATUS_OK);
    assert(source.read_count == 3U);
    assert(backend.begin_count == 1U);
    assert(backend.data_count == 3U);
    assert(backend.end_count == 1U);
    assert(backend.asset_size == sizeof(asset));
    assert(memcmp(backend.asset, asset, sizeof(asset)) == 0);

    qm_display_client_destroy(client);
    qm_display_server_destroy(link.server);
}

static void test_duplicate_key_with_different_payload(void)
{
    uint8_t payload[8] = {0};
    uint8_t frame[19];
    uint8_t response[12];
    test_link_t link = {0};
    test_backend_t backend = {0};
    qm_display_client_handle_t client = NULL;
    uint16_t frame_length;
    int32_t response_length;

    fake_now_ms = 0U;
    create_pair(&link, &backend, &client);
    payload[0] = QM_DISPLAY_SERVER_SCREEN_1;
    put_u16_be(&payload[1], 30U);
    payload[3] = QM_DISPLAY_SERVER_ASSET_FLASH_UPDATE;
    put_u32_be(&payload[4], 256U);
    frame_length = build_request(frame, 100U, 0x20U, payload, sizeof(payload));
    pipe_write(&link.client_to_server, frame, frame_length);
    assert(qm_display_server_process(link.server, 1000U) == QM_EOK);
    response_length = pipe_read(&link.server_to_client, response,
                                sizeof(response), 0U);
    assert(response_length == 12);
    assert(response[5] == 0x01U);
    assert(backend.begin_count == 1U);

    put_u16_be(&payload[1], 31U);
    frame_length = build_request(frame, 100U, 0x20U, payload, sizeof(payload));
    pipe_write(&link.client_to_server, frame, frame_length);
    assert(qm_display_server_process(link.server, 1000U) == QM_EOK);
    response_length = pipe_read(&link.server_to_client, response,
                                sizeof(response), 0U);
    assert(response_length == 12);
    assert(response[5] == 0x01U);
    assert(response[9] == QM_DISPLAY_SERVER_STATUS_INVALID_PARAM);
    assert(backend.begin_count == 1U);

    qm_display_client_destroy(client);
    qm_display_server_destroy(link.server);
}

int main(void)
{
    assert(qm_display_client_create(NULL) == NULL);
    assert(qm_display_server_create(NULL) == NULL);
    test_end_to_end();
    test_server_rejects_bad_asset_crc();
    test_multi_chunk_upload();
    test_duplicate_key_with_different_payload();
    puts("qm_display client/server integration tests passed");
    return 0;
}
