#include "qm_platform.h"

#define LOG_TAG "platform"

static const qm_platform_gpio_t  qm_platform_gpio_pins[] = 
    {
        [QM_GPIO_PIN_1] =   {GPIOC, GPIO_PIN_13},
        [QM_GPIO_PIN_2] =   {GPIOC, GPIO_PIN_14},
        [QM_GPIO_PIN_3] =   {GPIOC, GPIO_PIN_15},
        [QM_GPIO_PIN_8] =   {GPIOF, GPIO_PIN_0},
        [QM_GPIO_PIN_9] =   {GPIOF, GPIO_PIN_1},
        [QM_GPIO_PIN_10] =  {GPIOF, GPIO_PIN_2},
        [QM_GPIO_PIN_11] =  {GPIOA, GPIO_PIN_0},
        [QM_GPIO_PIN_12] =  {GPIOA, GPIO_PIN_1},
        [QM_GPIO_PIN_13] =  {GPIOA, GPIO_PIN_2},
        [QM_GPIO_PIN_14] =  {GPIOA, GPIO_PIN_3},
        [QM_GPIO_PIN_15] =  {GPIOA, GPIO_PIN_4},
        [QM_GPIO_PIN_16] =  {GPIOA, GPIO_PIN_5},
        [QM_GPIO_PIN_17] =  {GPIOA, GPIO_PIN_6},
        [QM_GPIO_PIN_18] =  {GPIOA, GPIO_PIN_7},
        [QM_GPIO_PIN_19] =  {GPIOB, GPIO_PIN_0},
        [QM_GPIO_PIN_20] =  {GPIOB, GPIO_PIN_1},
        [QM_GPIO_PIN_21] =  {GPIOB, GPIO_PIN_2},
        [QM_GPIO_PIN_22] =  {GPIOB, GPIO_PIN_10},
        [QM_GPIO_PIN_23] =  {GPIOB, GPIO_PIN_11},
        [QM_GPIO_PIN_24] =  {GPIOB, GPIO_PIN_12},
        [QM_GPIO_PIN_25] =  {GPIOB, GPIO_PIN_13},
        [QM_GPIO_PIN_26] =  {GPIOB, GPIO_PIN_14},
        [QM_GPIO_PIN_27] =  {GPIOB, GPIO_PIN_15},
        [QM_GPIO_PIN_28] =  {GPIOA, GPIO_PIN_8},
        [QM_GPIO_PIN_29] =  {GPIOA, GPIO_PIN_9},
        [QM_GPIO_PIN_30] =  {GPIOC, GPIO_PIN_6},
        [QM_GPIO_PIN_31] =  {GPIOC, GPIO_PIN_7},
        [QM_GPIO_PIN_32] =  {GPIOA, GPIO_PIN_10},
        [QM_GPIO_PIN_33] =  {GPIOA, GPIO_PIN_11},
        [QM_GPIO_PIN_34] =  {GPIOA, GPIO_PIN_12},
        [QM_GPIO_PIN_35] =  {GPIOA, GPIO_PIN_13},
        [QM_GPIO_PIN_36] =  {GPIOA, GPIO_PIN_14},
        [QM_GPIO_PIN_37] =  {GPIOD, GPIO_PIN_15},
        [QM_GPIO_PIN_38] =  {GPIOD, GPIO_PIN_0},
        [QM_GPIO_PIN_39] =  {GPIOD, GPIO_PIN_1},
        [QM_GPIO_PIN_40] =  {GPIOD, GPIO_PIN_2},
        [QM_GPIO_PIN_41] =  {GPIOD, GPIO_PIN_3},
        [QM_GPIO_PIN_42] =  {GPIOB, GPIO_PIN_3},
        [QM_GPIO_PIN_43] =  {GPIOB, GPIO_PIN_4},
        [QM_GPIO_PIN_44] =  {GPIOB, GPIO_PIN_5},
        [QM_GPIO_PIN_45] =  {GPIOB, GPIO_PIN_6},
        [QM_GPIO_PIN_46] =  {GPIOB, GPIO_PIN_7},
        [QM_GPIO_PIN_47] =  {GPIOB, GPIO_PIN_8},
        [QM_GPIO_PIN_48] =  {GPIOB, GPIO_PIN_9},
    };


