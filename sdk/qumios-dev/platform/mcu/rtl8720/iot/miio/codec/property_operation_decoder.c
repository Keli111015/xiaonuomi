/**
 * Copyright (C) 2013-2015
 *
 * @file   property_operation_decoder.c
 *
 * @remark
 *
 */

#include "property_operation_decoder.h"
#include "util.h"
#include "arch_dbg.h"

static int get_did(jsmi_parser_t *parser, jsmntok_t *token, int index, char did[128])
{
	jsmi_tok_path_t path[] =
	{
        {
            .key = (void*)index,
            .type = JSMN_OBJECT
        },
		{
			.key = "did",
            .type = JSMN_STRING
        }
    };

    return jsmi_get_value_str(parser, token, path, NELEMENTS(path), did, 128);
}

static int get_siid(jsmi_parser_t *parser, jsmntok_t *token, int index, uint32_t *siid)
{
	jsmi_tok_path_t path[] =
	{
        {
            .key = (void*)index,
            .type = JSMN_OBJECT
        },
		{
			.key = "siid",
            .type = JSMN_PRIMITIVE
        }
    };

    return jsmi_get_value_uint(parser, token, path, NELEMENTS(path), siid);
}

static int get_piid(jsmi_parser_t *parser, jsmntok_t *token, int index, uint32_t *piid)
{
	jsmi_tok_path_t path[] =
	{
        {
            .key = (void*)index,
            .type = JSMN_OBJECT
        },
		{
			.key = "piid",
            .type = JSMN_PRIMITIVE
        }
    };

    return jsmi_get_value_uint(parser, token, path, NELEMENTS(path), piid);
}

static int get_value(jsmi_parser_t *parser, jsmntok_t *token, int index, property_operation_t *o)
{
	int ret = MIIO_OK;

	jsmi_tok_path_t path[] =
	{
        {
            .key = (void*)index,
            .type = JSMN_OBJECT
        },
		{
			.key = "value",
            .type = JSMN_UNDEFINED
        }
    };

	do
	{
		jsmntok_t *v = jsmi_get_tok(parser, token, path, NELEMENTS(path));
    	if (v == NULL)
		{
			break;
		}

		switch (v->type)
		{
			case JSMN_OBJECT:
				ret = -1;
				break;

			case JSMN_ARRAY:
				ret = -2;
				break;

			case JSMN_STRING:
				o->value = property_value_new_string_from(parser->js, v->start, v->end);
				break;

			case JSMN_PRIMITIVE:
				o->value = property_value_new_from_jsmn_primitive(parser->js, v->start, v->end);
				break;

			default:
				LOG_INFO("value: undefined type: %d\n", v->type);
				ret = -3;
				break;
		}
	} while (false);

	return ret;
}

property_operation_t * property_operation_decode(jsmi_parser_t *parser, jsmntok_t *params, int index, bool hasValue)
{
	int ret = MIIO_OK;
	property_operation_t * thiz = property_operation_new();

	do
 	{
		ret = get_did(parser, params, index, thiz->did);
		//为了适配本地自动化网关可能没带did的情况，移除did的判断
		/*
		if (ret != MIIO_OK)
		{
			break;
		}
		*/

		ret = get_siid(parser, params, index, &thiz->siid);
		if (ret != MIIO_OK)
		{
			break;
		}

		ret = get_piid(parser, params, index, &thiz->piid);
		if (ret != MIIO_OK)
		{
			break;
		}

		if (! hasValue)
		{
			break;
		}

		ret = get_value(parser, params, index, thiz);
		if (ret != MIIO_OK)
		{
			break;
		}
	} while (false);

	if (ret != MIIO_OK)
	{
		property_operation_delete(thiz);
		thiz = NULL;
	}

	return thiz;
}
