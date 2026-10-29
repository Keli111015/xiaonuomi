#define BOOL_DEFINE_CONFLICT

#include "qm_tcp.h"
#include "qm_kernel.h"
#include "qm_utils_timer.h"
#include <stdio.h>
#include <string.h>

#include "lwip/netdb.h"
#include "lwip/sockets.h"

int qm_tcp_server_creat(uint16_t port)
{
    int fd;
    int reuse = 1;
    struct sockaddr_in address;

    fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        return -1;
    }

    (void)lwip_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (lwip_bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        lwip_listen(fd, 6) < 0) {
        (void)lwip_close(fd);
        return -1;
    }

    return fd;
}

int qm_tcp_accept(int socket, char ip[QM_NETWORK_ADDR_LEN], int timeout)
{
    fd_set set;
    struct timeval timeval;
    struct sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);
    int ret;
    int client;

    if (socket < 0 || ip == NULL) {
        return -1;
    }

    FD_ZERO(&set);
    FD_SET(socket, &set);
    if (timeout == QM_WAIT_FOREVER) {
        ret = lwip_select(socket + 1, &set, NULL, NULL, NULL);
    } else {
        timeval.tv_sec = timeout / 1000;
        timeval.tv_usec = (timeout % 1000) * 1000;
        ret = lwip_select(socket + 1, &set, NULL, NULL, &timeval);
    }
    if (ret <= 0) {
        return -1;
    }

    memset(&peer, 0, sizeof(peer));
    client = lwip_accept(socket, (struct sockaddr *)&peer, &peer_len);
    if (client < 0) {
        return -1;
    }

    ip[0] = '\0';
    (void)lwip_inet_ntop(AF_INET, &peer.sin_addr, ip, QM_NETWORK_ADDR_LEN);
    return client;
}

int qm_tcp_establish(const char *host, uint16_t port)
{
    int fd;
    struct hostent *host_entry;
    struct sockaddr_in address;

    if (host == NULL) {
        return -1;
    }

    host_entry = lwip_gethostbyname(host);
    if (host_entry == NULL || host_entry->h_addr_list == NULL ||
        host_entry->h_addr_list[0] == NULL) {
        return -1;
    }

    fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = *(uint32_t *)host_entry->h_addr_list[0];

    if (lwip_connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        (void)lwip_close(fd);
        return -1;
    }

    return fd;
}

int qm_tcp_destroy(int socket)
{
    if (socket < 0) {
        return -1;
    }

    (void)lwip_shutdown(socket, 2);
    return lwip_close(socket) == 0 ? 0 : -1;
}

int qm_tcp_write(int socket, const char *buf, uint32_t len, uint32_t timeout_ms)
{
    uint32_t written = 0;
    qm_utils_time_t deadline;

    if (socket < 0 || (buf == NULL && len != 0)) {
        return -1;
    }
    if (timeout_ms != QM_WAIT_FOREVER) {
        qm_utils_time_countdown_ms(&deadline, timeout_ms);
    }

    while (written < len) {
        int ret;

        if (timeout_ms != QM_WAIT_FOREVER && qm_utils_time_is_expired(&deadline)) {
            break;
        }

        ret = lwip_send(socket, buf + written, len - written, 0);
        if (ret < 0) {
            return written > 0 ? (int)written : -1;
        }
        if (ret == 0) {
            break;
        }
        written += (uint32_t)ret;
    }

    return (int)written;
}

int qm_tcp_read(int socket, char *buf, uint32_t len, uint32_t timeout_ms)
{
    fd_set set;
    struct timeval timeval;
    uint32_t received = 0;
    int ret;

    if (socket < 0 || buf == NULL || len == 0) {
        return -1;
    }

    FD_ZERO(&set);
    FD_SET(socket, &set);
    if (timeout_ms == QM_WAIT_FOREVER) {
        ret = lwip_select(socket + 1, &set, NULL, NULL, NULL);
    } else {
        timeval.tv_sec = timeout_ms / 1000;
        timeval.tv_usec = (timeout_ms % 1000) * 1000;
        ret = lwip_select(socket + 1, &set, NULL, NULL, &timeval);
    }
    if (ret <= 0) {
        return ret == 0 ? 0 : -1;
    }

    ret = lwip_recv(socket, buf, len, 0);
    if (ret > 0) {
        received += (uint32_t)ret;
    }
    return received > 0 ? (int)received : -1;
}
