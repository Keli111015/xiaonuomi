#include "qm_display_server.h"

#include "qm.h"
#include "qm_crc.h"
#include "qm_utils_timer.h"

#define SERVER_LOG_TAG "display_server"

static volatile uint32_t g_qm_display_server_log_flags = QM_DISPLAY_SERVER_KCONFIG_LOG_FLAGS;

uint32_t qm_display_server_get_log_flags(void)
{
    return g_qm_display_server_log_flags;
}

int qm_display_server_set_log_flags(uint32_t log_flags)
{
    if ((log_flags & ~((uint32_t)QM_DISPLAY_SERVER_LOG_ALL)) != 0U) {
        return -QM_EINVAL;
    }
    g_qm_display_server_log_flags = log_flags;
    return QM_EOK;
}

#define SERVER_SOF_0                    0x5AU
#define SERVER_SOF_1                    0xA5U
#define SERVER_VERSION_MAJOR_MASK       0xF0U
#define SERVER_FLAG_RESPONSE            0x01U
#define SERVER_FLAG_ACK_REQUIRED        0x02U
#define SERVER_FLAG_RETRY               0x08U
#define SERVER_FLAG_RESERVED_MASK       0xF4U
#define SERVER_PROTOCOL_VERSION         0x10U
#define SERVER_MAX_PAYLOAD_LEN          1024U
#define SERVER_MAX_CHUNK_LEN            1017U
#define SERVER_MIN_CHUNK_LEN            128U
#define SERVER_CMD_GET_FW_VERSION       0x01U
#define SERVER_CMD_DISPLAY              0x10U
#define SERVER_CMD_SET_SCREEN_POWER     0x11U
#define SERVER_CMD_SET_BRIGHTNESS       0x12U
#define SERVER_CMD_ASSET_BEGIN          0x20U
#define SERVER_CMD_ASSET_DATA           0x21U
#define SERVER_CMD_ASSET_END            0x22U
#define SERVER_HEADER_LEN               9U
#define SERVER_FRAME_OVERHEAD           11U
#define SERVER_MAX_FRAME_LEN            (SERVER_MAX_PAYLOAD_LEN + SERVER_FRAME_OVERHEAD)
#define SERVER_MAX_RESPONSE_PAYLOAD     5U
#define SERVER_MAX_RESPONSE_FRAME       (SERVER_MAX_RESPONSE_PAYLOAD + SERVER_FRAME_OVERHEAD)
#define SERVER_MAX_SCREEN_COUNT         2U
#define SERVER_DISPLAY_DELAY_MAX_MS     100U
#define SERVER_ASSET_BEGIN_LENGTH       8U
#define SERVER_ASSET_DATA_HEADER_LENGTH 7U
#define SERVER_ASSET_END_LENGTH         5U
#define SERVER_CRC16_INIT               0xFFFFU

#ifndef CONFIG_QM_DISPLAY_SERVER_TRANSACTION_CACHE_SIZE
#define CONFIG_QM_DISPLAY_SERVER_TRANSACTION_CACHE_SIZE 32U
#endif
#ifndef CONFIG_QM_DISPLAY_SERVER_TRANSACTION_CACHE_MS
#define CONFIG_QM_DISPLAY_SERVER_TRANSACTION_CACHE_MS 5000U
#endif

typedef struct {
    uint8_t version;
    uint8_t flags;
    uint8_t command;
    uint16_t sequence;
    uint16_t payload_length;
    uint8_t *payload;
} server_frame_t;

typedef struct {
    uint8_t valid;
    uint8_t command;
    uint16_t sequence;
    uint16_t request_length;
    uint8_t *request_payload;
    uint16_t response_length;
    uint8_t response[SERVER_MAX_RESPONSE_FRAME];
    qm_utils_time_t created;
} transaction_entry_t;

typedef struct {
    qm_display_server_screen_info_t info;
    uint16_t calculated_crc;
} server_screen_t;

struct qm_display_server {
    qm_display_server_config_t config;
    uint8_t process_active;
    uint8_t *rx_buffer;
    transaction_entry_t *cache;
    uint16_t cache_cursor;
    server_screen_t screens[SERVER_MAX_SCREEN_COUNT];
};

static uint16_t get_u16_be(const uint8_t *data)
{
    uint16_t value;

    memcpy(&value, data, sizeof(value));
    return be16_to_cpu(value);
}

static uint32_t get_u32_be(const uint8_t *data)
{
    uint32_t value;

    memcpy(&value, data, sizeof(value));
    return be32_to_cpu(value);
}

static void put_u16_be(uint8_t *data, uint16_t value)
{
    value = cpu_to_be16(value);
    memcpy(data, &value, sizeof(value));
}

static bool_t valid_screen_mask(qm_display_server_screen_mask_t screen_mask)
{
    return screen_mask == QM_DISPLAY_SERVER_SCREEN_1 ||
           screen_mask == QM_DISPLAY_SERVER_SCREEN_2 ||
           screen_mask == QM_DISPLAY_SERVER_SCREEN_BOTH;
}

