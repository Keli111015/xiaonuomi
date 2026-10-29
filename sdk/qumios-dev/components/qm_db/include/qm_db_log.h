#ifndef QM_DB_LOG_H
#define QM_DB_LOG_H

#include "qm_types.h"
#include "qm_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Runtime qm_db log categories. */
typedef enum {
    QM_DB_LOG_NONE      = 0U,
    QM_DB_LOG_ERROR     = 1U << 0,
    QM_DB_LOG_LIFECYCLE = 1U << 1,
    QM_DB_LOG_CONTROL   = 1U << 2,
    QM_DB_LOG_KV        = 1U << 3,
    QM_DB_LOG_TS        = 1U << 4,
    QM_DB_LOG_FLASH     = 1U << 5,
    QM_DB_LOG_STATS     = 1U << 6,
    QM_DB_LOG_ALL       = QM_DB_LOG_ERROR |
                          QM_DB_LOG_LIFECYCLE |
                          QM_DB_LOG_CONTROL |
                          QM_DB_LOG_KV |
                          QM_DB_LOG_TS |
                          QM_DB_LOG_FLASH |
                          QM_DB_LOG_STATS,
} qm_db_log_flags_t;

uint32_t qm_db_get_log_flags(void);
int qm_db_set_log_flags(uint32_t log_flags);

#ifdef __cplusplus
}
#endif

#ifndef CONFIG_QM_DB_LOG_CONFIG
#define CONFIG_QM_DB_LOG_CONFIG 0
#endif

#if CONFIG_QM_DB_LOG_CONFIG
#ifndef CONFIG_QM_DB_LOG_ERROR
#define CONFIG_QM_DB_LOG_ERROR 0
#endif
#ifndef CONFIG_QM_DB_LOG_LIFECYCLE
#define CONFIG_QM_DB_LOG_LIFECYCLE 0
#endif
#ifndef CONFIG_QM_DB_LOG_CONTROL
#define CONFIG_QM_DB_LOG_CONTROL 0
#endif
#ifndef CONFIG_QM_DB_LOG_KV
#define CONFIG_QM_DB_LOG_KV 0
#endif
#ifndef CONFIG_QM_DB_LOG_TS
#define CONFIG_QM_DB_LOG_TS 0
#endif
#ifndef CONFIG_QM_DB_LOG_FLASH
#define CONFIG_QM_DB_LOG_FLASH 0
#endif
#ifndef CONFIG_QM_DB_LOG_STATS
#define CONFIG_QM_DB_LOG_STATS 0
#endif
#define QM_DB_KCONFIG_LOG_FLAGS \
    ((CONFIG_QM_DB_LOG_ERROR ? QM_DB_LOG_ERROR : 0U) | \
     (CONFIG_QM_DB_LOG_LIFECYCLE ? QM_DB_LOG_LIFECYCLE : 0U) | \
     (CONFIG_QM_DB_LOG_CONTROL ? QM_DB_LOG_CONTROL : 0U) | \
     (CONFIG_QM_DB_LOG_KV ? QM_DB_LOG_KV : 0U) | \
     (CONFIG_QM_DB_LOG_TS ? QM_DB_LOG_TS : 0U) | \
     (CONFIG_QM_DB_LOG_FLASH ? QM_DB_LOG_FLASH : 0U) | \
     (CONFIG_QM_DB_LOG_STATS ? QM_DB_LOG_STATS : 0U))
#else
#define QM_DB_KCONFIG_LOG_FLAGS (QM_DB_LOG_ERROR | QM_DB_LOG_LIFECYCLE)
#endif

#define QM_DB_LOG_ENABLED(category) \
    ((qm_db_get_log_flags() & (category)) != 0U)
#define QM_DB_LOGE(tag, fmt, ...) \
    do { if (QM_DB_LOG_ENABLED(QM_DB_LOG_ERROR)) \
        QM_LOGE((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_DB_LOGW(tag, category, fmt, ...) \
    do { if (QM_DB_LOG_ENABLED(category)) \
        QM_LOGW((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_DB_LOGI(tag, category, fmt, ...) \
    do { if (QM_DB_LOG_ENABLED(category)) \
        QM_LOGI((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_DB_LOGD(tag, category, fmt, ...) \
    do { if (QM_DB_LOG_ENABLED(category)) \
        QM_LOGD((tag), fmt, ##__VA_ARGS__); } while (0)

#endif
