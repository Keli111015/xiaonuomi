/**
 * Copyright (C) 2013-2015
 *
 * @file   action_operation_decoder.h
 *
 * @remark
 *
 */

#ifndef __ACTION_OPERATION_DECODER_H__
#define __ACTION_OPERATION_DECODER_H__

#include "../typedef/action_operation.h"
#include <jsmi.h>

action_operation_t * action_operation_decode(jsmi_parser_t *parser, jsmntok_t *params);


#endif /* __ACTION_OPERATION_DECODER_H__ */
