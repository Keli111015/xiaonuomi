#ifndef __QM_PLATFORM_H__
#define __QM_PLATFORM_H__


/*            STM32F10x UART 映射表
 *   ----------------------------------------------------------------
 *  |  QM UART PORT     |  STM32 UART PORT |
 *   ----------------------------------------------------------------
 *  |  QM_UART1   |  USART1     |
 *  |  QM_UART2   |  USART2     |
 *  |  QM_UART3   |  USART3     |
 *  |  QM_UART4   |  USART4     | 
 *  |  QM_UART5   |  USART5     |
 *  |  QM_UART6   |  USART6     |
 */

/*            STM32F10x GPIO 映射表
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
 *  |  QM_GPIO_PIN_48  |  GPIOD_0     | 
 *  |  QM_GPIO_PIN_49  |  GPIOD_1     | 
 *  |  QM_GPIO_PIN_50  |  GPIOD_2     | 
 *  |  QM_GPIO_PIN_51  |  GPIOD_3     | 
 *  |  QM_GPIO_PIN_52  |  GPIOD_4     | 
 *  |  QM_GPIO_PIN_53  |  GPIOD_5     | 
 *  |  QM_GPIO_PIN_54  |  GPIOD_6     | 
 *  |  QM_GPIO_PIN_55  |  GPIOD_7     | 
 *  |  QM_GPIO_PIN_56  |  GPIOD_8     | 
 *  |  QM_GPIO_PIN_57  |  GPIOD_9     | 
 *  |  QM_GPIO_PIN_58  |  GPIOD_10    | 
 *  |  QM_GPIO_PIN_59  |  GPIOD_11    | 
 *  |  QM_GPIO_PIN_60  |  GPIOD_12    | 
 *  |  QM_GPIO_PIN_61  |  GPIOD_13    | 
 *  |  QM_GPIO_PIN_62  |  GPIOD_14    | 
 *  |  QM_GPIO_PIN_63  |  GPIOD_15    | 
 *  |  QM_GPIO_PIN_64  |  GPIOE_0     | 
 *  |  QM_GPIO_PIN_65  |  GPIOE_1     | 
 *  |  QM_GPIO_PIN_66  |  GPIOE_2     | 
 *  |  QM_GPIO_PIN_67  |  GPIOE_3     | 
 *  |  QM_GPIO_PIN_68  |  GPIOE_4     | 
 *  |  QM_GPIO_PIN_69  |  GPIOE_5     | 
 *  |  QM_GPIO_PIN_70  |  GPIOE_6     | 
 *  |  QM_GPIO_PIN_71  |  GPIOE_7     | 
 *  |  QM_GPIO_PIN_72  |  GPIOE_8     | 
 *  |  QM_GPIO_PIN_73  |  GPIOE_9     | 
 *  |  QM_GPIO_PIN_74  |  GPIOE_10    | 
 *  |  QM_GPIO_PIN_75  |  GPIOE_11    | 
 *  |  QM_GPIO_PIN_76  |  GPIOE_12    | 
 *  |  QM_GPIO_PIN_77  |  GPIOE_13    | 
 *  |  QM_GPIO_PIN_78  |  GPIOE_14    | 
 *  |  QM_GPIO_PIN_79  |  GPIOE_15    | 
 *  |  QM_GPIO_PIN_80  |  GPIOF_0     | 
 *  |  QM_GPIO_PIN_81  |  GPIOF_1     | 
 *  |  QM_GPIO_PIN_82  |  GPIOF_2     | 
 *  |  QM_GPIO_PIN_83  |  GPIOF_3     | 
 *  |  QM_GPIO_PIN_84  |  GPIOF_4     | 
 *  |  QM_GPIO_PIN_85  |  GPIOF_5     | 
 *  |  QM_GPIO_PIN_86  |  GPIOF_6     | 
 *  |  QM_GPIO_PIN_87  |  GPIOF_7     | 
 *  |  QM_GPIO_PIN_88  |  GPIOF_8     | 
 *  |  QM_GPIO_PIN_89  |  GPIOF_9     | 
 *  |  QM_GPIO_PIN_90  |  GPIOF_10    | 
 *  |  QM_GPIO_PIN_91  |  GPIOF_11    | 
 *  |  QM_GPIO_PIN_92  |  GPIOF_12    | 
 *  |  QM_GPIO_PIN_93  |  GPIOF_13    | 
 *  |  QM_GPIO_PIN_94  |  GPIOF_14    | 
 *  |  QM_GPIO_PIN_95  |  GPIOF_15    |
 *  |  QM_GPIO_PIN_96  |  GPIOF_0     | 
 *  |  QM_GPIO_PIN_97  |  GPIOF_1     | 
 *  |  QM_GPIO_PIN_98  |  GPIOF_2     | 
 *  |  QM_GPIO_PIN_99  |  GPIOF_3     | 
 *  |  QM_GPIO_PIN_100 |  GPIOF_4     | 
 *  |  QM_GPIO_PIN_101 |  GPIOG_5     | 
 *  |  QM_GPIO_PIN_102 |  GPIOG_6     | 
 *  |  QM_GPIO_PIN_103 |  GPIOG_7     | 
 *  |  QM_GPIO_PIN_104 |  GPIOG_8     | 
 *  |  QM_GPIO_PIN_105 |  GPIOG_9     | 
 *  |  QM_GPIO_PIN_106 |  GPIOG_10    | 
 *  |  QM_GPIO_PIN_107 |  GPIOG_11    | 
 *  |  QM_GPIO_PIN_108 |  GPIOG_12    | 
 *  |  QM_GPIO_PIN_109 |  GPIOG_13    | 
 *  |  QM_GPIO_PIN_110 |  GPIOG_14    | 
 *  |  QM_GPIO_PIN_111 |  GPIOG_15    |  
 * 
 */

