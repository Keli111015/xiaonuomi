/**
 * Copyright (C) 2013-2015
 *
 * @file   argument.h
 *
 * @remark
 *
 */

#ifndef __ARGUMENT_H__
#define __ARGUMENT_H__

#include "property_value.h"

typedef struct _argument
{
    uint32_t            piid;
    property_value_t  * value;
} argument_t;

argument_t * argument_new(void);
void argument_delete(argument_t *thiz);


#endif /* __ARGUMENT_H__ */
