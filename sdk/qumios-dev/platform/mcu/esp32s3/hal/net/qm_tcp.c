#include "qm_tcp.h"
#include "qm_log.h"
#include "qm_types.h"
#include "qm_errno.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/err.h"

#define LOG_TAG "hal tcp"

int qm_tcp_server_creat(uint16_t port)
{
    int ret = 0;
    int servfd = 0;
    int opt = 1;
    struct sockaddr_in servaddr;
    servfd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    setsockopt(servfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons((unsigned short)port);
    ret = bind(servfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
    if (ret < 0) {
        QM_LOGE(LOG_TAG, "bind ip and port failed");
        (void)closesocket(servfd);
        return -1;
    }

    ret = listen(servfd, 6);
    if (ret < 0) {
        (void)closesocket(servfd);
        QM_LOGE(LOG_TAG, "listen failed\n");
        return -1;
    }
    return servfd;
}

int qm_tcp_accept(int socket, char ip[QM_NETWORK_ADDR_LEN], int timeout)
{
    int ret = 0;
    int rv = 0;
    fd_set set;
    int sockfd = socket;
    char *ipaddr = NULL;
    struct timeval timeval;
    struct sockaddr_in cliaddr;
    uint32_t cliaddr_size = (uint32_t)sizeof(cliaddr);

    FD_ZERO(&set); 
    FD_SET(sockfd, &set); 

    timeval.tv_sec = timeout/1000;
    timeval.tv_usec = (timeout%1000)*1000;

    memset(&cliaddr, 0, sizeof(struct sockaddr_in));

    rv = select(sockfd + 1, &set, NULL, NULL, &timeval);
    if(rv == -1){
        QM_LOGE(LOG_TAG, "select error"); 
        return -1;
    }else if(rv == 0){
        //QM_LOGE(LOG_TAG, "accept timeout"); 
        return -1;
    }else{
        if(FD_ISSET(sockfd, &set)){
            ret = (int)accept(sockfd, (struct sockaddr *)&cliaddr, &cliaddr_size);
            if (ret < 0) {
                (void)closesocket(sockfd);
                QM_LOGE(LOG_TAG, "accept failed, %d\n", ret);
                return -1;
            }
            ipaddr = inet_ntoa(cliaddr.sin_addr);
            strncpy(ip, ipaddr, QM_NETWORK_ADDR_LEN);  
        }
    }
    return ret;
}

int qm_tcp_establish(const char *host, uint16_t port)
{
    int err = 0;
    int fd = -1;
    struct sockaddr_in addr = {0};
    struct hostent *hostent_content = NULL;

    QM_LOGD(LOG_TAG, "host name: %s", host);
    hostent_content = gethostbyname( host );
    if(hostent_content == NULL){
        QM_LOGE(LOG_TAG, "gethostbyname err");
        return -1;
    }

    fd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        QM_LOGE(LOG_TAG, "socket creat err");
        return -1;
     }

     addr.sin_family = AF_INET;
     addr.sin_addr.s_addr = *(uint32_t *) (*hostent_content->h_addr_list);
     addr.sin_port = htons(port);

     QM_LOGD(LOG_TAG, "ip %s port %d fd %d\n", inet_ntoa(addr.sin_addr), port, fd);

     err = connect( fd, (struct sockaddr *) &addr, sizeof(addr) );
     if (err < 0) {
         QM_LOGE(LOG_TAG, "ERROR: Failed to connect socket %d.", fd);
         close(fd);
         return -1;
       }

    return fd;
}


static uint32_t _platform_time_left(uint32_t t_end, uint32_t t_now)
{
    uint32_t res;
    if(t_now - t_end < (UINT32_MAX/2)){
        return 0;
    }
    else{
        res = t_end-t_now;
        return res;
    }
}

int qm_tcp_destroy(int socket)
{
    int rc = 0;
    if(socket < 0){
        return -QM_EINVAL;
    }
    //Shutdown both send and receive operations.
    rc = shutdown(socket, 2);
    if (0 != rc) {
        QM_LOGE(LOG_TAG, "shutdown error");
        return -QM_EIO;
    }

    rc = close(socket);
    if (0 != rc) {
        QM_LOGE(LOG_TAG, "closesocket error");
        return -QM_EIO;
    }

    return QM_EOK;
}

int qm_tcp_write(int socket, const char *buf, uint32_t len, uint32_t timeout_ms)
{
    int to_write = len;
    while (to_write > 0) {
        int written = send(socket, buf + (len - to_write), to_write, 0);
        if (written < 0 && errno != EINPROGRESS && errno != EAGAIN && errno != EWOULDBLOCK) {
            QM_LOGE(LOG_TAG, "[sock=%d]: %s\n error=%d: %s", socket, "Error occurred during sending", errno, strerror(errno));
            return -1;
        }
        to_write -= written;
    }
    return len; 
}

int qm_tcp_read(int socket, char *buf, uint32_t len, uint32_t timeout_ms)
{
    int ret = 0, err_code = 0;
    uint32_t len_recv = 0;
    uint32_t t_end = 0, t_left = 0;
    fd_set sets = {0};
    struct timeval timeout = {0};
    int fd = (int)socket;

    if(socket < 0 || buf == NULL || len == 0){
        return -QM_EINVAL;
    }

    t_end = qm_now_ms() + timeout_ms;

    do {
        t_left = _platform_time_left(t_end, qm_now_ms());
        if (0 == t_left) {
            err_code = 0;
            break;
        }
        FD_ZERO(&sets);
        FD_SET(fd, &sets);

        timeout.tv_sec = t_left / 1000;
        timeout.tv_usec = (t_left % 1000) * 1000;

        ret = select(fd + 1, &sets, NULL, NULL, &timeout);
        if (ret > 0) {
            if (FD_ISSET(fd, &sets))
             {
                ret = recv(fd, buf + len_recv, len - len_recv, 0);
                if (ret > 0) 
                {
                    len_recv += ret;
                } else if (0 == ret) {

                    if ((EINTR == errno) || (EAGAIN == errno) || (EWOULDBLOCK == errno) ||
                        (EPROTOTYPE == errno) || (EALREADY == errno) || (EINPROGRESS == errno)) {
                        continue;
                    }

                    QM_LOGE(LOG_TAG, "connection is closed");
                    err_code = -QM_EIO;
                    break;
                } 
                else 
                {
                    if (EINTR == errno) {
                        QM_LOGE(LOG_TAG, "EINTR be caught");
                        continue;
                    }
                    QM_LOGE(LOG_TAG, "recv fail");
                    err_code = -QM_EIO;
                    break;
                 }
               }
            } else if (0 == ret) {
                break;
            } else {
                    QM_LOGE(LOG_TAG, "select-recv fail");
                    err_code = -QM_EIO;
                    break;
        }
    } while ((len_recv < len) && (_platform_time_left(t_end, qm_now_ms()) > 0));

    /* priority to return data bytes if any data be received from TCP connection. */
    /* It will get error code on next calling */
    return (0 != len_recv) ? len_recv : err_code;
}