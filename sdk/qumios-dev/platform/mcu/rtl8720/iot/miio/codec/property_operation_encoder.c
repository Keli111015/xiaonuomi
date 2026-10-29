/**
 * Copyright (C) 2013-2015
 *
 * @file   property_operation_encoder.c
 *
 * @remark
 *
 */

#include "property_operation_encoder.h"
#include <jsmi.h>
#include "../typedef/operation_code.h"

/**
 * {
 *   "did": "57352830",
 *   "siid": 2,
 *   "piid": 1,
 *   "value": true,
 *   "code": 0
 * }
 */

int property_operation_encode(property_operation_t *o, bool hasValue, char out[], size_t size)
{
    size_t length = 0;
    jsmi_composer_t composer = JSMI_COMPOSER_INIT(out, size);
    jsmi_compose_start(&composer);

    jsmi_set_object_begin(&composer);

    jsmi_set_key(&composer, "did");
    jsmi_set_value_str(&composer, o->did, 0);

    jsmi_set_key(&composer, "siid");
    jsmi_set_value_uint(&composer, o->siid);

    jsmi_set_key(&composer, "piid");
    jsmi_set_value_uint(&composer, o->piid);

    jsmi_set_key(&composer, "code");
    jsmi_set_value_sint(&composer, o->code);

    if (hasValue && OPERATION_OK == o->code)
    {
        jsmi_set_key(&composer, "value");

        switch (o->value->format)
        {
            case PROPERTY_FORMAT_BOOLEAN:
                jsmi_set_value_bool(&composer, o->value->data.boolean.value);
                break;

            case PROPERTY_FORMAT_STRING:
                jsmi_set_value_str(&composer, o->value->data.string.value, 0);
                break;

            case PROPERTY_FORMAT_NUMBER:
                if (o->value->data.number.type == DATA_NUMBER_INTEGER)
                {
                    jsmi_set_value_sint(&composer, o->value->data.number.value.integerValue);
                }
                else
                {
                    jsmi_set_value_double(&composer, o->value->data.number.value.floatValue, 4);
                }
                break;

            default:
                break;
        }
    }

    jsmi_set_object_end(&composer);

    if (jsmi_compose_finish(&composer, NULL, &length) == MIIO_OK)
    {
        return length;
    }

    return 0;
}
