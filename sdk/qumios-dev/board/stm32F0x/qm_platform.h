#ifndef __QM_PLATFORM_H__
#define __QM_PLATFORM_H__

/*            STM32F0x GPIO 映射表
 *   ----------------------------------------------------------------
*  |  QM GPIO         |  STM32 GPIO |
 *   ----------------------------------------------------------------
 *  |  QM_GPIO_PIN_0   |  GPIOA_0     |
 *  |  QM_GPIO_PIN_1   |  GPIOA_1     |
 *  |  QM_GPIO_PIN_2   |  GPIOA_2     |
 *  |  QM_GPIO_PIN_3   |  GPIOA_3     | 
 *  |  QM_GPIO_PIN_4   |  GPIOA_4     |
 *  |  QM_GPIO_PIN_5   |  GPIOA_5     | 
 *  |  QM_GPIO_PIN_6   |  GPIOA_6     |
 *  |  QM_GPIO_PIN_7   |  GPIOA_7     | 
 *  |  QM_GPIO_PIN_8   |  GPIOA_8     |
 *  |  QM_GPIO_PIN_9   |  GPIOA_9     | 
 *  |  QM_GPIO_PIN_10  |  GPIOA_10    |
 *  |  QM_GPIO_PIN_11  |  GPIOA_11    | 
 *  |  QM_GPIO_PIN_12  |  GPIOA_12    |
 *  |  QM_GPIO_PIN_13  |  GPIOA_13    |  
 *  |  QM_GPIO_PIN_14  |  GPIOA_14    |
 *  |  QM_GPIO_PIN_15  |  GPIOA_15    | 
 *  |  QM_GPIO_PIN_16  |  GPIOB_0     |
 *  |  QM_GPIO_PIN_17  |  GPIOB_1     |  
 *  |  QM_GPIO_PIN_18  |  GPIOB_2     |
 *  |  QM_GPIO_PIN_19  |  GPIOB_3     | 
 *  |  QM_GPIO_PIN_20  |  GPIOB_4     |
 *  |  QM_GPIO_PIN_21  |  GPIOB_5     | 
 *  |  QM_GPIO_PIN_22  |  GPIOB_6     |
 *  |  QM_GPIO_PIN_23  |  GPIOB_7     | 
 *  |  QM_GPIO_PIN_24  |  GPIOB_8     |
 *  |  QM_GPIO_PIN_25  |  GPIOB_9     | 
 *  |  QM_GPIO_PIN_26  |  GPIOB_10    |
 *  |  QM_GPIO_PIN_27  |  GPIOB_11    | 
 *  |  QM_GPIO_PIN_28  |  GPIOB_12    |
 *  |  QM_GPIO_PIN_29  |  GPIOB_13    | 
 *  |  QM_GPIO_PIN_30  |  GPIOB_14    | 
 *  |  QM_GPIO_PIN_31  |  GPIOB_15    | 
 *  |  QM_GPIO_PIN_32  |  GPIOC_0     | 
 *  |  QM_GPIO_PIN_33  |  GPIOC_1     | 
 *  |  QM_GPIO_PIN_34  |  GPIOC_2     | 
 *  |  QM_GPIO_PIN_35  |  GPIOC_3     | 
 *  |  QM_GPIO_PIN_36  |  GPIOC_4     | 
 *  |  QM_GPIO_PIN_37  |  GPIOC_5     | 
 *  |  QM_GPIO_PIN_38  |  GPIOC_6     | 
 *  |  QM_GPIO_PIN_39  |  GPIOC_7     | 
 *  |  QM_GPIO_PIN_40  |  GPIOC_8     | 
 *  |  QM_GPIO_PIN_41  |  GPIOC_9     | 
 *  |  QM_GPIO_PIN_42  |  GPIOC_10    | 
 *  |  QM_GPIO_PIN_43  |  GPIOC_11    | 
 *  |  QM_GPIO_PIN_44  |  GPIOC_12    | 
 *  |  QM_GPIO_PIN_45  |  GPIOC_13    | 
 *  |  QM_GPIO_PIN_46  |  GPIOC_14    | 
 *  |  QM_GPIO_PIN_47  |  GPIOC_15    | 
 *  |  QM_GPIO_PIN_48  |  invaled     |
 *  |  QM_GPIO_PIN_49  |  invaled     |  
 *  |  QM_GPIO_PIN_50  |  GPIOD_2     |
 * 
 */

typedef enum
{
    QM_GPIO_PIN_0 = 0,
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
    QM_GPIO_PIN_48,
    QM_GPIO_PIN_49,
    QM_GPIO_PIN_50,
    QM_GPIO_PIN_MAX,
}qm_gpio_pin_t;

#endif
