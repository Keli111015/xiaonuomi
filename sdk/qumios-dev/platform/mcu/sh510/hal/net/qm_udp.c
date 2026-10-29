#include "qm_udp.h"
#include "qm_log.h"
#include "qm_errno.h"
#include "qm_kernel.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define LOG_TAG "hal udp"

static int qm_udp_wait_readable(int socket_id, uint32_t timeout_ms)
{
    fd_set read_fds;
    struct timeval tv;
    int ret;

    if (timeout_ms == QM_WAIT_FOREVER) {
        return 1;
    }

    FD_ZERO(&read_fds);
    FD_SET(socket_id, &read_fds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ret = select(socket_id + 1, &read_fds, NULL, NULL, timeout_ms == 0 ? NULL : &tv);
    if (ret == 0) {
        return 0;
    }
    if (ret < 0) {
        return -1;
    }
    return 1;
}

int qm_udp_server_create(char *host, uint16_t port)
{
    int socket_id;
    int opt = 1;
    struct sockaddr_in local_addr;

    socket_id = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_id < 0) {
        return -1;
    }

    setsockopt(socket_id, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    if (host != NULL && host[0] != '\0') {
        local_addr.sin_addr.s_addr = inet_addr(host);
    } else {
        local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    local_addr.sin_port = htons(port);

    if (bind(socket_id, (struct sockaddr *)&local_addr, sizeof(local_addr)) != 0) {
        close(socket_id);
        return -1;
    }

    return socket_id;
}

int qm_udp_client_create(char *host, uint16_t port)
{
    int socket_id;
    struct addrinfo hints;
    struct addrinfo *res;
    struct addrinfo *ainfo;
    char port_ptr[8];
    int rc;

    if (host == NULL) {
        return -1;
    }

    snprintf(port_ptr, sizeof(port_ptr), "%u", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family = AF_INET;
    hints.ai_protocol = IPPROTO_UDP;

    rc = getaddrinfo(host, port_ptr, &hints, &res);
    if (rc != 0) {
        return -1;
    }

    socket_id = -1;
    for (ainfo = res; ainfo != NULL; ainfo = ainfo->ai_next) {
        socket_id = socket(ainfo->ai_family, ainfo->ai_socktype, ainfo->ai_protocol);
        if (socket_id < 0) {
            continue;
        }
        if (connect(socket_id, ainfo->ai_addr, ainfo->ai_addrlen) == 0) {
            break;
        }
        close(socket_id);
        socket_id = -1;
    }

    freeaddrinfo(res);
    return socket_id;
}

int qm_udp_close(int socket)
{
    if (socket < 0) {
        return -QM_EINVAL;
    }

    close(socket);
    return QM_EOK;
}

int qm_udp_sendto(int socket, const char *buffer, uint32_t length, qm_netaddr_t *netaddr, uint32_t timeout_ms)
{
    struct sockaddr_in remote_addr;
    int rc;

    (void)timeout_ms;

    if (socket < 0 || netaddr == NULL || buffer == NULL) {
        return -QM_EINVAL;
    }

    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(netaddr->port);
    if (inet_aton((const char *)netaddr->addr, &remote_addr.sin_addr) == 0) {
        return -QM_EINVAL;
    }

    rc = (int)sendto(socket, buffer, length, 0, (struct sockaddr *)&remote_addr, sizeof(remote_addr));
    if (rc < 0) {
        return -QM_EIO;
    }

    return rc;
}

int qm_udp_recvfrom(int socket, char *buffer, uint32_t length, qm_netaddr_t *netaddr, uint32_t timeout_ms)
{
    struct sockaddr_in from;
    socklen_t addrlen;
    int ready;
    int count;

    if (socket < 0 || netaddr == NULL || buffer == NULL) {
        return -QM_EINVAL;
    }

    ready = qm_udp_wait_readable(socket, timeout_ms);
    if (ready == 0) {
        return -QM_ETIMEOUT;
    }
    if (ready < 0) {
        return -QM_EIO;
    }

    addrlen = sizeof(from);
    count = (int)recvfrom(socket, buffer, length, 0, (struct sockaddr *)&from, &addrlen);
    if (count < 0) {
        return -QM_EIO;
    }

    inet_ntop(AF_INET, &from.sin_addr, (char *)netaddr->addr, QM_NETWORK_ADDR_LEN);
    netaddr->port = ntohs(from.sin_port);
    return count;
}

int qm_udp_read(int socket, char *buffer, uint32_t length)
{
    if (socket < 0 || buffer == NULL) {
        return -QM_EINVAL;
    }

    return (int)read(socket, buffer, length);
}

int qm_udp_readtimeout(int socket, char *buffer, uint32_t length, uint32_t timeout_ms)
{
    int ready;
    int count;

    if (socket < 0 || buffer == NULL) {
        return -QM_EINVAL;
    }

    ready = qm_udp_wait_readable(socket, timeout_ms);
    if (ready == 0) {
        return -QM_ETIMEOUT;
    }
    if (ready < 0) {
        return -QM_EIO;
    }

    count = (int)read(socket, buffer, length);
    if (count < 0) {
        return -QM_EIO;
    }

    return count;
}

int qm_udp_write(int socket, char *buffer, uint32_t length, uint32_t timeout_ms)
{
    fd_set write_fds;
    struct timeval tv;
    int ret;
    int sent = 0;

    if (socket < 0 || buffer == NULL) {
        return -1;
    }

    if (timeout_ms != QM_WAIT_FOREVER && timeout_ms != 0) {
        FD_ZERO(&write_fds);
        FD_SET(socket, &write_fds);
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ret = select(socket + 1, NULL, &write_fds, NULL, &tv);
        if (ret <= 0) {
            return -1;
        }
    }

    while ((uint32_t)sent < length) {
        ret = (int)write(socket, buffer + sent, length - sent);
        if (ret <= 0) {
            return -1;
        }
        sent += ret;
    }

    return sent;
}
