#include "qm.h"
#include "qm_i2c.h"

#if CONFIG_QM_I2C_SUPPORT

#include <applet.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <nuttx/i2c/i2c_master.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include "qm_errno.h"
#include "qm_kernel.h"
#include "util.h"

typedef struct {
    int fd;
}_i2c_info_t;

int32_t qm_i2c_init(qm_i2c_dev_t *i2c)
{

    int ret;
    char dev_name[20];
    /*********************** 初始化iic ********************/

    _i2c_info_t *i2c_info_handle = NULL;

    if(i2c->port != 0){
        return -QM_ERROR;
    }

    i2c_info_handle = qm_malloc(sizeof(_i2c_info_t));
    if(i2c_info_handle == NULL){
        return -QM_ENOMEM;
    }
    memset(i2c_info_handle, 0, sizeof(_i2c_info_t));

    i2c_config_t config = {
        .id = i2c->port,
        .clk_freq = i2c->config.freq,
        .scl_pin = i2c->pins.sck_pin,
        .sda_pin= i2c->pins.sda_pin,
    };

    ret = boardctl(APP_BOARDIOC_I2C, &config);
    if(ret != QM_EOK){
        return -QM_ERROR;
    }

    sprintf(dev_name, "%s%d", "/dev/i2c", i2c->port);

    i2c_info_handle->fd = open(dev_name, O_RDWR);
    if (i2c_info_handle->fd < 0){
        return -QM_ERROR;
    }

    i2c->priv = (void *)i2c_info_handle;

    return QM_EOK;

}

int32_t qm_i2c_master_send(qm_i2c_dev_t *i2c, uint16_t dev_addr, const uint8_t *data,
                            uint16_t size, uint32_t timeout)
{
    int ret = QM_EOK;

    uint8_t *send_data = NULL;

    _i2c_info_t *i2c_info_handle = (_i2c_info_t *)i2c->priv;

    if(i2c_info_handle == NULL || i2c->port != 0){
        return -QM_EINVAL;
    }


    send_data = (uint8_t *)data;
    
    struct i2c_msg_s cmd = {
        .frequency = i2c->config.freq,
        .addr = dev_addr,
        .flags = 0x00,   //WRITE
        .buffer = send_data,
        .length = size,
    };

    ret = ioctl(i2c_info_handle->fd, I2CIOC_TRANSFER, &cmd);
    if (ret < 0) {
        return -QM_ERROR;
    }

    return QM_EOK;

}

int32_t qm_i2c_master_recv(qm_i2c_dev_t *i2c, uint16_t dev_addr, uint8_t *data,
                            uint16_t size, uint32_t timeout)
{
    int i,fd = 0,ret = 0;

    _i2c_info_t *i2c_info_handle = (_i2c_info_t *)i2c->priv;

    if(i2c_info_handle == NULL || i2c->port != 0){
        return -QM_EINVAL;
    }


    struct i2c_msg_s cmd = {
        .frequency = i2c->config.freq,
        .addr = dev_addr,
        .flags = I2C_M_READ,   //read
        .buffer = data,
        .length = size,
    };


    ret = ioctl(i2c_info_handle->fd, I2CIOC_TRANSFER, &cmd);
    if (ret < 0){
        return -QM_ERROR;
    }

#if 0
    syslog(LOG_INFO, "read result:\n");
    for (i = 0 ; i < size ; i++) {
        syslog(LOG_INFO, "%02x ", ((char *)cmd.buffer)[i]);
    }
    syslog(LOG_INFO, "read end\n");
#endif
    return QM_EOK;
}

int32_t qm_i2c_deinit(qm_i2c_dev_t *i2c)
{
    _i2c_info_t *i2c_info_handle = (_i2c_info_t *)i2c->priv;

    if(i2c_info_handle == NULL){
        return -QM_EINVAL;
    }

    close(i2c_info_handle->fd);

    qm_free(i2c_info_handle);
    i2c_info_handle = NULL;

    return QM_EOK;

}
#endif