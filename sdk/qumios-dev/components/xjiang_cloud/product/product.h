#ifndef __PRODUCT_H__
#define __PRODUCT_H__

#include "cloud_common.h"
#include "qm_config.h"


int hal_get_product_id(unsigned int *product_id);
int hal_set_product_id(unsigned int product_id);

int hal_get_device_secret(char *device_secret, int *len);
int hal_set_device_secret(char *device_secret, int len);

int hal_get_device_id(char *device_id, int *len);
int hal_set_device_id(char *device_id, int len);



#if CONFIG_DYN_REG_SUPPORT

int hal_get_product_secret(char *product_secret, int *len);
int hal_set_product_secret(char *product_secret, int len);

#endif

#endif

