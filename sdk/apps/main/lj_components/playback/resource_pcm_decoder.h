#ifndef LJYD_RESOURCE_PCM_DECODER_H
#define LJYD_RESOURCE_PCM_DECODER_H

#include "qm_player_decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Decoder for cloud resources whose audioType is pcm. */
const qm_player_decoder_if_t *resource_pcm_decoder_get_if(void);

#ifdef __cplusplus
}
#endif

#endif /* LJYD_RESOURCE_PCM_DECODER_H */