static bool_t mask_has_screen(qm_display_server_screen_mask_t screen_mask,
                              uint8_t screen_id)
{
    return ((uint8_t)screen_mask & (1U << (screen_id - 1U))) != 0U;
}

static bool_t valid_status(qm_display_server_status_t status)
{
    switch (status) {
        case QM_DISPLAY_SERVER_STATUS_OK:
        case QM_DISPLAY_SERVER_STATUS_UNKNOWN_CMD:
        case QM_DISPLAY_SERVER_STATUS_UNSUPPORTED_VERSION:
        case QM_DISPLAY_SERVER_STATUS_INVALID_LENGTH:
        case QM_DISPLAY_SERVER_STATUS_INVALID_PARAM:
        case QM_DISPLAY_SERVER_STATUS_BUSY:
        case QM_DISPLAY_SERVER_STATUS_ASSET_NOT_FOUND:
        case QM_DISPLAY_SERVER_STATUS_VERIFY_FAILED:
        case QM_DISPLAY_SERVER_STATUS_OFFSET_ERROR:
        case QM_DISPLAY_SERVER_STATUS_STORAGE_ERROR:
        case QM_DISPLAY_SERVER_STATUS_DISPLAY_FAILED:
        case QM_DISPLAY_SERVER_STATUS_INTERNAL_ERROR:
            return true;
        default:
            return false;
    }
}

static qm_err_t read_exact(qm_display_server_handle_t server, uint8_t *data,
                           uint32_t length, qm_utils_time_t *deadline)
{
    uint32_t offset = 0U;

    while (offset < length) {
        uint32_t time_left = qm_utils_time_left(deadline);
        int32_t count;

        if (time_left == 0U) {
            return -QM_ETIMEOUT;
        }
        count = server->config.read(server->config.transport_user, data + offset,
                                    length - offset, time_left);
        if (count < 0) {
            return -QM_EIO;
        }
        if (count == 0) {
            return -QM_ETIMEOUT;
        }
        if ((uint32_t)count > length - offset) {
            return -QM_EIO;
        }
        offset += (uint32_t)count;
    }
    return QM_EOK;
}

static qm_err_t write_exact(qm_display_server_handle_t server,
                            const uint8_t *data, uint32_t length,
                            uint32_t timeout_ms)
{
    qm_utils_time_t deadline;
    uint32_t offset = 0U;

    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    while (offset < length) {
        uint32_t time_left = qm_utils_time_left(&deadline);
        int32_t count;

        if (time_left == 0U) {
            return -QM_ETIMEOUT;
        }
        count = server->config.write(server->config.transport_user, data + offset,
                                     length - offset, time_left);
        if (count < 0) {
            return -QM_EIO;
        }
        if (count == 0) {
            return -QM_ETIMEOUT;
        }
        if ((uint32_t)count > length - offset) {
            return -QM_EIO;
        }
        offset += (uint32_t)count;
    }
    return QM_EOK;
}

static qm_err_t find_sof(qm_display_server_handle_t server,
                         qm_utils_time_t *deadline)
{
    uint8_t byte;
    bool_t saw_first = false;

    for (;;) {
        qm_err_t ret = read_exact(server, &byte, 1U, deadline);

        if (ret != QM_EOK) {
            return ret;
        }
        if (saw_first && byte == SERVER_SOF_1) {
            return QM_EOK;
        }
        saw_first = (byte == SERVER_SOF_0);
    }
}

static qm_err_t receive_frame(qm_display_server_handle_t server,
                              qm_utils_time_t *deadline, server_frame_t *frame)
{
    uint8_t *buffer = server->rx_buffer;

    for (;;) {
        uint16_t payload_length;
        uint16_t received_crc;
        uint16_t calculated_crc;
        qm_err_t ret;

        ret = find_sof(server, deadline);
        if (ret != QM_EOK) {
            return ret;
        }
        buffer[0] = SERVER_SOF_0;
        buffer[1] = SERVER_SOF_1;
        ret = read_exact(server, &buffer[2], SERVER_HEADER_LEN - 2U, deadline);
        if (ret != QM_EOK) {
            return ret;
        }
        payload_length = get_u16_be(&buffer[7]);
        if (payload_length > SERVER_MAX_PAYLOAD_LEN) {
            QM_DISPLAY_SERVER_LOGW(SERVER_LOG_TAG, QM_DISPLAY_SERVER_LOG_ERROR,
                                   "discard invalid length=%u", payload_length);
            continue;
        }
        ret = read_exact(server, &buffer[SERVER_HEADER_LEN],
                         (uint32_t)payload_length + 2U, deadline);
        if (ret != QM_EOK) {
            return ret;
        }
        received_crc = get_u16_be(&buffer[SERVER_HEADER_LEN + payload_length]);
        calculated_crc = qm_crc16_ccitt_false(&buffer[2],
                                               (uint16_t)(7U + payload_length));
        if (received_crc != calculated_crc) {
            QM_DISPLAY_SERVER_LOGW(SERVER_LOG_TAG, QM_DISPLAY_SERVER_LOG_ERROR,
                                   "discard crc expected=0x%04x actual=0x%04x",
                                   calculated_crc, received_crc);
            continue;
        }

        frame->version = buffer[2];
        frame->sequence = get_u16_be(&buffer[3]);
        frame->flags = buffer[5];
        frame->command = buffer[6];
        frame->payload_length = payload_length;
        frame->payload = &buffer[SERVER_HEADER_LEN];
        return QM_EOK;
    }
}

