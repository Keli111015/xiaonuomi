
#ifndef __IOT_OTA_FETCH_H__
#define __IOT_OTA_FETCH_H__

#include "qm_types.h"

void *ota_fetch_init(const char *url, uint16_t port);
int ota_fetch(void *handle, char *buf, uint32_t buf_len, uint32_t timeout);
int ota_fetch_deinit(void *handle);


#endif
