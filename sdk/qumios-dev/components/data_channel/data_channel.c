#include "data_channel.h"
#include "qm.h"
#include "qm_ringbuf.h"

#define LOG_TAG "data_channel"

typedef enum{
    DATA_CHANNEL_TYPE_NONE,
    DATA_CHANNEL_TYPE_SERVER,
    DATA_CHANNEL_TYPE_CLIENT
}data_channel_type_t;

typedef struct {
    data_channel_type_t type;
    uint32_t rd_size;
    uint8_t *rd_buf;
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_t rd_mutex;
    qm_sem_t rd_sem;
    qm_sem_t rd_wa_sem;
#endif
    qm_ringbuf_t rd_ringbuf;

    uint32_t wr_size;
    uint8_t *wr_buf;
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_t wr_mutex;
    qm_sem_t wr_sem;
    qm_sem_t wr_wa_sem;
#endif
    qm_ringbuf_t wr_ringbuf;
}data_channel_server_t;

typedef struct {
    data_channel_type_t type;
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_t *rd_mutex;
    qm_sem_t *rd_sem;
#endif
    qm_ringbuf_t *rd_ringbuf;
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_t *wr_mutex;
    qm_sem_t *wr_sem;
#endif
    qm_ringbuf_t *wr_ringbuf;
}data_channel_client_t;


static int data_channel_server_destroy(void *handle);
static int data_channel_client_destroy(void *handle);


void *data_channel_server_creat(uint32_t rd_size, uint32_t wr_size)
{
    qm_err_t ret = QM_EOK;
    data_channel_server_t *dc_server = NULL;
    dc_server = (data_channel_server_t*)qm_malloc(sizeof(data_channel_server_t));
    QM_RETURN_ON_FALSE(dc_server, NULL, LOG_TAG, "dc_server Create NULL");
    memset(dc_server, 0, sizeof(data_channel_server_t));

    dc_server->type = DATA_CHANNEL_TYPE_SERVER;

    dc_server->rd_size = rd_size;
    dc_server->rd_buf = (uint8_t*)qm_malloc(dc_server->rd_size);
    QM_RETURN_ON_FALSE(dc_server->rd_buf, NULL, LOG_TAG, "dc_server rd_buf Create NULL");
    qm_ringbuf_init(&dc_server->rd_ringbuf, dc_server->rd_buf, dc_server->rd_size);
#if CONFIG_QM_OS_SUPPORT
    ret = qm_sem_new(&dc_server->rd_sem, 0);
    QM_GOTO_ON_ERROR(ret, __exit, LOG_TAG, "dc_server rd_sem Create NULL");
    ret = qm_mutex_new(&dc_server->rd_mutex);
    QM_GOTO_ON_ERROR(ret, __exit, LOG_TAG, "dc_server rd_mutex Create NULL");
#endif
    dc_server->wr_size = wr_size;
    dc_server->wr_buf = (uint8_t*)qm_malloc(dc_server->wr_size);
    QM_GOTO_ON_FALSE(dc_server->wr_buf, -QM_ENOMEM, __exit, LOG_TAG, "dc_server wr_buf Create NULL");
    qm_ringbuf_init(&dc_server->wr_ringbuf, dc_server->wr_buf, dc_server->wr_size);
#if CONFIG_QM_OS_SUPPORT
    ret = qm_sem_new(&dc_server->wr_sem, 0);
    QM_GOTO_ON_ERROR(ret, __exit, LOG_TAG, "dc_server wr_sem Create NULL");
    ret = qm_mutex_new(&dc_server->wr_mutex);
    QM_GOTO_ON_ERROR(ret, __exit, LOG_TAG, "dc_server wr_mutex Create NULL");
#endif
    return (void*)dc_server;
__exit:
    data_channel_server_destroy((void*)dc_server);
    return NULL;
}

void *data_channel_client_creat(void)
{
    data_channel_client_t *dc_client = NULL;
    dc_client = (data_channel_client_t*)qm_malloc(sizeof(data_channel_client_t));
    QM_RETURN_ON_FALSE(dc_client, NULL, LOG_TAG, "dc_client Create NULL");
    memset(dc_client, 0, sizeof(data_channel_client_t));
    dc_client->type = DATA_CHANNEL_TYPE_CLIENT;
    return (void*)dc_client;

}

