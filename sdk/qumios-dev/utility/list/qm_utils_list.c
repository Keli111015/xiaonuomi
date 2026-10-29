#include "qm_utils_list.h"
#include "qm_kernel.h"

/*
 * Allocate a new qm_list_t. NULL on failure.
 */
qm_list_t *qm_list_new(void)
{
    qm_list_t *self;
    self = qm_malloc(sizeof(qm_list_t));
    if (!self) {
        return NULL;
    }
    self->head = NULL;
    self->tail = NULL;
    self->free = NULL;
    self->match = NULL;
    self->len = 0;
    return self;
}

/*
 * Free the list.
 */
void qm_list_destroy(qm_list_t *self)
{
    unsigned int len = self->len;
    qm_list_node_t *next;
    qm_list_node_t *curr = self->head;

    while (len--) {
        next = curr->next;
        if (self->free) {
            self->free(curr->val);
        }
        qm_free(curr);
        curr = next;
    }

    qm_free(self);
}

/**
 * insert a node after a node
 */

void qm_list_push_before(qm_list_node_t *node, qm_list_node_t *n)
{
    node->prev->next=n;
    n->prev=node->prev;
    node->prev=n;
    n->next=node;
}

/**
 * insert a node before a node
 */
void qm_list_push_after(qm_list_node_t *node, qm_list_node_t *n)
{
    node->next->prev=n;
    n->next=node->next;

    node->next=n;
    n->prev=node;
}

/*
 * Append the given node to the list
 * and return the node, NULL on failure.
 */
qm_list_node_t *qm_list_rpush(qm_list_t *self, qm_list_node_t *node)
{
    if (!node) {
        return NULL;
    }

    if (self->len) {
        node->prev = self->tail;
        node->next = NULL;
        self->tail->next = node;
        self->tail = node;
    } else {
        self->head = self->tail = node;
        node->prev = node->next = NULL;
    }

    ++self->len;
    return node;
}

/*
 * Return / detach the last node in the list, or NULL.
 */
qm_list_node_t *qm_list_rpop(qm_list_t *self)
{
    qm_list_node_t *node = NULL;
    if (!self->len) {
        return NULL;
    }

    node = self->tail;

    if (--self->len) {
        (self->tail = node->prev)->next = NULL;
    } else {
        self->tail = self->head = NULL;
    }

    node->next = node->prev = NULL;
    return node;
}

/*
 * Return / detach the first node in the list, or NULL.
 */
qm_list_node_t *qm_list_lpop(qm_list_t *self)
{
    qm_list_node_t *node = NULL;
    if (!self->len) {
        return NULL;
    }

    node = self->head;

    if (--self->len) {
        (self->head = node->next)->prev = NULL;
    } else {
        self->head = self->tail = NULL;
    }

    node->next = node->prev = NULL;
    return node;
}

/*
 * Prepend the given node to the list
 * and return the node, NULL on failure.
 */
qm_list_node_t *qm_list_lpush(qm_list_t *self, qm_list_node_t *node)
{
    if (!node) {
        return NULL;
    }

    if (self->len) {
        node->next = self->head;
        node->prev = NULL;
        self->head->prev = node;
        self->head = node;
    } else {
        self->head = self->tail = node;
        node->prev = node->next = NULL;
    }

    ++self->len;
    return node;
}

/*
 * Return the node associated to val or NULL.
 */
qm_list_node_t *qm_list_find(qm_list_t *self, void *val)
{
    qm_list_iterator_t *it;
    qm_list_node_t *node;

    if (NULL == (it = qm_list_iterator_new(self, LIST_HEAD))) {
        return NULL;
    }
    node = qm_list_iterator_next(it);
    while (node) {
        if (self->match) {
            if (self->match(val, node->val)) {
                qm_list_iterator_destroy(it);
                return node;
            }
        } else {
            if (val == node->val) {
                qm_list_iterator_destroy(it);
                return node;
            }
        }
        node = qm_list_iterator_next(it);
    }

    qm_list_iterator_destroy(it);
    return NULL;
}

/*
 * Return the node at the given index or NULL.
 */
qm_list_node_t *qm_list_at(qm_list_t *self, int index)
{
    qm_list_direction_t direction = LIST_HEAD;

    if (index < 0) {
        direction = LIST_TAIL;
        index = ~index;
    }

    if (index < self->len) {
        qm_list_iterator_t *it;
        qm_list_node_t *node;

        if (NULL == (it = qm_list_iterator_new(self, direction))) {
            return NULL;
        }
        node = qm_list_iterator_next(it);

        while (index--) {
            node = qm_list_iterator_next(it);
        }
        qm_list_iterator_destroy(it);
        return node;
    }

    return NULL;
}

