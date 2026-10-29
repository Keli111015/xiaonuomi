#ifndef _TM1651_H
#define _TM1651_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"

void * tm1651_init(int scl, int sda);
qm_err_t tm1651_deinit(void *handle);

qm_err_t tm1651_display_off(void *handle);
qm_err_t tm1651_display_num(void *handle,uint8_t num);
qm_err_t tm1651_display_errcode(void *handle, uint8_t code);

#ifdef __cplusplus
}
#endif

#endif