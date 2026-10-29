#ifndef __VIRTUAL_CHANNEL_H__
#define __VIRTUAL_CHANNEL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_ringbuf.h"
#include "qm_types.h"
#include "qm_kernel.h"

#ifndef CONFIG_VC_SERVER_NUM
#define CONFIG_VC_SERVER_NUM   (4)
#endif

#ifndef CONFIG_VC_LISTEN_NUM
#define CONFIG_VC_LISTEN_NUM   (8)
#endif

#ifndef CONFIG_VC_CLIENT_NUM
#define CONFIG_VC_CLIENT_NUM   (8)
#endif


int virtual_channel_server_creat(char *host, int port, uint32_t rd_size, uint32_t wr_size);
int virtual_channel_client_creat(uint32_t rd_size, uint32_t wr_size);
int virtual_channel_connect(int fd, char *host, int port, uint32_t timeout);
int virtual_channel_accept(int fd);
int virtual_channel_write(int fd, uint8_t *data, uint32_t len); 
int virtual_channel_read(int fd, uint8_t *data, uint32_t len, uint32_t timeout);
int virtual_channel_destroy(int fd);

#ifdef __cplusplus
}
#endif


#endif /* __VIRTUAL_CHANNEL_H__ */
