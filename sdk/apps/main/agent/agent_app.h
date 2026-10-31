#ifndef AGENT_APP_H
#define AGENT_APP_H

#include <stdint.h>
#include "qm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 创建 agent。唤醒词到来且云已连上时才 start。 */
int agent_app_init(void *iot_handle);
/* 云端 chat/quit：停会话。成功后清掉已 start 标记，下次唤醒词可以再 start。 */
int agent_app_leave(void);
/* 返回 1 表示 agent 会话正在运行（唤醒后已 start 且尚未 leave）。 */
int agent_app_is_active(void);
/* 返回 1 表示当前正处于语音交互活跃窗口（listening / thinking / speaking 或等待 TTS 完成）。 */
int agent_app_is_dialog_busy(void);
/* 以本地提示音方式播一段已按 qm_audio 线上格式编好的音频；会话未创建时返回 -QM_EINIT。 */
int agent_app_play_local_audio(const void *data, uint32_t len);

/* Stop cloud/local output and pause the physical capture path. */
int agent_app_abort_output(void);

/* Ask the cloud agent to speak one application-provided sentence. */
int agent_app_user_corpus(const char *text);

/* Wait until the local TTS queue and speaker have drained. */
int agent_app_wait_tts_complete(uint32_t timeout_ms);

/* Reserve or release the shared audio path for resource playback. */
int agent_app_set_resource_mode(qm_bool active);

/* Release any active agent downlink audio stream before playing a local sound effect, without disabling mic upload. */
int agent_app_prepare_for_local_sound(uint32_t timeout_ms);

/* Mute or unmute agent downlink TTS audio during Pomodoro focus chat penalties. */
void agent_app_set_pomodoro_tts_mute(qm_bool muted);

/* Notify agent that Pomodoro has started; play startup sound after cloud reply finishes. */
void agent_app_notify_pomodoro_started(void);

/* Check and fallback play startup sound if cloud had no voice reply. */
void agent_app_check_pomodoro_start_sound(void);

#ifdef __cplusplus
}
#endif

#endif
