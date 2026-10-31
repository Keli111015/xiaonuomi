#ifndef STUDY_VOICE_GUARD_H
#define STUDY_VOICE_GUARD_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 判断 ASR 识别文本是否属于番茄钟控制指令（开始、暂停、恢复、结束、退出等）
 */
bool study_voice_guard_is_pomodoro_cmd(const char *text);

/**
 * @brief 判断下行 TTS 文本是否为番茄钟状态播报
 */
bool study_voice_guard_is_status_tts(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* STUDY_VOICE_GUARD_H */
