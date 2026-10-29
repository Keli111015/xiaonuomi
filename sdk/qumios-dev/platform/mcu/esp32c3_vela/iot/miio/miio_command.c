#include "qm.h"
#include "qm_utils_list.h"

#include "qm_uart.h"

#include "util.h"
#include "miio_loop.h"
#include "miio_command.h"
#include "miio_operation.h"

#include "iot_generic.h"
#include "iot_generic_hal.h"

typedef int (*miio_cmd_handle_t)(void *handle, void *data, uint32_t len);

typedef struct 
{
	char name[20] ;
	miio_cmd_handle_t cmd_cb;
}miio_cmd_t;

typedef struct 
{
	void  *uart;
    qm_list_t   miio_cmd_list;
}miio_cmd_init_t;

static miio_cmd_init_t *miio_init = NULL;
static char g_cmd_buff[CMD_STR_MAX_LEN] = {0};

int do_get_properties(void *handle, void *data, uint32_t len)
{
	int ret = QM_EOK;
	char *result   = NULL;
	char *cmd_buf  = NULL;
	uint16_t result_len = 0;
	uint16_t params_pairs =  0;

	if(handle == NULL || data == NULL || len == 0){
		return -QM_EINVAL;
	}

	cmd_buf = (char *)qm_malloc(len);
	if(NULL == cmd_buf){
		return -QM_EINVAL;
	} 	

    memset(cmd_buf, 0, len);
	memcpy(cmd_buf, data, len - 1);	
	cmd_buf[len - 1] = '\0';

    char *temp = strtok(cmd_buf, " ");	// pass string "down" 
	temp = strtok(NULL, " ");			// pass string "get_properties"/"set_properties" 

	while(1) {
		temp = strtok(NULL, " ");
		if (NULL != temp){ 
			params_pairs++; 
		}else{ 
			break; 
		}
	}
	
	qm_free(cmd_buf);
	cmd_buf = NULL;

	if(params_pairs % 2 != 0) {
		ret = -1;
	}else{ 
		params_pairs /= 2; 
	}	

	for (uint32_t i = 0; i < params_pairs; ++i) {
		// decode command params 
		property_operation_t * opt = miio_property_operation_decode(data, len, i, 0);
		if (opt == NULL) { 
			break;
		}
		ret = on_property_get(opt);
		if(ret >= 0){
			result_len += ret;
		}
		miio_property_operation_delete(opt);
	}

	result_len += 16 * params_pairs + 10;

	result = (char *)qm_malloc(result_len + 1);
	if(result == NULL){
		return -QM_ERROR;
	}
    memset(result, 0, result_len + 1);
	str_n_cat(result, 1, "result ");
	for (uint32_t i = 0; i < params_pairs; ++i) {
		property_operation_t * opt = miio_property_operation_decode(data, len, i, 0);

		if (opt == NULL) { 
			break;
		}
		on_property_get(opt);
		property_operation_encode_param(result, result_len + 1, opt, 0);
		miio_property_operation_delete(opt);
	}
	property_operation_encode_tail(result, result_len + 1);

	miio_uart_send_str_wait_ack(handle, result, 1000);

	qm_free(result);
	result = NULL;

	return QM_EOK;	
}

int do_set_properties(void *handle, void *data, uint32_t len)
{

	if(handle == NULL || data == NULL || len == 0){
		return -QM_EINVAL;
	}

	int ret = QM_EOK;
	char *result = NULL;
	char *cmd_buf  = NULL;
	int params_pairs =  0;

	cmd_buf = (char *)qm_malloc(len);
	if(NULL == cmd_buf){
		return -QM_ERROR;
	} 	
    memset(cmd_buf, 0, len);
	memcpy(cmd_buf, data, len - 1);	
	cmd_buf[len - 1] = '\0';

    char *temp = strtok(cmd_buf, " ");	// pass string "down" 
	temp = strtok(NULL, " ");			// pass string "get_properties"/"set_properties" 

	while(1) {
		temp = strtok(NULL, " ");
		if (NULL != temp){ 
			params_pairs++; 
		}else{ 
			break; 
		}
	}
	
	qm_free(cmd_buf);
	cmd_buf = NULL;

	if(params_pairs % 3 != 0) {
		ret = -QM_ERROR;
	}else{ 
		params_pairs /= 3; 
	}	
	
	result = (char *)qm_malloc((params_pairs + 1) * 20);
	if(NULL == result){
		return -QM_ERROR;
	} 		
	memset(result, 0, (params_pairs + 1) * 20);

	str_n_cat(result, 1, "result ");
	
	for (uint32_t i = 0; i < params_pairs; ++i) {
		// decode command params 
		property_operation_t * opt = miio_property_operation_decode(data, len, i, 1);

		if (opt == NULL) { 
			break;
		}

		on_property_set(opt);

		property_operation_encode_param(result, (params_pairs + 1) * 20, opt, 1);
		miio_property_operation_delete(opt);
	}

	property_operation_encode_tail(result, (params_pairs + 1) * 20);

	miio_uart_send_str_wait_ack(handle, result, 1000);

	qm_free(result);
	result = NULL;

	return ret;
}

