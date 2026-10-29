#include "ymodem.h"

#include "qm.h"
#include "qm_crc.h"
#include "qm_string.h"
#include "qm_utils_timer.h"

#define YMODEM_SOH                  0x01U
#define YMODEM_STX                  0x02U
#define YMODEM_EOT                  0x04U
#define YMODEM_ACK                  0x06U
#define YMODEM_NAK                  0x15U
#define YMODEM_CAN                  0x18U
#define YMODEM_CRC_REQUEST          0x43U
#define YMODEM_HEADER_BLOCK         0U
#define YMODEM_FIRST_DATA_BLOCK     1U
#define YMODEM_SHORT_DATA_SIZE      128U
#define YMODEM_LONG_DATA_SIZE       1024U
#define YMODEM_FRAME_OVERHEAD       5U
#define YMODEM_MAX_FRAME_SIZE       (YMODEM_LONG_DATA_SIZE + YMODEM_FRAME_OVERHEAD)
#define YMODEM_CANCEL_BYTE_COUNT    8U

#ifndef CONFIG_YMODEM_HANDSHAKE_TIMEOUT_MS
#define CONFIG_YMODEM_HANDSHAKE_TIMEOUT_MS 5000U
#endif

#ifndef CONFIG_YMODEM_PACKET_TIMEOUT_MS
#define CONFIG_YMODEM_PACKET_TIMEOUT_MS 1000U
#endif

#ifndef CONFIG_YMODEM_RETRY_MAX
#define CONFIG_YMODEM_RETRY_MAX 10U
#endif

#ifndef CONFIG_YMODEM_FILE_NAME_MAX_LEN
#define CONFIG_YMODEM_FILE_NAME_MAX_LEN 64U
#endif

#ifndef CONFIG_YMODEM_MAX_FILE_SIZE
#define CONFIG_YMODEM_MAX_FILE_SIZE 0x00200000U
#endif

typedef enum {
    YMODEM_FRAME_PACKET = 1,
    YMODEM_FRAME_EOT,
    YMODEM_FRAME_CAN,
    YMODEM_FRAME_TIMEOUT,
    YMODEM_FRAME_IO,
} ymodem_frame_kind_t;

typedef struct {
    ymodem_frame_kind_t kind;
    uint16_t payload_length;
    uint8_t sequence;
    uint8_t valid;
} ymodem_frame_t;

struct ymodem {
    ymodem_config_t config;
    uint8_t packet[YMODEM_MAX_FRAME_SIZE];
    char file_name[CONFIG_YMODEM_FILE_NAME_MAX_LEN + 1U];
    uint32_t file_size;
    uint8_t active;
};

static qm_err_t normalize_callback_error(qm_err_t ret)
{
    if (ret == QM_EOK) {
        return QM_EOK;
    }
    if (ret < 0) {
        return ret;
    }
    return -QM_EIO;
}

static qm_err_t write_exact(ymodem_handle_t receiver, const uint8_t *data,
                            uint32_t length, uint32_t timeout_ms)
{
    qm_utils_time_t deadline = {0};
    uint32_t offset = 0U;
    uint32_t time_left = 0U;
    int32_t count = 0;

    if (receiver == NULL || data == NULL || length == 0U) {
        return -QM_EINVAL;
    }
    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    while (offset < length) {
        time_left = qm_utils_time_left(&deadline);
        if (time_left == 0U) {
            return -QM_ETIMEOUT;
        }
        count = receiver->config.write(receiver->config.transport_user,
                                       &data[offset], length - offset,
                                       time_left);
        if (count == -QM_ETIMEOUT) {
            return -QM_ETIMEOUT;
        }
        if (count < 0) {
            return -QM_EIO;
        }
        if (count == 0 || (uint32_t)count > length - offset) {
            return (count == 0) ? -QM_ETIMEOUT : -QM_EIO;
        }
        offset += (uint32_t)count;
    }
    return QM_EOK;
}

