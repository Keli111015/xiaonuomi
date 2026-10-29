#ifndef BOOL_DEFINE_CONFLICT
#define BOOL_DEFINE_CONFLICT
#endif

#include "qm_uart.h"

#include "qm_errno.h"

#include "device/device.h"
#include "device/uart.h"

#include <string.h>

#define QM_AC7911BX_UART_PORT_COUNT       3
#define QM_AC7911BX_UART_RX_BUFFER_SIZE   1024
#define QM_AC7911BX_UART_CALLBACK_IDLE_MS  10

typedef struct {
    void *device;
    u8 *rx_buffer;
    qm_uart_dev_t *uart;
    qm_uart_read_callback_t read_callback;
    qm_uart_write_callback_t write_callback;
} qm_ac7911bx_uart_ctx_t;

static const char *const s_uart_device_names[QM_AC7911BX_UART_PORT_COUNT] = {
    "uart0",
    "uart1",
    "uart2",
};

static qm_ac7911bx_uart_ctx_t *s_uart_contexts[QM_AC7911BX_UART_PORT_COUNT];

static int uart_rx_timeout(unsigned int port)
{
    qm_ac7911bx_uart_ctx_t *ctx;
    qm_uart_read_callback_t callback;
    u32 received = 0;

    if (port >= QM_AC7911BX_UART_PORT_COUNT) {
        return -1;
    }

    ctx = s_uart_contexts[port];
    if (ctx == NULL || ctx->read_callback == NULL) {
        return 0;
    }
    if (dev_ioctl(ctx->device, UART_GET_RECV_CNT, (u32)&received) < 0 ||
        received == 0) {
        return 0;
    }

    /* The SDK invokes this after the configured RX idle period. */
    callback = ctx->read_callback;
    callback(ctx->uart, received);
    return 0;
}

static int uart0_rx_timeout(void)
{
    return uart_rx_timeout(0);
}

static int uart1_rx_timeout(void)
{
    return uart_rx_timeout(1);
}

static int uart2_rx_timeout(void)
{
    return uart_rx_timeout(2);
}

static int (*const s_uart_rx_timeout_callbacks[QM_AC7911BX_UART_PORT_COUNT])(void) = {
    uart0_rx_timeout,
    uart1_rx_timeout,
    uart2_rx_timeout,
};

static int qm_ac7911bx_uart_validate(const qm_uart_dev_t *uart)
{
    if (uart == NULL || uart->port >= QM_AC7911BX_UART_PORT_COUNT) {
        return -QM_EINVAL;
    }
    if (uart->config.mode != QM_UART_MODE_UART ||
        uart->config.data_bits != QM_UART_DATA_8_BITS ||
        uart->config.parity != QM_UART_PARITY_NONE ||
        uart->config.stop_bits != QM_UART_STOP_BITS_1 ||
        uart->config.flow_ctrl != QM_UART_FLOW_CTRL_NONE) {
        return -QM_ENOSYS;
    }
    return QM_EOK;
}

int32_t qm_uart_init(qm_uart_dev_t *uart)
{
    qm_ac7911bx_uart_ctx_t *ctx;
    int ret;

    ret = qm_ac7911bx_uart_validate(uart);
    if (ret != QM_EOK) {
        return ret;
    }
    if (uart->priv != NULL) {
        return QM_EOK;
    }
    if (s_uart_contexts[uart->port] != NULL) {
        return -QM_EBUSY;
    }

    ctx = qm_malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return -QM_ENOMEM;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->uart = uart;

    ctx->rx_buffer = qm_aligned_alloc(32, QM_AC7911BX_UART_RX_BUFFER_SIZE);
    if (ctx->rx_buffer == NULL) {
        qm_free(ctx);
        return -QM_ENOMEM;
    }

    ctx->device = dev_open(s_uart_device_names[uart->port], NULL);
    if (ctx->device == NULL) {
        qm_free(ctx->rx_buffer);
        qm_free(ctx);
        return -QM_ENOENT;
    }

    /* Pin mux and 8N1 framing are registered by the selected board. */
    ret = dev_ioctl(ctx->device, UART_SET_BAUDRATE, uart->config.baud_rate);
    if (ret < 0) {
        goto fail;
    }
    ret = dev_ioctl(ctx->device, UART_SET_CIRCULAR_BUFF_ADDR, (u32)ctx->rx_buffer);
    if (ret < 0) {
        goto fail;
    }
    ret = dev_ioctl(ctx->device, UART_SET_CIRCULAR_BUFF_LENTH,
                    QM_AC7911BX_UART_RX_BUFFER_SIZE);
    if (ret < 0) {
        goto fail;
    }
    ret = dev_ioctl(ctx->device, UART_SET_RECV_BLOCK, 0);
    if (ret < 0) {
        goto fail;
    }
    ret = dev_ioctl(ctx->device, UART_START, 0);
    if (ret < 0) {
        goto fail;
    }

    s_uart_contexts[uart->port] = ctx;
    uart->priv = ctx;
    return QM_EOK;

fail:
    dev_close(ctx->device);
    qm_free(ctx->rx_buffer);
    qm_free(ctx);
    return ret;
}

