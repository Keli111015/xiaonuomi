#ifndef QM_WEBSOCKET_CLIENT_LOG_H
#define QM_WEBSOCKET_CLIENT_LOG_H

#include "qm_types.h"
#include "qm_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Runtime WebSocket client log categories. */
typedef enum {
    QM_WEBSOCKET_LOG_NONE      = 0U,
    QM_WEBSOCKET_LOG_ERROR     = 1U << 0,
    QM_WEBSOCKET_LOG_LIFECYCLE = 1U << 1,
    QM_WEBSOCKET_LOG_CONTROL   = 1U << 2,
    QM_WEBSOCKET_LOG_TX        = 1U << 3,
    QM_WEBSOCKET_LOG_RX        = 1U << 4,
    QM_WEBSOCKET_LOG_ALL       = QM_WEBSOCKET_LOG_ERROR |
                                 QM_WEBSOCKET_LOG_LIFECYCLE |
                                 QM_WEBSOCKET_LOG_CONTROL |
                                 QM_WEBSOCKET_LOG_TX |
                                 QM_WEBSOCKET_LOG_RX,
} qm_websocket_log_flags_t;

uint32_t qm_websocket_client_get_log_flags(void);
int qm_websocket_client_set_log_flags(uint32_t log_flags);

#ifdef __cplusplus
}
#endif

#ifndef CONFIG_QM_WEBSOCKET_CLIENT_LOG_CONFIG
#define CONFIG_QM_WEBSOCKET_CLIENT_LOG_CONFIG 0
#endif

#if CONFIG_QM_WEBSOCKET_CLIENT_LOG_CONFIG
#ifndef CONFIG_QM_WEBSOCKET_CLIENT_LOG_ERROR
#define CONFIG_QM_WEBSOCKET_CLIENT_LOG_ERROR 0
#endif
#ifndef CONFIG_QM_WEBSOCKET_CLIENT_LOG_LIFECYCLE
#define CONFIG_QM_WEBSOCKET_CLIENT_LOG_LIFECYCLE 0
#endif
#ifndef CONFIG_QM_WEBSOCKET_CLIENT_LOG_CONTROL
#define CONFIG_QM_WEBSOCKET_CLIENT_LOG_CONTROL 0
#endif
#ifndef CONFIG_QM_WEBSOCKET_CLIENT_LOG_TX
#define CONFIG_QM_WEBSOCKET_CLIENT_LOG_TX 0
#endif
#ifndef CONFIG_QM_WEBSOCKET_CLIENT_LOG_RX
#define CONFIG_QM_WEBSOCKET_CLIENT_LOG_RX 0
#endif
#define QM_WEBSOCKET_CLIENT_KCONFIG_LOG_FLAGS \
    ((CONFIG_QM_WEBSOCKET_CLIENT_LOG_ERROR ? QM_WEBSOCKET_LOG_ERROR : 0U) | \
     (CONFIG_QM_WEBSOCKET_CLIENT_LOG_LIFECYCLE ? QM_WEBSOCKET_LOG_LIFECYCLE : 0U) | \
     (CONFIG_QM_WEBSOCKET_CLIENT_LOG_CONTROL ? QM_WEBSOCKET_LOG_CONTROL : 0U) | \
     (CONFIG_QM_WEBSOCKET_CLIENT_LOG_TX ? QM_WEBSOCKET_LOG_TX : 0U) | \
     (CONFIG_QM_WEBSOCKET_CLIENT_LOG_RX ? QM_WEBSOCKET_LOG_RX : 0U))
#else
#define QM_WEBSOCKET_CLIENT_KCONFIG_LOG_FLAGS \
    (QM_WEBSOCKET_LOG_ERROR | QM_WEBSOCKET_LOG_LIFECYCLE)
#endif

#define QM_WEBSOCKET_LOG_FLAGS_ENABLED(category) \
    ((qm_websocket_client_get_log_flags() & (category)) != 0U)

#define QM_WEBSOCKET_LOGE(tag, fmt, ...) \
    do { if (QM_WEBSOCKET_LOG_FLAGS_ENABLED(QM_WEBSOCKET_LOG_ERROR)) \
        QM_LOGE((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_WEBSOCKET_LOGW(tag, category, fmt, ...) \
    do { if (QM_WEBSOCKET_LOG_FLAGS_ENABLED(category)) \
        QM_LOGW((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_WEBSOCKET_LOGI(tag, category, fmt, ...) \
    do { if (QM_WEBSOCKET_LOG_FLAGS_ENABLED(category)) \
        QM_LOGI((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_WEBSOCKET_LOGD(tag, category, fmt, ...) \
    do { if (QM_WEBSOCKET_LOG_FLAGS_ENABLED(category)) \
        QM_LOGD((tag), fmt, ##__VA_ARGS__); } while (0)

#endif
