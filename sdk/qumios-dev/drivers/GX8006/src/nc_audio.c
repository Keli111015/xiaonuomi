#include "nc_audio.h"
#include "qm_string.h"
#include "qm_utils_timer.h"
#include "qm_log.h"

#define LOG_TAG "nc_audio"

/* Synchronous bounded framing keeps callbacks within the caller's lifetime.
 * The former comm_base event queue could outlive UART teardown. */
#define NC_WIRE_PREFIX_BYTES 6U
#define NC_SPK_HEADER_BYTES 5U
#define NC_WIRE_OVERHEAD_BYTES 7U

typedef struct {
    nc_audio_param_t config;
    qm_mutex_t tx_lock;
    qm_utils_time_t deadline;
    uint32_t used;
    uint32_t expected;
    uint8_t rx[CONFIG_NC_AUDIO_RECV_MAX_SIZE + NC_WIRE_OVERHEAD_BYTES];
} nc_audio_ctx_t;

static nc_audio_ctx_t *g_comm_ctx;

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void write_be32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static uint8_t checksum(const uint8_t *data, uint32_t len)
{
    uint8_t sum = 0U;
    while (len-- != 0U) {
        sum = (uint8_t)(sum + *data++);
    }
    return sum;
}

static int send_frame(uint8_t command, const void *header, uint32_t header_size,
                      const void *payload, uint32_t payload_size, uint32_t timeout_ms)
{
    nc_audio_ctx_t *ctx = g_comm_ctx;
    uint8_t *frame;
    uint32_t data_size;
    uint32_t total;
    int ret;
    qm_utils_time_t deadline;

    if (ctx == NULL || (header_size != 0U && header == NULL) ||
        (payload_size != 0U && payload == NULL) ||
        header_size > UINT16_MAX - 1U ||
        payload_size > UINT16_MAX - 1U - header_size) {
        return -QM_EINVAL;
    }
    data_size = 1U + header_size + payload_size;
    total = NC_WIRE_OVERHEAD_BYTES + data_size;
    frame = (uint8_t *)qm_malloc(total);
    if (frame == NULL) {
        return -QM_ENOMEM;
    }
    frame[0] = NC_AUDIO_FRAME_HEADER_1;
    frame[1] = NC_AUDIO_FRAME_HEADER_2;
    frame[2] = NC_AUDIO_FRAME_VER_1;
    frame[3] = NC_AUDIO_FRAME_CMD;
    frame[4] = (uint8_t)(data_size >> 8);
    frame[5] = (uint8_t)data_size;
    frame[6] = command;
    if (header_size != 0U) {
        qm_memcpy(frame + NC_AUDIO_FRAME_HEAD_LEN, header, header_size);
    }
    if (payload_size != 0U) {
        qm_memcpy(frame + NC_AUDIO_FRAME_HEAD_LEN + header_size,
                  payload, payload_size);
    }
    frame[total - 1U] = checksum(frame, total - 1U);
    if (timeout_ms != QM_WAIT_FOREVER) qm_utils_time_countdown_ms(&deadline, timeout_ms);
    ret = qm_mutex_lock(&ctx->tx_lock, timeout_ms);
    if (ret == QM_EOK) {
        uint32_t left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
                        qm_utils_time_left(&deadline);
        ret = ctx->config.send(frame, (int)total, left);
        (void)qm_mutex_unlock(&ctx->tx_lock);
    }
    qm_free(frame);
    return ret;
}

static int configure_mic(uint8_t gain)
{
    const nc_audio_format_t *format = &g_comm_ctx->config.mic;
    uint8_t data[sizeof(nc_audio_mic_cfg_t)] = {0};
    write_be32(data, format->sample_rate_hz);
    data[4] = format->sample_bits;
    data[5] = format->channels;
    data[6] = gain;
    data[7] = (uint8_t)format->codec;
    write_be32(data + 8U, format->max_payload_bytes);
    return send_frame(NC_AUDIO_SUB_CMD_MIC_CFG, data, sizeof(data), NULL, 0U,
                       CONFIG_NC_AUDIO_SEND_TIMEOUT_MS);
}

