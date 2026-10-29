/**
 * Copyright (C) 2013-2015
 *
 * @file   action_operation_encoder.c
 *
 * @remark
 *
 */

#include "action_operation_encoder.h"
#include "arguments_encoder.h"
#include <jsmi.h>

/* 
 * 失败应答
 * ------------------------------------------------
 * {
 *   "id": 123,
 *   "result" : {
 *       "code" : -4004
 *    }
 * }
 */

static int action_operation_encode_result_error(action_operation_t *o, char out[], size_t size)
{
    size_t length = 0;
    jsmi_composer_t composer = JSMI_COMPOSER_INIT(out, size);
    jsmi_compose_start(&composer);

    jsmi_set_object_begin(&composer);

    jsmi_set_key(&composer, "code");
    jsmi_set_value_sint(&composer, o->code);

    jsmi_set_object_end(&composer);

    if (jsmi_compose_finish(&composer, NULL, &length) == MIIO_OK)
    {
        return length;
    }

    return 0;
}

/*
 * 成功应答
 * ------------------------------------------------
 * {
 *   "id" : 123,
 *    "result" : {
 *        "code" : 0,
 *        "out" : [
 *            {
 *                "piid" : 3,
 *                "value" : 10
 *            },
 * 		      {
 *                 "piid" : 9,
 *                 "value": 10.3
 *            }
 *        ]
 *    }
 * }
 */

static int action_operation_encode_result_out(action_operation_t *o, char out[], size_t size)
{
    size_t length = 0;
    jsmi_composer_t composer = JSMI_COMPOSER_INIT(out, size);
    jsmi_compose_start(&composer);

    jsmi_set_object_begin(&composer);

    jsmi_set_key(&composer, "code");
    jsmi_set_value_sint(&composer, o->code);

    add_arguments("out", &composer, o->out);

    jsmi_set_object_end(&composer);

    if (jsmi_compose_finish(&composer, NULL, &length) == MIIO_OK)
    {
        return length;
    }

    return 0;
}

int action_operation_encode_result(action_operation_t *o, char out[], size_t size)
{
    if (o->code != 0)
    {
        return action_operation_encode_result_error(o, out, size);
    }

    return action_operation_encode_result_out(o, out, size);
}