/*
 * Remove the given node from the list, freeing it and it's value.
 */
void qm_list_remove(qm_list_t *self, qm_list_node_t *node)
{
    node->prev ? (node->prev->next = node->next) : (self->head = node->next);

    node->next ? (node->next->prev = node->prev) : (self->tail = node->prev);

    if (self->free) {
        self->free(node->val);
    }
	
    --self->len;
}

qm_list_iterator_t *qm_list_iterator_init(qm_list_t *list, qm_list_direction_t direction, qm_list_iterator_t *iterator)
{
    qm_list_node_t *node = direction == LIST_HEAD ? list->head : list->tail;

    iterator->next = node;
    iterator->direction = direction;
    return iterator;
}

/*
 * Allocate a new qm_list_iterator_t. NULL on failure.
 * Accepts a direction, which may be LIST_HEAD or LIST_TAIL.
 */
qm_list_iterator_t *qm_list_iterator_new(qm_list_t *list, qm_list_direction_t direction)
{
    qm_list_node_t *node = direction == LIST_HEAD ? list->head : list->tail;
    return qm_list_iterator_new_from_node(node, direction);
}

/*
 * Allocate a new qm_list_iterator_t with the given start
 * node. NULL on failure.
 */
qm_list_iterator_t *qm_list_iterator_new_from_node(qm_list_node_t *node, qm_list_direction_t direction)
{
    qm_list_iterator_t *self;
    self = qm_malloc(sizeof(qm_list_iterator_t));
    if (!self) {
        return NULL;
    }
    self->next = node;
    self->direction = direction;
    return self;
}

/*
 * Return the next qm_list_node_t or NULL when no more
 * nodes remain in the list.
 */
qm_list_node_t *qm_list_iterator_next(qm_list_iterator_t *self)
{
    qm_list_node_t *curr = self->next;
    if (curr) {
        self->next = self->direction == LIST_HEAD ? curr->next : curr->prev;
    }
    return curr;
}

/*
 * Free the list iterator.
 */
void qm_list_iterator_destroy(qm_list_iterator_t *self)
{
    qm_free(self);
    self = NULL;
}

/*
 * Allocates a new qm_list_node_t. NULL on failure.
 */
qm_list_node_t *qm_list_node_new(void *val)
{
    qm_list_node_t *self;
    self = (qm_list_node_t*)qm_malloc(sizeof(qm_list_node_t));
    if (!self) {
        return NULL;
    }

    self->prev = NULL;
    self->next = NULL;
    self->val = val;
    return self;
}

qm_list_node_t *qm_list_node_extra_new(uint32_t len)
{
    qm_list_node_t *self;
    self = (qm_list_node_t*)qm_malloc(sizeof(qm_list_node_t) + len);
    if (!self) {
        return NULL;
    }

    self->prev = NULL;
    self->next = NULL;
    self->val = (void*)((uint8_t*)self + sizeof(qm_list_node_t));
    memset(self->val, 0, len);
    return self;
}

void qm_list_node_destroy(qm_list_node_t *node)
{
    qm_free(node);
    node = NULL;
}

void qm_list_node_init(qm_list_node_t *node, void *val)
{
    if (!node) {
         return;
     }

    node->prev = NULL;
    node->next = NULL;
    node->val = val;
}

int qm_list_len_get(qm_list_t *self)
{
    if (!self) {
        return 0;
    }
    return self->len;
}

void *qm_list_node_val_get(qm_list_node_t *node)
{
    if (!node) {
        return NULL;
    }
    return node->val;
}

void qm_list_reverse(qm_list_t *self)
{
    qm_list_iterator_t *it = NULL;
    qm_list_node_t *head_node = NULL;
    qm_list_node_t *cur_node = NULL;
    qm_list_node_t *tmp_node = NULL;

    if(self->len  == 1){
        return;
    }

    if (NULL == (it = qm_list_iterator_new(self, LIST_HEAD))) {
        return;
    }

    cur_node = qm_list_iterator_next(it);

    head_node = cur_node;

    cur_node = qm_list_iterator_next(it);

    while (cur_node) {
        tmp_node = cur_node->prev;
        cur_node->prev = cur_node->next;
        cur_node->next = tmp_node;
        cur_node = cur_node->prev;
    }

    head_node->next = NULL;
    tmp_node = self->head;
    self->head = self->tail;
    self->tail = tmp_node;

    qm_list_iterator_destroy(it);
}
