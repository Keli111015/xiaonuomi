#ifndef LGEINE_AGENT_LOG_H
#define LGEINE_AGENT_LOG_H

#include "qm_log.h"
#include "qm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Runtime lgeine_agent log categories.
 */
typedef enum {
    LGEINE_AGENT_LOG_NONE      = 0U,
    LGEINE_AGENT_LOG_ERROR     = 1U << 0,
    LGEINE_AGENT_LOG_LIFECYCLE = 1U << 1,
    LGEINE_AGENT_LOG_CONTROL   = 1U << 2,
    LGEINE_AGENT_LOG_UPLINK    = 1U << 3,
    LGEINE_AGENT_LOG_DOWNLINK  = 1U << 4,
    LGEINE_AGENT_LOG_PAYLOAD   = 1U << 5,
    LGEINE_AGENT_LOG_ALL       = LGEINE_AGENT_LOG_ERROR |
                                 LGEINE_AGENT_LOG_LIFECYCLE |
                                 LGEINE_AGENT_LOG_CONTROL |
                                 LGEINE_AGENT_LOG_UPLINK |
                                 LGEINE_AGENT_LOG_DOWNLINK |
                                 LGEINE_AGENT_LOG_PAYLOAD,
} lgeine_agent_log_flags_t;

/**
 * @brief Return the process-wide lgeine_agent runtime log mask.
 *
 * @return Current log category bitmask of lgeine_agent_log_flags_t
 */
uint32_t lgeine_agent_get_log_flags(void);

/**
 * @brief Set the process-wide lgeine_agent runtime log mask.
 *
 * @param[in] log_flags Log category bitmask of lgeine_agent_log_flags_t
 * @return QM_EOK on success
 * @return -QM_EINVAL if log_flags contains unknown bits
 */
int lgeine_agent_set_log_flags(uint32_t log_flags);

#ifdef __cplusplus
}
#endif

#ifndef CONFIG_LGEINE_AGENT_LOG_CONFIG
#define CONFIG_LGEINE_AGENT_LOG_CONFIG 0
#endif

#if CONFIG_LGEINE_AGENT_LOG_CONFIG
#ifndef CONFIG_LGEINE_AGENT_LOG_ERROR
#define CONFIG_LGEINE_AGENT_LOG_ERROR 0
#endif
#ifndef CONFIG_LGEINE_AGENT_LOG_LIFECYCLE
#define CONFIG_LGEINE_AGENT_LOG_LIFECYCLE 0
#endif
#ifndef CONFIG_LGEINE_AGENT_LOG_CONTROL
#define CONFIG_LGEINE_AGENT_LOG_CONTROL 0
#endif
#ifndef CONFIG_LGEINE_AGENT_LOG_UPLINK
#define CONFIG_LGEINE_AGENT_LOG_UPLINK 0
#endif
#ifndef CONFIG_LGEINE_AGENT_LOG_DOWNLINK
#define CONFIG_LGEINE_AGENT_LOG_DOWNLINK 0
#endif
#ifndef CONFIG_LGEINE_AGENT_LOG_PAYLOAD
#define CONFIG_LGEINE_AGENT_LOG_PAYLOAD 0
#endif
#define LGEINE_AGENT_KCONFIG_LOG_FLAGS \
    ((CONFIG_LGEINE_AGENT_LOG_ERROR ? LGEINE_AGENT_LOG_ERROR : 0U) | \
     (CONFIG_LGEINE_AGENT_LOG_LIFECYCLE ? LGEINE_AGENT_LOG_LIFECYCLE : 0U) | \
     (CONFIG_LGEINE_AGENT_LOG_CONTROL ? LGEINE_AGENT_LOG_CONTROL : 0U) | \
     (CONFIG_LGEINE_AGENT_LOG_UPLINK ? LGEINE_AGENT_LOG_UPLINK : 0U) | \
     (CONFIG_LGEINE_AGENT_LOG_DOWNLINK ? LGEINE_AGENT_LOG_DOWNLINK : 0U) | \
     (CONFIG_LGEINE_AGENT_LOG_PAYLOAD ? LGEINE_AGENT_LOG_PAYLOAD : 0U))
#else
#define LGEINE_AGENT_KCONFIG_LOG_FLAGS \
    (LGEINE_AGENT_LOG_ERROR | LGEINE_AGENT_LOG_LIFECYCLE)
#endif

#define LGEINE_AGENT_LOG_FLAGS_ENABLED(category) \
    ((lgeine_agent_get_log_flags() & (category)) != 0U)

#define LGEINE_AGENT_LOGE(tag, fmt, ...) \
    do { if (LGEINE_AGENT_LOG_FLAGS_ENABLED(LGEINE_AGENT_LOG_ERROR)) \
        QM_LOGE((tag), fmt, ##__VA_ARGS__); } while (0)
#define LGEINE_AGENT_LOGW(tag, category, fmt, ...) \
    do { if (LGEINE_AGENT_LOG_FLAGS_ENABLED(category)) \
        QM_LOGW((tag), fmt, ##__VA_ARGS__); } while (0)
#define LGEINE_AGENT_LOGI(tag, category, fmt, ...) \
    do { if (LGEINE_AGENT_LOG_FLAGS_ENABLED(category)) \
        QM_LOGI((tag), fmt, ##__VA_ARGS__); } while (0)
#define LGEINE_AGENT_LOGD(tag, category, fmt, ...) \
    do { if (LGEINE_AGENT_LOG_FLAGS_ENABLED(category)) \
        QM_LOGD((tag), fmt, ##__VA_ARGS__); } while (0)

#endif