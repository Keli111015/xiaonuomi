#ifndef _QM_LIST_H_
#define _QM_LIST_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"


/*
 * qm_list_t iterator direction.
 */
typedef enum {
    LIST_HEAD,
    LIST_TAIL
} qm_list_direction_t;

/*
 * qm_list_t node struct.
 */
typedef struct list_node {
    struct list_node *prev;
    struct list_node *next;
    void *val;
} qm_list_node_t;

/*
 * qm_list_t struct.
 */
typedef struct {
    qm_list_node_t *head;
    qm_list_node_t *tail;
    int len;
    void (*free)(void *val);
    int (*match)(void *a, void *b);
} qm_list_t;

/*
 * qm_list_t iterator struct.
 */
typedef struct {
    qm_list_node_t *next;
    qm_list_direction_t direction;
} qm_list_iterator_t;

/* Node prototypes. */
qm_list_node_t *qm_list_node_new(void *val);
/*save memory fragments*/
qm_list_node_t *qm_list_node_extra_new(uint32_t len);

void qm_list_node_destroy(qm_list_node_t *node);

void qm_list_node_init(qm_list_node_t *node, void *val);

int qm_list_len_get(qm_list_t *self);

/* qm_list_t prototypes. */
qm_list_t *qm_list_new(void);

qm_list_node_t *qm_list_rpush(qm_list_t *self, qm_list_node_t *node);

qm_list_node_t *qm_list_lpush(qm_list_t *self, qm_list_node_t *node);

qm_list_node_t *qm_list_find(qm_list_t *self, void *val);

void *qm_list_node_val_get(qm_list_node_t *node);

void qm_list_push_before(qm_list_node_t *node, qm_list_node_t *n);

void qm_list_push_after(qm_list_node_t *node, qm_list_node_t *n);

qm_list_node_t *qm_list_at(qm_list_t *self, int index);

qm_list_node_t *qm_list_rpop(qm_list_t *self);

qm_list_node_t *qm_list_lpop(qm_list_t *self);

void qm_list_remove(qm_list_t *self, qm_list_node_t *node);

void qm_list_destroy(qm_list_t *self);

qm_list_iterator_t *qm_list_iterator_init(qm_list_t *list, qm_list_direction_t direction, qm_list_iterator_t *iterator);
/* qm_list_t iterator prototypes. */
qm_list_iterator_t *qm_list_iterator_new(qm_list_t *list, qm_list_direction_t direction);

qm_list_iterator_t *qm_list_iterator_new_from_node(qm_list_node_t *node, qm_list_direction_t direction);

qm_list_node_t *qm_list_iterator_next(qm_list_iterator_t *self);

void qm_list_iterator_destroy(qm_list_iterator_t *self);

void qm_list_reverse(qm_list_t *self);


#ifdef __cplusplus
}
#endif


#endif

