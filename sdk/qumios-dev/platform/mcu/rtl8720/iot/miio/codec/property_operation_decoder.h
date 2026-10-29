/**
 * Copyright (C) 2013-2015
 *
 * @file   property_operation_decoder.h
 *
 * @remark
 *
 */

#ifndef __PROPERTY_OPERATION_DECODER_H__
#define __PROPERTY_OPERATION_DECODER_H__

#include "../typedef/property_operation.h"
#include <jsmi.h>

property_operation_t * property_operation_decode(jsmi_parser_t *parser, jsmntok_t *params, int index, bool hasValue);


#endif /* __PROPERTY_OPERATION_DECODER_H__ */
