/**
 * @file joyinside_client_auth.h
 * @brief JoyInside HTTP authentication and device registration API. / JoyInside HTTP 鉴权和设备注册 API。
 *
 * All request pointers are borrowed for the synchronous function call. Output
 * lengths exclude the terminating NUL byte.
 */

#ifndef JOYINSIDE_CLIENT_AUTH_H
#define JOYINSIDE_CLIENT_AUTH_H

#include "qm_errno.h"
#include "qm_http_client.h"
#include "qm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief JoyInside Client access signature text length and required buffer size. / JoyInside 访问签名文本长度与所需缓冲区大小。
 */
#define JOYINSIDE_CLIENT_SIGNATURE_HEX_LEN 32U
#define JOYINSIDE_CLIENT_SIGNATURE_BUFFER_LEN (JOYINSIDE_CLIENT_SIGNATURE_HEX_LEN + 1U)

/**
 * @brief JoyInside Client HTTP auth/register endpoint configuration. / JoyInside HTTP 鉴权与注册端点配置。
 */
typedef struct {
    const char *token_url;                /*!< Token URL, NULL uses JoyInside Client default */
    const char *refresh_url;              /*!< Refresh URL, NULL uses /auth/refreshToken */
    const char *register_url;             /*!< Device register URL, NULL uses JoyInside Client default */
    const qm_http_tls_config_t *http_tls; /*!< TLS config borrowed for the request */
} joyinside_client_auth_endpoint_config_t;

/**
 * @brief JoyInside Client credential configuration. / JoyInside 访问凭据配置。
 */
typedef struct {
    const char *access_key_id;            /*!< JoyInside Client AccessKeyId */
    const char *access_key_secret;        /*!< JoyInside Client AccessKeySecret */
    const char *vendor_id;                /*!< Vendor ID, required for register and vendor token */
    const char *product_id;               /*!< Product ID, required for register */
} joyinside_client_credentials_config_t;

/**
 * @brief JoyInside Client device identity used for registration. / 用于设备注册的 JoyInside 设备身份。
 */
typedef struct {
    const char *type;                     /*!< Device type, e.g. PHYSICAL_ROBOT or APP_ROBOT */
    const char *name;                     /*!< Device display name */
    const char *device_id;                /*!< Device unique ID */
    const char *device_model;             /*!< Optional device model */
    const char *timbre_id;                /*!< Optional timbre ID */
    const char *desc;                     /*!< Optional description */
    const char *const *resource_authed_list; /*!< Optional provider names, borrowed during registration. / 注册期间借用的可选资源提供方数组。 */
    uint32_t resource_authed_count;         /*!< Number of provider names. / 资源提供方数量。 */
} joyinside_client_device_config_t;

/**
 * @brief Token request configuration for one-shot token retrieval. / 单次获取 Token 的请求配置。
 */
typedef struct {
    joyinside_client_auth_endpoint_config_t endpoint; /*!< Token endpoint and HTTP TLS options */
    joyinside_client_credentials_config_t credentials; /*!< Access credentials */
    const char *bot_id;                            /*!< Bot ID for bot token, NULL uses vendor token */
    uint32_t timeout_ms;                           /*!< HTTP timeout in milliseconds, 0 uses default */
    int max_retries;                               /*!< HTTP retry count, 0 uses no retry */
    uint32_t base_delay_ms;                        /*!< Retry base delay in milliseconds */
    uint32_t response_size;                        /*!< Response body buffer size, 0 uses default */
} joyinside_client_token_request_t;

/**
 * @brief Extended token response fields parsed from getToken/refreshToken. / 从 getToken/refreshToken 解析出的扩展 Token 返回字段。
 *
 * refresh_token uses the same caller-provided buffer pattern as the token
 * output: pass a non-NULL buffer with capacity in refresh_token_len to copy
 * the value, or NULL to query the required length only.
 */
typedef struct {
    uint32_t expire_in_sec;                        /*!< accessToken remaining seconds, 0 if absent */
    char *refresh_token;                           /*!< Output buffer, or NULL to query length */
    uint32_t *refresh_token_len;                   /*!< In: capacity, out: length without '\0' */
    uint32_t refresh_expire_in_sec;                /*!< refreshToken remaining seconds, 0 if absent */
} joyinside_client_token_extra_t;

