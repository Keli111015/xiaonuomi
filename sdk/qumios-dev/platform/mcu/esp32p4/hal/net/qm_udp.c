#include "qm_udp.h"
#include "qm_log.h"
#include "qm_types.h"
#include "qm_errno.h"


#include <lwip/def.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

#define LOG_TAG "hal udp"
#define NETWORK_ADDR_LEN (16)

int qm_udp_server_create(char *host, uint16_t port)
{
    int                socket_id = -1;
    struct sockaddr_in local_addr; /*local addr*/

    if ((socket_id = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        QM_LOGE(LOG_TAG, "socket create failed\r\n");
        return -1;
    }

    memset(&local_addr, 0x00, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    if (NULL != host) {
        inet_aton(host, &local_addr.sin_addr);
    } else {
        local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    local_addr.sin_port = htons(port);
    bind(socket_id, (struct sockaddr *)&local_addr, sizeof(local_addr));

    return socket_id;
}

int qm_udp_client_create(char *host, uint16_t port)
{
    int                     rc = -1;
    long                    socket_id = -1;
    char                    port_ptr[6] = {0};
    struct addrinfo         hints;
    char                    addr[NETWORK_ADDR_LEN] = {0};
    struct addrinfo        *res, *ainfo;
    struct sockaddr_in     *sa = NULL;

    if (NULL == host) {
        return -1;
    }

    snprintf(port_ptr, 6, "%u", port);
    memset((char *)&hints, 0x00, sizeof(hints));
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family = AF_INET;
    hints.ai_protocol = IPPROTO_UDP;

    rc = getaddrinfo(host, port_ptr, &hints, &res);
    if (0 != rc) {
        QM_LOGE(LOG_TAG, "getaddrinfo error");
        return -1;
    }

    for (ainfo = res; ainfo != NULL; ainfo = ainfo->ai_next) {
        if (AF_INET == ainfo->ai_family) {
            sa = (struct sockaddr_in *)ainfo->ai_addr;
            inet_ntop(AF_INET, &sa->sin_addr, addr, NETWORK_ADDR_LEN);
            QM_LOGD(LOG_TAG, "The host IP %s, port is %d\r\n", addr, ntohs(sa->sin_port));

            socket_id = socket(ainfo->ai_family, ainfo->ai_socktype, ainfo->ai_protocol);
            if (socket_id < 0) {
                QM_LOGE(LOG_TAG, "create socket error");
                continue;
            }
            if (0 == connect(socket_id, ainfo->ai_addr, ainfo->ai_addrlen)) {
                break;
            }

            close(socket_id);
        }
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

int qm_udp_sendto(int socket, const char *buffer,
                uint32_t length, qm_netaddr_t *netaddr, uint32_t timeout_ms)
{
    int                rc        = -1;
    int                socket_id = -1;
    struct sockaddr_in remote_addr;

    if (socket < 0 || NULL == netaddr || NULL == buffer) {
        return -QM_EINVAL;
    }

    socket_id              = (int)socket;
    remote_addr.sin_family = AF_INET;
    if (1 !=
        (rc = inet_pton(remote_addr.sin_family, (const char *)netaddr->addr,
                        &remote_addr.sin_addr.s_addr))) {
        return -QM_ERROR;
    }
    remote_addr.sin_port = htons(netaddr->port);
    rc                   = sendto(socket_id, buffer, (size_t)length, 0,
                                  (const struct sockaddr *)&remote_addr, sizeof(remote_addr));
    if (-1 == rc) {
        return -QM_EIO;
    }
    return rc;
}

int qm_udp_recvfrom(int socket, char *buffer,
                  uint32_t length, qm_netaddr_t *netaddr, uint32_t timeout_ms)
{
    int             socket_id = -1;
    struct sockaddr from;
    int             count = -1, ret = -1;
    socklen_t       addrlen = 0;
    struct timeval  tv;
    fd_set          read_fds;

    if (socket < 0 || NULL == netaddr || NULL == buffer) {
        return -QM_EINVAL;
    }

    socket_id = (int)socket;

    if (socket_id < 0) {
        return -QM_EINVAL;
    }

    FD_ZERO(&read_fds);
    FD_SET(socket_id, &read_fds);

    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    ret = select(socket_id + 1, &read_fds, NULL, NULL,
                 timeout_ms == 0 ? NULL : &tv);

    /* Zero fds ready means we timed out */
    if (ret == 0) {
        return -QM_ETIMEOUT; /* receive timeout */
    }

    if (ret < 0) {
        if (errno == EINTR) {
            return 0; /* want read */
        }
        return -QM_EIO; /* receive failed */
    }

    addrlen = sizeof(struct sockaddr);
    count   = recvfrom(socket_id, buffer, (size_t)length, 0, &from, &addrlen);
    if (-1 == count) {
        return -QM_EIO;
    }
    if (from.sa_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)&from;
        inet_ntop(AF_INET, &sin->sin_addr, (char *)netaddr->addr,
                  NETWORK_ADDR_LEN);
        netaddr->port = ntohs(sin->sin_port);
    }
    return count;
}

int qm_udp_read(int socket, char *buffer, uint32_t length)
{
    int socket_id = -1;
    int  count    = -1;

    if (socket < 0 || NULL == buffer) {
        return -QM_EINVAL;
    }

    socket_id = (int)socket;
    
    if (socket_id < 0) {
        return -QM_EINVAL;
    }

    count     = (int)read(socket_id, buffer, length);
    return count;
}

int qm_udp_readtimeout(int socket, char *buffer, uint32_t length, uint32_t timeout_ms)
{   
    int            ret;
    struct timeval tv;
    fd_set         read_fds;
    int           socket_id = -1;

    if (socket < 0 || NULL == buffer) {
        return -QM_EINVAL;
    }

    socket_id = (int)socket;

    if (socket_id < 0) {
        return -QM_EINVAL;
    }

    FD_ZERO(&read_fds);
    FD_SET(socket_id, &read_fds);

    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    ret = select(socket_id + 1, &read_fds, NULL, NULL, timeout_ms == 0 ? NULL : &tv);

    /* Zero fds ready means we timed out */
    if (ret == 0) {
        return -QM_ETIMEOUT; /* receive timeout */
    }

    if (ret < 0) {
        if (errno == EINTR) {
            return 0; /* want read */
        }

        return -QM_EIO; /* receive failed */
    }

    /* This call will not block */
    return qm_udp_read(socket_id, buffer, length);
}

int qm_udp_write(int socket, char *buffer, uint32_t length, uint32_t timeout_ms)
{
    int            ret = 0;
    int            sockfd = -1;
    fd_set         write_fds;

    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };

    if (socket < 0 || NULL == buffer) {
        return -QM_EINVAL;
    }

    sockfd = (int)socket;
    
    if (sockfd < 0) {
        return -QM_EINVAL;
    }

    FD_ZERO(&write_fds);
    FD_SET(sockfd, &write_fds);

select:
    ret = select(sockfd + 1, NULL, &write_fds, NULL, timeout_ms == 0 ? NULL : &tv);
    if (ret == 0) {
        return -QM_ETIMEOUT;; /* write timeout */
    }

    if (ret < 0) {
        if (errno == EINTR) {
            goto select; /* want write */
        }
        return -QM_EIO;; /* write failed */
    }

    ret = send(sockfd, (char *)buffer, (int)length, 0);

    if (ret < 0) {
        QM_LOGE(LOG_TAG, "send err");
        return -QM_EIO;
    }

    return ret;
}