static uint16_t build_frame(uint8_t *frame, uint16_t sequence, uint8_t flags,
                            uint8_t command, const uint8_t *payload,
                            uint16_t payload_length)
{
    uint16_t crc;

    frame[0] = SERVER_SOF_0;
    frame[1] = SERVER_SOF_1;
    frame[2] = SERVER_PROTOCOL_VERSION;
    put_u16_be(&frame[3], sequence);
    frame[5] = flags;
    frame[6] = command;
    put_u16_be(&frame[7], payload_length);
    memcpy(&frame[SERVER_HEADER_LEN], payload, payload_length);
    crc = qm_crc16_ccitt_false(&frame[2], (uint16_t)(7U + payload_length));
    put_u16_be(&frame[SERVER_HEADER_LEN + payload_length], crc);
    return (uint16_t)(payload_length + SERVER_FRAME_OVERHEAD);
}

static void free_cache_entry(transaction_entry_t *entry)
{
    if (entry->request_payload != NULL) {
        qm_free(entry->request_payload);
    }
    memset(entry, 0, sizeof(*entry));
}

static void expire_cache(qm_display_server_handle_t server)
{
    uint16_t i;

    for (i = 0U; i < CONFIG_QM_DISPLAY_SERVER_TRANSACTION_CACHE_SIZE; ++i) {
        transaction_entry_t *entry = &server->cache[i];

        if (entry->valid &&
            qm_utils_time_spend(&entry->created) >=
                CONFIG_QM_DISPLAY_SERVER_TRANSACTION_CACHE_MS) {
            free_cache_entry(entry);
        }
    }
}

static transaction_entry_t *find_transaction(qm_display_server_handle_t server,
                                             const server_frame_t *request)
{
    uint16_t i;

    expire_cache(server);
    for (i = 0U; i < CONFIG_QM_DISPLAY_SERVER_TRANSACTION_CACHE_SIZE; ++i) {
        transaction_entry_t *entry = &server->cache[i];

        if (entry->valid && entry->sequence == request->sequence &&
            entry->command == request->command) {
            return entry;
        }
    }
    return NULL;
}

static void store_transaction(qm_display_server_handle_t server,
                              const server_frame_t *request,
                              uint8_t *request_copy,
                              const uint8_t *response,
                              uint16_t response_length)
{
    transaction_entry_t *entry = NULL;
    uint16_t i;

    expire_cache(server);
    for (i = 0U; i < CONFIG_QM_DISPLAY_SERVER_TRANSACTION_CACHE_SIZE; ++i) {
        if (!server->cache[i].valid) {
            entry = &server->cache[i];
            break;
        }
    }
    if (entry == NULL) {
        entry = &server->cache[server->cache_cursor];
        free_cache_entry(entry);
    }
    entry->valid = true;
    entry->sequence = request->sequence;
    entry->command = request->command;
    entry->request_length = request->payload_length;
    entry->request_payload = request_copy;
    entry->response_length = response_length;
    memcpy(entry->response, response, response_length);
    qm_utils_time_start(&entry->created);
    server->cache_cursor = (uint16_t)((entry - server->cache + 1U) %
                                     CONFIG_QM_DISPLAY_SERVER_TRANSACTION_CACHE_SIZE);
}

static bool_t same_transaction_payload(const transaction_entry_t *entry,
                                       const server_frame_t *request)
{
    if (entry->request_length != request->payload_length) {
        return false;
    }
    return request->payload_length == 0U ||
           memcmp(entry->request_payload, request->payload,
                  request->payload_length) == 0;
}

static void clear_task(qm_display_server_handle_t server,
                       qm_display_server_screen_mask_t screen_mask,
                       uint16_t asset_id, bool_t notify_backend)
{
    uint8_t screen_id;

    if (notify_backend && server->config.asset_cancel != NULL) {
        server->config.asset_cancel(server->config.backend_user,
                                    screen_mask, asset_id);
    }
    for (screen_id = 1U; screen_id <= SERVER_MAX_SCREEN_COUNT; ++screen_id) {
        server_screen_t *screen = &server->screens[screen_id - 1U];

        if (screen->info.transfer_active &&
            screen->info.transfer_screen_mask == screen_mask &&
            screen->info.transfer_asset_id == asset_id) {
            screen->info.transfer_active = false;
        }
    }
}

