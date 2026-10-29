#include "iot_generic_hal.h"
#include "qm_tlv.h"
#include "qm_kernel.h"
#include "qm_errno.h"

#include "qm_kv.h"
#include "qm_log.h"

#include "ali_main.h"
#include "netconfig.h"
#include "app_entry.h"
#include "iot_export.h"
#include "serial_protocol.h"

#include "cloudapi.h"

#include "xjiang_main.h"

#define LOG_TAG "iot_hal"

static int iot_init(void *arg)
{

    return QM_EOK;
}

static int iot_deinit(void *arg)
{

    return QM_EOK;
}

static int iot_reset(void *arg)
{
    module_status_t status = qm_serial_module_status_get();
    if(status == MODULE_CONNECTED){
        xjiang_reset_start();
    }else{
        aos_schedule_call((aos_call_t)do_awss_reset, NULL);
    }

    netconfig_set(NETCONFIG_ENTRY);
    return QM_EOK;
}


static int iot_set(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx)
{
    if(data == NULL || len == 0 || tlv_ctx == NULL){
        return -QM_EINVAL;
    }
    qm_tlv_deserialize(tlv_ctx, (uint8_t *)data, len, QM_BLOCK_TYPE_DTTV);
    return QM_EOK;
}


static int iot_report(void *map_handle, qm_tlv_ctx_t *tlv_ctx)
{
    #define TLV_REPORT_BUF_SIZE  32

    static uint32_t id = 0;
    qm_tlv_ctx_t *tlv_ctx_tmp = NULL;
    qm_tlv_block_t *tlv_block = NULL;

    int ret = 0;
    int tlv_len = 0;
    uint8_t *tlv_buf = NULL;
    uint8_t *alloc_buf = NULL;
    uint8_t sta_buf[TLV_REPORT_BUF_SIZE] = {0};

    user_example_ctx_t *user_example_ctx = user_example_get_ctx();

    if(tlv_ctx == NULL || user_example_ctx == NULL){
        return -QM_EINVAL;
    }

    tlv_ctx_tmp = qm_tlv_ctx_creat();
    if(tlv_ctx_tmp == NULL){
        QM_LOGD(LOG_TAG,"Down REPORT TLV CREAT NULL!!");
        return -QM_EINVAL; 
    }

    tlv_block = qm_tlv_block_creat();
    if(tlv_block == NULL){
        QM_LOGD(LOG_TAG,"Down REPORT TLV BLOCK CREAT NULL!!");
        qm_tlv_ctx_delete(tlv_ctx_tmp);
        return -QM_EINVAL; 
    }

    qm_tlv_pack_uint32(tlv_block, ++id);
    qm_tlv_block_tag_set(tlv_block, 0xF1);
    qm_tlv_block_data_type_set(tlv_block, QM_DTTV_TYPE_UINT32);

    qm_tlv_block_add(tlv_ctx_tmp, tlv_block);
    qm_tlv_merge(tlv_ctx_tmp, tlv_ctx);

    qm_tlv_serialize(tlv_ctx_tmp, NULL, &tlv_len, QM_BLOCK_TYPE_DTTV, QM_TLV_SERIALIZE_TYPE_ALL);
    if(tlv_len > TLV_REPORT_BUF_SIZE - 1){
        alloc_buf = (uint8_t*)qm_malloc(tlv_len + 1);
        if(alloc_buf == NULL){
            qm_tlv_ctx_delete(tlv_ctx_tmp);
            return -QM_EINVAL;
        }
        memset(alloc_buf, 0, tlv_len + 1);
        tlv_buf = alloc_buf;
    }else{
        tlv_buf = sta_buf;
    }

    qm_tlv_serialize(tlv_ctx_tmp, tlv_buf + 1, &tlv_len, QM_BLOCK_TYPE_DTTV, QM_TLV_SERIALIZE_TYPE_ALL);

    tlv_buf[0] = CMD_REPORT;
    ret = cloud_packet_send(0, MSG_TYPE_RAW_DATA_REPORT, tlv_buf, tlv_len + 1);
    QM_LOGD(LOG_TAG, "Post Raw Data Message ID: %d", ret);
    QM_HEX_LOGD(LOG_TAG,"Post Raw", tlv_buf, tlv_len + 1);

    if(alloc_buf){
        qm_free(alloc_buf);
        alloc_buf = NULL;
    }

    if(tlv_ctx_tmp){
        qm_tlv_ctx_delete(tlv_ctx_tmp);
        tlv_ctx_tmp = NULL;
    }
    return QM_EOK;
}

