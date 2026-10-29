/**
 * @file qm_audio_frontend.h
 * @brief Voice frontend adaptation API. / 语音前端适配接口。
 *
 * Voice frontend (wakeword detection) adaptation interface; declarations only.
 *
 * This interface hides the vendor wakeword/audio frontend algorithm behind four named
 * functions. The application only includes this header and never references
 * platform implementation names. Exactly one platform implementation is linked
 * into the firmware; the vendor is selected at build time (which
 * qm_audio_frontend.c / vendor sources are compiled), not at runtime.
 *
 * Scope:
 * - Lifecycle: open/close the frontend, start/stop the capture path.
 * - Mode: toggle wakeword detection only. MODE_DIALOG pauses detection to
 *   avoid false wakeup while a voice dialog is active.
 *
 * Out-of-scope (by design):
 * - Captured media uses the platform capture ingress and the build-selected
 *   PUSH/PULL contract; applications subscribe through qm_audio_capture.
 * - Uplink gating is decided at the consumer
 *   side (capture subscription enable / uplink gate), not here.
 *
 * Callers: the application manages the lifecycle explicitly. qm_audio internal
 * modules never call open/close/set_mode automatically.
 */
#ifndef QM_AUDIO_FRONTEND_H
#define QM_AUDIO_FRONTEND_H

#include "qm_audio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Wake notifications use qm_audio_report_wake()/qm_audio_wake_subscribe().
 * / 唤醒通知通过 qm_audio_report_wake()/qm_audio_wake_subscribe() 交付。 */
/* Kept for source compatibility; new producers use the unified audio event API. */
#define QM_AUDIO_FRONTEND_EVENT_BASE    "qm.audio.frontend"
enum {
    QM_AUDIO_FRONTEND_EVENT_WAKEUP = 1,
    QM_AUDIO_FRONTEND_EVENT_VAD,
    QM_AUDIO_FRONTEND_EVENT_ERROR,
};

/**
 * @brief Wake word detection mode. / 唤醒词检测模式。
 *
 * Frontend mode; controls wakeword detection only.
 */
typedef enum {
    /** @brief Standby with wake word detection enabled. / 待机状态，启用唤醒词检测。 */
    QM_AUDIO_FRONTEND_MODE_WAKEUP = 0,
    /** @brief Active dialog with wake word detection paused. / 语音对话状态，暂停唤醒词检测。 */
    QM_AUDIO_FRONTEND_MODE_DIALOG,
} qm_audio_frontend_mode_t;

/**
 * @brief Open the frontend and start the vendor capture path. / 打开前端并启动厂商采集通路。
 *
 * Open the frontend and start the vendor capture path.
 *
 * Vendor, sample rate, and model path are build-time configuration
 * (qm_config.h), so this takes no arguments. The first open starts the
 * capture/algorithm; wakeword detection runs in MODE_WAKEUP by default.
 * @return `QM_EOK` on success; -QM_EBUSY if already open; another negative
 * qm_errno value on failure (the implementation unwinds partial resources).
 */
int qm_audio_frontend_open(void);

/**
 * @brief Stop capture and release the vendor frontend. / 停止采集并释放厂商前端实例。
 *
 * Stop the capture path and release the vendor instance.
 *
 * The caller is expected to have returned to MODE_WAKEUP first. close forces
 * the vendor stop/deinit even if a dialog mode was active.
 * @return `QM_EOK` only after capture I/O and vendor callbacks are quiescent;
 * another negative qm_errno value on failure.
 */
int qm_audio_frontend_close(void);

/**
 * @brief Set the wake word detection mode. / 设置唤醒词检测模式。
 *
 * Set the frontend mode (wakeword detection state).
 * @param[in] mode `QM_AUDIO_FRONTEND_MODE_WAKEUP` or `QM_AUDIO_FRONTEND_MODE_DIALOG`. / 唤醒模式或对话模式。
 * @return `QM_EOK` on success; -QM_EINVAL for an invalid mode; -QM_ENOSYS if
 * not open; another negative qm_errno value on failure.
 */
int qm_audio_frontend_set_mode(qm_audio_frontend_mode_t mode);

/**
 * @brief Query the current frontend mode. / 查询当前前端模式。
 *
 * Query the current frontend mode.
 * @return The current mode; `QM_AUDIO_FRONTEND_MODE_WAKEUP` when not open. / 当前模式；未打开时返回 `QM_AUDIO_FRONTEND_MODE_WAKEUP`。
 */
qm_audio_frontend_mode_t qm_audio_frontend_get_mode(void);

#ifdef __cplusplus
}
#endif

#endif /* QM_AUDIO_FRONTEND_H */
