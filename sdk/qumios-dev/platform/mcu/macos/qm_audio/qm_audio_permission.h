#ifndef QM_MACOS_AUDIO_PERMISSION_H
#define QM_MACOS_AUDIO_PERMISSION_H

#include <stdint.h>

int qm_macos_microphone_authorize(uint32_t timeout_ms);

#endif /* QM_MACOS_AUDIO_PERMISSION_H */
