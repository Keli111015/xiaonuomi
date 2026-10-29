#ifndef __PRODTST_STREAM_H__
#define __PRODTST_STREAM_H__

#include "qm_types.h"

void *prodtst_stream_creat(uint32_t size);

int prodtst_stream_destroy(void *handle);

int prodtst_stream_read(void *handle, uint8_t *buf, int size, uint32_t timeout);

int prodtst_stream_flush(void *handle);

int prodtst_stream_write(void *handle, uint8_t *buf, int size);

#endif
