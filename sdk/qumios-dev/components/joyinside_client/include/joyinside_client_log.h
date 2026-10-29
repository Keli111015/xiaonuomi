/**
 * @file joyinside_client_log.h
 * @brief Runtime logging controls for JoyInside Client. / JoyInside 客户端运行时日志控制接口。
 */

#ifndef JOYINSIDE_CLIENT_LOG_H
#define JOYINSIDE_CLIENT_LOG_H

#include "qm_types.h"
#include "qm_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Runtime JoyInside Client log categories. / JoyInside 客户端运行时日志类别。 */
typedef enum {
    JOYINSIDE_CLIENT_LOG_NONE      = 0U,
    JOYINSIDE_CLIENT_LOG_ERROR     = 1U << 0,
    JOYINSIDE_CLIENT_LOG_LIFECYCLE = 1U << 1,
    JOYINSIDE_CLIENT_LOG_CONTROL   = 1U << 2,
    JOYINSIDE_CLIENT_LOG_UPLINK    = 1U << 3,
    JOYINSIDE_CLIENT_LOG_DOWNLINK  = 1U << 4,
    JOYINSIDE_CLIENT_LOG_PAYLOAD   = 1U << 5,
    JOYINSIDE_CLIENT_LOG_STATS     = 1U << 6,
    JOYINSIDE_CLIENT_LOG_ALL       = JOYINSIDE_CLIENT_LOG_ERROR |
                                     JOYINSIDE_CLIENT_LOG_LIFECYCLE |
                                     JOYINSIDE_CLIENT_LOG_CONTROL |
                                     JOYINSIDE_CLIENT_LOG_UPLINK |
                                     JOYINSIDE_CLIENT_LOG_DOWNLINK |
                                     JOYINSIDE_CLIENT_LOG_PAYLOAD |
                                     JOYINSIDE_CLIENT_LOG_STATS,
} joyinside_client_log_flags_t;

/**
 * @brief Get the global JoyInside Client log category mask. / 获取全局 JoyInside 客户端日志类别掩码。
 *
 * The mask applies to every client instance and standalone authentication
 * helper, but does not change the QUMI global log level.
 *
 * @return Bitmask of joyinside_client_log_flags_t.
 */
uint32_t joyinside_client_get_log_flags(void);

/**
 * @brief Set the global JoyInside Client log category mask. / 设置全局 JoyInside 客户端日志类别掩码。
 *
 * @param[in] log_flags Bitmask of joyinside_client_log_flags_t.
 * @return QM_EOK on success, or -QM_EINVAL for unsupported bits.
 */
int joyinside_client_set_log_flags(uint32_t log_flags);

#ifdef __cplusplus
}
#endif

#ifndef CONFIG_JOYINSIDE_CLIENT_LOG_CONFIG
#define CONFIG_JOYINSIDE_CLIENT_LOG_CONFIG 0
#endif

#if CONFIG_JOYINSIDE_CLIENT_LOG_CONFIG
#ifndef CONFIG_JOYINSIDE_CLIENT_LOG_ERROR
#define CONFIG_JOYINSIDE_CLIENT_LOG_ERROR 0
#endif
#ifndef CONFIG_JOYINSIDE_CLIENT_LOG_LIFECYCLE
#define CONFIG_JOYINSIDE_CLIENT_LOG_LIFECYCLE 0
#endif
#ifndef CONFIG_JOYINSIDE_CLIENT_LOG_CONTROL
#define CONFIG_JOYINSIDE_CLIENT_LOG_CONTROL 0
#endif
#ifndef CONFIG_JOYINSIDE_CLIENT_LOG_UPLINK
#define CONFIG_JOYINSIDE_CLIENT_LOG_UPLINK 0
#endif
#ifndef CONFIG_JOYINSIDE_CLIENT_LOG_DOWNLINK
#define CONFIG_JOYINSIDE_CLIENT_LOG_DOWNLINK 0
#endif
#ifndef CONFIG_JOYINSIDE_CLIENT_LOG_PAYLOAD
#define CONFIG_JOYINSIDE_CLIENT_LOG_PAYLOAD 0
#endif
#ifndef CONFIG_JOYINSIDE_CLIENT_LOG_STATS
#define CONFIG_JOYINSIDE_CLIENT_LOG_STATS 0
#endif
#define JOYINSIDE_CLIENT_KCONFIG_LOG_FLAGS \
    ((CONFIG_JOYINSIDE_CLIENT_LOG_ERROR ? JOYINSIDE_CLIENT_LOG_ERROR : 0U) | \
     (CONFIG_JOYINSIDE_CLIENT_LOG_LIFECYCLE ? JOYINSIDE_CLIENT_LOG_LIFECYCLE : 0U) | \
     (CONFIG_JOYINSIDE_CLIENT_LOG_CONTROL ? JOYINSIDE_CLIENT_LOG_CONTROL : 0U) | \
     (CONFIG_JOYINSIDE_CLIENT_LOG_UPLINK ? JOYINSIDE_CLIENT_LOG_UPLINK : 0U) | \
     (CONFIG_JOYINSIDE_CLIENT_LOG_DOWNLINK ? JOYINSIDE_CLIENT_LOG_DOWNLINK : 0U) | \
     (CONFIG_JOYINSIDE_CLIENT_LOG_PAYLOAD ? JOYINSIDE_CLIENT_LOG_PAYLOAD : 0U) | \
     (CONFIG_JOYINSIDE_CLIENT_LOG_STATS ? JOYINSIDE_CLIENT_LOG_STATS : 0U))
#else
#define JOYINSIDE_CLIENT_KCONFIG_LOG_FLAGS \
    (JOYINSIDE_CLIENT_LOG_ERROR | JOYINSIDE_CLIENT_LOG_LIFECYCLE)
#endif

#define JOYINSIDE_CLIENT_LOG_FLAGS_ENABLED(category) \
    ((joyinside_client_get_log_flags() & (category)) != 0U)

#define JOYINSIDE_CLIENT_LOGE(tag, fmt, ...) \
    do { if (JOYINSIDE_CLIENT_LOG_FLAGS_ENABLED(JOYINSIDE_CLIENT_LOG_ERROR)) \
        QM_LOGE((tag), fmt, ##__VA_ARGS__); } while (0)
#define JOYINSIDE_CLIENT_LOGW(tag, category, fmt, ...) \
    do { if (JOYINSIDE_CLIENT_LOG_FLAGS_ENABLED(category)) \
        QM_LOGW((tag), fmt, ##__VA_ARGS__); } while (0)
#define JOYINSIDE_CLIENT_LOGI(tag, category, fmt, ...) \
    do { if (JOYINSIDE_CLIENT_LOG_FLAGS_ENABLED(category)) \
        QM_LOGI((tag), fmt, ##__VA_ARGS__); } while (0)
#define JOYINSIDE_CLIENT_LOGD(tag, category, fmt, ...) \
    do { if (JOYINSIDE_CLIENT_LOG_FLAGS_ENABLED(category)) \
        QM_LOGD((tag), fmt, ##__VA_ARGS__); } while (0)

#endif
