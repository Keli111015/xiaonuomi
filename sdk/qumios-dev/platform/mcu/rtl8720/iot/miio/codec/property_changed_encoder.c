/**
 * Copyright (C) 2013-2015
 *
 * @file   property_changed_encoder.c
 *
 * @remark
 *
 */

#include "property_changed_encoder.h"
#include <jsmi.h>
#include "util.h"

/**
 * {
 *   "did": "57352830",
 *   "siid": 2,
 *   "piid": 1,
 *   "value": 232
 * }
 */

int property_changed_encode(property_operation_t *o, char out[], size_t size, uint8_t num)
{
    size_t length = 0;
    jsmi_composer_t composer = JSMI_COMPOSER_INIT(out, size);
    jsmi_compose_start(&composer);

    jsmi_set_array_begin(&composer);

    for(uint8_t offset = 0;offset < num; offset++)
    {
        jsmi_set_value_object_begin(&composer);

        jsmi_set_key(&composer, "did");

        arch_u64toa(arch_get_did_ex(), (o + offset)->did);
        jsmi_set_value_str(&composer, (o + offset)->did, 0);

        jsmi_set_key(&composer, "siid");
        jsmi_set_value_uint(&composer, (o + offset)->siid);

        jsmi_set_key(&composer, "piid");
        jsmi_set_value_uint(&composer, (o + offset)->piid);

        jsmi_set_key(&composer, "value");
        switch ((o + offset)->value->format)
        {
            case PROPERTY_FORMAT_BOOLEAN:
                jsmi_set_value_bool(&composer, (o + offset)->value->data.boolean.value);
                break;

            case PROPERTY_FORMAT_STRING:
                jsmi_set_value_str(&composer, (o + offset)->value->data.string.value,(o + offset)->value->data.string.length);
                break;

            case PROPERTY_FORMAT_NUMBER:
                if ((o + offset)->value->data.number.type == DATA_NUMBER_INTEGER){
                    jsmi_set_value_sint(&composer, (o + offset)->value->data.number.value.integerValue);
                }else{
                    jsmi_set_value_double(&composer, (o + offset)->value->data.number.value.floatValue, 4);
                }
                break;

            default:
                break;
        }

        jsmi_set_value_object_end(&composer);
    }

    jsmi_set_array_end(&composer);

    if (jsmi_compose_finish(&composer, NULL, &length) == MIIO_OK)
    {
        return length;
    }

    return 0;
}