int32_t qm_uart_write(qm_uart_dev_t *uart, const void *data,
                      uint32_t size, uint32_t timeout)
{
    qm_ac7911bx_uart_ctx_t *ctx;
    qm_uart_write_callback_t callback;
    int ret;

    (void)timeout;
    if (uart == NULL || uart->priv == NULL || data == NULL || size == 0) {
        return -QM_EINVAL;
    }

    ctx = (qm_ac7911bx_uart_ctx_t *)uart->priv;
    ret = dev_write(ctx->device, (void *)data, size);
    callback = ctx->write_callback;
    if (ret > 0 && callback != NULL) {
        /* The SDK write API has no separate TX-complete interrupt. */
        callback(uart, (uint32_t)ret);
    }
    return ret;
}

int32_t qm_uart_read(qm_uart_dev_t *uart, void *data,
                     uint32_t expect_size, uint32_t timeout)
{
    qm_ac7911bx_uart_ctx_t *ctx;
    int ret;

    if (uart == NULL || uart->priv == NULL || data == NULL || expect_size == 0) {
        return -QM_EINVAL;
    }

    ctx = (qm_ac7911bx_uart_ctx_t *)uart->priv;
    ret = dev_ioctl(ctx->device, UART_SET_RECV_BLOCK, timeout != 0 ? 1 : 0);
    if (ret < 0) {
        return ret;
    }
    if (timeout == QM_WAIT_FOREVER) {
        ret = dev_ioctl(ctx->device, UART_SET_RECV_TIMEOUT, 0);
        if (ret < 0) {
            return ret;
        }
    } else if (timeout != QM_NO_WAIT) {
        ret = dev_ioctl(ctx->device, UART_SET_RECV_TIMEOUT, timeout);
        if (ret < 0) {
            return ret;
        }
    }

    ret = dev_read(ctx->device, data, expect_size);
    if (ret == UART_RECV_TIMEOUT) {
        return -QM_ETIMEOUT;
    }
    if (ret == UART_CIRCULAR_BUFFER_WRITE_OVERLAY) {
        return -QM_EFULL;
    }
    if (ret == UART_RECV_EXIT) {
        return -QM_EINTR;
    }
    return ret >= 0 ? ret : -QM_EIO;
}

int32_t qm_uart_ctrl(qm_uart_dev_t *uart, qm_uart_ctrl_cmd_t cmd, void *param)
{
    qm_ac7911bx_uart_ctx_t *ctx;
    int ret;

    if (uart == NULL || uart->priv == NULL) {
        return -QM_EINVAL;
    }

    ctx = (qm_ac7911bx_uart_ctx_t *)uart->priv;
    switch (cmd) {
    case QM_UART_CTRL_CMD_RX_CLEAR:
        ret = dev_ioctl(ctx->device, UART_FLUSH, 0);
        return ret < 0 ? ret : QM_EOK;
    case QM_UART_CTRL_CMD_READ_CALLBACK:
        if (param == NULL) {
            ctx->read_callback = NULL;
            ret = dev_ioctl(ctx->device, UART_SET_RECV_TIMEOUT_CB, 0);
            return ret < 0 ? ret : QM_EOK;
        }
        ctx->read_callback = (qm_uart_read_callback_t)param;
        ret = dev_ioctl(ctx->device, UART_SET_RECV_TIMEOUT_CB,
                        (u32)s_uart_rx_timeout_callbacks[uart->port]);
        if (ret < 0) {
            ctx->read_callback = NULL;
            return ret;
        }
        ret = dev_ioctl(ctx->device, UART_SET_RECV_TIMEOUT,
                        QM_AC7911BX_UART_CALLBACK_IDLE_MS);
        return ret < 0 ? ret : QM_EOK;
    case QM_UART_CTRL_CMD_WRITE_CALLBACK:
        ctx->write_callback = (qm_uart_write_callback_t)param;
        return QM_EOK;
    case QM_UART_CTRL_CMD_NOEN:
        return QM_EOK;
    default:
        return -QM_EINVAL;
    }
}

int32_t qm_uart_deinit(qm_uart_dev_t *uart)
{
    qm_ac7911bx_uart_ctx_t *ctx;
    int ret;

    if (uart == NULL || uart->priv == NULL) {
        return -QM_EINVAL;
    }

    ctx = (qm_ac7911bx_uart_ctx_t *)uart->priv;
    s_uart_contexts[uart->port] = NULL;
    ret = dev_close(ctx->device);
    qm_free(ctx->rx_buffer);
    qm_free(ctx);
    uart->priv = NULL;
    return ret < 0 ? ret : QM_EOK;
}
