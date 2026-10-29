/**
 * @file qm_udp.c
 * @brief BK7258 lwIP implementation of the QUMI UDP HAL.
 */
#include "qm_udp.h"
#include "qm_errno.h"
#include "qm_kernel.h"

#include <errno.h>
#include <string.h>

#include <lwip/def.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

static int wait_for_socket(int socket_fd, int write_ready, uint32_t timeout_ms)
{
    fd_set set;
    struct timeval timeout;
    struct timeval *timeout_ptr;
    int result;

    do {
        FD_ZERO(&set);
        FD_SET(socket_fd, &set);
        timeout_ptr = NULL;
        if (timeout_ms != 0U && timeout_ms != QM_WAIT_FOREVER) {
            timeout.tv_sec = timeout_ms / 1000U;
            timeout.tv_usec = (int)(timeout_ms % 1000U) * 1000;
            timeout_ptr = &timeout;
        }
        result = select(socket_fd + 1, write_ready ? NULL : &set, write_ready ? &set : NULL,
                        NULL, timeout_ptr);
    } while (result < 0 && errno == EINTR);
    if (result == 0)
        return -QM_ETIMEOUT;
    return result < 0 ? -QM_EIO : QM_EOK;
}

int qm_udp_server_create(char *host, uint16_t port)
{
    struct sockaddr_in address;
    int socket_fd;
    int reuse = 1;

    socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0)
        return -1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
        (void)close(socket_fd);
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (!host || !host[0]) {
        address.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_aton(host, &address.sin_addr) == 0) {
        (void)close(socket_fd);
        return -1;
    }
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        (void)close(socket_fd);
        return -1;
    }
    return socket_fd;
}

int qm_udp_client_create(char *host, uint16_t port)
{
    struct sockaddr_in address;
    struct hostent *host_entry = NULL;
    int socket_fd;

    if (!host || !host[0])
        return -1;
    socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0)
        return -1;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_aton(host, &address.sin_addr) == 0) {
        host_entry = gethostbyname(host);
        if (!host_entry || !host_entry->h_addr_list || !host_entry->h_addr_list[0]) {
            (void)close(socket_fd);
            return -1;
        }
        memcpy(&address.sin_addr, host_entry->h_addr_list[0], sizeof(address.sin_addr));
    }
    if (connect(socket_fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        (void)close(socket_fd);
        return -1;
    }
    return socket_fd;
}

int qm_udp_close(int socket_fd)
{
    if (socket_fd < 0)
        return -QM_EINVAL;
    return close(socket_fd) == 0 ? QM_EOK : -QM_EIO;
}

int qm_udp_sendto(int socket_fd, const char *buffer, uint32_t length, qm_netaddr_t *netaddr,
                  uint32_t timeout_ms)
{
    struct sockaddr_in remote;
    int result;

    if (socket_fd < 0 || !buffer || !netaddr)
        return -QM_EINVAL;
    result = wait_for_socket(socket_fd, 1, timeout_ms);
    if (result != QM_EOK)
        return result;
    memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_port = htons(netaddr->port);
    if (inet_aton(netaddr->addr, &remote.sin_addr) == 0)
        return -QM_EINVAL;
    result = (int)sendto(socket_fd, buffer, length, 0, (struct sockaddr *)&remote, sizeof(remote));
    return result < 0 ? -QM_EIO : result;
}

int qm_udp_recvfrom(int socket_fd, char *buffer, uint32_t length, qm_netaddr_t *netaddr,
                    uint32_t timeout_ms)
{
    struct sockaddr_in remote;
    socklen_t remote_length = sizeof(remote);
    const char *address_text;
    int result;

    if (socket_fd < 0 || !buffer || !netaddr)
        return -QM_EINVAL;
    result = wait_for_socket(socket_fd, 0, timeout_ms);
    if (result != QM_EOK)
        return result;
    result = (int)recvfrom(socket_fd, buffer, length, 0, (struct sockaddr *)&remote, &remote_length);
    if (result < 0)
        return -QM_EIO;
    address_text = inet_ntoa(remote.sin_addr);
    if (!address_text)
        return -QM_EIO;
    (void)strncpy(netaddr->addr, address_text, QM_NETWORK_ADDR_LEN - 1U);
    netaddr->addr[QM_NETWORK_ADDR_LEN - 1U] = '\0';
    netaddr->port = ntohs(remote.sin_port);
    return result;
}

int qm_udp_read(int socket_fd, char *buffer, uint32_t length)
{
    int result;

    if (socket_fd < 0 || !buffer)
        return -QM_EINVAL;
    result = (int)recv(socket_fd, buffer, length, 0);
    return result < 0 ? -QM_EIO : result;
}

int qm_udp_readtimeout(int socket_fd, char *buffer, uint32_t length, uint32_t timeout_ms)
{
    int result;

    if (socket_fd < 0 || !buffer)
        return -QM_EINVAL;
    result = wait_for_socket(socket_fd, 0, timeout_ms);
    return result == QM_EOK ? qm_udp_read(socket_fd, buffer, length) : result;
}

int qm_udp_write(int socket_fd, char *buffer, uint32_t length, uint32_t timeout_ms)
{
    int result;

    if (socket_fd < 0 || !buffer)
        return -QM_EINVAL;
    result = wait_for_socket(socket_fd, 1, timeout_ms);
    if (result != QM_EOK)
        return result;
    result = (int)send(socket_fd, buffer, length, 0);
    return result < 0 ? -QM_EIO : result;
}
