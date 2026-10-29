#include "qm_uart.h"
#include "qm_errno.h"

#include "qm_utils_timer.h"
#include "qm.h"

#include <FreeRTOS.h>

#include <task.h>
#include "serial.h"

#include "qm_ringbuf.h"
#include "flash_partition_mgr.h"

#define UART_NUM       2
#define RECV_BUFF_SIZE 200

// // typedef struct vfs_uart_dev {
// // 	hosal_uart_dev_t uart;
// //     void         *rx_ringbuf_handle;
// //     void         *tx_ringbuf_handle;
// //     uint32_t     rx_buf_size;
// //     uint32_t     tx_buf_size;
// //     void         *priv;
// // } vfs_uart_dev_t;

typedef struct vfs_uart_dev {
    Serial_t              uart;         //meigei chuankou 
    qm_uart_baudrate_t    baud_rate;//haimeigei botelv
    //haimeigei yinjiaodingyi 
	// Serial_t     uart;
    void                 *rx_ringbuf_handle;
    void                 *tx_ringbuf_handle;
    uint32_t             rx_buf_size;
    uint32_t             tx_buf_size;
    // void         *mutex;
    // void         *poll_cb;
    void         *fd;
    // void         *poll_data;
    // void         *taskhdl;
    uint8_t                      read_block_flag;
    void                         *priv;
    uint8_t                      is_use;
    uint32_t                     rx_size;
    qm_uart_dev_t *              uart_dev; 
    qm_ringbuf_t                 rx_ringbuf;
    uint8_t                      rx_buff[RECV_BUFF_SIZE];
    qm_uart_read_callback_t      read;
    qm_uart_write_callback_t     write;
    qm_timer_t                   recv_timeout;
} vfs_uart_dev_t;

// Serial_t uart_stdio;

static int __uart_rx_irq(void *qm_uart);
static int __uart_tx_irq(qm_uart_dev_t *qm_uart);





int32_t qm_uart_init(qm_uart_dev_t *uart) 
{
    if (uart == NULL || uart->port > (UART_NUM - 1)) {
        return -QM_ERROR;
    }

    vfs_uart_dev_t *uart_dev = calloc(sizeof(struct vfs_uart_dev), 1);
    uart_dev->rx_buf_size = 256;
    uart_dev->tx_buf_size = 256;
    // uart_dev->rx_ringbuf_handle = xStreamBufferCreate(uart_dev->rx_buf_size, 1);
    // uart_dev->tx_ringbuf_handle = xStreamBufferCreate(uart_dev->tx_buf_size, 1);
    // HOSAL_UART_DEV_DECL(uart_stdio, uart->port, uart->config.pins.tx_pin, uart->config.pins.rx_pin, uart->config.baud_rate);
    // uart_dev->uart = uart_stdio;
    // printf("--------------[qm_uart_init] %d %d %d %ld----------------\n", uart->port, uart->config.pins.tx_pin, uart->config.pins.rx_pin, uart->config.baud_rate);
    serial_init(uart_dev->fd, uart_dev->uart.port_id, uart_dev->baud_rate, uart_dev->uart.rx_callback);
    
    // hosal_uart_init(&uart_dev->uart);
    // hosal_uart_callback_set(&uart_dev->uart, HOSAL_UART_TX_CALLBACK, __uart_tx_irq, uart_dev);
    // hosal_uart_ioctl(&uart_dev->uart, HOSAL_UART_MODE_SET, (void *)HOSAL_UART_MODE_INT);

    qm_ringbuf_init(&uart_dev->rx_ringbuf, &uart_dev->rx_buff[0], RECV_BUFF_SIZE);
    qm_ringbuf_flush(&uart_dev->rx_ringbuf);

    uart->priv = (void*)uart_dev;

    return QM_EOK;
}

