#ifndef WEEKLY_TIMER_LOG_H
#define WEEKLY_TIMER_LOG_H

#include "qm_types.h"
#include "qm_log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WEEKLY_TIMER_LOG_NONE      = 0U,
    WEEKLY_TIMER_LOG_ERROR     = 1U << 0,
    WEEKLY_TIMER_LOG_LIFECYCLE = 1U << 1,
    WEEKLY_TIMER_LOG_CONTROL   = 1U << 2,
    WEEKLY_TIMER_LOG_STATS     = 1U << 3,
    WEEKLY_TIMER_LOG_ALL       = WEEKLY_TIMER_LOG_ERROR |
                                 WEEKLY_TIMER_LOG_LIFECYCLE |
                                 WEEKLY_TIMER_LOG_CONTROL |
                                 WEEKLY_TIMER_LOG_STATS,
} weekly_timer_log_flags_t;

uint32_t weekly_timer_get_log_flags(void);
int weekly_timer_set_log_flags(uint32_t log_flags);

#ifdef __cplusplus
}
#endif

#ifndef CONFIG_WEEKLY_TIMER_LOG_CONFIG
#define CONFIG_WEEKLY_TIMER_LOG_CONFIG 0
#endif

#if CONFIG_WEEKLY_TIMER_LOG_CONFIG
#ifndef CONFIG_WEEKLY_TIMER_LOG_ERROR
#define CONFIG_WEEKLY_TIMER_LOG_ERROR 0
#endif
#ifndef CONFIG_WEEKLY_TIMER_LOG_LIFECYCLE
#define CONFIG_WEEKLY_TIMER_LOG_LIFECYCLE 0
#endif
#ifndef CONFIG_WEEKLY_TIMER_LOG_CONTROL
#define CONFIG_WEEKLY_TIMER_LOG_CONTROL 0
#endif
#ifndef CONFIG_WEEKLY_TIMER_LOG_STATS
#define CONFIG_WEEKLY_TIMER_LOG_STATS 0
#endif
#define WEEKLY_TIMER_KCONFIG_LOG_FLAGS \
    ((CONFIG_WEEKLY_TIMER_LOG_ERROR ? WEEKLY_TIMER_LOG_ERROR : 0U) | \
     (CONFIG_WEEKLY_TIMER_LOG_LIFECYCLE ? WEEKLY_TIMER_LOG_LIFECYCLE : 0U) | \
     (CONFIG_WEEKLY_TIMER_LOG_CONTROL ? WEEKLY_TIMER_LOG_CONTROL : 0U) | \
     (CONFIG_WEEKLY_TIMER_LOG_STATS ? WEEKLY_TIMER_LOG_STATS : 0U))
#else
#define WEEKLY_TIMER_KCONFIG_LOG_FLAGS \
    (WEEKLY_TIMER_LOG_ERROR | WEEKLY_TIMER_LOG_LIFECYCLE)
#endif

#define WEEKLY_TIMER_LOG_ENABLED(category) \
    ((weekly_timer_get_log_flags() & (category)) != 0U)
#define WEEKLY_TIMER_LOGE(tag, fmt, ...) \
    do { if (WEEKLY_TIMER_LOG_ENABLED(WEEKLY_TIMER_LOG_ERROR)) \
        QM_LOGE((tag), fmt, ##__VA_ARGS__); } while (0)
#define WEEKLY_TIMER_LOGW(tag, category, fmt, ...) \
    do { if (WEEKLY_TIMER_LOG_ENABLED(category)) \
        QM_LOGW((tag), fmt, ##__VA_ARGS__); } while (0)
#define WEEKLY_TIMER_LOGI(tag, category, fmt, ...) \
    do { if (WEEKLY_TIMER_LOG_ENABLED(category)) \
        QM_LOGI((tag), fmt, ##__VA_ARGS__); } while (0)
#define WEEKLY_TIMER_LOGD(tag, category, fmt, ...) \
    do { if (WEEKLY_TIMER_LOG_ENABLED(category)) \
        QM_LOGD((tag), fmt, ##__VA_ARGS__); } while (0)

#endif