static int configure_spk(uint8_t gain)
{
    const nc_audio_format_t *format = &g_comm_ctx->config.spk;
    uint8_t data[sizeof(nc_audio_spk_cfg_t)] = {0};
    write_be32(data, format->sample_rate_hz);
    data[4] = format->sample_bits;
    data[5] = gain;
    data[6] = (uint8_t)format->codec;
    write_be32(data + 7U, format->max_payload_bytes);
    return send_frame(NC_AUDIO_SUB_CMD_SPK_CFG, data, sizeof(data), NULL, 0U,
                       CONFIG_NC_AUDIO_SEND_TIMEOUT_MS);
}

static int notify(nc_audio_event_type_t type, nc_audio_event_param_t *event)
{
    return g_comm_ctx->config.notify != NULL ?
           g_comm_ctx->config.notify(type, event) : QM_EOK;
}

static int dispatch(uint8_t command, uint8_t *data, uint32_t size)
{
    nc_audio_event_param_t event = {0};
    int ret;

    switch (command) {
        case NC_AUDIO_SUB_CMD_GET_MCU_VER:
            if (size < 3U || size > 3U + CONFIG_NC_AUDIO_RECV_VER_MAX_LEN) {
                return -QM_EINVAL;
            }
            event.mcu_ver.type = data[0];
            event.mcu_ver.status = data[1];
            event.mcu_ver.level = data[2];
            if (size > 3U) {
                uint32_t count = size - 3U;
                if (count >= sizeof(event.mcu_ver.str_ver)) {
                    count = sizeof(event.mcu_ver.str_ver) - 1U;
                }
                qm_memcpy(event.mcu_ver.str_ver, data + 3U, count);
            }
            ret = notify(NC_AUDIO_EVENT_TYPE_GET_MCU_VER, &event);
            if (ret != QM_EOK) {
                return ret;
            }
            if (data[0] == 0U) {
                return send_frame(command, NULL, 0U, NULL, 0U,
                                   CONFIG_NC_AUDIO_SEND_TIMEOUT_MS);
            }
            ret = configure_mic(g_comm_ctx->config.mic_gain);
            if (ret == QM_EOK) {
                ret = configure_spk(g_comm_ctx->config.spk_gain);
            }
            if (ret == QM_EOK) {
                ret = nc_audio_cfg(NC_AUDIO_CFG_TYPE_SLIENT_TIMEOUT,
                                    g_comm_ctx->config.vad_timeout);
            }
            return ret;
        case NC_AUDIO_SUB_CMD_MIC_CFG:
        case NC_AUDIO_SUB_CMD_SPK_CFG:
            /* 只保证 code 字节存在；模块对 SPK_CFG 的应答带额外字节（实测 5 字节）。 */
            if (size < 1U) {
                return -QM_EINVAL;
            }
            if (size != 1U) {
                QM_HEX_LOGW(LOG_TAG, "cfg ack", data, size);
            }
            event.mic_cfg_result.code = data[0];
            return notify(command == NC_AUDIO_SUB_CMD_MIC_CFG ?
                          NC_AUDIO_EVENT_TYPE_MIC_CFG : NC_AUDIO_EVENT_TYPE_SPK_CFG,
                          &event);
        case NC_AUDIO_SUB_CMD_MIC_DATA:
            if (size < 1U || data[0] > NC_AUDIO_VAD_STOP) {
                return -QM_EINVAL;
            }
            event.mic_data.status = (nc_audio_vad_status_t)data[0];
            event.mic_data.payload_len = (uint16_t)(size - 1U);
            event.mic_data.payload = size > 1U ? data + 1U : NULL;
            return notify(NC_AUDIO_EVENT_TYPE_MIC_DATA_GET, &event);
        case NC_AUDIO_SUB_CMD_SPK_PLAY:
            if (size != 5U) {
                return -QM_EINVAL;
            }
            event.spk_play_ack.code = data[0];
            event.spk_play_ack.msg_id = read_be32(data + 1U);
            return notify(NC_AUDIO_EVENT_TYPE_SPK_PLAY_ACK, &event);
        case NC_AUDIO_SUB_CMD_SKP_REPLY:
            if (size != 4U || read_be32(data) < NC_AUDIO_SPK_REPLY_TYPE_START ||
                read_be32(data) > NC_AUDIO_SPK_REPLY_TYPE_STOP) {
                return -QM_EINVAL;
            }
            event.spk_state.status = (nc_audio_spk_reply_type_t)read_be32(data);
            return notify(NC_AUDIO_EVENT_TYPE_SPK_REPLY, &event);
        case NC_AUDIO_SUB_CMD_AUDIO_WAKEUP:
            return notify(NC_AUDIO_EVENT_TYPE_WAKEUP, NULL);
        case NC_AUDIO_SUB_CMD_AUDIO_SLEEP:
            return notify(NC_AUDIO_EVENT_TYPE_SLEEP, NULL);
        default:
            return QM_EOK;
    }
}

