#ifndef QM_AUDIO_AC7911_DEVICE_PRIV_H
#define QM_AUDIO_AC7911_DEVICE_PRIV_H

#include "qm_audio_types.h"

/* Platform capture-source ingress; copied before return and gated by capture lifecycle. */
int qm_audio_device_capture_submit(const qm_audio_data_t *input);

#endif
