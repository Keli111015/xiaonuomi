#ifndef QM_AT_LOG_H
#define QM_AT_LOG_H

#include "qm_types.h"
#include "qm_log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    QM_AT_LOG_NONE      = 0U,
    QM_AT_LOG_ERROR     = 1U << 0,
    QM_AT_LOG_LIFECYCLE = 1U << 1,
    QM_AT_LOG_CONTROL   = 1U << 2,
    QM_AT_LOG_UPLINK    = 1U << 3,
    QM_AT_LOG_DOWNLINK  = 1U << 4,
    QM_AT_LOG_PAYLOAD   = 1U << 5,
    QM_AT_LOG_STATS     = 1U << 6,
    QM_AT_LOG_ALL       = QM_AT_LOG_ERROR |
                          QM_AT_LOG_LIFECYCLE |
                          QM_AT_LOG_CONTROL |
                          QM_AT_LOG_UPLINK |
                          QM_AT_LOG_DOWNLINK |
                          QM_AT_LOG_PAYLOAD |
                          QM_AT_LOG_STATS,
} qm_at_log_flags_t;

uint32_t qm_at_get_log_flags(void);
int qm_at_set_log_flags(uint32_t log_flags);

#ifdef __cplusplus
}
#endif

#ifndef CONFIG_QM_AT_LOG_CONFIG
#define CONFIG_QM_AT_LOG_CONFIG 0
#endif

#if CONFIG_QM_AT_LOG_CONFIG
#ifndef CONFIG_QM_AT_LOG_ERROR
#define CONFIG_QM_AT_LOG_ERROR 0
#endif
#ifndef CONFIG_QM_AT_LOG_LIFECYCLE
#define CONFIG_QM_AT_LOG_LIFECYCLE 0
#endif
#ifndef CONFIG_QM_AT_LOG_CONTROL
#define CONFIG_QM_AT_LOG_CONTROL 0
#endif
#ifndef CONFIG_QM_AT_LOG_UPLINK
#define CONFIG_QM_AT_LOG_UPLINK 0
#endif
#ifndef CONFIG_QM_AT_LOG_DOWNLINK
#define CONFIG_QM_AT_LOG_DOWNLINK 0
#endif
#ifndef CONFIG_QM_AT_LOG_PAYLOAD
#define CONFIG_QM_AT_LOG_PAYLOAD 0
#endif
#ifndef CONFIG_QM_AT_LOG_STATS
#define CONFIG_QM_AT_LOG_STATS 0
#endif
#define QM_AT_KCONFIG_LOG_FLAGS \
    ((CONFIG_QM_AT_LOG_ERROR ? QM_AT_LOG_ERROR : 0U) | \
     (CONFIG_QM_AT_LOG_LIFECYCLE ? QM_AT_LOG_LIFECYCLE : 0U) | \
     (CONFIG_QM_AT_LOG_CONTROL ? QM_AT_LOG_CONTROL : 0U) | \
     (CONFIG_QM_AT_LOG_UPLINK ? QM_AT_LOG_UPLINK : 0U) | \
     (CONFIG_QM_AT_LOG_DOWNLINK ? QM_AT_LOG_DOWNLINK : 0U) | \
     (CONFIG_QM_AT_LOG_PAYLOAD ? QM_AT_LOG_PAYLOAD : 0U) | \
     (CONFIG_QM_AT_LOG_STATS ? QM_AT_LOG_STATS : 0U))
#else
#define QM_AT_KCONFIG_LOG_FLAGS (QM_AT_LOG_ERROR | QM_AT_LOG_LIFECYCLE)
#endif

#define QM_AT_LOG_ENABLED(category) \
    ((qm_at_get_log_flags() & (category)) != 0U)
#define QM_AT_LOGE(tag, fmt, ...) \
    do { if (QM_AT_LOG_ENABLED(QM_AT_LOG_ERROR)) \
        QM_LOGE((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_AT_LOGW(tag, category, fmt, ...) \
    do { if (QM_AT_LOG_ENABLED(category)) \
        QM_LOGW((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_AT_LOGI(tag, category, fmt, ...) \
    do { if (QM_AT_LOG_ENABLED(category)) \
        QM_LOGI((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_AT_LOGD(tag, category, fmt, ...) \
    do { if (QM_AT_LOG_ENABLED(category)) \
        QM_LOGD((tag), fmt, ##__VA_ARGS__); } while (0)

#endif
