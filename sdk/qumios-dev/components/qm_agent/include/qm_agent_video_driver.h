#ifndef QM_AGENT_VIDEO_DRIVER_H
#define QM_AGENT_VIDEO_DRIVER_H

/**
 * @file qm_agent_video_driver.h
 * @brief Video driver interface used by qm_agent video engine.
 *
 * qm_agent 视频引擎使用的板级视频驱动接口。驱动负责采集调度并交付借用帧；stop() 和
 * close() 返回前必须阻止后续回调并等待已进入的回调结束。
 */

#include "qm_agent_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Video frame descriptor exchanged with board video drivers.
 * 板级视频驱动与 qm_agent 交换的视频帧描述。data 属于驱动，回调返回后即可被驱动复用。
 */
typedef struct {
    /**
     * @brief Borrowed frame bytes.
     *
     * The driver owns this storage and may recycle it as soon as the frame
     * callback returns.  A callback must not retain this pointer.
     */
    const uint8_t *data;

    /** @brief Frame byte length. */
    uint32_t len;

    /** @brief Frame format. */
    qm_agent_video_format_t format;

    /** @brief Optional presentation timestamp in milliseconds. */
    uint64_t pts_ms;
} qm_agent_video_frame_t;

/**
 * @brief Video driver control command.
 * 视频驱动控制命令。0x1000 以下保留，厂商扩展从 CUSTOM 开始编号。
 */
typedef enum {
    /** @brief Driver-specific control range starts here. */
    QM_AGENT_VIDEO_DRIVER_CTRL_CUSTOM = 0x1000,
} qm_agent_video_driver_ctrl_t;

/**
 * @brief Callback used by video drivers to emit normalized events.
 * 视频驱动上报标准化事件的回调。event 和其中的借用字段只在本次调用期间有效。
 *
 * @param event Event data. The pointer must remain valid only for the call.
 * @param user_ctx User context passed to qm_agent_video_driver_if_t::open.
 */
typedef void (*qm_agent_video_driver_event_cb_t)(const qm_agent_event_t *event,
                                              void *user_ctx);

/**
 * @brief Callback used by a video driver to push one captured frame.
 *
 * The callback runs in the driver's execution context and is deliberately
 * synchronous: it either admits the borrowed frame to the transport or
 * returns an error so the driver can drop/recycle it.  Drivers must not invoke
 * this callback from an ISR because the Agent transport admission path uses
 * task-context synchronization.
 *
 * 回调在驱动执行上下文中同步运行：要么在有界时间内接收借用帧，要么返回错误让驱动丢弃或
 * 回收该帧。不得从 ISR 调用，因为传输准入路径使用任务上下文同步机制。
 *
 * @param frame Borrowed frame descriptor, valid only during this call.
 * @param user_ctx User context passed to qm_agent_video_driver_if_t::open.
 * @return QM_EOK when the frame was admitted, otherwise a negative qm_errno
 *         value such as -QM_EBUSY when the session is not ready.
 */
typedef int (*qm_agent_video_driver_frame_cb_t)(const qm_agent_video_frame_t *frame,
                                                void *user_ctx);

/**
 * @brief Board video driver interface.
 * 板级视频驱动接口。open() 绑定回调，start()/stop() 控制采集，close() 是释放 user_ctx
 * 之前必须完成的回调静默屏障。
 */
typedef struct {
    /** @brief Human-readable driver name. */
    const char *name;

    /**
     * @brief Open the video driver.
     *
     * @param driver_ctx Output driver context.
     * @param input_format Capture video format.
     * @param event_cb Event callback into qm_agent core.
     * @param frame_cb Synchronous frame-push callback into qm_agent core.
     * @param user_ctx User context passed back with event_cb and frame_cb.
     * @return QM_EOK on success, negative qm_errno value on failure.
     */
    int (*open)(void **driver_ctx,
                const qm_agent_video_format_t *input_format,
                qm_agent_video_driver_event_cb_t event_cb,
                qm_agent_video_driver_frame_cb_t frame_cb,
                void *user_ctx);

    /**
     * @brief Close the video driver.
     *
 * Before returning QM_EOK, close must stop production and wait until every
 * event_cb and frame_cb invocation from this driver has returned. This is
 * the lifetime barrier that permits qm_agent to release user_ctx.
 * 返回 QM_EOK 前，close() 必须停止产帧并等待所有 event_cb 与 frame_cb 返回；这是
 * qm_agent 可以释放 user_ctx 的生命周期屏障。
     *
     * @param driver_ctx Driver context from open.
     * @return QM_EOK on success, negative qm_errno value on failure.
     */
    int (*close)(void *driver_ctx);

    /**
     * @brief Start streaming frames from the video driver.
     *
     * @param driver_ctx Driver context from open.
     * @return QM_EOK on success, negative qm_errno value on failure.
     */
    int (*start)(void *driver_ctx);

    /**
     * @brief Stop streaming frames from the video driver.
     *
 * Before returning QM_EOK, stop must prevent new callbacks and wait for
 * callbacks already in progress. A failed stop keeps driver_ctx valid so
 * qm_agent can retry the lifecycle operation.
 * 返回 QM_EOK 前，stop() 必须阻止新的回调并等待在途回调结束。失败时必须保留 driver_ctx，
 * 以便 qm_agent 重试生命周期操作。
     *
     * @param driver_ctx Driver context from open.
     * @return QM_EOK on success, negative qm_errno value on failure.
     */
    int (*stop)(void *driver_ctx);

    /**
     * @brief Send a control command to the video driver.
     *
     * @param driver_ctx Driver context from open.
     * @param ctrl Control command.
     * @param arg Command argument.
     * @return QM_EOK on success, negative qm_errno value on failure.
     */
    int (*control)(void *driver_ctx, qm_agent_video_driver_ctrl_t ctrl, void *arg);
} qm_agent_video_driver_if_t;

#ifdef __cplusplus
}
#endif

#endif /* QM_AGENT_VIDEO_DRIVER_H */