static void cancel_intersecting_tasks(qm_display_server_handle_t server,
                                      qm_display_server_screen_mask_t screen_mask)
{
    uint8_t screen_id;

    for (screen_id = 1U; screen_id <= SERVER_MAX_SCREEN_COUNT; ++screen_id) {
        server_screen_t *screen = &server->screens[screen_id - 1U];
        qm_display_server_screen_mask_t old_mask;
        uint16_t old_asset_id;

        if (!screen->info.transfer_active ||
            (((uint8_t)screen->info.transfer_screen_mask & (uint8_t)screen_mask) == 0U)) {
            continue;
        }
        old_mask = screen->info.transfer_screen_mask;
        old_asset_id = screen->info.transfer_asset_id;
        clear_task(server, old_mask, old_asset_id, true);
    }
}

static qm_display_server_status_t dispatch_get_fw(
    qm_display_server_handle_t server, const server_frame_t *request,
    uint8_t *payload, uint16_t *payload_length)
{
    if (request->payload_length != 0U) {
        return QM_DISPLAY_SERVER_STATUS_INVALID_LENGTH;
    }
    payload[0] = QM_DISPLAY_SERVER_STATUS_OK;
    payload[1] = server->config.firmware_version.major;
    payload[2] = server->config.firmware_version.minor;
    payload[3] = server->config.firmware_version.patch;
    payload[4] = (uint8_t)server->config.firmware_state;
    *payload_length = 5U;
    return QM_DISPLAY_SERVER_STATUS_OK;
}

static qm_display_server_status_t dispatch_display(
    qm_display_server_handle_t server, const server_frame_t *request,
    uint8_t *payload, uint16_t *payload_length)
{
    qm_display_server_item_t items[SERVER_MAX_SCREEN_COUNT];
    qm_display_server_screen_mask_t screen_mask = 0;
    qm_display_server_status_t status;
    uint16_t activate_delay_ms;
    uint8_t item_count;
    uint8_t i;

    if (request->payload_length < 1U) {
        return QM_DISPLAY_SERVER_STATUS_INVALID_LENGTH;
    }
    item_count = request->payload[0];
    if (item_count == 0U || item_count > SERVER_MAX_SCREEN_COUNT ||
        request->payload_length != (uint16_t)(3U + item_count * 4U)) {
        return QM_DISPLAY_SERVER_STATUS_INVALID_LENGTH;
    }
    for (i = 0U; i < item_count; ++i) {
        uint8_t screen_id = request->payload[1U + i * 4U];
        uint8_t bit;

        items[i].screen_id = screen_id;
        items[i].source = (qm_display_server_object_source_t)request->payload[2U + i * 4U];
        items[i].asset_id = get_u16_be(&request->payload[3U + i * 4U]);
        if (screen_id < 1U || screen_id > 2U ||
            items[i].source > QM_DISPLAY_SERVER_SOURCE_RAM_READY ||
            items[i].asset_id == 0U) {
            return QM_DISPLAY_SERVER_STATUS_INVALID_PARAM;
        }
        bit = (uint8_t)(1U << (screen_id - 1U));
        if (((uint8_t)screen_mask & bit) != 0U) {
            return QM_DISPLAY_SERVER_STATUS_INVALID_PARAM;
        }
        screen_mask = (qm_display_server_screen_mask_t)((uint8_t)screen_mask | bit);
        if (items[i].source == QM_DISPLAY_SERVER_SOURCE_RAM_READY &&
            (!server->screens[screen_id - 1U].info.has_ram_ready ||
             server->screens[screen_id - 1U].info.ram_ready_asset_id != items[i].asset_id)) {
            return QM_DISPLAY_SERVER_STATUS_ASSET_NOT_FOUND;
        }
    }
    if (item_count == 2U && screen_mask != QM_DISPLAY_SERVER_SCREEN_BOTH) {
        return QM_DISPLAY_SERVER_STATUS_INVALID_PARAM;
    }
    activate_delay_ms = get_u16_be(&request->payload[1U + item_count * 4U]);
    if (activate_delay_ms > SERVER_DISPLAY_DELAY_MAX_MS) {
        return QM_DISPLAY_SERVER_STATUS_INVALID_PARAM;
    }
    if (server->config.display == NULL) {
        return QM_DISPLAY_SERVER_STATUS_UNKNOWN_CMD;
    }

    status = server->config.display(server->config.backend_user, items,
                                    item_count, activate_delay_ms);
    if (!valid_status(status)) {
        return QM_DISPLAY_SERVER_STATUS_INTERNAL_ERROR;
    }
    if (status != QM_DISPLAY_SERVER_STATUS_OK) {
        return status;
    }
    for (i = 0U; i < item_count; ++i) {
        qm_display_server_screen_info_t *screen =
            &server->screens[items[i].screen_id - 1U].info;

        screen->has_current_display = true;
        screen->current_source = items[i].source;
        screen->current_asset_id = items[i].asset_id;
        if (items[i].source == QM_DISPLAY_SERVER_SOURCE_RAM_READY) {
            screen->has_ram_ready = false;
        }
    }
    payload[0] = QM_DISPLAY_SERVER_STATUS_OK;
    *payload_length = 1U;
    return QM_DISPLAY_SERVER_STATUS_OK;
}