int data_channel_connect(void *handle_server, void *handle_client)
{
    data_channel_client_t *dc_client = (data_channel_client_t*)handle_client;
    data_channel_server_t *dc_server = (data_channel_server_t*)handle_server;
    
    QM_RETURN_ON_FALSE(dc_client && dc_server , -QM_EINVAL, LOG_TAG, "data_channel_connect handle NULL");
#if CONFIG_QM_OS_SUPPORT
    dc_client->rd_sem = &dc_server->wr_sem;
    dc_client->rd_mutex = &dc_server->wr_mutex;
#endif
    dc_client->rd_ringbuf = &dc_server->wr_ringbuf;
#if CONFIG_QM_OS_SUPPORT
    dc_client->wr_sem = &dc_server->rd_sem;
    dc_client->wr_mutex = &dc_server->rd_mutex;
#endif
    dc_client->wr_ringbuf = &dc_server->rd_ringbuf;

    return QM_EOK;
}

static data_channel_type_t data_channel_type_get(void *handle)
{
    data_channel_type_t type = DATA_CHANNEL_TYPE_NONE;
    data_channel_client_t *dc_client = (data_channel_client_t*)handle;
    QM_RETURN_ON_FALSE(handle, -QM_EINVAL, LOG_TAG, "data_channel_type_get handle NULL");
    type = dc_client->type;
    if(type == DATA_CHANNEL_TYPE_SERVER){
        return DATA_CHANNEL_TYPE_SERVER;
    }else if(type == DATA_CHANNEL_TYPE_CLIENT){
        return DATA_CHANNEL_TYPE_CLIENT;
    }else{
        return DATA_CHANNEL_TYPE_NONE;
    }
}

static int data_channel_client_write(void *handle, uint8_t *data, uint32_t len, uint32_t timeout)
{
    int write_len = 0;
    data_channel_client_t *dc_client = (data_channel_client_t*)handle;
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_lock(dc_client->wr_mutex, QM_WAIT_FOREVER);
#endif
    write_len = qm_ringbuf_push(dc_client->wr_ringbuf, data, (int)len);
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_unlock(dc_client->wr_mutex);
    qm_sem_signal(dc_client->wr_sem);
#endif
    return write_len;
}

static int data_channel_server_write(void *handle, uint8_t *data, uint32_t len, uint32_t timeout)
{
    int write_len = 0;
    data_channel_server_t *dc_server = (data_channel_server_t*)handle;
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_lock(&dc_server->wr_mutex, QM_WAIT_FOREVER);
#endif
    write_len = qm_ringbuf_push(&dc_server->wr_ringbuf, data, (int)len);
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_unlock(&dc_server->wr_mutex);
    
    qm_sem_signal(&dc_server->wr_sem);
#endif
    return write_len;
}

int data_channel_write(void *handle, uint8_t *data, uint32_t len, uint32_t timeout)
{
    data_channel_type_t type = DATA_CHANNEL_TYPE_NONE;
    QM_RETURN_ON_FALSE(handle && data && len, -QM_EINVAL, LOG_TAG, "data_channel_write handle NULL");
    type = data_channel_type_get(handle);
    if(type == DATA_CHANNEL_TYPE_SERVER){
        return data_channel_server_write(handle, data, len, timeout);
    }else if(type == DATA_CHANNEL_TYPE_CLIENT){
        return data_channel_client_write(handle, data, len, timeout);
    }else{
        return -QM_EINVAL;
    }
}

static int data_channel_client_read(void *handle, uint8_t *data, uint32_t len, uint32_t timeout)
{
    int is_empty = 0;
    qm_err_t ret = QM_EOK;
    int read_len = 0;

    data_channel_client_t *dc_client = (data_channel_client_t*)handle;
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_lock(dc_client->rd_mutex, QM_WAIT_FOREVER);
#endif
    is_empty = qm_ringbuf_isempty(dc_client->rd_ringbuf);
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_unlock(dc_client->rd_mutex);
#endif
    if(!is_empty){
    #if CONFIG_QM_OS_SUPPORT
        qm_mutex_lock(dc_client->rd_mutex, QM_WAIT_FOREVER);
    #endif
        read_len = qm_ringbuf_pop(dc_client->rd_ringbuf, data, len);
    #if CONFIG_QM_OS_SUPPORT
        qm_mutex_unlock(dc_client->rd_mutex);
    #endif
        if(read_len == len){
            return read_len;
        }
    }
 #if CONFIG_QM_OS_SUPPORT
    ret = qm_sem_wait(dc_client->rd_sem, timeout);
    if(ret != QM_EOK){
        return read_len;
    }else{
        qm_mutex_lock(dc_client->rd_mutex, QM_WAIT_FOREVER);
#endif
        read_len += qm_ringbuf_pop(dc_client->rd_ringbuf, data + read_len, len - read_len);
 #if CONFIG_QM_OS_SUPPORT
        qm_mutex_unlock(dc_client->rd_mutex);
    }
#endif
    return read_len;
}

