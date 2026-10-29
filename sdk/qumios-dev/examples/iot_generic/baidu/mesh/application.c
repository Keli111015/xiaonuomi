#include "qm.h"
#include "qm_log.h"

#include "iot_generic.h"

#include "serial_protocol.h"

#define LOG_TAG "honor"

static int iot_hook_set(qm_tlv_ctx_t *tlv_ctx, void *ctx)
{
    return QM_EOK;
}

static int iot_hook_get(qm_tlv_ctx_t *tlv_ctx, void *ctx)
{
    return QM_EOK;
}

static void iot_hook_event_notify(iot_generic_event_t event, void *ctx)
{
    switch (event) {
        case IOT_GENERIC_EVENT_WIFI_CONFIG:
            QM_LOGD(LOG_TAG, "IOT_GENERIC_EVENT_WIFI_CONFIG");  
        break;
        case IOT_GENERIC_EVENT_CONFIG_TIMEOUT:
            QM_LOGD(LOG_TAG, "IOT_GENERIC_EVENT_CONFIG_TIMEOUT");
        break; 
        case IOT_GENERIC_EVENT_CONNECTING_ROUTER:
            QM_LOGD(LOG_TAG, "IOT_GENERIC_EVENT_CONNECTING_ROUTER");
        break;
        case IOT_GENERIC_EVENT_CONNECTED_ROUTER:
            QM_LOGD(LOG_TAG, "IOT_GENERIC_EVENT_CONNECTED_ROUTER");
        break;
        case IOT_GENERIC_EVENT_CONNECTED_CLOUD:
        case IOT_GENERIC_EVENT_DEVICE_REGISTERED:
            QM_LOGD(LOG_TAG, "IOT_GENERIC_EVENT_CONNECTED_CLOUD");
            qm_serial_module_status_set(MODULE_CONNECTED);
        default:
        break;
    }
}

static iot_generic_hooks_t baidu_hook = {
    .set = iot_hook_set,
    .get = NULL,
    .event_notify = iot_hook_event_notify,
    .ctx = NULL,
};

void qm_application_start(void)
{
    void *iot_handle = NULL;

    /* 1. 绑定iot相关的操作 */
    iot_handle = iot_generic_init(IOT_GENERIC_PLATFORM_TYPE_BAIDU, IOT_GENERIC_NETWORK_TYPE_MESH);
    if (iot_handle == NULL) {
        QM_LOGD(LOG_TAG, "qm_iot_handle is NULL !!!\n");
    }  

    uint32_t pid = 7046003;
    char *sercest = "f421ae12adf6";
    char *model_name = "655c5d33c187bdd4b9f805b324541fdb";

    iot_generic_option_set(iot_handle, IOT_OPTION_PRODUCT_ID, &pid, sizeof(uint32_t));
    iot_generic_option_set(iot_handle, IOT_OPTION_MODEL, model_name, strlen(model_name));
    iot_generic_option_set(iot_handle, IOT_OPTION_DEVICE_SECRET, sercest, strlen(sercest));

    iot_generic_hooks_register(iot_handle, &baidu_hook);

    /* 2. 映射的服务属性 */
    // iot_generic_map_set(qm_iot_handle, (uint8_t *)test_profile, sizeof(test_profile));
}



