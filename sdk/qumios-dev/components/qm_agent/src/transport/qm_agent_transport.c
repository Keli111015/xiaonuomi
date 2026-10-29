/**
 * @file qm_agent_transport.c
 * @brief Built-in transport lookup.
 *
 * Custom transports bypass this table by setting qm_agent_config_t::transport_if
 * directly.
 *
 * 内置传输适配器查找表。设置 qm_agent_config_t::transport_if 的自定义传输不经过此表；
 * 仅返回当前 Kconfig 构建中启用的内置适配器。
 */

#include "qm_agent_transport.h"

#if defined(CONFIG_QM_AGENT_TRANSPORT_AGORA) && CONFIG_QM_AGENT_TRANSPORT_AGORA
#define QM_AGENT_HAS_KCONFIG_AGORA 1
#else
#define QM_AGENT_HAS_KCONFIG_AGORA 0
#endif

#if defined(CONFIG_QM_AGENT_TRANSPORT_BAIDU) && CONFIG_QM_AGENT_TRANSPORT_BAIDU
#define QM_AGENT_HAS_KCONFIG_BAIDU 1
#else
#define QM_AGENT_HAS_KCONFIG_BAIDU 0
#endif

#if defined(CONFIG_QM_AGENT_TRANSPORT_JOYINSIDE) && CONFIG_QM_AGENT_TRANSPORT_JOYINSIDE
#define QM_AGENT_HAS_KCONFIG_JOYINSIDE 1
#else
#define QM_AGENT_HAS_KCONFIG_JOYINSIDE 0
#endif

#if defined(CONFIG_QM_AGENT_TRANSPORT_LGEINE) && CONFIG_QM_AGENT_TRANSPORT_LGEINE
#define QM_AGENT_HAS_KCONFIG_LGEINE 1
#else
#define QM_AGENT_HAS_KCONFIG_LGEINE 0
#endif

/**
 * @brief Return the built-in adapter selected by type and current Kconfig.
 * 根据类型和当前 Kconfig 返回内置适配器。
 *
 * A disabled adapter is reported as NULL so Core can reject it before session
 * start. 未启用的适配器返回 NULL，使 Core 能在会话启动前拒绝该配置。
 */
const qm_agent_transport_if_t *qm_agent_transport_get_default(qm_agent_transport_type_t type)
{
    switch (type) {
        case QM_AGENT_TRANSPORT_AGORA:
#if QM_AGENT_HAS_KCONFIG_AGORA
            return qm_agent_transport_agora();
#else
            return NULL;
#endif
        case QM_AGENT_TRANSPORT_BAIDU:
#if QM_AGENT_HAS_KCONFIG_BAIDU
            return qm_agent_transport_baidu_chat_agent();
#else
            return NULL;
#endif
        case QM_AGENT_TRANSPORT_JOYINSIDE:
#if QM_AGENT_HAS_KCONFIG_JOYINSIDE
            return qm_agent_transport_joyinside();
#else
            return NULL;
#endif
        case QM_AGENT_TRANSPORT_LGEINE:
#if QM_AGENT_HAS_KCONFIG_LGEINE
            return qm_agent_transport_lgeine();
#else
            return NULL;
#endif
        default:
            return NULL;
    }
}