static qm_display_server_status_t dispatch_screen_parameter(
    qm_display_server_handle_t server, const server_frame_t *request,
    uint8_t *payload, uint16_t *payload_length)
{
    qm_display_server_screen_mask_t screen_mask;
    qm_display_server_status_t status;
    uint8_t value;
    uint8_t screen_id;

    if (request->payload_length != 2U) {
        return QM_DISPLAY_SERVER_STATUS_INVALID_LENGTH;
    }
    screen_mask = (qm_display_server_screen_mask_t)request->payload[0];
    value = request->payload[1];
    if (!valid_screen_mask(screen_mask)) {
        return QM_DISPLAY_SERVER_STATUS_INVALID_PARAM;
    }

    if (request->command == SERVER_CMD_SET_SCREEN_POWER) {
        if (value > QM_DISPLAY_SERVER_POWER_ON) {
            return QM_DISPLAY_SERVER_STATUS_INVALID_PARAM;
        }
        if (server->config.set_screen_power == NULL) {
            return QM_DISPLAY_SERVER_STATUS_UNKNOWN_CMD;
        }
        status = server->config.set_screen_power(
            server->config.backend_user, screen_mask,
            (qm_display_server_power_state_t)value);
    } else if (request->command == SERVER_CMD_SET_BRIGHTNESS) {
        if (value > 100U) {
            return QM_DISPLAY_SERVER_STATUS_INVALID_PARAM;
        }
        if (server->config.set_brightness == NULL) {
            return QM_DISPLAY_SERVER_STATUS_UNKNOWN_CMD;
        }
        status = server->config.set_brightness(server->config.backend_user,
                                               screen_mask, value);
    } else {
        return QM_DISPLAY_SERVER_STATUS_UNKNOWN_CMD;
    }

    if (!valid_status(status)) {
        return QM_DISPLAY_SERVER_STATUS_INTERNAL_ERROR;
    }
    if (status != QM_DISPLAY_SERVER_STATUS_OK) {
        return status;
    }
    for (screen_id = 1U; screen_id <= SERVER_MAX_SCREEN_COUNT; ++screen_id) {
        qm_display_server_screen_info_t *screen;

        if (!mask_has_screen(screen_mask, screen_id)) {
            continue;
        }
        screen = &server->screens[screen_id - 1U].info;
        if (request->command == SERVER_CMD_SET_SCREEN_POWER) {
            screen->has_power_state = true;
            screen->power_state = (qm_display_server_power_state_t)value;
        } else {
            screen->has_brightness = true;
            screen->brightness = value;
        }
    }
    payload[0] = QM_DISPLAY_SERVER_STATUS_OK;
    *payload_length = 1U;
    return QM_DISPLAY_SERVER_STATUS_OK;
}

static qm_display_server_status_t dispatch_asset_begin(
    qm_display_server_handle_t server, const server_frame_t *request,
    uint8_t *payload, uint16_t *payload_length)
{
    qm_display_server_screen_mask_t screen_mask;
    qm_display_server_asset_mode_t mode;
    qm_display_server_status_t status;
    uint16_t asset_id;
    uint32_t total_size;
    uint8_t screen_id;

    if (request->payload_length != SERVER_ASSET_BEGIN_LENGTH) {
        return QM_DISPLAY_SERVER_STATUS_INVALID_LENGTH;
    }
    screen_mask = (qm_display_server_screen_mask_t)request->payload[0];
    asset_id = get_u16_be(&request->payload[1]);
    mode = (qm_display_server_asset_mode_t)request->payload[3];
    total_size = get_u32_be(&request->payload[4]);
    if (!valid_screen_mask(screen_mask) || asset_id == 0U || total_size == 0U ||
        mode > QM_DISPLAY_SERVER_ASSET_FLASH_UPDATE ||
        (screen_mask == QM_DISPLAY_SERVER_SCREEN_BOTH &&
         mode == QM_DISPLAY_SERVER_ASSET_RAM_AUTO_PRESENT)) {
        return QM_DISPLAY_SERVER_STATUS_INVALID_PARAM;
    }
    if (server->config.asset_begin == NULL) {
        return QM_DISPLAY_SERVER_STATUS_UNKNOWN_CMD;
    }

    cancel_intersecting_tasks(server, screen_mask);
    status = server->config.asset_begin(server->config.backend_user, screen_mask,
                                        asset_id, mode, total_size);
    if (!valid_status(status)) {
        return QM_DISPLAY_SERVER_STATUS_INTERNAL_ERROR;
    }
    if (status != QM_DISPLAY_SERVER_STATUS_OK) {
        return status;
    }
    for (screen_id = 1U; screen_id <= SERVER_MAX_SCREEN_COUNT; ++screen_id) {
        server_screen_t *screen;

        if (!mask_has_screen(screen_mask, screen_id)) {
            continue;
        }
        screen = &server->screens[screen_id - 1U];
        screen->info.transfer_active = true;
        screen->info.transfer_screen_mask = screen_mask;
        screen->info.transfer_mode = mode;
        screen->info.transfer_asset_id = asset_id;
        screen->info.transfer_total_size = total_size;
        screen->info.received_size = 0U;
        screen->calculated_crc = SERVER_CRC16_INIT;
        if (mode != QM_DISPLAY_SERVER_ASSET_FLASH_UPDATE) {
            screen->info.has_ram_ready = false;
        }
    }
    payload[0] = QM_DISPLAY_SERVER_STATUS_OK;
    *payload_length = 1U;
    return QM_DISPLAY_SERVER_STATUS_OK;
}