#ifdef CONFIG_IOT_GENERIC_ACTION_SUPPORT
int do_invoke_action(void *handle, void *data, uint32_t len)
{

	if(handle == NULL || data == NULL || len == 0){
		return -QM_EINVAL;
	}

	int ret = 0;
	int params_pairs = 0;
	
	char *cmd_buf = (char *)qm_malloc(len);
	if(NULL == cmd_buf){
		return -QM_ERROR;
	}

	memset(cmd_buf, 0, len);
	memcpy(cmd_buf, data, len - 1);

	cmd_buf[len - 1] = '\0'; /* remove '\r' at the end */

	/* don't need to check temp, cause it checked before enter into this func */
	char *temp = strtok(cmd_buf, " ");	/* pass string "down" */
	temp = strtok(NULL, " ");	/* pass string "action" */

	while(1) {
		temp = strtok(NULL, " ");
		if (NULL != temp) {
			params_pairs++; 
		}
		else { break; }
	}

	qm_free(cmd_buf);
	cmd_buf = NULL;

	if(params_pairs >= 4 && params_pairs % 2 != 0) {
		return -QM_ERROR;
	}

	params_pairs = (params_pairs + 3) / 4;

	char * result = qm_malloc(RESULT_BUF_SIZE);
	if(result == NULL){
		return -QM_ERROR;
	}
	memset(result, 0, RESULT_BUF_SIZE);

	str_n_cat(result, 1, "result ");
	
	for (uint32_t i = 0; i < params_pairs; ++i) {

		// decode command params
		action_operation_t * opt = action_operation_decode(data, len, i);

		if (opt == NULL){		
			break; 
		}
		on_action_invoke(opt);	
		action_operation_encode_param(result, RESULT_BUF_SIZE, opt);
		action_operation_delete(opt);

	}
	
	action_operation_encode_tail(result, RESULT_BUF_SIZE);

	miio_uart_send_str_wait_ack(handle, result, 1000);


	qm_free(result);
	result = NULL;
	return ret;
}
#endif

#if 0
int do_invoke_none(void *data, uint32_t len)
{
	QM_LOGD(LOG_TAG,"=== none ====");


	return QM_EOK;
}



int do_net_changed(void *handle, void *data, uint32_t len)
{
	QM_LOGD(LOG_TAG,"=== do_net_changed ===");
	return QM_EOK;
}
#endif

int do_update(void *handle, void *data, uint32_t len)
{
	int ret = QM_EOK;
	char pbuf[25] = {0};
	uint8_t ota_progress = 101;

	if(handle == NULL || data == NULL || len == 0){
		return -QM_EINVAL;
	}

	ret = miio_uart_send_str_wait_ack(handle, "result \"ready\"\r", 1000);
	if(ret == QM_EOK){
		ota_progress = 100;
		iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, IOT_GENERIC_EVENT_DEVICE_OTA_COMPLETE);
	}

	snprintf(pbuf, 25, "inc_installing %s\r", (ota_progress == 100 ? "100" : "error"));

	return miio_uart_send_str_wait_ack(handle, pbuf, 1000);
}

int do_get_version(void *handle, void *data, uint32_t len)
{
	miio_uart_send_str_wait_ack(handle, "mcu_version "CONFIG_MIIO_VERSION"\r", 1000);
	return QM_EOK;
}

