#ifndef QM_EPOCHTIME_LOG_H
#define QM_EPOCHTIME_LOG_H

#include "qm_types.h"
#include "qm_log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    QM_EPOCHTIME_LOG_NONE      = 0U,
    QM_EPOCHTIME_LOG_ERROR     = 1U << 0,
    QM_EPOCHTIME_LOG_LIFECYCLE = 1U << 1,
    QM_EPOCHTIME_LOG_CONTROL   = 1U << 2,
    QM_EPOCHTIME_LOG_PAYLOAD   = 1U << 3,
    QM_EPOCHTIME_LOG_STATS     = 1U << 4,
    QM_EPOCHTIME_LOG_ALL       = QM_EPOCHTIME_LOG_ERROR |
                                 QM_EPOCHTIME_LOG_LIFECYCLE |
                                 QM_EPOCHTIME_LOG_CONTROL |
                                 QM_EPOCHTIME_LOG_PAYLOAD |
                                 QM_EPOCHTIME_LOG_STATS,
} qm_epochtime_log_flags_t;

uint32_t qm_epochtime_get_log_flags(void);
int qm_epochtime_set_log_flags(uint32_t log_flags);

#ifdef __cplusplus
}
#endif

#ifndef CONFIG_QM_EPOCHTIME_LOG_CONFIG
#define CONFIG_QM_EPOCHTIME_LOG_CONFIG 0
#endif

#if CONFIG_QM_EPOCHTIME_LOG_CONFIG
#ifndef CONFIG_QM_EPOCHTIME_LOG_ERROR
#define CONFIG_QM_EPOCHTIME_LOG_ERROR 0
#endif
#ifndef CONFIG_QM_EPOCHTIME_LOG_LIFECYCLE
#define CONFIG_QM_EPOCHTIME_LOG_LIFECYCLE 0
#endif
#ifndef CONFIG_QM_EPOCHTIME_LOG_CONTROL
#define CONFIG_QM_EPOCHTIME_LOG_CONTROL 0
#endif
#ifndef CONFIG_QM_EPOCHTIME_LOG_PAYLOAD
#define CONFIG_QM_EPOCHTIME_LOG_PAYLOAD 0
#endif
#ifndef CONFIG_QM_EPOCHTIME_LOG_STATS
#define CONFIG_QM_EPOCHTIME_LOG_STATS 0
#endif
#define QM_EPOCHTIME_KCONFIG_LOG_FLAGS \
    ((CONFIG_QM_EPOCHTIME_LOG_ERROR ? QM_EPOCHTIME_LOG_ERROR : 0U) | \
     (CONFIG_QM_EPOCHTIME_LOG_LIFECYCLE ? QM_EPOCHTIME_LOG_LIFECYCLE : 0U) | \
     (CONFIG_QM_EPOCHTIME_LOG_CONTROL ? QM_EPOCHTIME_LOG_CONTROL : 0U) | \
     (CONFIG_QM_EPOCHTIME_LOG_PAYLOAD ? QM_EPOCHTIME_LOG_PAYLOAD : 0U) | \
     (CONFIG_QM_EPOCHTIME_LOG_STATS ? QM_EPOCHTIME_LOG_STATS : 0U))
#else
#define QM_EPOCHTIME_KCONFIG_LOG_FLAGS \
    (QM_EPOCHTIME_LOG_ERROR | QM_EPOCHTIME_LOG_LIFECYCLE)
#endif

#define QM_EPOCHTIME_LOG_ENABLED(category) \
    ((qm_epochtime_get_log_flags() & (category)) != 0U)
#define QM_EPOCHTIME_LOGE(tag, fmt, ...) \
    do { if (QM_EPOCHTIME_LOG_ENABLED(QM_EPOCHTIME_LOG_ERROR)) \
        QM_LOGE((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_EPOCHTIME_LOGW(tag, category, fmt, ...) \
    do { if (QM_EPOCHTIME_LOG_ENABLED(category)) \
        QM_LOGW((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_EPOCHTIME_LOGI(tag, category, fmt, ...) \
    do { if (QM_EPOCHTIME_LOG_ENABLED(category)) \
        QM_LOGI((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_EPOCHTIME_LOGD(tag, category, fmt, ...) \
    do { if (QM_EPOCHTIME_LOG_ENABLED(category)) \
        QM_LOGD((tag), fmt, ##__VA_ARGS__); } while (0)

#endif
