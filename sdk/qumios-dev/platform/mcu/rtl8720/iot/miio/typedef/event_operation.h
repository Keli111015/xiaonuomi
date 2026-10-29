/**
 * Copyright (C) 2013-2015
 *
 * @file   event_operation.h
 *
 * @remark
 *
 */

#ifndef __EVENT_OPERATION_H__
#define __EVENT_OPERATION_H__

#include "../typedef/arguments.h"

#define DID_MAX_LENGTH  128

typedef struct _event_operation
{
    char                did[DID_MAX_LENGTH + 1];
    uint32_t            siid;
    uint32_t            eiid;
    arguments_t      *  arguments;
} event_operation_t;

event_operation_t * event_operation_new(void);
void event_operation_delete(event_operation_t *thiz);


#endif /* __EVENT_OPERATION_H__ */
