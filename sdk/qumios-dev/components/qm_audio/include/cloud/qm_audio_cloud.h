/**
 * @file qm_audio_cloud.h
 * @brief Optional cloud player adapter API. / 可选的云端播放器适配接口。
 *
 * This module is an optional extension of the qm_audio aggregate directory; it
 * is not part of the Control, Service, Pipeline, or Engine core. It receives
 * the platform player protocol through qm_cloud, translates it to Control
 * operations, and reports resulting Control events asynchronously.
 * Applications still own product resource resolution through the injected
 * callback. The adapter does not expose qm_cloud or JSON types in this API.
 * / 本模块是 qm_audio 聚合目录的可选扩展，不属于 Control、Service、Pipeline 或
 * Engine 核心。它通过 qm_cloud 接收平台播放器协议，转换为 Control 操作，并异步
 * 上报产生的 Control 事件。产品资源解析仍由注入的回调由应用负责。本 API 不暴露
 * qm_cloud 或 JSON 类型。
 */
#ifndef QM_AUDIO_CLOUD_H
#define QM_AUDIO_CLOUD_H

#include "qm_audio_control.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque optional cloud-adapter handle. / 不透明的可选云端适配器句柄。 */
typedef struct qm_audio_cloud qm_audio_cloud_t;

/** @brief Player intents supported by the default cloud protocol. / 默认云端协议支持的播放器意图。 */
typedef enum {
    /** @brief Invalid or unrecognized command. / 无效或无法识别的命令。 */
    QM_AUDIO_CLOUD_COMMAND_INVALID = 0,
    /** @brief Start playback. / 开始播放。 */
    QM_AUDIO_CLOUD_COMMAND_PLAY,
    /** @brief Stop playback. / 停止播放。 */
    QM_AUDIO_CLOUD_COMMAND_STOP,
    /** @brief Apply protocol-specific playback limit. / 应用协议特定的播放限制。 */
    QM_AUDIO_CLOUD_COMMAND_LIMIT,
    /** @brief Pause playback. / 暂停播放。 */
    QM_AUDIO_CLOUD_COMMAND_PAUSE,
    /** @brief Resume playback. / 恢复播放。 */
    QM_AUDIO_CLOUD_COMMAND_RESUME,
    /** @brief Query player status. / 查询播放器状态。 */
    QM_AUDIO_CLOUD_COMMAND_GET,
} qm_audio_cloud_command_type_t;

/**
 * @brief Typed command decoded from the default player protocol. / 从默认播放器协议解码得到的类型化命令。
 *
 * String fields are owned by the protocol decoder and remain valid only while
 * the resolver or policy callback is executing; a callback retaining any field
 * must copy it. msg_id belongs to the cloud protocol and must never be used as
 * a Control request or operation identifier. / 字符串字段归协议解码器所有，仅在
 * 解析器或策略回调执行期间有效；回调若要保留任何字段必须复制。msg_id 属于云端
 * 协议，绝不能用作 Control 请求或操作标识。
 */
typedef struct {
    /** @brief Message ID used only for reply correlation. / 仅用于回复关联的平台消息 ID。 */
    uint32_t msg_id;
    /** @brief Parsed player intent. / 已解析的播放器意图。 */
    qm_audio_cloud_command_type_t type;
    /** @brief Borrowed original protocol submethod. / 借用的原始协议子方法。 */
    const char *submethod;
    /** @brief Whether a numeric protocol resource ID exists. / 是否存在数值协议资源 ID。 */
    qm_bool has_resource_id;
    /** @brief Parsed numeric resource ID when present. / 存在时已解析的数值资源 ID。 */
    uint32_t resource_id;
    /** @brief Optional signed or unsigned resource URI; do not log it verbatim. / 可选的带签名或未签名资源 URI，禁止原样记录日志。 */
    const char *uri;
    /** @brief Optional user-visible resource title. / 可选的用户可见资源标题。 */
    const char *title;
    /** @brief Optional product resource description. / 可选的产品资源描述。 */
    const char *description;
    /** @brief Optional cloud codec or container label. / 可选的云端编解码器或容器标签。 */
    const char *codec;
    /** @brief Optional product bundle or collection ID. / 可选的产品包或集合标识。 */
    const char *resource_bundle;
    /** @brief Optional product guidance text. / 可选的产品引导文本。 */
    const char *guide_words;
    /** @brief Declared resource size; zero means unknown. / 声明的资源大小；零表示未知。 */
    uint32_t file_size;
    /** @brief Declared media duration; zero means unknown. / 声明的媒体时长；零表示未知。 */
    uint32_t duration_s;
} qm_audio_cloud_command_t;

