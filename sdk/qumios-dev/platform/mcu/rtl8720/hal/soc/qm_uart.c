#include "serial_api.h"
#include "serial_ex_api.h"

#include "qm_uart.h"
#include "qm_types.h"
#include "qm_errno.h"
#include "qm_kernel.h"
// #include "qm_utils_timer.h"
#include "qm_ringbuf.h"

#include "stdio_port_func.h"

#define UART_NUM                    2
#define UART_SEND_FAST_MAX_SIZE    16
#define UART_PORT(X)    (PIN_NAME(PORT_A, (X)))

#define RECV_BUFF_SIZE 50

static uint8_t tx_busy = QM_FALSE;
static serial_t sobj[UART_NUM] = {0};

typedef struct
{
    uint8_t is_use;
    uint32_t rx_size;
    qm_uart_dev_t *uart_dev;
    qm_ringbuf_t rx_ringbuf;
    uint8_t rx_buff[RECV_BUFF_SIZE];
    qm_uart_read_callback_t read;
    qm_uart_write_callback_t write;
    qm_timer_t recv_timeout;
}qm_uart_info_t;

static qm_uart_info_t g_uart_info = {0};

static void uart_send_string_done (uint32_t id)
{
	tx_busy = QM_FALSE;
}

int32_t qm_uart_init(qm_uart_dev_t *uart)
{
    if(uart == NULL || uart->port > (UART_NUM - 1)){
        return -QM_EINVAL;
    }

     if( uart->config.stop_bits == QM_UART_STOP_BITS_1_5){
        return -QM_EINVAL;  
    }   

    if( uart->config.data_bits ==  QM_UART_DATA_5_BITS || 
        uart->config.data_bits == QM_UART_DATA_6_BITS){
        return -QM_EINVAL;  
    }

    qm_uart_info_t *uart_info = &g_uart_info;

    if(uart_info->is_use){
        goto _next;
    }

    serial_init(&sobj[uart->port], UART_PORT(uart->config.pins.tx_pin), UART_PORT(uart->config.pins.rx_pin));
    serial_baud(&sobj[uart->port], uart->config.baud_rate);

    switch(uart->config.parity)
    {
        case QM_UART_PARITY_NONE:
            serial_format(&sobj[uart->port], uart->config.data_bits, ParityNone, uart->config.stop_bits);
        break;
        case QM_UART_PARITY_EVEN:
            serial_format(&sobj[uart->port], uart->config.data_bits, ParityEven, uart->config.stop_bits);
        break;
        case QM_UART_PARITY_ODD:
            serial_format(&sobj[uart->port], uart->config.data_bits, ParityOdd, uart->config.stop_bits);
        break;
        default:
        break;
    }

    serial_rx_fifo_level(&sobj[uart->port], 0);
	serial_send_comp_handler(&sobj[uart->port], (void *)uart_send_string_done, (uint32_t)&sobj[uart->port]);
    uart->priv = (void *)(&sobj[uart->port]);

    //init ringbuffer
    qm_ringbuf_init(&uart_info->rx_ringbuf, &uart_info->rx_buff[0], RECV_BUFF_SIZE);

    // return QM_EOK;
_next:
    qm_ringbuf_flush(&uart_info->rx_ringbuf);
    uart_info->is_use = QM_TRUE;
    uart_info->uart_dev = uart;
    uart->priv = (void *)uart_info;
    return QM_EOK;
}

int32_t qm_uart_write(qm_uart_dev_t *uart, const void *data, uint32_t size, uint32_t timeout)
{

    if( (uart == NULL) || (uart->priv == NULL) || 
        (data == NULL) || (size == 0) )
    {
        return -QM_EINVAL;
    }

    qm_err_t ret = QM_EOK;
    char *tx_buff = (char *)data;

    for(int i = 0; i< size; i++) {
        tx_busy = QM_TRUE;
        serial_putc(&sobj[uart->port], tx_buff[i]);
    }
    tx_busy = QM_FALSE;

    return size;
}

int32_t qm_uart_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout)
{

    if( (uart == NULL) || (uart->priv == NULL) || 
        (data == NULL) || (expect_size == 0) )
    {
        return -QM_EINVAL;
    }

    qm_err_t ret = QM_EOK;

    qm_uart_info_t *uart_info = (qm_uart_info_t *)uart->priv;
    if (qm_ringbuf_isempty(&uart_info->rx_ringbuf)) {
        return 0;
    }

    int32_t len = qm_ringbuf_pop(&uart_info->rx_ringbuf, data, expect_size);
    uart_info->rx_size -= len;
    
    return (len > 0 ? len : 0); 
}

int32_t qm_uart_deinit(qm_uart_dev_t *uart)
{
    if((uart == NULL) || (uart->priv == NULL)){
        return -QM_EINVAL;
    }

    serial_t *port = (serial_t *)uart->priv; 
    serial_free(port);

    return QM_EOK;
}

static void uart_read_callback(uint32_t id, SerialIrq event)
{
    unsigned char rc=0;
    static uint8_t flag = 0;
    serial_t *obj = (void*)id;
    if(flag == 0) 
    {

        flag = 1;
        return;
    }
    rc = serial_getc(obj);
    stdio_port_putc(rc);

    if(event == RxIrq){
        qm_uart_info_t *uart_info = &g_uart_info;
        if (qm_ringbuf_isfull(&uart_info->rx_ringbuf)) {
            qm_ringbuf_pop(&uart_info->rx_ringbuf, &rc, 1);
        }
        qm_ringbuf_push(&uart_info->rx_ringbuf, &rc, 1);
        uart_info->rx_size++;

        // if(uart_info->read != NULL){
        //     uart_info->read(uart_info->uart_dev, uart_info->rx_size);
        // }
    }
}

int32_t qm_uart_ctrl(qm_uart_dev_t *uart, qm_uart_ctrl_cmd_t cmd, void *param)
{
    if(uart == NULL || uart->priv == NULL || param == NULL){
        return -QM_EINVAL;
    }

    qm_uart_info_t *uart_info = (qm_uart_info_t *)uart->priv;

    switch (cmd)
    {
        case QM_UART_CTRL_CMD_RX_CLEAR:
            qm_ringbuf_flush(&uart_info->rx_ringbuf);
        break;
        case QM_UART_CTRL_CMD_READ_CALLBACK:
            uart_info->read = (qm_uart_read_callback_t)param;
            serial_rx_fifo_level(&sobj[uart->port], 0);
			serial_irq_handler(&sobj[uart->port], uart_read_callback, (uint32_t)&sobj[uart->port]);
    		serial_irq_set(&sobj[uart->port], RxIrq, 1);
        break;
        case QM_UART_CTRL_CMD_WRITE_CALLBACK:
            uart_info->write = (qm_uart_write_callback_t)param;
        break;       
        default:
            break;
    }

    return QM_EOK;
}