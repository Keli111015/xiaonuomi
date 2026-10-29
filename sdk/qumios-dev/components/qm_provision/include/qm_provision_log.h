#ifndef QM_PROVISION_LOG_H
#define QM_PROVISION_LOG_H

#include "qm_log.h"
#include "qm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Runtime qm_provision log categories. */
typedef enum {
    QM_PROVISION_LOG_NONE      = 0U,
    QM_PROVISION_LOG_ERROR     = 1U << 0,
    QM_PROVISION_LOG_LIFECYCLE = 1U << 1,
    QM_PROVISION_LOG_PROTOCOL  = 1U << 2,
    QM_PROVISION_LOG_ALL       = QM_PROVISION_LOG_ERROR |
                                 QM_PROVISION_LOG_LIFECYCLE |
                                 QM_PROVISION_LOG_PROTOCOL,
} qm_provision_log_flags_t;

uint32_t qm_provision_get_log_flags(void);
int qm_provision_set_log_flags(uint32_t log_flags);

#ifdef __cplusplus
}
#endif

#ifndef CONFIG_QM_PROVISION_LOG_CONFIG
#define CONFIG_QM_PROVISION_LOG_CONFIG 0
#endif

#if CONFIG_QM_PROVISION_LOG_CONFIG
#ifndef CONFIG_QM_PROVISION_LOG_ERROR
#define CONFIG_QM_PROVISION_LOG_ERROR 0
#endif
#ifndef CONFIG_QM_PROVISION_LOG_LIFECYCLE
#define CONFIG_QM_PROVISION_LOG_LIFECYCLE 0
#endif
#ifndef CONFIG_QM_PROVISION_LOG_PROTOCOL
#define CONFIG_QM_PROVISION_LOG_PROTOCOL 0
#endif
#define QM_PROVISION_KCONFIG_LOG_FLAGS \
    ((CONFIG_QM_PROVISION_LOG_ERROR ? QM_PROVISION_LOG_ERROR : 0U) | \
     (CONFIG_QM_PROVISION_LOG_LIFECYCLE ? QM_PROVISION_LOG_LIFECYCLE : 0U) | \
     (CONFIG_QM_PROVISION_LOG_PROTOCOL ? QM_PROVISION_LOG_PROTOCOL : 0U))
#else
#define QM_PROVISION_KCONFIG_LOG_FLAGS \
    (QM_PROVISION_LOG_ERROR | QM_PROVISION_LOG_LIFECYCLE)
#endif

#define QM_PROVISION_LOG_ENABLED(category) \
    ((qm_provision_get_log_flags() & (category)) != 0U)
#define QM_PROVISION_LOGE(tag, fmt, ...) \
    do { if (QM_PROVISION_LOG_ENABLED(QM_PROVISION_LOG_ERROR)) \
        QM_LOGE((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_PROVISION_LOGW(tag, category, fmt, ...) \
    do { if (QM_PROVISION_LOG_ENABLED(category)) \
        QM_LOGW((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_PROVISION_LOGI(tag, category, fmt, ...) \
    do { if (QM_PROVISION_LOG_ENABLED(category)) \
        QM_LOGI((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_PROVISION_LOGD(tag, category, fmt, ...) \
    do { if (QM_PROVISION_LOG_ENABLED(category)) \
        QM_LOGD((tag), fmt, ##__VA_ARGS__); } while (0)

#endif /* QM_PROVISION_LOG_H */
