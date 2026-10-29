#ifndef __OPPLE_SERIAL_COMMON_H__
#define __OPPLE_SERIAL_COMMON_H__

#include "qm_types.h"
#include "qm_config.h"

#if CONFIG_OPPLE_SERIAL_SUPPORT

#define OPPLE_SERIAL_SOFTWARE_VER  "1.0.2"

#ifndef CONFIG_OPPLE_SERIAL_REPORT_SUPPORT
#define CONFIG_OPPLE_SERIAL_REPORT_SUPPORT   1
#endif

#ifndef CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
#define CONFIG_OPPLE_SERIAL_RTOS_SUPPORT   0
#endif

#ifndef CONFIG_OPPLE_SERIAL_RESEND_SUPPORT
#define CONFIG_OPPLE_SERIAL_RESEND_SUPPORT  1
#endif

#ifndef CONFIG_OPPLE_SERIAL_PRINTF_SUPPORT
#define CONFIG_OPPLE_SERIAL_PRINTF_SUPPORT  0
#endif

#ifndef CONFIG_OPPLE_SERIAL_SEND_INTERVAL_TIMEOUT_MS
#define CONFIG_OPPLE_SERIAL_SEND_INTERVAL_TIMEOUT_MS  40
#endif

#ifndef CONFIG_OPPLE_SERIAL_ACK_TIMEOUT_MS
#define CONFIG_OPPLE_SERIAL_ACK_TIMEOUT_MS  100
#endif

#ifndef CONFIG_OPPLE_SERIAL_ID_CHECK_TIMEOUT_MS
#define CONFIG_OPPLE_SERIAL_ID_CHECK_TIMEOUT_MS  100
#endif

#ifndef CONFIG_OPPLE_SERIAL_RECV_TIMEOUT_MS
#define CONFIG_OPPLE_SERIAL_RECV_TIMEOUT_MS  50
#endif

#ifndef CONFIG_OPPLE_SERIAL_SEND_QUEUE_NUM
#define CONFIG_OPPLE_SERIAL_SEND_QUEUE_NUM  30
#endif

#ifndef CONFIG_OPPLE_SERIAL_DOWN_QUEUE_NUM
#define CONFIG_OPPLE_SERIAL_DOWN_QUEUE_NUM  30
#endif

#ifndef CONFIG_OPPLE_SERIAL_SEND_RETRY_COUNT
#define CONFIG_OPPLE_SERIAL_SEND_RETRY_COUNT  1
#endif

#ifndef CONFIG_OPPLE_SERIAL_KV_MAX_SIZE 
#define CONFIG_OPPLE_SERIAL_KV_MAX_SIZE       16
#endif

#if CONFIG_OPPLE_SERIAL_PRINTF_SUPPORT == 0
#define OPPLE_SERIAL_LOG(format, ...) \
    do{ \
        opple_serial_printf("[%s:%d]",__FUNCTION__,__LINE__);\
        opple_serial_printf(format,##__VA_ARGS__);\
        opple_serial_printf("\n");\
     }while(0)

#define OPPLE_SERIAL_HEX_LOG(name, p_data, len) \
    do{\
        uint32_t i; \
        opple_serial_printf("[%s:%d]",__FUNCTION__,__LINE__);\
        opple_serial_printf("%s ",name);  \
        for(i=0;i<len;i++)\
        {\
          opple_serial_printf("%02X ", *(p_data+i));\
        }\
        opple_serial_printf("\n");\
      }while(0)
#else 
      
#define OPPLE_SERIAL_LOG(format, ...)           opple_serial_printf("serial", format, ##__VA_ARGS__)
#define OPPLE_SERIAL_HEX_LOG(name, p_data, len) opple_serial_hex_printf("opple serial", name, p_data, len)
    
#endif

#endif

#endif