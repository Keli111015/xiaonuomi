/**
 * Copyright (C) 2013-2015
 *
 * @file   property_value.c
 *
 * @remark
 *
 */

#include "property_value.h"
#include <stdlib.h>
#include <string.h>

property_value_t * property_value_New(void)
{
    property_value_t * thiz = NULL;

    do
    {
        thiz = (property_value_t *) malloc(sizeof(property_value_t));
        if (thiz == NULL)
        {
            break;
        }

        memset(thiz, 0, sizeof(property_value_t));
        thiz->format = PROPERTY_FORMAT_UNDEFINED;
    } while (false);

    return thiz;
}

property_value_t * property_value_new_string(const char *value)
{
    property_value_t * thiz = property_value_New();
    if (thiz != NULL)
    {
        thiz->format = PROPERTY_FORMAT_STRING;
        strncpy(thiz->data.string.value, value, DATA_STRING_MAX_LENGTH);
        thiz->data.string.length = strlen(thiz->data.string.value);
    }

    return thiz;
}

property_value_t * property_value_new_integer(long value)
{
    property_value_t * thiz = property_value_New();
    if (thiz != NULL)
    {
        thiz->format = PROPERTY_FORMAT_NUMBER;
        thiz->data.number.type = DATA_NUMBER_INTEGER;
        thiz->data.number.value.integerValue = value;
    }

    return thiz;
}

property_value_t * property_value_new_float(float value)
{
    property_value_t * thiz = property_value_New();
    if (thiz != NULL)
    {
        thiz->format = PROPERTY_FORMAT_NUMBER;
        thiz->data.number.type = DATA_NUMBER_FLOAT;
        thiz->data.number.value.floatValue = value;
    }

    return thiz;
}

property_value_t * property_value_new_boolean(bool value)
{
    property_value_t * thiz = property_value_New();
    if (thiz != NULL)
    {
        thiz->format = PROPERTY_FORMAT_BOOLEAN;
        thiz->data.boolean.value = value;
    }

    return thiz;
}

void property_value_delete(property_value_t *thiz)
{
    switch (thiz->format)
    {
        case PROPERTY_FORMAT_BOOLEAN:
            break;

        case PROPERTY_FORMAT_STRING:
            break;

        case PROPERTY_FORMAT_NUMBER:
            break;

        default:
            break;
    }

    free(thiz);
    thiz = NULL;
}

property_value_t * property_value_new_string_from(const char *buffer, int start, int end)
{
    property_value_t * thiz = NULL;
    
    do
    {
        thiz = property_value_New();
        if (thiz == NULL)
        {
            break;
        }
        
        if (property_value_construct_string_from(thiz, buffer, start, end) != 0)
        {
            property_value_delete(thiz);
            thiz = NULL;
            break;
        }
    } while (false);

    return thiz;
}

#if 0
property_value_t * property_value_new_number_from(const char *buffer, int start, int end)
{
    property_value_t * thiz = property_value_New();
    if (thiz != NULL)
    {
        char value[MAX_NUMBER_LENGTH + 1];
        int length = end - start;
        const char *dot = NULL;
        char *stop = NULL;

        if (length > MAX_NUMBER_LENGTH)
        {
            length = MAX_NUMBER_LENGTH;
        }

        memset(value, 0, MAX_NUMBER_LENGTH + 1);
        strncpy(value, buffer + start, length);

        dot = strstr(value, ".");
        thiz->format = PROPERTY_FORMAT_NUMBER;

        if (dot == NULL)
        {
            thiz->data.number.type = DATA_NUMBER_INTEGER;
            thiz->data.number.value.integerValue = strtol(value, &stop, 10);
        }
        else
        {
            thiz->data.number.type = DATA_NUMBER_FLOAT;
            thiz->data.number.value.floatValue = strtof(value, &stop);
        }
    }

    return thiz;
}

property_value_t * property_value_new_boolean_from(const char *buffer, int start, int end)
{
    property_value_t * thiz = property_value_New();
    if (thiz != NULL)
    {
        char value[MAX_BOOLEAN_LENGTH + 1];
        int length = end - start;

        if (length > MAX_BOOLEAN_LENGTH)
        {
            length = MAX_BOOLEAN_LENGTH;
        }

        memset(value, 0, MAX_NUMBER_LENGTH + 1);
        strncpy(value, buffer + start, length);

        thiz->format = PROPERTY_FORMAT_STRING;
        thiz->data.boolean.value = (strncmp(value, "true") == 0 || strncmp(value, "TRUE") == 0);
    }

    return thiz;
}
#endif

property_value_t * property_value_new_from_jsmn_primitive(const char *buffer, int start, int end)
{
    property_value_t * thiz = NULL;

    do
    {
        thiz = property_value_New();
        if (thiz == NULL)
        {
            break;
        }

        if (property_value_construct_from_jsmn_primitive(thiz, buffer, start, end) != 0)
        {
            property_value_delete(thiz);
            thiz = NULL;
            break;
        }
    } while (false);

    return thiz; 
}

int property_value_construct_string_from(property_value_t *thiz, const char *buffer, int start, int end)
{
    int length = end - start;
    thiz->format = PROPERTY_FORMAT_STRING;
    strncpy(thiz->data.string.value, buffer + start, length > DATA_STRING_MAX_LENGTH ? DATA_STRING_MAX_LENGTH : length);
    thiz->data.string.length = strlen(thiz->data.string.value);

    return 0;
}

int property_value_construct_from_jsmn_primitive(property_value_t *thiz, const char *buffer, int start, int end)
{
    do
    {
        char value[MAX_NUMBER_LENGTH + 1];
        int length = end - start;
        const char *dot = NULL;

        if (length > MAX_NUMBER_LENGTH)
        {
            length = MAX_NUMBER_LENGTH;
        }

        memset(value, 0, MAX_NUMBER_LENGTH + 1);
        strncpy(value, buffer + start, length);

        if (strncmp(value, "true", MAX_NUMBER_LENGTH) == 0)
        {
            thiz->format = PROPERTY_FORMAT_BOOLEAN;
            thiz->data.boolean.value = true;
            break;
        }

        if (strncmp(value, "false", MAX_NUMBER_LENGTH) == 0)
        {
            thiz->format = PROPERTY_FORMAT_BOOLEAN;
            thiz->data.boolean.value = false;
            break;
        }

        dot = strstr(value, ".");
        thiz->format = PROPERTY_FORMAT_NUMBER;

        if (dot == NULL)
        {
            char *stop = NULL;
            thiz->data.number.type = DATA_NUMBER_INTEGER;
            thiz->data.number.value.integerValue = strtol(value, &stop, 10);
        }
        else
        {
            char *stop = NULL;
            thiz->data.number.type = DATA_NUMBER_FLOAT;
            thiz->data.number.value.floatValue = strtof(value, &stop);
        }
    } while (false);

    return 0; 
}
