#ifndef OFFLINE_VOICE_HAL_H
#define OFFLINE_VOICE_HAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "offline_voice.h"

#if CONFIG_OFFLINE_VOICE_UART_SUPPORT
void *offline_voice_uart_init(void);
int offline_voice_uart_deinit(void *handle);
#endif

int32_t offline_voice_channel_send(void *handle, char *buf, int size);
int32_t offline_voice_channel_recv(void *handle, char *buf, int size, int timeout);

#ifdef __cplusplus
}
#endif


#endif /* PRODTST_CLIENT_H */
