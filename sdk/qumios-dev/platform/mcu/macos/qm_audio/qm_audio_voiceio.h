#ifndef QM_AUDIO_VOICEIO_H
#define QM_AUDIO_VOICEIO_H

#include "qm_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Return the macOS VoiceProcessingIO full-duplex PCM driver.
 *
 * The driver keeps capture active while playback is running so Apple's
 * voice-processing unit can use the speaker reference for echo cancellation.
 */
const qm_audio_driver_if_t *qm_macos_voiceio_driver(void);

#ifdef __cplusplus
}
#endif

#endif
