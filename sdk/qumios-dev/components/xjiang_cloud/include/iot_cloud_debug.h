#ifndef __IOT_CLOUD_DEBUG_H__
#define __IOT_CLOUD_DEBUG_H__

#include "qm_types.h"
#include "qm_log.h"
#include "qm_config.h"

#ifndef CONFIG_IOT_CLOUD_DEBUG
#define CONFIG_IOT_CLOUD_DEBUG  1
#endif

#define IOT_POINTER_SANITY_CHECK(ptr, err) \
    do { \
        if (NULL == (ptr)) { \
            iot_log_err("Invalid argument, %s = %p", #ptr, ptr); \
            return (err); \
        } \
    } while(0)

#define IOT_STRING_PTR_SANITY_CHECK(ptr, err) \
    do { \
        if (NULL == (ptr)) { \
            iot_log_err("Invalid argument, %s = %p", #ptr, (ptr)); \
            return (err); \
        } \
        if (0 == strlen((ptr))) { \
            iot_log_err("Invalid argument, %s = '%s'", #ptr, (ptr)); \
            return (err); \
        } \
    } while(0)

#if CONFIG_IOT_CLOUD_DEBUG

#define iot_log_err(M, ...)         QM_LOGE("iot cloud", M, ##__VA_ARGS__)
#define iot_log_debug(M, ...)       QM_LOGD("iot cloud", M, ##__VA_ARGS__)
#define iot_log_warning(M, ...)     QM_LOGD("iot cloud", M, ##__VA_ARGS__)
#define iot_log_info(M, ...)        QM_LOGI("iot cloud", M, ##__VA_ARGS__)

#else

#define iot_log_err(M, ...)        
#define iot_log_debug(M, ...)      
#define iot_log_warning(M, ...)     
#define iot_log_info(M, ...)        

#endif


#endif
