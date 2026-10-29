/**
 * Copyright (C) 2013-2015
 *
 * @file   event_operation_encoder.c
 *
 * @remark
 *
 */

#include "event_operation_encoder.h"
#include "arguments_encoder.h"

/*
 * {
 *     "id": 123,
 *     "method": "event_occured",
 *     "params": {
 *         "did": "xxx",
 *         "siid": 1,
 *         "eiid": 4,
 *         "arguments": [
 *            {
 *              "piid": 1,
 *              "value": 17
 *            },
 *            {
 *              "piid": 2,
 *              "value": 11.3
 *           }
 *         ]
 *    }
 * }
 */

int event_operation_encode(event_operation_t *o, char out[], size_t size)
{
    size_t length = 0;
    jsmi_composer_t composer = JSMI_COMPOSER_INIT(out, size);
    jsmi_compose_start(&composer);

    jsmi_set_object_begin(&composer);

    jsmi_set_key(&composer, "did");
    jsmi_set_value_str(&composer, o->did, 0);

    jsmi_set_key(&composer, "siid");
    jsmi_set_value_uint(&composer, o->siid);

    jsmi_set_key(&composer, "eiid");
    jsmi_set_value_uint(&composer, o->eiid);

    add_arguments("arguments", &composer, o->arguments);

    jsmi_set_object_end(&composer);

    if (jsmi_compose_finish(&composer, NULL, &length) == MIIO_OK)
    {
        return length;
    }

    return 0;
}
