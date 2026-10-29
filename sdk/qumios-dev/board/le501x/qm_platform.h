#ifndef __QM_PLATFORM_H__
#define __QM_PLATFORM_H__


/*            le501x GPIO 映射表
 *   ----------------------------------------------------------------
*  |  QM GPIO         |  le501x GPIO |
 *   ----------------------------------------------------------------
 *  |  QM_GPIO_PIN_0   |  GPIOA00    |
 *  |  QM_GPIO_PIN_1   |  GPIOA01    |
 *  |  QM_GPIO_PIN_2   |  GPIOA02    |
 *  |  QM_GPIO_PIN_3   |  GPIOA03    | 
 *  |  QM_GPIO_PIN_4   |  GPIOA04    |
 *  |  QM_GPIO_PIN_5   |  GPIOA05    | 
 *  |  QM_GPIO_PIN_6   |  GPIOA06    |
 *  |  QM_GPIO_PIN_7   |  GPIOA07    | 
 *  |  QM_GPIO_PIN_8   |  GPIOA08    |
 *  |  QM_GPIO_PIN_9   |  GPIOA09    | 
 *  |  QM_GPIO_PIN_10  |  GPIOA10    |
 *  |  QM_GPIO_PIN_11  |  GPIOA11    | 
 *  |  QM_GPIO_PIN_12  |  GPIOA12    |
 *  |  QM_GPIO_PIN_13  |  GPIOA13    |  
 *  |  QM_GPIO_PIN_14  |  GPIOA14    |
 *  |  QM_GPIO_PIN_15  |  GPIOA15    | 
 *  |  QM_GPIO_PIN_16  |  GPIOB00    |
 *  |  QM_GPIO_PIN_17  |  GPIOB01    |  
 *  |  QM_GPIO_PIN_18  |  GPIOB02    |
 *  |  QM_GPIO_PIN_19  |  GPIOB03    | 
 *  |  QM_GPIO_PIN_20  |  GPIOB04    |
 *  |  QM_GPIO_PIN_21  |  GPIOB05    | 
 *  |  QM_GPIO_PIN_22  |  GPIOB06    |
 *  |  QM_GPIO_PIN_23  |  GPIOB07    | 
 *  |  QM_GPIO_PIN_24  |  GPIOB08    |
 *  |  QM_GPIO_PIN_25  |  GPIOB09    | 
 *  |  QM_GPIO_PIN_26  |  GPIOB10    |
 *  |  QM_GPIO_PIN_27  |  GPIOB11    | 
 *  |  QM_GPIO_PIN_28  |  GPIOB12    |
 *  |  QM_GPIO_PIN_29  |  GPIOB13    | 
 *  |  QM_GPIO_PIN_30  |  GPIOB14    | 
 *  |  QM_GPIO_PIN_31  |  GPIOB15    | 
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
    QM_GPIO_PIN_MAX,
}qm_platform_gpio_pin_t;

#endif
