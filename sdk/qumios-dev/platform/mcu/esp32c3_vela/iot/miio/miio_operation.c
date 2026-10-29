/**
 * Copyright (C) 2013-2015
 *
 * @file   property_operation.c
 *
 * @remark
 *
 */
#include "qm_types.h"
#include "qm_kernel.h"
#include "qm_errno.h"
#include "qm_log.h"

#include "syslog.h"
// #include "user_config.h"
#include "util.h"
#include "miio_operation.h"

#define LOG_TAG "miio_opr"

#define ID_MAX_LEN			4
#define VALUE_MAX_LEN		16


property_value_t * property_value_new(void)
{
    property_value_t * thiz = NULL;

    thiz = (property_value_t *) qm_malloc(sizeof(property_value_t));
    if (thiz == NULL){
        goto _exit;
    }

    memset(thiz, 0, sizeof(property_value_t));
    thiz->format = PROPERTY_FORMAT_UNDEFINED;

_exit:
    return thiz;
}

property_value_t * property_value_new_string(const char *value)
{
    property_value_t * thiz = property_value_new();
    if (thiz != NULL){
        thiz->format = PROPERTY_FORMAT_STRING;
        strncpy(thiz->data.string.value, value, DATA_STRING_MAX_LENGTH);
        thiz->data.string.length = strlen(thiz->data.string.value);
    }

    return thiz;
}

property_value_t * property_value_new_uinteger(long value)
{
    property_value_t * thiz = property_value_new();

    if (thiz != NULL){
        thiz->format = PROPERTY_FORMAT_NUMBER;
        thiz->data.number.type = DATA_NUMBER_UINTEGER;
        thiz->data.number.value.integerValue = value;
    }

    return thiz;	
}

property_value_t * property_value_new_integer(long value)
{
    property_value_t * thiz = property_value_new();

    if (thiz != NULL){
        thiz->format = PROPERTY_FORMAT_NUMBER;
        thiz->data.number.type = DATA_NUMBER_INTEGER;
        thiz->data.number.value.integerValue = value;
    }

    return thiz;
}

property_value_t * property_value_new_float(double value)
{
    property_value_t * thiz = property_value_new();
    if (thiz != NULL){
        thiz->format = PROPERTY_FORMAT_NUMBER;
        thiz->data.number.type = DATA_NUMBER_FLOAT;
        thiz->data.number.value.floatValue = value;
    }

    return thiz;
}

property_value_t * property_value_new_boolean(bool_t value)
{
    property_value_t * thiz = property_value_new();
    if (thiz != NULL){
        thiz->format = PROPERTY_FORMAT_BOOLEAN;
        thiz->data.boolean.value = value;
    }

    return thiz;
}

void property_value_delete(property_value_t *thiz)
{
    qm_free(thiz);
	thiz = NULL;
}


arguments_t * arguments_new(void)
{
	arguments_t * thiz = NULL;

	do
	{
		thiz = (arguments_t *) qm_malloc(sizeof(arguments_t));
		if (thiz == NULL)
		{
			break;
		}

		memset(thiz, 0, sizeof(arguments_t));
		thiz->size = 0;
	} while (QM_FALSE);

	return thiz;
}

void arguments_delete(arguments_t *thiz)
{
	int i = 0;
	if (thiz != NULL) {
		for (i = 0; i < thiz->size; ++i) {
			if (thiz->arguments[i].value != NULL) {
				property_value_delete(thiz->arguments[i].value);
				thiz->arguments[i].value = NULL;
			}
		}
		
		qm_free(thiz);
	}

	return;
}

property_operation_t *miio_property_operation_new(void)
{
    property_operation_t *thiz = NULL;

    thiz = (property_operation_t *)qm_malloc(sizeof(property_operation_t));
    if (thiz == NULL){
        goto _exit;
    }
    memset(thiz, 0, sizeof(property_operation_t));
_exit:
    return thiz;
}

void miio_property_operation_delete(property_operation_t *thiz)
{
	if (thiz->value != NULL) {
		property_value_delete(thiz->value);
	}

	qm_free(thiz);
}

