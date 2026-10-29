#include "qm_display_client.h"

#include "qm.h"
#include "qm_crc.h"
#include "qm_utils_timer.h"

#define DISPLAY_LOG_TAG "display_client"

static volatile uint32_t g_qm_display_client_log_flags = QM_DISPLAY_CLIENT_KCONFIG_LOG_FLAGS;

uint32_t qm_display_client_get_log_flags(void)
{
    return g_qm_display_client_log_flags;
}

int qm_display_client_set_log_flags(uint32_t log_flags)
{
    if ((log_flags & ~((uint32_t)QM_DISPLAY_CLIENT_LOG_ALL)) != 0U) {
        return -QM_EINVAL;
    }
    g_qm_display_client_log_flags = log_flags;
    return QM_EOK;
}

#define DISPLAY_SOF_0                  0x5AU
#define DISPLAY_SOF_1                  0xA5U
#define DISPLAY_FLAG_RESPONSE          0x01U
#define DISPLAY_FLAG_ACK_REQUIRED      0x02U
#define DISPLAY_FLAG_EVENT             0x04U
#define DISPLAY_FLAG_RETRY             0x08U
#define DISPLAY_PROTOCOL_VERSION       0x10U
#define DISPLAY_FLAGS_RESERVED_MASK    0xF0U
#define DISPLAY_REQUEST_FLAGS          DISPLAY_FLAG_ACK_REQUIRED
#define DISPLAY_EVENT_FLAGS            (DISPLAY_FLAG_EVENT | DISPLAY_FLAG_ACK_REQUIRED)
#define DISPLAY_MAX_PAYLOAD_LEN        1024U
#define DISPLAY_MAX_CHUNK_LEN          1017U
#define DISPLAY_MAX_OTA_CHUNK_LEN      1020U
#define DISPLAY_MIN_CHUNK_LEN          128U
#define DISPLAY_HEADER_LEN             9U
#define DISPLAY_FRAME_OVERHEAD         11U
#define DISPLAY_MAX_FRAME_LEN          (DISPLAY_MAX_PAYLOAD_LEN + DISPLAY_FRAME_OVERHEAD)
#define DISPLAY_ASSET_DATA_HEADER_LEN  7U
#define DISPLAY_MAX_SCREEN_COUNT       2U
#define DISPLAY_ACTIVATE_DELAY_MAX_MS  100U
#define DISPLAY_MAX_SUCCESS_PAYLOAD    5U
#define DISPLAY_CRC16_INIT             0xFFFFU

#define DISPLAY_CMD_GET_FW_VERSION     0x01U
#define DISPLAY_CMD_DISPLAY            0x10U
#define DISPLAY_CMD_SET_SCREEN_POWER   0x11U
#define DISPLAY_CMD_SET_BRIGHTNESS     0x12U
#define DISPLAY_CMD_DISPLAY_COMPLETE   0x13U
#define DISPLAY_CMD_ASSET_BEGIN        0x20U
#define DISPLAY_CMD_ASSET_DATA         0x21U
#define DISPLAY_CMD_ASSET_END          0x22U
#define DISPLAY_CMD_OTA_BEGIN           0x30U
#define DISPLAY_CMD_OTA_DATA            0x31U
#define DISPLAY_CMD_OTA_END             0x32U

#ifndef CONFIG_QM_DISPLAY_CLIENT_FW_TIMEOUT_MS
#define CONFIG_QM_DISPLAY_CLIENT_FW_TIMEOUT_MS 300U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_FW_RETRY_MAX
#define CONFIG_QM_DISPLAY_CLIENT_FW_RETRY_MAX 2U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_DISPLAY_EXTRA_TIMEOUT_MS
#define CONFIG_QM_DISPLAY_CLIENT_DISPLAY_EXTRA_TIMEOUT_MS 5000U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_DISPLAY_RETRY_MAX
#define CONFIG_QM_DISPLAY_CLIENT_DISPLAY_RETRY_MAX 2U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_SCREEN_CONTROL_TIMEOUT_MS
#define CONFIG_QM_DISPLAY_CLIENT_SCREEN_CONTROL_TIMEOUT_MS 500U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_SCREEN_CONTROL_RETRY_MAX
#define CONFIG_QM_DISPLAY_CLIENT_SCREEN_CONTROL_RETRY_MAX 2U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_ASSET_BEGIN_TIMEOUT_MS
#define CONFIG_QM_DISPLAY_CLIENT_ASSET_BEGIN_TIMEOUT_MS 1000U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_ASSET_BEGIN_RETRY_MAX
#define CONFIG_QM_DISPLAY_CLIENT_ASSET_BEGIN_RETRY_MAX 3U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_ASSET_DATA_TIMEOUT_MS
#define CONFIG_QM_DISPLAY_CLIENT_ASSET_DATA_TIMEOUT_MS 1000U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_ASSET_DATA_RETRY_MAX
#define CONFIG_QM_DISPLAY_CLIENT_ASSET_DATA_RETRY_MAX 2U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_ASSET_OFFSET_RESTART_MAX
#define CONFIG_QM_DISPLAY_CLIENT_ASSET_OFFSET_RESTART_MAX 2U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_ASSET_CHUNK_SIZE
#define CONFIG_QM_DISPLAY_CLIENT_ASSET_CHUNK_SIZE 512U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_ASSET_END_TIMEOUT_MS
#define CONFIG_QM_DISPLAY_CLIENT_ASSET_END_TIMEOUT_MS 5000U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_ASSET_END_RETRY_MAX
#define CONFIG_QM_DISPLAY_CLIENT_ASSET_END_RETRY_MAX 2U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_BUSY_RETRY_INTERVAL_MS
#define CONFIG_QM_DISPLAY_CLIENT_BUSY_RETRY_INTERVAL_MS 100U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_BUSY_RETRY_MAX
#define CONFIG_QM_DISPLAY_CLIENT_BUSY_RETRY_MAX 3U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_EVENT_ACK_TIMEOUT_MS
#define CONFIG_QM_DISPLAY_CLIENT_EVENT_ACK_TIMEOUT_MS 500U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_EVENT_ACK_RETRY_MAX
#define CONFIG_QM_DISPLAY_CLIENT_EVENT_ACK_RETRY_MAX 2U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_EVENT_CACHE_SIZE
#define CONFIG_QM_DISPLAY_CLIENT_EVENT_CACHE_SIZE 32U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_EVENT_DEDUP_WINDOW_MS
#define CONFIG_QM_DISPLAY_CLIENT_EVENT_DEDUP_WINDOW_MS 5000U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_OTA_SUPPORT
#define CONFIG_QM_DISPLAY_CLIENT_OTA_SUPPORT 1
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_OTA_BEGIN_TIMEOUT_MS
#define CONFIG_QM_DISPLAY_CLIENT_OTA_BEGIN_TIMEOUT_MS 5000U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_OTA_BEGIN_RETRY_MAX
#define CONFIG_QM_DISPLAY_CLIENT_OTA_BEGIN_RETRY_MAX 2U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_OTA_DATA_TIMEOUT_MS
#define CONFIG_QM_DISPLAY_CLIENT_OTA_DATA_TIMEOUT_MS 1000U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_OTA_DATA_RETRY_MAX
#define CONFIG_QM_DISPLAY_CLIENT_OTA_DATA_RETRY_MAX 2U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_OTA_OFFSET_RESTART_MAX
#define CONFIG_QM_DISPLAY_CLIENT_OTA_OFFSET_RESTART_MAX 2U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_OTA_CHUNK_SIZE
#define CONFIG_QM_DISPLAY_CLIENT_OTA_CHUNK_SIZE 512U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_OTA_END_TIMEOUT_MS
#define CONFIG_QM_DISPLAY_CLIENT_OTA_END_TIMEOUT_MS 10000U
#endif
#ifndef CONFIG_QM_DISPLAY_CLIENT_OTA_END_RETRY_MAX
#define CONFIG_QM_DISPLAY_CLIENT_OTA_END_RETRY_MAX 2U
#endif

