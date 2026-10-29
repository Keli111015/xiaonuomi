#ifndef QM_LOG_IMPL_H
#define QM_LOG_IMPL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_config.h"

extern unsigned int qm_log_level;
static inline unsigned int qm_log_get_level(void)
{
    return qm_log_level;
}

enum qm_log_level_bit {
    QM_LL_V_NONE_BIT = -1,
    QM_LL_V_FATAL_BIT,
    QM_LL_V_ERROR_BIT,
    QM_LL_V_WARN_BIT,
    QM_LL_V_INFO_BIT,
    QM_LL_V_DEBUG_BIT,
    QM_LL_V_MAX_BIT
};

#define QM_LOG_LEVEL qm_log_get_level()

#define QM_LL_V_NONE  0
#define QM_LL_V_ALL   0XFF
#define QM_LL_V_FATAL (1 << QM_LL_V_FATAL_BIT)
#define QM_LL_V_ERROR (1 << QM_LL_V_ERROR_BIT)
#define QM_LL_V_WARN  (1 << QM_LL_V_WARN_BIT)
#define QM_LL_V_INFO  (1 << QM_LL_V_INFO_BIT)
#define QM_LL_V_DEBUG (1 << QM_LL_V_DEBUG_BIT)

/*
 * color def.
 * see http://stackoverflow.com/questions/3585846/color-text-in-terminal-applications-in-unix
 */
#define COL_DEF "\x1B[0m"  /* white */
#define COL_RED "\x1B[31m" /* red */
#define COL_GRE "\x1B[32m" /* green */
#define COL_BLU "\x1B[34m" /* blue */
#define COL_YEL "\x1B[33m" /* yellow */
#define COL_WHE "\x1B[37m" /* white */
#define COL_CYN "\x1B[36m"
#define COL_MAG "\x1B[35m"