static int miio_get_value(const char *pbuf, property_operation_t *opt)
{
	int ret = QM_EOK;
	property_value_t *new_value =(property_value_t *)qm_malloc(sizeof(property_value_t));
	if(new_value == NULL){
		return -QM_ENOMEM;
	}

    const char *temp = pbuf;

    if( *temp == '\"'){
        new_value->format = PROPERTY_FORMAT_STRING;
        new_value->data.string.length = strlen(temp) - 2;
        strncpy(new_value->data.string.value, temp + 1, new_value->data.string.length);
    }
    else if( !strncmp(temp, "true", strlen("true"))){
        new_value->format = PROPERTY_FORMAT_BOOLEAN;
        new_value->data.boolean.value = QM_TRUE;
    }
    else if( !strncmp(temp, "false", strlen("false"))){
        new_value->format = PROPERTY_FORMAT_BOOLEAN;
        new_value->data.boolean.value = QM_FALSE;
    }
    else {
        // judge if integer value
        if(NULL == strchr(temp, '.')) {
            new_value->format = PROPERTY_FORMAT_NUMBER;
            new_value->data.number.type = DATA_NUMBER_INTEGER;
            new_value->data.number.value.integerValue = atos64n(temp, strlen(temp));
            new_value->data.number.value.floatValue = atof_my(temp);
        }else {
            new_value->format = PROPERTY_FORMAT_NUMBER;
            new_value->data.number.type = DATA_NUMBER_FLOAT;
            new_value->data.number.value.floatValue = atof_my(temp);
        }
    }

	opt->value = new_value;

	return ret;
}

property_operation_t * miio_property_operation_decode(const char *pbuf, size_t buf_sz, uint32_t index, bool_t has_value)
{
	int ret = QM_EOK;

	property_operation_t * thiz = miio_property_operation_new();
	if(thiz == NULL){
		return NULL;
	}
	char *p_cmd_str = (char *)qm_malloc(buf_sz);
	if (p_cmd_str == NULL){
		miio_property_operation_delete(thiz);
		return NULL;
	}
	memset(p_cmd_str, 0, buf_sz);
	// remove '\r' at the pbuf end
	memcpy(p_cmd_str, pbuf, buf_sz - 1);

	char *temp = NULL;
    temp = strtok(p_cmd_str, " ");	// pass string "down" 
	temp = strtok(NULL, " ");		// pass string "get_properties"/"set_properties" 

	// pass (index)s param
	int index_temp = index;
	if(has_value){
		while(index_temp){
			temp = strtok(NULL, " ");
			temp = strtok(NULL, " ");
			temp = strtok(NULL, " ");
			index_temp--;
		}
	}
	else {
		while(index_temp){
			temp = strtok(NULL, " ");
			temp = strtok(NULL, " ");
			index_temp--;
		}
	}
	
	do {
		// get siid string
		temp = strtok(NULL, " ");
		thiz->siid = atos64n(temp, strlen(temp));

		// get piid string
		temp = strtok(NULL, " ");
		thiz->piid = atos64n(temp, strlen(temp));

		// 如果是 get 方法不需要获得参数中的 value
		if (! has_value) {
			break;
		}

		// get value string
		temp = strtok(NULL, " ");
		ret = miio_get_value(temp, thiz);

		if (ret != QM_EOK) { 
			break; 
		}

	} while (QM_FALSE);
	
	if (ret != QM_EOK) {
		miio_property_operation_delete(thiz);
		thiz = NULL;
	}
	
	if(NULL != p_cmd_str) {
		qm_free(p_cmd_str);
	}

	return thiz;
}

int property_operation_encode_param(char *pbuf, size_t buf_sz, property_operation_t *opt, bool_t has_value)
{
	int ret = QM_EOK;

	if (pbuf == NULL || buf_sz <= 0 || opt == NULL) {
		ret = QM_EINVAL;
		return ret;
	}

	// TODO : 错误处理
	char siid_buf[ID_MAX_LEN] = {0};
	char piid_buf[ID_MAX_LEN] = {0};
	char code_buf[ID_MAX_LEN + 4] = {0};
	snprintf(siid_buf, ID_MAX_LEN, "%d", opt->siid);
	snprintf(piid_buf, ID_MAX_LEN, "%d", opt->piid);
	snprintf(code_buf, ID_MAX_LEN + 4, "%d", opt->code);
	str_n_cat(pbuf, 6, siid_buf, " ", piid_buf, " ", code_buf, " ");
	if(0 != opt->code) {
		str_n_cat(pbuf, 1, " ");
		return QM_EOK;
	}
	
	if(!has_value) {
		switch(opt->value->format){
				
			case PROPERTY_FORMAT_BOOLEAN :
				if(opt->value->data.boolean.value == QM_FALSE)
					strncat(pbuf, "false", strlen("false"));
				else
					strncat(pbuf, "true", strlen("true"));
				break;

			case PROPERTY_FORMAT_STRING :
				str_n_cat(pbuf, 3, "\"", opt->value->data.string.value, "\"");
				break;

			case PROPERTY_FORMAT_NUMBER :
				if(opt->value->data.number.type == DATA_NUMBER_INTEGER){
					char integer_buf[VALUE_MAX_LEN] = {0};
					snprintf(integer_buf, VALUE_MAX_LEN, "%ld", opt->value->data.number.value.integerValue);
					strncat(pbuf, integer_buf, strlen(integer_buf));
				}

				if(opt->value->data.number.type == DATA_NUMBER_UINTEGER){
					char integer_buf[VALUE_MAX_LEN] = {0};
					snprintf(integer_buf, VALUE_MAX_LEN, "%lu", opt->value->data.number.value.integerValue);
					strncat(pbuf, integer_buf, strlen(integer_buf));
				}

				if(opt->value->data.number.type == DATA_NUMBER_FLOAT){
					char integer_buf[VALUE_MAX_LEN] = {0};
					snprintf(integer_buf, VALUE_MAX_LEN, "%f", (double)opt->value->data.number.value.floatValue);
					strncat(pbuf, integer_buf, strlen(integer_buf));
				}
				break;
			case PROPERTY_FORMAT_UNDEFINED :
				break;
		}
		str_n_cat(pbuf, 1, " ");
	}

	return ret;
}

