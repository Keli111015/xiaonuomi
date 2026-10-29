#include "qm.h"
#include "qm_uart.h"

#include <stdio.h>
#include <FreeRTOS.h>
#include <stream_buffer.h>
#include <task.h>
#include <string.h>
#include <device/vfs_uart.h>
#include <hal/soc/soc.h>
#include <vfs_err.h>
#include <yloop_types.h>
#include <utils_log.h>
#include <blog.h>
#include <hosal_uart.h>
#include <libfdt.h>
#include <vfs_register.h>

#include "qm_utils_timer.h"

#define UART_NUM       2
#define RECV_BUFF_SIZE 512

#define ms2tick(ms) ((ms)/portTICK_PERIOD_MS)

typedef struct 
{
	hosal_uart_dev_t uart;
    void         *rx_ringbuf_handle;
    void         *tx_ringbuf_handle;
    uint32_t     rx_buf_size;
    uint32_t     tx_buf_size;
    void         *mutex;
    void         *fd;
    void         *poll_data;
    void         *taskhdl;
    uint8_t      read_block_flag;
    void         *priv;
    qm_uart_read_callback_t read;
    qm_uart_write_callback_t write;
} qm_uart_hal_dev_t;

static int __uart_rx_irq(void *p_arg)
{
    uint8_t tmp_buf[64];
    int length = 0;
    qm_uart_hal_dev_t *uart = (qm_uart_hal_dev_t *)p_arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    length = hosal_uart_receive(&uart->uart, tmp_buf, sizeof(tmp_buf));
    if (length > 0) {
        xStreamBufferSendFromISR(uart->rx_ringbuf_handle, tmp_buf,
                length, &xHigherPriorityTaskWoken);
        if (uart->read != NULL) {
            uart->read(uart->priv, length);
        }
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
    return 0;
}

static int __uart_tx_irq(void *p_arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    qm_uart_hal_dev_t *uart = (qm_uart_hal_dev_t *)p_arg;
    uint8_t ch;
    size_t ret;

    ret = xStreamBufferReceiveFromISR(uart->tx_ringbuf_handle, &ch, 1,
                                      &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    if (ret == 1) {
        hosal_uart_send(&uart->uart, (const void *)&ch, 1);
    } else {
        hosal_uart_ioctl(&uart->uart, HOSAL_UART_TX_TRIGGER_OFF, NULL);
    }
    return 0;
}

int32_t qm_uart_init(qm_uart_dev_t *uart) 
{
    if (uart == NULL || uart->port > (UART_NUM - 1)) {
        return -QM_ERROR;
    }

    qm_uart_hal_dev_t *uart_dev = (qm_uart_hal_dev_t *)qm_calloc(sizeof(qm_uart_hal_dev_t), 1);
    if (uart_dev == NULL) {
        return -QM_ENOMEM;
    }

    uart_dev->rx_buf_size = RECV_BUFF_SIZE;
    uart_dev->tx_buf_size = RECV_BUFF_SIZE;
    uart_dev->rx_ringbuf_handle = xStreamBufferCreate(uart_dev->rx_buf_size, 1);
    uart_dev->tx_ringbuf_handle = xStreamBufferCreate(uart_dev->tx_buf_size, 1);
    if (uart_dev->rx_ringbuf_handle == NULL || uart_dev->tx_ringbuf_handle == NULL) {
        qm_free(uart_dev);
        return -QM_ENOMEM;
    }
    HOSAL_UART_DEV_DECL(uart_stdio, uart->port, uart->config.pins.tx_pin, uart->config.pins.rx_pin, uart->config.baud_rate);
    memcpy(&uart_dev->uart, &uart_stdio, sizeof(hosal_uart_dev_t));

    printf("--------------[qm_uart_init] %d %d %d %ld----------------\n", uart->port, uart->config.pins.tx_pin, uart->config.pins.rx_pin, uart->config.baud_rate);
            /*  init uart device. */
    uart_dev->uart.port = uart->port;
    hosal_uart_callback_set(&uart_dev->uart, HOSAL_UART_TX_CALLBACK,
                            __uart_tx_irq, uart_dev);
    hosal_uart_callback_set(&uart_dev->uart, HOSAL_UART_RX_CALLBACK,
                            __uart_rx_irq, uart_dev);

    uart_dev->uart.config.mode = HOSAL_UART_MODE_INT;
    hosal_uart_init(&uart_dev->uart);

    uart_dev->priv = uart;
    uart->priv = (void*)uart_dev;

    return QM_EOK;
}

int32_t qm_uart_write(qm_uart_dev_t *uart, const void *data, uint32_t size, uint32_t timeout) 
{
    int ret = -1;                /* return value */
    size_t total_bytes;          /* need to write */
    size_t written_bytes;         /* written bytes */
    qm_uart_hal_dev_t *uart_dev = NULL; /* device pointer */

    if (uart == NULL || uart->port > (UART_NUM - 1)) {
        return -QM_ERROR;
    }

    /* check empty pointer. */
        /* get the device pointer. */
        uart_dev = (qm_uart_hal_dev_t *)(uart->priv);

        if (uart_dev != NULL) {

            total_bytes = size;
            written_bytes = 0;
            while(total_bytes > 0)
            {
                ret = xStreamBufferSend(uart_dev->tx_ringbuf_handle, data+written_bytes, total_bytes, 0);
                /*Trigger UART Write Now*/
                if (ret > 0) {
                    hosal_uart_ioctl(&uart_dev->uart, HOSAL_UART_TX_TRIGGER_ON, NULL);
                    written_bytes+=ret;
                    total_bytes-=ret;
                }
            }
            ret = written_bytes;
        } else {
            ret = -QM_EINVAL;
        }

    return ret;
}


int32_t qm_uart_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout) 
{
    int ret = -1;       
    int wait = 0;         /* return value */
    uint32_t t_left = 0;
    qm_utils_time_t time = {0};
    qm_uart_hal_dev_t *uart_dev = NULL; /* device pointer */
    if (uart == NULL || uart->port > (UART_NUM - 1)) {
        return -QM_ERROR;
    }
    uart_dev = (qm_uart_hal_dev_t *)uart->priv;

    ret = 0;

    if(timeout == QM_WAIT_FOREVER){
        wait = 1;
        t_left = QM_WAIT_FOREVER / 2;
    }else{
        if(timeout){
            qm_utils_time_init(&time);
            qm_utils_time_countdown_ms(&time, timeout);  
        }
    }

    while (1) {

        if(!wait && timeout){
            t_left = qm_utils_time_left(&time);
            if(t_left <= 0){
                break;
            }
        }

        if(!timeout && uart_dev->read){
            ret += xStreamBufferReceiveFromISR(uart_dev->rx_ringbuf_handle,
                data + ret, expect_size - ret, NULL);
        }else{
            ret += xStreamBufferReceive(uart_dev->rx_ringbuf_handle,
                data + ret, expect_size - ret, timeout == QM_WAIT_FOREVER ? QM_WAIT_FOREVER : ms2tick(t_left));
        }

        if ((ret == expect_size)) {
            break;
        }
    }

    return (ret > 0 ? ret : 0); 
}


int32_t qm_uart_deinit(qm_uart_dev_t *uart) 
{
    qm_uart_hal_dev_t *uart_dev = NULL;
    if (uart == NULL || uart->port > (UART_NUM - 1)) {
        return -QM_ERROR;
    }

    uart_dev = (qm_uart_hal_dev_t *)uart->priv;

    hosal_uart_finalize(uart->port);

    vStreamBufferDelete(uart_dev->rx_ringbuf_handle);
    vStreamBufferDelete(uart_dev->tx_ringbuf_handle);
    qm_free(uart_dev);

    uart->priv = NULL;

    return QM_EOK;
}

int32_t qm_uart_ctrl(qm_uart_dev_t *uart, qm_uart_ctrl_cmd_t cmd, void *param)
{
    if(uart == NULL || uart->priv == NULL || param == NULL){
        return -QM_EINVAL;
    }

    qm_uart_hal_dev_t *uart_dev = (qm_uart_hal_dev_t *)uart->priv;

    switch (cmd)
    {
        case QM_UART_CTRL_CMD_RX_CLEAR:
            xStreamBufferReset(uart_dev->rx_ringbuf_handle);
        break;
        case QM_UART_CTRL_CMD_READ_CALLBACK:
            uart_dev->read = (qm_uart_read_callback_t)param;
        break;
        case QM_UART_CTRL_CMD_WRITE_CALLBACK:
            uart_dev->write = (qm_uart_write_callback_t)param;
        break;       
        default:
            break;
    }

    return QM_EOK;
}