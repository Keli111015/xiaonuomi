/**
 * @file qm_agent_video_engine.c
 * @brief Board-video adapter layer.
 *
 * Video drivers own capture scheduling and push borrowed frames through the
 * callback registered at open time.  The Agent only gates session admission
 * and forwards each frame to the selected transport; it does not create a
 * polling task or retain frame storage.
 *
 * 板级视频适配层。视频驱动负责采集并通过 open() 注册的回调交付借用帧；Agent 仅控制
 * 会话准入并将帧转发到当前传输，不创建轮询任务，也不保留帧内存。
 */

#include "qm_agent_internal.h"

/**
 * @brief Forward one driver-owned frame to the active transport.
 *
 * The driver may recycle frame->data as soon as this function returns.  The
 * transport contract therefore requires every adapter to consume or copy the
 * bytes before returning from send_video().
 * 本函数返回后驱动可能立即复用 frame->data，因此 transport 约定要求每个适配器在
 * send_video() 返回前消费或复制字节。
 */
static int video_frame_callback(const qm_agent_video_frame_t *frame,
                                void *user_ctx)
{
    qm_agent_handle_t handle = (qm_agent_handle_t)user_ctx;

    if (handle == NULL || frame == NULL || frame->data == NULL ||
        frame->len == 0U) {
        return -QM_EINVAL;
    }

    /* qm_agent_transport_send_video() performs the readiness check while
     * holding the network lock, so stop/disconnect cannot race admission.
     * qm_agent_transport_send_video() 在持有网络锁时检查就绪状态，因此 stop/disconnect
     * 不会与帧准入竞争。 */
    return qm_agent_transport_send_video(handle,
                                         frame->data,
                                         frame->len,
                                         &frame->format);
}

/**
 * @brief Open the configured video driver with frame admission disabled.
 * 打开已配置的视频驱动，并保持帧准入关闭。
 */
int qm_agent_video_engine_open(qm_agent_handle_t handle)
{
    const qm_agent_video_driver_if_t *driver;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    driver = handle->config.video_driver;
    if (driver == NULL) {
        return QM_EOK;
    }

    if (driver->open == NULL) {
        return -QM_EINVAL;
    }

    /* A driver may synchronously emit from open/start; leave admission closed
     * until the Agent session is fully started and connected.
     * 驱动可能在 open/start 中同步上报帧；必须在 Agent 会话完全启动并连接前保持准入关闭。 */
    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    handle->video.running = QM_FALSE;
    handle->video.stop_required = QM_FALSE;
    handle->video.transport_ready = QM_FALSE;
    qm_mutex_unlock(&handle->network_lock);

    return driver->open(&handle->video.driver_ctx,
                        &handle->config.input_video,
                        qm_agent_on_video_event,
                        video_frame_callback,
                        handle);
}

/**
 * @brief Quiesce and close the video driver while preserving retry state.
 * 让视频驱动静默并关闭，同时保留可重试状态。
 */
int qm_agent_video_engine_close(qm_agent_handle_t handle)
{
    const qm_agent_video_driver_if_t *driver;
    qm_bool stop_required;
    int ret = QM_EOK;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    /* Stop admission before asking the driver to quiesce.  Driver close()
     * must wait for any callback already in flight before returning.
     * 请求驱动静默前先关闭准入；driver close() 返回前必须等待已在执行的回调结束。 */
    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    stop_required = handle->video.running || handle->video.stop_required;
    handle->video.running = QM_FALSE;
    handle->video.transport_ready = QM_FALSE;
    qm_mutex_unlock(&handle->network_lock);

    driver = handle->config.video_driver;
    if (stop_required && driver && driver->stop && handle->video.driver_ctx) {
        ret = driver->stop(handle->video.driver_ctx);
        if (ret != QM_EOK) {
            /* Keep the context intact so a later destroy can retry the
             * driver's required callback barrier.
             * 保留上下文，使后续 destroy 可重试驱动要求的回调屏障。 */
            return ret;
        }
        handle->video.stop_required = QM_FALSE;
    }

    if (driver && driver->close && handle->video.driver_ctx) {
        ret = driver->close(handle->video.driver_ctx);
    }

    /* Preserve the context on a failed close so destruction can be retried
     * without losing the only handle needed to quiesce the driver.
     * close 失败时保留上下文，后续销毁重试才不会丢失让驱动静默所需的唯一句柄。 */
    if (ret == QM_EOK) {
        handle->video.driver_ctx = NULL;
        handle->video.stop_required = QM_FALSE;
    }
    return ret;
}

