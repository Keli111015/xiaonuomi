#ifndef QM_AGENT_AUTH_H
#define QM_AGENT_AUTH_H

/**
 * @file qm_agent_auth.h
 * @brief Agent authentication state and operations owned by qm_agent.
 *
 * Authentication is owned and scheduled by qm_agent.  Applications configure
 * authentication through qm_agent_config_t and do not call these functions
 * directly.
 *
 * 认证由 qm_agent 所有并调度。应用只通过 qm_agent_config_t 配置认证；认证回复中的
 * 字符串由组件深拷贝，调用方取得副本后必须调用 qm_agent_auth_info_deinit() 释放。
 */

#include "qm_agent_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Upstream method used to request agent authentication credentials. 上行认证凭据请求方法。 */
#define QM_AGENT_AUTH_METHOD_GET_AGENT_TOKEN "getAgentToken"

/** @brief Downstream method used to deliver agent authentication credentials. 下行认证凭据回复方法。 */
#define QM_AGENT_AUTH_METHOD_GET_AGENT_TOKEN_REPLY "getAgentTokenReply"

/** @brief LJYD authentication payload. LJYD 认证载荷，字符串由 qm_agent_auth_info_t 所有。 */
typedef struct {
    /** @brief LJYD token string, or NULL when absent. */
    char *token;

    /** @brief LJYD user id (userId), or NULL when absent. */
    char *user_id;

    /** @brief Token expiry time in seconds, or zero when absent. */
    uint32_t expire_time;
} qm_agent_auth_ljyd_auth_t;

/** @brief JoyInside authentication payload. JoyInside 认证载荷，字符串由 qm_agent_auth_info_t 所有。 */
typedef struct {
    /** @brief JoyInside access token, or NULL when absent. */
    char *access_token;

    /** @brief JoyInside bot identifier, or NULL when absent. */
    char *bot_id;

    /** @brief Token expiry time in seconds, or zero when absent. */
    uint32_t expire_time;
} qm_agent_auth_joyinside_auth_t;

/** @brief Agora authentication payload. Agora 认证载荷，字符串由 qm_agent_auth_info_t 所有。 */
typedef struct {
    /** @brief Agora license string, or NULL when absent. */
    char *license;

    /** @brief Agora RTC token string, or NULL when absent. */
    char *token;

    /** @brief Agora application identifier, or NULL when absent. */
    char *app_id;

    /** @brief Agora local user identifier, or zero when absent. */
    uint32_t uid;

    /** @brief Agora channel name, or NULL when absent. */
    char *channel_name;

    /** @brief Token expiry time in seconds, or zero when absent. */
    uint32_t expire_time;
} qm_agent_auth_agora_auth_t;

/** @brief Baidu baidu_chat_agent authentication payload. Baidu baidu_chat_agent 认证载荷，字符串由 qm_agent_auth_info_t 所有。 */
typedef struct {
    /** @brief Purchased Baidu license key, or NULL when absent. */
    char *license_key;

    /**
     * @brief Baidu realtime interaction instance identifier, or NULL when absent.
     *
     * Stored as a string because the upstream value may exceed 32-bit integer
     * range.
     */
    char *instance_id;

    /** @brief Baidu open platform application identifier, or NULL when absent. */
    char *app_id;

    /** @brief Baidu realtime interaction audio codec string, or NULL when absent. */
    char *audio_codec;

    /** @brief Baidu business access token, or NULL when absent. */
    char *token;

    /** @brief Token expiry time in seconds, or zero when absent. */
    uint32_t expire_time;
} qm_agent_auth_baidu_auth_t;

/** @brief Active authentication payload type. 当前生效的认证载荷类型。 */
typedef enum {
    /** @brief No authentication payload is active. */
    QM_AGENT_AUTH_TYPE_NONE = 0,

    /** @brief The active payload is qm_agent_auth_joyinside_auth_t. */
    QM_AGENT_AUTH_TYPE_JOYINSIDE,

    /** @brief The active payload is qm_agent_auth_agora_auth_t. */
    QM_AGENT_AUTH_TYPE_AGORA,

    /** @brief The active payload is qm_agent_auth_baidu_auth_t. */
    QM_AGENT_AUTH_TYPE_BAIDU,

    /** @brief The active payload is only the common qm_agent_auth_ljyd_auth_t. */
    QM_AGENT_AUTH_TYPE_LGEINE,
} qm_agent_auth_type_t;