static qm_err_t read_one(ymodem_handle_t receiver, qm_utils_time_t *deadline,
                         uint8_t *value)
{
    uint32_t time_left = 0U;
    int32_t count = 0;

    if (receiver == NULL || deadline == NULL || value == NULL) {
        return -QM_EINVAL;
    }
    time_left = qm_utils_time_left(deadline);
    if (time_left == 0U) {
        return -QM_ETIMEOUT;
    }
    count = receiver->config.read(receiver->config.transport_user, value, 1U,
                                  time_left);
    if (count == -QM_ETIMEOUT) {
        return -QM_ETIMEOUT;
    }
    if (count < 0) {
        return -QM_EIO;
    }
    if (count == 0) {
        return -QM_ETIMEOUT;
    }
    if (count != 1) {
        return -QM_EIO;
    }
    return QM_EOK;
}

static qm_err_t read_exact(ymodem_handle_t receiver, qm_utils_time_t *deadline,
                           uint8_t *data, uint32_t length,
                           uint32_t *received_length)
{
    uint32_t offset = 0U;
    uint32_t time_left = 0U;
    int32_t count = 0;

    if (receiver == NULL || deadline == NULL || data == NULL ||
        length == 0U || received_length == NULL) {
        return -QM_EINVAL;
    }
    *received_length = 0U;
    while (offset < length) {
        time_left = qm_utils_time_left(deadline);
        if (time_left == 0U) {
            return -QM_ETIMEOUT;
        }
        count = receiver->config.read(receiver->config.transport_user,
                                      &data[offset], length - offset,
                                      time_left);
        if (count == -QM_ETIMEOUT) {
            return -QM_ETIMEOUT;
        }
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
        *received_length = offset;
    }
    return QM_EOK;
}

static qm_err_t send_control(ymodem_handle_t receiver, uint8_t value)
{
    return write_exact(receiver, &value, 1U, receiver->config.packet_timeout_ms);
}

static qm_err_t send_cancel(ymodem_handle_t receiver)
{
    uint8_t cancel[YMODEM_CANCEL_BYTE_COUNT] = {0};
    uint32_t index = 0U;

    for (index = 0U; index < YMODEM_CANCEL_BYTE_COUNT; index++) {
        cancel[index] = YMODEM_CAN;
    }
    return write_exact(receiver, cancel, sizeof(cancel),
                       receiver->config.packet_timeout_ms);
}

static ymodem_frame_kind_t map_read_error(qm_err_t ret)
{
    if (ret == -QM_ETIMEOUT) {
        return YMODEM_FRAME_TIMEOUT;
    }
    return YMODEM_FRAME_IO;
}

static ymodem_frame_kind_t read_frame(ymodem_handle_t receiver,
                                      uint32_t timeout_ms,
                                      ymodem_frame_t *frame)
{
    qm_utils_time_t deadline = {0};
    qm_err_t ret = QM_EOK;
    uint8_t start = 0U;
    uint16_t payload_length = 0U;
    uint16_t received_crc = 0U;
    uint16_t calculated_crc = 0U;
    uint32_t frame_bytes_read = 0U;
    uint32_t frame_bytes_expected = 0U;
    qm_utils_time_t discard_deadline = {0};
    uint32_t discarded_bytes = 0U;

    if (receiver == NULL || frame == NULL) {
        return YMODEM_FRAME_IO;
    }
    qm_memset(frame, 0, sizeof(*frame));
    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    for (;;) {
        ret = read_one(receiver, &deadline, &start);
        if (ret != QM_EOK) {
            return map_read_error(ret);
        }
        if (start == YMODEM_SOH || start == YMODEM_STX ||
            start == YMODEM_EOT || start == YMODEM_CAN) {
            break;
        }
    }
    if (start == YMODEM_EOT) {
        frame->kind = YMODEM_FRAME_EOT;
        return frame->kind;
    }
    if (start == YMODEM_CAN) {
        frame->kind = YMODEM_FRAME_CAN;
        return frame->kind;
    }

    payload_length = (start == YMODEM_SOH) ? YMODEM_SHORT_DATA_SIZE :
                     YMODEM_LONG_DATA_SIZE;
    receiver->packet[0] = start;
    frame_bytes_expected = (uint32_t)payload_length + 4U;
    ret = read_exact(receiver, &deadline, &receiver->packet[1U],
                     frame_bytes_expected, &frame_bytes_read);
    if (ret != QM_EOK) {
        if (ret == -QM_ETIMEOUT && frame_bytes_read < frame_bytes_expected) {
            qm_utils_time_countdown_ms(&discard_deadline,
                                       receiver->config.packet_timeout_ms);
            (void)read_exact(receiver, &discard_deadline,
                             &receiver->packet[1U + frame_bytes_read],
                             frame_bytes_expected - frame_bytes_read,
                             &discarded_bytes);
        }
        return map_read_error(ret);
    }
    frame->kind = YMODEM_FRAME_PACKET;
    frame->payload_length = payload_length;
    frame->sequence = receiver->packet[1];
    if ((uint8_t)~receiver->packet[1] != receiver->packet[2]) {
        return frame->kind;
    }
    received_crc = (uint16_t)(((uint16_t)receiver->packet[3U + payload_length] << 8) |
                              receiver->packet[4U + payload_length]);
    calculated_crc = qm_crc16_xmodem(&receiver->packet[3], payload_length);
    frame->valid = (received_crc == calculated_crc) ? 1U : 0U;
    return frame->kind;
}