typedef struct {
    uint8_t flags;
    uint8_t command;
    uint16_t sequence;
    uint16_t payload_length;
    uint8_t *payload;
} display_frame_t;

typedef struct {
    uint16_t payload_length;
    uint8_t payload[DISPLAY_MAX_SUCCESS_PAYLOAD];
} display_response_t;

typedef struct {
    uint8_t has_ram_ready;
    uint16_t ram_ready_asset_id;
} display_screen_state_t;

typedef struct {
    uint8_t valid;
    uint16_t sequence;
    uint8_t screen_id;
    uint16_t asset_id;
    qm_utils_time_t seen_at;
} display_event_cache_entry_t;

struct qm_display_client {
    qm_display_client_config_t config;
    uint16_t next_sequence;
    uint8_t request_active;
    uint8_t *tx_frame;
    uint8_t *rx_frame;
    display_screen_state_t screens[DISPLAY_MAX_SCREEN_COUNT];
    display_event_cache_entry_t events[CONFIG_QM_DISPLAY_CLIENT_EVENT_CACHE_SIZE];
};

static uint16_t get_u16_be(const uint8_t *data)
{
    uint16_t value;

    memcpy(&value, data, sizeof(value));
    return be16_to_cpu(value);
}

static void put_u16_be(uint8_t *data, uint16_t value)
{
    value = cpu_to_be16(value);
    memcpy(data, &value, sizeof(value));
}

static void put_u32_be(uint8_t *data, uint32_t value)
{
    value = cpu_to_be32(value);
    memcpy(data, &value, sizeof(value));
}

static bool_t valid_screen_mask(qm_display_screen_mask_t screen_mask)
{
    return screen_mask == QM_DISPLAY_SCREEN_1 ||
           screen_mask == QM_DISPLAY_SCREEN_2 ||
           screen_mask == QM_DISPLAY_SCREEN_BOTH;
}

static bool_t mask_has_screen(qm_display_screen_mask_t screen_mask, uint8_t screen_id)
{
    return (screen_mask & (1U << (screen_id - 1U))) != 0U;
}

static void clear_result(qm_display_result_t *result)
{
    memset(result, 0, sizeof(*result));
}

