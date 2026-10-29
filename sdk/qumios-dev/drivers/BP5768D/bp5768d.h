#ifndef _BP5768D_H_
#define _BP5768D_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"
#include "qm_errno.h"

//每个通道1024级灰度调节
#define BP5768D_MAX_GRADE   (1023)

#ifndef BP5768D_ADDR_BIT_TYPE   
#define BP5768D_ADDR_BIT_TYPE   (7)
#endif
/**
 * @brief Output configuration
 *
 */
typedef struct {
    uint8_t  port;
    uint16_t iic_clk;
    uint16_t iic_sda;
    uint16_t freq_khz;             
    uint8_t current[5];
#if 0
    bool_t enable_iic_queue;
#endif
}qm_driver_bp5768d_t;

/**
 * @brief BP5768D channel abstract definition
 *
 */
typedef enum {
    QM_BP5768D_CHANNEL_R = 0,
    QM_BP5768D_CHANNEL_G,
    QM_BP5768D_CHANNEL_B,
    QM_BP5768D_CHANNEL_C,
    QM_BP5768D_CHANNEL_W,
    QM_BP5768D_CHANNEL_MAX,
}qm_bp5768d_channel_t;

/**
 * @brief BP5768D output pin definition
 *
 */
typedef enum {
    QM_BP5768D_PIN_OUT1 = 0,
    QM_BP5768D_PIN_OUT2,
    QM_BP5768D_PIN_OUT3,
    QM_BP5768D_PIN_OUT4,
    QM_BP5768D_PIN_OUT5,
    QM_BP5768D_PIN_OUT_MAX,
}qm_bp5758d_out_pin_t;

/**
 * @brief Initialize BP5768D output
 *
 * @param config Driver configuration
 * @return qm_err_t
 */
qm_err_t qm_bp5768d_init(qm_driver_bp5768d_t *config);

/**
 * @brief Register the BP5768D channel
 * @note Needs to correspond to the real hardware
 *
 * @param channel Abstract channel
 * @param pin Chip pin
 * @return qm_err_t
 */
qm_err_t qm_bp5768d_regist_channel(qm_bp5768d_channel_t channel, qm_bp5758d_out_pin_t pin);

/**
 * @brief Set any channel output
 *
 * @param channel Abstract pin
 * @param value Output value
 * @return qm_err_t
 */
qm_err_t qm_bp5768d_set_channel(qm_bp5768d_channel_t channel, uint16_t value);

/**
 * @brief Set only rgb channel output
 *
 * @param value_r Output red value
 * @param value_g Output green value
 * @param value_b Output blue value
 * @return qm_err_t
 */
qm_err_t qm_bp5768d_set_rgb_channel(uint16_t value_r, uint16_t value_g, uint16_t value_b);

/**
 * @brief Set only cw channel output
 *
 * @param value_c Output cold value
 * @param value_w Output white value
 * @return qm_err_t
 */
qm_err_t qm_bp5768d_set_cw_channel(uint16_t value_c, uint16_t value_w);

/**
 * @brief Set all channel output
 *
 * @param value_r Output red value
 * @param value_g Output green value
 * @param value_b Output blue value
 * @param value_w Output cold value
 * @param value_y Output white value
 * @return qm_err_t
 */
qm_err_t qm_bp5768d_set_rgbcw_channel(uint16_t value_r, uint16_t value_g, uint16_t value_b, uint16_t value_c, uint16_t value_w);

/**
 * @brief Stop all channel output
 *
 * @return qm_err_t
 */
qm_err_t qm_bp5768d_set_shutdown(void);

/**
 * @brief Set standby mode
 *
 * @param enable_sleep If set to true will enter standby mode
 * @return qm_err_t
 */
qm_err_t qm_bp5768d_set_standby_mode(bool_t enable_sleep);

/**
 * @brief Deinitialize bp5758d and release resources
 *
 * @return qm_err_t
 */
qm_err_t qm_bp5768d_deinit(void);

#ifdef __cplusplus
}
#endif

#endif