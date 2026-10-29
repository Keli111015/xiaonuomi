#include "qm_ringbuf.h"

#include <string.h>

int qm_ringbuf_init(qm_ringbuf_t* ringbuf, uint8_t* buff, uint32_t size)
{

    if(ringbuf == NULL || buff == NULL || size == 0){
        return 0;
    }   

    ringbuf->buffer     = buff;
    ringbuf->size       = size;
    ringbuf->readpoint  = 0;
    ringbuf->writepoint = 0;
    memset(ringbuf->buffer, 0, ringbuf->size);
    ringbuf->full = 0;

    return QM_EOK;
}

int qm_ringbuf_flush(qm_ringbuf_t* ringbuf)
{
    if(ringbuf == NULL || ringbuf->buffer == NULL){
        return 0;
    }   

    ringbuf->readpoint  = 0;
    ringbuf->writepoint = 0;
    memset(ringbuf->buffer, 0, ringbuf->size);
    ringbuf->full = 0;

    return QM_EOK;
}

int qm_ringbuf_push(qm_ringbuf_t* ringbuf, uint8_t* data, int len)
{
    int i = 0;

    if(ringbuf == NULL || data == NULL || len == 0){
        return 0;
    }   

    if(ringbuf->buffer == NULL){
        return 0;
    }

    if (len < 0 || (uint32_t)len > ringbuf->size) {
        return -QM_EINVAL;
    }

    for (i = 0; i < len; i++) {
        if (((ringbuf->writepoint + 1) % ringbuf->size) == ringbuf->readpoint) {
            ringbuf->full = 1;
            return i;
        } else {
            if (ringbuf->writepoint < (ringbuf->size - 1)) {
                ringbuf->writepoint++;
            } else {
                ringbuf->writepoint = 0;
            }
            ringbuf->buffer[ringbuf->writepoint] = data[i];
        }
    }

    return i;
}

int qm_ringbuf_pop(qm_ringbuf_t* ringbuf, uint8_t* data, int len)
{
    int i = 0;

    for (i = 0; i < len; i++) {
        if (ringbuf->writepoint == ringbuf->readpoint) {
            break;
        } else {
            if (ringbuf->readpoint == (ringbuf->size - 1)) {
                ringbuf->readpoint = 0;
            } else {
                ringbuf->readpoint++;
            }
            data[i] = ringbuf->buffer[ringbuf->readpoint];
            ringbuf->full = 0;
        }
    }
    return i;
}

int qm_ringbuf_isempty(qm_ringbuf_t* ringbuf)
{
    int isempty = 0;

    isempty = ringbuf->writepoint == ringbuf->readpoint;

    return isempty;
}

int qm_ringbuf_isfull(qm_ringbuf_t* ringbuf)
{
    int full = 0;

    full = ringbuf->full;

    return full;
}
