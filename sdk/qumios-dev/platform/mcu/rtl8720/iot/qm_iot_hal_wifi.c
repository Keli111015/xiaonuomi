#include "iot_generic_hal.h"

#include "qm_log.h"
#include "id_map.h"
#include "qm_tlv.h"
#include "qm_kernel.h"
#include "qm_errno.h"
#include "qm_utils_list.h"
#include "operation_code.h"
#include "qm_iot_hal_wifi.h"
#include "util.h"

#include "jsmi.h"

#define REPORT_PRO_NUM      6  
#define REPORT_MAX_NUM      20
#define LOG_TAG "iot_hal"


typedef struct {
    qm_sem_t sem;
    qm_list_t list;
    qm_mutex_t mutex;
}report_handle_t;

typedef struct 
{   
    uint8_t num;
    property_operation_t operation[REPORT_PRO_NUM];
}qm_report_info_t;


report_handle_t *report_handle = NULL;

static iot_generic_event_t event_status = IOT_GENERIC_EVENT_CONNECTING_ROUTER;
static qm_sem_t report_sem = {0};
static uint32_t report_id = 0;
extern miio_handle_t miio_handle;

static qm_timer_t config_timer = {0};
static int report_result = QM_EOK;
static void *iot_hal_handle = NULL;
static qm_timer_t report_timer = {0};
static qm_timer_t wifi_reset_timer = {0};

void on_property_set(property_operation_t *o)
{
#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "[on_property_set] siid %d, piid %d type %d", o->siid, o->piid, o->value->format);
#endif
    o->code = 0;
    int ret = QM_EOK;
    ret = iot_generic_set(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, (char *)o , sizeof(property_operation_t));
    if(ret != QM_EOK){
        o->code = OPERATION_ERROR_CANNOT_WRITE;
    }
} 

int on_property_get(property_operation_t *o)
{
#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "[on_property_get] siid %d, piid %d ", o->siid, o->piid);
#endif
    o->code = 0;
    int out_len = 0;
    int ret = QM_EOK;

    ret = iot_generic_get(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, (char *)o , sizeof(property_operation_t), (char *)o , &out_len);
    if(ret != QM_EOK){
        o->code = OPERATION_ERROR_CANNOT_READ;
    }

    return QM_EOK;
}

void on_action_invoke(action_operation_t *o)
{
    o->code = 0;
#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "[on_action_invoke] siid %d, Aiid %d ", o->siid, o->aiid);
#endif
    int out_len = 0;
    int ret = QM_EOK;
#if CONFIG_IOT_GENERIC_ACTION_SUPPORT
    o->code = iot_generic_action(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, (char *)o , sizeof(action_operation_t), (char *)o , &out_len);
#endif
    if(ret != QM_EOK){
        o->code = OPERATION_ERROR_VALUE;
    }

}

int qm_iot_hal_wifi_handle_set(void *handle)
{
    iot_hal_handle = handle;
    if(iot_hal_handle == NULL){
        return -QM_EINVAL;
    }

    return QM_EOK;
}

int qm_iot_hal_wifi_status_set(iot_generic_event_t status)
{
    event_status = status;
    return QM_EOK;
}

static void config_timer_callback(qm_timer_t *timer, void *arg)
{
    qm_reboot();
}

int qm_iot_hal_wifi_reset(void)
{
    if(miio_handle == NULL){
        qm_timer_new(&config_timer, config_timer_callback, NULL, 200, 0);
        qm_timer_start(&config_timer);
        return QM_EOK;
    }
    arch_psm_erase_key("network", "ssid");
    arch_psm_erase_key("network", "password");
    qm_timer_new(&config_timer, config_timer_callback, NULL, 200, 0);
    qm_timer_start(&config_timer);
    return QM_EOK;
}

static int report_change_ack(miio_rpc_delegate_arg_t* arg, void* ctx)
{
    jsmi_parser_t jsmi_parser = JSMI_PARSER_INIT(arg->pload, arg->pload_len);
    if(QM_EOK == jsmi_parse_start(&jsmi_parser)){
        int v_int = 0;
        report_result = QM_EOK;
        jsmi_tok_path_t path[] = {
            {
                .key = "error",
                .type = JSMN_OBJECT
            },
            {
                .key = "code",
                .type = JSMN_PRIMITIVE
            }
        };
        if(MIIO_OK == jsmi_get_value_sint(&jsmi_parser, NULL, path, NELEMENTS(path), &v_int)){
            if(v_int != QM_EOK){
                report_result = v_int;
            }
        } 
        jsmi_parse_finish(&jsmi_parser);
    }
#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "[report_change_ack] report_result %d paylod : %s  ", report_result, arg->pload);
#endif 

    qm_sem_signal(&report_sem);
    return QM_EOK;
}