static qm_err_t read_exact(qm_display_client_handle_t client, uint8_t *data,
                           uint32_t length, qm_utils_time_t *deadline)
{
    uint32_t offset = 0;

    while (offset < length) {
        uint32_t time_left = qm_utils_time_left(deadline);
        int32_t count;

        if (time_left == 0U) {
            return -QM_ETIMEOUT;
        }
        count = client->config.read(client->config.user, data + offset,
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

static qm_err_t write_exact(qm_display_client_handle_t client, const uint8_t *data,
                            uint32_t length, uint32_t timeout_ms)
{
    qm_utils_time_t deadline;
    uint32_t offset = 0;

    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    while (offset < length) {
        uint32_t time_left = qm_utils_time_left(&deadline);
        int32_t count;

        if (time_left == 0U) {
            return -QM_ETIMEOUT;
        }
        count = client->config.write(client->config.user, data + offset,
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

static qm_err_t find_sof(qm_display_client_handle_t client,
                         qm_utils_time_t *deadline)
{
    uint8_t byte;
    bool_t saw_first = false;

    for (;;) {
        qm_err_t ret = read_exact(client, &byte, 1U, deadline);

        if (ret != QM_EOK) {
            return ret;
        }
        if (saw_first && byte == DISPLAY_SOF_1) {
            return QM_EOK;
        }
        saw_first = (byte == DISPLAY_SOF_0);
    }
}

static qm_err_t receive_frame(qm_display_client_handle_t client, uint8_t *buffer,
                              qm_utils_time_t *deadline, display_frame_t *frame)
{
    for (;;) {
        uint16_t payload_length;
        uint16_t received_crc;
        uint16_t calculated_crc;
        qm_err_t ret;

        ret = find_sof(client, deadline);
        if (ret != QM_EOK) {
            return ret;
        }
        buffer[0] = DISPLAY_SOF_0;
        buffer[1] = DISPLAY_SOF_1;
        ret = read_exact(client, &buffer[2], DISPLAY_HEADER_LEN - 2U, deadline);
        if (ret != QM_EOK) {
            return ret;
        }

        payload_length = get_u16_be(&buffer[7]);
        if (payload_length > DISPLAY_MAX_PAYLOAD_LEN) {
            QM_DISPLAY_CLIENT_LOGW(DISPLAY_LOG_TAG, QM_DISPLAY_CLIENT_LOG_ERROR,
                                   "discard invalid length=%u", payload_length);
            continue;
        }
        ret = read_exact(client, &buffer[DISPLAY_HEADER_LEN],
                         (uint32_t)payload_length + 2U, deadline);
        if (ret != QM_EOK) {
            return ret;
        }
        if (buffer[2] != DISPLAY_PROTOCOL_VERSION) {
            QM_DISPLAY_CLIENT_LOGW(DISPLAY_LOG_TAG, QM_DISPLAY_CLIENT_LOG_ERROR,
                                   "discard version=0x%02x", buffer[2]);
            continue;
        }

        if ((buffer[5] & DISPLAY_FLAGS_RESERVED_MASK) != 0U) {
            QM_DISPLAY_CLIENT_LOGW(DISPLAY_LOG_TAG, QM_DISPLAY_CLIENT_LOG_ERROR,
                                   "discard reserved flags=0x%02x", buffer[5]);
            continue;
        }

        received_crc = get_u16_be(&buffer[DISPLAY_HEADER_LEN + payload_length]);
        calculated_crc = qm_crc16_ccitt_false(&buffer[2],
                                               (uint16_t)(7U + payload_length));
        if (received_crc != calculated_crc) {
            QM_DISPLAY_CLIENT_LOGW(DISPLAY_LOG_TAG, QM_DISPLAY_CLIENT_LOG_ERROR,
                                   "discard crc expected=0x%04x actual=0x%04x",
                                   calculated_crc, received_crc);
            continue;
        }

        frame->sequence = get_u16_be(&buffer[3]);
        frame->flags = buffer[5];
        frame->command = buffer[6];
        frame->payload_length = payload_length;
        frame->payload = &buffer[DISPLAY_HEADER_LEN];
        return QM_EOK;
    }
}

static uint32_t build_frame(uint8_t *frame, uint16_t sequence, uint8_t flags,
                            uint8_t command, const uint8_t *payload,
                            uint16_t payload_length)
{
    uint16_t crc;

    frame[0] = DISPLAY_SOF_0;
    frame[1] = DISPLAY_SOF_1;
    frame[2] = DISPLAY_PROTOCOL_VERSION;
    put_u16_be(&frame[3], sequence);
    frame[5] = flags;
    frame[6] = command;
    put_u16_be(&frame[7], payload_length);
    if (payload_length > 0U) {
        memmove(&frame[DISPLAY_HEADER_LEN], payload, payload_length);
    }
    crc = qm_crc16_ccitt_false(&frame[2], (uint16_t)(7U + payload_length));
    put_u16_be(&frame[DISPLAY_HEADER_LEN + payload_length], crc);
    return (uint32_t)payload_length + DISPLAY_FRAME_OVERHEAD;
}

static bool_t response_flags_valid(const display_frame_t *frame)
{
    return frame->flags == DISPLAY_FLAG_RESPONSE;
}

static qm_err_t parse_result(const display_frame_t *frame,
                             qm_display_result_t *result)
{
    if (!response_flags_valid(frame) || frame->payload_length < 1U) {
        return -QM_EIO;
    }

    clear_result(result);
    result->status = (qm_display_status_t)frame->payload[0];
    return result->status == QM_DISPLAY_STATUS_OK || frame->payload_length == 1U ?
           QM_EOK : -QM_EIO;
}

static bool_t event_key_equal(const display_event_cache_entry_t *entry,
                              const qm_display_complete_event_t *event)
{
    return entry->valid && entry->sequence == event->sequence &&
           entry->screen_id == event->screen_id &&
           entry->asset_id == event->asset_id;
}

static bool_t event_seen(qm_display_client_handle_t client,
                         const qm_display_complete_event_t *event)
{
    uint8_t i;

    for (i = 0U; i < CONFIG_QM_DISPLAY_CLIENT_EVENT_CACHE_SIZE; ++i) {
        display_event_cache_entry_t *entry = &client->events[i];

        if (!entry->valid) {
            continue;
        }
        if (qm_utils_time_spend(&entry->seen_at) >
            CONFIG_QM_DISPLAY_CLIENT_EVENT_DEDUP_WINDOW_MS) {
            entry->valid = false;
            continue;
        }
        if (event_key_equal(entry, event)) {
            return true;
        }
    }
    return false;
}

static void remember_event(qm_display_client_handle_t client,
                           const qm_display_complete_event_t *event)
{
    uint8_t i;
    uint8_t slot = 0U;
    uint32_t oldest_age = 0U;

    for (i = 0U; i < CONFIG_QM_DISPLAY_CLIENT_EVENT_CACHE_SIZE; ++i) {
        display_event_cache_entry_t *entry = &client->events[i];
        uint32_t age;

        if (!entry->valid) {
            slot = i;
            break;
        }
        age = qm_utils_time_spend(&entry->seen_at);
        if (age > oldest_age) {
            oldest_age = age;
            slot = i;
        }
    }
    client->events[slot].valid = true;
    client->events[slot].sequence = event->sequence;
    client->events[slot].screen_id = event->screen_id;
    client->events[slot].asset_id = event->asset_id;
    qm_utils_time_start(&client->events[slot].seen_at);
}

static qm_err_t acknowledge_event(qm_display_client_handle_t client,
                                  const qm_display_complete_event_t *event)
{
    uint8_t payload = QM_DISPLAY_STATUS_OK;
    uint8_t attempt;
    uint32_t frame_length;

    frame_length = build_frame(client->rx_frame, event->sequence,
                               DISPLAY_FLAG_RESPONSE,
                               DISPLAY_CMD_DISPLAY_COMPLETE, &payload, 1U);
    for (attempt = 0U; attempt <= CONFIG_QM_DISPLAY_CLIENT_EVENT_ACK_RETRY_MAX;
         ++attempt) {
        qm_err_t ret = write_exact(client, client->rx_frame, frame_length,
                                   CONFIG_QM_DISPLAY_CLIENT_EVENT_ACK_TIMEOUT_MS);

        if (ret == QM_EOK) {
            return QM_EOK;
        }
        if (ret != -QM_ETIMEOUT) {
            return ret;
        }
        QM_DISPLAY_CLIENT_LOGW(DISPLAY_LOG_TAG, QM_DISPLAY_CLIENT_LOG_CONTROL,
                               "retry event ack seq=%u attempt=%u",
                               event->sequence, attempt + 1U);
    }
    return -QM_ETIMEOUT;
}

static qm_err_t handle_event(qm_display_client_handle_t client,
                             const display_frame_t *frame)
{
    qm_display_complete_event_t event;
    qm_err_t ret;

    if (frame->flags != DISPLAY_EVENT_FLAGS ||
        frame->command != DISPLAY_CMD_DISPLAY_COMPLETE ||
        frame->payload_length != 3U || frame->payload[0] < 1U ||
        frame->payload[0] > DISPLAY_MAX_SCREEN_COUNT ||
        get_u16_be(&frame->payload[1]) == 0U) {
        return -QM_EIO;
    }
    event.sequence = frame->sequence;
    event.screen_id = frame->payload[0];
    event.asset_id = get_u16_be(&frame->payload[1]);
    ret = acknowledge_event(client, &event);
    if (ret != QM_EOK) {
        QM_DISPLAY_CLIENT_LOGW(DISPLAY_LOG_TAG, QM_DISPLAY_CLIENT_LOG_ERROR,
                               "event ack failed seq=%u ret=%d",
                               event.sequence, ret);
        return ret;
    }
    if (!event_seen(client, &event)) {
        remember_event(client, &event);
        if (client->config.event_cb != NULL) {
            client->config.event_cb(client, &event, client->config.event_user_data);
        }
    }
    return QM_EOK;
}

static qm_err_t transact(qm_display_client_handle_t client, uint8_t command,
                         const uint8_t *payload, uint16_t payload_length,
                         uint32_t timeout_ms, uint8_t retry_max,
                         display_response_t *response, qm_display_result_t *result)
{
    uint16_t sequence;
    uint8_t busy_attempt = 0U;
    qm_err_t ret = -QM_ETIMEOUT;

    if (client->request_active) {
        return -QM_EBUSY;
    }
    if (payload_length > DISPLAY_MAX_PAYLOAD_LEN ||
        (payload_length > 0U && payload == NULL)) {
        return -QM_EINVAL;
    }

    client->request_active = true;
    sequence = client->next_sequence++;
    for (;;) {
        uint8_t attempt;
        bool_t new_sequence = false;

        for (attempt = 0U; attempt <= retry_max; ++attempt) {
            uint8_t flags = DISPLAY_REQUEST_FLAGS;
            uint32_t tx_length;
            qm_utils_time_t deadline;
            display_frame_t received;

            if (attempt > 0U) {
                flags |= DISPLAY_FLAG_RETRY;
                QM_DISPLAY_CLIENT_LOGW(DISPLAY_LOG_TAG, QM_DISPLAY_CLIENT_LOG_CONTROL,
                                       "retry cmd=0x%02x seq=%u attempt=%u",
                                       command, sequence, attempt);
            }
            tx_length = build_frame(client->tx_frame, sequence, flags, command,
                                    payload, payload_length);
            ret = write_exact(client, client->tx_frame, tx_length, timeout_ms);
            if (ret != QM_EOK) {
                if (ret == -QM_ETIMEOUT && attempt < retry_max) {
                    continue;
                }
                break;
            }

            qm_utils_time_countdown_ms(&deadline, timeout_ms);
            for (;;) {
                ret = receive_frame(client, client->rx_frame, &deadline, &received);
                if (ret != QM_EOK) {
                    break;
                }
                if ((received.flags & DISPLAY_FLAG_EVENT) != 0U) {
                    if (received.flags == DISPLAY_EVENT_FLAGS) {
                        ret = handle_event(client, &received);
                        if (ret != QM_EOK) {
                            goto cleanup;
                        }
                    }
                    continue;
                }
                if (received.sequence == sequence &&
                    received.command == command && response_flags_valid(&received)) {
                    ret = parse_result(&received, result);
                    if (ret != QM_EOK) {
                        QM_DISPLAY_CLIENT_LOGW(DISPLAY_LOG_TAG, QM_DISPLAY_CLIENT_LOG_DOWNLINK,
                                               "ignore malformed response cmd=0x%02x seq=%u",
                                               received.command, received.sequence);
                        continue;
                    }
                    if (result->status == QM_DISPLAY_STATUS_BUSY &&
                        busy_attempt < client->config.busy_retry_max) {
                        ++busy_attempt;
                        new_sequence = true;
                        break;
                    }
                    if (result->status == QM_DISPLAY_STATUS_OK &&
                        received.payload_length > sizeof(response->payload)) {
                        QM_DISPLAY_CLIENT_LOGW(DISPLAY_LOG_TAG, QM_DISPLAY_CLIENT_LOG_PAYLOAD,
                                               "ignore oversized response payload=%u",
                                               received.payload_length);
                        continue;
                    }
                    if (result->status == QM_DISPLAY_STATUS_OK) {
                        response->payload_length = received.payload_length;
                        memcpy(response->payload, received.payload,
                               received.payload_length);
                    }
                    goto cleanup;
                }
                QM_DISPLAY_CLIENT_LOGW(DISPLAY_LOG_TAG, QM_DISPLAY_CLIENT_LOG_DOWNLINK,
                                       "ignore unmatched cmd=0x%02x seq=%u",
                                       received.command, received.sequence);
            }
            if (new_sequence) {
                break;
            }
            if (ret != -QM_ETIMEOUT) {
                break;
            }
        }
        if (!new_sequence) {
            break;
        }
        if (client->config.busy_retry_interval_ms > 0U) {
            (void)qm_msleep((int)client->config.busy_retry_interval_ms);
        }
        sequence = client->next_sequence++;
        QM_DISPLAY_CLIENT_LOGW(DISPLAY_LOG_TAG, QM_DISPLAY_CLIENT_LOG_CONTROL,
                               "retry busy cmd=0x%02x new_seq=%u attempt=%u",
                               command, sequence, busy_attempt);
    }

cleanup:
    client->request_active = false;
    return ret;
}

static qm_err_t require_success_payload(const display_response_t *response,
                                        const qm_display_result_t *result,
                                        uint16_t expected_length)
{
    if (result->status != QM_DISPLAY_STATUS_OK) {
        return QM_EOK;
    }
    return response->payload_length == expected_length ? QM_EOK : -QM_EIO;
}

static qm_err_t read_upload_chunk(qm_display_asset_read_fn read, void *read_user,
                                  uint32_t offset, uint8_t *buffer,
                                  uint16_t max_length, uint16_t *out_length)
{
    uint16_t length = 0U;

    while (length < max_length) {
        uint16_t read_length = 0U;
        qm_err_t ret = read(read_user, offset + length, buffer + length,
                            (uint16_t)(max_length - length), &read_length);

        if (ret != QM_EOK) {
            return ret;
        }
        if (read_length == 0U || read_length > max_length - length) {
            return -QM_EIO;
        }
        length = (uint16_t)(length + read_length);
    }
    *out_length = length;
    return QM_EOK;
}

qm_display_client_handle_t qm_display_client_create(
    const qm_display_client_config_t *config)
{
    qm_display_client_handle_t client;

    if (config == NULL || config->write == NULL || config->read == NULL) {
        return NULL;
    }
    client = (qm_display_client_handle_t)qm_calloc(1U, sizeof(*client));
    if (client == NULL) {
        return NULL;
    }
    client->tx_frame = (uint8_t *)qm_malloc(DISPLAY_MAX_FRAME_LEN);
    client->rx_frame = (uint8_t *)qm_malloc(DISPLAY_MAX_FRAME_LEN);
    if (client->tx_frame == NULL || client->rx_frame == NULL) {
        qm_display_client_destroy(client);
        return NULL;
    }
    client->config = *config;
    if (client->config.busy_retry_interval_ms == 0U &&
        client->config.busy_retry_max == 0U) {
        client->config.busy_retry_interval_ms = CONFIG_QM_DISPLAY_CLIENT_BUSY_RETRY_INTERVAL_MS;
        client->config.busy_retry_max = CONFIG_QM_DISPLAY_CLIENT_BUSY_RETRY_MAX;
    }
    client->next_sequence = config->initial_sequence;
    return client;
}

void qm_display_client_destroy(qm_display_client_handle_t client)
{
    if (client == NULL) {
        return;
    }
    if (client->tx_frame != NULL) {
        qm_free(client->tx_frame);
    }
    if (client->rx_frame != NULL) {
        qm_free(client->rx_frame);
    }
    memset(client, 0, sizeof(*client));
    qm_free(client);
}

qm_err_t qm_display_client_get_fw_version(qm_display_client_handle_t client,
                                          qm_display_fw_info_t *info,
                                          qm_display_result_t *result)
{
    display_response_t response;
    qm_err_t ret;

    if (client == NULL || info == NULL || result == NULL) {
        return -QM_EINVAL;
    }
    ret = transact(client, DISPLAY_CMD_GET_FW_VERSION, NULL, 0U,
                   CONFIG_QM_DISPLAY_CLIENT_FW_TIMEOUT_MS,
                   CONFIG_QM_DISPLAY_CLIENT_FW_RETRY_MAX, &response, result);
    if (ret != QM_EOK) {
        return ret;
    }
    if (result->status != QM_DISPLAY_STATUS_OK ||
        (response.payload_length != 4U && response.payload_length != 5U)) {
        return result->status == QM_DISPLAY_STATUS_OK ? -QM_EIO : QM_EOK;
    }
    info->major = response.payload[1];
    info->minor = response.payload[2];
    info->patch = response.payload[3];
    info->state = QM_DISPLAY_FW_STATE_UNKNOWN;
    if (response.payload_length == 5U) {
        if (response.payload[4] > QM_DISPLAY_FW_UPGRADE_PENDING) {
            return -QM_EIO;
        }
        info->state = (qm_display_fw_state_t)response.payload[4];
    }
    return QM_EOK;
}

qm_err_t qm_display_client_poll(qm_display_client_handle_t client,
                                uint32_t timeout_ms)
{
    qm_utils_time_t deadline;
    display_frame_t frame;
    qm_err_t ret;

    if (client == NULL) {
        return -QM_EINVAL;
    }
    if (client->request_active) {
        return -QM_EBUSY;
    }
    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    ret = receive_frame(client, client->rx_frame, &deadline, &frame);
    if (ret != QM_EOK) {
        return ret;
    }
    if ((frame.flags & DISPLAY_FLAG_EVENT) == 0U) {
        return -QM_EIO;
    }
    return handle_event(client, &frame);
}

qm_err_t qm_display_client_display(qm_display_client_handle_t client,
                                   const qm_display_item_t *items,
                                   uint8_t item_count,
                                   uint16_t activate_delay_ms,
                                   qm_display_result_t *result)
{
    uint8_t payload[11];
    qm_display_screen_mask_t screen_mask = 0;
    display_response_t response;
    uint8_t i;
    qm_err_t ret;

    if (client == NULL || items == NULL || result == NULL ||
        item_count == 0U || item_count > DISPLAY_MAX_SCREEN_COUNT ||
        activate_delay_ms > DISPLAY_ACTIVATE_DELAY_MAX_MS) {
        return -QM_EINVAL;
    }
    payload[0] = item_count;
    for (i = 0U; i < item_count; ++i) {
        uint8_t bit;

        if (items[i].screen_id < 1U || items[i].screen_id > 2U ||
            items[i].source > QM_DISPLAY_SOURCE_RAM_READY ||
            items[i].asset_id == 0U) {
            return -QM_EINVAL;
        }
        bit = (uint8_t)(1U << (items[i].screen_id - 1U));
        if (((uint8_t)screen_mask & bit) != 0U) {
            return -QM_EINVAL;
        }
        screen_mask = (qm_display_screen_mask_t)((uint8_t)screen_mask | bit);
        if (items[i].source == QM_DISPLAY_SOURCE_RAM_READY &&
            (!client->screens[items[i].screen_id - 1U].has_ram_ready ||
             client->screens[items[i].screen_id - 1U].ram_ready_asset_id != items[i].asset_id)) {
            return -QM_EINVAL;
        }
        payload[1U + i * 4U] = items[i].screen_id;
        payload[2U + i * 4U] = (uint8_t)items[i].source;
        put_u16_be(&payload[3U + i * 4U], items[i].asset_id);
    }
    if (item_count == 2U && screen_mask != QM_DISPLAY_SCREEN_BOTH) {
        return -QM_EINVAL;
    }
    put_u16_be(&payload[1U + item_count * 4U], activate_delay_ms);

    ret = transact(client, DISPLAY_CMD_DISPLAY, payload,
                   (uint16_t)(3U + item_count * 4U),
                   (uint32_t)activate_delay_ms + CONFIG_QM_DISPLAY_CLIENT_DISPLAY_EXTRA_TIMEOUT_MS,
                   CONFIG_QM_DISPLAY_CLIENT_DISPLAY_RETRY_MAX, &response, result);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = require_success_payload(&response, result, 1U);
    if (ret != QM_EOK || result->status != QM_DISPLAY_STATUS_OK) {
        return ret;
    }
    for (i = 0U; i < item_count; ++i) {
        display_screen_state_t *screen = &client->screens[items[i].screen_id - 1U];

        if (items[i].source == QM_DISPLAY_SOURCE_RAM_READY) {
            screen->has_ram_ready = false;
        }
    }
    return QM_EOK;
}

static qm_err_t set_screen_parameter(qm_display_client_handle_t client,
                                     uint8_t command,
                                     qm_display_screen_mask_t screen_mask,
                                     uint8_t value, uint8_t max_value,
                                     qm_display_result_t *result)
{
    uint8_t payload[2];
    display_response_t response;
    qm_err_t ret;

    if (client == NULL || result == NULL || !valid_screen_mask(screen_mask) ||
        value > max_value) {
        return -QM_EINVAL;
    }
    payload[0] = (uint8_t)screen_mask;
    payload[1] = value;
    ret = transact(client, command, payload, sizeof(payload),
                   CONFIG_QM_DISPLAY_CLIENT_SCREEN_CONTROL_TIMEOUT_MS,
                   CONFIG_QM_DISPLAY_CLIENT_SCREEN_CONTROL_RETRY_MAX,
                   &response, result);
    if (ret != QM_EOK) {
        return ret;
    }
    return require_success_payload(&response, result, 1U);
}

qm_err_t qm_display_client_set_screen_power(
    qm_display_client_handle_t client, qm_display_screen_mask_t screen_mask,
    qm_display_power_state_t power_state, qm_display_result_t *result)
{
    return set_screen_parameter(client, DISPLAY_CMD_SET_SCREEN_POWER,
                                screen_mask, (uint8_t)power_state,
                                (uint8_t)QM_DISPLAY_POWER_ON, result);
}

qm_err_t qm_display_client_set_brightness(
    qm_display_client_handle_t client, qm_display_screen_mask_t screen_mask,
    uint8_t brightness, qm_display_result_t *result)
{
    return set_screen_parameter(client, DISPLAY_CMD_SET_BRIGHTNESS,
                                screen_mask, brightness, 100U, result);
}

static qm_err_t begin_asset(qm_display_client_handle_t client,
                            const qm_display_asset_upload_t *upload,
                            qm_display_result_t *result)
{
    uint8_t payload[8];
    display_response_t response;
    uint8_t screen_id;
    qm_err_t ret;

    if (!valid_screen_mask(upload->screen_mask) || upload->asset_id == 0U ||
        upload->total_size == 0U || upload->mode > QM_DISPLAY_ASSET_FLASH_UPDATE ||
        (upload->screen_mask == QM_DISPLAY_SCREEN_BOTH &&
         upload->mode == QM_DISPLAY_ASSET_RAM_AUTO_PRESENT)) {
        return -QM_EINVAL;
    }
    payload[0] = (uint8_t)upload->screen_mask;
    put_u16_be(&payload[1], upload->asset_id);
    payload[3] = (uint8_t)upload->mode;
    put_u32_be(&payload[4], upload->total_size);

    ret = transact(client, DISPLAY_CMD_ASSET_BEGIN, payload, sizeof(payload),
                   CONFIG_QM_DISPLAY_CLIENT_ASSET_BEGIN_TIMEOUT_MS,
                   CONFIG_QM_DISPLAY_CLIENT_ASSET_BEGIN_RETRY_MAX,
                   &response, result);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = require_success_payload(&response, result, 1U);
    if (ret != QM_EOK || result->status != QM_DISPLAY_STATUS_OK) {
        return ret;
    }

    for (screen_id = 1U; screen_id <= DISPLAY_MAX_SCREEN_COUNT; ++screen_id) {
        if (mask_has_screen(upload->screen_mask, screen_id) &&
            upload->mode != QM_DISPLAY_ASSET_FLASH_UPDATE) {
            client->screens[screen_id - 1U].has_ram_ready = false;
        }
    }
    return QM_EOK;
}

static qm_err_t send_asset_data(qm_display_client_handle_t client,
                                const qm_display_asset_upload_t *upload,
                                uint32_t offset, const uint8_t *data,
                                uint16_t data_length,
                                qm_display_result_t *result)
{
    uint8_t *payload;
    display_response_t response;
    qm_err_t ret;

    if (data == NULL || data_length == 0U || data_length > DISPLAY_MAX_CHUNK_LEN ||
        offset > upload->total_size || upload->total_size - offset < data_length ||
        (data_length < DISPLAY_MIN_CHUNK_LEN &&
         offset + data_length != upload->total_size)) {
        return -QM_EINVAL;
    }

    payload = &client->tx_frame[DISPLAY_HEADER_LEN];
    payload[0] = (uint8_t)upload->screen_mask;
    put_u16_be(&payload[1], upload->asset_id);
    put_u32_be(&payload[3], offset);
    memmove(&payload[DISPLAY_ASSET_DATA_HEADER_LEN], data, data_length);

    ret = transact(client, DISPLAY_CMD_ASSET_DATA, payload,
                   (uint16_t)(DISPLAY_ASSET_DATA_HEADER_LEN + data_length),
                   CONFIG_QM_DISPLAY_CLIENT_ASSET_DATA_TIMEOUT_MS,
                   CONFIG_QM_DISPLAY_CLIENT_ASSET_DATA_RETRY_MAX,
                   &response, result);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = require_success_payload(&response, result, 1U);
    if (ret != QM_EOK || result->status != QM_DISPLAY_STATUS_OK) {
        return ret;
    }
    return QM_EOK;
}

static qm_err_t finish_asset(qm_display_client_handle_t client,
                             const qm_display_asset_upload_t *upload,
                             uint16_t asset_crc16,
                             qm_display_result_t *result)
{
    uint8_t payload[5];
    display_response_t response;
    uint8_t screen_id;
    qm_err_t ret;

    payload[0] = (uint8_t)upload->screen_mask;
    put_u16_be(&payload[1], upload->asset_id);
    put_u16_be(&payload[3], asset_crc16);

    ret = transact(client, DISPLAY_CMD_ASSET_END, payload, sizeof(payload),
                   CONFIG_QM_DISPLAY_CLIENT_ASSET_END_TIMEOUT_MS,
                   CONFIG_QM_DISPLAY_CLIENT_ASSET_END_RETRY_MAX,
                   &response, result);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = require_success_payload(&response, result, 1U);
    if (ret != QM_EOK || result->status != QM_DISPLAY_STATUS_OK) {
        return ret;
    }

    for (screen_id = 1U; screen_id <= DISPLAY_MAX_SCREEN_COUNT; ++screen_id) {
        display_screen_state_t *screen;

        if (!mask_has_screen(upload->screen_mask, screen_id)) {
            continue;
        }
        screen = &client->screens[screen_id - 1U];
        if (upload->mode == QM_DISPLAY_ASSET_RAM_DEFERRED) {
            screen->has_ram_ready = true;
            screen->ram_ready_asset_id = upload->asset_id;
        } else if (upload->mode == QM_DISPLAY_ASSET_RAM_AUTO_PRESENT) {
            screen->has_ram_ready = false;
        }
    }
    return QM_EOK;
}

qm_err_t qm_display_client_upload_asset(
    qm_display_client_handle_t client, const qm_display_asset_upload_t *upload,
    qm_display_result_t *result)
{
    uint8_t restart_count = 0U;
    qm_err_t ret;

    if (client == NULL || upload == NULL || upload->read == NULL ||
        result == NULL) {
        return -QM_EINVAL;
    }

    for (;;) {
        uint32_t offset = 0U;
        uint16_t asset_crc16 = DISPLAY_CRC16_INIT;

        ret = begin_asset(client, upload, result);
        if (ret != QM_EOK || result->status != QM_DISPLAY_STATUS_OK) {
            return ret;
        }

        while (offset < upload->total_size) {
            uint32_t remaining = upload->total_size - offset;
            uint16_t max_length = remaining > CONFIG_QM_DISPLAY_CLIENT_ASSET_CHUNK_SIZE ?
                                  CONFIG_QM_DISPLAY_CLIENT_ASSET_CHUNK_SIZE :
                                  (uint16_t)remaining;
            uint16_t data_length = 0U;
            uint8_t *data = &client->tx_frame[
                DISPLAY_HEADER_LEN + DISPLAY_ASSET_DATA_HEADER_LEN];

            ret = read_upload_chunk(upload->read, upload->read_user, offset,
                                    data, max_length, &data_length);
            if (ret != QM_EOK) {
                return ret;
            }
            asset_crc16 = qm_crc16_ccitt_false_update(asset_crc16, data, data_length);
            ret = send_asset_data(client, upload, offset, data, data_length, result);
            if (ret != QM_EOK) {
                return ret;
            }
            if (result->status == QM_DISPLAY_STATUS_OFFSET_ERROR) {
                if (restart_count >= CONFIG_QM_DISPLAY_CLIENT_ASSET_OFFSET_RESTART_MAX) {
                    return QM_EOK;
                }
                ++restart_count;
                QM_DISPLAY_CLIENT_LOGW(DISPLAY_LOG_TAG, QM_DISPLAY_CLIENT_LOG_CONTROL,
                                       "restart asset after offset error count=%u",
                                       restart_count);
                break;
            }
            if (result->status != QM_DISPLAY_STATUS_OK) {
                return QM_EOK;
            }
            offset += data_length;
        }
        if (offset < upload->total_size) {
            continue;
        }
        ret = finish_asset(client, upload, asset_crc16, result);
        return ret;
    }
}

#if CONFIG_QM_DISPLAY_CLIENT_OTA_SUPPORT

static qm_err_t begin_ota(qm_display_client_handle_t client,
                          const qm_display_ota_upload_t *upload,
                          qm_display_result_t *result)
{
    uint8_t payload[4];
    display_response_t response;
    qm_err_t ret;

    if (upload->total_size == 0U) {
        return -QM_EINVAL;
    }
    put_u32_be(payload, upload->total_size);
    ret = transact(client, DISPLAY_CMD_OTA_BEGIN, payload, sizeof(payload),
                   CONFIG_QM_DISPLAY_CLIENT_OTA_BEGIN_TIMEOUT_MS,
                   CONFIG_QM_DISPLAY_CLIENT_OTA_BEGIN_RETRY_MAX,
                   &response, result);
    if (ret != QM_EOK) {
        return ret;
    }
    return require_success_payload(&response, result, 1U);
}

static qm_err_t send_ota_data(qm_display_client_handle_t client,
                              const qm_display_ota_upload_t *upload,
                              uint32_t offset, const uint8_t *data,
                              uint16_t data_length,
                              qm_display_result_t *result)
{
    uint8_t *payload = &client->tx_frame[DISPLAY_HEADER_LEN];
    display_response_t response;
    qm_err_t ret;

    if (data == NULL || data_length == 0U || data_length > DISPLAY_MAX_OTA_CHUNK_LEN ||
        offset > upload->total_size || upload->total_size - offset < data_length) {
        return -QM_EINVAL;
    }
    put_u32_be(payload, offset);
    memmove(&payload[4], data, data_length);
    ret = transact(client, DISPLAY_CMD_OTA_DATA, payload,
                   (uint16_t)(4U + data_length),
                   CONFIG_QM_DISPLAY_CLIENT_OTA_DATA_TIMEOUT_MS,
                   CONFIG_QM_DISPLAY_CLIENT_OTA_DATA_RETRY_MAX,
                   &response, result);
    if (ret != QM_EOK) {
        return ret;
    }
    return require_success_payload(&response, result, 1U);
}

static qm_err_t finish_ota(qm_display_client_handle_t client,
                           uint16_t firmware_crc16,
                           qm_display_result_t *result)
{
    uint8_t payload[2];
    display_response_t response;
    qm_err_t ret;

    put_u16_be(payload, firmware_crc16);
    ret = transact(client, DISPLAY_CMD_OTA_END, payload, sizeof(payload),
                   CONFIG_QM_DISPLAY_CLIENT_OTA_END_TIMEOUT_MS,
                   CONFIG_QM_DISPLAY_CLIENT_OTA_END_RETRY_MAX,
                   &response, result);
    if (ret != QM_EOK) {
        return ret;
    }
    return require_success_payload(&response, result, 1U);
}

static bool_t fw_version_equal(const qm_display_fw_info_t *left,
                               const qm_display_fw_info_t *right)
{
    return left->major == right->major && left->minor == right->minor &&
           left->patch == right->patch;
}

qm_err_t qm_display_client_upload_ota(
    qm_display_client_handle_t client, const qm_display_ota_upload_t *upload,
    qm_display_result_t *result)
{
    uint8_t restart_count = 0U;
    qm_err_t ret;

    if (client == NULL || upload == NULL || upload->read == NULL ||
        result == NULL || upload->total_size == 0U) {
        return -QM_EINVAL;
    }
    for (;;) {
        uint32_t offset = 0U;
        uint16_t firmware_crc16 = DISPLAY_CRC16_INIT;

        ret = begin_ota(client, upload, result);
        if (ret != QM_EOK) {
            return ret;
        }
        if (result->status != QM_DISPLAY_STATUS_OK) {
            return QM_EOK;
        }
        while (offset < upload->total_size) {
            uint32_t remaining = upload->total_size - offset;
            uint16_t max_length = remaining > CONFIG_QM_DISPLAY_CLIENT_OTA_CHUNK_SIZE ?
                                  CONFIG_QM_DISPLAY_CLIENT_OTA_CHUNK_SIZE :
                                  (uint16_t)remaining;
            uint16_t data_length = 0U;
            uint8_t *data = &client->tx_frame[DISPLAY_HEADER_LEN + 4U];

            ret = read_upload_chunk(upload->read, upload->read_user, offset,
                                    data, max_length, &data_length);
            if (ret != QM_EOK) {
                return ret;
            }
            firmware_crc16 = qm_crc16_ccitt_false_update(firmware_crc16, data,
                                                         data_length);
            ret = send_ota_data(client, upload, offset, data, data_length, result);
            if (ret != QM_EOK) {
                return ret;
            }
            if (result->status == QM_DISPLAY_STATUS_OFFSET_ERROR) {
                if (restart_count >= CONFIG_QM_DISPLAY_CLIENT_OTA_OFFSET_RESTART_MAX) {
                    return QM_EOK;
                }
                ++restart_count;
                break;
            }
            if (result->status != QM_DISPLAY_STATUS_OK) {
                return QM_EOK;
            }
            offset += data_length;
        }
        if (offset < upload->total_size) {
            continue;
        }
        ret = finish_ota(client, firmware_crc16, result);
        if (ret == -QM_ETIMEOUT && upload->verify_version) {
            qm_display_fw_info_t actual;

            ret = qm_display_client_get_fw_version(client, &actual, result);
            if (ret == QM_EOK && result->status == QM_DISPLAY_STATUS_OK &&
                fw_version_equal(&actual, &upload->expected_version)) {
                return QM_EOK;
            }
        }
        return ret;
    }
}

#else

qm_err_t qm_display_client_upload_ota(
    qm_display_client_handle_t client, const qm_display_ota_upload_t *upload,
    qm_display_result_t *result)
{
    (void)client;
    (void)upload;
    (void)result;
    return -QM_ENOSYS;
}

#endif
