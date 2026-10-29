#include "qm.h"

#ifdef CONFIG_QM_UART_SUPPORT
#include "qm_uart.h"
#include "qm_log.h"
#include "qm_ringbuf.h"
#include "qm_utils_timer.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <nuttx/serial/serial.h>
#include "syslog.h"


#define LOG_TAG "qm_uart"

// #define QM_UART_USE_REG          1   // 寄存器方式使用此宏，先使用任务读入ringbuff，再从ringbuff中读取数据 设备树操作无需此操作
// #defin  QM_UART_RINGBUF_SIZE     (128)

typedef struct 
{
    int is_use;
    int fd;
    const char *data;
    int type;
    void *arg;
}qm_uart_info_t;

typedef struct 
{
    qm_sem_t sem;
    qm_uart_dev_t *uart;
    qm_uart_read_callback_t read;
    qm_uart_write_callback_t write;
#if QM_UART_USE_REG
    qm_ringbuf_t ringbuf;
    uint8_t recv_buff[QM_UART_RINGBUF_SIZE];
    uint8_t ringbuff_data[QM_UART_RINGBUF_SIZE];
#endif
}qm_uart_recv_ctx_t;

static qm_uart_info_t  uart_config[] = {
    {0,0,"/dev/ttyS0",O_RDWR | O_NONBLOCK, NULL},
    {0,0,"/dev/ttyS1",O_RDWR | O_NONBLOCK, NULL},
};

int32_t qm_uart_init(qm_uart_dev_t *uart)
{
    qm_uart_info_t *uart_info = NULL;

    if(uart == NULL){
        return -QM_EINVAL;
    }

    if(uart->port > (QM_ARRAY_SIZE(uart_config) - 1)){
        return -QM_EINVAL;
    }

    uart_info = &uart_config[uart->port];
    if(uart_info->is_use){
        goto __next;
    }
    int fd = open(uart_info->data, uart_info->type);
#ifdef LOG_TAG
    QM_LOGE(LOG_TAG, "open uart %s fd = %d", uart_info->data, fd);
#endif
    syslog(LOG_ERR, "open uart %s fd = %d", uart_info->data, fd);

    if (fd < 0) {
        return fd;
    } 
    uart_info->fd = fd;
    uart_info->is_use = QM_TRUE;
    
__next:
    if (uart->priv)
    {
        return QM_EOK;
    }
    uart->priv = (void*)uart_info;
    qm_uart_recv_ctx_t *recv_ctx = NULL;
    recv_ctx = (qm_uart_recv_ctx_t *)qm_malloc(sizeof(qm_uart_recv_ctx_t));
    if(recv_ctx == NULL){
        return -QM_ENOMEM;
    }
    recv_ctx->read = NULL;
    recv_ctx->write = NULL;
    qm_sem_new(&recv_ctx->sem, 0);
    recv_ctx->uart = uart;
    uart_info->arg = (void *)recv_ctx;
    return QM_EOK;
}

int32_t qm_uart_ctrl(qm_uart_dev_t *uart, qm_uart_ctrl_cmd_t cmd, void *param)
{
    int ret;
    if(uart == NULL || uart->priv == NULL || param == NULL){
        return -QM_EINVAL;
    }

    qm_uart_info_t *uart_info = (qm_uart_info_t *)uart->priv;
    qm_uart_recv_ctx_t *recv_ctx = (qm_uart_recv_ctx_t *)uart_info->arg;

    if(recv_ctx == NULL){
        return -QM_EINVAL;
    }

    switch (cmd)
    {
        case QM_UART_CTRL_CMD_RX_CLEAR:
            // fd open 判断
            if (uart_info->fd <= 0) {
                return 0;
            }
            
            struct pollfd fdp = {
                .fd = uart_info->fd,
                .events = POLLIN,
            };

            uint8_t *data = (uint8_t *)qm_malloc(32);
            if(data == NULL){
                return -QM_ENOMEM;
            }
            ret = 32;
            do {
                ret = poll((struct pollfd*)&fdp, 1, 0);
                if (ret >= 0) {
                    if ((fdp.revents & POLLIN) != 0) {
                        ret = read(uart_info->fd, data, 32);
                    }
                }
            } while (ret);
            qm_free(data);
        break;
        case QM_UART_CTRL_CMD_READ_CALLBACK:
            recv_ctx->read = (qm_uart_read_callback_t)param;
            if(qm_sem_is_valid(&recv_ctx->sem)){
                qm_sem_free(&recv_ctx->sem); //节省资源,注册回调时无需使用sem
            }
        break;
        case QM_UART_CTRL_CMD_WRITE_CALLBACK:
            recv_ctx->write = (qm_uart_write_callback_t)param;
        break;       
        default:
            break;
    }

    return QM_EOK;
}

