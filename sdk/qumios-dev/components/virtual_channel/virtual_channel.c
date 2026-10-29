#include "virtual_channel.h"
#include "qm_errno.h"

enum{
    VC_ROLE_NONE,
    VC_ROLE_CLIENT,
    VC_ROLE_LISTEN,
    VC_ROLE_SERVER
};

#define VC_HOST_MAX_LEN    (16)

typedef struct {
    int role;
    int fd;
    int err_code;
    int server_fd;
    int client_fd;
    uint32_t rd_size;
    uint32_t wr_size;
    uint8_t *buf;
    qm_mutex_t lock;
    qm_sem_t signal;
    qm_ringbuf_t ringbuf; 
}vc_node_t;

typedef struct {
    int fd;
    char host[VC_HOST_MAX_LEN + 1];
    int port;
    uint32_t rd_size;
    uint32_t wr_size;
    qm_sem_t accept;
    qm_mutex_t lock;
    vc_node_t vc_node[CONFIG_VC_LISTEN_NUM];
}vc_server_t;


static vc_server_t g_vc_server[CONFIG_VC_SERVER_NUM] = {0};
static vc_node_t g_vc_node[CONFIG_VC_CLIENT_NUM]     = {0};

static int vc_server_fd_new(void)
{
    int i = 0;
    for(i = 0; i < CONFIG_VC_SERVER_NUM; i++){
        if(g_vc_server[i].fd == 0){
            g_vc_server[i].fd = (i + 1) << 8;
            return g_vc_server[i].fd;
        }
    }
    return 0;
}

static int vc_server_fd_del(int fd)
{
    fd = fd >> 8;

    if(fd > CONFIG_VC_SERVER_NUM || fd == 0){
        return -QM_EINVAL;
    }

    g_vc_server[fd - 1].fd = 0;

    return QM_EOK;
}

static int vc_server_fd_is_valid(int fd)
{
    if((uint8_t)fd != 0){
        return 0;
    }

    fd >>= 8;

    if( fd > CONFIG_VC_SERVER_NUM || fd == 0){
        return 0;
    }
    return 1;
}

static vc_server_t *vc_server_get(int fd)
{
    if(!vc_server_fd_is_valid(fd)){
        return NULL;
    }

    fd >>= 8;

    if(g_vc_server[fd - 1].fd == 0){
        return NULL;
    }
    return &g_vc_server[fd - 1];
}


static int vc_listen_fd_is_valid(int fd)
{
    int server_fd = 0;
    int listen_fd = 0;

    if((uint8_t)fd == 0){
        return 0;
    }

    server_fd = fd >> 8;
    listen_fd = (uint8_t)fd;

    if( server_fd > CONFIG_VC_SERVER_NUM || server_fd == 0 ){
        return 0;
    }
    
    if(listen_fd > CONFIG_VC_LISTEN_NUM || listen_fd == 0){
        return 0;
    }
    return 1;
}

static vc_node_t *vc_listen_get(int fd)
{
    int server_fd = 0;
    int listen_fd = 0;

    if(!vc_listen_fd_is_valid(fd)){
        return NULL;
    }

    server_fd = fd >> 8;
    listen_fd = (uint8_t)fd;

    if(g_vc_server[server_fd - 1].fd == 0){
        return NULL;
    }

    if(g_vc_server[server_fd - 1].vc_node[listen_fd - 1].fd == 0){
        return NULL;
    }

    return &g_vc_server[server_fd - 1].vc_node[listen_fd - 1];
}

static int vc_listen_fd_del(int fd)
{
    int server_fd = 0;
    int listen_fd = 0;

    if(!vc_listen_fd_is_valid(fd)){
        return -QM_EINVAL;
    }

    server_fd = fd >> 8;
    listen_fd = (uint8_t)fd;

    g_vc_server[server_fd - 1].vc_node[listen_fd - 1].fd = 0;
    return QM_EOK;
}