#define GPIOX_PIN_MAX_NUM       (16) // GPIOPIN0 ~ GPIO15 
#define GPIOX_TYPEDEF_MAX_NUM   (7) // GPIOA~GPIOG

typedef enum {
    QM_UART1 = 0x00,
    QM_UART2,
    QM_UART3,
    QM_UART4,
    QM_UART5,
    QM_UART6,
    QM_UART_MAX,
} qm_uart_port_t;

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
    QM_GPIO_PIN_51, 
    QM_GPIO_PIN_52, 
    QM_GPIO_PIN_53, 
    QM_GPIO_PIN_54, 
    QM_GPIO_PIN_55, 
    QM_GPIO_PIN_56, 
    QM_GPIO_PIN_57, 
    QM_GPIO_PIN_58, 
    QM_GPIO_PIN_59, 
    QM_GPIO_PIN_60, 
    QM_GPIO_PIN_61, 
    QM_GPIO_PIN_62, 
    QM_GPIO_PIN_63, 
    QM_GPIO_PIN_64, 
    QM_GPIO_PIN_65, 
    QM_GPIO_PIN_66, 
    QM_GPIO_PIN_67, 
    QM_GPIO_PIN_68, 
    QM_GPIO_PIN_69, 
    QM_GPIO_PIN_70, 
    QM_GPIO_PIN_71, 
    QM_GPIO_PIN_72, 
    QM_GPIO_PIN_73, 
    QM_GPIO_PIN_74, 
    QM_GPIO_PIN_75, 
    QM_GPIO_PIN_76, 
    QM_GPIO_PIN_77, 
    QM_GPIO_PIN_78, 
    QM_GPIO_PIN_79, 
    QM_GPIO_PIN_80, 
    QM_GPIO_PIN_81, 
    QM_GPIO_PIN_82, 
    QM_GPIO_PIN_83, 
    QM_GPIO_PIN_84, 
    QM_GPIO_PIN_85, 
    QM_GPIO_PIN_86, 
    QM_GPIO_PIN_87, 
    QM_GPIO_PIN_88, 
    QM_GPIO_PIN_89, 
    QM_GPIO_PIN_90, 
    QM_GPIO_PIN_91, 
    QM_GPIO_PIN_92, 
    QM_GPIO_PIN_93, 
    QM_GPIO_PIN_94, 
    QM_GPIO_PIN_95,
    QM_GPIO_PIN_96, 
    QM_GPIO_PIN_97, 
    QM_GPIO_PIN_98, 
    QM_GPIO_PIN_99, 
    QM_GPIO_PIN_100, 
    QM_GPIO_PIN_101, 
    QM_GPIO_PIN_102, 
    QM_GPIO_PIN_103, 
    QM_GPIO_PIN_104, 
    QM_GPIO_PIN_105, 
    QM_GPIO_PIN_106, 
    QM_GPIO_PIN_107, 
    QM_GPIO_PIN_108, 
    QM_GPIO_PIN_109, 
    QM_GPIO_PIN_110, 
    QM_GPIO_PIN_111,  
    QM_GPIO_PIN_MAX,
}qm_gpio_pin_t;

typedef enum{
    QM_GPIOA = 0,
    QM_GPIOB = 1,
    QM_GPIOC = 2,
    QM_GPIOD = 3,
    QM_GPIOE = 4,
    QM_GPIOF = 5,
    QM_GPIOG = 6,
}qm_gpio_typedef_t;


typedef enum
{
    APBPeriph_TYPE_1,
    APBPeriph_TYPE_2,
    APBPeriph_TYPE_MAX,
}qm_apbperiph_t;


#endif