/**
 * @brief Token refresh request configuration. / Token 刷新请求配置。
 *
 * The refresh endpoint does not participate in HMAC-MD5 signature. Per the
 * handbook some pages require a Bearer header while others do not, so the
 * header is an explicit opt-in and must be fixed by the target environment.
 *
 * @par 中文说明
 * refresh 接口不参与 HMAC-MD5 签名。官方文档对 Bearer 请求头的要求存在环境差异，因此由 use_bearer
 * 显式控制，产品应为目标环境固定该策略。
 */
typedef struct {
    joyinside_client_auth_endpoint_config_t endpoint; /*!< Refresh endpoint and HTTP TLS options */
    joyinside_client_credentials_config_t credentials; /*!< Access credentials, access_key_id required */
    const char *refresh_token;                     /*!< Refresh token returned by getToken, required */
    const char *bot_id;                            /*!< Bot ID, NULL uses vendor token */
    qm_bool use_bearer;                            /*!< Whether to send Authorization: Bearer header */
    const char *bearer_token;                      /*!< Bearer token when use_bearer is QM_TRUE */
    uint32_t timeout_ms;                           /*!< HTTP timeout in milliseconds, 0 uses default */
    int max_retries;                               /*!< HTTP retry count, 0 uses no retry */
    uint32_t base_delay_ms;                        /*!< Retry base delay in milliseconds */
    uint32_t response_size;                        /*!< Response body buffer size, 0 uses default */
} joyinside_client_refresh_request_t;

/**
 * @brief Device user binding request configuration. / 设备用户绑定请求配置。
 *
 * Used by bindUser, queryBotBind and unbindUser. The request carries the
 * Bearer accessToken; bot_id and request_id are always required, uid is
 * required for bind/unbind.
 *
 * @par 中文说明
 * 用于 bindUser、queryBotBind、unbindUser。请求携带 Bearer accessToken；bot_id、request_id 必填，
 * bind/unbind 还必须提供 uid。
 */
typedef struct {
    joyinside_client_auth_endpoint_config_t endpoint; /*!< HTTP TLS and endpoint URL overrides */
    const char *bind_url;                        /*!< Optional, NULL uses /device/bindUser */
    const char *query_url;                       /*!< Optional, NULL uses /device/queryBotBind */
    const char *unbind_url;                      /*!< Optional, NULL uses /device/unbindUser */
    const char *access_token;                    /*!< Access token for Authorization: Bearer */
    const char *uid;                             /*!< User ID, bind/unbind */
    const char *bot_id;                          /*!< Bot ID, required */
    const char *request_id;                      /*!< Request ID, required */
    qm_bool delete_memory;                       /*!< unbindUser: also delete long-term memory */
    uint32_t timeout_ms;                         /*!< HTTP timeout in milliseconds, 0 uses default */
    int max_retries;                             /*!< HTTP retry count, 0 uses no retry */
    uint32_t base_delay_ms;                      /*!< Retry base delay in milliseconds */
    uint32_t response_size;                      /*!< Response body buffer size, 0 uses default */
} joyinside_client_user_bind_request_t;

/**
 * @brief Device register request configuration. / 设备注册请求配置。
 */
typedef struct {
    joyinside_client_auth_endpoint_config_t endpoint; /*!< Register endpoint and HTTP TLS options */
    joyinside_client_credentials_config_t credentials; /*!< Access credentials */
    joyinside_client_device_config_t device;           /*!< Device identity to register */
    uint32_t timeout_ms;                           /*!< HTTP timeout in milliseconds, 0 uses default */
    int max_retries;                               /*!< HTTP retry count, 0 uses no retry */
    uint32_t base_delay_ms;                        /*!< Retry base delay in milliseconds */
    uint32_t response_size;                        /*!< Response body buffer size, 0 uses default */
} joyinside_client_register_request_t;

