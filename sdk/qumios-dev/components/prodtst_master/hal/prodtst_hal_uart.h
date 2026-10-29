#ifndef __PRODTST_HAL_UART_H__
#define __PRODTST_HAL_UART_H__

#include "qm_types.h"

typedef enum{

    PRODTST_UART_NUM_0,  
    PRODTST_UART_NUM_1,  
    PRODTST_UART_NUM_2, 
    PRODTST_UART_NUM_3,  
    PRODTST_UART_NUM_4,  
    PRODTST_UART_NUM_5,
    PRODTST_UART_NUM_6,  
    PRODTST_UART_NUM_7,

    PRODTST_UART_NUM_MAX,

}prodtst_uart_port_t;

int prodtst_uart_init(prodtst_uart_port_t uart_num, uint32_t baudrate);

int prodtst_uart_write(prodtst_uart_port_t uart_num, const uint8_t* buf, uint32_t length);

int prodtst_uart_read(prodtst_uart_port_t uart_num, uint8_t* buf, uint32_t length, uint32_t timeout);



#endif


