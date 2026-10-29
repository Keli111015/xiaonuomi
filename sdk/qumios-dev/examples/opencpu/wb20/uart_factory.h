#ifndef __UART_FACTORY_H__
#define __UART_FACTORY_H__


#include "qm.h"
#include "qm_at.h"
#include "qm_log.h"
#include "qm_kernel.h"
#include "qm_kv.h"
#include "qm_uart.h"

#define  XJIANG_FIRMWARE_VERSION_HEAD "AC.0001.M.002_FW_WB20"
#define  XJIANG_FIRMWARE_VERSION_XJIANG "0.0.5"
#define XJIANG_FIRMWARE_VERSION_MI "0.0.3"

#define SERIAL_MSGLEN 1024

void uart_fac_init(void);


#endif