/**
 * @brief Generate JoyInside Client HMAC-MD5 access signature. / 生成 JoyInside HMAC-MD5 访问签名。
 *
 * @param[in] access_key_id JoyInside Client AccessKeyId
 * @param[in] access_timestamp Millisecond timestamp string
 * @param[in] access_nonce Nonce string
 * @param[in] access_secret JoyInside Client AccessKeySecret
 * @param[out] out_hex Output buffer for JOYINSIDE_CLIENT_SIGNATURE_HEX_LEN lowercase hex chars plus '\0'
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_generate_signature(const char *access_key_id,
                                    const char *access_timestamp,
                                    const char *access_nonce,
                                    const char *access_secret,
                                    char out_hex[JOYINSIDE_CLIENT_SIGNATURE_BUFFER_LEN]);

/**
 * @brief Request JoyInside Client access token. / 请求 JoyInside 访问 Token。
 *
 * @param[in] request Token request configuration
 * @param[out] token Output token buffer, or NULL to query required length
 * @param[in,out] token_len Input buffer capacity, output token length without '\0'
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_request_token(const joyinside_client_token_request_t *request,
                               char *token,
                               uint32_t *token_len);

/**
 * @brief Request JoyInside Client access token and extended token fields. / 请求访问 Token 及扩展 Token 字段。
 *
 * Same behavior as joyinside_client_request_token(), additionally parsing
 * expireIn/refreshToken/refreshExpireIn into @p extra when non-NULL.
 *
 * @par 中文说明
 * 与 joyinside_client_request_token() 的行为相同；extra 非 NULL 时还会解析 expireIn、refreshToken、
 * refreshExpireIn。
 *
 * @param[in] request Token request configuration
 * @param[out] token Output token buffer, or NULL to query required length
 * @param[in,out] token_len Input buffer capacity, output token length without '\0'
 * @param[out] extra Optional extended token response fields
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_request_token_ex(const joyinside_client_token_request_t *request,
                                  char *token,
                                  uint32_t *token_len,
                                  joyinside_client_token_extra_t *extra);

/**
 * @brief Refresh JoyInside Client access token. / 刷新 JoyInside 访问 Token。
 *
 * POST /auth/refreshToken with accessKeyId, refreshToken and botId/vendorId.
 * The refreshed accessToken is written to @p token; the response may or may
 * not contain a new refreshToken depending on the target environment, so
 * @p extra is only filled with fields the server actually returns.
 *
 * @par 中文说明
 * 刷新 Token 写入 token。不同目标环境可能不返回新的 refreshToken，因此 extra 只填充服务端实际返回的字段。
 *
 * @param[in] request Refresh request configuration
 * @param[out] token Output token buffer, or NULL to query required length
 * @param[in,out] token_len Input buffer capacity, output token length without '\0'
 * @param[out] extra Optional extended token response fields
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_refresh_token(const joyinside_client_refresh_request_t *request,
                               char *token,
                               uint32_t *token_len,
                               joyinside_client_token_extra_t *extra);

/**
 * @brief Bind a user to a bot (POST /device/bindUser). / 将用户绑定到设备 Bot。
 *
 * @param[in] request Binding request, uid and bot_id required
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_bind_user(const joyinside_client_user_bind_request_t *request);

/**
 * @brief Query the bound user of a bot (POST /device/queryBotBind). / 查询设备 Bot 当前绑定的用户。
 *
 * @param[in] request Query request, bot_id required
 * @param[out] uid Bound UID buffer, or NULL to query its required length
 * @param[in,out] uid_len Input capacity, output length excluding NUL
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_query_bot_bind(const joyinside_client_user_bind_request_t *request,
                                    char *uid, uint32_t *uid_len);

/**
 * @brief Unbind a user from a bot (POST /device/unbindUser). / 解除用户与设备 Bot 的绑定。
 *
 * @param[in] request Unbind request, uid and bot_id required; delete_memory
 *            also removes long-term memory
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_unbind_user(const joyinside_client_user_bind_request_t *request);

/**
 * @brief Register a JoyInside Client device and return Bot ID. / 注册 JoyInside 设备并返回 Bot ID。
 *
 * @param[in] request Register request configuration
 * @param[out] bot_id Output Bot ID buffer, or NULL to query required length
 * @param[in,out] bot_id_len Input buffer capacity, output Bot ID length without '\0'
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_register_device(const joyinside_client_register_request_t *request,
                                 char *bot_id,
                                 uint32_t *bot_id_len);

#ifdef __cplusplus
}
#endif

#endif
