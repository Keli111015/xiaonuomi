/**
 * Copyright (C) 2013-2015
 *
 * @file   property_format.h
 *
 * @remark
 *
 */

#ifndef __PROPERTY_FORMAT_H__
#define __PROPERTY_FORMAT_H__


#define MAX_NUMBER_LENGTH           64
#define MAX_BOOLEAN_LENGTH          8

typedef enum _property_format
{
    PROPERTY_FORMAT_UNDEFINED      = 0,
    PROPERTY_FORMAT_BOOLEAN        = 1,
    PROPERTY_FORMAT_STRING         = 2,
    PROPERTY_FORMAT_NUMBER         = 3,
} property_format_t;


#endif /* __PROPERTY_FORMAT_H__ */
