#ifndef BOOT_SOUND_H
#define BOOT_SOUND_H

#include "qm_errno.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 播放一次开机音效。异步：起一次性任务编码+播放，调用完立即返回。
 * 需要在 qm_audio 就绪之后调用（voice.c 的 on_chip_ready）。
 */
qm_err_t boot_sound_start(void);

/*
 * 播放一次扣血音效。异步：由本地音效任务编码+播放，调用完立即返回。
 * 内部从 4 种扣血音效中伪随机选择一种播放。
 */
qm_err_t hurt_sound_start(void);

/*
 * 播放指定变体的扣血音效（variant: 0 ~ 3）。
 */
qm_err_t hurt_sound_start_variant(uint8_t variant);

/*
 * 播放一次死亡音效（血量归零时播放）。异步：由本地音效任务编码+播放，调用完立即返回。
 */
qm_err_t death_sound_start(void);

/*
 * 播放一次番茄钟启动标志音效（开启番茄钟且云端播完后播放）。异步：由本地音效任务编码+播放，调用完立即返回。
 */
qm_err_t pomodoro_start_sound_start(void);

#ifdef __cplusplus
}
#endif

#endif