/**
 * @brief Product-resolved playback input consumed by Control. / 由产品解析并由 Control 使用的播放输入。
 *
 * The resolver fills this structure for a PLAY command. URI and file strings
 * are copied by Pipeline during Control submission. Memory buffers and custom
 * source contexts remain borrowed until the release callback runs. The adapter
 * calls release exactly once after a failed submission or after the resulting
 * Control operation is released. / 解析器为 PLAY 命令填充本结构。URI 与文件字符串
 * 在 Control 提交时由 Pipeline 复制。内存缓冲与自定义源上下文在 release 回调
 * 运行前保持借用。适配器在提交失败后或对应 Control 操作被释放后，恰好调用一次
 * release。
 */
typedef struct {
    /** @brief Service priority selected by product policy. / 产品策略选择的 Service 优先级。 */
    qm_audio_priority_t priority;
    /** @brief Admission behavior selected by product policy. / 产品策略选择的准入行为。 */
    qm_audio_admission_policy_t admission_policy;
    /** @brief Admission deadline; zero rejects when not immediate. / 准入截止时间；零值表示无法立即准入时拒绝。 */
    uint32_t acquire_timeout_ms;
    /** @brief Input source passed to Pipeline through Control. / 经由 Control 传递给 Pipeline 的输入源。 */
    qm_audio_source_t source;
    /** @brief Encoded input description; zero value is raw PCM. / 编码输入描述；零值为原始 PCM。 */
    qm_audio_codec_config_t codec;
    /** @brief Product-owned context released after completion. / 操作结束后释放的产品自有上下文。 */
    void *release_ctx;
    /**
     * @brief Optional release callback for borrowed source resources. / 借用源资源的可选释放回调。
     *
     * The callback runs in the cloud worker task, after Control quiesces the
     * operation. It must not call Control lifecycle APIs for that operation.
     * / 回调在 Control 静止该操作后，于云任务中执行。它不得对该操作调用 Control
     * 生命周期 API。
     */
    void (*release)(void *release_ctx);
} qm_audio_cloud_playback_t;

/**
 * @brief Resolve a PLAY command into a Control playback input. / 将 PLAY 命令解析为 Control 播放输入。
 *
 * The callback runs in the cloud worker task. It may perform bounded product
 * resource lookup, but it must not retain the borrowed command strings. A
 * successful result transfers release responsibility to the cloud adapter.
 * / 回调在云任务中执行；可做有界的产品资源查找，但不得保留借用的命令字符串。
 * 成功结果会把释放责任转移给云适配器。
 * @param[in] command Borrowed decoded PLAY command. / 借用的已解码 PLAY 命令。
 * @param[out] playback Product-resolved playback input. / 产品解析的播放输入。
 * @param[in] user_ctx Borrowed product callback context. / 借用的产品回调上下文。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK，失败返回负的 QUMI 错误码。
 */
typedef int (*qm_audio_cloud_resolve_play_fn)(const qm_audio_cloud_command_t *command, qm_audio_cloud_playback_t *playback, void *user_ctx);

/**
 * @brief Authorize a decoded cloud command. / 鉴权一个已解码的云端命令。
 *
 * The callback runs before any Control operation is changed. Return QM_EOK to
 * accept the command or a negative QUMI error to reject it. / 回调在任何 Control
 * 操作改变之前执行。返回 QM_EOK 接受命令，返回负的 QUMI 错误码拒绝。
 * @param[in] command Borrowed decoded command. / 借用的已解码命令。
 * @param[in] user_ctx Borrowed product callback context. / 借用的产品回调上下文。
 * @return QM_EOK to authorize; otherwise a negative QUMI error. / 授权时返回 QM_EOK，否则返回负的 QUMI 错误码。
 */
typedef int (*qm_audio_cloud_authorize_fn)(const qm_audio_cloud_command_t *command, void *user_ctx);

/**
 * @brief Apply product behavior for a LIMIT command. / 为 LIMIT 命令执行产品特定行为。
 *
 * LIMIT is a protocol-level intent, not a second audio scheduler. The adapter
 * pauses the current Control operation first, then invokes this optional hook
 * for product-only effects such as entitlement state or prompt scheduling. The
 * command remains borrowed for the callback duration. / LIMIT 是协议级意图，不是
 * 第二个音频调度器。适配器先暂停当前 Control 操作，再调用本可选钩子执行仅产品
 * 相关的效果（如授权状态或提示调度）。命令在回调期间保持借用。
 * @param[in] command Borrowed decoded LIMIT command. / 借用的已解码 LIMIT 命令。
 * @param[in] user_ctx Borrowed product callback context. / 借用的产品回调上下文。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK，失败返回负的 QUMI 错误码。
 */
