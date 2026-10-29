#ifndef QM_ALARM_LOG_H
#define QM_ALARM_LOG_H

#include "qm_types.h"
#include "qm_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Runtime qm_alarm log categories. */
typedef enum {
    QM_ALARM_LOG_NONE      = 0U,
    QM_ALARM_LOG_ERROR     = 1U << 0,
    QM_ALARM_LOG_LIFECYCLE = 1U << 1,
    QM_ALARM_LOG_CONTROL   = 1U << 2,
    QM_ALARM_LOG_SCHEDULER = 1U << 3,
    QM_ALARM_LOG_STORAGE   = 1U << 4,
    QM_ALARM_LOG_CLOUD     = 1U << 5,
    QM_ALARM_LOG_STATS     = 1U << 6,
    QM_ALARM_LOG_ALL       = QM_ALARM_LOG_ERROR |
                             QM_ALARM_LOG_LIFECYCLE |
                             QM_ALARM_LOG_CONTROL |
                             QM_ALARM_LOG_SCHEDULER |
                             QM_ALARM_LOG_STORAGE |
                             QM_ALARM_LOG_CLOUD |
                             QM_ALARM_LOG_STATS,
} qm_alarm_log_flags_t;

uint32_t qm_alarm_get_log_flags(void);
int qm_alarm_set_log_flags(uint32_t log_flags);

#ifdef __cplusplus
}
#endif

#ifndef CONFIG_QM_ALARM_LOG_CONFIG
#define CONFIG_QM_ALARM_LOG_CONFIG 0
#endif

#if CONFIG_QM_ALARM_LOG_CONFIG
#ifndef CONFIG_QM_ALARM_LOG_ERROR
#define CONFIG_QM_ALARM_LOG_ERROR 0
#endif
#ifndef CONFIG_QM_ALARM_LOG_LIFECYCLE
#define CONFIG_QM_ALARM_LOG_LIFECYCLE 0
#endif
#ifndef CONFIG_QM_ALARM_LOG_CONTROL
#define CONFIG_QM_ALARM_LOG_CONTROL 0
#endif
#ifndef CONFIG_QM_ALARM_LOG_SCHEDULER
#define CONFIG_QM_ALARM_LOG_SCHEDULER 0
#endif
#ifndef CONFIG_QM_ALARM_LOG_STORAGE
#define CONFIG_QM_ALARM_LOG_STORAGE 0
#endif
#ifndef CONFIG_QM_ALARM_LOG_CLOUD
#define CONFIG_QM_ALARM_LOG_CLOUD 0
#endif
#ifndef CONFIG_QM_ALARM_LOG_STATS
#define CONFIG_QM_ALARM_LOG_STATS 0
#endif
#define QM_ALARM_KCONFIG_LOG_FLAGS \
    ((CONFIG_QM_ALARM_LOG_ERROR ? QM_ALARM_LOG_ERROR : 0U) | \
     (CONFIG_QM_ALARM_LOG_LIFECYCLE ? QM_ALARM_LOG_LIFECYCLE : 0U) | \
     (CONFIG_QM_ALARM_LOG_CONTROL ? QM_ALARM_LOG_CONTROL : 0U) | \
     (CONFIG_QM_ALARM_LOG_SCHEDULER ? QM_ALARM_LOG_SCHEDULER : 0U) | \
     (CONFIG_QM_ALARM_LOG_STORAGE ? QM_ALARM_LOG_STORAGE : 0U) | \
     (CONFIG_QM_ALARM_LOG_CLOUD ? QM_ALARM_LOG_CLOUD : 0U) | \
     (CONFIG_QM_ALARM_LOG_STATS ? QM_ALARM_LOG_STATS : 0U))
#else
#define QM_ALARM_KCONFIG_LOG_FLAGS (QM_ALARM_LOG_ERROR | QM_ALARM_LOG_LIFECYCLE)
#endif

#define QM_ALARM_LOG_ENABLED(category) \
    ((qm_alarm_get_log_flags() & (category)) != 0U)
#define QM_ALARM_LOGE(tag, fmt, ...) \
    do { if (QM_ALARM_LOG_ENABLED(QM_ALARM_LOG_ERROR)) QM_LOGE((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_ALARM_LOGW(tag, category, fmt, ...) \
    do { if (QM_ALARM_LOG_ENABLED(category)) QM_LOGW((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_ALARM_LOGI(tag, category, fmt, ...) \
    do { if (QM_ALARM_LOG_ENABLED(category)) QM_LOGI((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_ALARM_LOGD(tag, category, fmt, ...) \
    do { if (QM_ALARM_LOG_ENABLED(category)) QM_LOGD((tag), fmt, ##__VA_ARGS__); } while (0)

#endif
