#ifndef QM_CLOUD_LOG_H
#define QM_CLOUD_LOG_H

#include "qm_types.h"
#include "qm_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Runtime log categories shared by all qm_cloud submodules.
 */
typedef enum {
    QM_CLOUD_LOG_NONE = 0U,
    QM_CLOUD_LOG_ERROR = 1U << 0,
    QM_CLOUD_LOG_LIFECYCLE = 1U << 1,
    QM_CLOUD_LOG_CONTROL = 1U << 2,
    QM_CLOUD_LOG_UPLINK = 1U << 3,
    QM_CLOUD_LOG_DOWNLINK = 1U << 4,
    QM_CLOUD_LOG_PAYLOAD = 1U << 5,
    QM_CLOUD_LOG_AUTH = 1U << 6,
    QM_CLOUD_LOG_OTA = 1U << 7,
    QM_CLOUD_LOG_SUBDEV = 1U << 8,
    QM_CLOUD_LOG_SERVICE = 1U << 9,
    QM_CLOUD_LOG_STATS = 1U << 10,
    QM_CLOUD_LOG_ALL = QM_CLOUD_LOG_ERROR |
                       QM_CLOUD_LOG_LIFECYCLE |
                       QM_CLOUD_LOG_CONTROL |
                       QM_CLOUD_LOG_UPLINK |
                       QM_CLOUD_LOG_DOWNLINK |
                       QM_CLOUD_LOG_PAYLOAD |
                       QM_CLOUD_LOG_AUTH |
                       QM_CLOUD_LOG_OTA |
                       QM_CLOUD_LOG_SUBDEV |
                       QM_CLOUD_LOG_SERVICE |
                       QM_CLOUD_LOG_STATS,
} qm_cloud_log_category_t;

uint32_t qm_cloud_get_log_flags(void);
int qm_cloud_set_log_flags(uint32_t log_flags);

#ifdef __cplusplus
}
#endif

#ifndef CONFIG_QM_CLOUD_LOG_CONFIG
#define CONFIG_QM_CLOUD_LOG_CONFIG 0
#endif

#if CONFIG_QM_CLOUD_LOG_CONFIG
#ifndef CONFIG_QM_CLOUD_LOG_ERROR
#define CONFIG_QM_CLOUD_LOG_ERROR 0
#endif
#ifndef CONFIG_QM_CLOUD_LOG_LIFECYCLE
#define CONFIG_QM_CLOUD_LOG_LIFECYCLE 0
#endif
#ifndef CONFIG_QM_CLOUD_LOG_CONTROL
#define CONFIG_QM_CLOUD_LOG_CONTROL 0
#endif
#ifndef CONFIG_QM_CLOUD_LOG_UPLINK
#define CONFIG_QM_CLOUD_LOG_UPLINK 0
#endif
#ifndef CONFIG_QM_CLOUD_LOG_DOWNLINK
#define CONFIG_QM_CLOUD_LOG_DOWNLINK 0
#endif
#ifndef CONFIG_QM_CLOUD_LOG_PAYLOAD
#define CONFIG_QM_CLOUD_LOG_PAYLOAD 0
#endif
#ifndef CONFIG_QM_CLOUD_LOG_AUTH
#define CONFIG_QM_CLOUD_LOG_AUTH 0
#endif
#ifndef CONFIG_QM_CLOUD_LOG_OTA
#define CONFIG_QM_CLOUD_LOG_OTA 0
#endif
#ifndef CONFIG_QM_CLOUD_LOG_SUBDEV
#define CONFIG_QM_CLOUD_LOG_SUBDEV 0
#endif
#ifndef CONFIG_QM_CLOUD_LOG_SERVICE
#define CONFIG_QM_CLOUD_LOG_SERVICE 0
#endif
#ifndef CONFIG_QM_CLOUD_LOG_STATS
#define CONFIG_QM_CLOUD_LOG_STATS 0
#endif
#define QM_CLOUD_KCONFIG_LOG_FLAGS \
    ((CONFIG_QM_CLOUD_LOG_ERROR ? QM_CLOUD_LOG_ERROR : 0U) | \
     (CONFIG_QM_CLOUD_LOG_LIFECYCLE ? QM_CLOUD_LOG_LIFECYCLE : 0U) | \
     (CONFIG_QM_CLOUD_LOG_CONTROL ? QM_CLOUD_LOG_CONTROL : 0U) | \
     (CONFIG_QM_CLOUD_LOG_UPLINK ? QM_CLOUD_LOG_UPLINK : 0U) | \
     (CONFIG_QM_CLOUD_LOG_DOWNLINK ? QM_CLOUD_LOG_DOWNLINK : 0U) | \
     (CONFIG_QM_CLOUD_LOG_PAYLOAD ? QM_CLOUD_LOG_PAYLOAD : 0U) | \
     (CONFIG_QM_CLOUD_LOG_AUTH ? QM_CLOUD_LOG_AUTH : 0U) | \
     (CONFIG_QM_CLOUD_LOG_OTA ? QM_CLOUD_LOG_OTA : 0U) | \
     (CONFIG_QM_CLOUD_LOG_SUBDEV ? QM_CLOUD_LOG_SUBDEV : 0U) | \
     (CONFIG_QM_CLOUD_LOG_SERVICE ? QM_CLOUD_LOG_SERVICE : 0U) | \
     (CONFIG_QM_CLOUD_LOG_STATS ? QM_CLOUD_LOG_STATS : 0U))
#else
#define QM_CLOUD_KCONFIG_LOG_FLAGS (QM_CLOUD_LOG_ERROR | QM_CLOUD_LOG_LIFECYCLE)
#endif

#define QM_CLOUD_LOG_ENABLED(category) \
    ((qm_cloud_get_log_flags() & (category)) != 0U)
#define QM_CLOUD_LOGE(tag, fmt, ...) \
    do { if (QM_CLOUD_LOG_ENABLED(QM_CLOUD_LOG_ERROR)) QM_LOGE((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_CLOUD_LOGW(tag, category, fmt, ...) \
    do { if (QM_CLOUD_LOG_ENABLED(category)) QM_LOGW((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_CLOUD_LOGI(tag, category, fmt, ...) \
    do { if (QM_CLOUD_LOG_ENABLED(category)) QM_LOGI((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_CLOUD_LOGD(tag, category, fmt, ...) \
    do { if (QM_CLOUD_LOG_ENABLED(category)) QM_LOGD((tag), fmt, ##__VA_ARGS__); } while (0)

#endif
