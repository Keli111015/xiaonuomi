#ifndef __QM_RINGBUF_H__
#define __QM_RINGBUF_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"
#include "qm_errno.h"
#include "qm_config.h"

#define QM_RINGBUF_OK          0  /* No error, everything OK. */
#define QM_RINGBUF_ERR         -1 /* Out of memory error.     */
#define QM_RINGBUF_EMPTY       -3 /* Timeout.                 */
#define QM_RINGBUF_FULL        -4 /* Routing problem.         */
#define QM_RINGBUF_TOO_SHORT   -5

typedef struct {
    uint32_t size;
    uint32_t readpoint;
    uint32_t writepoint;
    uint8_t *buffer;
    uint8_t  full;
} qm_ringbuf_t;

int qm_ringbuf_init(qm_ringbuf_t* ringbuf, uint8_t* buff, uint32_t size);
int qm_ringbuf_flush(qm_ringbuf_t* ringbuf);
int qm_ringbuf_push(qm_ringbuf_t* ringbuf, uint8_t* data, int len);
int qm_ringbuf_pop(qm_ringbuf_t* ringbuf, uint8_t* data, int len);
int qm_ringbuf_isempty(qm_ringbuf_t* ringbuf);
int qm_ringbuf_isfull(qm_ringbuf_t* ringbuf);

#ifdef __cplusplus
}
#endif

#endif
