#include "iot_generic.h"
#include "qm.h"
#include "qm_log.h"
#include "qm_errno.h"

#define LOG_TAG "iot generic"

static void iot_event_notify(iot_generic_event_t event, void *ctx)
{



}

static int iot_set(qm_tlv_ctx_t *tlv_ctx, void *ctx)
{

    return QM_EOK;
}

static int iot_get(qm_tlv_ctx_t *tlv_ctx, void *ctx)
{

    return QM_EOK;
}

static iot_generic_hooks_t hooks = {
    iot_event_notify,
    iot_set,
    iot_get,
    NULL,
    NULL,
    NULL,
};

static void *iot_handle = NULL;

void qm_application_start(void)
{

    QM_LOGI(LOG_TAG, "qm_start");

    iot_handle = iot_generic_init(IOT_GENERIC_PLATFORM_TYPE_ALI, IOT_GENERIC_NETWORK_TYPE_WIFI);
    if(iot_handle == NULL){
        QM_LOGE(LOG_TAG, "iot generic init fail");
        return;
    }

    
    


}