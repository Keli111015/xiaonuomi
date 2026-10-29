#include "qm_tcp.h"
#include "qm_log.h"
#include "qm_types.h"
#include "qm_errno.h"

#include "aos/aos.h"
#include <aos/errno.h>
#include <aos/network.h>

#define LOG_TAG "hal tcp"

void* qm_tcp_establish(const char *host, uint16_t port)
{
    int err = 0;
    int fd = -1;
    struct sockaddr_in addr = {0};
    struct hostent *hostent_content = NULL;

    QM_LOGD(LOG_TAG, "host name: %s", host);
    hostent_content = gethostbyname( host );
    if(hostent_content == NULL){
        QM_LOGE(LOG_TAG, "gethostbyname err");
        return NULL;
    }

    fd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        QM_LOGE(LOG_TAG, "socket creat err");
        return NULL;
     }

     addr.sin_family = AF_INET;
     addr.sin_addr.s_addr = *(uint32_t *) (*hostent_content->h_addr_list);
     addr.sin_port = htons(port);

     QM_LOGD(LOG_TAG, "ip %s port %d fd %d\n", inet_ntoa(addr.sin_addr), port, fd);

     err = connect( fd, (struct sockaddr *) &addr, sizeof(addr) );
     if (err < 0) {
         QM_LOGE(LOG_TAG, "ERROR: Failed to connect socket %d.", fd);
         close(fd);
         return NULL;
       }

    return (void*)fd;
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

int32_t qm_tcp_destroy(void *handle)
{
    int rc = 0;
    if(handle == NULL){
        return -QM_EINVAL;
    }
    //Shutdown both send and receive operations.
    rc = shutdown((int)handle, 2);
    if (0 != rc) {
        QM_LOGE(LOG_TAG, "shutdown error");
        return -QM_EIO;
    }

    rc = close((int) handle);
    if (0 != rc) {
        QM_LOGE(LOG_TAG, "closesocket error");
        return -QM_EIO;
    }

    return QM_EOK;

}

int32_t qm_tcp_write(void *handle, const char *buf, uint32_t len, uint32_t timeout_ms)
{
    int ret = 0, err_code = 0;
    uint32_t len_sent = 0;
    uint32_t t_end = 0, t_left = 0;
    fd_set sets = {0};
    struct timeval timeout = {0};
    int fd = -1;

    if(handle == NULL || buf == NULL || len == 0){
        return -QM_EINVAL;
    }

    t_end = qm_now_ms() + timeout_ms;

    fd = (int)handle;

    do {
        t_left = _platform_time_left(t_end, qm_now_ms());

        if (0 != t_left) {
            
            FD_ZERO(&sets);
            FD_SET(fd, &sets);

            timeout.tv_sec  = t_left / 1000;
            timeout.tv_usec = (t_left % 1000) * 1000;

            ret = select(fd + 1, NULL, &sets, NULL, &timeout);
            if (ret > 0) {
                if (0 == FD_ISSET(fd, &sets)) {
                    QM_LOGD(LOG_TAG, "Should NOT arrive");
                    // If timeout in next loop, it will not sent any data
                    ret = 0;
                    continue;
                }
            } else if (0 == ret) {
                // PLATFORM_LOG_D("select-write timeout %lu", fd);
                break;
            } else {
                if (EINTR == errno) {
                    QM_LOGD(LOG_TAG, "EINTR be caught");
                    continue;
                }

                err_code = -QM_EIO;
                QM_LOGE(LOG_TAG, "select-write fail");
                break;
            }
        }

        if (ret > 0) {
            ret = send(fd, buf + len_sent, len - len_sent, 0);
            if (ret > 0) {
                len_sent += ret;
            } else if (0 == ret) {
                QM_LOGD(LOG_TAG, "No data be sent");
            } else {
                if (EINTR == errno) {
                    QM_LOGD(LOG_TAG, "EINTR be caught");
                    continue;
                }

                err_code = -QM_EIO;
                QM_LOGE(LOG_TAG, "send fail");
                break;
            }
        }
    } while ((len_sent < len) &&
             (_platform_time_left(t_end, qm_now_ms()) > 0));

    return err_code == 0 ? len_sent : err_code; 
}

int32_t qm_tcp_read(void *handle, char *buf, uint32_t len, uint32_t timeout_ms)
{
    int ret = 0, err_code = 0;
    uint32_t len_recv = 0;
    uint32_t t_end = 0, t_left = 0;
    fd_set sets = {0};
    struct timeval timeout = {0};
    int fd = (int)handle;

    if(handle == NULL || buf == NULL || len == 0){
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