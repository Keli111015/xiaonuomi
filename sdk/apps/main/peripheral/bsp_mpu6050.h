#ifndef BSP_MPU6050_H
#define BSP_MPU6050_H

#include "qm.h"
#include "mpu6050.h"

#ifdef __cplusplus
extern "C" {
#endif

qm_err_t bsp_mpu6050_init(void);
qm_err_t bsp_mpu6050_read(mpu6050_acceleration_t *accel,
                          mpu6050_rotation_t *gyro,
                          float *temp_c);

#ifdef __cplusplus
}
#endif

#endif
