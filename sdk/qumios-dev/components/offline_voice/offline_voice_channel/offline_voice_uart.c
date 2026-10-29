#include "qm.h"
#include "qm_log.h"
#include "qm_uart.h"

#if CONFIG_OFFLINE_VOICE_UART_SUPPORT


int32_t offline_voice_channel_send(void *handle, char *buf, int size)
{
    if(handle == NULL || buf == NULL || size == 0){
        return -QM_EINVAL;
    }
    qm_uart_dev_t *uart = (qm_uart_dev_t *)handle;
    return qm_uart_write(uart, buf, size, 0);
}

int32_t offline_voice_channel_recv(void *handle, char *buf, int size, int timeout)
{
    if(handle == NULL || buf == NULL || size == 0){
        return -QM_EINVAL;
    }
    qm_uart_dev_t *uart = (qm_uart_dev_t *)handle;
    return qm_uart_read(uart, buf, size, timeout);
}

void *offline_voice_uart_init(void)
{

    qm_uart_dev_t uart = {
        .config = {
            .baud_rate = CONFIG_OFFLINE_VOICE_UART_BAUDRATE,
            .data_bits = QM_UART_DATA_8_BITS,
            .parity    = QM_UART_PARITY_NONE,
            .pins.tx_pin = CONFIG_OFFLINE_VOICE_UART_TX_PIN,
            .pins.rx_pin = CONFIG_OFFLINE_VOICE_UART_RX_PIN,
            .stop_bits = QM_UART_STOP_BITS_1 ,
        },
        .port = CONFIG_OFFLINE_VOICE_UART_PORT,
    };

    qm_uart_dev_t *uart_dev = qm_malloc(sizeof(qm_uart_dev_t));
    if(uart_dev == NULL){
        return NULL;
    }

    memcpy(uart_dev, &uart, sizeof(qm_uart_dev_t));
    qm_uart_init(uart_dev);

    return (void *)uart_dev; 
}

int offline_voice_uart_deinit(void *handle)
{
    if (handle == NULL){
        return -QM_EINVAL;
    }

    int ret = QM_EOK;
    qm_uart_dev_t *uart = (qm_uart_dev_t *)handle;

    qm_uart_deinit(uart);
    handle = NULL;
    return ret;
}
#endif