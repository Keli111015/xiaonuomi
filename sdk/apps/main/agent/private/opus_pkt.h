#ifndef AGENT_OPUS_PKT_H
#define AGENT_OPUS_PKT_H

#include <stdint.h>

/*
 * 芯片是 PCM。上行尽量 20 ms / 40 字节。下行按实际包长解，上限 255。
 * 返回值：>0 字节数，0 还凑不齐一帧，<0 失败。
 */
#define OPUS_PKT_FRAME_MS 20
#define OPUS_PKT_BYTES    40
#define OPUS_PKT_MAX      255
int opus_pcm_to_packet(const uint8_t *pcm, uint32_t pcm_len,
                       uint8_t *opus, uint32_t opus_cap);

int opus_packet_to_pcm(const uint8_t *opus, uint32_t opus_len,
                       uint8_t *pcm, uint32_t pcm_cap);

#endif
