/**
 * Copyright (C) 2013-2015
 *
 * @file   property_operation_encoder.h
 *
 * @remark
 *
 */

#ifndef __PROPERTY_OPERATION_ENCODER_H__
#define __PROPERTY_OPERATION_ENCODER_H__

#include "../typedef/property_operation.h"
#include <stddef.h>

int property_operation_encode(property_operation_t *o, bool hasValue, char out[], size_t size);


#endif /* __PROPERTY_OPERATION_ENCODER_H__ */
