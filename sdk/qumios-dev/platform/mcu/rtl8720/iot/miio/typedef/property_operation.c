/**
 * Copyright (C) 2013-2015
 *
 *
 * @file   property_operation.c
 *
 * @remark
 *
 */

#include "property_operation.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "util.h"

property_operation_t * property_operation_new(void)
{
    property_operation_t * thiz = NULL;

    do
    {
        thiz = (property_operation_t *) malloc(sizeof(property_operation_t));
        if (thiz == NULL)
        {
            break;
        }

        memset(thiz, 0, sizeof(property_operation_t));

        thiz->code = 0;
        thiz->siid = 0;
        thiz->piid = 0;
        thiz->value = NULL;
    } while (false);

    return thiz;
}

property_operation_t * property_operation_new_value(uint64_t did, uint32_t siid, uint32_t piid, property_value_t *newValue)
{
    property_operation_t * thiz = NULL;

    do
    {
        thiz = property_operation_new();
        if (thiz == NULL)
        {
            break;
        }

        arch_u64toa(did, thiz->did);
        thiz->siid = siid;
        thiz->piid = piid;
        thiz->value = newValue;
    } while (false);

    return thiz;
}

void property_operation_delete(property_operation_t *thiz)
{
    if (thiz->value != NULL)
    {
        property_value_delete(thiz->value);
    }

    free(thiz);
    thiz = NULL;
}
