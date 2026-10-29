#include "qm_types.h"
#include "qm_uart.h"
#include "qm_errno.h"
#include "liot_gpio.h"
#include "liot_os.h"
#include "liot_type.h"
#include "liot_uart.h"
#include "qm_ringbuf.h"
#include "qm_utils_timer.h"
#include "lierda_log.h"
#include "qm_platform.h"

typedef struct
{
    liot_uart_config_s usart_config;

    uint8_t *buf;
    uint16_t size;
    uint16_t rx_size;
    qm_sem_t rx_sem;
    qm_ringbuf_t ringbuf;

    uint8_t is_wait;
    qm_uart_dev_t *uart_dev;
    qm_uart_read_callback_t read;
    qm_uart_write_callback_t write;

}qm_uart_info_t;

static qm_uart_info_t g_uart_info[] = {
    {{0}, NULL, 512, 0, {0}, {0}, 0, NULL, NULL, NULL},
    {{0}, NULL, 512, 0, {0}, {0}, 0, NULL, NULL, NULL},
    {{0}, NULL, 1024, 0, {0}, {0}, 0, NULL, NULL, NULL},
    {{0}, NULL, 512, 0, {0}, {0}, 0, NULL, NULL, NULL},
    {{0}, NULL, 512, 0, {0}, {0}, 0, NULL, NULL, NULL},
};

extern const liot_uart_func_s liot_uart_pin_func[];
__attribute__((used)) static bool config_uart_bit_func(int uartPort)
{
    int uart_tx_bit     = 0;
    int uart_tx_func    = 0;
    int uart_rx_bit     = 0;
    int uart_rx_func    = 0;

    if(liot_uart_pin_func[uartPort].port == LIOT_PORT_NONE)
        return false;

    uart_tx_bit = liot_uart_pin_func[uartPort].tx_pin;
    uart_tx_func = liot_uart_pin_func[uartPort].tx_func;
    uart_rx_bit = liot_uart_pin_func[uartPort].rx_pin;
    uart_rx_func = liot_uart_pin_func[uartPort].rx_func;

    if(uart_tx_bit == LIOT_UART_PIN_NONE || uart_tx_func != LIOT_UART_FUNC_NONE)
        return false;

    liot_pin_set_func(uart_tx_bit, uart_tx_func);
    liot_pin_set_func(uart_rx_bit, uart_rx_func);

    return true;
}

static void liot_uart_notify_cb(uint32 ind_type, liot_uart_port_number_e port, uint32 size)
{
    #define RECV_BUF_SIZE 256
    uint8_t recv_buf[RECV_BUF_SIZE] = {0}; 
    qm_uart_info_t *info = NULL;
    unsigned int real_size = 0;
    int read_len           = 0;
    if(port >= QM_ARRAY_SIZE(g_uart_info)){
        return;
    }
    info = &g_uart_info[port];

    switch (ind_type)
    {
        case LIOT_UART_RX_OVERFLOW_IND: // rx buffer overflow
        case LIOT_UART_RX_RECV_DATA_IND:
        {



            while (size > 0)
            {
                real_size = MIN(size, RECV_BUF_SIZE);
                read_len  = liot_uart_read(port, recv_buf, real_size);
                info->rx_size += qm_ringbuf_push(&info->ringbuf, recv_buf, read_len);

                if ((read_len > 0) && (size >= read_len)){
                    size -= read_len;
                }else{
                    break;
                }    

            }

            if(info->read){
                info->read(info->uart_dev, info->rx_size);
            }else{
                qm_sem_signal(&info->rx_sem);
            }
            
            break;
        }
        case LIOT_UART_TX_FIFO_COMPLETE_IND:
        {
            break;
        }
    }
}

