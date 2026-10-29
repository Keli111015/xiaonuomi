#include "prodtst_hal_tcp.h"
#include "qm_log.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/err.h"
#include "sys/socket.h"
#include "errno.h"
#include "qm_kernel.h"
#include "qm_errno.h"

#define LOG_TAG  "prodtst tcp"

/** @defgroup group_platform_network network
 *  @{
 */
/**
 * @brief Establish a TCP server.
 *
 * @param [in] port: @n Specify the TCP port of TCP server
 *
 * @return The handle of TCP connection.
   @retval   0 : Fail.
   @retval > 0 : Success, the value is handle of this TCP connection.
 */
int32_t prodtst_tcp_server_creat(uint16_t port)
{
    int ret = 0;
    int servfd = -1;
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

int32_t prodtst_tcp_accept(int sockfd, int timeout)
{
    int ret = 0;
    int rv = 0;
    fd_set set;
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
        return -QM_EIO;
    }else if(rv == 0){
        //QM_LOGE(LOG_TAG, "accept timeout"); 
        return -QM_ETIMEOUT;
    }else{
        if(FD_ISSET(sockfd, &set)){
            ret = (int)accept(sockfd, (struct sockaddr *)&cliaddr, &cliaddr_size);
            if (ret < 0) {
                (void)closesocket(sockfd);
                QM_LOGE(LOG_TAG, "accept failed, %d\n", ret);
                return -QM_EIO;
            }
        }
    }
    return ret;
}

/**
 * @brief Establish a TCP connection.
 *
 * @param [in] host: @n Specify the hostname(IP) of the TCP server
 * @param [in] port: @n Specify the TCP port of TCP server
 *
 * @return The handle of TCP connection.
   @retval   0 : Fail.
   @retval > 0 : Success, the value is handle of this TCP connection.
 */
int32_t prodtst_tcp_establish(const char *host, uint16_t port)
{
    int err = 0;
    int fd = -1;
    struct sockaddr_in addr = {0};
    struct hostent *hostent_content = NULL;

    QM_LOGI(LOG_TAG, "host name: %s", host);
    hostent_content = gethostbyname( host );
    if(hostent_content == NULL)
    {
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

    QM_LOGI(LOG_TAG, "ip %s port %d fd %d\n", inet_ntoa(addr.sin_addr), port, fd);

    err = connect( fd, (struct sockaddr *) &addr, sizeof(addr) );
    if (err < 0) {
        QM_LOGE(LOG_TAG, "ERROR: Failed to connect socket %d.", fd);
        close(fd);
        return -1;
    }

    return fd;
}

int32_t prodtst_tcp_establish_timeout(const char *host, uint16_t port, int timeout_ms)
{
    int err = 0;
    int fd = -1;
    int ret = 0;
    fd_set wsets = {0};
    struct timeval timeout;  
    struct sockaddr_in addr = {0};
    struct hostent *hostent_content = NULL;

    QM_LOGI(LOG_TAG, "host name: %s", host);
    hostent_content = gethostbyname( host );
    if(hostent_content == NULL){
        QM_LOGE(LOG_TAG, "gethostbyname err");
        return -1;
    }

    fd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        QM_LOGE(LOG_TAG, "socket creat fail");
        return -1;
    }

    unsigned long ul = 1;
    ioctlsocket(fd, FIONBIO, &ul);

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = *(uint32_t *) (*hostent_content->h_addr_list);
    addr.sin_port = htons(port);

    QM_LOGI(LOG_TAG, "ip %s port %d fd %d\n", inet_ntoa(addr.sin_addr), port, fd);

    err = connect( fd, (struct sockaddr *) &addr, sizeof(addr) );
    if (err == -1 && errno == EINPROGRESS) {
        FD_ZERO(&wsets);
        FD_SET(fd, &wsets);
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;
        if( select(fd+1, NULL, &wsets, NULL, &timeout) <= 0){
            ret = 0;
        }else{
            if(FD_ISSET(fd, &wsets)){
                int error = -1;  
                int optLen = sizeof(int);  
                getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&error, (socklen_t*)&optLen);   
                if (0 != error)  {  
                    ret = 0; 
                }  
                else {  
                    ret = 1;  
                } 
            }
        }
    }

    ul = 0;
    ioctlsocket(fd, FIONBIO, &ul);

    if(ret){
        QM_LOGI(LOG_TAG, "connect success");
        return fd;
    }else{
        QM_LOGI(LOG_TAG, "connect fail");
        close(fd);
        return -1;
    }
}


