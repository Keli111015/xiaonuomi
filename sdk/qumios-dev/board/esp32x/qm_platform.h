#ifndef __QM_PLATFORM_H__
#define __QM_PLATFORM_H__

/*            ESP32X UART 映射表
 *   ----------------------------------------------------------------
 *  |  QM UART PORT     |  STM32 UART PORT |
 *   ----------------------------------------------------------------
 *  |  QM_UART0   |  USART0     |
 *  |  QM_UART1   |  USART1     |
 *  |  QM_UART2   |  USART2     |
 */

typedef enum {
    QM_UART0 = 0x00,
    QM_UART1,
    QM_UART2,
    QM_UART_MAX,
} qm_uart_port_t;
/* 
 *   ----------------------------------------------------------------
*  |  QM GPIO         |  ESP32 GPIO |
 *   ----------------------------------------------------------------
 *  |  QM_GPIO_PIN_1   |  GPIO_1     |
 *  |  QM_GPIO_PIN_2   |  GPIO_2     |
 *  |  QM_GPIO_PIN_3   |  GPIO_3     | 
 *  |  QM_GPIO_PIN_4   |  GPIO_4     |
 *  |  QM_GPIO_PIN_5   |  GPIO_5     | 
 *  |  QM_GPIO_PIN_6   |  GPIO_6     |
 *  |  QM_GPIO_PIN_7   |  GPIO_7     | 
 *  |  QM_GPIO_PIN_8   |  GPIO_8     |
 *  |  QM_GPIO_PIN_9   |  GPIO_9     |
 *  |  QM_GPIO_PIN_10  |  GPIO_10    |
 *  |  QM_GPIO_PIN_11  |  GPIO_11    | 
 *  |  QM_GPIO_PIN_12  |  GPIO_12    |
 *  |  QM_GPIO_PIN_13  |  GPIO_13    |
 *  |  QM_GPIO_PIN_14  |  GPIO_14    | 
 *  |  QM_GPIO_PIN_15  |  GPIO_15    | 
 *  |  QM_GPIO_PIN_16  |  GPIO_16    |
 *  |  QM_GPIO_PIN_17  |  GPIO_17    |  
 *  |  QM_GPIO_PIN_18  |  GPIO_18    |
 *  |  QM_GPIO_PIN_19  |  GPIO_19    |
 *  |  QM_GPIO_PIN_20  |  GPIO_20    |
 *  |  QM_GPIO_PIN_21  |  GPIO_21    | 
 *  |  QM_GPIO_PIN_22  |  GPIO_22    |
 *  |  QM_GPIO_PIN_23  |  GPIO_23    | 
 *  |  QM_GPIO_PIN_24  |  GPIO_24    |
 *  |  QM_GPIO_PIN_25  |  GPIO_25    | 
 *  |  QM_GPIO_PIN_26  |  GPIO_26    |
 *  |  QM_GPIO_PIN_27  |  GPIO_27    | 
 *  |  QM_GPIO_PIN_28  |  GPIO_28    |
 *  |  QM_GPIO_PIN_29  |  GPIO_29    |  
 *  |  QM_GPIO_PIN_30  |  GPIO_30    |
 *  |  QM_GPIO_PIN_31  |  GPIO_31    | 
 *  |  QM_GPIO_PIN_32  |  GPIO_32    |
 *  |  QM_GPIO_PIN_33  |  GPIO_33    | 
 *  |  QM_GPIO_PIN_34  |  GPIO_34    |
 *  |  QM_GPIO_PIN_35  |  GPIO_35    | 
 *  |  QM_GPIO_PIN_36  |  GPIO_36    |
 *  |  QM_GPIO_PIN_37  |  GPIO_37    | 
 *  |  QM_GPIO_PIN_38  |  GPIO_38    |
 *  |  QM_GPIO_PIN_39  |  GPIO_39    |  
 *  |  QM_GPIO_PIN_40  |  GPIO_40    |
 *  |  QM_GPIO_PIN_41  |  GPIO_41    | 
 *  |  QM_GPIO_PIN_42  |  GPIO_42    |
 *  |  QM_GPIO_PIN_43  |  GPIO_43    | 
 *  |  QM_GPIO_PIN_44  |  GPIO_44    |
 *  |  QM_GPIO_PIN_45  |  GPIO_45    |  
 *  |  QM_GPIO_PIN_46  |  GPIO_46    |
 *  |  QM_GPIO_PIN_47  |  GPIO_47    | 
 *  |  QM_GPIO_PIN_48  |  GPIO_48    |
 * 
 */

typedef enum
{
    QM_GPIO_PIN_1 = 1,
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
    QM_GPIO_PIN_48
} qm_gpio_pin_t;


#endif
