#ifndef __SERIAL_COMMON_H__
#define __SERIAL_COMMON_H__

#include "qm.h"

#define SERIAL_SOFTWARE_VER  "3.1.4"


#ifndef CONFIG_QM_SERIAL_REPORT_NOTIFY_SUPPORT
#define CONFIG_QM_SERIAL_REPORT_NOTIFY_SUPPORT            1
#endif

#ifndef CONFIG_QM_SERIAL_ONLY_REPORT_NOTIFY_SUPPORT
#define CONFIG_QM_SERIAL_ONLY_REPORT_NOTIFY_SUPPORT       0
#endif

#if CONFIG_QM_SERIAL_REPORT_NOTIFY_SUPPORT && CONFIG_QM_SERIAL_ONLY_REPORT_NOTIFY_SUPPORT
#error "only choice report notify or only report notify support"
#endif

#if !CONFIG_QM_SERIAL_REPORT_NOTIFY_SUPPORT && !CONFIG_QM_SERIAL_ONLY_REPORT_NOTIFY_SUPPORT
#error "must choice report notify or only report notify support"
#endif

#ifndef CONFIG_QM_SERIAL_EVENT_SUPPORT
#define CONFIG_QM_SERIAL_EVENT_SUPPORT          1
#endif

#if CONFIG_QM_SERIAL_EVENT_SUPPORT

#ifndef CONFIG_QM_SERIAL_EVENT_NOTIFY_SUPPORT
#define CONFIG_QM_SERIAL_EVENT_NOTIFY_SUPPORT        1
#endif

#ifndef CONFIG_QM_SERIAL_EVENT_ONLY_NOTIFY_SUPPORT
#define CONFIG_QM_SERIAL_EVENT_ONLY_NOTIFY_SUPPORT   0
#endif

#if CONFIG_QM_SERIAL_EVENT_NOTIFY_SUPPORT && CONFIG_QM_SERIAL_EVENT_ONLY_NOTIFY_SUPPORT
#error "only choice event report notify or event only report notify support"
#endif

#if !CONFIG_QM_SERIAL_EVENT_NOTIFY_SUPPORT && !CONFIG_QM_SERIAL_EVENT_ONLY_NOTIFY_SUPPORT
#error "must choice event report notify or event only report notify support"
#endif

#ifndef CONFIG_QM_SERIAL_EVENT_MAX_NUM 
#define CONFIG_QM_SERIAL_EVENT_MAX_NUM          10
#endif

#endif

#ifndef CONFIG_QM_SERIAL_ACTION_SUPPORT
#define CONFIG_QM_SERIAL_ACTION_SUPPORT         1
#endif

#ifndef CONFIG_QM_SERIAL_RTOS_SUPPORT
#define CONFIG_QM_SERIAL_RTOS_SUPPORT         0
#endif

#ifndef CONFIG_QM_SERIAL_RESEND_SUPPORT
#define CONFIG_QM_SERIAL_RESEND_SUPPORT       1
#endif

#ifndef CONFIG_QM_SERIAL_OTA_SUPPORT          
#define CONFIG_QM_SERIAL_OTA_SUPPORT          0
#endif

#ifndef CONFIG_SERIAL_MCU_OTA_SUPPORT
#define CONFIG_SERIAL_MCU_OTA_SUPPORT         0
#endif

#ifndef CONFIG_QM_SERIAL_RAW_SUPPORT          
#define CONFIG_QM_SERIAL_RAW_SUPPORT          0
#endif

#ifndef CONFIG_QM_SERIAL_LOWPOWER_SUPPORT          
#define CONFIG_QM_SERIAL_LOWPOWER_SUPPORT     0
#endif

#ifndef CONFIG_QM_SERIAL_HEARTBEAT_NOTIFY_SUPPORT          
#define CONFIG_QM_SERIAL_HEARTBEAT_NOTIFY_SUPPORT     0
#endif

#ifndef CONFIG_QM_SERIAL_DISABLE_HEARTBEAT
#define CONFIG_QM_SERIAL_DISABLE_HEARTBEAT    0
#endif

#ifndef CONFIG_QM_SERIAL_PRINTF_SUPPORT          
#define CONFIG_QM_SERIAL_PRINTF_SUPPORT       0
#endif


#ifndef CONFIG_QM_SERIAL_SEND_INTERVAL_TIMEOUT_MS    
#define CONFIG_QM_SERIAL_SEND_INTERVAL_TIMEOUT_MS    200
#endif

#ifndef CONFIG_QM_SERIAL_ACK_TIMEOUT_MS 
#define CONFIG_QM_SERIAL_ACK_TIMEOUT_MS              500
#endif

#ifndef CONFIG_QM_SERIAL_ID_CHECK_TIMEOUT_MS 
#define CONFIG_QM_SERIAL_ID_CHECK_TIMEOUT_MS         500
#endif

#if CONFIG_QM_SERIAL_ID_CHECK_TIMEOUT_MS > CONFIG_QM_SERIAL_ACK_TIMEOUT_MS
#error serial id checkout timeout less than serial ack timeout
#endif


#ifndef CONFIG_QM_SERIAL_RECV_TIMEOUT_MS    
#define CONFIG_QM_SERIAL_RECV_TIMEOUT_MS             500
#endif


#if CONFIG_QM_SERIAL_RECV_TIMEOUT_MS > CONFIG_QM_SERIAL_ACK_TIMEOUT_MS
#error serial recv timeout less than serial ack timeout
#endif

#ifndef CONFIG_QM_SERIAL_SEND_QUEUE_NUM   
#define CONFIG_QM_SERIAL_SEND_QUEUE_NUM          20
#endif

#ifndef CONFIG_QM_SERIAL_DOWN_QUEUE_NUM   
#define CONFIG_QM_SERIAL_DOWN_QUEUE_NUM          10
#endif

#ifndef CONFIG_QM_SERIAL_SEND_RETRY_COUNT   
#define CONFIG_QM_SERIAL_SEND_RETRY_COUNT         1
#endif


#if CONFIG_QM_SERIAL_PRINTF_SUPPORT
#define SERIAL_LOG(format, ...)           qm_serial_printf("serial", format, ##__VA_ARGS__)
#define SERIAL_HEX_LOG(name, p_data, len) qm_serial_hex_printf("qm serial", name, p_data, len)
#else
#define SERIAL_LOG(format, ...)
#define SERIAL_HEX_LOG(name, p_data, len)
#endif
#endif
