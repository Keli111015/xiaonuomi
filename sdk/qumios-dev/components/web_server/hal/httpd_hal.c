#include "httpd_hal.h"
#include "qm_errno.h"
#include "qm_tcp.h"

int httpd_net_creat(int port)
{   
    return qm_tcp_server_creat(port);
}

int httpd_net_accept(int socket, char ip[16], int timeout)
{
    return qm_tcp_accept(socket, ip, timeout);
}

int httpd_net_read(int socket, char *buf, int len)
{
    return qm_tcp_read(socket, buf, len, 100);  
}

int httpd_net_write(int socket, char*buf, int len)
{
    return qm_tcp_write(socket, buf, len, 0); 
}

int httpd_net_destroy(int socket)
{
    return qm_tcp_destroy(socket);
}
