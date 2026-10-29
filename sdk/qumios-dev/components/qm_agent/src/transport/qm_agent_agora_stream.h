#ifndef QM_AGENT_AGORA_STREAM_H
#define QM_AGENT_AGORA_STREAM_H

/**
 * @file qm_agent_agora_stream.h
 * @brief Incremental Agora stream parser used by the qm_agent adapter.
 *
 * qm_agent Agora 适配器使用的增量流解析器。输入字节仅在 input() 调用期间借用；
 * 解析器会保存不完整帧，并通过回调输出标准化 qm_agent_event_t。
 */

#include "qm_agent_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Deliver a normalized event parsed from an Agora stream frame.
 *
 * 交付从 Agora 流帧解析出的标准化事件。event 只在回调期间有效，接收方如需异步处理必须
 * 克隆其内容。
 *
 * @param[in] event Borrowed normalized event. 借用的标准化事件。
 * @param[in] user_ctx Parser context supplied at initialization. 初始化时提供的上下文。
 */
typedef void (*qm_agent_agora_stream_event_cb_t)(const qm_agent_event_t *event,
                                                 void *user_ctx);

/** @brief Opaque incremental Agora stream parser. 不透明的 Agora 增量流解析器。 */
typedef struct qm_agent_agora_stream qm_agent_agora_stream_t;

/**
 * @brief Create an Agora stream parser. 创建 Agora 流解析器。
 *
 * @param[out] stream Destination parser pointer. 输出解析器指针。
 * @param[in] event_cb Event callback invoked during input(). 在 input() 中调用的事件回调。
 * @param[in] user_ctx Borrowed callback context. 借用的回调上下文。
 * @return QM_EOK on success, negative qm_errno value on failure. 成功返回 QM_EOK，失败返回负 qm_errno。
 */
int qm_agent_agora_stream_init(qm_agent_agora_stream_t **stream,
                               qm_agent_agora_stream_event_cb_t event_cb,
                               void *user_ctx);
/** @brief Destroy an Agora stream parser. 销毁 Agora 流解析器。 */
void qm_agent_agora_stream_deinit(qm_agent_agora_stream_t *stream);
/** @brief Discard an incomplete input frame. 丢弃尚未完成的输入帧。 */
void qm_agent_agora_stream_reset(qm_agent_agora_stream_t *stream);
/**
 * @brief Feed borrowed stream bytes into the parser. 输入借用的流字节到解析器。
 *
 * @param[in] stream Parser context. 解析器上下文。
 * @param[in] data Input bytes borrowed for this call. 本次调用期间借用的输入字节。
 * @param[in] len Input byte count. 输入字节数。
 * @return QM_EOK on success, negative qm_errno value on failure. 成功返回 QM_EOK，失败返回负 qm_errno。
 */
int qm_agent_agora_stream_input(qm_agent_agora_stream_t *stream,
                                const char *data,
                                uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* QM_AGENT_AGORA_STREAM_H */