static uint8_t decimal_digit(uint8_t value)
{
    return (value >= (uint8_t)'0' && value <= (uint8_t)'9') ? 1U : 0U;
}

static qm_err_t parse_file_header(ymodem_handle_t receiver,
                                  const ymodem_frame_t *frame)
{
    const uint8_t *payload = NULL;
    uint32_t name_length = 0U;
    uint32_t size_index = 0U;
    uint32_t size = 0U;
    uint8_t character = 0U;
    uint8_t have_size_digit = 0U;

    if (receiver == NULL || frame == NULL || frame->kind != YMODEM_FRAME_PACKET ||
        frame->valid == 0U || frame->sequence != YMODEM_HEADER_BLOCK) {
        return -QM_EINVAL;
    }
    payload = &receiver->packet[3];
    while (name_length < frame->payload_length && payload[name_length] != 0U) {
        character = payload[name_length];
        if (character < 0x20U || character > 0x7EU || character == '/' ||
            character == '\\') {
            return -QM_EINVAL;
        }
        name_length++;
    }
    if (name_length == 0U || name_length >= frame->payload_length ||
        name_length > (uint32_t)CONFIG_YMODEM_FILE_NAME_MAX_LEN) {
        return -QM_EINVAL;
    }
    size_index = name_length + 1U;
    while (size_index < frame->payload_length &&
           decimal_digit(payload[size_index]) != 0U) {
        character = payload[size_index];
        have_size_digit = 1U;
        if (size > (UINT32_MAX - (uint32_t)(character - (uint8_t)'0')) / 10U) {
            return -QM_EINVAL;
        }
        size = size * 10U + (uint32_t)(character - (uint8_t)'0');
        size_index++;
    }
    if (have_size_digit == 0U || size > receiver->config.max_file_size) {
        return -QM_EINVAL;
    }
    while (size_index < frame->payload_length && payload[size_index] != 0U) {
        size_index++;
    }
    if (size_index >= frame->payload_length) {
        return -QM_EINVAL;
    }
    qm_memcpy(receiver->file_name, payload, name_length);
    receiver->file_name[name_length] = '\0';
    receiver->file_size = size;
    return QM_EOK;
}

static uint8_t is_empty_header(ymodem_handle_t receiver,
                               const ymodem_frame_t *frame)
{
    uint16_t index = 0U;

    if (receiver == NULL || frame == NULL || frame->kind != YMODEM_FRAME_PACKET ||
        frame->valid == 0U || frame->sequence != YMODEM_HEADER_BLOCK) {
        return 0U;
    }
    for (index = 0U; index < frame->payload_length; index++) {
        if (receiver->packet[3U + index] != 0U) {
            return 0U;
        }
    }
    return 1U;
}

static qm_err_t notify_progress(ymodem_handle_t receiver, uint32_t transferred,
                                uint32_t total_size)
{
    ymodem_progress_t progress = {0};
    qm_err_t ret = QM_EOK;

    if (receiver->config.progress == NULL) {
        return QM_EOK;
    }
    progress.transferred = transferred;
    progress.total_size = total_size;
    ret = receiver->config.progress(&progress, receiver->config.progress_user);
    return (ret == QM_EOK) ? QM_EOK : -QM_EINTR;
}

