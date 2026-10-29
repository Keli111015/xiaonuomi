#include "qm_uart.h"
#include "qm_errno.h"
#include "qm_kernel.h"
#include "hal/soc/uart.h"

#define SUPPORT_SIMULATE_UART 1

typedef struct {
    uint8_t tx_pin;
    uint8_t rx_pin;
} uart_pin_t;

#if SUPPORT_SIMULATE_UART

#define  SUPPORT_SIMULATE_UART_PORT  2
extern void *simulate_uart_init(uint8_t tx_pin, uint8_t rx_pin);
extern void simulate_uart_send(void *handle, uint8_t *data, uint16_t len);
extern int32_t simulate_uart_recv(void *handle, void *data, uint32_t expect_size, uint32_t timeout);

#endif

int32_t qm_uart_init(qm_uart_dev_t *uart)
{
    int res = 0;
    qm_err_t ret = QM_EOK;
    uart_pin_t pins = {0};
    uart_dev_t *user_uart = NULL;

    if(uart == NULL){
        return -QM_EINVAL;
    }

#if SUPPORT_SIMULATE_UART
    if(uart->port == SUPPORT_SIMULATE_UART_PORT){
        uart->priv = simulate_uart_init(uart->config.pins.tx_pin, uart->config.pins.rx_pin);
        if(uart->priv == NULL){
            return -QM_ERROR;
        }
        return QM_EOK;
    }

#endif

    user_uart = (uart_dev_t*)qm_malloc(sizeof(uart_dev_t));
    if(user_uart == NULL){
        return -QM_ENOMEM;
    }
    memset(user_uart, 0, sizeof(uart_dev_t));
    uart->priv = (void*)user_uart;

    user_uart->port = uart->port;
    user_uart->config.baud_rate = uart->config.baud_rate;
    user_uart->config.mode = MODE_TX_RX;

    pins.tx_pin = uart->config.pins.tx_pin;
    pins.rx_pin = uart->config.pins.rx_pin;
    user_uart->priv = (void*)&pins;

    if(uart->config.data_bits == QM_UART_DATA_8_BITS){
        user_uart->config.data_width = DATA_WIDTH_8BIT;
    }else if(uart->config.data_bits == QM_UART_DATA_7_BITS){
        user_uart->config.data_width = DATA_WIDTH_7BIT;
    }else if(uart->config.data_bits == QM_UART_DATA_6_BITS){
        user_uart->config.data_width = DATA_WIDTH_6BIT;
    }else if(uart->config.data_bits == QM_UART_DATA_5_BITS){
        user_uart->config.data_width = DATA_WIDTH_5BIT;
    }

    if(uart->config.parity == QM_UART_PARITY_NONE){
        user_uart->config.parity = NO_PARITY;
    }else if(uart->config.parity == QM_UART_PARITY_EVEN){
        user_uart->config.parity = EVEN_PARITY;
    }else if(uart->config.parity == QM_UART_PARITY_ODD){
        user_uart->config.parity = ODD_PARITY;
    }

    if(uart->config.stop_bits == QM_UART_STOP_BITS_1){
        user_uart->config.stop_bits = STOP_BITS_1;
    }else if(uart->config.stop_bits == QM_UART_STOP_BITS_1_5){
        ret = -QM_EINVAL;
        goto __exit;
    }else if(uart->config.stop_bits == QM_UART_STOP_BITS_2){
        user_uart->config.stop_bits = STOP_BITS_2;
    }

    if(uart->config.flow_ctrl == QM_UART_FLOW_CTRL_NONE){
        user_uart->config.flow_control = FLOW_CONTROL_DISABLED;
    }else if(uart->config.flow_ctrl == QM_UART_FLOW_CTRL_RTS){
        user_uart->config.flow_control = FLOW_CONTROL_RTS;
    }else if(uart->config.flow_ctrl == QM_UART_FLOW_CTRL_CTS){
         user_uart->config.flow_control = FLOW_CONTROL_CTS;
    }else if(uart->config.flow_ctrl == QM_UART_FLOW_CTRL_CTS_RTS){
         user_uart->config.flow_control = FLOW_CONTROL_CTS_RTS;
    }

    res = hal_uart_init(user_uart);
    if(res != 0){
        ret = -QM_EIO;
        goto __exit;
    }

    return QM_EOK;

__exit:
    if(user_uart){
        qm_free(user_uart);
        user_uart = NULL;
    }

    return ret;
}

int32_t qm_uart_write(qm_uart_dev_t *uart, const void *data, uint32_t size, uint32_t timeout)
{
    int res = 0;
    uint32_t recv_size = 0;
    uart_dev_t *user_uart = NULL;
    const uint8_t *tx_data = (const uint8_t *)data;
    if(uart == NULL || uart->priv == NULL || data == NULL || size == 0){
        return -QM_EINVAL;
    }

#if SUPPORT_SIMULATE_UART
    if(uart->port == SUPPORT_SIMULATE_UART_PORT){
        simulate_uart_send(uart->priv, data, (uint16_t)size);
        return QM_EOK;
    }
#endif

    user_uart = (uart_dev_t*)uart->priv;
    for(int i = 0; i < size;)
    {
        res = bl_uart_data_send(user_uart->port, *(tx_data + i));
        if(res != 0){
            return -QM_EIO;
        }
        i++;
    }
    return (int32_t)size;
}


int32_t qm_uart_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout)
{   
    int res = 0;
    uint32_t recv_size = 0;
    uart_dev_t *user_uart = NULL;
    if(uart == NULL || uart->priv == NULL){
        return -QM_EINVAL;
    }

#if SUPPORT_SIMULATE_UART
    if(uart->port == SUPPORT_SIMULATE_UART_PORT){
        return simulate_uart_recv(uart->priv, data, expect_size, timeout);
    }
#endif

    user_uart = (uart_dev_t*)uart->priv;

    res = hal_uart_recv_II(user_uart, data, expect_size, &recv_size, timeout);
    if(res != 0){
        return -QM_EIO;
    }
    
    return (int32_t)recv_size;
}


int32_t qm_uart_deinit(qm_uart_dev_t *uart)
{
    int res = 0;
    qm_err_t ret = QM_EOK;
    uart_dev_t *user_uart = NULL;
    if(uart == NULL || uart->priv == NULL){
        return -QM_EINVAL;
    }

    user_uart = (uart_dev_t*)uart->priv;

    res = hal_uart_finalize(user_uart);
    if(res != 0){
        ret = -QM_EIO;
    }

    qm_free(user_uart);

    return QM_EOK;
}