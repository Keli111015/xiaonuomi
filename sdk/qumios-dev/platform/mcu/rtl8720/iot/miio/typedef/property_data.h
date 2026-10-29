/**
 * Copyright (C) 2013-2015
 *
 * @file   property_data.h
 *
 * @remark
 *
 */

#ifndef __PROPERTY_DATA_H__
#define __PROPERTY_DATA_H__

#include <stdint.h>
#include <stdbool.h>


typedef struct _data_boolean
{
    bool value;
} data_boolean_t;


#define DATA_STRING_MAX_LENGTH  2048

typedef struct _data_string
{
    char          value[DATA_STRING_MAX_LENGTH + 1];
    uint32_t      length;
} data_string_t;

typedef enum _data_number_type
{
    DATA_NUMBER_INTEGER  = 0,
    DATA_NUMBER_FLOAT    = 1,
} data_number_type_t;

typedef union _data_number_value
{
    long    integerValue;
    double   floatValue;
} data_number_value_t;

typedef struct _data_number
{
    data_number_type_t      type;
    data_number_value_t     value;
} data_number_t;

typedef union _property_data
{
    data_boolean_t     boolean;
    data_string_t      string;
    data_number_t      number;
} property_data_t;


#endif /* __PROPERTY_DATA_H__ */