static qm_err_t receive_initial_header(ymodem_handle_t receiver,
                                       ymodem_frame_t *frame)
{
    uint32_t attempt = 0U;
    ymodem_frame_kind_t kind = YMODEM_FRAME_TIMEOUT;
    qm_err_t ret = QM_EOK;

    for (attempt = 0U; attempt <= receiver->config.retry_max; attempt++) {
        ret = send_control(receiver, YMODEM_CRC_REQUEST);
        if (ret != QM_EOK) {
            return ret;
        }
        kind = read_frame(receiver, receiver->config.handshake_timeout_ms, frame);
        if (kind == YMODEM_FRAME_CAN) {
            return -QM_EINTR;
        }
        if (kind == YMODEM_FRAME_TIMEOUT) {
            ret = -QM_ETIMEOUT;
            continue;
        }
        if (kind == YMODEM_FRAME_IO) {
            ret = -QM_EIO;
            return ret;
        }
        if (frame->sequence != YMODEM_HEADER_BLOCK || frame->valid == 0U) {
            ret = -QM_EIO;
            if (attempt >= receiver->config.retry_max) {
                return ret;
            }
            ret = send_control(receiver, YMODEM_NAK);
            if (ret != QM_EOK) {
                return ret;
            }
            continue;
        }
        ret = parse_file_header(receiver, frame);
        if (ret != QM_EOK) {
            return ret;
        }
        return QM_EOK;
    }
    return ret;
}

static qm_err_t receive_final_header(ymodem_handle_t receiver,
                                     ymodem_frame_t *frame)
{
    uint32_t attempt = 0U;
    ymodem_frame_kind_t kind = YMODEM_FRAME_TIMEOUT;
    qm_err_t ret = QM_EOK;

    ret = send_control(receiver, YMODEM_CRC_REQUEST);
    if (ret != QM_EOK) {
        return ret;
    }
    for (attempt = 0U; attempt <= receiver->config.retry_max; attempt++) {
        kind = read_frame(receiver, receiver->config.handshake_timeout_ms, frame);
        if (kind == YMODEM_FRAME_CAN) {
            return -QM_EINTR;
        }
        if (kind == YMODEM_FRAME_EOT) {
            ret = send_control(receiver, YMODEM_ACK);
            if (ret == QM_EOK) {
                ret = send_control(receiver, YMODEM_CRC_REQUEST);
            }
            if (ret != QM_EOK) {
                return ret;
            }
            if (attempt >= receiver->config.retry_max) {
                return -QM_ETIMEOUT;
            }
            continue;
        }
        if (kind == YMODEM_FRAME_TIMEOUT) {
            ret = -QM_ETIMEOUT;
            if (attempt >= receiver->config.retry_max) {
                return ret;
            }
            ret = send_control(receiver, YMODEM_CRC_REQUEST);
            if (ret != QM_EOK) {
                return ret;
            }
            continue;
        }
        if (kind == YMODEM_FRAME_IO) {
            return -QM_EIO;
        }
        if (is_empty_header(receiver, frame) != 0U) {
            return QM_EOK;
        }
        ret = -QM_EIO;
        if (attempt >= receiver->config.retry_max) {
            return ret;
        }
        ret = send_control(receiver, YMODEM_NAK);
        if (ret != QM_EOK) {
            return ret;
        }
    }
    return ret;
}

