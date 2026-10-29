#ifndef BOOL_DEFINE_CONFLICT
#define BOOL_DEFINE_CONFLICT
#endif

#include "qm.h"
#include "qm_errno.h"
#include "qm_usb.h"

#include "app_config.h"

#if CONFIG_QM_USB_SUPPORT

#if TCFG_USB_SLAVE_CDC_ENABLE
#include "asm/cpu.h"
#include "usb/device/cdc.h"

typedef struct {
    uint8_t *rx_buffer;
    uint32_t rx_buffer_size;
    uint32_t read_pos;
    uint32_t write_pos;
    qm_usb_cdcacm_callback_t callback_rx;
    bool rx_overflowed;
    bool initialized;
} qm_ac7911bx_usb_cdc_ctx_t;

static qm_ac7911bx_usb_cdc_ctx_t s_cdc_ctx;

static int qm_ac7911bx_usb_cdc_output(void *priv, u8 *data, u32 length)
{
    qm_ac7911bx_usb_cdc_ctx_t *ctx = (qm_ac7911bx_usb_cdc_ctx_t *)priv;
    qm_usb_cdcacm_callback_t callback;
    qm_usb_cdcacm_event_t event = { .type = QM_USB_CDC_EVENT_RX };
    uint32_t i;

    if (ctx == NULL || data == NULL || length == 0) {
        return 0;
    }

    CPU_CRITICAL_ENTER();
    for (i = 0; i < length; i++) {
        uint32_t next_write_pos;

        if (ctx->rx_overflowed) {
            break;
        }
        ctx->rx_buffer[ctx->write_pos] = data[i];
        next_write_pos = (ctx->write_pos + 1) % ctx->rx_buffer_size;
        if (next_write_pos == ctx->read_pos) {
            ctx->read_pos = 0;
            ctx->write_pos = 0;
            ctx->rx_overflowed = true;
            break;
        }
        ctx->write_pos = next_write_pos;
    }
    callback = ctx->callback_rx;
    CPU_CRITICAL_EXIT();

    if (callback != NULL) {
        callback(QM_USB_CDCACM_0, &event);
    }
    return (int)length;
}

int qm_usb_cdcacm_init(qm_usb_cdcacm_config_t *cfg)
{
    if (cfg == NULL || cfg->usb_port != QM_USB_0 ||
        cfg->cdc_port != QM_USB_CDCACM_0 || cfg->rx_buf_sz <= 0) {
        return -QM_EINVAL;
    }

    if (s_cdc_ctx.initialized) {
        s_cdc_ctx.callback_rx = cfg->callback_rx;
        return QM_EOK;
    }

    s_cdc_ctx.rx_buffer = qm_malloc((unsigned int)cfg->rx_buf_sz + 1U);
    if (s_cdc_ctx.rx_buffer == NULL) {
        return -QM_ENOMEM;
    }

    s_cdc_ctx.rx_buffer_size = (uint32_t)cfg->rx_buf_sz + 1U;
    s_cdc_ctx.callback_rx = cfg->callback_rx;
    s_cdc_ctx.initialized = true;

    /* The selected board owns USB start-up and the CDC device descriptor. */
    cdc_set_output_handle((usb_dev)cfg->usb_port, &s_cdc_ctx,
                          qm_ac7911bx_usb_cdc_output);
    return QM_EOK;
}

int qm_usb_cdcacm_register_callback(qm_usb_cdcacm_port_t port,
                                    qm_usb_cdcacm_event_type_t event_type,
                                    qm_usb_cdcacm_callback_t callback)
{
    if (port != QM_USB_CDCACM_0 || event_type != QM_USB_CDC_EVENT_RX) {
        return -QM_EINVAL;
    }
    if (!s_cdc_ctx.initialized) {
        return -QM_EINIT;
    }

    CPU_CRITICAL_ENTER();
    s_cdc_ctx.callback_rx = callback;
    CPU_CRITICAL_EXIT();
    return QM_EOK;
}

int qm_usb_cdcacm_write(qm_usb_cdcacm_port_t port, const uint8_t *in_buf,
                        int in_size, uint32_t timeout)
{
    (void)timeout;
    if (port != QM_USB_CDCACM_0 || in_buf == NULL || in_size <= 0) {
        return -QM_EINVAL;
    }
    if (!s_cdc_ctx.initialized) {
        return -QM_EINIT;
    }

    return (int)cdc_write_data((usb_dev)QM_USB_0, (u8 *)in_buf,
                               (u32)in_size);
}

int qm_usb_cdcacm_read(qm_usb_cdcacm_port_t port, uint8_t *out_buf,
                       int out_size)
{
    int read_size = 0;

    if (port != QM_USB_CDCACM_0 || out_buf == NULL || out_size <= 0) {
        return -QM_EINVAL;
    }
    if (!s_cdc_ctx.initialized) {
        return -QM_EINIT;
    }

    CPU_CRITICAL_ENTER();
    if (s_cdc_ctx.rx_overflowed) {
        s_cdc_ctx.rx_overflowed = false;
        CPU_CRITICAL_EXIT();
        return -QM_EFULL;
    }
    while (read_size < out_size && s_cdc_ctx.read_pos != s_cdc_ctx.write_pos) {
        out_buf[read_size++] = s_cdc_ctx.rx_buffer[s_cdc_ctx.read_pos];
        s_cdc_ctx.read_pos = (s_cdc_ctx.read_pos + 1) % s_cdc_ctx.rx_buffer_size;
    }
    CPU_CRITICAL_EXIT();
    return read_size;
}

#else

int qm_usb_cdcacm_init(qm_usb_cdcacm_config_t *cfg)
{
    (void)cfg;
    return -QM_ENOSYS;
}

int qm_usb_cdcacm_register_callback(qm_usb_cdcacm_port_t port,
                                    qm_usb_cdcacm_event_type_t event_type,
                                    qm_usb_cdcacm_callback_t callback)
{
    (void)port;
    (void)event_type;
    (void)callback;
    return -QM_ENOSYS;
}

int qm_usb_cdcacm_write(qm_usb_cdcacm_port_t port, const uint8_t *in_buf,
                        int in_size, uint32_t timeout)
{
    (void)port;
    (void)in_buf;
    (void)in_size;
    (void)timeout;
    return -QM_ENOSYS;
}

int qm_usb_cdcacm_read(qm_usb_cdcacm_port_t port, uint8_t *out_buf,
                       int out_size)
{
    (void)port;
    (void)out_buf;
    (void)out_size;
    return -QM_ENOSYS;
}

#endif
#endif
