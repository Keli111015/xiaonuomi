/**
 * Copyright (C) 2013-2015
 *
 * @file   property_changed_encoder.h
 *
 * @remark
 *
 */

#ifndef __PROPERTY_CHANGED_ENCODER_H__
#define __PROPERTY_CHANGED_ENCODER_H__

#include "../typedef/property_operation.h"
#include <stddef.h>

int property_changed_encode(property_operation_t *o, char out[], size_t size, uint8_t num);


#endif /* __PROPERTY_CHANGED_ENCODER_H__ */
