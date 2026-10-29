/**
 * Copyright (C) 2013-2015
 *
 * @file   event_operation_encoder.h
 *
 * @remark
 *
 */

#ifndef __EVENT_OPERATION_ENCODER_H__
#define __EVENT_OPERATION_ENCODER_H__

#include "../typedef/event_operation.h"
#include <stddef.h>

int event_operation_encode(event_operation_t *o, char out[], size_t size);


#endif /* __EVENT_OPERATION_ENCODER_H__ */