static int miio_cmd_reg(miio_cmd_init_t *mi_init, char *name, miio_cmd_handle_t cmd_cb)
{
	qm_list_node_t *node = NULL;
	miio_cmd_t *miio_cmd = NULL;

	if(mi_init == NULL || name == NULL || cmd_cb == NULL){
		return -QM_EINVAL;
	}

	node = qm_list_node_extra_new(sizeof(miio_cmd_t));
    if(node == NULL){
        return -QM_ENOMEM;
    }

	miio_cmd = (miio_cmd_t *)node->val;
	memcpy(miio_cmd->name, name, strlen(name));
	miio_cmd->cmd_cb = cmd_cb;

    qm_list_rpush(&mi_init->miio_cmd_list, node);

	return QM_EOK;
}

void *miio_cmd_init(void *handle)
{
	int ret = QM_EOK;

	if(handle == NULL){
		goto _exit;
	}

    miio_init = (miio_cmd_init_t*)qm_malloc(sizeof(miio_cmd_init_t));
    if(miio_init == NULL){
        goto _exit;
    } 

    memset(miio_init, 0, sizeof(miio_cmd_init_t));

	miio_init->uart = handle;

	ret = miio_cmd_reg(miio_init, "get_properties", do_get_properties);
	if (ret != QM_EOK) {
		goto _exit;

	}
	ret = miio_cmd_reg(miio_init, "set_properties", do_set_properties);
	if (ret != QM_EOK) {
		goto _exit;

	}

#ifdef CONFIG_IOT_GENERIC_ACTION_SUPPORT
	ret = miio_cmd_reg(miio_init, "action", do_invoke_action);
	if (ret != QM_EOK) {
		goto _exit;

	}
#endif

	ret = miio_cmd_reg(miio_init, "MIIO_net_change", do_net_changed);
	if (ret != QM_EOK) {
		goto _exit;

	}

	//add ot cmd method
	ret = miio_cmd_reg(miio_init, "update_fw", do_update);
	if (ret != QM_EOK) {
		goto _exit;
	}

	ret = miio_cmd_reg(miio_init, "MIIO_mcu_version_req", do_get_version);
	if (ret != QM_EOK)
	{
		goto _exit;
	}

_exit:

    return (void *)miio_init;   
}

int miio_command_find_by_method(void *handle, char *method, void *data, uint32_t len)
{
	int ret = QM_ERROR;
	miio_cmd_init_t *miio_handle = (miio_cmd_init_t *)handle;
	
	qm_list_iterator_t self = {0};
    qm_list_node_t *node = NULL;
    miio_cmd_t *cmd_handle = NULL;
 
    self.direction = LIST_HEAD;
    self.next = miio_handle->miio_cmd_list.head;

	if(handle == NULL || method == NULL){
		return -QM_EINVAL;
	}

	if(data == NULL || len == 0){
		return -QM_EINVAL;
	}

    node = qm_list_iterator_next(&self);
    while (node)
    {
        cmd_handle = (miio_cmd_t *)node->val;
		if (cmd_handle){	
			if(!strncmp(cmd_handle->name, method, strlen(method))){
				ret = cmd_handle->cmd_cb(miio_handle->uart, data, len);
				break;
			}
		}
        node = qm_list_iterator_next(&self);
    }
	
	return ret;
}

int uart_comamnd_decoder(char *pbuf, uint32_t buf_sz, char *method, uint32_t *method_len)
{
    int ret = QM_EOK;
	char *cmd_buf = &g_cmd_buff[0];
	if(pbuf == NULL || method == NULL || method_len == NULL) {
        ret = -QM_EINVAL;
		goto _exit;
	}

	memset(cmd_buf, 0, CMD_STR_MAX_LEN);

	strncpy(cmd_buf, pbuf, buf_sz);
	cmd_buf[buf_sz - 1] = ' ';

	char *temp = NULL;

	temp = strtok(cmd_buf, " ");	/* pass "down" */
	if(NULL == temp) {
        ret = -QM_EINVAL;
		goto _exit;
	}

	/* judge if recieved "error\r" */
	if(!strncmp(temp, "error", strlen("error") < strlen(temp) ? strlen("error") : strlen(temp))) {
        ret = -QM_ERROR;
        goto _exit;
	}

	/* get "get_properties"/"set_properties"/"none"/"update_fw"/"action" */
	temp = strtok(NULL, " ");
	if(NULL == temp){
        ret = -QM_ERROR;
        goto _exit; 
    }

	if(!strncmp(temp, "none", strlen("none") < strlen(temp) ? strlen("none") : strlen(temp))) {
        goto _exit; 
	}

	memcpy(method, temp, strlen(temp));
	*method_len = strlen(method);

_exit:

	return ret;
}