const qm_platform_gpio_t *qm_platform_gpio_get_port(qm_gpio_pin_t qm_pins)
{
    QM_RETURN_ON_FALSE(qm_pins > QM_GPIO_PIN_NONE && qm_pins < QM_GPIO_PIN_MAX, NULL, LOG_TAG, "gpio invaled");

    return &qm_platform_gpio_pins[qm_pins];
}

int qm_platform_gpio_enable_clock(qm_gpio_pin_t qm_pins)
{
    int ret = QM_EOK;
    const qm_platform_gpio_t *gpio = qm_platform_gpio_get_port(qm_pins);
    
    QM_RETURN_ON_FALSE(gpio, -QM_EINVAL, LOG_TAG, "gpio invaled");
    QM_RETURN_ON_FALSE(gpio->port, -QM_EINVAL, LOG_TAG, "gpio port invaled");
		
    switch((uint32_t)gpio->port)
    {
        case GPIOA_BASE:
            __HAL_RCC_GPIOA_CLK_ENABLE();
        break;
        case GPIOB_BASE:
            __HAL_RCC_GPIOB_CLK_ENABLE();
        break;
        case GPIOC_BASE:
            __HAL_RCC_GPIOC_CLK_ENABLE();
        break;
        case GPIOD_BASE:
            __HAL_RCC_GPIOD_CLK_ENABLE();
        break;
        case GPIOF_BASE:
            __HAL_RCC_GPIOF_CLK_ENABLE();
        break;
        default:
            ret = -QM_EIO;
        break;
    }
    return ret;
}

int qm_platform_gpio_set_alternate_function(qm_gpio_pin_t qm_pins,
                                            qm_gpio_mode_t output_type, 
                                            qm_gpio_pull_mode_t pull_up_down_type, 
                                            uint8_t alternation_function)
{
    int ret = QM_EOK;
    GPIO_InitTypeDef  gpio_init_structure = {0};
    const qm_platform_gpio_t *gpio = qm_platform_gpio_get_port(qm_pins);

    QM_RETURN_ON_FALSE(gpio, -QM_EINVAL, LOG_TAG, "gpio invaled");
    QM_RETURN_ON_FALSE(gpio->port, -QM_EINVAL, LOG_TAG, "gpio invaled");

    qm_platform_gpio_enable_clock(qm_pins);

    gpio_init_structure.Pin = gpio->pin_number;
    gpio_init_structure.Alternate = alternation_function;
    gpio_init_structure.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    switch (pull_up_down_type)
    {
        case QM_GPIO_PULLUP_ONLY:
            gpio_init_structure.Pull = GPIO_PULLUP;
        break;
        case QM_GPIO_PULLDOWN_ONLY:
            gpio_init_structure.Pull = GPIO_PULLDOWN;
        break;       
        case QM_GPIO_FLOATING:
            gpio_init_structure.Pull = GPIO_NOPULL;
        break;       
        default:
            ret = -QM_EIO;
        break;
    }
    
    switch (output_type)
    {
        case QM_GPIO_MODE_OUTPUT_OD:
        case QM_GPIO_MODE_INPUT_OUTPUT_OD:
            gpio_init_structure.Mode = GPIO_MODE_AF_OD;
        break;
        case QM_GPIO_MODE_INPUT:
        case QM_GPIO_MODE_OUTPUT:
        case QM_GPIO_MODE_INPUT_OUTPUT:
            gpio_init_structure.Mode = GPIO_MODE_AF_PP;
        break;
        default :
            ret = -QM_EIO;
        break;
    }

    HAL_GPIO_Init(gpio->port, &gpio_init_structure);

    return ret;
}
