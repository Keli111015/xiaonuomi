#ifndef QM_AGENT_LOG_H
#define QM_AGENT_LOG_H

/**
 * @file qm_agent_log.h
 * @brief Runtime logging controls for qm_agent.
 *
 * qm_agent 的运行时日志控制。日志掩码对整个组件生效，不属于某个会话实例；应用可按分类
 * 开启诊断日志，而不影响传输或音频组件自身的日志配置。
 */

#include "qm_types.h"
#include "qm_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Runtime qm_agent log categories.
 *
 * The mask controls logs emitted by qm_agent itself. Logs emitted by a
 * selected transport or another component keep their own component-level
 * configuration.
 *
 * 掩码只控制 qm_agent 自身输出的日志。所选传输适配器和其他组件仍保持各自独立的
 * 组件级日志配置。
 */
typedef enum {
    /** @brief No qm_agent logs. 不输出 qm_agent 日志。 */
    QM_AGENT_LOG_NONE      = 0U,
    /** @brief Error paths. 错误路径。 */
    QM_AGENT_LOG_ERROR     = 1U << 0,
    /** @brief Create/start/stop/destroy transitions. 创建、启动、停止和销毁状态转换。 */
    QM_AGENT_LOG_LIFECYCLE = 1U << 1,
    /** @brief Serialized control commands. 串行控制命令。 */
    QM_AGENT_LOG_CONTROL   = 1U << 2,
    /** @brief Upstream audio/video admission. 上行音视频准入。 */
    QM_AGENT_LOG_UPLINK    = 1U << 3,
    /** @brief Downstream media delivery. 下行媒体交付。 */
    QM_AGENT_LOG_DOWNLINK  = 1U << 4,
    /** @brief Metadata and payload diagnostics. 元数据与载荷诊断。 */
    QM_AGENT_LOG_PAYLOAD   = 1U << 5,
    /** @brief Counters and timing diagnostics. 计数器与时序诊断。 */
    QM_AGENT_LOG_STATS     = 1U << 6,
    /** @brief All qm_agent log categories. 所有 qm_agent 日志分类。 */
    QM_AGENT_LOG_ALL       = QM_AGENT_LOG_ERROR |
                             QM_AGENT_LOG_LIFECYCLE |
                             QM_AGENT_LOG_CONTROL |
                             QM_AGENT_LOG_UPLINK |
                             QM_AGENT_LOG_DOWNLINK |
                             QM_AGENT_LOG_PAYLOAD |
                             QM_AGENT_LOG_STATS,
} qm_agent_log_flags_t;

/** @brief Get the current component-wide log mask. 获取当前组件级日志掩码。 */
uint32_t qm_agent_get_log_flags(void);
/** @brief Set the component-wide log mask. 设置组件级日志掩码。 */
int qm_agent_set_log_flags(uint32_t log_flags);

#ifdef __cplusplus
}
#endif

#ifndef CONFIG_QM_AGENT_LOG_CONFIG
#define CONFIG_QM_AGENT_LOG_CONFIG 0
#endif

#if CONFIG_QM_AGENT_LOG_CONFIG
#ifndef CONFIG_QM_AGENT_LOG_ERROR
#define CONFIG_QM_AGENT_LOG_ERROR 0
#endif
#ifndef CONFIG_QM_AGENT_LOG_LIFECYCLE
#define CONFIG_QM_AGENT_LOG_LIFECYCLE 0
#endif
#ifndef CONFIG_QM_AGENT_LOG_CONTROL
#define CONFIG_QM_AGENT_LOG_CONTROL 0
#endif
#ifndef CONFIG_QM_AGENT_LOG_UPLINK
#define CONFIG_QM_AGENT_LOG_UPLINK 0
#endif
#ifndef CONFIG_QM_AGENT_LOG_DOWNLINK
#define CONFIG_QM_AGENT_LOG_DOWNLINK 0
#endif
#ifndef CONFIG_QM_AGENT_LOG_PAYLOAD
#define CONFIG_QM_AGENT_LOG_PAYLOAD 0
#endif
#ifndef CONFIG_QM_AGENT_LOG_STATS
#define CONFIG_QM_AGENT_LOG_STATS 0
#endif
#define QM_AGENT_KCONFIG_LOG_FLAGS \
    ((CONFIG_QM_AGENT_LOG_ERROR ? QM_AGENT_LOG_ERROR : 0U) | \
     (CONFIG_QM_AGENT_LOG_LIFECYCLE ? QM_AGENT_LOG_LIFECYCLE : 0U) | \
     (CONFIG_QM_AGENT_LOG_CONTROL ? QM_AGENT_LOG_CONTROL : 0U) | \
     (CONFIG_QM_AGENT_LOG_UPLINK ? QM_AGENT_LOG_UPLINK : 0U) | \
     (CONFIG_QM_AGENT_LOG_DOWNLINK ? QM_AGENT_LOG_DOWNLINK : 0U) | \
     (CONFIG_QM_AGENT_LOG_PAYLOAD ? QM_AGENT_LOG_PAYLOAD : 0U) | \
     (CONFIG_QM_AGENT_LOG_STATS ? QM_AGENT_LOG_STATS : 0U))
#else
#define QM_AGENT_KCONFIG_LOG_FLAGS \
    (QM_AGENT_LOG_ERROR | QM_AGENT_LOG_LIFECYCLE)
#endif

#define QM_AGENT_LOG_FLAGS_ENABLED(category) \
    ((qm_agent_get_log_flags() & (category)) != 0U)

#define QM_AGENT_LOGE(tag, fmt, ...) \
    do { if (QM_AGENT_LOG_FLAGS_ENABLED(QM_AGENT_LOG_ERROR)) \
        QM_LOGE((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_AGENT_LOGW(tag, category, fmt, ...) \
    do { if (QM_AGENT_LOG_FLAGS_ENABLED(category)) \
        QM_LOGW((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_AGENT_LOGI(tag, category, fmt, ...) \
    do { if (QM_AGENT_LOG_FLAGS_ENABLED(category)) \
        QM_LOGI((tag), fmt, ##__VA_ARGS__); } while (0)
#define QM_AGENT_LOGD(tag, category, fmt, ...) \
    do { if (QM_AGENT_LOG_FLAGS_ENABLED(category)) \
        QM_LOGD((tag), fmt, ##__VA_ARGS__); } while (0)

#endif
