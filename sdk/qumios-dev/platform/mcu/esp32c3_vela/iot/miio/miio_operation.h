/**
 * @par     Copyright (c):
 *
 *    Copyright 2019 MIoT,MI
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */


#ifndef __MIIO_PROPERTY_OPERATION_H__
#define __MIIO_PROPERTY_OPERATION_H__

#include "iot_generic.h"

#define MAX_ARGUMENTS     5
#define DID_MAX_LENGTH  128

#define MAX_NUMBER_LENGTH           64
#define MAX_BOOLEAN_LENGTH          8

#define DATA_STRING_MAX_LENGTH      (64)

typedef enum _property_format
{
    PROPERTY_FORMAT_UNDEFINED      = 0,
    PROPERTY_FORMAT_BOOLEAN        = 1,
    PROPERTY_FORMAT_STRING         = 2,
    PROPERTY_FORMAT_NUMBER         = 3,
} property_format_t;

typedef struct _data_boolean
{
    bool_t value;
} data_boolean_t;

typedef struct _data_string
{
    char          value[DATA_STRING_MAX_LENGTH + 1];
    uint32_t      length;
} data_string_t;

typedef enum _data_number_type
{
    DATA_NUMBER_INTEGER  = 0,
    DATA_NUMBER_FLOAT    = 1,
    DATA_NUMBER_UINTEGER = 2,
} data_number_type_t;

typedef struct _data_number_value
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

typedef struct
{
    property_format_t   format;
    property_data_t     data;
} property_value_t;

typedef struct _miio_property_operation
{
    uint32_t            siid;
    uint32_t            piid;
    int                 code;
    property_value_t  * value;
} property_operation_t;


typedef struct _argument
{
    uint32_t            piid;
    property_value_t  * value;
} argument_t;
#define MAX_ARGUMENTS     5

typedef struct _arguments
{
    int           size;
    argument_t    arguments[MAX_ARGUMENTS];
} arguments_t;


typedef struct _action_operation
{
	uint32_t			siid;
	uint32_t			aiid;
	uint32_t			piid;
	int					code;
	arguments_t			* in;
	arguments_t			* out;
} action_operation_t;

typedef struct _event_operation
{
    uint32_t            siid;
    uint32_t            eiid;
    arguments_t      *  arguments;
} event_operation_t;

event_operation_t * event_operation_new(void);
void event_operation_delete(event_operation_t *thiz);

property_value_t * property_value_new(void);
void property_value_delete(property_value_t *thiz);
property_value_t * property_value_new_string(const char *value);
property_value_t * property_value_new_integer(long value);

property_value_t * property_value_new_uinteger(long value);


property_value_t * property_value_new_float(double value);
property_value_t * property_value_new_boolean(bool_t value);

int property_operation_encode_tail(char *pbuf, size_t buf_sz);
int property_operation_encode_param(char *pbuf, size_t buf_sz, property_operation_t *opt, bool_t has_value);



property_operation_t * miio_property_operation_new(void);
void miio_property_operation_delete(property_operation_t *thiz);
static int miio_action_get_arguments_in(action_operation_t *thiz, char *pbuf, uint32_t piid, int index);
action_operation_t * action_operation_decode(const char *pbuf, size_t buf_sz, uint32_t index);
property_operation_t * miio_property_operation_decode(const char *pbuf, size_t buf_sz, uint32_t index, bool_t has_value);


argument_t * argument_new(void);
arguments_t * arguments_new(void);
void argument_delete(argument_t *thiz);
void arguments_delete(arguments_t *thiz);

#ifdef CONFIG_IOT_GENERIC_ACTION_SUPPORT
action_operation_t * action_operation_new(void);
void action_operation_delete(action_operation_t *thiz);
int action_operation_encode_tail(char *pbuf, size_t buf_sz);
int action_operation_encode_param(char *pbuf, size_t buf_sz, action_operation_t *opt);
#endif

#endif /* __PROPERTY_OPERATION_H__ */