int32_t nc_audio_recv_push(uint8_t *data, int len)
{
    nc_audio_ctx_t *ctx = g_comm_ctx;
    int result = QM_EOK;

    if (ctx == NULL || data == NULL || len <= 0) {
        return -QM_EINVAL;
    }
    if (ctx->used != 0U && qm_utils_time_is_expired(&ctx->deadline)) {
        ctx->used = 0U;
    }
    while (len-- > 0) {
        uint8_t value = *data++;
        if (ctx->used == 0U) {
            if (value != NC_AUDIO_FRAME_HEADER_1) {
                continue;
            }
            qm_utils_time_countdown_ms(&ctx->deadline, CONFIG_NC_AUDIO_RECV_TIMEOUT_MS);
        } else if ((ctx->used == 1U && value != NC_AUDIO_FRAME_HEADER_2) ||
                   (ctx->used == 2U && value != NC_AUDIO_FRAME_VER) ||
                   (ctx->used == 3U && value != NC_AUDIO_FRAME_CMD)) {
            ctx->used = value == NC_AUDIO_FRAME_HEADER_1 ? 1U : 0U;
            ctx->rx[0] = value;
            continue;
        }
        ctx->rx[ctx->used++] = value;
        if (ctx->used == NC_WIRE_PREFIX_BYTES) {
            uint32_t size = ((uint32_t)ctx->rx[4] << 8) | ctx->rx[5];
            if (size == 0U || size > CONFIG_NC_AUDIO_RECV_MAX_SIZE) {
                ctx->used = 0U;
                result = -QM_EINVAL;
                continue;
            }
            ctx->expected = size + NC_WIRE_OVERHEAD_BYTES;
        }
        if (ctx->used > NC_WIRE_PREFIX_BYTES && ctx->used == ctx->expected) {
            int ret = -QM_EIO;
            uint8_t command = ctx->rx[6];
            uint32_t payload_size = ctx->used - NC_AUDIO_FRAME_MIN_LEN;

            if (checksum(ctx->rx, ctx->used - 1U) == value) {
                /* MIC 数据是高速通路，其余都是控制帧，只在控制帧打日志避免刷屏。 */
                if (command != NC_AUDIO_SUB_CMD_MIC_DATA) {
                    QM_LOGD(LOG_TAG, "frame cmd=0x%02X size=%u", command, (unsigned)payload_size);
                }
                ret = dispatch(command, ctx->rx + NC_AUDIO_FRAME_HEAD_LEN, payload_size);
                if (ret != QM_EOK) {
                    QM_LOGW(LOG_TAG, "frame cmd=0x%02X size=%u dispatch failed ret=%d",
                            command, (unsigned)payload_size, ret);
                }
            } else {
                QM_LOGW(LOG_TAG, "frame checksum bad cmd=0x%02X size=%u",
                        command, (unsigned)payload_size);
            }
            ctx->used = 0U;
            if (ret != QM_EOK) {
                result = ret;
            }
        }
    }
    return result;
}

