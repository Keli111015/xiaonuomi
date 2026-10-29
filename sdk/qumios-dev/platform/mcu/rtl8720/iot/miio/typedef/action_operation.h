/**
 * Copyright (C) 2013-2015
 *
 * @file   action_operation.h
 *
 * @remark
 *
 */

#ifndef __ACTION_OPERATION_H__
#define __ACTION_OPERATION_H__

#include "../typedef/arguments.h"

#define DID_MAX_LENGTH  128

typedef struct _action_operation
{
    char                did[DID_MAX_LENGTH + 1];
    uint32_t            siid;
    uint32_t            aiid;
    int                 code;
    arguments_t      *  in;
    arguments_t      *  out;
} action_operation_t;

action_operation_t * action_operation_new(void);
void action_operation_delete(action_operation_t *thiz);


#endif /* __ACTION_OPERATION_H__ */
