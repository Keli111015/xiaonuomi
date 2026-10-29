#ifndef __PRODTST_TCP_MGR_H__
#define __PRODTST_TCP_MGR_H__


int prodtst_tcp_mgr_init(void);
int prodtst_tcp_mgr_deinit(void);

void *prodtst_tcp_mgr_handle(void);
int prodtst_tcp_mgr_establish(void *handle, char *host, uint16_t port);
int prodtst_tcp_mgr_destroy(void *handle);
int prodtst_tcp_mgr_read(void *handle, uint8_t *buf, int size, uint32_t timeout);
int prodtst_tcp_mgr_write(void *handle, uint8_t *buf, int size);

#endif

