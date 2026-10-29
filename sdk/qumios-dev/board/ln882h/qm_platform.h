#ifndef __QM__BL602PLATFORM_H__
#define __QM__BL602PLATFORM_H__


/*            BL602 GPIO 映射表
 *   ----------------------------------------------------------------
*  |  QM GPIO         |  BL602 GPIO |
 *   ----------------------------------------------------------------
 *  |  QM_GPIO_PIN_0   |  GPIO_0     |
 *  |  QM_GPIO_PIN_1   |  GPIO_1     |
 *  |  QM_GPIO_PIN_2   |  GPIO_2     |
 *  |  QM_GPIO_PIN_3   |  GPIO_3     | 
 *  |  QM_GPIO_PIN_4   |  GPIO_4     |
 *  |  QM_GPIO_PIN_5   |  GPIO_5     | 
 *  |  QM_GPIO_PIN_6   |  GPIO_6     |
 *  |  QM_GPIO_PIN_7   |  GPIO_7     | 
 *  |  QM_GPIO_PIN_8   |  GPIO_8     |
 *  |  QM_GPIO_PIN_11  |  GPIO_11    | 
 *  |  QM_GPIO_PIN_12  |  GPIO_12    |
 *  |  QM_GPIO_PIN_14  |  GPIO_14    | 
 *  |  QM_GPIO_PIN_16  |  GPIO_16    |
 *  |  QM_GPIO_PIN_17  |  GPIO_17    |  
 *  |  QM_GPIO_PIN_20  |  GPIO_20    |
 *  |  QM_GPIO_PIN_21  |  GPIO_21    | 
 *  |  QM_GPIO_PIN_22  |  GPIO_22    |
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
    QM_GPIO_PIN_11 = 11,
    QM_GPIO_PIN_12,
    QM_GPIO_PIN_14 = 14,
    QM_GPIO_PIN_16 = 16,
    QM_GPIO_PIN_17,
    QM_GPIO_PIN_20 = 20,
    QM_GPIO_PIN_21,
    QM_GPIO_PIN_22,
} qm_gpio_pin_t;


#endif

