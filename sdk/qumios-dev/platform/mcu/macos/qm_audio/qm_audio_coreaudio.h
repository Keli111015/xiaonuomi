#ifndef QM_MACOS_AUDIO_COREAUDIO_H
#define QM_MACOS_AUDIO_COREAUDIO_H

#include "qm_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Return the macOS AudioQueue PCM driver.
 *
 * The driver accepts interleaved signed linear PCM for capture and playback.
 * The returned interface has static lifetime.
 */
const qm_audio_driver_if_t *qm_macos_coreaudio_driver(void);

#ifdef __cplusplus
}
#endif

#endif /* QM_MACOS_AUDIO_COREAUDIO_H */
