/**
 * Copyright (C) 2013-2015
 *
 * @file   arguments_encoder.h
 *
 * @remark
 *
 */

#ifndef __ARGUMENTS_ENCODER_H__
#define __ARGUMENTS_ENCODER_H__

#include "../typedef/event_operation.h"
#include <jsmi.h>

void add_arguments(const char *name, jsmi_composer_t *composer, arguments_t *out);

#endif /* __ARGUMENTS_ENCODER_H__ */
