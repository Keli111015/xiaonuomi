/**
 * Copyright (C) 2013-2015
 *
 * @file   action_operation_decoder.c
 *
 * @remark
 *
 */

#include "action_operation_decoder.h"
#include "util.h"
#include "arch_dbg.h"

#define TAG "action_operation_decoder"

static int get_did(jsmi_parser_t *parser, jsmntok_t *token, char did[128])
{
	jsmi_tok_path_t path[] =
	{
		{
			.key = "did",
            .type = JSMN_STRING
        }
    };

    return jsmi_get_value_str(parser, token, path, NELEMENTS(path), did, 128);
}

static int get_siid(jsmi_parser_t *parser, jsmntok_t *token, uint32_t *siid)
{
	jsmi_tok_path_t path[] =
	{
		{
			.key = "siid",
            .type = JSMN_PRIMITIVE
        }
    };

    return jsmi_get_value_uint(parser, token, path, NELEMENTS(path), siid);
}

static int get_aiid(jsmi_parser_t *parser, jsmntok_t *token, uint32_t *aiid)
{
	jsmi_tok_path_t path[] =
	{
		{
			.key = "aiid",
            .type = JSMN_PRIMITIVE
        }
    };

    return jsmi_get_value_uint(parser, token, path, NELEMENTS(path), aiid);
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

static int get_argument(jsmi_parser_t *parser, jsmntok_t *token, int index, argument_t *argument)
{
	int ret = MIIO_OK;

	ret = get_piid(parser,token,index,&argument->piid);
	if (ret != MIIO_OK)
	{
		return ret;
	}

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
				argument->value = property_value_new_string_from(parser->js, v->start, v->end);
				break;

			case JSMN_PRIMITIVE:
				argument->value = property_value_new_from_jsmn_primitive(parser->js, v->start, v->end);
				break;

			default:
				LOG_INFO("value: undefined type: %d\n", v->type);
				ret = -3;
				break;
		}
	} while (false);

	return ret;
}

static int get_arguments_in(jsmi_parser_t *parser, jsmntok_t *token, action_operation_t *o)
{
	int ret = MIIO_OK;

	do
	{
		jsmntok_t *in = NULL;
		{
			jsmi_tok_path_t path[] =
			{
				{
					.key = "in",
					.type = JSMN_ARRAY
				}
			};

			in = jsmi_get_tok(parser, token, path, NELEMENTS(path));
		}
		if (in && in->size){
			if (in->size > MAX_ARGUMENTS)
			{
				LOG_ERROR_TAG(TAG, "argument in > %d\n", MAX_ARGUMENTS);
				ret = MIIO_ERROR_PARAM;
				break;
			}

			for (int i = 0; i < in->size; ++i)
			{
				ret = get_argument(parser, in, i, &(o->in->arguments[i]));
				if (ret != MIIO_OK)
				{
					LOG_ERROR_TAG(TAG, "argument in [%d] parse fail[%d]\n", i, ret);
					ret = MIIO_ERROR_PARAM;
					break;
				}

				o->in->size ++;
			}
		}
	} while (false);

	return ret;
}

/**
 * {
 *   "did" : "1234",
 *   "siid" : 1,
 *   "aiid" : 1,
 *   "in" : [
 *      {
 *        "piid" : 1,
 *        "value": 10
 *      },
 * 		{
 *        "piid" : 7,
 *        "value": false
 *      }
 *   ]
 * }
 */
action_operation_t * action_operation_decode(jsmi_parser_t *parser, jsmntok_t *params)
{
	int ret = MIIO_OK;
	action_operation_t * thiz = action_operation_new();

	do
 	{
		ret = get_did(parser, params, thiz->did);
		if (ret != MIIO_OK)
		{
			break;
		}

		ret = get_siid(parser, params, &thiz->siid);
		if (ret != MIIO_OK)
		{
			break;
		}

		ret = get_aiid(parser, params, &thiz->aiid);
		if (ret != MIIO_OK)
		{
			break;
		}

		ret = get_arguments_in(parser, params, thiz);
		if (ret != MIIO_OK)
		{
			break;
		}
	} while (false);

	if (ret != MIIO_OK)
	{
		action_operation_delete(thiz);
		thiz = NULL;
	}

	return thiz;
}