/**
 * @brief Start the video driver but wait for transport readiness before forwarding.
 * 启动视频驱动，但在传输就绪前不转发帧。
 */
int qm_agent_video_engine_start(qm_agent_handle_t handle)
{
    const qm_agent_video_driver_if_t *driver;
    int ret;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    driver = handle->config.video_driver;
    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    handle->video.running = QM_TRUE;
    handle->video.stop_required = QM_TRUE;
    handle->video.transport_ready = QM_FALSE;
    qm_mutex_unlock(&handle->network_lock);

    if (driver == NULL || handle->video.driver_ctx == NULL) {
        return QM_EOK;
    }

    if (driver->start == NULL) {
        return QM_EOK;
    }

    ret = driver->start(handle->video.driver_ctx);
    if (ret != QM_EOK) {
        qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
        handle->video.running = QM_FALSE;
        qm_mutex_unlock(&handle->network_lock);
    }
    return ret;
}

/**
 * @brief Close frame admission and stop the configured video driver.
 * 关闭帧准入并停止已配置的视频驱动。
 */
int qm_agent_video_engine_stop(qm_agent_handle_t handle)
{
    const qm_agent_video_driver_if_t *driver;
    qm_bool was_running;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    /* Close the admission gate before stop() so a concurrent frame callback
     * cannot begin a new transport send while the driver is winding down.
     * stop() 前关闭准入门，避免驱动收尾时并发帧回调开始新的传输发送。 */
    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    was_running = handle->video.running;
    handle->video.running = QM_FALSE;
    /* Keep this set until the driver confirms its callback barrier.
     * 保持该标志，直到驱动确认其回调屏障已经完成。 */
    handle->video.stop_required = handle->video.stop_required ||
                                  was_running;
    handle->video.transport_ready = QM_FALSE;
    qm_mutex_unlock(&handle->network_lock);

    driver = handle->config.video_driver;
    if (driver && driver->stop && handle->video.driver_ctx) {
        int ret = driver->stop(handle->video.driver_ctx);
        if (ret == QM_EOK) {
            handle->video.stop_required = QM_FALSE;
        }
        return ret;
    }

    return QM_EOK;
}

/**
 * @brief Apply transport readiness to the active video admission gate.
 * 将传输就绪状态应用到活动视频准入门。
 */
void qm_agent_video_engine_set_transport_ready(qm_agent_handle_t handle,
                                                qm_bool ready)
{
    if (handle == NULL) {
        return;
    }

    /* Readiness is meaningful only while the driver remains admitted.  Keep
     * this state transition in the video boundary so Core never reaches into
     * driver-owned lifecycle state.
     * 仅当驱动仍被准入时就绪状态才有意义；状态切换留在视频边界，避免 Core 直接操作驱动所有的
     * 生命周期状态。 */
    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    handle->video.transport_ready = (ready && handle->video.running &&
                                     !handle->destroying) ? QM_TRUE : QM_FALSE;
    qm_mutex_unlock(&handle->network_lock);
}

/**
 * @brief Publish a normalized driver event through the Agent event boundary.
 * 通过 Agent 事件边界发布标准化驱动事件。
 */
void qm_agent_on_video_event(const qm_agent_event_t *event, void *user_ctx)
{
    qm_agent_handle_t handle = (qm_agent_handle_t)user_ctx;

    if (handle == NULL || event == NULL) {
        return;
    }

    qm_agent_emit(handle, event);
}
