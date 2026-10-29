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
*  |  QM GPIO         |  LIERDA GPIO |
 *   ----------------------------------------------------------------
 *  |  QM_GPIO_PIN_0   | LIOT_GPIO_0     |
 *  |  QM_GPIO_PIN_1   | LIOT_GPIO_1     |
 *  |  QM_GPIO_PIN_2   | LIOT_GPIO_2     |
 *  |  QM_GPIO_PIN_3   | LIOT_GPIO_3     | 
 *  |  QM_GPIO_PIN_4   | LIOT_GPIO_4     |
 *  |  QM_GPIO_PIN_5   | LIOT_GPIO_5     | 
 *  |  QM_GPIO_PIN_6   | LIOT_GPIO_6     |
 *  |  QM_GPIO_PIN_7   | LIOT_GPIO_7     | 
 *  |  QM_GPIO_PIN_8   | LIOT_GPIO_8     |
 *  |  QM_GPIO_PIN_9   | LIOT_GPIO_9     |
 *  |  QM_GPIO_PIN_10  | LIOT_GPIO_10    |
 *  |  QM_GPIO_PIN_11  | LIOT_GPIO_11    | 
 *  |  QM_GPIO_PIN_12  | LIOT_GPIO_12    |
 *  |  QM_GPIO_PIN_13  | LIOT_GPIO_13    |
 *  |  QM_GPIO_PIN_14  | LIOT_GPIO_14    | 
 *  |  QM_GPIO_PIN_15  | LIOT_GPIO_15    | 
 *  |  QM_GPIO_PIN_16  | LIOT_GPIO_16    |
 *  |  QM_GPIO_PIN_17  | LIOT_GPIO_17    |  
 *  |  QM_GPIO_PIN_18  | LIOT_GPIO_18    |
 *  |  QM_GPIO_PIN_19  | LIOT_GPIO_19    |
 *  |  QM_GPIO_PIN_20  | LIOT_GPIO_20    |
 *  |  QM_GPIO_PIN_21  | LIOT_GPIO_21    | 
 *  |  QM_GPIO_PIN_22  | LIOT_GPIO_22    |
 *  |  QM_GPIO_PIN_23  | LIOT_GPIO_23    | 
 *  |  QM_GPIO_PIN_24  | LIOT_GPIO_24    |
 *  |  QM_GPIO_PIN_25  | LIOT_GPIO_25    | 
 *  |  QM_GPIO_PIN_26  | LIOT_GPIO_26    |
 *  |  QM_GPIO_PIN_27  | LIOT_GPIO_27    | 
 *  |  QM_GPIO_PIN_28  | LIOT_GPIO_28    |
 *  |  QM_GPIO_PIN_29  | LIOT_GPIO_29    |  
 *  |  QM_GPIO_PIN_30  | LIOT_GPIO_30    |
 *  |  QM_GPIO_PIN_31  | LIOT_GPIO_31    | 
 *  |  QM_GPIO_PIN_32  | LIOT_GPIO_32    |
 *  |  QM_GPIO_PIN_33  | LIOT_GPIO_33    | 
 *  |  QM_GPIO_PIN_34  | LIOT_GPIO_34    |
 *  |  QM_GPIO_PIN_35  | LIOT_GPIO_35    | 
 *  |  QM_GPIO_PIN_36  | LIOT_GPIO_36    |
 *  |  QM_GPIO_PIN_37  | LIOT_GPIO_37    | 
 *  |  QM_GPIO_PIN_38  | LIOT_GPIO_38    |
 *  |  QM_GPIO_PIN_39  | LIOT_GPIO_39    |  
 *  |  QM_GPIO_PIN_40  | LIOT_GPIO_40    |
 *  |  QM_GPIO_PIN_41  | LIOT_GPIO_41    | 
 *  |  QM_GPIO_PIN_42  | LIOT_GPIO_42    |
 *  |  QM_GPIO_PIN_43  | LIOT_GPIO_43    | 
 *  |  QM_GPIO_PIN_44  | LIOT_GPIO_44    |
 *  |  QM_GPIO_PIN_45  | LIOT_GPIO_45    |  
 *  |  QM_GPIO_PIN_46  | LIOT_GPIO_46    |
 *  |  QM_GPIO_PIN_47  | LIOT_GPIO_47    | 
 *  |  QM_GPIO_PIN_48  | LIOT_GPIO_48    |
 * 
 */

typedef enum
{
    QM_GPIO_PIN_0 = 0,
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
    QM_GPIO_PIN_48
} qm_gpio_pin_t;


#endif
