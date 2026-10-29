#ifndef __UTIL_HTTP_DEBUG_H__
#define __UTIL_HTTP_DEBUG_H__

#include "qm_types.h"
#include "qm_log.h"
#include "qm_config.h"

#ifndef CONFIG_HTTP_DEBUG_PRINT
#define CONFIG_HTTP_DEBUG_PRINT  1
#endif

#if CONFIG_HTTP_DEBUG_PRINT

#define http_log_err(M, ...)         QM_LOGE("http", M, ##__VA_ARGS__)
#define http_log_debug(M, ...)       QM_LOGD("http", M, ##__VA_ARGS__)
#define http_log_info(M, ...)        QM_LOGI("http", M, ##__VA_ARGS__)

#else

#define http_log_err(M, ...)        
#define http_log_debug(M, ...)        
#define http_log_info(M, ...)        

#endif


#endif