static int iot_get_req(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx)
{
    int ret = QM_EOK;
    uint32_t index_len = 0;
    qm_tlv_block_t *tlv_block = NULL;

    if(data == NULL ||  tlv_ctx == NULL){
        return -QM_EINVAL;
    }


    while (index_len < len)
    {
        tlv_block = qm_tlv_block_creat();
        if(tlv_block == NULL){
            return -QM_ENOMEM;
        }  

        tlv_block->tag  = *(data + index_len);
        index_len++;
        qm_tlv_block_add(tlv_ctx, tlv_block); 
    }

    return QM_EOK;

}
static int iot_get_rsp(void *map_handle, qm_tlv_ctx_t *tlv_ctx, char *data, int *len)
{
    #define TLV_REPORT_BUF_SIZE  32

    static uint32_t id = 0;
    qm_tlv_ctx_t *tlv_ctx_tmp = NULL;
    qm_tlv_block_t *tlv_block = NULL;

    int ret = 0;
    int tlv_len = 0;
    uint8_t *tlv_buf = NULL;
    uint8_t *alloc_buf = NULL;
    uint8_t sta_buf[TLV_REPORT_BUF_SIZE] = {0};

    user_example_ctx_t *user_example_ctx = user_example_get_ctx();

    if(data == NULL || tlv_ctx == NULL || user_example_ctx == NULL){
        return -QM_EINVAL;
    }

    tlv_ctx_tmp = qm_tlv_ctx_creat();
    if(tlv_ctx_tmp == NULL){
        QM_LOGD(LOG_TAG,"Down REPORT TLV CREAT NULL!!");
        return -QM_EINVAL; 
    }

    tlv_block = qm_tlv_block_creat();
    if(tlv_block == NULL){
        QM_LOGD(LOG_TAG,"Down REPORT TLV BLOCK CREAT NULL!!");
        qm_tlv_ctx_delete(tlv_ctx_tmp);
        return -QM_EINVAL; 
    }

    qm_tlv_pack_uint32(tlv_block, ++id);
    qm_tlv_block_tag_set(tlv_block, 0xF1);
    qm_tlv_block_data_type_set(tlv_block, QM_DTTV_TYPE_UINT32);

    qm_tlv_block_add(tlv_ctx_tmp, tlv_block);
    qm_tlv_merge(tlv_ctx_tmp, tlv_ctx);

    qm_tlv_serialize(tlv_ctx_tmp, NULL, &tlv_len, QM_BLOCK_TYPE_DTTV, QM_TLV_SERIALIZE_TYPE_ALL);
    if(tlv_len > TLV_REPORT_BUF_SIZE - 1){
        alloc_buf = (uint8_t*)qm_malloc(tlv_len + 1);
        if(alloc_buf == NULL){
            qm_tlv_ctx_delete(tlv_ctx_tmp);
            return -QM_EINVAL;
        }
        memset(alloc_buf, 0, tlv_len + 1);
        tlv_buf = alloc_buf;
    }else{
        tlv_buf = sta_buf;
    }

    qm_tlv_serialize(tlv_ctx_tmp, tlv_buf + 1, &tlv_len, QM_BLOCK_TYPE_DTTV, QM_TLV_SERIALIZE_TYPE_ALL);

    tlv_buf[0] = CMD_GET;
    ret = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_POST_RAW_DATA,
                             tlv_buf, tlv_len + 1);
    QM_LOGD(LOG_TAG, "Post Raw Data Message ID: %d", ret);
    QM_HEX_LOGD(LOG_TAG,"Post Raw", tlv_buf, tlv_len + 1);

    if(alloc_buf){
        qm_free(alloc_buf);
        alloc_buf = NULL;
    }

    if(tlv_ctx_tmp){
        qm_tlv_ctx_delete(tlv_ctx_tmp);
        tlv_ctx_tmp = NULL;
    }

    return QM_EOK;
}



iot_generic_hal_t  xjiang_generic_hal = {

    .init = iot_init,
    .report = iot_report,
    .set = iot_set,
    .get_req = iot_get_req,
    .get_rsp = iot_get_rsp,
    .up_rawdata = NULL,
    .reset = iot_reset,
    .deinit = iot_deinit,
};


int iot_generic_xjiang_wifi_init(void)
{
    return iot_generic_hal_register(IOT_GENERIC_PLATFORM_TYPE_XJIANG, IOT_GENERIC_NETWORK_TYPE_WIFI, &xjiang_generic_hal);
}