static void serial_report_thread(void *arg)
{
    int report_num = 0;
    uint8_t wait_time = 0;
    qm_list_node_t *node  = NULL;
    qm_report_info_t *report_info = NULL;

    while(1)
    {
        qm_sem_wait(&report_handle->sem, QM_WAIT_FOREVER);

        qm_mutex_lock(&report_handle->mutex, QM_WAIT_FOREVER);
        report_num = qm_list_len_get(&report_handle->list);
        qm_mutex_unlock(&report_handle->mutex);

        if(report_num == 0){
            continue;
        }

        qm_mutex_lock(&report_handle->mutex, QM_WAIT_FOREVER);
        node = qm_list_rpop(&report_handle->list);
        report_num = qm_list_len_get(&report_handle->list);
        qm_mutex_unlock(&report_handle->mutex);
        if(node == NULL){
            continue;
        }

        qm_report_info_t *report_info = (uint8_t*)node->val;
        
        report_result = 0;
		report_id = miio_get_rpc_id(miio_handle);

        do{
            if(event_status != IOT_GENERIC_EVENT_CONNECTED_CLOUD){
                break;
            }
            // qm_msleep(100);
            send_property_changed(report_id, miio_handle, report_change_ack, report_info->operation, report_info->num);
            qm_sem_wait(&report_sem, 10 * 1000);
        }while (report_result != QM_EOK);
    
        for(uint8_t i = 0; i < report_info->num; i++){
            if((report_info->operation + i)->value != NULL){
                free((report_info->operation + i)->value);
                (report_info->operation + i)->value = NULL;
            }
        }    
        wait_time = 0;

        if(node != NULL)
        {
            qm_free(node);
            node = NULL;
        }
        if(report_num){
            qm_sem_signal(&report_handle->sem);
    }
    }

    if(node != NULL)
    {
        qm_free(node);
        node = NULL;
    }

    if(report_num){
        qm_sem_signal(&report_handle->sem);
    }
}

int qm_iot_hal_wifi_init(void)
{
    qm_task_t report_task = {0};

    qm_sem_new(&report_sem, 0);

    report_handle = (report_handle_t*)qm_malloc(sizeof(report_handle_t));
    memset(report_handle , 0, sizeof(report_handle_t));

    qm_sem_new(&report_handle->sem, 0);
    qm_mutex_new(&report_handle->mutex);
    qm_task_new(&report_task, "report", serial_report_thread, NULL, 2048, 3);
    return QM_EOK;
}

int qm_iot_hal_wifi_adapter_prop_report(property_operation_t *operation, uint8_t num)
{
    qm_list_node_t *node = NULL;
    qm_report_info_t  *report_info = NULL;

    if(event_status != IOT_GENERIC_EVENT_CONNECTED_CLOUD){
        return -QM_EINVAL;
    }
    
    node = qm_list_node_extra_new(sizeof(qm_report_info_t));

    report_info = (qm_report_info_t *)node->val;

    report_info->num = num;
    memcpy(report_info->operation, operation, sizeof(property_operation_t) * report_info->num);

    qm_mutex_lock(&report_handle->mutex, QM_WAIT_FOREVER);
    qm_list_lpush(&report_handle->list, node);
    qm_mutex_unlock(&report_handle->mutex);

    qm_sem_signal(&report_handle->sem);

    return QM_EOK;
}

int qm_iot_hal_wifi_adapter_event_report(event_operation_t  *operation)
{
    if (operation == NULL){
        return -QM_EINVAL;
    }

    int ret = QM_EOK;
    arch_u64toa(arch_get_did_ex(), operation->did);
    ret = send_event_occurred(miio_handle, operation);
    if(operation->arguments){
        arguments_delete(operation->arguments);
        operation->arguments = NULL;
    }
    return ret;

}
