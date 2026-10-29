/**
 * Copyright (C) 2013-2015
 *
 * @file   property_value.h
 *
 * @remark
 *
 */

#ifndef __PROPERTY_VALUE_H__
#define __PROPERTY_VALUE_H__

#include <stdint.h>
#include <stdbool.h>
#include "property_format.h"
#include "property_data.h"


typedef struct _property_value
{
    property_format_t   format;
    property_data_t     data;
} property_value_t;

property_value_t * property_value_new(void);

property_value_t * property_value_new_string(const char *value);

property_value_t * property_value_new_integer(long value);

property_value_t * property_value_new_float(float value);

property_value_t * property_value_new_boolean(bool value);

void property_value_delete(property_value_t *thiz);

property_value_t * property_value_new_string_from(const char *buffer, int start, int end);

property_value_t * property_value_new_from_jsmn_primitive(const char *buffer, int start, int end);

int property_value_construct_string_from(property_value_t *thiz, const char *buffer, int start, int end);

int property_value_construct_from_jsmn_primitive(property_value_t *thiz, const char *buffer, int start, int end);


#endif /* __PROPERTY_VALUE_H__ */