int property_operation_encode_tail(char *pbuf, size_t buf_sz)
{
	if(NULL == pbuf){
		return -QM_EINVAL;
	}
	if(buf_sz <= strlen(pbuf)){
		return -QM_ENOMEM;
	}

	strcat(pbuf, "\r");
	return QM_EOK;
}

#ifdef CONFIG_IOT_GENERIC_ACTION_SUPPORT
static int miio_action_get_arguments_in(action_operation_t *thiz, char *pbuf, uint32_t piid, int index)
{
	int ret = QM_EOK;
    char *temp = pbuf;
    thiz->in->arguments[index].piid = piid;
    thiz->in->size += 1;

    if( *temp == '\"'){
        thiz->in->arguments[index].value = property_value_new_string(temp);
    } else if( !strncmp(temp, "true", strlen("true"))){
        thiz->in->arguments[index].value =  property_value_new_boolean(QM_TRUE);
    } else if( !strncmp(temp, "false", strlen("false"))){
        thiz->in->arguments[index].value =  property_value_new_boolean(QM_FALSE);
    } else {
        // judge if integer value
        if(NULL == strchr(temp, '.')) {
            thiz->in->arguments[index].value = property_value_new_integer(atos64n(temp, strlen(temp)));
        } else {
            thiz->in->arguments[index].value = property_value_new_float(atof_my(temp));
        }
    }
	return ret;
}


