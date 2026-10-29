#ifndef _QM_SOFT_I2C_H
#define _QM_SOFT_I2C_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"

/**
 *   简化i2c驱动
*/

typedef struct
{
    uint8_t slave_addr; // 7-bit slave address
    uint16_t read_wait_time;
	void (*scl_pin_set)(uint8_t state);
	void (*sda_pin_set)(uint8_t state);
	uint8_t (*sda_pin_get)(void);
	void (*sda_pin_dir)(uint8_t dir); //1 output 0 input
	void (*delay)(void);
} qm_soft_i2c_t;

qm_err_t qm_soft_i2c_init(qm_soft_i2c_t *i2c);
qm_err_t qm_soft_i2c_write(qm_soft_i2c_t *i2c, uint8_t *data, uint8_t size);
qm_err_t qm_soft_i2c_read(qm_soft_i2c_t *i2c, uint8_t *data, uint8_t size);

// 有些芯片需要先stop
void qm_soft_i2c_start(qm_soft_i2c_t *i2c);
void qm_soft_i2c_stop(qm_soft_i2c_t *i2c);


#ifdef __cplusplus
}
#endif

#endif // end of _QM_SOFT_I2C_H
