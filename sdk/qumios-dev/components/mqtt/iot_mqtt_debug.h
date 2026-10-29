#ifndef __MQTT_DEBUG_H__
#define __MQTT_DEBUG_H__

#include "qm_types.h"
#include "qm_log.h"
#include "qm_config.h"

#ifndef CONFIG_MQTT_DEBUG
#define CONFIG_MQTT_DEBUG  1
#endif

#define POINTER_SANITY_CHECK(ptr, err) \
    do { \
        if (NULL == (ptr)) { \
            mqtt_log_err("Invalid argument, %s = %p", #ptr, ptr); \
            return (err); \
        } \
    } while(0)

#define STRING_PTR_SANITY_CHECK(ptr, err) \
    do { \
        if (NULL == (ptr)) { \
            mqtt_log_err("Invalid argument, %s = %p", #ptr, (ptr)); \
            return (err); \
        } \
        if (0 == strlen((ptr))) { \
            mqtt_log_err("Invalid argument, %s = '%s'", #ptr, (ptr)); \
            return (err); \
        } \
    } while(0)

#if CONFIG_MQTT_DEBUG

#define mqtt_log_err(M, ...)         QM_LOGE("mqtt", M, ##__VA_ARGS__)
#define mqtt_log_debug(M, ...)       QM_LOGD("mqtt", M, ##__VA_ARGS__)
#define mqtt_log_warning(M, ...)     QM_LOGD("mqtt", M, ##__VA_ARGS__)
#define mqtt_log_info(M, ...)        QM_LOGI("mqtt", M, ##__VA_ARGS__)
#define mqtt_hex_log(M, pdata, len)  QM_HEX_LOGD("mqtt", M, pdata, len)
#else

#define mqtt_log_err(M, ...)        
#define mqtt_log_debug(M, ...)      
#define mqtt_log_warning(M, ...)     
#define mqtt_log_info(M, ...)        

#endif


#endif
