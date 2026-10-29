#include "qm_tcp.h"
#include "qm_errno.h"
#include "qm_kernel.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

static int wait_fd(int fd, int write_ready, uint32_t timeout_ms)
{
    fd_set set;
    struct timeval timeout;
    int rc;

    FD_ZERO(&set);
    FD_SET(fd, &set);
    if (timeout_ms == QM_WAIT_FOREVER) {
        return select(fd + 1, write_ready ? NULL : &set, write_ready ? &set : NULL, NULL, NULL);
    }
    timeout.tv_sec = timeout_ms / 1000u;
    timeout.tv_usec = (int)(timeout_ms % 1000u) * 1000;
    rc = select(fd + 1, write_ready ? NULL : &set, write_ready ? &set : NULL, NULL,
                &timeout);
    return rc;
}

int qm_tcp_server_creat(uint16_t port)
{
    struct sockaddr_in address;
    int fd;
    int reuse = 1;

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        return -1;
    }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(fd, 6) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int qm_tcp_accept(int socket_fd, char ip[QM_NETWORK_ADDR_LEN], int timeout_ms)
{
    struct sockaddr_in peer;
    socklen_t length = sizeof(peer);
    int fd;

    if (socket_fd < 0 || ip == NULL || wait_fd(socket_fd, 0, timeout_ms) <= 0) {
        return -1;
    }
    fd = (int)accept(socket_fd, (struct sockaddr *)&peer, &length);
    if (fd < 0) {
        return -1;
    }
    if (inet_ntop(AF_INET, &peer.sin_addr, ip, QM_NETWORK_ADDR_LEN) == NULL) {
        ip[0] = '\0';
    }
    return fd;
}

int qm_tcp_establish(const char *host, uint16_t port)
{
    struct addrinfo hints;
    struct addrinfo *results;
    struct addrinfo *entry;
    char port_string[8];
    int fd = -1;

    if (host == NULL) {
        return -1;
    }
    snprintf(port_string, sizeof(port_string), "%u", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port_string, &hints, &results) != 0) {
        return -1;
    }
    for (entry = results; entry != NULL; entry = entry->ai_next) {
        fd = socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
        if (fd >= 0 && connect(fd, entry->ai_addr, entry->ai_addrlen) == 0) {
            break;
        }
        if (fd >= 0) {
            close(fd);
            fd = -1;
        }
    }
    freeaddrinfo(results);
    return fd;
}

int qm_tcp_destroy(int socket_fd)
{
    if (socket_fd < 0) {
        return -QM_EINVAL;
    }
    shutdown(socket_fd, SHUT_RDWR);
    return close(socket_fd) == 0 ? QM_EOK : -QM_EIO;
}

int qm_tcp_write(int socket_fd, const char *buf, uint32_t len, uint32_t timeout_ms)
{
    uint32_t sent = 0;
    if (socket_fd < 0 || (buf == NULL && len != 0)) {
        return -QM_EINVAL;
    }
    while (sent < len) {
        ssize_t rc;
        if (wait_fd(socket_fd, 1, timeout_ms) <= 0) {
            return sent != 0 ? (int)sent : 0;
        }
        rc = send(socket_fd, buf + sent, len - sent, 0);
        if (rc > 0) {
            sent += (uint32_t)rc;
        } else if (rc < 0 && errno == EINTR) {
            continue;
        } else {
            return -QM_EIO;
        }
    }
    return (int)sent;
}

int qm_tcp_read(int socket_fd, char *buf, uint32_t len, uint32_t timeout_ms)
{
    uint32_t received = 0;
    if (socket_fd < 0 || buf == NULL || len == 0) {
        return -QM_EINVAL;
    }
    while (received < len) {
        ssize_t rc;
        if (wait_fd(socket_fd, 0, timeout_ms) <= 0) {
            break;
        }
        rc = recv(socket_fd, buf + received, len - received, 0);
        if (rc > 0) {
            received += (uint32_t)rc;
        } else if (rc == 0) {
            return received != 0 ? (int)received : -QM_EIO;
        } else if (errno != EINTR) {
            return received != 0 ? (int)received : -QM_EIO;
        }
    }
    return (int)received;
}