static qm_display_server_status_t dispatch_asset_data(
    qm_display_server_handle_t server, const server_frame_t *request,
    uint8_t *payload, uint16_t *payload_length)
{
    qm_display_server_screen_mask_t screen_mask;
    qm_display_server_status_t status;
    uint16_t asset_id;
    uint16_t data_length;
    uint32_t offset;
    uint32_t total_size = 0U;
    uint8_t screen_id;

    if (request->payload_length <= SERVER_ASSET_DATA_HEADER_LENGTH) {
        return QM_DISPLAY_SERVER_STATUS_INVALID_LENGTH;
    }
    screen_mask = (qm_display_server_screen_mask_t)request->payload[0];
    asset_id = get_u16_be(&request->payload[1]);
    offset = get_u32_be(&request->payload[3]);
    data_length = (uint16_t)(request->payload_length - SERVER_ASSET_DATA_HEADER_LENGTH);
    if (!valid_screen_mask(screen_mask) || asset_id == 0U ||
        data_length > SERVER_MAX_CHUNK_LEN ||
        offset > UINT32_MAX - data_length) {
        return QM_DISPLAY_SERVER_STATUS_INVALID_PARAM;
    }

    for (screen_id = 1U; screen_id <= SERVER_MAX_SCREEN_COUNT; ++screen_id) {
        const server_screen_t *screen;

        if (!mask_has_screen(screen_mask, screen_id)) {
            continue;
        }
        screen = &server->screens[screen_id - 1U];
        if (!screen->info.transfer_active ||
            screen->info.transfer_screen_mask != screen_mask ||
            screen->info.transfer_asset_id != asset_id) {
            return QM_DISPLAY_SERVER_STATUS_INVALID_PARAM;
        }
        if (screen->info.received_size != offset) {
            return QM_DISPLAY_SERVER_STATUS_OFFSET_ERROR;
        }
        total_size = screen->info.transfer_total_size;
        if (offset > total_size || total_size - offset < data_length ||
            (data_length < SERVER_MIN_CHUNK_LEN &&
             offset + data_length != total_size)) {
            return QM_DISPLAY_SERVER_STATUS_INVALID_PARAM;
        }
    }
    if (server->config.asset_data == NULL) {
        return QM_DISPLAY_SERVER_STATUS_UNKNOWN_CMD;
    }

    status = server->config.asset_data(
        server->config.backend_user, screen_mask, asset_id, offset,
        &request->payload[SERVER_ASSET_DATA_HEADER_LENGTH], data_length);
    if (!valid_status(status)) {
        return QM_DISPLAY_SERVER_STATUS_INTERNAL_ERROR;
    }
    if (status != QM_DISPLAY_SERVER_STATUS_OK) {
        return status;
    }
    for (screen_id = 1U; screen_id <= SERVER_MAX_SCREEN_COUNT; ++screen_id) {
        server_screen_t *screen;

        if (!mask_has_screen(screen_mask, screen_id)) {
            continue;
        }
        screen = &server->screens[screen_id - 1U];
        screen->calculated_crc = qm_crc16_ccitt_false_update(
            screen->calculated_crc,
            &request->payload[SERVER_ASSET_DATA_HEADER_LENGTH], data_length);
        screen->info.received_size = offset + data_length;
    }
    payload[0] = QM_DISPLAY_SERVER_STATUS_OK;
    *payload_length = 1U;
    return QM_DISPLAY_SERVER_STATUS_OK;
}

