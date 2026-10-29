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

#ifdef __cplusplus
}
#endif

#endif
