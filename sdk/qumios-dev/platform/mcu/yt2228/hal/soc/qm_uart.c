#include "qm_uart.h"
#include "qm_types.h"
#include "qm_errno.h"
#include "qm_kernel.h"

#include "uart_dev.h"
#include "user_uart.h"

#define  UART_NUM   2

typedef struct {
    qm_uart_dev_t *uart_dev;
    qm_uart_read_callback_t read;
    qm_uart_write_callback_t write;
}uart_info_t;

static uart_info_t g_uart_info[UART_NUM] = {0};

static void callback_fun(void *ut_bus, u32 status) 
{
    u8 uart_num = 0;
    uart_bus_t* bus = NULL;
    uart_info_t *uart_info = NULL;
	if (status == UT_RX || status == UT_RX_OT) {
		bus = (uart_bus_t*)ut_bus;
		if(bus == NULL) {
            return;
		}
        uart_num = *((u8*)bus->argv);
        uart_info = &g_uart_info[uart_num];
        if(uart_info->read){
            uart_info->read(uart_info->uart_dev, bus->frame_length);
        }
	}
}

int32_t qm_uart_ctrl(qm_uart_dev_t *uart, qm_uart_ctrl_cmd_t cmd, void *param)
{
    if(uart == NULL || uart->priv == NULL){
        return -QM_EINVAL;
    }

    uart_info_t *uart_info = (uart_info_t *)uart->priv;

    switch (cmd)
    {
        case QM_UART_CTRL_CMD_RX_CLEAR:
            
        break;
        case QM_UART_CTRL_CMD_READ_CALLBACK:
            uart_info->read = (qm_uart_read_callback_t)param;
        break;
        case QM_UART_CTRL_CMD_WRITE_CALLBACK:
            uart_info->write = (qm_uart_write_callback_t)param;
        break;       
        default:
        break;
    }

    return QM_EOK;
}

int32_t qm_uart_init(qm_uart_dev_t *uart)
{
    int ret = QM_EOK;

    if(uart == NULL || uart->port > (UART_NUM - 1)){
        return -QM_EINVAL;
    }

    if(g_uart_info[uart->port].uart_dev){
        g_uart_info[uart->port].uart_dev = uart;
        uart->priv = &g_uart_info[uart->port];
        return QM_EOK;
    }

    if(uart->config.parity != QM_UART_PARITY_NONE ||
        uart->config.stop_bits != QM_UART_STOP_BITS_1 ||
        uart->config.data_bits !=  QM_UART_DATA_8_BITS ||
        uart->config.flow_ctrl !=  QM_UART_FLOW_CTRL_NONE){
        return -QM_EINVAL;  
    }   

    ret = user_uart_init(   uart->config.pins.tx_pin, 
                            uart->config.pins.rx_pin,
                            uart->config.baud_rate,
                            uart->config.data_bits,
                            uart->config.stop_bits,
                            uart->config.parity,
                            callback_fun,
                            uart->port);
    if(ret != QM_EOK){
        return -QM_EINVAL;
    }

    g_uart_info[uart->port].uart_dev = uart;
    uart->priv = &g_uart_info[uart->port];

    return  QM_EOK;
}



int32_t qm_uart_write(qm_uart_dev_t *uart, const void *data, uint32_t size, uint32_t timeout)
{

    if( (uart == NULL) || (data == NULL) || (size == 0) )
    {
        return -QM_EINVAL;
    }

    return user_uart_send(data, size, uart->port);
}

int32_t qm_uart_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout)
{
    if( (uart == NULL) || (data == NULL) || (expect_size == 0) )
    {
        return -QM_EINVAL;
    }

    return user_uart_receive(data, expect_size, timeout, uart->port);
}

int32_t qm_uart_deinit(qm_uart_dev_t *uart)
{
    if((uart == NULL) || (uart->priv == NULL)){
        return -QM_EINVAL;
    }

    user_uart_deinit(uart->port);
    uart->priv = NULL;
    return QM_EOK;
}