typedef int (*qm_audio_cloud_limit_fn)(const qm_audio_cloud_command_t *command, void *user_ctx);

/** @brief Runtime options copied by qm_audio_cloud_init(). / qm_audio_cloud_init() 复制的运行时选项。 */
typedef struct {
    /** @brief Connected qm_cloud IoT handle borrowed by the adapter. / 适配器借用的已连接 qm_cloud IoT 句柄。 */
    void *iot_handle;
    /** @brief Control for every cloud-created playback operation. / 用于所有云端创建播放操作的 Control。 */
    qm_audio_control_t *control;
    /** @brief Resolver for PLAY resources and policy. / PLAY 资源与产品播放策略的解析回调。 */
    qm_audio_cloud_resolve_play_fn resolve_play;
    /** @brief Optional authorization hook for all commands. / 所有已解码播放器命令的可选鉴权钩子。 */
    qm_audio_cloud_authorize_fn authorize;
    /** @brief Optional LIMIT product-policy hook. / 可选的 LIMIT 产品策略钩子。 */
    qm_audio_cloud_limit_fn on_limit;
    /** @brief Borrowed context for product callbacks. / 产品回调借用的上下文。 */
    void *user_ctx;
} qm_audio_cloud_config_t;

/**
 * @brief Create and register the optional player cloud adapter. / 创建并注册可选的云端播放器适配器。
 *
 * CONFIG_QM_AUDIO_CLOUD_SUPPORT only includes this module in the firmware; it
 * does not initialize an adapter automatically. Call this after Engine,
 * Service, and Control are successfully created and after qm_cloud is ready to
 * register device/down methods. Only one adapter may register the default
 * "player" method on an IoT handle. / CONFIG_QM_AUDIO_CLOUD_SUPPORT 只是把本模块
 * 编入固件，不会自动初始化适配器。应在 Engine、Service、Control 创建成功且
 * qm_cloud 准备好注册设备/下行方法后调用。一个 IoT 句柄上只允许一个适配器注册
 * 默认的 "player" 方法。
 * @param[in] config Cloud adapter configuration. / 云端适配器配置。
 * @param[out] out_cloud New adapter output. / 新适配器输出。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK，失败返回负的 QUMI 错误码。
 */
int qm_audio_cloud_init(const qm_audio_cloud_config_t *config, qm_audio_cloud_t **out_cloud);

/**
 * @brief Stop cloud ingress and release cloud-owned operations. / 停止云端输入并释放云端自有操作。
 *
 * This function never shuts down the borrowed Control. It unregisters cloud
 * methods before stopping cloud-created operations, waits for their workers,
 * and releases any resolver-owned input contexts. Retry on timeout; do not
 * destroy the cloud handle until this succeeds. / 本函数绝不会关闭借用的 Control。
 * 它先注销云端方法，再停止云端创建的操作，等待其任务退出，并释放解析器自有的
 * 输入上下文。超时须重试；成功前不得销毁云句柄。
 * @param[in] cloud Cloud adapter to shut down. / 待关闭的云端适配器。
 * @param[in] timeout_ms Shutdown wait budget in milliseconds. / 关闭等待时长（毫秒）。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK，失败返回负的 QUMI 错误码。
 */
int qm_audio_cloud_shutdown(qm_audio_cloud_t *cloud, uint32_t timeout_ms);

/**
 * @brief Free a successfully shut-down cloud adapter. / 释放已成功关闭的云端适配器。
 * @param[in] cloud Cloud adapter to destroy. / 待销毁的云端适配器。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK，失败返回负的 QUMI 错误码。
 */
int qm_audio_cloud_destroy(qm_audio_cloud_t *cloud);

/**
 * @brief Test whether cloud commands are queued or executing. / 查询是否仍有云端命令排队或执行。
 * @param[in] cloud Cloud adapter to query. / 待查询的云端适配器。
 * @return Nonzero when work remains; zero otherwise. / 有未完成工作时返回非零，否则返回零。
 */
int qm_audio_cloud_command_pending(const qm_audio_cloud_t *cloud);

#ifdef __cplusplus
}
#endif

#endif /* QM_AUDIO_CLOUD_H */
