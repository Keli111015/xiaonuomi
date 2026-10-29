#ifndef QM_I2C_H
#define QM_I2C_H

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup qm_i2c I2C
 *  qm i2c API.
 *
 *  @{
 */

#include "qm_types.h"


#define QM_I2C_MODE_MASTER 1 /**< i2c communication is master mode */
#define QM_I2C_MODE_SLAVE  2 /**< i2c communication is slave mode */

/*
 * Specifies one of the standard I2C bus bit rates for I2C communication
 */
#define QM_I2C_BUS_BIT_RATES_100K  100000
#define QM_I2C_BUS_BIT_RATES_400K  400000
#define QM_I2C_BUS_BIT_RATES_3400K 3400000

/* Addressing mode */
#define QM_I2C_ADDRESS_WIDTH_7BIT  0   /**< 7 bit mode */
#define QM_I2C_ADDRESS_WIDTH_10BIT 1   /**< 10 bit mode */

/* This struct define i2c config args */
typedef struct {
    uint32_t address_width; /**< Addressing mode: 7 bit or 10 bit */
    uint32_t freq;          /**< CLK freq */
    uint8_t  mode;          /**< master or slave mode */
    uint16_t dev_addr;      /**< slave device addr */
} qm_i2c_config_t;

/**
 * @brief i2c pins
 */
typedef struct {
    uint8_t sda_pin;
    uint8_t sck_pin;
} qm_i2c_pins_t;

/* This struct define i2c main handle */
typedef struct {
    uint8_t          port;   /**< i2c port */
    qm_i2c_config_t  config; /**< i2c config */
    qm_i2c_pins_t    pins;    /**< i2c pins*/
    void             *priv;   /**< priv data */
} qm_i2c_dev_t;

/**
 * Initialises an I2C interface
 * Prepares an I2C hardware interface for communication as a master or slave
 *
 * @param[in]  i2c  the device for which the i2c port should be initialised
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_i2c_init(qm_i2c_dev_t *i2c);

/**
 * I2c master send
 *
 * @param[in]  i2c       the i2c device
 * @param[in]  dev_addr  device address
 * @param[in]  data      i2c send data
 * @param[in]  size      i2c send data size
 * @param[in]  timeout   timeout in milisecond, set this value to HAL_WAIT_FOREVER
 *                       if you want to wait forever
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_i2c_master_write(qm_i2c_dev_t *i2c, uint16_t dev_addr, const uint8_t *data,
                            uint16_t size, uint32_t timeout);

/**
 * I2c master recv
 *
 * @param[in]   i2c       the i2c device
 * @param[in]   dev_addr  device address
 * @param[out]  data      i2c receive data
 * @param[in]   size      i2c receive data size
 * @param[in]   timeout   timeout in milisecond, set this value to HAL_WAIT_FOREVER
 *                        if you want to wait forever
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_i2c_master_write_read(qm_i2c_dev_t *i2c, uint16_t dev_addr, 
                                const uint8_t* write_buffer, size_t write_size,
                                uint8_t* read_buffer, size_t read_size, uint32_t timeout);

/**
 * I2c slave send
 *
 * @param[in]  i2c      the i2c device
 * @param[in]  data     i2c slave send data
 * @param[in]  size     i2c slave send data size
 * @param[in]  timeout  timeout in milisecond, set this value to HAL_WAIT_FOREVER
 *                      if you want to wait forever
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_i2c_slave_write(qm_i2c_dev_t *i2c, const uint8_t *data, uint16_t size, uint32_t timeout);

/**
 * I2c slave receive
 *
 * @param[in]   i2c      tthe i2c device
 * @param[out]  data     i2c slave receive data
 * @param[in]   size     i2c slave receive data size
 * @param[in]  timeout   timeout in milisecond, set this value to HAL_WAIT_FOREVER
 *                       if you want to wait forever
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_i2c_slave_read(qm_i2c_dev_t *i2c, uint8_t *data, uint16_t size, uint32_t timeout);

/**
 * Deinitialises an I2C device
 *
 * @param[in]  i2c  the i2c device
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_i2c_deinit(qm_i2c_dev_t *i2c);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* QM_I2C_H */