static qm_err_t receive_eot(ymodem_handle_t receiver, uint8_t *pending_final)
{
    uint32_t attempt = 0U;
    ymodem_frame_t frame = {0};
    ymodem_frame_kind_t kind = YMODEM_FRAME_TIMEOUT;
    qm_err_t ret = QM_EOK;

    if (pending_final == NULL) {
        return -QM_EINVAL;
    }
    *pending_final = 0U;
    ret = send_control(receiver, YMODEM_NAK);
    if (ret != QM_EOK) {
        return ret;
    }
    for (attempt = 0U; attempt <= receiver->config.retry_max; attempt++) {
        kind = read_frame(receiver, receiver->config.packet_timeout_ms, &frame);
        if (kind == YMODEM_FRAME_EOT) {
            ret = send_control(receiver, YMODEM_ACK);
            return ret;
        }
        if (kind == YMODEM_FRAME_CAN) {
            return -QM_EINTR;
        }
        if (kind == YMODEM_FRAME_TIMEOUT) {
            /* A few PC tools accept ACK for the first EOT directly. */
            ret = send_control(receiver, YMODEM_ACK);
            return ret;
        }
        if (kind == YMODEM_FRAME_IO) {
            return -QM_EIO;
        }
        if (is_empty_header(receiver, &frame) != 0U) {
            *pending_final = 1U;
            return QM_EOK;
        }
        ret = -QM_EIO;
        if (attempt >= receiver->config.retry_max) {
            return ret;
        }
        ret = send_control(receiver, YMODEM_NAK);
        if (ret != QM_EOK) {
            return ret;
        }
    }
    return ret;
}

static qm_err_t fail_session(ymodem_handle_t receiver, qm_err_t ret,
                             uint8_t storage_started, uint8_t remote_cancel,
                             uint8_t storage_committed)
{
    if (storage_started != 0U) {
        receiver->config.storage.abort(receiver->config.storage.user);
    }
    if (remote_cancel == 0U && storage_committed == 0U) {
        (void)send_cancel(receiver);
    }
    return ret;
}

ymodem_handle_t ymodem_create(const ymodem_config_t *config)
{
    ymodem_handle_t receiver = NULL;

    if (config == NULL || config->write == NULL || config->read == NULL ||
        config->storage.begin == NULL || config->storage.write == NULL ||
        config->storage.finish == NULL || config->storage.abort == NULL ||
        config->handshake_timeout_ms > UINT32_MAX / 2U ||
        config->packet_timeout_ms > UINT32_MAX / 2U) {
        return NULL;
    }
    receiver = (ymodem_handle_t)qm_malloc(sizeof(*receiver));
    if (receiver == NULL) {
        return NULL;
    }
    qm_memset(receiver, 0, sizeof(*receiver));
    receiver->config = *config;
    if (receiver->config.handshake_timeout_ms == 0U) {
        receiver->config.handshake_timeout_ms = CONFIG_YMODEM_HANDSHAKE_TIMEOUT_MS;
    }
    if (receiver->config.packet_timeout_ms == 0U) {
        receiver->config.packet_timeout_ms = CONFIG_YMODEM_PACKET_TIMEOUT_MS;
    }
    if (receiver->config.retry_max == 0U) {
        receiver->config.retry_max = CONFIG_YMODEM_RETRY_MAX;
    }
    if (receiver->config.max_file_size == 0U) {
        receiver->config.max_file_size = CONFIG_YMODEM_MAX_FILE_SIZE;
    }
    return receiver;
}

void ymodem_destroy(ymodem_handle_t receiver)
{
    if (receiver == NULL) {
        return;
    }
    qm_free(receiver);
}

