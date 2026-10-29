#include "bsp_mpu6050.h"

#include "qm_log.h"
#include "qm_kernel.h"

#include <i2cdev.h>
#include <driver/i2c_types.h>
#include <driver/gpio.h>

#define LOG_TAG "mpu6050"

#define BSP_MPU6050_I2C_PORT  I2C_NUM_0
#define BSP_MPU6050_SDA_PIN   GPIO_NUM_8
#define BSP_MPU6050_SCL_PIN   GPIO_NUM_9
#define BSP_MPU6050_ADDR      MPU6050_I2C_ADDRESS_LOW
#define BSP_MPU6050_PROBE_MS  200
#define BSP_MPU6050_PROBE_TRY 10
#define BSP_MPU6050_LOG_MS    1000
#define BSP_MPU6050_STACK     4096
#define BSP_MPU6050_PRIO      4

static mpu6050_dev_t s_dev;
static uint8_t s_ready;

static int wait_device(void)
{
    int i;

    for (i = 0; i < BSP_MPU6050_PROBE_TRY; i++) {
        if (i2c_dev_probe(&s_dev.i2c_dev, I2C_DEV_WRITE) == ESP_OK) {
            return 0;
        }
        QM_LOGD(LOG_TAG, "probe %d/%d fail, SDA=%d SCL=%d addr=0x%02x",
                i + 1, BSP_MPU6050_PROBE_TRY,
                (int)BSP_MPU6050_SDA_PIN, (int)BSP_MPU6050_SCL_PIN,
                (unsigned)BSP_MPU6050_ADDR);
        qm_msleep(BSP_MPU6050_PROBE_MS);
    }
    return -1;
}

static void mpu_log_task(void *arg)
{
    (void)arg;
    while (1) {
        mpu6050_acceleration_t accel = {0};
        mpu6050_rotation_t gyro = {0};
        float temp = 0;

        if (bsp_mpu6050_read(&accel, &gyro, &temp) == QM_EOK) {
            QM_LOGI(LOG_TAG, "acc %.3f %.3f %.3f g  gyro %.2f %.2f %.2f dps  t %.1f C",
                    accel.x, accel.y, accel.z,
                    gyro.x, gyro.y, gyro.z, temp);
        }
        qm_msleep(BSP_MPU6050_LOG_MS);
    }
}

qm_err_t bsp_mpu6050_read(mpu6050_acceleration_t *accel,
                          mpu6050_rotation_t *gyro,
                          float *temp_c)
{
    if (!s_ready) {
        return -QM_EIO;
    }
    if (accel && gyro && mpu6050_get_motion(&s_dev, accel, gyro) != ESP_OK) {
        return -QM_EIO;
    }
    if (temp_c && mpu6050_get_temperature(&s_dev, temp_c) != ESP_OK) {
        return -QM_EIO;
    }
    return QM_EOK;
}

qm_err_t bsp_mpu6050_init(void)
{
    static qm_task_t task;

    if (i2cdev_init() != ESP_OK) {
        QM_LOGE(LOG_TAG, "i2cdev_init fail");
        return -QM_EIO;
    }
    if (mpu6050_init_desc(&s_dev, BSP_MPU6050_ADDR, BSP_MPU6050_I2C_PORT,
                          BSP_MPU6050_SDA_PIN, BSP_MPU6050_SCL_PIN) != ESP_OK) {
        QM_LOGE(LOG_TAG, "init_desc fail");
        return -QM_EIO;
    }
    if (wait_device() != 0) {
        QM_LOGE(LOG_TAG, "not found on I2C0 SDA=%d SCL=%d addr=0x%02x",
                (int)BSP_MPU6050_SDA_PIN, (int)BSP_MPU6050_SCL_PIN,
                (unsigned)BSP_MPU6050_ADDR);
        (void)mpu6050_free_desc(&s_dev);
        return -QM_EIO;
    }
    if (mpu6050_init(&s_dev) != ESP_OK) {
        QM_LOGE(LOG_TAG, "mpu6050_init fail");
        (void)mpu6050_free_desc(&s_dev);
        return -QM_EIO;
    }
    s_ready = 1;
    QM_LOGI(LOG_TAG, "ready accel_range=%d gyro_range=%d",
            (int)s_dev.ranges.accel, (int)s_dev.ranges.gyro);

    if (qm_task_new(&task, "mpu6050", mpu_log_task, NULL,
                    BSP_MPU6050_STACK, BSP_MPU6050_PRIO) != 0) {
        QM_LOGD(LOG_TAG, "log task create fail");
    }
    return QM_EOK;
}
