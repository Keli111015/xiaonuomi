#include "qm_uart.h"
#include "qm_errno.h"
#include "qm_log.h"
#include "qm.h"
#include "qm_kernel.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <termios.h>

#define LOG_TAG "linux uart"

typedef struct {
    int fd;
} linux_uart_ctx_t;

static speed_t linux_uart_baud_to_speed(uint32_t baud)
{
    switch (baud) {
    case QM_UART_BAUDRATE_300:     return B300;
    case QM_UART_BAUDRATE_600:     return B600;
    case QM_UART_BAUDRATE_1200:    return B1200;
    case QM_UART_BAUDRATE_2400:    return B2400;
    case QM_UART_BAUDRATE_4800:    return B4800;
    case QM_UART_BAUDRATE_9600:    return B9600;
    case QM_UART_BAUDRATE_19200:   return B19200;
    case QM_UART_BAUDRATE_38400:   return B38400;
    case QM_UART_BAUDRATE_57600:   return B57600;
    case QM_UART_BAUDRATE_115200:  return B115200;
    case QM_UART_BAUDRATE_230400:  return B230400;
    case QM_UART_BAUDRATE_460800:  return B460800;
    case QM_UART_BAUDRATE_921600:  return B921600;
    default:                       return B115200;
    }
}

static int linux_uart_dev_path(uint8_t port, char *path, size_t path_len)
{
    snprintf(path, path_len, "/dev/ttyS%u", port);
    return 0;
}

static int linux_uart_wait_readable(int fd, uint32_t timeout_ms)
{
    fd_set read_fds;
    struct timeval tv;
    int ret;

    if (timeout_ms == QM_WAIT_FOREVER) {
        return 1;
    }

    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);
    if (timeout_ms == 0) {
        ret = select(fd + 1, &read_fds, NULL, NULL, NULL);
    } else {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ret = select(fd + 1, &read_fds, NULL, NULL, &tv);
    }

    if (ret == 0) {
        return 0;
    }
    if (ret < 0) {
        return -1;
    }
    return 1;
}

int32_t qm_uart_init(qm_uart_dev_t *uart)
{
    linux_uart_ctx_t *ctx;
    char dev_path[32];
    struct termios tio;
    speed_t speed;

    if (uart == NULL) {
        return -QM_EINVAL;
    }

    ctx = (linux_uart_ctx_t *)qm_malloc(sizeof(linux_uart_ctx_t));
    if (ctx == NULL) {
        return -QM_ENOMEM;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->fd = -1;

    linux_uart_dev_path(uart->port, dev_path, sizeof(dev_path));
    ctx->fd = open(dev_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (ctx->fd < 0) {
        QM_LOGW(LOG_TAG, "open %s failed errno=%d", dev_path, errno);
        qm_free(ctx);
        return -QM_EIO;
    }

    if (tcgetattr(ctx->fd, &tio) != 0) {
        close(ctx->fd);
        qm_free(ctx);
        return -QM_EIO;
    }

    cfmakeraw(&tio);
    speed = linux_uart_baud_to_speed(uart->config.baud_rate);
    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);

    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~CSIZE;
    switch (uart->config.data_bits) {
    case QM_UART_DATA_5_BITS: tio.c_cflag |= CS5; break;
    case QM_UART_DATA_6_BITS: tio.c_cflag |= CS6; break;
    case QM_UART_DATA_7_BITS: tio.c_cflag |= CS7; break;
    case QM_UART_DATA_8_BITS:
    default:                  tio.c_cflag |= CS8; break;
    }

    if (uart->config.stop_bits == QM_UART_STOP_BITS_2) {
        tio.c_cflag |= CSTOPB;
    } else {
        tio.c_cflag &= ~CSTOPB;
    }

    tio.c_cflag &= ~(PARENB | PARODD);
    if (uart->config.parity == QM_UART_PARITY_EVEN) {
        tio.c_cflag |= PARENB;
    } else if (uart->config.parity == QM_UART_PARITY_ODD) {
        tio.c_cflag |= PARENB | PARODD;
    }

    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(ctx->fd, TCSANOW, &tio) != 0) {
        close(ctx->fd);
        qm_free(ctx);
        return -QM_EIO;
    }

    fcntl(ctx->fd, F_SETFL, 0);
    uart->priv = ctx;
    QM_LOGI(LOG_TAG, "open %s baud=%u", dev_path, uart->config.baud_rate);
    return QM_EOK;
}

