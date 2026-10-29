/**
 * @file qm_audio_cloud_protocol.h
 * @brief Default cloud player protocol codec API. / 默认云端播放器协议编解码接口。
 *
 * Most applications use qm_audio_cloud_init() and do not call these helpers
 * directly. They are public so protocol tests or a transport replacement can
 * validate the stable player JSON mapping without depending on Control.
 * / 大多数应用使用 qm_audio_cloud_init()，不会直接调用这些辅助函数。它们保持公开，
 * 以便协议测试或传输替换方在不依赖 Control 的情况下校验稳定的播放器 JSON 映射。
 *
 * This is a pure JSON codec. It neither performs qm_cloud I/O nor calls
 * qm_audio_control APIs. Transport registration, command serialization, and
 * asynchronous status reporting belong to qm_audio_cloud.c. / 这是纯 JSON 编解码器，
 * 既不执行 qm_cloud I/O，也不调用 qm_audio_control API。传输注册、命令序列化和
 * 异步状态上报属于 qm_audio_cloud.c。
 */
#ifndef QM_AUDIO_CLOUD_PROTOCOL_H
#define QM_AUDIO_CLOUD_PROTOCOL_H

#include "cloud/qm_audio_cloud.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Consume one decoded player command. / 消费一条已解码的播放器命令。
 *
 * All command strings are borrowed from the decoder's JSON tree. They are
 * valid only until this callback returns and must be copied before queuing or
 * retaining. The callback runs synchronously in the caller's task context.
 * Return QM_EOK on success or a negative qm_errno value to reject the command.
 * / 所有命令字符串都借用自解码器的 JSON 树，仅在本回调返回前有效；入队或保留前
 * 必须复制。回调在调用方的任务上下文中同步执行。成功返回 QM_EOK，拒绝命令返回
 * 负的 qm_errno 值。
 * @param[in] command Borrowed decoded command. / 借用的已解码命令。
 * @param[in] user_ctx Borrowed callback context. / 借用的回调上下文。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK，失败返回负的 QUMI 错误码。
 */
typedef int (*qm_audio_cloud_protocol_command_cb_t)(const qm_audio_cloud_command_t *command, void *user_ctx);

/**
 * @brief Decode a JSON player command. / 解码一条 JSON 播放器命令。
 *
 * payload may be an MQTT payload without a trailing NUL; payload_len is the
 * exact byte length consumed by cJSON. The decoder owns all parsed JSON
 * storage and releases it after the callback returns. The callback result is
 * returned for a syntactically valid command; malformed or incomplete player
 * commands return -QM_EINVAL. / payload 可能是不带末尾 NUL 的 MQTT 载荷；
 * payload_len 是 cJSON 消费的精确字节数。解码器拥有全部解析出的 JSON 存储，并在
 * 回调返回后释放。语法有效的命令返回回调结果；格式错误或不完整的播放器命令返回
 * -QM_EINVAL。
 * @param[in] payload Borrowed UTF-8 JSON bytes. / 借用的 UTF-8 JSON 字节。
 * @param[in] payload_len Exact byte count, excluding an optional NUL. / 精确的字节数，不含可选 NUL。
 * @param[in] callback Synchronous command consumer. / 同步命令消费回调。
 * @param[in] user_ctx Borrowed callback context, possibly NULL. / 借用的回调上下文，可为 NULL。
 * @return QM_EOK or the callback's negative error for a valid command;
 * -QM_EINVAL for invalid arguments or unsupported/malformed JSON. / 有效命令返回
 * QM_EOK 或回调的负错误码；参数无效或 JSON 不支持/格式错误返回 -QM_EINVAL。
 */
int qm_audio_cloud_protocol_decode(const char *payload, uint32_t payload_len, qm_audio_cloud_protocol_command_cb_t callback, void *user_ctx);

/**
 * @brief Control status and metadata for protocol reporting. / 用于协议上报的 Control 状态与元数据。
 *
 * Scalar values are copied while encoding. String fields are borrowed only for
 * the duration of qm_audio_cloud_protocol_encode_status(); the codec emits
 * them as empty JSON strings when NULL and does not resolve product resources.
 * / 编码时复制标量值。字符串字段仅在 qm_audio_cloud_protocol_encode_status()
 * 执行期间借用；为 NULL 时编解码器输出空 JSON 字符串，且不解析产品资源。
 */
typedef struct {
    /** @brief Control operation ID; zero means no active operation. / Control 操作 ID；零表示没有活跃操作。 */
    qm_audio_operation_id_t operation_id;
    /** @brief Current normalized Control state. / 当前标准化 Control 状态。 */
    qm_audio_control_state_t state;
    /** @brief Latest Control result. / 最近的 Control 结果。 */
    int result;
    /** @brief Hardware-observed or cached position in milliseconds. / 硬件观测或缓存的位置（毫秒）。 */
    uint32_t position_ms;
    /** @brief Product title associated with this operation, may be NULL. / 与该操作关联的产品标题，可为 NULL。 */
    const char *title;
    /** @brief Product codec label associated with this operation, may be NULL. / 与该操作关联的产品编解码标签，可为 NULL。 */
    const char *codec;
    /** @brief Declared resource size; zero when unknown. / 声明的资源大小；未知时为零。 */
    uint32_t file_size;
    /** @brief Declared duration in seconds; zero when unknown. / 声明的时长（秒）；未知时为零。 */
    uint32_t duration_s;
    /** @brief Source type used to derive protocol location type. / 用于推导协议位置类型的输入源类型。 */
    qm_audio_source_type_t source_type;
} qm_audio_cloud_protocol_status_t;

/**
 * @brief Map a QUMI result to a protocol reply code. / 将 QUMI 结果映射为协议回复码。
 *
 * The returned value is a protocol result, not a qm_errno value: 0 success,
 * 1 invalid request/resource, 2 unsupported capability, 4 busy/interrupted,
 * and 7 other failures. / 返回值是协议结果而非 qm_errno 值：0 成功，1 无效请求/
 * 资源，2 能力不支持，4 忙/被打断，7 其他失败。
 * @param[in] result QUMI result to map. / 待映射的 QUMI 结果。
 * @return Protocol reply code. / 协议回复码。
 */
int qm_audio_cloud_protocol_result_code(int result);

/**
 * @brief Allocate a JSON status payload. / 分配一个 JSON 状态载荷。
 *
 * The returned string is allocated by cJSON and must be released with
 * qm_audio_cloud_protocol_free_payload(). NULL denotes invalid input,
 * allocation failure, or an unavailable protocol build. The function is safe
 * to call from the cloud worker task; it performs no network transmission.
 * / 返回的字符串由 cJSON 分配，必须用 qm_audio_cloud_protocol_free_payload()
 * 释放。NULL 表示输入无效、分配失败或协议构建不可用。本函数可从云任务安全调用，
 * 不执行任何网络传输。
 * @param[in] status Status and metadata to encode. / 待编码的状态和元数据。
 * @return cJSON-allocated payload, or NULL on failure. / cJSON 分配的载荷；失败时为 NULL。
 */
char *qm_audio_cloud_protocol_encode_status(const qm_audio_cloud_protocol_status_t *status);

/**
 * @brief Release a payload allocated by the protocol codec. / 释放由协议编解码器分配的载荷。
 * @param[in] payload cJSON-owned string, or NULL. No other allocator is valid. / cJSON 拥有的字符串，可为 NULL；不得使用其他分配器。
 */
void qm_audio_cloud_protocol_free_payload(char *payload);

#ifdef __cplusplus
}
#endif

#endif /* QM_AUDIO_CLOUD_PROTOCOL_H */