static qm_display_server_status_t dispatch_asset_end(
    qm_display_server_handle_t server, const server_frame_t *request,
    uint8_t *payload, uint16_t *payload_length)
{
    qm_display_server_screen_mask_t screen_mask;
    qm_display_server_asset_mode_t mode = QM_DISPLAY_SERVER_ASSET_RAM_DEFERRED;
    qm_display_server_status_t status;
    uint16_t asset_id;
    uint16_t asset_crc16;
    uint8_t screen_id;

    if (request->payload_length != SERVER_ASSET_END_LENGTH) {
        return QM_DISPLAY_SERVER_STATUS_INVALID_LENGTH;
    }
    screen_mask = (qm_display_server_screen_mask_t)request->payload[0];
    asset_id = get_u16_be(&request->payload[1]);
    asset_crc16 = get_u16_be(&request->payload[3]);
    if (!valid_screen_mask(screen_mask) || asset_id == 0U) {
        return QM_DISPLAY_SERVER_STATUS_INVALID_PARAM;
    }
    for (screen_id = 1U; screen_id <= SERVER_MAX_SCREEN_COUNT; ++screen_id) {
        const server_screen_t *screen;

        if (!mask_has_screen(screen_mask, screen_id)) {
            continue;
        }
        screen = &server->screens[screen_id - 1U];
        if (!screen->info.transfer_active ||
            screen->info.transfer_screen_mask != screen_mask ||
            screen->info.transfer_asset_id != asset_id) {
            return QM_DISPLAY_SERVER_STATUS_INVALID_PARAM;
        }
        if (screen->info.received_size != screen->info.transfer_total_size) {
            return QM_DISPLAY_SERVER_STATUS_OFFSET_ERROR;
        }
        if (screen->calculated_crc != asset_crc16) {
            clear_task(server, screen_mask, asset_id, true);
            return QM_DISPLAY_SERVER_STATUS_VERIFY_FAILED;
        }
        mode = screen->info.transfer_mode;
    }
    if (server->config.asset_end == NULL) {
        return QM_DISPLAY_SERVER_STATUS_UNKNOWN_CMD;
    }

    status = server->config.asset_end(server->config.backend_user, screen_mask,
                                      asset_id, mode, asset_crc16);
    if (!valid_status(status)) {
        status = QM_DISPLAY_SERVER_STATUS_INTERNAL_ERROR;
    }
    if (status != QM_DISPLAY_SERVER_STATUS_OK) {
        if (status != QM_DISPLAY_SERVER_STATUS_BUSY) {
            clear_task(server, screen_mask, asset_id, true);
        }
        return status;
    }
    clear_task(server, screen_mask, asset_id, false);
    for (screen_id = 1U; screen_id <= SERVER_MAX_SCREEN_COUNT; ++screen_id) {
        qm_display_server_screen_info_t *screen;

        if (!mask_has_screen(screen_mask, screen_id)) {
            continue;
        }
        screen = &server->screens[screen_id - 1U].info;
        if (mode == QM_DISPLAY_SERVER_ASSET_RAM_DEFERRED) {
            screen->has_ram_ready = true;
            screen->ram_ready_asset_id = asset_id;
        } else if (mode == QM_DISPLAY_SERVER_ASSET_RAM_AUTO_PRESENT) {
            screen->has_ram_ready = false;
            screen->has_current_display = true;
            screen->current_source = QM_DISPLAY_SERVER_SOURCE_RAM_READY;
            screen->current_asset_id = asset_id;
        }
    }
    payload[0] = QM_DISPLAY_SERVER_STATUS_OK;
    *payload_length = 1U;
    return QM_DISPLAY_SERVER_STATUS_OK;
}

static qm_display_server_status_t dispatch_request(
    qm_display_server_handle_t server, const server_frame_t *request,
    uint8_t *payload, uint16_t *payload_length)
{
    if ((request->version & SERVER_VERSION_MAJOR_MASK) !=
        (SERVER_PROTOCOL_VERSION & SERVER_VERSION_MAJOR_MASK)) {
        return QM_DISPLAY_SERVER_STATUS_UNSUPPORTED_VERSION;
    }
    if ((request->flags & SERVER_FLAG_RESERVED_MASK) != 0U ||
        (request->flags & SERVER_FLAG_ACK_REQUIRED) == 0U ||
        (request->flags & SERVER_FLAG_RESPONSE) != 0U) {
        return QM_DISPLAY_SERVER_STATUS_INVALID_PARAM;
    }

    switch (request->command) {
        case SERVER_CMD_GET_FW_VERSION:
            return dispatch_get_fw(server, request, payload, payload_length);
        case SERVER_CMD_DISPLAY:
            return dispatch_display(server, request, payload, payload_length);
        case SERVER_CMD_SET_SCREEN_POWER:
        case SERVER_CMD_SET_BRIGHTNESS:
            return dispatch_screen_parameter(server, request, payload,
                                             payload_length);
        case SERVER_CMD_ASSET_BEGIN:
            return dispatch_asset_begin(server, request, payload, payload_length);
        case SERVER_CMD_ASSET_DATA:
            return dispatch_asset_data(server, request, payload, payload_length);
        case SERVER_CMD_ASSET_END:
            return dispatch_asset_end(server, request, payload, payload_length);
        default:
            return QM_DISPLAY_SERVER_STATUS_UNKNOWN_CMD;
    }
}

