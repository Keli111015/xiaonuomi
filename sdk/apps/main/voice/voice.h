#ifndef VOICE_H
#define VOICE_H

#include "qm.h"
#include "qm_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 qm_audio。串口、版本和开麦在 VB701N 设备绑定里。 */
qm_err_t voice_start(void);

/* qm_agent 借用这份 Control。音频没起来时返回 NULL。 */
qm_audio_control_t *voice_get_control(void);

int voice_audio_ready(void);

typedef void (*voice_wakeup_cb_t)(void);
void voice_set_wakeup_cb(voice_wakeup_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif
