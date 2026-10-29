#include "qm_udp.h"
#include "qm_errno.h"
#include "qm_kernel.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

static int wait_readable(int fd, uint32_t timeout_ms)
{
    fd_set set;
    struct timeval timeout;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    if (timeout_ms == QM_WAIT_FOREVER) {
        return select(fd + 1, &set, NULL, NULL, NULL);
    }
    timeout.tv_sec = timeout_ms / 1000u;
    timeout.tv_usec = (int)(timeout_ms % 1000u) * 1000;
    return select(fd + 1, &set, NULL, NULL, timeout_ms == 0 ? NULL : &timeout);
}

int qm_udp_server_create(char *host, uint16_t port)
{
    struct sockaddr_in address;
    int fd;
    int reuse = 1;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = host == NULL || host[0] == '\0' ? htonl(INADDR_ANY) : inet_addr(host);
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int qm_udp_client_create(char *host, uint16_t port)
{
    struct addrinfo hints;
    struct addrinfo *results;
    char port_string[8];
    int fd = -1;

    if (host == NULL) {
        return -1;
    }
    snprintf(port_string, sizeof(port_string), "%u", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, port_string, &hints, &results) != 0) {
        return -1;
    }
    if (results != NULL) {
        fd = socket(results->ai_family, results->ai_socktype, results->ai_protocol);
        if (fd >= 0 && connect(fd, results->ai_addr, results->ai_addrlen) != 0) {
            close(fd);
            fd = -1;
        }
    }
    freeaddrinfo(results);
    return fd;
}

int qm_udp_close(int socket_fd)
{
    if (socket_fd < 0) {
        return -QM_EINVAL;
    }
    return close(socket_fd) == 0 ? QM_EOK : -QM_EIO;
}

int qm_udp_sendto(int socket_fd, const char *buffer, uint32_t length,
                  qm_netaddr_t *netaddr, uint32_t timeout_ms)
{
    struct sockaddr_in remote;
    (void)timeout_ms;
    if (socket_fd < 0 || buffer == NULL || netaddr == NULL) {
        return -QM_EINVAL;
    }
    memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_port = htons(netaddr->port);
    if (inet_aton((const char *)netaddr->addr, &remote.sin_addr) == 0) {
        return -QM_EINVAL;
    }
    return (int)sendto(socket_fd, buffer, length, 0, (struct sockaddr *)&remote, sizeof(remote));
}

int qm_udp_recvfrom(int socket_fd, char *buffer, uint32_t length,
                    qm_netaddr_t *netaddr, uint32_t timeout_ms)
{
    struct sockaddr_in remote;
    socklen_t remote_len = sizeof(remote);
    int rc;
    if (socket_fd < 0 || buffer == NULL || netaddr == NULL) {
        return -QM_EINVAL;
    }
    rc = wait_readable(socket_fd, timeout_ms);
    if (rc == 0) {
        return -QM_ETIMEOUT;
    }
    if (rc < 0) {
        return -QM_EIO;
    }
    rc = (int)recvfrom(socket_fd, buffer, length, 0, (struct sockaddr *)&remote, &remote_len);
    if (rc < 0) {
        return -QM_EIO;
    }
    inet_ntop(AF_INET, &remote.sin_addr, (char *)netaddr->addr, QM_NETWORK_ADDR_LEN);
    netaddr->port = ntohs(remote.sin_port);
    return rc;
}

int qm_udp_read(int socket_fd, char *buffer, uint32_t length)
{
    return socket_fd < 0 || buffer == NULL ? -QM_EINVAL : (int)read(socket_fd, buffer, length);
}

int qm_udp_readtimeout(int socket_fd, char *buffer, uint32_t length, uint32_t timeout_ms)
{
    int rc;
    if (socket_fd < 0 || buffer == NULL) {
        return -QM_EINVAL;
    }
    rc = wait_readable(socket_fd, timeout_ms);
    if (rc == 0) {
        return -QM_ETIMEOUT;
    }
    if (rc < 0) {
        return -QM_EIO;
    }
    return (int)read(socket_fd, buffer, length);
}

int qm_udp_write(int socket_fd, char *buffer, uint32_t length, uint32_t timeout_ms)
{
    fd_set set;
    struct timeval timeout;
    uint32_t sent = 0;
    if (socket_fd < 0 || buffer == NULL) {
        return -QM_EINVAL;
    }
    if (timeout_ms != QM_WAIT_FOREVER) {
        FD_ZERO(&set);
        FD_SET(socket_fd, &set);
        timeout.tv_sec = timeout_ms / 1000u;
        timeout.tv_usec = (int)(timeout_ms % 1000u) * 1000;
        if (select(socket_fd + 1, NULL, &set, NULL, timeout_ms == 0 ? NULL : &timeout) <= 0) {
            return -QM_ETIMEOUT;
        }
    }
    while (sent < length) {
        ssize_t rc = write(socket_fd, buffer + sent, length - sent);
        if (rc <= 0) {
            return -QM_EIO;
        }
        sent += (uint32_t)rc;
    }
    return (int)sent;
}

