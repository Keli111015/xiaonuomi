#ifndef QM_GPIO_H
#define QM_GPIO_H

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup qm_gpio GPIO
 *  qm gpio API.
 *
 *  @{
 */

#include "qm_types.h"


/*
 * Pin interrupt configuration
 */
typedef enum {
    QM_GPIO_INTR_DISABLE = 0,     /*!< Disable GPIO interrupt                             */
    QM_GPIO_INTR_POSEDGE = 1,     /*!< GPIO interrupt type : rising edge                  */
    QM_GPIO_INTR_NEGEDGE = 2,     /*!< GPIO interrupt type : falling edge                 */
    QM_GPIO_INTR_ANYEDGE = 3,     /*!< GPIO interrupt type : both rising and falling edge */
    QM_GPIO_INTR_LOW_LEVEL = 4,   /*!< GPIO interrupt type : input low level trigger      */
    QM_GPIO_INTR_HIGH_LEVEL = 5,  /*!< GPIO interrupt type : input high level trigger     */
    QM_GPIO_INTR_MAX,
} qm_gpio_intr_type_t;


typedef enum {
    QM_GPIO_MODE_DISABLE = 0,                    /*!< GPIO mode : disable input and output             */
    QM_GPIO_MODE_INPUT = 1,                      /*!< GPIO mode : input only                           */
    QM_GPIO_MODE_OUTPUT = 2,                     /*!< GPIO mode : output only mode                     */
    QM_GPIO_MODE_OUTPUT_OD = 3,                  /*!< GPIO mode : output only with open-drain mode     */
    QM_GPIO_MODE_INPUT_OUTPUT_OD = 4,            /*!< GPIO mode : output and input with open-drain mode*/
    QM_GPIO_MODE_INPUT_OUTPUT = 5,               /*!< GPIO mode : output and input mode                */
} qm_gpio_mode_t;


typedef enum {
    QM_GPIO_PULLUP_ONLY,               /*!< Pad pull up            */
    QM_GPIO_PULLDOWN_ONLY,             /*!< Pad pull down          */
    QM_GPIO_PULLUP_PULLDOWN,           /*!< Pad pull up + pull down*/
    QM_GPIO_FLOATING,                  /*!< Pad floating           */
} qm_gpio_pull_mode_t;

/*
 * Pin configuration
 */
typedef struct {
    qm_gpio_mode_t mode;                  /*!< GPIO mode: set input/output mode                  */
    qm_gpio_pull_mode_t pull_en;          /*!< GPIO pull nmode                                   */        
} qm_gpio_config_t;

/*
 * GPIO dev struct
 */
typedef struct {
    uint8_t        port;   /**< gpio port */
    qm_gpio_config_t  config; /**< gpio config */
    void          *priv;   /**< priv data */
} qm_gpio_dev_t;

/*
 * GPIO interrupt callback handler
 */
typedef void (*qm_gpio_irq_handler_t)(void *arg);


/**
 * Initialises a GPIO pin
 *
 * @note  Prepares a GPIO pin for use.
 *
 * @param[in]  gpio the gpio pin which should be initialised
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_gpio_init(qm_gpio_dev_t *gpio);

/**
 * @brief  GPIO set output level
 *
 * @param  gpio  gpio dev struct
 * @param  level Output level. 0: low ; 1: high
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_gpio_set_level(qm_gpio_dev_t *gpio, uint8_t level);

/**
 * @brief  GPIO get input level
 *
 * @warning If the pad is not configured for input (or input and output) the returned value is always 0.
 *
 * @param  gpio dev struct
 *
 * @return
 *     - 0 the GPIO input level is 0
 *     - 1 the GPIO input level is 1
 *
 */
uint8_t qm_gpio_get_level(qm_gpio_dev_t *gpio);

/**
 * Enables an interrupt trigger for an input GPIO pin.
 * Using this function on a gpio pin which is set to
 * output mode is undefined.
 *
 * @param[in]  gpio     the gpio pin which will provide the interrupt trigger
 * @param[in]  intr_type  the type of trigger (rising/falling edge or both)
 * @param[in]  handler  a function pointer to the interrupt handler
 * @param[in]  arg      an argument that will be passed to the interrupt handler
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_gpio_enable_irq(qm_gpio_dev_t *gpio, qm_gpio_intr_type_t intr_type,
                            qm_gpio_irq_handler_t handler, void *arg);

/**
 * Disables an interrupt trigger for an input GPIO pin.
 * Using this function on a gpio pin which has not been setted up using
 * @ref qm_gpio_enable_irq is undefined.
 *
 * @param[in]  gpio  the gpio pin which provided the interrupt trigger
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_gpio_disable_irq(qm_gpio_dev_t *gpio);

/**
 * Clear an interrupt status for an input GPIO pin.
 * Using this function on a gpio pin which has generated a interrupt.
 *
 * @param[in]  gpio  the gpio pin which provided the interrupt trigger
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_gpio_clear_irq(qm_gpio_dev_t *gpio);

/**
 * Set a GPIO pin in default state.
 *
 * @param[in]  gpio  the gpio pin which should be deinitialised
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_gpio_deinit(qm_gpio_dev_t *gpio);


/** @} */

#ifdef __cplusplus
}
#endif

#endif /* QM_GPIO_H */

