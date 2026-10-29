#include "qm.h"
#include "qm_uart.h"
#include "qm_log.h"
#include "vb701n.h"

#include <string.h>

#define LOG_TAG "vb701n"

/*
 * 串口帧打印。改这个数字再编译。
 * 0 关
 * 1 只打方向和命令：TX/RX、名字、cmd、数据长度。20 81 每一帧都打。
 * 2 在 1 的基础上把整帧十六进制打出来（包头到校验）。
 */
#ifndef VB701N_UART_TRACE
#define VB701N_UART_TRACE            (0)
#endif

#define VB701N_TX_BUF_SIZE           (384)
#define VB701N_PARSE_BUF_SIZE        (640)
#define VB701N_MAX_DATA              (520)
#define VB701N_FIXED_HDR             (6)

typedef enum {
    PARSE_HEAD0 = 0,
    PARSE_HEAD1,
    PARSE_FIXED,
    PARSE_BODY,
} parse_state_t;

static qm_uart_dev_t *g_uart = NULL;
static uint8_t g_parse_buf[VB701N_PARSE_BUF_SIZE];
static uint16_t g_parse_len = 0;
static uint16_t g_need_len = 0;
static parse_state_t g_parse_state = PARSE_HEAD0;
static vb701n_mic_cb_t g_mic_cb = NULL;
static void *g_mic_cb_user = NULL;
static vb701n_frame_cb_t g_frame_cb = NULL;
static void *g_frame_cb_user = NULL;
static uint8_t g_mic_enabled = 0;
static uint8_t g_stop_sent = 0;

#if VB701N_UART_TRACE

static const char *cmd_name(uint16_t cmd)
{
    switch (cmd) {
    case VB701N_CMD_HOST_STOP_AUDIO: return "STOP";
    case VB701N_CMD_HOST_VOLUME:     return "VOL";
    case VB701N_CMD_HOST_OTA_CTRL:   return "OTA";
    case VB701N_CMD_HOST_GET_VER:    return "GET_VER";
    case VB701N_CMD_HOST_REC_CTRL:   return "REC";
    case VB701N_CMD_DEV_BOOT_OK:     return "BOOT";
    case VB701N_CMD_DEV_VERSION:     return "VER";
    case VB701N_CMD_DEV_ASR:         return "ASR";
    case VB701N_CMD_DEV_MIC_AUDIO:   return "MIC";
    case VB701N_CMD_HOST_SPK_AUDIO:  return "SPK";
    default:                         return "UNK";
    }
}

static void trace_frame(const char *dir, uint16_t cmd, uint16_t data_len,
                        const uint8_t *frame, uint16_t frame_len)
{
    QM_LOGD(LOG_TAG, "%s %s 0x%04X dlen=%u",
            dir, cmd_name(cmd), cmd, (unsigned)data_len);
#if VB701N_UART_TRACE >= 2
    QM_HEX_LOGD(LOG_TAG, dir, frame, frame_len);
#else
    (void)frame;
    (void)frame_len;
#endif
}

#else

#define trace_frame(dir, cmd, data_len, frame, frame_len) ((void)0)

#endif

static uint8_t vb701n_sum8(const uint8_t *data, uint16_t len)
{
    uint32_t sum = 0;
    uint16_t i;

    for (i = 0; i < len; i++) {
        sum += data[i];
    }
    return (uint8_t)(sum & 0xFF);
}

static void parse_reset(void)
{
    g_parse_state = PARSE_HEAD0;
    g_parse_len = 0;
    g_need_len = 0;
}

static void handle_frame(uint16_t cmd, const uint8_t *data, uint16_t data_len)
{
    if (cmd == VB701N_CMD_DEV_MIC_AUDIO && !g_mic_enabled) {
        if (!g_stop_sent) {
            g_stop_sent = 1;
            vb701n_proto_stop_audio();
        }
        return;
    }

    trace_frame("RX", cmd, data_len, g_parse_buf, g_parse_len);

    if (cmd == VB701N_CMD_DEV_MIC_AUDIO) {
        if (g_mic_cb != NULL) {
            g_mic_cb(data, data_len, g_mic_cb_user);
        }
        return;
    }

    if (g_frame_cb != NULL) {
        g_frame_cb(cmd, data, data_len, g_frame_cb_user);
    }
}

static void parse_byte(uint8_t b)
{
    uint16_t data_len;
    uint16_t cmd;
    uint8_t sum_rx;
    uint8_t sum_calc;

    switch (g_parse_state) {
    case PARSE_HEAD0:
        if (b == VB701N_FRAME_HEAD_0) {
            g_parse_buf[0] = b;
            g_parse_len = 1;
            g_parse_state = PARSE_HEAD1;
        }
        break;

    case PARSE_HEAD1:
        if (b == VB701N_FRAME_HEAD_1) {
            g_parse_buf[g_parse_len++] = b;
            g_need_len = VB701N_FIXED_HDR;
            g_parse_state = PARSE_FIXED;
        } else {
            parse_reset();
            if (b == VB701N_FRAME_HEAD_0) {
                g_parse_buf[0] = b;
                g_parse_len = 1;
                g_parse_state = PARSE_HEAD1;
            }
        }
        break;

    case PARSE_FIXED:
        g_parse_buf[g_parse_len++] = b;
        if (g_parse_len < VB701N_FIXED_HDR) {
            break;
        }
        data_len = ((uint16_t)g_parse_buf[2] << 8) | g_parse_buf[3];
        if (data_len > VB701N_MAX_DATA) {
            QM_LOGD(LOG_TAG, "data_len too big %u, drop", (unsigned)data_len);
            parse_reset();
            break;
        }
        g_need_len = (uint16_t)(VB701N_FIXED_HDR + data_len + 1);
        if (g_need_len > VB701N_PARSE_BUF_SIZE) {
            QM_LOGD(LOG_TAG, "frame too long, drop");
            parse_reset();
            break;
        }
        g_parse_state = PARSE_BODY;
        break;

    case PARSE_BODY:
        g_parse_buf[g_parse_len++] = b;
        if (g_parse_len < g_need_len) {
            break;
        }

        data_len = ((uint16_t)g_parse_buf[2] << 8) | g_parse_buf[3];
        cmd = ((uint16_t)g_parse_buf[4] << 8) | g_parse_buf[5];
        sum_rx = g_parse_buf[VB701N_FIXED_HDR + data_len];
        sum_calc = vb701n_sum8(g_parse_buf, (uint16_t)(VB701N_FIXED_HDR + data_len));
        if (sum_rx != sum_calc) {
            QM_LOGD(LOG_TAG, "SUM fail rx=0x%02X calc=0x%02X", sum_rx, sum_calc);
            parse_reset();
            break;
        }

        handle_frame(cmd,
                     (data_len > 0) ? &g_parse_buf[VB701N_FIXED_HDR] : NULL,
                     data_len);
        parse_reset();
        break;

    default:
        parse_reset();
        break;
    }
}

