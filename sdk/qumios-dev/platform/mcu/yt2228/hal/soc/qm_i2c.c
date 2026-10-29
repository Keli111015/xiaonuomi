#include "qm_i2c.h"
#include "qm_errno.h"
#include "qm_kernel.h"

#include "iic_soft.h"

int32_t qm_i2c_init(qm_i2c_dev_t *i2c)
{
    if(i2c->port != 0){
        return -QM_ERROR;
    }
    
    return soft_iic_init(i2c->port);
}

int32_t qm_i2c_master_send(qm_i2c_dev_t *i2c, uint16_t dev_addr, const uint8_t *data,
                            uint16_t size, uint32_t timeout)
{
    if(i2c->port != 0){
        return -QM_EINVAL;
    }

    soft_iic_start(i2c->port);
    soft_iic_tx_byte(i2c->port, (uint8_t)dev_addr);
    soft_iic_write_buf(i2c->port, data, size);
    soft_iic_stop(i2c->port);

    return QM_EOK;

}

int32_t qm_i2c_master_recv(qm_i2c_dev_t *i2c, uint16_t dev_addr, uint8_t *data,
                            uint16_t size, uint32_t timeout)
{
    if(i2c->port != 0){
        return -QM_EINVAL;
    }

    soft_iic_start(i2c->port);
    soft_iic_tx_byte(i2c->port, (uint8_t)dev_addr);
    soft_iic_read_buf(i2c->port, data, size);
    soft_iic_stop(i2c->port);

    return QM_EOK;
}

int32_t qm_i2c_deinit(qm_i2c_dev_t *i2c)
{

    if(i2c == NULL){
        return -QM_EINVAL;
    }

    soft_iic_uninit(i2c->port);
    return QM_EOK;

}