static int vc_server_fd_find(char *host, int port)
{
    int i = 0;
    for(i = 0; i < CONFIG_VC_SERVER_NUM; i++){
        if(g_vc_server[i].fd == 0){
            continue;
        }
        if(g_vc_server[i].port == port){
            if(host){
                if(strlen(g_vc_server[i].host) && strcmp(g_vc_server[i].host, host) == 0){
                    return (i + 1) << 8;
                }
            }else{
                if(strlen(g_vc_server[i].host) == 0){
                    return (i + 1) << 8;
                }
            }
        }
    }
    return 0;
}

static int vc_client_connect_set(int server_fd, int client_fd)
{
    int i = 0;
    vc_server_t *vc_server = NULL;

    vc_server = vc_server_get(server_fd);
    if(vc_server == NULL){
        return -QM_EINVAL;
    }

    qm_mutex_lock(&vc_server->lock, QM_WAIT_FOREVER);
    for(i = 0; i < CONFIG_VC_LISTEN_NUM; i++){
        if(vc_server->vc_node[i].fd == 0 && 
           vc_server->vc_node[i].client_fd == 0){
            
            vc_server->vc_node[i].server_fd = server_fd;
            vc_server->vc_node[i].client_fd = client_fd;
            qm_mutex_unlock(&vc_server->lock);
            return QM_EOK;
        }
    }
    qm_mutex_unlock(&vc_server->lock);

    return -QM_EIO;
}

static int vc_server_fd_accept(int fd)
{
    int i = 0;
    vc_server_t *vc_server = NULL;

    if(!vc_server_fd_is_valid(fd)){
        return 0;
    }
    vc_server = vc_server_get(fd);
    if(vc_server == NULL){
        return 0;
    }
    qm_mutex_lock(&vc_server->lock, QM_WAIT_FOREVER);
    for(i = 0; i < CONFIG_VC_LISTEN_NUM; i++){
        if(vc_server->vc_node[i].fd == 0){
            vc_server->vc_node[i].server_fd = fd;
            vc_server->vc_node[i].fd = fd + (i + 1);
            qm_mutex_unlock(&vc_server->lock);
            return vc_server->vc_node[i].fd;
        }
    }
    qm_mutex_unlock(&vc_server->lock);
    return 0;
}

static vc_node_t *vc_client_get(int fd)
{
    if( fd > CONFIG_VC_CLIENT_NUM || fd == 0){
        return NULL;
    }
    if(g_vc_node[fd - 1].fd == 0){
        return NULL;
    }
    return &g_vc_node[fd - 1];
}

static int vc_client_fd_new(void)
{
    int i = 0;
    for(i = 0; i < CONFIG_VC_CLIENT_NUM; i++){
        if(g_vc_node[i].fd == 0){
            g_vc_node[i].fd = i + 1;
            return g_vc_node[i].fd;
        }
    }
    return 0;
}

static int vc_client_fd_del(int fd)
{
    if( fd > CONFIG_VC_CLIENT_NUM || fd == 0){
        return -QM_EINVAL;
    }

    g_vc_node[fd - 1].fd = 0;

    return QM_EOK;
}

int virtual_channel_server_creat(char *host, int port, uint32_t rd_size, uint32_t wr_size)
{   
    int ret = QM_EOK;
    int fd = 0;
    vc_server_t *vc_server = NULL;
    if(rd_size == 0 || wr_size == 0){
        return -QM_EINVAL;
    }
    if(host && strlen(host) > VC_HOST_MAX_LEN){
        return -QM_EINVAL;
    }

    fd = vc_server_fd_new();
    if(fd == 0){
        return -QM_EFULL;
    }
    
    vc_server = vc_server_get(fd);
    
    if(host){
        memcpy(vc_server->host, host, strlen(host));
    }

    vc_server->port = port;
    vc_server->rd_size = rd_size;
    vc_server->wr_size = wr_size;

    ret = qm_sem_new(&vc_server->accept, 0);
    if(ret != QM_EOK){
        goto __exit;
    }

    ret = qm_mutex_new(&vc_server->lock);
    if(ret != QM_EOK){
        goto __exit;
    }

    return fd;

__exit:
    if(qm_sem_is_valid(&vc_server->accept)){
        qm_sem_free(&vc_server->accept);
    }
    if(qm_mutex_is_valid(&vc_server->lock)){
        qm_mutex_free(&vc_server->lock);
    }
    vc_server_fd_del(fd);
    return ret;
}


