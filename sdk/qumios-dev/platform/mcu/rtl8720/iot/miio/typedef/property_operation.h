/**
 * Copyright (C) 2013-2015
 *
 * @file   property_operation.h
 *
 * @remark
 *
 */

#ifndef __PROPERTY_OPERATION_H__
#define __PROPERTY_OPERATION_H__

#include "../typedef/property_value.h"

#define DID_MAX_LENGTH  128

typedef enum _property_operation_type
{
    PROPERTY_OPERATION_GET = 0,
    PROPERTY_OPERATION_SET = 1,
} property_operation_type;

typedef struct _property_operation
{
    char                did[DID_MAX_LENGTH + 1];
    uint32_t            siid;
    uint32_t            piid;
    int                 code;
    property_value_t  * value;
} property_operation_t;

property_operation_t * property_operation_new(void);
property_operation_t * property_operation_new_value(uint64_t did, uint32_t siid, uint32_t piid, property_value_t *newValue);
void property_operation_delete(property_operation_t *thiz);



#endif /* __PROPERTY_OPERATION_H__ */
