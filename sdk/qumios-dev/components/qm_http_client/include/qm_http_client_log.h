#ifndef QM_HTTP_CLIENT_LOG_H
#define QM_HTTP_CLIENT_LOG_H

#include "qm_types.h"
#include "qm_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Runtime qm_http_client log categories. */
typedef enum {
    QM_HTTP_CLIENT_LOG_NONE      = 0U,
    QM_HTTP_CLIENT_LOG_ERROR     = 1U << 0,
    QM_HTTP_CLIENT_LOG_LIFECYCLE = 1U << 1,
    QM_HTTP_CLIENT_LOG_CONTROL   = 1U << 2,
    QM_HTTP_CLIENT_LOG_UPLINK    = 1U << 3,
    QM_HTTP_CLIENT_LOG_DOWNLINK  = 1U << 4,
    QM_HTTP_CLIENT_LOG_PAYLOAD   = 1U << 5,
    QM_HTTP_CLIENT_LOG_STATS     = 1U << 6,
    QM_HTTP_CLIENT_LOG_ALL       = QM_HTTP_CLIENT_LOG_ERROR |
                                   QM_HTTP_CLIENT_LOG_LIFECYCLE |
                                   QM_HTTP_CLIENT_LOG_CONTROL |
                                   QM_HTTP_CLIENT_LOG_UPLINK |
                                   QM_HTTP_CLIENT_LOG_DOWNLINK |
                                   QM_HTTP_CLIENT_LOG_PAYLOAD |
                                   QM_HTTP_CLIENT_LOG_STATS,
} qm_http_client_log_flags_t;

uint32_t qm_http_client_get_log_flags(void);
int qm_http_client_set_log_flags(uint32_t log_flags);

#ifdef __cplusplus
}
#endif

#ifndef CONFIG_QM_HTTP_CLIENT_LOG_CONFIG
#define CONFIG_QM_HTTP_CLIENT_LOG_CONFIG 0
#endif

#if CONFIG_QM_HTTP_CLIENT_LOG_CONFIG
#ifndef CONFIG_QM_HTTP_CLIENT_LOG_ERROR
#define CONFIG_QM_HTTP_CLIENT_LOG_ERROR 0
#endif
#ifndef CONFIG_QM_HTTP_CLIENT_LOG_LIFECYCLE
#define CONFIG_QM_HTTP_CLIENT_LOG_LIFECYCLE 0
#endif
#ifndef CONFIG_QM_HTTP_CLIENT_LOG_CONTROL
#define CONFIG_QM_HTTP_CLIENT_LOG_CONTROL 0
#endif
#ifndef CONFIG_QM_HTTP_CLIENT_LOG_UPLINK
#define CONFIG_QM_HTTP_CLIENT_LOG_UPLINK 0
#endif
#ifndef CONFIG_QM_HTTP_CLIENT_LOG_DOWNLINK
#define CONFIG_QM_HTTP_CLIENT_LOG_DOWNLINK 0
#endif
#ifndef CONFIG_QM_HTTP_CLIENT_LOG_PAYLOAD
#define CONFIG_QM_HTTP_CLIENT_LOG_PAYLOAD 0
#endif
#ifndef CONFIG_QM_HTTP_CLIENT_LOG_STATS
#define CONFIG_QM_HTTP_CLIENT_LOG_STATS 0
#endif
#define QM_HTTP_CLIENT_KCONFIG_LOG_FLAGS \
    ((CONFIG_QM_HTTP_CLIENT_LOG_ERROR ? QM_HTTP_CLIENT_LOG_ERROR : 0U) | \
     (CONFIG_QM_HTTP_CLIENT_LOG_LIFECYCLE ? QM_HTTP_CLIENT_LOG_LIFECYCLE : 0U) | \
     (CONFIG_QM_HTTP_CLIENT_LOG_CONTROL ? QM_HTTP_CLIENT_LOG_CONTROL : 0U) | \
     (CONFIG_QM_HTTP_CLIENT_LOG_UPLINK ? QM_HTTP_CLIENT_LOG_UPLINK : 0U) | \
     (CONFIG_QM_HTTP_CLIENT_LOG_DOWNLINK ? QM_HTTP_CLIENT_LOG_DOWNLINK : 0U) | \
     (CONFIG_QM_HTTP_CLIENT_LOG_PAYLOAD ? QM_HTTP_CLIENT_LOG_PAYLOAD : 0U) | \
     (CONFIG_QM_HTTP_CLIENT_LOG_STATS ? QM_HTTP_CLIENT_LOG_STATS : 0U))
#else
#define QM_HTTP_CLIENT_KCONFIG_LOG_FLAGS \
    (QM_HTTP_CLIENT_LOG_ERROR | QM_HTTP_CLIENT_LOG_LIFECYCLE)
#endif

#define QM_HTTP_CLIENT_LOG_FLAGS_ENABLED(category) \
    ((qm_http_client_get_log_flags() & (category)) != 0U)

#define QM_HTTP_CLIENT_LOGE(tag, fmt, ...) \
    do { if (QM_HTTP_CLIENT_LOG_FLAGS_ENABLED(QM_HTTP_CLIENT_LOG_ERROR)) \
        QM_LOGE((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_HTTP_CLIENT_LOGW(tag, category, fmt, ...) \
    do { if (QM_HTTP_CLIENT_LOG_FLAGS_ENABLED(category)) \
        QM_LOGW((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_HTTP_CLIENT_LOGI(tag, category, fmt, ...) \
    do { if (QM_HTTP_CLIENT_LOG_FLAGS_ENABLED(category)) \
        QM_LOGI((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_HTTP_CLIENT_LOGD(tag, category, fmt, ...) \
    do { if (QM_HTTP_CLIENT_LOG_FLAGS_ENABLED(category)) \
        QM_LOGD((tag), fmt, ##__VA_ARGS__); } while (0)

#endif