static int virtual_channel_listen_creat(int fd, uint32_t rd_size, uint32_t wr_size)
{
    int listen_fd = 0;
    int ret = QM_EOK;
    vc_node_t *vc_node = NULL;
    if(rd_size == 0 || wr_size == 0){
        return -QM_EINVAL;
    }

    listen_fd = vc_server_fd_accept(fd);
    if(listen_fd == 0){
        return 0;
    }

    vc_node = vc_listen_get(listen_fd);
    if(vc_node == NULL){
        return 0;
    }

    vc_node->buf = (uint8_t*)qm_malloc(rd_size);
    if(vc_node->buf == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    qm_ringbuf_init(&vc_node->ringbuf, vc_node->buf, rd_size);

    ret = qm_sem_new(&vc_node->signal, 0);
    if(ret != QM_EOK){
        goto __exit;
    }

    ret = qm_mutex_new(&vc_node->lock);
    if(ret != QM_EOK){
        goto __exit;
    }

    vc_node->role = VC_ROLE_LISTEN;
    return listen_fd;

__exit:
    if(qm_sem_is_valid(&vc_node->signal)){
        qm_sem_free(&vc_node->signal);
    }
    if(qm_mutex_is_valid(&vc_node->lock)){
        qm_mutex_free(&vc_node->lock);
    }

    if(vc_node->buf){
        qm_free(vc_node->buf);
        vc_node->buf = NULL;
    }

    vc_listen_fd_del(listen_fd);
    return 0;
}

int virtual_channel_client_creat(uint32_t rd_size, uint32_t wr_size)
{
    int fd = 0;
    int ret = QM_EOK;
    vc_node_t *vc_node = NULL;
    if(rd_size == 0 || wr_size == 0){
        return -QM_EINVAL;
    }
    fd = vc_client_fd_new();
    if(fd == 0){
        return -QM_EFULL;
    }

    vc_node = vc_client_get(fd);

    vc_node->buf = (uint8_t*)qm_malloc(rd_size);
    if(vc_node->buf == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    qm_ringbuf_init(&vc_node->ringbuf, vc_node->buf, rd_size);

    ret = qm_sem_new(&vc_node->signal, 0);
    if(ret != QM_EOK){
        goto __exit;
    }

    ret = qm_mutex_new(&vc_node->lock);
    if(ret != QM_EOK){
        goto __exit;
    }

    vc_node->role = VC_ROLE_CLIENT;

    return fd;

__exit:
    if(qm_sem_is_valid(&vc_node->signal)){
        qm_sem_free(&vc_node->signal);
    }
    if(qm_mutex_is_valid(&vc_node->lock)){
        qm_mutex_free(&vc_node->lock);
    }

    if(vc_node->buf){
        qm_free(vc_node->buf);
        vc_node->buf = NULL;
    }

    vc_client_fd_del(fd);
    return ret;
}

int virtual_channel_connect(int fd, char *host, int port, uint32_t timeout)
{
    int ret = 0;
    int server_fd = 0;
    vc_node_t *vc_node = NULL;
    vc_server_t *vc_server = NULL;

    if( fd > CONFIG_VC_CLIENT_NUM || fd == 0){
        return -QM_EINVAL;
    }

    server_fd = vc_server_fd_find(host, port);
    if(server_fd == 0){
        return -QM_EIO;
    }

    ret = vc_client_connect_set(server_fd, fd);
    if(ret != QM_EOK){
        return ret;
    }

    vc_server = vc_server_get(server_fd);
    if(vc_server == NULL){
        return -QM_EIO;
    }

    vc_node = vc_client_get(fd);
    if(vc_node == NULL){
        return -QM_EIO;
    }

    qm_sem_signal(&vc_server->accept);
    ret = qm_sem_wait(&vc_node->signal, timeout);
    if(ret != QM_EOK){
        return ret;
    }
    return QM_EOK;
}

int virtual_channel_accept(int fd)
{
    int ret = QM_EOK;
    int accept_fd = 0;
    vc_node_t *vc_node = NULL;
    vc_server_t *vc_server = NULL;

    vc_server = vc_server_get(fd);
    if(vc_server == NULL){
        return -QM_EINVAL;
    }

    ret = qm_sem_wait(&vc_server->accept, QM_WAIT_FOREVER);
    if(ret != QM_EOK){
        return 0;
    }

    accept_fd = virtual_channel_listen_creat(fd, vc_server->rd_size, vc_server->wr_size);
    if(accept_fd == 0){
        return 0;
    }

    vc_node = vc_listen_get(accept_fd);
    if(vc_node == NULL){
        return 0;
    }

    vc_node = vc_client_get(vc_node->client_fd);
    if(vc_node == NULL){
        return 0;
    }
    vc_node->client_fd = accept_fd;
    qm_sem_signal(&vc_node->signal);

    return accept_fd;
}

static int vc_role_get(int fd)
{
    if((fd >> 8) > 0){
        if((uint8_t)fd > 0){
            return VC_ROLE_LISTEN;
        }else{
            return VC_ROLE_SERVER;
        }
    }else {
        if((uint8_t)fd > 0){
            return VC_ROLE_CLIENT;
        }
    }
    return VC_ROLE_NONE;
}

static vc_node_t *vc_node_write_get(int fd)
{
    vc_node_t *vc_client_node = NULL;
    vc_node_t *vc_listen_node = NULL;
    int role = vc_role_get(fd);
    if(VC_ROLE_CLIENT == role){
        vc_client_node = vc_client_get(fd);
        if(vc_client_node == NULL){
            return NULL;
        }
        vc_listen_node = vc_listen_get(vc_client_node->client_fd);
        return vc_listen_node;
    }else if(VC_ROLE_LISTEN == role){
        vc_listen_node = vc_listen_get(fd);
        if(vc_listen_node == NULL){
            return NULL;
        }
        vc_client_node = vc_client_get(vc_listen_node->client_fd);
        return vc_client_node;
    }else{
        return NULL;
    }
}

static vc_node_t *vc_node_read_get(int fd)
{
    vc_node_t *vc_node = NULL;
    int role = vc_role_get(fd);

    if(VC_ROLE_CLIENT == role){
        vc_node = vc_client_get(fd);
        return vc_node;
    }else if(VC_ROLE_LISTEN == role){
        vc_node = vc_listen_get(fd);
        return vc_node;
    }else{
        return NULL;
    }
}

int virtual_channel_write(int fd, uint8_t *data, uint32_t len)
{
    int write_len = 0;
    vc_node_t *vc_node = NULL;;
    if(fd == 0 || data == NULL || len == 0){
        return -QM_EINVAL;
    }

    vc_node = vc_node_write_get(fd);
    if(vc_node == NULL){
        return -QM_EIO;
    }

    qm_mutex_lock(&vc_node->lock, QM_WAIT_FOREVER);
    write_len = qm_ringbuf_push(&vc_node->ringbuf, data, len);
    qm_mutex_unlock(&vc_node->lock);

    qm_sem_signal(&vc_node->signal);

    return write_len;
}

int virtual_channel_read(int fd, uint8_t *data, uint32_t len, uint32_t timeout)
{
    int err_code = 0;
    int is_empty = 0;
    int ret = QM_EOK;
    int read_len = 0;
    vc_node_t *vc_node = NULL;

    if(fd == 0 || data == NULL || len == 0){
        return -QM_EINVAL;
    }

    vc_node = vc_node_read_get(fd);
    if(vc_node == NULL){
        return -QM_EIO;
    }

    qm_mutex_lock(&vc_node->lock, QM_WAIT_FOREVER);
    err_code = vc_node->err_code;
    is_empty = qm_ringbuf_isempty(&vc_node->ringbuf);
    qm_mutex_unlock(&vc_node->lock);

    if(err_code != QM_EOK){
        return -QM_EIO;
    }
    
    if(!is_empty){
        read_len = qm_ringbuf_pop(&vc_node->ringbuf, data, len);
        if(read_len == len){
            return read_len;
        }
    }
    ret = qm_sem_wait(&vc_node->signal, timeout);
    if(ret != QM_EOK){
        return read_len;
    }else{
        qm_mutex_lock(&vc_node->lock, QM_WAIT_FOREVER);
        read_len += qm_ringbuf_pop(&vc_node->ringbuf, data + read_len, len - read_len);
        qm_mutex_unlock(&vc_node->lock);
    }
    return read_len;
}

static int vc_client_destroy(int fd)
{
    int err_code = 0;
    vc_node_t *vc_node = NULL;
    vc_node_t *vc_listen_node = NULL;
    if( fd > CONFIG_VC_CLIENT_NUM || fd == 0){
        return -QM_EINVAL;
    }

    vc_node = vc_client_get(fd);
    if(vc_node == NULL){
        return -QM_EINVAL;
    }

    qm_mutex_lock(&vc_node->lock, QM_WAIT_FOREVER);
    vc_client_fd_del(fd);
    err_code = vc_node->err_code;
    qm_mutex_unlock(&vc_node->lock);

    if(err_code == QM_EOK){
        vc_listen_node = vc_listen_get(vc_node->client_fd);
        if(vc_listen_node){
            qm_mutex_lock(&vc_listen_node->lock, QM_WAIT_FOREVER);
            vc_listen_node->err_code = -QM_EIO;
            qm_mutex_unlock(&vc_listen_node->lock);
        }
    }

    if(qm_sem_is_valid(&vc_node->signal)){
        qm_sem_free(&vc_node->signal);
    }
    if(qm_mutex_is_valid(&vc_node->lock)){
        qm_mutex_free(&vc_node->lock);
    }

    if(vc_node->buf){
        qm_free(vc_node->buf);
        vc_node->buf = NULL;
    }

    memset(vc_node, 0, sizeof(vc_node_t));

    return QM_EOK;
}

static int vc_listen_destroy(int fd)
{
    int err_code = 0;
    vc_node_t *vc_node = NULL;
    vc_node_t *vc_client_node = NULL;

    vc_node = vc_listen_get(fd);
    if(vc_node == NULL){
        return -QM_EINVAL;
    }

    qm_mutex_lock(&vc_node->lock, QM_WAIT_FOREVER);
    vc_listen_fd_del(fd);
    err_code = vc_node->err_code;
    qm_mutex_unlock(&vc_node->lock);

    if(err_code == QM_EOK){
        vc_client_node = vc_client_get(vc_node->client_fd);
        if(vc_client_node){
            qm_mutex_lock(&vc_client_node->lock, QM_WAIT_FOREVER);
            vc_client_node->err_code = -QM_EIO;
            qm_mutex_unlock(&vc_client_node->lock);
        }
    }

    if(qm_sem_is_valid(&vc_node->signal)){
        qm_sem_free(&vc_node->signal);
    }
    if(qm_mutex_is_valid(&vc_node->lock)){
        qm_mutex_free(&vc_node->lock);
    }

    if(vc_node->buf){
        qm_free(vc_node->buf);
        vc_node->buf = NULL;
    }

    memset(vc_node, 0, sizeof(vc_node_t));

    return QM_EOK;
}

static int vc_server_destroy(int fd)
{
    int i = 0;
    vc_server_t *vc_server = NULL;

    vc_server = vc_server_get(fd);
    if(vc_server == NULL){
        return -QM_EINVAL;
    }
  
    qm_mutex_lock(&vc_server->lock, QM_WAIT_FOREVER);
    vc_server_fd_del(fd);
    qm_mutex_unlock(&vc_server->lock);

    for(i = 0; i < CONFIG_VC_LISTEN_NUM; i++){
        if(vc_server->vc_node[i].fd > 0){
            vc_listen_destroy(vc_server->vc_node[i].fd);
        }
    }
    memset(vc_server, 0, sizeof(vc_server_t));
    return QM_EOK;
}

int virtual_channel_destroy(int fd)
{
    int role = 0; 
    role = vc_role_get(fd);
    if(role == VC_ROLE_NONE){
        return -QM_EINVAL;
    }else if(role == VC_ROLE_CLIENT){
        return vc_client_destroy(fd);
    }else if(role == VC_ROLE_LISTEN){
        return vc_listen_destroy(fd);
    }else if(role == VC_ROLE_SERVER){
        return vc_server_destroy(fd);
    }
    return QM_EOK;
} 