#include "prodtst_hal_uart.h"
#include "uart_ext.h"
#include "qm_errno.h"


int prodtst_uart_init(prodtst_uart_port_t uart_num, uint32_t baudrate)
{
    int ret = 0; 
    ret = uart_ext_config((uart_ext_port_t)uart_num, baudrate, 1024, 1024);
    if(ret == 0){
        return QM_EOK;
    }else{
        return -QM_ERROR;
    }
}

int prodtst_uart_write(prodtst_uart_port_t uart_num, const uint8_t *buf, uint32_t length)
{
    int ret = 0; 
    ret = uart_ext_write_bytes((uart_ext_port_t)uart_num, buf, length);
    if(ret > 0){
        return ret;
    }else{
        return -QM_ERROR;
    }
}

int prodtst_uart_read(prodtst_uart_port_t uart_num, uint8_t *buf, uint32_t length, uint32_t timeout)
{
    int ret = 0; 
    ret = uart_ext_read_bytes((uart_ext_port_t)uart_num, buf, length, timeout);
    if(ret > 0){
        return ret;
    }else{
        return -QM_ERROR;
    }
}