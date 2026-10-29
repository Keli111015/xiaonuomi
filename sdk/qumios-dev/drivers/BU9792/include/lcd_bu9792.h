#ifndef _LCD_BU9792_H
#define _LCD_BU9792_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file lcd_bu9792.h
 * @brief BU9792 LCD driver header file.
*/
#include "qm.h"
#include "qm_gpio.h"
#include "qm_errno.h"
#include "qm_soft_i2c.h"

enum
{
    BU_LOW_POWER_LEVEL_1,
    BU_LOW_POWER_LEVEL_2,
    BU_LOW_POWER_LEVEL_3,
    BU_LOW_POWER_LEVEL_4,
}; // 1最低功耗，支持四级设置
/**
 *  init time series
 *  powon -> 100us -> stop -> start -> slave_addr -> ICSET -> DISCTL -> ADSET -> Displaydata -> stop
 * 
 * display on time series
 *  start -> slave_addr -> DISCTL -> BLKCTL -> APCTL -> MODESET -> stop
 * 
 * write ram time series
 *  start -> slave_addr -> DISCTL -> BLKCTL -> APCTL -> MODESET -> ADSET -> Displaydata -> stop
 * 
 * display off time series
 *  start -> slave_addr -> MODESET -> stop
 */
qm_err_t qm_bu9792_mode_set(uint8_t display_onoff, uint8_t bias);
qm_err_t qm_bu9792_addr_set(uint8_t addr);
qm_err_t qm_bu9792_rst_rc_set(uint8_t rst, uint8_t rc);
qm_err_t qm_bu9792_disctl(uint8_t fr_lv, uint8_t fm, uint8_t sr_lv);
qm_err_t qm_bu9792_blkctl(uint8_t mode);

qm_err_t qm_bu9792_test_full(qm_soft_i2c_t *i2c, uint8_t onoff);
qm_err_t qm_bu9792_init(qm_soft_i2c_t *i2c);


#ifdef __cplusplus
}
#endif

#endif // end of _LCD_BU9792_H