int32_t qm_uart_write(qm_uart_dev_t *uart, const void *data, uint32_t size, uint32_t timeout)
{
    int ret = 0;
    // 参数有效判断
    if(uart == NULL || uart->priv == NULL || data == NULL || size == 0) {
        return 0;
    }
    // 串口有效判断 - QM_ARRAY_SIZE == 0?
    if (uart->port > (QM_ARRAY_SIZE(uart_config) - 1)) {
        return 0;
    }

    qm_uart_info_t *uart_info = (qm_uart_info_t*)uart->priv;
    qm_uart_recv_ctx_t *recv_ctx = (qm_uart_recv_ctx_t *)uart_info->arg;

    // fd open 判断
    if (uart_info->fd <= 0) {
        return 0;
    }

    struct pollfd fdp = {
        .fd = uart_info->fd,
        .events = POLLOUT,
    };

    ret = poll((struct pollfd*)&fdp, 1, timeout);
    if (ret >= 0) {
        if ((fdp.revents & POLLOUT) != 0) {
            ret = write(uart_info->fd, data, size);
            if(recv_ctx->write){
                recv_ctx->write(uart, (0 <= ret) ? ret : 0);
            }
        }
    }
    return (0 <= ret) ? ret : 0;
}

int32_t qm_uart_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout)
{
    int ret  = QM_EOK;
    if(uart == NULL || data == NULL || expect_size == 0) {
        return 0;
    }
    if (uart->priv == NULL || uart->port > (QM_ARRAY_SIZE(uart_config) - 1)) {
        return 0;
    }
    
    qm_uart_info_t *uart_info = (qm_uart_info_t*)uart->priv;
    qm_uart_recv_ctx_t *recv_ctx = (qm_uart_recv_ctx_t *)uart_info->arg;

    // fd open 判断
    if (uart_info->fd <= 0) {
        return 0;
    }
    
    struct pollfd fdp = {
        .fd = uart_info->fd,
        .events = POLLIN,
    };

    ret = poll((struct pollfd*)&fdp, 1, timeout);
    if (ret >= 0) {
        if ((fdp.revents & POLLIN) != 0) {
            ret = read(uart_info->fd, data, expect_size);
            if(recv_ctx->read){
                recv_ctx->read(uart, (0 <= ret) ? ret : 0);
            }
        }
    }

    return (0 <= ret) ? ret : 0;
}

int32_t qm_uart_deinit(qm_uart_dev_t *uart)
{
    if(uart == NULL || uart->priv == NULL){
        return -QM_EINVAL;
    }
    
    qm_uart_info_t *uart_info = (qm_uart_info_t *)uart->priv;
    qm_uart_recv_ctx_t *recv_ctx = (qm_uart_recv_ctx_t *)uart_info->arg;

    if (recv_ctx){
        if (recv_ctx->read || recv_ctx->write)
        {
            // 注册回调使用时无无需sem
            if(qm_sem_is_valid(&recv_ctx->sem)){
                qm_sem_free(&recv_ctx->sem); 
            }
        }
        qm_free(recv_ctx);
    }
    close(uart_info->fd);
    uart_info->is_use = QM_FALSE;
    uart_info->fd = 0;
    uart_info->arg = NULL;
    uart->priv = NULL;
    return QM_EOK;
}
#endif
