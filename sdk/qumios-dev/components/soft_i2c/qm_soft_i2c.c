#include "qm_errno.h"
#include "qm_log.h"
#include "qm_kernel.h"

#include "qm_soft_i2c.h"

#define LOG_TAG "qm_soft_i2c"

#ifdef CONFIG_SOFT_IIC_SUPPORT

/*****************************************************************************/
static void soft_i2c_start(qm_soft_i2c_t *i2c)
{
    // start condition: SCL high, SDA goes from high to low
    i2c->sda_pin_dir(1); // Set SDA as output
    i2c->scl_pin_set(1); // Set SCL high
    i2c->sda_pin_set(1); // Set SDA high
    i2c->delay();        // Delay for setup time
    i2c->sda_pin_set(0); // Set SDA low to start condition
    i2c->delay();        // Delay for hold time
    i2c->scl_pin_set(0); // Set SCL low
    i2c->delay();        // Delay
}
static void soft_i2c_stop(qm_soft_i2c_t *i2c)
{
    // stop condition: SCL high, SDA goes from low to high
    i2c->sda_pin_dir(1); // Set SDA as output
    i2c->scl_pin_set(0); // Set SCL low
    i2c->sda_pin_set(0); // Set SDA low
    i2c->delay();
    i2c->scl_pin_set(1); // Set SCL high
    i2c->delay();
    i2c->sda_pin_set(1); // Set SDA high to complete stop condition
    i2c->delay();        // Delay for hold time after stop condition
}
void qm_soft_i2c_start(qm_soft_i2c_t *i2c)
{
    soft_i2c_start(i2c);
}
void qm_soft_i2c_stop(qm_soft_i2c_t *i2c)
{
    soft_i2c_stop(i2c);
}
static uint8_t soft_i2c_send_byte_wait_nack(qm_soft_i2c_t *i2c, uint8_t data)
{
    uint8_t i = 8;
    i2c->sda_pin_dir(1);
    while (i--)
    {
        i2c->scl_pin_set(0); // Set SCL low
        i2c->delay();
        i2c->sda_pin_set((data >> 7) & 0x01);
        i2c->delay();
        i2c->scl_pin_set(1); // Set SCL low
        i2c->delay();
        data <<= 1;
        i2c->delay();
    }
    i2c->scl_pin_set(0); // Set SCL low
    i2c->delay();
    uint8_t wait_time = i2c->read_wait_time;
    // Set SDA as input to read ACK
    i2c->sda_pin_dir(0);
    i2c->scl_pin_set(1); // Set SCL high to read ACK
    i2c->delay();        // Delay for setup time
    while (wait_time--)
    {
        if (!i2c->sda_pin_get())
        {
            i2c->scl_pin_set(0);
            i2c->delay(); // Delay
            return 0;
        }
    }
    i2c->scl_pin_set(0);
    i2c->delay(); // Delay
    return 1;
}
static void soft_i2c_send_ack(qm_soft_i2c_t *i2c, uint8_t ack)
{
    i2c->sda_pin_dir(1);
    i2c->sda_pin_set(0);
    i2c->scl_pin_set(0);
    i2c->delay(); // Delay
    i2c->sda_pin_set(ack);
    i2c->delay(); // Delay
    i2c->scl_pin_set(1);
    i2c->delay(); // Delay
    i2c->scl_pin_set(0);
    i2c->delay(); // Delay
}
static uint8_t soft_i2c_recv_byte(qm_soft_i2c_t *i2c)
{
    unsigned char i = 8;
    unsigned char byte;

    i2c->sda_pin_dir(0);
    i2c->scl_pin_set(0);
    i2c->delay(); // Delay
    while (i--)
    {
        i2c->scl_pin_set(1);
        byte = (byte << 1) | i2c->sda_pin_get();
        i2c->delay(); // Delay
        i2c->scl_pin_set(0);
        i2c->delay(); // Delay
    }
    return byte;
}
/*****************************************************************************/
qm_err_t qm_soft_i2c_init(qm_soft_i2c_t *i2c)
{
    // check parameters
    if (i2c == NULL || i2c->scl_pin_set == NULL || i2c->sda_pin_set == NULL ||
        i2c->sda_pin_get == NULL || i2c->sda_pin_dir == NULL || i2c->delay == NULL)
    {
        QM_LOGE(LOG_TAG, "Invalid parameters for soft I2C initialization");
        return QM_EINVAL;
    }
    return QM_EOK;
}
/*****************************************************************************/
qm_err_t qm_soft_i2c_write(qm_soft_i2c_t *i2c, uint8_t *data, uint8_t size)
{
    soft_i2c_start(i2c);
    if (soft_i2c_send_byte_wait_nack(i2c, (i2c->slave_addr << 1) & ~0x01))
    {
        soft_i2c_stop(i2c);
        return QM_ERROR;
    }

    while (size--)
    {
        if (soft_i2c_send_byte_wait_nack(i2c, *data++))
        {
            soft_i2c_stop(i2c);
            return QM_ERROR;
        }
    }

    soft_i2c_stop(i2c);
    return QM_EOK;
}

qm_err_t qm_soft_i2c_read(qm_soft_i2c_t *i2c, uint8_t *data, uint8_t size)
{
    soft_i2c_start(i2c);
    if (soft_i2c_send_byte_wait_nack(i2c, (i2c->slave_addr << 1) | 0x01))
    {
        soft_i2c_stop(i2c);
        return QM_ERROR;
    }

    while (size--)
    {
        *data++ = soft_i2c_recv_byte(i2c);
        if (size)
        {
            soft_i2c_send_ack(i2c, 0);
        }
        else
        {
            soft_i2c_send_ack(i2c, 1);
        }
    }
    soft_i2c_stop(i2c);
    return QM_EOK;
}

#endif // CONFIG_SOFT_IIC_SUPPORT
