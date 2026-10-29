#include "iot_generic_hal.h"
#include "qm_tlv.h"
#include "qm_kernel.h"
#include "qm_errno.h"


static int iot_hal_init(void *arg)
{

    return QM_EOK;
}


static int iot_hal_report(void *map_handle, qm_tlv_ctx_t *tlv_ctx)
{

    return QM_EOK;
}


static int iot_hal_set(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx)
{

    return QM_EOK;
}
 
static int iot_hal_get_req(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx)
{

    return QM_EOK;
}

static int iot_hal_get_rsp(void *map_handle, qm_tlv_ctx_t *tlv_ctx, char *data, int *len)
{

    return QM_EOK;
}

static int iot_hal_reset(void *arg)
{

    return QM_EOK;
}

static int iot_hal_deinit(void *arg)
{

    return QM_EOK;
}



static iot_generic_hal_t  generic_hal_ali_wifi = {

    .init = iot_hal_init,
    .report = iot_hal_report,
    .set = iot_hal_set,
    .get_req = iot_hal_get_req,
    .get_rsp = iot_hal_get_rsp,
    .event = NULL,
    .action_req = NULL,
    .action_rsp = NULL,
    .up_rawdata = NULL,
    .reset = iot_hal_reset,
    .deinit = iot_hal_deinit,
};

int iot_generic_hal_ali_wifi_init(void)
{
    return iot_generic_hal_register(IOT_GENERIC_PLATFORM_TYPE_ALI, IOT_GENERIC_NETWORK_TYPE_WIFI, &generic_hal_ali_wifi);
}