qm_display_server_handle_t qm_display_server_create(
    const qm_display_server_config_t *config)
{
    qm_display_server_handle_t server;

    if (config == NULL || config->write == NULL || config->read == NULL ||
        config->firmware_state > QM_DISPLAY_SERVER_FW_UPGRADE_PENDING) {
        return NULL;
    }
    server = (qm_display_server_handle_t)qm_calloc(1U, sizeof(*server));
    if (server == NULL) {
        return NULL;
    }
    server->rx_buffer = (uint8_t *)qm_malloc(SERVER_MAX_FRAME_LEN);
    server->cache = (transaction_entry_t *)qm_calloc(
        CONFIG_QM_DISPLAY_SERVER_TRANSACTION_CACHE_SIZE,
        sizeof(transaction_entry_t));
    if (server->rx_buffer == NULL || server->cache == NULL) {
        qm_display_server_destroy(server);
        return NULL;
    }
    server->config = *config;
    return server;
}

void qm_display_server_destroy(qm_display_server_handle_t server)
{
    uint16_t i;

    if (server == NULL) {
        return;
    }
    for (i = 0U; i < SERVER_MAX_SCREEN_COUNT; ++i) {
        if (server->screens[i].info.transfer_active) {
            clear_task(server,
                       server->screens[i].info.transfer_screen_mask,
                       server->screens[i].info.transfer_asset_id, true);
        }
    }
    if (server->cache != NULL) {
        for (i = 0U; i < CONFIG_QM_DISPLAY_SERVER_TRANSACTION_CACHE_SIZE; ++i) {
            free_cache_entry(&server->cache[i]);
        }
        qm_free(server->cache);
    }
    if (server->rx_buffer != NULL) {
        qm_free(server->rx_buffer);
    }
    memset(server, 0, sizeof(*server));
    qm_free(server);
}

qm_err_t qm_display_server_process(qm_display_server_handle_t server,
                                   uint32_t timeout_ms)
{
    uint8_t response_payload[SERVER_MAX_RESPONSE_PAYLOAD] = {0};
    uint8_t response_frame[SERVER_MAX_RESPONSE_FRAME];
    uint8_t *request_copy = NULL;
    uint16_t response_payload_length = 0U;
    uint16_t response_frame_length;
    qm_display_server_status_t status;
    transaction_entry_t *cached;
    server_frame_t request;
    qm_utils_time_t deadline;
    qm_err_t ret;

    if (server == NULL || timeout_ms == 0U) {
        return -QM_EINVAL;
    }
    if (server->process_active) {
        return -QM_EBUSY;
    }
    server->process_active = true;
    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    ret = receive_frame(server, &deadline, &request);
    if (ret != QM_EOK) {
        goto cleanup;
    }

    cached = find_transaction(server, &request);
    if (cached != NULL) {
        if (same_transaction_payload(cached, &request)) {
            ret = write_exact(server, cached->response,
                              cached->response_length, timeout_ms);
            goto cleanup;
        }
        status = QM_DISPLAY_SERVER_STATUS_INVALID_PARAM;
        response_payload[0] = (uint8_t)status;
        response_payload_length = 1U;
        response_frame_length = build_frame(
            response_frame, request.sequence,
            SERVER_FLAG_RESPONSE, request.command,
            response_payload, response_payload_length);
        ret = write_exact(server, response_frame, response_frame_length, timeout_ms);
        goto cleanup;
    }

    if (request.payload_length > 0U) {
        request_copy = (uint8_t *)qm_malloc(request.payload_length);
        if (request_copy == NULL) {
            status = QM_DISPLAY_SERVER_STATUS_INTERNAL_ERROR;
            response_payload[0] = (uint8_t)status;
            response_payload_length = 1U;
            response_frame_length = build_frame(
                response_frame, request.sequence,
                SERVER_FLAG_RESPONSE, request.command,
                response_payload, response_payload_length);
            ret = write_exact(server, response_frame, response_frame_length, timeout_ms);
            goto cleanup;
        }
        memcpy(request_copy, request.payload, request.payload_length);
    }

    status = dispatch_request(server, &request, response_payload,
                              &response_payload_length);
    if (!valid_status(status)) {
        status = QM_DISPLAY_SERVER_STATUS_INTERNAL_ERROR;
    }
    if (status != QM_DISPLAY_SERVER_STATUS_OK) {
        response_payload[0] = (uint8_t)status;
        response_payload_length = 1U;
    }
    response_frame_length = build_frame(response_frame, request.sequence,
                                        SERVER_FLAG_RESPONSE, request.command,
                                        response_payload, response_payload_length);
    store_transaction(server, &request, request_copy,
                      response_frame, response_frame_length);
    request_copy = NULL;
    ret = write_exact(server, response_frame, response_frame_length, timeout_ms);

cleanup:
    if (request_copy != NULL) {
        qm_free(request_copy);
    }
    server->process_active = false;
    return ret;
}

qm_err_t qm_display_server_get_screen_info(qm_display_server_handle_t server,
                                           uint8_t screen_id,
                                           qm_display_server_screen_info_t *info)
{
    if (server == NULL || info == NULL || screen_id < 1U || screen_id > 2U) {
        return -QM_EINVAL;
    }
    *info = server->screens[screen_id - 1U].info;
    return QM_EOK;
}
