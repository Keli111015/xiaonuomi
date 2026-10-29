#ifndef __DATA_CHANNEL_H__
#define __DATA_CHANNEL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"

void *data_channel_server_creat(uint32_t rd_size, uint32_t wr_size);
void *data_channel_client_creat(void);
int data_channel_connect(void *handle_server, void *handle_client);
int data_channel_write(void *handle, uint8_t *data, uint32_t len, uint32_t timeout);
int data_channel_read(void *handle, uint8_t *data, uint32_t len, uint32_t timeout);
int data_channel_destroy(void *handle);

#ifdef __cplusplus
}
#endif


#endif /* __DATA_CHANNEL_H__ */