int32_t qm_uart_init(qm_uart_dev_t *uart)
{
    int ret                         = 0;
    qm_uart_info_t *info = NULL;
    if(uart == NULL) {
        return -QM_EINVAL;
    }

    if(uart->port >= QM_ARRAY_SIZE(g_uart_info)){
        return -QM_EINVAL;
    }

    info = &g_uart_info[uart->port];

    info->usart_config.baudrate = uart->config.baud_rate;

    if(uart->config.data_bits == QM_UART_DATA_8_BITS){
        info->usart_config.data_bit = LIOT_UART_DATABIT_8;
    }else if(uart->config.data_bits == QM_UART_DATA_7_BITS){
        info->usart_config.data_bit = LIOT_UART_DATABIT_7;
    }else{
        return -QM_ERROR;
    }

    if(uart->config.parity == QM_UART_PARITY_NONE){
        info->usart_config.parity_bit = LIOT_UART_PARITY_NONE;
    }else if(uart->config.parity == QM_UART_PARITY_EVEN){
        info->usart_config.parity_bit = LIOT_UART_PARITY_EVEN;
    }else if(uart->config.parity == QM_UART_PARITY_ODD){
        info->usart_config.parity_bit = LIOT_UART_PARITY_ODD;
    }

    if(uart->config.stop_bits == QM_UART_STOP_BITS_1){
        info->usart_config.stop_bit = LIOT_UART_STOP_1;
    }else if(uart->config.stop_bits == QM_UART_STOP_BITS_2){
        info->usart_config.stop_bit = LIOT_UART_STOP_2;
    }else{
        return -QM_ERROR;
    }

    if(uart->config.flow_ctrl == QM_UART_FLOW_CTRL_NONE){
        info->usart_config.flow_ctrl = LIOT_FC_NONE;
    }else if(uart->config.flow_ctrl == QM_UART_FLOW_CTRL_CTS_RTS){
        info->usart_config.flow_ctrl = LIOT_FC_HW;
    }else{
        return -QM_ERROR;
    }

    config_uart_bit_func(uart->port);

    liot_uart_set_dcbconfig(uart->port, &info->usart_config);

    ret = liot_uart_open(uart->port);
    if (ret == LIOT_UART_SUCCESS){
        liot_uart_register_cb(uart->port, liot_uart_notify_cb);
        QM_LOGE("uart", "uart %d open success", uart->port);
    }else{
        QM_LOGE("uart", "uart %d open fail", uart->port);
        return -QM_ERROR;
    }
    switch (uart->port) {
        case QM_UART0:
            liot_uartPort_tx_way_config(uart->port, LIOT_UART_TX_DRIVER);
            break;
        case QM_UART1:
            liot_uartPort_tx_way_config(uart->port, LIOT_UART_TX_DRIVER);
            break;      
        case QM_UART2:
            liot_uartPort_tx_way_config(uart->port, LIOT_UART_TX_DRIVER);
            break;
        default:
            break;
    }

    info->buf = (uint8_t*)qm_malloc(info->size);
    if(info->buf == NULL){
        return -QM_ENOMEM;
    }
    memset(info->buf, 0, info->size);

    qm_sem_new(&info->rx_sem, 0);
    qm_ringbuf_init(&info->ringbuf, info->buf, info->size);
    
    info->uart_dev = uart;
    uart->priv = info;

    return QM_EOK;
}

int32_t qm_uart_write(qm_uart_dev_t *uart, const void *data, uint32_t size, uint32_t timeout)
{
    if(uart == NULL || data == NULL || size == 0){
        return -QM_EINVAL;
    }
    return liot_uart_write(uart->port, (uint8_t*)data, size);
}

int32_t qm_uart_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout)
{
    int len = 0;
    int recv_len = 0;
    qm_err_t ret = QM_EOK;
    uint32_t t_left = 0;
    uint8_t isempty = 0;
    qm_utils_time_t time = {0};
    qm_uart_info_t *uart_info = NULL;

    if(uart == NULL || data == NULL || expect_size == 0){
        return -QM_EINVAL;
    }

    if(uart->priv == NULL){
        return -QM_EINVAL;
    }

    if(timeout >= (UINT32_MAX / 2)){
        timeout = (UINT32_MAX / 2);
    }

    uart_info = (qm_uart_info_t*)uart->priv;
    qm_utils_time_init(&time);
    qm_utils_time_countdown_ms(&time, timeout);  

    do{
        t_left = qm_utils_time_left(&time);
        isempty = qm_ringbuf_isempty(&uart_info->ringbuf);

        if(!isempty){
            
            len = qm_ringbuf_pop(&uart_info->ringbuf, (uint8_t *)data + recv_len, expect_size-recv_len);
            if(len > 0){
                recv_len += len;
                uart_info->rx_size -= len;
            }else{
                ret = 0;
                break;
            }
        }else{

            if(t_left == 0 || uart_info->read){
                ret = 0;
                break;
            }

            ret = qm_sem_wait(&uart_info->rx_sem, t_left);
            if(ret != QM_EOK){
                ret = 0;
                break;
            }
        }            
    }while ((recv_len < expect_size) && (qm_utils_time_left(&time) > 0));

    return (0 != recv_len) ? recv_len : ret;
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
            qm_ringbuf_flush(&uart_info->ringbuf);
        break;
        case QM_UART_CTRL_CMD_READ_CALLBACK:
            uart_info->read = (qm_uart_read_callback_t)param;
            // qm_sem_free(&uart_info->rx_sem);
        break;
        case QM_UART_CTRL_CMD_WRITE_CALLBACK:
            uart_info->write = (qm_uart_write_callback_t)param;
        break;       
        default:
            break;
    }

    return QM_EOK;
}

int32_t qm_uart_deinit(qm_uart_dev_t *uart)
{
    return QM_EOK;
}