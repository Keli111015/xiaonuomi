/**
 * @file joyinside_client_vision.h
 * @brief Session-bound JPEG reporting. / 关联当前会话的 JPEG 上报。
 */
#ifndef JOYINSIDE_CLIENT_VISION_H
#define JOYINSIDE_CLIENT_VISION_H

#include "joyinside_client.h"
#include "qm_http_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One borrowed image and HTTP options. / 一张借用图片及 HTTP 选项。 */
typedef struct {
    const char *app_id;                /*!< Vision application ID. / 视觉应用 ID。 */
    const uint8_t *jpeg;               /*!< JPEG bytes, borrowed until return. / 返回前有效的 JPEG 数据。 */
    uint32_t jpeg_len;                 /*!< JPEG byte count. / JPEG 字节数。 */
    uint64_t timestamp_ms;             /*!< Capture time, Unix milliseconds. / 采集时间，Unix 毫秒。 */
    const char *url;                   /*!< NULL uses production image/report. / NULL 使用生产图片上报地址。 */
    const qm_http_tls_config_t *tls;   /*!< Borrowed TLS options; NULL uses HTTP defaults. / 借用 TLS 配置，NULL 使用默认值。 */
    uint32_t timeout_ms;               /*!< HTTP timeout; 0 uses component default. / HTTP 超时，0 使用组件默认值。 */
} joyinside_client_image_t;

/**
 * @brief Upload JPEG using the current WS identity and token. / 使用当前 WS 身份和 Token 上传 JPEG。
 *
 * Requires CONFIG_JOYINSIDE_CLIENT_VISION_SUPPORT. Runs synchronously on the
 * caller's application worker; never call from a client event callback or an
 * audio/video capture callback. Base64 is streamed with bounded scratch space.
 * Trigger from IS_START or an application timer. Serialize entry with stop and
 * destroy; all image pointers remain borrowed until return. The captured
 * session identity stays fixed even if the WS connection reconnects meanwhile.
 * 在应用工作线程同步执行，不得在客户端事件或音视频采集回调中调用。流式编码避免整帧
 * Base64 副本。应用可由 IS_START 或定时器触发；进入调用时须与停止、销毁串行。
 * 调用期间图片数据保持有效，上传使用开始时快照的会话身份。
 *
 * @param[in] handle Connected client. / 已连接客户端。
 * @param[in] image Borrowed image and options. / 借用图片及选项。
 * @return QM_EOK only on server success; otherwise negative error. / 服务器确认成功返回 QM_EOK，否则返回负错误码。
 */
int joyinside_client_report_image(joyinside_client_handle_t handle,
                                const joyinside_client_image_t *image);

#ifdef __cplusplus
}
#endif
#endif