#if CONFIG_QM_OS_SUPPORT
#include <qmos/qm_kernel.h>
extern qm_mutex_t g_log_mutex;
#if CONFIG_LOG_DETAILS
#define qm_log_print(CON, MOD, LVL, FMT, ...) \
    do {                                          \
        if (QM_LOG_LEVEL & CON) {                \
            uint32_t ms = qm_now_ms();          \
            qm_mutex_lock(&g_log_mutex, QM_WAIT_FOREVER);  \
            qm_printf("[%4d.%03d]<%s> %s [%s#%d] : ", (int)(ms/1000), (int)(ms%1000), LVL, MOD, __FUNCTION__, __LINE__); \
            qm_printf(FMT "\r\n", ##__VA_ARGS__); \
            qm_mutex_unlock(&g_log_mutex);  \
        } \
    } while (0)
    
#if CONFIG_QM_LOG_HEX_CACHE_SUPPORT
#include "qm_utils_string.h"
#define qm_hex_log_print(CON, MOD, LVL, name, p_data, len) \
    do{\
        char hex_buffer[CONFIG_QM_LOG_HEX_CACHE_SIZE + 1] = {0}; \
        if (QM_LOG_LEVEL & CON) { \
            uint32_t ms = qm_now_ms(); \
            if(len *3 > CONFIG_QM_LOG_HEX_CACHE_SIZE){break;}\
            hex_to_strs_print((uint8_t*)p_data, len, hex_buffer);   \
            qm_mutex_lock(&g_log_mutex, QM_WAIT_FOREVER);  \
            qm_printf("[%4d.%03d]<%s> %s [%s#%d] : %s %s\r\n", (int)(ms/1000), (int)(ms%1000), LVL, MOD, __FUNCTION__, __LINE__, name, hex_buffer); \
            qm_mutex_unlock(&g_log_mutex);  \
        }\
      }while(0)
    
#else
    
#define qm_hex_log_print(CON, MOD, LVL, name, p_data, len) \
    do{\
        uint32_t i = 0; \
        if (QM_LOG_LEVEL & CON) { \
            uint32_t ms = qm_now_ms(); \
            qm_mutex_lock(&g_log_mutex, QM_WAIT_FOREVER);  \
            qm_printf("[%4d.%03d]<%s> %s [%s#%d] : ", (int)(ms/1000), (int)(ms%1000), LVL, MOD, __FUNCTION__, __LINE__); \
            qm_printf("%s ", name);  \
            for(i = 0; i<len; i++)\
            {\
                qm_printf("%02X ", *((uint8_t*)p_data+i));\
            }\
            qm_printf("\r\n");\
            qm_mutex_unlock(&g_log_mutex);  \
        }\
      }while(0)
    
#endif
    
#else
#if CONFIG_LOG_SIMPLE
#define qm_log_print(CON, MOD, LVL, FMT, ...) \
    do { \
        if (QM_LOG_LEVEL & CON) { \
            qm_mutex_lock(&g_log_mutex, QM_WAIT_FOREVER);  \
            qm_printf("[%06d]<" LVL "> "FMT"\r\n", (unsigned)qm_now_ms(), ##__VA_ARGS__); \
            qm_mutex_unlock(&g_log_mutex);  \
        } \
    } while (0)
    
#if CONFIG_QM_LOG_HEX_CACHE_SUPPORT 
 #include "qm_utils_string.h"
 #define qm_hex_log_print(CON, MOD, LVL, name, p_data, len) \
    do{\
        char hex_buffer[CONFIG_QM_LOG_HEX_CACHE_SIZE + 1] = {0}; \
        if (QM_LOG_LEVEL & CON) { \
            if(len *3 > CONFIG_QM_LOG_HEX_CACHE_SIZE){break;}\
            hex_to_strs_print((uint8_t*)p_data, len, hex_buffer);   \
            qm_mutex_lock(&g_log_mutex, QM_WAIT_FOREVER);  \
            qm_printf("[%06d]<" LVL "> %s %s \r\n", (unsigned)qm_now_ms(), name, hex_buffer); \
            qm_mutex_unlock(&g_log_mutex);  \
        }\
      }while(0)
 
#else     
#define qm_hex_log_print(CON, MOD, LVL, name, p_data, len) \
    do{\
        uint32_t i = 0; \
        if (QM_LOG_LEVEL & CON) { \
            qm_mutex_lock(&g_log_mutex, QM_WAIT_FOREVER);  \
            qm_printf("[%06d]<" LVL "> %s", (unsigned)qm_now_ms(),name); \
            for(i = 0; i<len; i++)\
            {\
                qm_printf("%02X ", *((uint8_t*)p_data+i));\
            }\
            qm_printf("\r\n");\
            qm_mutex_unlock(&g_log_mutex);  \
        }\
      }while(0)
#endif
      
#endif
#endif

#else
#if CONFIG_LOG_DETAILS
#define qm_log_print(CON, MOD, LVL, FMT, ...) \
    do {                                          \
        if (QM_LOG_LEVEL & CON) {                \
            uint32_t ms = qm_now_ms();          \
            qm_printf("[%4d.%03d]<%s> %s [%s#%d] : ", (int)(ms/1000), (int)(ms%1000), LVL, MOD, __FUNCTION__, __LINE__); \
            qm_printf(FMT "\r\n", ##__VA_ARGS__); \
        } \
    } while (0)
    
#if CONFIG_QM_LOG_HEX_CACHE_SUPPORT
#include "qm_utils_string.h"
#define qm_hex_log_print(CON, MOD, LVL, name, p_data, len) \
    do{\
        char hex_buffer[CONFIG_QM_LOG_HEX_CACHE_SIZE + 1] = {0}; \
        if (QM_LOG_LEVEL & CON) { \
            uint32_t ms = qm_now_ms(); \
            if(len *3 > CONFIG_QM_LOG_HEX_CACHE_SIZE){break;}\
            hex_to_strs_print((uint8_t*)p_data, len, hex_buffer);   \
            qm_printf("[%4d.%03d]<%s> %s [%s#%d] : %s %s\r\n", (int)(ms/1000), (int)(ms%1000), LVL, MOD, __FUNCTION__, __LINE__, name, hex_buffer); \
        }\
      }while(0)
    
#else
    
#define qm_hex_log_print(CON, MOD, LVL, name, p_data, len) \
    do{\
        uint32_t i = 0; \
        if (QM_LOG_LEVEL & CON) { \
            uint32_t ms = qm_now_ms(); \
            qm_printf("[%4d.%03d]<%s> %s [%s#%d] : ", (int)(ms/1000), (int)(ms%1000), LVL, MOD, __FUNCTION__, __LINE__); \
            qm_printf("%s ", name);  \
            for(i = 0; i<len; i++)\
            {\
                qm_printf("%02X ", *((uint8_t*)p_data+i));\
            }\
            qm_printf("\r\n");\
        }\
      }while(0)
    
#endif
    
#else
#if CONFIG_LOG_SIMPLE
#include "qm_kernel.h"
extern qm_mutex_t g_log_mutex;
#define qm_log_print(CON, MOD, LVL, FMT, ...) \
    do { \
        if (QM_LOG_LEVEL & CON) { \
            qm_printf("[%06d]<" LVL "> "FMT"\r\n", (unsigned)qm_now_ms(), ##__VA_ARGS__); \
        } \
    } while (0)
    
#if CONFIG_QM_LOG_HEX_CACHE_SUPPORT 
 #include "qm_utils_string.h"
 #define qm_hex_log_print(CON, MOD, LVL, name, p_data, len) \
    do{\
        char hex_buffer[CONFIG_QM_LOG_HEX_CACHE_SIZE + 1] = {0}; \
        if (QM_LOG_LEVEL & CON) { \
            if(len *3 > CONFIG_QM_LOG_HEX_CACHE_SIZE){break;}\
            hex_to_strs_print((uint8_t*)p_data, len, hex_buffer);   \
            qm_printf("[%06d]<" LVL "> %s %s \r\n", (unsigned)qm_now_ms(), name, hex_buffer); \
        }\
      }while(0)
 
#else     
#define qm_hex_log_print(CON, MOD, LVL, name, p_data, len) \
    do{\
        uint32_t i = 0; \
        if (QM_LOG_LEVEL & CON) { \
            qm_printf("[%06d]<" LVL "> %s", (unsigned)qm_now_ms(),name); \
            for(i = 0; i<len; i++)\
            {\
                qm_printf("%02X ", *((uint8_t*)p_data+i));\
            }\
            qm_printf("\r\n");\
        }\
      }while(0)
#endif
      
#endif
#endif
#endif

#ifdef __GNUC__
#define SHORT_FILE __FILENAME__
#else
#define SHORT_FILE strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__
#endif


#if CONFIG_QM_ASSERT
#define QM_ASSERT_IMPL(expr) \
    do { \
        if(!expr){ \
            qm_printf("ASSERT!!!, file: %s, line: %d\r\n", __FILE__, __LINE__); \
        } \
    } while (0)
#else
#define QM_ASSERT_IMPL(expr) ((void)0U)
#endif

#if CONFIG_QM_LOG_SUPPORT

extern int qm_printf(const char *fmt, ...);

#define QM_LOGD_IMPL(mod, fmt, ...) \
        qm_log_print(QM_LL_V_DEBUG, mod, "D", fmt, ##__VA_ARGS__)
#define QM_LOGF_IMPL(mod, fmt, ...) \
        qm_log_print(QM_LL_V_FATAL, mod, "F", fmt, ##__VA_ARGS__)
#define QM_LOGE_IMPL(mod, fmt, ...) \
        qm_log_print(QM_LL_V_ERROR, mod, "E", fmt, ##__VA_ARGS__)
#define QM_LOGW_IMPL(mod, fmt, ...) \
        qm_log_print(QM_LL_V_WARN, mod, "W", fmt, ##__VA_ARGS__)
#define QM_LOGI_IMPL(mod, fmt, ...) \
        qm_log_print(QM_LL_V_INFO, mod, "I", fmt, ##__VA_ARGS__)


#define QM_HEX_LOGD_IMPL(mod, name, p_data, len) \
        qm_hex_log_print(QM_LL_V_DEBUG, mod, "D", name, p_data, len)
#define QM_HEX_LOGF_IMPL(mod, name, p_data, len) \
        qm_hex_log_print(QM_LL_V_FATAL, mod, "F", name, p_data, len)
#define QM_HEX_LOGE_IMPL(mod, name, p_data, len) \
        qm_hex_log_print(QM_LL_V_ERROR, mod, "E", name, p_data, len)
#define QM_HEX_LOGW_IMPL(mod, name, p_data, len) \
        qm_hex_log_print(QM_LL_V_WARN, mod, "W", name, p_data, len)
#define QM_HEX_LOGI_IMPL(mod, name, p_data, len) \
        qm_hex_log_print(QM_LL_V_INFO, mod, "I", name, p_data, len)


#else

#define QM_LOGF_IMPL(mod, fmt, ...) 
#define QM_LOGE_IMPL(mod, fmt, ...) 
#define QM_LOGW_IMPL(mod, fmt, ...) 
#define QM_LOGI_IMPL(mod, fmt, ...) 
#define QM_LOGD_IMPL(mod, fmt, ...) 

#define QM_HEX_LOGF_IMPL(mod, name, p_data, len) 
#define QM_HEX_LOGE_IMPL(mod, name, p_data, len) 
#define QM_HEX_LOGW_IMPL(mod, name, p_data, len) 
#define QM_HEX_LOGI_IMPL(mod, name, p_data, len) 
#define QM_HEX_LOGD_IMPL(mod, name, p_data, len) 

#endif /* CONFIG_QM_LOG_SUPPORT */

#ifdef __cplusplus
}
#endif

#endif /* QM_LOG_IMPL_H */