qm_err_t ymodem_receive(ymodem_handle_t receiver)
{
    ymodem_frame_t frame = {0};
    ymodem_file_info_t file = {0};
    ymodem_frame_kind_t kind = YMODEM_FRAME_TIMEOUT;
    qm_err_t ret = QM_EOK;
    uint8_t storage_started = 0U;
    uint8_t storage_committed = 0U;
    uint8_t remote_cancel = 0U;
    uint8_t expected_block = YMODEM_FIRST_DATA_BLOCK;
    uint8_t previous_block = 0U;
    uint8_t have_previous = 0U;
    uint8_t retries = 0U;
    uint8_t pending_final = 0U;
    uint32_t received = 0U;
    uint32_t remaining = 0U;
    uint32_t valid_length = 0U;

    if (receiver == NULL) {
        return -QM_EINVAL;
    }
    if (receiver->active != 0U) {
        return -QM_EBUSY;
    }
    receiver->active = 1U;

    ret = receive_initial_header(receiver, &frame);
    if (ret != QM_EOK) {
        remote_cancel = (ret == -QM_EINTR) ? 1U : 0U;
        goto fail;
    }
    file.name = receiver->file_name;
    file.total_size = receiver->file_size;
    ret = normalize_callback_error(receiver->config.storage.begin(
        &file, receiver->config.storage.user));
    if (ret != QM_EOK) {
        goto fail;
    }
    storage_started = 1U;
    ret = send_control(receiver, YMODEM_ACK);
    if (ret != QM_EOK) {
        goto fail;
    }
    ret = send_control(receiver, YMODEM_CRC_REQUEST);
    if (ret != QM_EOK) {
        goto fail;
    }

    retries = 0U;
    for (;;) {
        kind = read_frame(receiver, receiver->config.packet_timeout_ms, &frame);
        if (kind == YMODEM_FRAME_EOT) {
            break;
        }
        if (kind == YMODEM_FRAME_CAN) {
            remote_cancel = 1U;
            ret = -QM_EINTR;
            goto fail;
        }
        if (kind == YMODEM_FRAME_TIMEOUT) {
            ret = -QM_ETIMEOUT;
        } else if (kind == YMODEM_FRAME_IO) {
            ret = -QM_EIO;
        } else if (frame.valid == 0U) {
            ret = -QM_EIO;
        } else if (frame.sequence == 0U && expected_block != 0U &&
                   have_previous == 0U) {
            /* A retransmitted block 0 means the previous ACK was lost. */
            ret = send_control(receiver, YMODEM_ACK);
            if (ret == QM_EOK) {
                ret = send_control(receiver, YMODEM_CRC_REQUEST);
            }
            if (ret != QM_EOK) {
                goto fail;
            }
            retries = 0U;
            continue;
        } else if (frame.sequence == expected_block) {
            if (received >= file.total_size) {
                ret = -QM_EINVAL;
            } else {
                remaining = file.total_size - received;
                valid_length = (frame.payload_length < remaining) ?
                               frame.payload_length : remaining;
                ret = normalize_callback_error(receiver->config.storage.write(
                    received, &receiver->packet[3], valid_length,
                    receiver->config.storage.user));
                if (ret == QM_EOK) {
                    received += valid_length;
                    ret = notify_progress(receiver, received, file.total_size);
                }
                if (ret == QM_EOK) {
                    ret = send_control(receiver, YMODEM_ACK);
                    previous_block = frame.sequence;
                    have_previous = 1U;
                    expected_block = (uint8_t)(expected_block + 1U);
                }
            }
            if (ret != QM_EOK) {
                goto fail;
            }
            retries = 0U;
            continue;
        } else if (have_previous != 0U &&
                   frame.sequence == previous_block) {
            ret = send_control(receiver, YMODEM_ACK);
            if (ret != QM_EOK) {
                goto fail;
            }
            retries = 0U;
            continue;
        } else {
            ret = -QM_EIO;
        }

        if (ret == -QM_ETIMEOUT || ret == -QM_EIO || ret == -QM_EINVAL) {
            if (retries >= receiver->config.retry_max) {
                goto fail;
            }
            retries++;
            ret = send_control(receiver, YMODEM_NAK);
            if (ret != QM_EOK) {
                goto fail;
            }
            continue;
        }
        goto fail;
    }
    ret = receive_eot(receiver, &pending_final);
    if (ret != QM_EOK) {
        remote_cancel = (ret == -QM_EINTR) ? 1U : 0U;
        goto fail;
    }
    if (pending_final == 0U) {
        ret = receive_final_header(receiver, &frame);
        if (ret != QM_EOK) {
            remote_cancel = (ret == -QM_EINTR) ? 1U : 0U;
            goto fail;
        }
    }
    if (received != file.total_size) {
        ret = -QM_EIO;
        goto fail;
    }
    ret = normalize_callback_error(receiver->config.storage.finish(
        &file, receiver->config.storage.user));
    if (ret != QM_EOK) {
        goto fail;
    }
    storage_committed = 1U;
    storage_started = 0U;
    ret = send_control(receiver, YMODEM_ACK);
    if (ret != QM_EOK) {
        goto fail;
    }
    receiver->active = 0U;
    return QM_EOK;

fail:
    ret = fail_session(receiver, ret, storage_started, remote_cancel,
                       storage_committed);
    receiver->active = 0U;
    return ret;
}