/** @brief Parsed agent authentication reply cached by qm_agent_auth. qm_agent_auth 缓存的已解析认证回复。 */
typedef struct {
    /** @brief Reply message id from getAgentTokenReply. */
    uint32_t msg_id;

    /** @brief Reply business code. Zero means success. */
    int32_t code;

    /** @brief Reply timestamp field, or zero when absent. */
    uint32_t timestamp;

    /** @brief Active authentication payload type. */
    qm_agent_auth_type_t type;

    /** @brief Common LJYD authentication payload. */
    qm_agent_auth_ljyd_auth_t ljyd;

    /** @brief Active transport authentication payload. */
    union {
        /** @brief Parsed joyinside payload. */
        qm_agent_auth_joyinside_auth_t joyinside;

        /** @brief Parsed agora payload. */
        qm_agent_auth_agora_auth_t agora;

        /** @brief Parsed baidu payload. */
        qm_agent_auth_baidu_auth_t baidu;
    } data;
} qm_agent_auth_info_t;

/** @brief Opaque qm_agent_auth context. */
typedef struct qm_agent_auth qm_agent_auth_t;

/**
 * @brief Initialize the internal qm_agent_auth authentication helper.
 *
 * MQTT builds register getAgentTokenReply on owner->config.iot_handle.
 * All requests and retry deadlines are owned by the Agent control task.
 * MQTT 构建会在 owner->config.iot_handle 注册 getAgentTokenReply；请求和重试截止时间均由
 * Agent 控制任务所有。
 *
 * @param owner Agent that owns this helper and its control task.
 * @return Authentication helper context on success, NULL on failure.
 */
qm_agent_auth_t *qm_agent_auth_init(qm_agent_handle_t owner);

/**
 * @brief Deinitialize the qm_agent_auth authentication helper.
 * 反初始化 qm_agent_auth 认证辅助对象。成功返回后不再保留认证回调或重试任务。
 *
 * @param auth Helper context returned by qm_agent_auth_init().
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
int qm_agent_auth_deinit(qm_agent_auth_t *auth);

/**
 * @brief Request agent credentials through the configured auth channel.
 *
 * MQTT builds should request only after qm_cloud reports QM_IOT_EVENT_CLOUD_CONN.
 * HTTP builds use the configured HTTP parameters and do not require cloud conn.
 * MQTT 构建只应在 qm_cloud 报告 QM_IOT_EVENT_CLOUD_CONN 后请求；HTTP 构建使用配置的
 * HTTP 参数，不依赖云端连接事件。
 *
 * @param auth Helper context.
 * @param msg_id Request id. Passing zero lets the qm_iot dev layer assign one.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
int qm_agent_auth_request_token(qm_agent_auth_t *auth, uint32_t msg_id);

/**
 * @brief Copy the most recently parsed authentication reply.
 * 深拷贝最近一次解析成功的认证回复。输出字符串由调用方通过
 * qm_agent_auth_info_deinit() 释放。
 *
 * @param auth Helper context.
 * @param info Output deep-copied authentication info.
 * @return QM_EOK on success, -QM_ENOENT when no cached reply exists,
 *         or negative qm_errno value on failure.
 */
int qm_agent_auth_get_token(qm_agent_auth_t *auth, qm_agent_auth_info_t *info);

/**
 * @brief Release strings owned by a qm_agent_auth_info_t.
 * 释放 qm_agent_auth_info_t 所有的字符串，并清零其内容。
 *
 * @param info Authentication info to release.
 */
void qm_agent_auth_info_deinit(qm_agent_auth_info_t *info);

/** @brief Retry only if generation still identifies the active auth cycle. 仅当 generation 仍对应当前认证周期时重试。 */
int qm_agent_auth_retry_now(qm_agent_auth_t *auth, uint32_t generation);

/** @brief Consume copied MQTT input on the Agent control task. 在 Agent 控制任务中消费已复制的 MQTT 输入。 */
void qm_agent_auth_process_pending(qm_agent_auth_t *auth);

/** @brief Invalidate pending retries while retaining any valid credentials. 使待执行重试失效，同时保留已有有效凭据。 */
void qm_agent_auth_cancel(qm_agent_auth_t *auth);

#ifdef __cplusplus
}
#endif

#endif /* QM_AGENT_AUTH_H */
