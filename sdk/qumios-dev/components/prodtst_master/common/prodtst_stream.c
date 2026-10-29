#include "prodtst_stream.h"
#include "qm_ringbuf.h"
#include "qm_types.h"
#include "qm_errno.h"
#include "qm_kernel.h"

typedef struct {
    uint8_t *buf;
    uint32_t size;
    qm_mutex_t lock;
    qm_sem_t sem;
    qm_ringbuf_t ringbuf;
}prodtst_stream_t;

void *prodtst_stream_creat(uint32_t size)
{
    qm_err_t ret = QM_EOK;

    prodtst_stream_t *stream = NULL;
    if(size == 0){
        return NULL;
    }

    stream = (uint8_t*)qm_malloc(size);
    if(stream == NULL){
        return NULL;
    }

    memset(stream, 0 ,sizeof(prodtst_stream_t));
    stream->buf = (uint8_t*)qm_malloc(size);
    if(stream->buf == NULL){
        goto __exit;
    }

    stream->size = size;
    qm_ringbuf_init(&stream->ringbuf, stream->buf, stream->size);

    ret = qm_sem_new(&stream->sem, 0);
    if(ret != QM_EOK){
        goto __exit; 
    }

    ret = qm_mutex_new(&stream->lock);
    if(ret != QM_EOK){
        goto __exit; 
    }

    return (void*)stream;

__exit:

    if(stream->buf){
        qm_free(stream->buf);
        stream->buf = NULL;
    }

    if(stream->sem.hdl){
        qm_sem_free(&stream->sem);
    }

    if(stream->lock.hdl){
        qm_mutex_free(&stream->lock);
    }

    if(stream){
        qm_free(stream);
        stream = NULL;
    }

    return NULL;
}

int prodtst_stream_destroy(void *handle)
{
    prodtst_stream_t *stream = NULL;
    stream = (prodtst_stream_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }

    if(stream->buf){
        qm_free(stream->buf);
        stream->buf = NULL;
    }

    if(stream->sem.hdl){
        qm_sem_free(&stream->sem);
    }

    if(stream->lock.hdl){
        qm_mutex_free(&stream->lock);
    }

    if(stream){
        qm_free(stream);
        stream = NULL;
    }

    return QM_EOK;
}

static uint32_t _platform_time_left(uint32_t t_end, uint32_t t_now)
{
    uint32_t res = 0;
    if(t_now - t_end < (UINT32_MAX/2)){
      return 0;
    }
    else{
      res = t_end - t_now;
      return res;
    }
    
}

int prodtst_stream_read(void *handle, uint8_t *buf, int size, uint32_t timeout)
{
    qm_err_t ret = QM_EOK;
    int len_recv = 0, len = 0;
    prodtst_stream_t *stream = NULL;

    uint32_t t_end = 0, t_left = 0;
    t_end = qm_now_ms() + timeout;

    t_left = _platform_time_left(t_end, qm_now_ms());

    stream = (prodtst_stream_t*)handle;
    if(handle == NULL || buf == NULL || size == 0){
        return -QM_EINVAL;
    }

    if(timeout == QM_WAIT_FOREVER){
        do{
            ret = qm_sem_wait(&stream->sem, QM_WAIT_FOREVER);
            if(ret == QM_EOK){
                qm_mutex_lock(&stream->lock, QM_WAIT_FOREVER);
                len = qm_ringbuf_pop(&stream->ringbuf, buf, size);
                qm_mutex_unlock(&stream->lock);
                if(len > 0){
                    len_recv += len;
                }else{
                    continue;
                }

                if(!qm_ringbuf_isempty((void*)stream)){
                    qm_sem_signal(&stream->sem);
                }
            }

        }while (len_recv < size);
        
    }else{

        do{
            ret = qm_sem_wait(&stream->sem, t_left);
            if(ret == QM_EOK){
                qm_mutex_lock(&stream->lock, t_left);
                len = qm_ringbuf_pop(&stream->ringbuf, buf, size);
                qm_mutex_unlock(&stream->lock);
                if(len > 0){
                    len_recv += len;
                }else{
                    continue;
                }

                if(!qm_ringbuf_isempty((void*)stream)){
                    qm_sem_signal(&stream->sem);
                }
            }

        } while ((len_recv < size) && ((t_left = _platform_time_left(t_end, qm_now_ms())) > 0));
    }

    return (0 != len_recv) ? len_recv : ret;
}

int prodtst_stream_flush(void *handle)
{
    prodtst_stream_t *stream = NULL;
    stream = (prodtst_stream_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }

    return qm_ringbuf_flush(&stream->ringbuf);
}

int prodtst_stream_write(void *handle, uint8_t *buf, int size)
{
    int len = 0;
    prodtst_stream_t *stream = NULL;
    stream = (prodtst_stream_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }

    qm_mutex_lock(&stream->lock, QM_WAIT_FOREVER);
    len = qm_ringbuf_push(&stream->ringbuf, buf, size);
    qm_mutex_unlock(&stream->lock);

    qm_sem_signal(&stream->sem);

    return len;
}
