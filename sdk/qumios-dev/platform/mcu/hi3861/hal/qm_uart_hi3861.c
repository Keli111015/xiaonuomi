#include "qm_uart.h"
#include "hi_uart.h"
#include "qm_errno.h"

int32_t qm_uart_init(qm_uart_dev_t *uart)
{
    hi_u32 ret;
    hi_uart_attribute uart_attr = {0};

    uart_attr.baud_rate = 115200;
    if(uart->config.data_bits == QM_UART_DATA_5_BITS){
        uart_attr.data_bits = 5;
    }else if(uart->config.data_bits == QM_UART_DATA_6_BITS){
        uart_attr.data_bits = 6;
    }else if(uart->config.data_bits == QM_UART_DATA_7_BITS){
        uart_attr.data_bits = 7;
    }else if(uart->config.data_bits == QM_UART_DATA_8_BITS){
        uart_attr.data_bits = 8;
    }

    if(uart->config.parity == QM_UART_PARITY_NONE){
        uart_attr.parity = 0;
    }else if(uart->config.parity == QM_UART_PARITY_EVEN){
        uart_attr.parity = 1;
    }
    
    if(uart->config.stop_bits == QM_UART_STOP_BITS_1){
        uart_attr.stop_bits = 1;
    }else if(uart->config.stop_bits == QM_UART_STOP_BITS_1_5){
        return -QM_EINVAL;
    }else if(uart->config.stop_bits == QM_UART_STOP_BITS_2){
         uart_attr.stop_bits = 2;
    }

    /* Initialize uart driver */
    ret = hi_uart_init(uart->port, &uart_attr, HI_NULL);
    if (ret != HI_ERR_SUCCESS) {
        printf("Failed to init uart! Err code = %d\n", ret);
    }

    return 0;

}


int32_t qm_uart_write(qm_uart_dev_t *uart, const void *data, uint32_t size, uint32_t timeout)
{
    hi_s32 ret = 0;
    ret = hi_uart_write(uart->port, data, size);
    if(ret > 0){
        return (int32_t)ret;
    }else{
        return -QM_EIO;
    }
}


int32_t qm_uart_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout)
{
    hi_s32 ret = 0;

    ret =  hi_uart_read_timeout(uart->port, (hi_u8*)data, (hi_u32)expect_size, (hi_u32)timeout);
    if(ret > 0){
        return (int32_t)ret;
    }else{
        return -QM_EIO;
    }
}