static int data_channel_server_read(void *handle, uint8_t *data, uint32_t len, uint32_t timeout)
{
    int is_empty = 0;
    int read_len = 0;
#if CONFIG_QM_OS_SUPPORT
    qm_err_t ret = QM_EOK;
#endif
    data_channel_server_t *dc_server = (data_channel_server_t*)handle;
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_lock(&dc_server->rd_mutex, QM_WAIT_FOREVER);
#endif
    is_empty = qm_ringbuf_isempty(&dc_server->rd_ringbuf);
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_unlock(&dc_server->rd_mutex);
#endif
    if(!is_empty){
        read_len = qm_ringbuf_pop(&dc_server->rd_ringbuf, data, len);
        if(read_len == len){
            return read_len;
        }
    }
#if CONFIG_QM_OS_SUPPORT
    ret = qm_sem_wait(&dc_server->rd_sem, timeout);
    if(ret != QM_EOK){
        return read_len;
    }else{
        qm_mutex_lock(&dc_server->rd_mutex, QM_WAIT_FOREVER);
#endif
        read_len += qm_ringbuf_pop(&dc_server->rd_ringbuf, data + read_len, len - read_len);
#if CONFIG_QM_OS_SUPPORT
        qm_mutex_unlock(&dc_server->rd_mutex);
    }
#endif
    return read_len;
}

int data_channel_read(void *handle, uint8_t *data, uint32_t len, uint32_t timeout)
{
    data_channel_type_t type = DATA_CHANNEL_TYPE_NONE;
    QM_RETURN_ON_FALSE(handle && data && len, -QM_EINVAL, LOG_TAG, "data_channel_read handle NULL");

    type = data_channel_type_get(handle);
    if(type == DATA_CHANNEL_TYPE_SERVER){
        return data_channel_server_read(handle, data, len, timeout);
    }else if(type == DATA_CHANNEL_TYPE_CLIENT){
        return data_channel_client_read(handle, data, len, timeout);
    }else{
        return -QM_EINVAL;
    }
}

static int data_channel_server_destroy(void *handle)
{
    data_channel_server_t *dc_server = (data_channel_server_t*)handle;
    if(dc_server->rd_buf){
        qm_free(dc_server->rd_buf);
    }
    if(dc_server->wr_buf){
        qm_free(dc_server->wr_buf);
    }
#if CONFIG_QM_OS_SUPPORT
    if(qm_sem_is_valid(&dc_server->rd_sem)){
        qm_sem_free(&dc_server->rd_sem);
    }
    if(qm_sem_is_valid(&dc_server->wr_sem)){
        qm_sem_free(&dc_server->wr_sem);
    }
    if(qm_mutex_is_valid(&dc_server->rd_mutex)){
        qm_mutex_free(&dc_server->rd_mutex);
    }
    if(qm_mutex_is_valid(&dc_server->wr_mutex)){
        qm_mutex_free(&dc_server->wr_mutex);
    }
#endif
    return QM_EOK;
}

static int data_channel_client_destroy(void *handle)
{
    data_channel_client_t *dc_client = (data_channel_client_t*)handle;
    qm_free(dc_client);
    return QM_EOK;
}

int data_channel_destroy(void *handle)
{
    data_channel_type_t type = DATA_CHANNEL_TYPE_NONE;
    QM_RETURN_ON_FALSE(handle, -QM_EINVAL, LOG_TAG, "data_channel_destroy handle NULL");
    type = data_channel_type_get(handle);
    if(type == DATA_CHANNEL_TYPE_SERVER){
        data_channel_server_destroy(handle);
    }else if(type == DATA_CHANNEL_TYPE_CLIENT){
        data_channel_client_destroy(handle);
    }else{
        return -QM_EINVAL;
    }
    return QM_EOK;
}

