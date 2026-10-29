/**
 * Copyright (C) 2013-2015
 *
 * @file   arguments.h
 *
 * @remark
 *
 */

#ifndef __ARGUMENTS_H__
#define __ARGUMENTS_H__

#include "argument.h"

#define MAX_ARGUMENTS     5

typedef struct _arguments
{
    int           size;
    argument_t    arguments[MAX_ARGUMENTS];
} arguments_t;

arguments_t * arguments_new(void);
void arguments_delete(arguments_t *thiz);


#endif /* __ARGUMENTS_H__ */
