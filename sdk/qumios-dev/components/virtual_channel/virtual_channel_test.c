#include "virtual_channel.h"
#include "qm_kernel.h"
#include "qm_log.h"

#define LOG_TAG "vc_test"

static void vc_client_task(void *arg)
{   
    int ret = 0;
    int read_len = 0;
    int client_fd = 0;
    char *data = "hello";
    uint8_t buf[64] = {0};
    client_fd = virtual_channel_client_creat(512, 512);
    QM_LOGD(LOG_TAG, "vc client creat, client fd: %d", client_fd);
connect:
    ret = virtual_channel_connect(client_fd, NULL, 1, 1000);
    if(ret != QM_EOK){
        QM_LOGD(LOG_TAG, "connect failed");
        qm_msleep(1000);
        goto connect;
    }

    QM_LOGD(LOG_TAG, "vc client connect success");

    while(1){
        ret = virtual_channel_write(client_fd, (uint8_t*)data, strlen(data)+1);
        if(ret < 0){
            QM_LOGD(LOG_TAG, "client write error");
            break;
        }
        read_len = virtual_channel_read(client_fd, buf, 10, 2000);
        if(read_len > 0){
            QM_LOGD(LOG_TAG, "client read: %s", buf);
        }else{
            QM_LOGD(LOG_TAG, "client read timeout");
        }
        qm_msleep(1000);
    }
    qm_task_exit(NULL);
}

static void vc_client_2_task(void *arg)
{   
    int ret = 0;
    int read_len = 0;
    int client_fd = 0;
    char *data = "hello";
    uint8_t buf[64] = {0};
    client_fd = virtual_channel_client_creat(512, 512);
    QM_LOGD(LOG_TAG, "vc client 2 creat, client fd: %d", client_fd);
connect:
    ret = virtual_channel_connect(client_fd, NULL, 1, 1000);
    if(ret != QM_EOK){
        QM_LOGD(LOG_TAG, "connect failed");
        qm_msleep(1000);
        goto connect;
    }

    QM_LOGD(LOG_TAG, "vc client 2 connect success");

    while(1){
        ret = virtual_channel_write(client_fd, (uint8_t*)data, strlen(data)+1);
        if(ret < 0){
            QM_LOGD(LOG_TAG, "client 2 write error");
            break;
        }
        read_len = virtual_channel_read(client_fd, buf, 10, 2000);
        if(read_len > 0){
            QM_LOGD(LOG_TAG, "client 2 read: %s", buf);
        }else{
            QM_LOGD(LOG_TAG, "client 2 read timeout");
        }

        qm_msleep(1000);
    }

    qm_task_exit(NULL);
}


static void vc_listen_task(void *arg)
{   
    int ret = 0;
    int read_len = 0;
    int listen_fd = 0;
    uint8_t buf[64] = {0};
    listen_fd = (int)arg;
    char *data = "hello ack";

    QM_LOGD(LOG_TAG, "vc listen creat, listen fd: %d", listen_fd);

    while(1){
        
        read_len = virtual_channel_read(listen_fd, buf, 6, 2000);
        if(read_len > 0){
            QM_LOGD(LOG_TAG, "listen read: %s", buf);
            ret = virtual_channel_write(listen_fd, (uint8_t*)data, strlen(data)+1);
            if(ret < 0){
                QM_LOGD(LOG_TAG, "listen write error");
            }
            virtual_channel_destroy(listen_fd);
            break;
        }else if(read_len == 0){
            QM_LOGD(LOG_TAG, "listen read timeout");
        }else{
            QM_LOGD(LOG_TAG, "io error");
            break;
        }
    }
    qm_task_exit(NULL);
}


static void vc_server_task(void *arg)
{
    int server_fd = 0;
    int listen_fd = 0;
    qm_task_t task = {0};
    server_fd = virtual_channel_server_creat(NULL, 1, 512, 512);

    QM_LOGD(LOG_TAG, "vc server creat, server fd: %d", server_fd);

    while(1){
        listen_fd = virtual_channel_accept(server_fd);
        if(listen_fd == 0){
            QM_LOGD(LOG_TAG, "vc accept fail");
            continue;
        }

        QM_LOGD(LOG_TAG, "vc accept success listen fd: %d", listen_fd);

        qm_task_new(&task, "listen", vc_listen_task, (void*)listen_fd, 4096, 20);
    }
}


void virtual_channel_test(void)
{
    qm_task_t server_task = {0};
    qm_task_t client_task = {0};
    qm_task_t client_2_task = {0};
    qm_task_new(&server_task, "server", vc_server_task, NULL, 4096, 20);
    qm_task_new(&client_task, "client", vc_client_task, NULL, 4096, 20);
    qm_task_new(&client_2_task, "client_2", vc_client_2_task, NULL, 4096, 20);
}