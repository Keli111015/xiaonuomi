#ifndef BAIDU_CHAT_AGENT_LOG_H
#define BAIDU_CHAT_AGENT_LOG_H

#include "qm_types.h"
#include "qm_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Runtime baidu_chat_agent log categories. */
typedef enum {
    BAIDU_CHAT_AGENT_LOG_NONE      = 0U,
    BAIDU_CHAT_AGENT_LOG_ERROR     = 1U << 0,
    BAIDU_CHAT_AGENT_LOG_LIFECYCLE = 1U << 1,
    BAIDU_CHAT_AGENT_LOG_CONTROL   = 1U << 2,
    BAIDU_CHAT_AGENT_LOG_UPLINK    = 1U << 3,
    BAIDU_CHAT_AGENT_LOG_DOWNLINK  = 1U << 4,
    BAIDU_CHAT_AGENT_LOG_PAYLOAD   = 1U << 5,
    BAIDU_CHAT_AGENT_LOG_STATS     = 1U << 6,
    BAIDU_CHAT_AGENT_LOG_ALL       = BAIDU_CHAT_AGENT_LOG_ERROR |
                                     BAIDU_CHAT_AGENT_LOG_LIFECYCLE |
                                     BAIDU_CHAT_AGENT_LOG_CONTROL |
                                     BAIDU_CHAT_AGENT_LOG_UPLINK |
                                     BAIDU_CHAT_AGENT_LOG_DOWNLINK |
                                     BAIDU_CHAT_AGENT_LOG_PAYLOAD |
                                     BAIDU_CHAT_AGENT_LOG_STATS,
} baidu_chat_agent_log_flags_t;

uint32_t baidu_chat_agent_get_log_flags(void);
int baidu_chat_agent_set_log_flags(uint32_t log_flags);

#ifdef __cplusplus
}
#endif

#ifndef CONFIG_BAIDU_CHAT_AGENT_LOG_CONFIG
#define CONFIG_BAIDU_CHAT_AGENT_LOG_CONFIG 0
#endif

#if CONFIG_BAIDU_CHAT_AGENT_LOG_CONFIG
#ifndef CONFIG_BAIDU_CHAT_AGENT_LOG_ERROR
#define CONFIG_BAIDU_CHAT_AGENT_LOG_ERROR 0
#endif
#ifndef CONFIG_BAIDU_CHAT_AGENT_LOG_LIFECYCLE
#define CONFIG_BAIDU_CHAT_AGENT_LOG_LIFECYCLE 0
#endif
#ifndef CONFIG_BAIDU_CHAT_AGENT_LOG_CONTROL
#define CONFIG_BAIDU_CHAT_AGENT_LOG_CONTROL 0
#endif
#ifndef CONFIG_BAIDU_CHAT_AGENT_LOG_UPLINK
#define CONFIG_BAIDU_CHAT_AGENT_LOG_UPLINK 0
#endif
#ifndef CONFIG_BAIDU_CHAT_AGENT_LOG_DOWNLINK
#define CONFIG_BAIDU_CHAT_AGENT_LOG_DOWNLINK 0
#endif
#ifndef CONFIG_BAIDU_CHAT_AGENT_LOG_PAYLOAD
#define CONFIG_BAIDU_CHAT_AGENT_LOG_PAYLOAD 0
#endif
#ifndef CONFIG_BAIDU_CHAT_AGENT_LOG_STATS
#define CONFIG_BAIDU_CHAT_AGENT_LOG_STATS 0
#endif
#define BAIDU_CHAT_AGENT_KCONFIG_LOG_FLAGS \
    ((CONFIG_BAIDU_CHAT_AGENT_LOG_ERROR ? BAIDU_CHAT_AGENT_LOG_ERROR : 0U) | \
     (CONFIG_BAIDU_CHAT_AGENT_LOG_LIFECYCLE ? BAIDU_CHAT_AGENT_LOG_LIFECYCLE : 0U) | \
     (CONFIG_BAIDU_CHAT_AGENT_LOG_CONTROL ? BAIDU_CHAT_AGENT_LOG_CONTROL : 0U) | \
     (CONFIG_BAIDU_CHAT_AGENT_LOG_UPLINK ? BAIDU_CHAT_AGENT_LOG_UPLINK : 0U) | \
     (CONFIG_BAIDU_CHAT_AGENT_LOG_DOWNLINK ? BAIDU_CHAT_AGENT_LOG_DOWNLINK : 0U) | \
     (CONFIG_BAIDU_CHAT_AGENT_LOG_PAYLOAD ? BAIDU_CHAT_AGENT_LOG_PAYLOAD : 0U) | \
     (CONFIG_BAIDU_CHAT_AGENT_LOG_STATS ? BAIDU_CHAT_AGENT_LOG_STATS : 0U))
#else
#define BAIDU_CHAT_AGENT_KCONFIG_LOG_FLAGS \
    (BAIDU_CHAT_AGENT_LOG_ERROR | BAIDU_CHAT_AGENT_LOG_LIFECYCLE)
#endif

#define BAIDU_CHAT_AGENT_LOG_FLAGS_ENABLED(category) \
    ((baidu_chat_agent_get_log_flags() & (category)) != 0U)

#define BAIDU_CHAT_AGENT_LOGE(tag, fmt, ...) \
    do { if (BAIDU_CHAT_AGENT_LOG_FLAGS_ENABLED(BAIDU_CHAT_AGENT_LOG_ERROR)) \
        QM_LOGE((tag), fmt, ##__VA_ARGS__); } while (0)
#define BAIDU_CHAT_AGENT_LOGW(tag, category, fmt, ...) \
    do { if (BAIDU_CHAT_AGENT_LOG_FLAGS_ENABLED(category)) \
        QM_LOGW((tag), fmt, ##__VA_ARGS__); } while (0)
#define BAIDU_CHAT_AGENT_LOGI(tag, category, fmt, ...) \
    do { if (BAIDU_CHAT_AGENT_LOG_FLAGS_ENABLED(category)) \
        QM_LOGI((tag), fmt, ##__VA_ARGS__); } while (0)
#define BAIDU_CHAT_AGENT_LOGD(tag, category, fmt, ...) \
    do { if (BAIDU_CHAT_AGENT_LOG_FLAGS_ENABLED(category)) \
        QM_LOGD((tag), fmt, ##__VA_ARGS__); } while (0)

#endif
