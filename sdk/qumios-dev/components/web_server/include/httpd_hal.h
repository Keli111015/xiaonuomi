#ifndef _HTTPD_HAL_H_
#define _HTTPD_HAL_H_

#ifdef __cplusplus
extern "C" {
#endif

int httpd_net_creat(int port);
int httpd_net_accept(int socket, char ip[16], int timeout);
int httpd_net_read(int socket, char *buf, int len);
int httpd_net_write(int socket, char*buf, int len);
int httpd_net_destroy(int socket);

#ifdef __cplusplus
}
#endif

#endif /* HTTPD_HAL_H */


