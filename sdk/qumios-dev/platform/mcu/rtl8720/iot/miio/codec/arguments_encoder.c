/**
 * Copyright (C) 2013-2015
 *
 * @file   arguments_encoder.c
 *
 * @remark
 *
 */

#include "arguments_encoder.h"

static void add_argument(jsmi_composer_t *composer, argument_t *argument)
{
	jsmi_set_value_object_begin(composer);
    jsmi_set_key_value_uint(composer, "piid", argument->piid);

    switch (argument->value->format)
    {
        case PROPERTY_FORMAT_BOOLEAN:
            jsmi_set_key_value_bool(composer, "value", argument->value->data.boolean.value);
            break;

        case PROPERTY_FORMAT_STRING:
            jsmi_set_key_value_str(composer, "value", argument->value->data.string.value, 0);
            break;

        case PROPERTY_FORMAT_NUMBER:
            if (argument->value->data.number.type == DATA_NUMBER_INTEGER)
            {
                jsmi_set_key_value_sint(composer, "value", argument->value->data.number.value.integerValue);
            }
            else
            {
                jsmi_set_key_value_double(composer, "value", argument->value->data.number.value.floatValue, 4);
            }
            break;

        default:
            break;
    }

    jsmi_set_key_object_end(composer);
}

void add_arguments(const char *name, jsmi_composer_t *composer, arguments_t *out)
{
    jsmi_set_key_array_begin(composer, name);

    for (int i = 0; i < out->size; ++i)
    {
        add_argument(composer, & out->arguments[i]);
    }

    jsmi_set_key_array_end(composer);
}