action_operation_t * action_operation_decode(const char *pbuf, size_t buf_sz, uint32_t index)
{
	int ret = QM_EOK;
	action_operation_t *thiz = action_operation_new();
	char *p_cmd_str = (char *)qm_malloc(buf_sz);
	if (p_cmd_str == NULL){
        return NULL;
    }
	memset(p_cmd_str, 0, buf_sz);
	memcpy(p_cmd_str, pbuf, buf_sz - 1);	/* remove '\r' at the pbuf end */

	char *temp = NULL;
    temp = strtok(p_cmd_str, " ");	/* pass "down" */
	temp = strtok(NULL, " ");		/* pass "action" */

	while(index){	/* pass (index * 4) parameters */
		temp = strtok(NULL, " ");
		temp = strtok(NULL, " ");
		temp = strtok(NULL, " ");
		temp = strtok(NULL, " ");
		index--;
	}

    // get siid
    temp = strtok(NULL, " ");
    thiz->siid = atou64n(temp, strlen(temp));

    // get aiid
    temp = strtok(NULL, " ");
    thiz->aiid = atou64n(temp, strlen(temp));

    // get piid & value
    index = 0;
    while((temp = strtok(NULL, " ")) != NULL) {
        uint32_t piid = atou64n(temp, strlen(temp));
        temp = strtok(NULL, " ");
    
        if(NULL == temp || (index + 1) > MAX_ARGUMENTS) {
            ret = -QM_EINVAL;
            break;
        }

        miio_action_get_arguments_in(thiz, temp, piid, index++);
    }
	
	if (ret != QM_EOK) {
		action_operation_delete(thiz);
		thiz = NULL;
	}
	
	if(NULL != p_cmd_str) {
		qm_free(p_cmd_str);
	}
	
	return thiz;
	
}
int action_operation_encode_param(char *pbuf, size_t buf_sz, action_operation_t *opt)
{
	// TODO : 出错处理
	int ret = QM_EOK;

	if (pbuf == NULL || buf_sz <= 0 || opt == NULL) {
		ret = QM_EINVAL;
		return ret;
	}

	char siid_buf[ID_MAX_LEN] = { 0 };
	memset(siid_buf, 0, ID_MAX_LEN);
	char aiid_buf[ID_MAX_LEN] = { 0 };
	memset(aiid_buf, 0, ID_MAX_LEN);
	char piid_buf[ID_MAX_LEN] = { 0 };
	memset(piid_buf, 0, ID_MAX_LEN);
	char code_buf[ID_MAX_LEN] = { 0 };
	memset(code_buf, 0, ID_MAX_LEN);

	snprintf(siid_buf, ID_MAX_LEN, "%d", opt->siid);
	snprintf(aiid_buf, ID_MAX_LEN, "%d", opt->aiid);
	snprintf(code_buf, ID_MAX_LEN, "%d", opt->code);

	if(0 != opt->code) {
		str_n_cat(pbuf, 1, " ");
		return QM_EOK;
	}
	

	str_n_cat(pbuf, 6, siid_buf, " ", aiid_buf, " ", code_buf, " ");

	for(int i = 0; i < opt->out->size; i++) {
		
		snprintf(piid_buf, ID_MAX_LEN, "%d", opt->out->arguments[i].piid);
		str_n_cat(pbuf, 2, piid_buf, " ");
	
		switch(opt->out->arguments[i].value->format){
				
			case PROPERTY_FORMAT_BOOLEAN :
				if(opt->out->arguments[i].value->data.boolean.value == QM_FALSE) {
					strncat(pbuf, "false", strlen("false"));
				} else {
					strncat(pbuf, "true", strlen("true"));
				}
				break;

			case PROPERTY_FORMAT_STRING :
				str_n_cat(pbuf, 3, "\"", opt->out->arguments[i].value->data.string.value, "\"");
				break;

			case PROPERTY_FORMAT_NUMBER :
				if(opt->out->arguments[i].value->data.number.type == DATA_NUMBER_INTEGER){
					char integer_buf[VALUE_MAX_LEN] = {0};
					snprintf(integer_buf, VALUE_MAX_LEN, "%ld", opt->out->arguments[i].value->data.number.value.integerValue);
					strncat(pbuf, integer_buf, strlen(integer_buf));
				}

				if(opt->out->arguments[i].value->data.number.type == DATA_NUMBER_UINTEGER){
					char integer_buf[VALUE_MAX_LEN] = {0};
					snprintf(integer_buf, VALUE_MAX_LEN, "%lu", opt->out->arguments[i].value->data.number.value.integerValue);
					strncat(pbuf, integer_buf, strlen(integer_buf));
				}

				if(opt->out->arguments[i].value->data.number.type == DATA_NUMBER_FLOAT){
					char integer_buf[VALUE_MAX_LEN] = {0};
					snprintf(integer_buf, VALUE_MAX_LEN, "%f", opt->out->arguments[i].value->data.number.value.floatValue);
					strncat(pbuf, integer_buf, strlen(integer_buf));
				}
				break;
				
			case PROPERTY_FORMAT_UNDEFINED :
				break;
		}

		str_n_cat(pbuf, 1, " ");
	}

	return ret;
}

int action_operation_encode_tail(char *pbuf, size_t buf_sz)
{
	if(NULL == pbuf){
		return -QM_EINVAL;
	}
	if(buf_sz <= strlen(pbuf)){
		return -QM_ERROR;
	}

	strcat(pbuf, "\r");
	return QM_EOK;
}

action_operation_t * action_operation_new(void)
{
    action_operation_t * thiz = NULL;

    do
    {
        thiz = (action_operation_t *) qm_malloc(sizeof(action_operation_t));
        if (thiz == NULL)
        {
            break;
        }

        memset(thiz, 0, sizeof(action_operation_t));

        thiz->code = 0;
        thiz->siid = 0;
        thiz->aiid = 0;

        thiz->in = arguments_new();
        if (thiz->in == NULL)
        {
            action_operation_delete(thiz);
            thiz = NULL;
            break;
        }

        thiz->out = arguments_new();
        if (thiz->out == NULL)
        {
            action_operation_delete(thiz);
            thiz = NULL;
            break;
        }
    } while (QM_FALSE);

    return thiz;
}

void action_operation_delete(action_operation_t *thiz)
{
    if (thiz->in != NULL)
    {
        arguments_delete(thiz->in);
    }

    if (thiz->out != NULL)
    {
        arguments_delete(thiz->out);
    }

    qm_free(thiz);
}
#endif