int32_t qm_uart_ctrl(qm_uart_dev_t *uart, qm_uart_ctrl_cmd_t cmd, void *param)
{
    linux_uart_ctx_t *ctx;

    (void)param;

    if (uart == NULL || uart->priv == NULL) {
        return -QM_EINVAL;
    }

    ctx = (linux_uart_ctx_t *)uart->priv;
    if (cmd == QM_UART_CTRL_CMD_RX_CLEAR) {
        tcflush(ctx->fd, TCIFLUSH);
    }

    return QM_EOK;
}

int32_t qm_uart_write(qm_uart_dev_t *uart, const void *data, uint32_t size, uint32_t timeout)
{
    linux_uart_ctx_t *ctx;
    const uint8_t *buf = (const uint8_t *)data;
    uint32_t sent = 0;
    int ready;
    fd_set write_fds;
    struct timeval tv;

    (void)timeout;

    if (uart == NULL || uart->priv == NULL || data == NULL || size == 0) {
        return -QM_EINVAL;
    }

    ctx = (linux_uart_ctx_t *)uart->priv;
    while (sent < size) {
        if (timeout != QM_WAIT_FOREVER && timeout != 0) {
            FD_ZERO(&write_fds);
            FD_SET(ctx->fd, &write_fds);
            tv.tv_sec = timeout / 1000;
            tv.tv_usec = (timeout % 1000) * 1000;
            ready = select(ctx->fd + 1, NULL, &write_fds, NULL, &tv);
            if (ready <= 0) {
                return (sent > 0) ? (int32_t)sent : -QM_ETIMEOUT;
            }
        }

        ready = (int)write(ctx->fd, buf + sent, size - sent);
        if (ready < 0) {
            return (sent > 0) ? (int32_t)sent : -QM_EIO;
        }
        sent += (uint32_t)ready;
    }

    return (int32_t)sent;
}

int32_t qm_uart_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout)
{
    linux_uart_ctx_t *ctx;
    uint8_t *buf = (uint8_t *)data;
    uint32_t received = 0;
    int ready;
    int count;

    if (uart == NULL || uart->priv == NULL || data == NULL || expect_size == 0) {
        return -QM_EINVAL;
    }

    ctx = (linux_uart_ctx_t *)uart->priv;
    while (received < expect_size) {
        ready = linux_uart_wait_readable(ctx->fd, timeout);
        if (ready == 0) {
            return (received > 0) ? (int32_t)received : -QM_ETIMEOUT;
        }
        if (ready < 0) {
            return (received > 0) ? (int32_t)received : -QM_EIO;
        }

        count = (int)read(ctx->fd, buf + received, expect_size - received);
        if (count < 0) {
            return (received > 0) ? (int32_t)received : -QM_EIO;
        }
        if (count == 0) {
            return (received > 0) ? (int32_t)received : -QM_ETIMEOUT;
        }
        received += (uint32_t)count;
    }

    return (int32_t)received;
}

int32_t qm_uart_deinit(qm_uart_dev_t *uart)
{
    linux_uart_ctx_t *ctx;

    if (uart == NULL || uart->priv == NULL) {
        return -QM_EINVAL;
    }

    ctx = (linux_uart_ctx_t *)uart->priv;
    if (ctx->fd >= 0) {
        close(ctx->fd);
    }
    qm_free(ctx);
    uart->priv = NULL;
    return QM_EOK;
}
