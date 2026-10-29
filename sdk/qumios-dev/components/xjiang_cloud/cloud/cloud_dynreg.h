#ifndef _CLOUD_DYNREG_H_
#define _CLOUD_DYNREG_H_

#include "iot_device.h"
#include "cloud_common.h"
#include "qm_config.h"

#if CONFIG_DYN_REG_SUPPORT

typedef struct {
    
    uint32_t product_id;
    char sn[DEVICE_SN_LEN+1];
    char product_secret[PRODUCT_SECRET_LEN+1];
    char device_id[DEVICE_ID_LEN+1];
    char device_secret[DEVICE_SECRET_LEN+1];
}dev_dynreg_info_t;


int32_t cloud_device_dynreg(dev_dynreg_info_t *dynreg_info);

#endif

#endif