static uint32_t _platform_time_left(uint32_t t_end, uint32_t t_now)
{
    uint32_t res = 0;
    if(t_now - t_end < (UINT32_MAX/2)){
      return 0;
    }
    else{
      res=t_end-t_now;
      return res;
    }
    
}


/**
 * @brief Destroy the specific TCP connection.
 *
 * @param [in] fd: @n Specify the TCP connection by handle.
 *
 * @return The result of destroy TCP connection.
 * @retval < 0 : Fail.
 * @retval   0 : Success.
 */
int32_t prodtst_tcp_destroy(int fd)
{
    int rc = 0;

    rc = close((int) fd);

    if (0 != rc) {
        QM_LOGE(LOG_TAG, "closesocket error");
        return -1;
    }

    return 0;
}


int32_t prodtst_tcp_write(int fd, const char *buf, uint32_t len, uint32_t timeout_ms)
{
    int to_write = len;
    while (to_write > 0) {
        int written = send(fd, buf + (len - to_write), to_write, 0);
        if (written < 0 && errno != EINPROGRESS && errno != EAGAIN && errno != EWOULDBLOCK) {
            QM_LOGE(LOG_TAG, "[sock=%d]: %s\n error=%d: %s", fd, "Error occurred during sending", errno, strerror(errno));
            return -1;
        }
        to_write -= written;
    }
    return len;  
}


int32_t prodtst_tcp_try_read(int fd, char *buf, uint32_t len)
{
    int ret = recv(fd, buf, len, 0);
    if (ret < 0) {
        if (errno == EINPROGRESS || errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;   // Not an error
        }
        if (errno == ENOTCONN) {
            QM_LOGE(LOG_TAG, "[sock=%d]: Connection closed", fd);
            return -2;  // Socket has been disconnected
        }

        QM_LOGE(LOG_TAG, "[sock=%d]: %s\n error=%d: %s", fd, "Error occurred during receiving", errno, strerror(errno));
        return -1;
    }

    return ret;
}

int32_t prodtst_tcp_read(int fd, char *buf, uint32_t len, uint32_t timeout_ms)
{
    int ret = 0, err_code = 0;
    uint32_t len_recv = 0;
    uint32_t t_end = 0, t_left = 0;
    fd_set sets = {0};
    struct timeval timeout = {0};

    t_end = qm_now_ms() + timeout_ms;
    len_recv = 0;
    err_code = 0;

    do {
        t_left = _platform_time_left(t_end, qm_now_ms());
        FD_ZERO(&sets);
        FD_SET(fd, &sets);

        timeout.tv_sec = t_left / 1000;
        timeout.tv_usec = (t_left % 1000) * 1000;

        ret = select(fd + 1, &sets, NULL, NULL, timeout_ms == 0 ? NULL : &timeout);
        if (ret > 0) {
            if (FD_ISSET(fd, &sets))
             {
                ret = recv(fd, buf + len_recv, len - len_recv, 0);
                if (ret > 0) 
                {
                    len_recv += ret;
                } else if (0 == ret) {
                    QM_LOGI(LOG_TAG, "connection is closed");
                    err_code = -1;
                    break;
                } 
                else 
                {
                    if (EINTR == errno) {
                        QM_LOGE(LOG_TAG, "EINTR be caught");
                        continue;
                    }
                    QM_LOGE(LOG_TAG, "recv fail");
                    err_code = -2;
                    break;
                 }
               }
            } else if (0 == ret) {
                break;
            } else {
                    QM_LOGE(LOG_TAG, "select-recv fail");
                    err_code = -2;
                    break;
        }
    } while ((len_recv < len) && (_platform_time_left(t_end, qm_now_ms()) > 0));

    /* priority to return data bytes if any data be received from TCP connection. */
    /* It will get error code on next calling */
    return (0 != len_recv) ? len_recv : err_code;
}
