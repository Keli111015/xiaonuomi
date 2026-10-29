/**
 * Copyright (C) 2013-2015
 *
 * @file   operation_code.h
 *
 * @remark
 *
 */

#ifndef __OPERATION_CODE_H__
#define __OPERATION_CODE_H__


typedef enum _operation_code
{
    OPERATION_OK = 0,
    OPERATION_INVALID = -4003,
    OPERATION_ERROR_CANNOT_READ = -4001,
    OPERATION_ERROR_CANNOT_WRITE = -4002,
    OPERATION_ERROR_VALUE = -4005,
} operation_code_t;


#endif /* __OPERATION_CODE_H__ */