int32_t nc_audio_init(nc_audio_param_t *param)
{
    nc_audio_ctx_t *ctx;
    int ret;
    if (param == NULL || param->send == NULL ||
        param->mic.sample_rate_hz == 0U || param->spk.sample_rate_hz == 0U ||
        (unsigned)param->mic.codec > NC_AUDIO_FMT_TYPE_MP3 ||
        (unsigned)param->spk.codec > NC_AUDIO_FMT_TYPE_MP3 ||
        param->mic.sample_bits == 0U || param->spk.sample_bits == 0U ||
        param->mic.channels == 0U || param->spk.channels == 0U ||
        param->mic.max_payload_bytes == 0U || param->spk.max_payload_bytes == 0U ||
        param->mic.max_payload_bytes > CONFIG_NC_AUDIO_RECV_MAX_SIZE ||
        param->spk.max_payload_bytes > UINT16_MAX - NC_SPK_HEADER_BYTES - 1U) {
        return -QM_EINVAL;
    }
    if (g_comm_ctx != NULL) {
        return -QM_EBUSY;
    }
    ctx = (nc_audio_ctx_t *)qm_calloc(1U, sizeof(*ctx));
    if (ctx == NULL) {
        return -QM_ENOMEM;
    }
    ctx->config = *param;
    ret = qm_mutex_new(&ctx->tx_lock);
    if (ret != QM_EOK) {
        qm_free(ctx);
        return ret;
    }
    g_comm_ctx = ctx;
    ret = send_frame(NC_AUDIO_SUB_CMD_GET_MCU_VER, NULL, 0U, NULL, 0U,
                      CONFIG_NC_AUDIO_SEND_TIMEOUT_MS);
    if (ret != QM_EOK) {
        (void)nc_audio_deinit();
    }
    return ret;
}

int32_t nc_audio_deinit(void)
{
    if (g_comm_ctx != NULL) {
        (void)qm_mutex_free(&g_comm_ctx->tx_lock);
        qm_free(g_comm_ctx);
        g_comm_ctx = NULL;
    }
    return QM_EOK;
}

int32_t nc_audio_event_register(nc_audio_event_t callback)
{
    if (g_comm_ctx == NULL) {
        return -QM_EINVAL;
    }
    g_comm_ctx->config.notify = callback;
    return QM_EOK;
}

int32_t nc_audio_cfg(nc_audio_cfg_type_t type, int value)
{
    uint8_t data[5];
    if ((unsigned int)type > NC_AUDIO_CFG_TYPE_NOISE_REDUCTION || value < 0) {
        return -QM_EINVAL;
    }
    data[0] = (uint8_t)type;
    if (value <= UINT8_MAX) {
        data[1] = (uint8_t)value;
        return send_frame(NC_AUDIO_SUB_CMD_AUDIO_CFG, data, 2U, NULL, 0U,
                           CONFIG_NC_AUDIO_SEND_TIMEOUT_MS);
    }
    write_be32(data + 1U, (uint32_t)value);
    return send_frame(NC_AUDIO_SUB_CMD_AUDIO_CFG, data, sizeof(data), NULL, 0U,
                       CONFIG_NC_AUDIO_SEND_TIMEOUT_MS);
}

int32_t nc_audio_record_func(nc_audio_record_func_t func)
{
    return send_frame(NC_AUDIO_SUB_CMD_RECORD_FUNC, &func, sizeof(func), NULL, 0U,
                       CONFIG_NC_AUDIO_SEND_TIMEOUT_MS);
}

int32_t nc_audio_set_spk_volume(uint8_t volume)
{
    if (g_comm_ctx == NULL || volume > 100U) {
        return -QM_EINVAL;
    }
    return configure_spk(volume);
}

int mcu_set_mic_gain(uint8_t gain)
{
    if (g_comm_ctx == NULL || gain > 100U) {
        return -QM_EINVAL;
    }
    return configure_mic(gain);
}

int32_t nc_audio_spk_play(nc_audio_spk_play_header_t *play,
                         uint8_t *payload, int payload_len, uint32_t timeout_ms)
{
    uint8_t header[NC_SPK_HEADER_BYTES];
    if (g_comm_ctx == NULL || play == NULL || payload_len < 0 ||
        play->type > NC_AUDIO_SPK_PLAY_TYPE_STOP ||
        (play->type == NC_AUDIO_SPK_PLAY_TYPE_DATA &&
         (payload_len == 0 || (uint32_t)payload_len > g_comm_ctx->config.spk.max_payload_bytes)) ||
        (play->type != NC_AUDIO_SPK_PLAY_TYPE_DATA && payload_len != 0)) {
        return -QM_EINVAL;
    }
    header[0] = play->type;
    write_be32(header + 1U, play->msg_id);
    return send_frame(NC_AUDIO_SUB_CMD_SPK_PLAY, header, sizeof(header),
                      payload, (uint32_t)payload_len, timeout_ms);
}