void vb701n_proto_recv_push(const uint8_t *data, uint32_t len)
{
    uint32_t i;

    if (data == NULL || len == 0) {
        return;
    }
    for (i = 0; i < len; i++) {
        parse_byte(data[i]);
    }
}

qm_err_t vb701n_proto_init(qm_uart_dev_t *uart)
{
    if (uart == NULL) {
        return -QM_EINVAL;
    }

    parse_reset();
    g_uart = uart;
    g_mic_cb = NULL;
    g_mic_cb_user = NULL;
    QM_LOGD(LOG_TAG, "bind uart port %u", (unsigned)uart->port);
    return QM_EOK;
}

qm_err_t vb701n_proto_send(uint16_t cmd, const uint8_t *data, uint16_t data_len)
{
    uint8_t tx[VB701N_TX_BUF_SIZE];
    uint16_t idx = 0;
    int ret;
    static uint16_t spk_ok;

    if (g_uart == NULL) {
        return -QM_EINIT;
    }
    if (data_len > VB701N_MAX_DATA) {
        return -QM_EINVAL;
    }
    if ((VB701N_FIXED_HDR + data_len + 1) > VB701N_TX_BUF_SIZE) {
        return -QM_EINVAL;
    }

    tx[idx++] = VB701N_FRAME_HEAD_0;
    tx[idx++] = VB701N_FRAME_HEAD_1;
    tx[idx++] = (uint8_t)((data_len >> 8) & 0xFF);
    tx[idx++] = (uint8_t)(data_len & 0xFF);
    tx[idx++] = (uint8_t)((cmd >> 8) & 0xFF);
    tx[idx++] = (uint8_t)(cmd & 0xFF);
    if (data_len > 0 && data != NULL) {
        memcpy(&tx[idx], data, data_len);
        idx = (uint16_t)(idx + data_len);
    }
    tx[idx] = vb701n_sum8(tx, idx);
    idx++;

    trace_frame("TX", cmd, data_len, tx, idx);
    ret = qm_uart_write(g_uart, tx, idx, 0);
    if (ret != (int)idx) {
        QM_LOGD(LOG_TAG, "tx fail cmd=0x%04X want=%u write=%d sum=0x%02X",
                cmd, (unsigned)idx, ret, tx[idx - 1]);
        return -QM_EIO;
    }
    if (cmd == VB701N_CMD_HOST_SPK_AUDIO) {
        spk_ok++;
        if (spk_ok == 1 || spk_ok == 267) {
            QM_LOGD(LOG_TAG, "tx ok spk n=%u len=%u write=%d sum=0x%02X",
                    (unsigned)spk_ok, (unsigned)idx, ret, tx[idx - 1]);
        }
    } else {
        QM_LOGD(LOG_TAG, "tx ok cmd=0x%04X len=%u write=%d",
                cmd, (unsigned)idx, ret);
    }
    return QM_EOK;
}

qm_err_t vb701n_proto_query_version(void)
{
    uint8_t data = 0x00;

    QM_LOGD(LOG_TAG, "query version (0x020A)");
    return vb701n_proto_send(VB701N_CMD_HOST_GET_VER, &data, 1);
}

qm_err_t vb701n_proto_stop_audio(void)
{
    g_mic_enabled = 0;
    g_stop_sent = 1;
    QM_LOGD(LOG_TAG, "stop audio (0x0201)");
    return vb701n_proto_send(VB701N_CMD_HOST_STOP_AUDIO, NULL, 0);
}

qm_err_t vb701n_proto_set_volume(uint8_t vol)
{
    if (vol > 31) {
        vol = 31;
    }
    QM_LOGD(LOG_TAG, "set volume %u", vol);
    return vb701n_proto_send(VB701N_CMD_HOST_VOLUME, &vol, 1);
}

qm_err_t vb701n_proto_start_record(void)
{
    uint8_t on = 0x01;

    g_mic_enabled = 1;
    g_stop_sent = 0;
    QM_LOGD(LOG_TAG, "start record (0x0231)");
    return vb701n_proto_send(VB701N_CMD_HOST_REC_CTRL, &on, 1);
}

void vb701n_proto_set_mic_cb(vb701n_mic_cb_t cb, void *user)
{
    g_mic_cb = cb;
    g_mic_cb_user = user;
}

void vb701n_proto_set_frame_cb(vb701n_frame_cb_t cb, void *user)
{
    g_frame_cb = cb;
    g_frame_cb_user = user;
}