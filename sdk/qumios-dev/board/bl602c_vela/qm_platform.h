#ifndef __QM_PLATFORM_H__
#define __QM_PLATFORM_H__

#include "qm.h"
#include "qm_soc.h"

/******************************************************
 *                   Enumerations
 ******************************************************/

/******************************************************
 *                 Type Definitions
 ******************************************************/


typedef enum {
    QM_UART1 = 0x00,
    QM_UART2,
    QM_UART_MAX,
} qm_uart_port_t;

/*            BL602C_VELA GPIO 映射表 (与米家模组实际iopin想对应)
 *   ----------------------------------------------------------------
*  |  QM GPIO         |  STM32 GPIO |
 *   ----------------------------------------------------------------
 * 
 */

typedef enum
{
    QM_GPIO_PIN_NONE,
    QM_GPIO_PIN_0 =     0,
    QM_GPIO_PIN_1 =     1,
    QM_GPIO_PIN_2 =     2, 
    QM_GPIO_PIN_3 =     3,
    QM_GPIO_PIN_4 =     4,  
    QM_GPIO_PIN_5 =     5,
    QM_GPIO_PIN_7 =     7,
    QM_GPIO_PIN_8 =     8,
    QM_GPIO_PIN_11 =    11,
    QM_GPIO_PIN_12 =    12,
    QM_GPIO_PIN_14 =    14,
    QM_GPIO_PIN_16 =    16,
    QM_GPIO_PIN_20 =    20,
    QM_GPIO_PIN_21 =    21,
    QM_GPIO_PIN_22 =    22,
    QM_GPIO_PIN_MAX,
}qm_gpio_pin_t;

#endif
