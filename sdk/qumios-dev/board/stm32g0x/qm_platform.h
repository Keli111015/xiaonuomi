#ifndef __QM_PLATFORM_H__
#define __QM_PLATFORM_H__

#include "qm.h"
#include "qm_soc.h"

/******************************************************
 *                   Enumerations
 ******************************************************/
#include "stm32g0xx_hal.h"
#include "stm32g0xx_hal_conf.h"
/******************************************************
 *                 Type Definitions
 ******************************************************/


typedef enum {
    QM_UART1 = 0x00,
    QM_UART2,
    QM_UART3,
    QM_UART4,
    QM_UART5,
    QM_UART6,
    QM_UART_MAX,
} qm_uart_port_t;

/*            STM32G0X GPIO 映射表
 *   ----------------------------------------------------------------
*  |  QM GPIO         |  STM32 GPIO |
 *   ----------------------------------------------------------------
 * 
 */

typedef enum
{
    QM_GPIO_PIN_NONE,
    QM_GPIO_PIN_1,
    QM_GPIO_PIN_2, 
    QM_GPIO_PIN_3,
    QM_GPIO_PIN_4,
    QM_GPIO_PIN_5,
    QM_GPIO_PIN_6,
    QM_GPIO_PIN_7,
    QM_GPIO_PIN_8,
    QM_GPIO_PIN_9,
    QM_GPIO_PIN_10,
    QM_GPIO_PIN_11,
    QM_GPIO_PIN_12,
    QM_GPIO_PIN_13,
    QM_GPIO_PIN_14,
    QM_GPIO_PIN_15,
    QM_GPIO_PIN_16,
    QM_GPIO_PIN_17,
    QM_GPIO_PIN_18,
    QM_GPIO_PIN_19,
    QM_GPIO_PIN_20,
    QM_GPIO_PIN_21,
    QM_GPIO_PIN_22,
    QM_GPIO_PIN_23,
    QM_GPIO_PIN_24,
    QM_GPIO_PIN_25,
    QM_GPIO_PIN_26,
    QM_GPIO_PIN_27,
    QM_GPIO_PIN_28,
    QM_GPIO_PIN_29,
    QM_GPIO_PIN_30,
    QM_GPIO_PIN_31,
    QM_GPIO_PIN_32,
    QM_GPIO_PIN_33,
    QM_GPIO_PIN_34,
    QM_GPIO_PIN_35,
    QM_GPIO_PIN_36,
    QM_GPIO_PIN_37,
    QM_GPIO_PIN_38,
    QM_GPIO_PIN_39,
    QM_GPIO_PIN_40,
    QM_GPIO_PIN_41,
    QM_GPIO_PIN_42,
    QM_GPIO_PIN_43,
    QM_GPIO_PIN_44,
    QM_GPIO_PIN_45,
    QM_GPIO_PIN_46,
    QM_GPIO_PIN_47,
    QM_GPIO_PIN_48,
    QM_GPIO_PIN_MAX,
}qm_gpio_pin_t;

/* GPIO port */
typedef GPIO_TypeDef  platform_gpio_port_t;

typedef struct
{
    platform_gpio_port_t  *port; 
    uint16_t               pin_number;
} qm_platform_gpio_t;

const qm_platform_gpio_t *qm_platform_gpio_get_port(qm_gpio_pin_t qm_pins);

int qm_platform_gpio_enable_clock(qm_gpio_pin_t qm_pins);

int qm_platform_gpio_set_alternate_function(qm_gpio_pin_t qm_pins,
                                            qm_gpio_mode_t output_type, 
                                            qm_gpio_pull_mode_t pull_up_down_type, 
                                            uint8_t alternation_function);

#endif