static int __uart_rx_irq(void *qm_uart)
{
    uint8_t tmp_buf[64];
    int length = 0;
    vfs_uart_dev_t *uart = (vfs_uart_dev_t *)qm_uart;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // for(int i = 0; i < uart->rx_buf_size; i++)
    //     printf("%02X ", (uint8_t)uart->rx_ringbuf_handle[i]);
    
    length = hosal_uart_receive(&uart->uart, tmp_buf, sizeof(tmp_buf));
    if (length > 0) {
        printf("%02X ", tmp_buf[0]);
        // xStreamBufferSendFromISR(uart->rx_ringbuf_handle, tmp_buf,
        //         length, &xHigherPriorityTaskWoken);
        // portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        if (qm_ringbuf_isfull(&uart->rx_ringbuf)) {
            qm_ringbuf_pop(&uart->rx_ringbuf, tmp_buf, length);
        }
        qm_ringbuf_push(&uart->rx_ringbuf, tmp_buf, length);
        uart->rx_size++;
    }

    
    return 0;
}

// static int __uart_tx_irq(qm_uart_dev_t *qm_uart)
// {
//     BaseType_t xHigherPriorityTaskWoken = pdFALSE;
//     vfs_uart_dev_t *uart = (vfs_uart_dev_t *)qm_uart->priv;
//     uint8_t ch;
//     size_t ret;

//     ret = xStreamBufferReceiveFromISR(uart->tx_ringbuf_handle, &ch, 1,
//                                       &xHigherPriorityTaskWoken);
//     portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

//     if (ret == 1) {
//         hosal_uart_send(&uart->uart, (const void *)&ch, 1);
//     } else {
//         hosal_uart_ioctl(&uart->uart, HOSAL_UART_TX_TRIGGER_OFF, NULL);
//     }
//     return 0;
// }


int32_t qm_uart_write(qm_uart_dev_t *uart, const void *data, uint32_t size, uint32_t timeout) 
{
    if (uart == NULL || uart->port > (UART_NUM - 1)) {
        return -QM_ERROR;
    }

    vfs_uart_dev_t *uart_dev = (vfs_uart_dev_t *)uart->priv;
    serial_write(&uart_dev->fd, data, size);
    return uart_get_rx_not_empty_flag(&uart_dev->uart);

}


int32_t qm_uart_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout) 
{
    if (uart == NULL || uart->port > (UART_NUM - 1)) {
        return -QM_ERROR;
    }
    vfs_uart_dev_t *uart_dev = (vfs_uart_dev_t *)uart->priv;
    // return hosal_uart_receive(&uart_dev->uart, data, expect_size);
     if (qm_ringbuf_isempty(&uart_dev->rx_ringbuf)) {
        return 0;
    }

    int32_t len = qm_ringbuf_pop(&uart_dev->rx_ringbuf, data, expect_size);
    uart_dev->rx_size -= len;
    
    return (len > 0 ? len : 0); 
}


int32_t qm_uart_deinit(qm_uart_dev_t *uart) 
{
    if (uart == NULL || uart->port > (UART_NUM - 1)) {
        return -QM_ERROR;
    }

    vfs_uart_dev_t *uart_dev = (vfs_uart_dev_t *)uart->priv;

    serial_purge_rx(uart_dev->fd);//luanluanluanluanluan----------------------------------------
    serial_purge_rx(uart_dev->fd);
    free(uart_dev);

    bl_uart_int_disable(uart->port);

    
    return QM_EOK;
}

int32_t qm_uart_ctrl(qm_uart_dev_t *uart, qm_uart_ctrl_cmd_t cmd, void *param)
{
    if(uart == NULL || uart->priv == NULL || param == NULL){
        return -QM_EINVAL;
    }

    vfs_uart_dev_t *uart_dev = (vfs_uart_dev_t *)uart->priv;

    switch (cmd)
    {
        case QM_UART_CTRL_CMD_RX_CLEAR:
            qm_ringbuf_flush(&uart_dev->rx_ringbuf);
        break;
        case QM_UART_CTRL_CMD_READ_CALLBACK:
            uart_dev->read = (qm_uart_read_callback_t)param;
            // serial_rx_fifo_level(&sobj[uart->port], 0);
			// serial_irq_handler(&sobj[uart->port], uart_read_callback, (uint32_t)&sobj[uart->port]);
    		// serial_irq_set(&sobj[uart->port], RxIrq, 1);
            //hosal_uart_callback_set(&uart_dev->uart, HOSAL_UART_RX_CALLBACK, __uart_rx_irq, uart_dev);
        break;
        case QM_UART_CTRL_CMD_WRITE_CALLBACK:
            uart_dev->write = (qm_uart_write_callback_t)param;
        break;       
        default:
            break;
    }

    return QM_EOK;
}




 //my test








