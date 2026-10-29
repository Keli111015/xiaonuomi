
#include "iot_ota.h"
#include "iot_ota_internal.h"
#include "iot_cloud_debug.h"
#include "cloud_common.h"
#include "qm_iot_import.h"
#include "product.h"
#include "iot_event.h"


#if CONFIG_OTA_SUPPORT

#include "iot_ota_lib.c"
#include "iot_ota_fetch.c"
#include "iot_ota_mqtt.c"

/* check whether the progress state is valid or not */
/* return: true, valid progress state; false, invalid progress state. */
static int ota_check_progress(iot_ota_progress_t progress)
{
    return ((progress >= IOT_OTAP_BURN_FAILED)
            && (progress <= IOT_OTAP_FETCH_PERCENTAGE_MAX));
}


static void ota_callback(void *pcontext, const char *msg, uint32_t msg_len)
{
    char *pvalue;
    uint32_t val_len;
    int code;
    char *tmp=(char*)msg;
    char *version = NULL;
    int err_code;

    iot_ota_type_t ota_type = {0};

#ifdef MCU_OTA_SUPPORT  
    iot_ota_notify_t ota_notify = {0};
    int event_value = 0;
    void *callback = NULL;
#endif

    ota_struct_pt h_ota = (ota_struct_pt) pcontext;

    if (h_ota->state >= IOT_OTAS_FETCHING) {
        OTA_LOG_INFO("In downloading or downloaded state");
        return;
    }
    
    *(tmp+msg_len)='\0';

    pvalue = (char*)otalib_JsonValueOf(tmp, msg_len, "method", &val_len);
    if (NULL == pvalue) {
        OTA_LOG_ERROR("invalid json doc of OTA ");
        return;
    }

    /* check whether is positive message */
    if (!((strlen("upgrade") == val_len) && (0 == strncmp(pvalue, "upgrade", val_len)))) {
        OTA_LOG_ERROR("fail state of json doc of OTA");
        return ;
    }

    /* get value of 'data' key */
    pvalue = (char*)otalib_JsonValueOf(tmp, msg_len, "params", &val_len);
    if (NULL == pvalue) {
        OTA_LOG_ERROR("Not 'params' key in json doc of OTA");
        return;
    }

    if (0 != qm_otalib_GetParams(pvalue, val_len, &h_ota->purl, &h_ota->version, h_ota->md5sum, &h_ota->size_file, &ota_type)) {
        OTA_LOG_ERROR("Get firmware parameter failed");
        return;
    }

    if(ota_type == IOT_OTA_TYPE_NONE){
        OTA_LOG_ERROR("get module type failed");
        return ;
    }

    h_ota->ota_type = ota_type;

    if (NULL == (h_ota->ch_fetch = qm_ofc_Init(h_ota->purl))) {
        OTA_LOG_ERROR("Initialize fetch module failed");
        return ;
    }

    OTA_LOG_DEBUG("version %s", h_ota->version);
    OTA_LOG_DEBUG("size_file %d", h_ota->size_file);

    if(ota_type == IOT_OTA_TYPE_DEVICE){
        version = app_version_get();
    }

#ifdef MCU_OTA_SUPPORT  
    if(ota_type == IOT_OTA_TYPE_MCU){
        version = mcu_version_get();
    }
#endif

    if(strncmp(h_ota->version, version, strlen(h_ota->version)) > 0) {

        if(ota_type == IOT_OTA_TYPE_DEVICE){
            //here upgrade init
            hal_firmware_upgrade_start();
        }
#ifdef MCU_OTA_SUPPORT      
        if(ota_type == IOT_OTA_TYPE_MCU){

            event_value = EVENT_VALUE_OTA_REQUEST;
            callback = iot_event_callback(IOT_EVENT_NOTIFY);
            if (callback) {
                ota_notify.file_size = h_ota->size_file;
                memcpy(ota_notify.md5, h_ota->md5sum, 16);
                err_code = ((int (*)(const int, const int, const char*, const int))callback)(IOT_MCU_DEVID, event_value, (char*)&ota_notify, sizeof(ota_notify)); 
                if(err_code != IOT_OTAP_SUCCESS){
                    OTA_LOG_ERROR("ota device status:%d", err_code);
                    iot_ota_reportprogress(h_ota, err_code, NULL, h_ota->ota_type);
                }        
            }
        }
#endif
        if(NULL == (h_ota->ota_buf= OTA_MALLOC( OTA_BUF_LEN ))){
            OTA_LOG_ERROR("ota buf allocate failed");
            return;
        }

        h_ota->state = IOT_OTAS_FETCHING;
    }

}


/* Initialize OTA module */
void *iot_ota_init(unsigned int product_id, const char *device_id, void *ch_signal)
{
    ota_struct_pt h_ota = NULL;

    if ((NULL == product_id) || (NULL == device_id) || (NULL == ch_signal)) {
        OTA_LOG_ERROR("one or more parameters is invalid");
        return NULL;
    }

    if (NULL == (h_ota = OTA_MALLOC(sizeof(ota_struct_t)))) {
        OTA_LOG_ERROR("allocate failed");
        return NULL;
    }

    memset(h_ota, 0, sizeof(ota_struct_t));

    h_ota->state = IOT_OTAS_UNINITED;
    h_ota->ch_signal = qm_osc_Init(product_id, device_id, ch_signal, ota_callback, h_ota);
    if (NULL == h_ota->ch_signal) {
        OTA_LOG_ERROR("initialize signal channel failed");
        goto do_exit;
    }

    h_ota->md5 = otalib_MD5Init();
    if (NULL == h_ota->md5) {
        OTA_LOG_ERROR("initialize md5 failed");
        goto do_exit;
    }
    
    h_ota->product_id = product_id;
    h_ota->device_id = device_id;
    h_ota->state = IOT_OTAS_INITED;
    return h_ota;

do_exit:

    if (NULL != h_ota->ch_signal) {
        qm_osc_Deinit(h_ota->ch_signal);
    }

    if (NULL != h_ota->md5) {
        otalib_MD5Deinit(h_ota->md5);
    }
    if(NULL !=h_ota->ota_buf)
        OTA_FREE(h_ota->ota_buf);

    if (NULL != h_ota) {
        OTA_FREE(h_ota);
    }

    return NULL;

}


/* deinitialize OTA module */
int iot_ota_deinit(void *handle)
{
    ota_struct_pt h_ota = (ota_struct_pt) handle;

    if (NULL == h_ota) {
        OTA_LOG_ERROR("handle is NULL");
        return IOT_OTAE_INVALID_PARAM;
    }

    if (IOT_OTAS_UNINITED == h_ota->state) {
        OTA_LOG_ERROR("handle is uninitialized");
        h_ota->err = IOT_OTAE_INVALID_STATE;
        return -1;
    }

    qm_osc_Deinit(h_ota->ch_signal);
    qm_ofc_Deinit(h_ota->ch_fetch);
    otalib_MD5Deinit(h_ota->md5);

    if (NULL != h_ota->purl) {
        OTA_FREE(h_ota->purl);
    }

    if (NULL != h_ota->version) {
        OTA_FREE(h_ota->version);
    }

    if(NULL !=h_ota->ota_buf)
        OTA_FREE(h_ota->ota_buf);

    OTA_FREE(h_ota);
    return 0;
}


#define OTA_VERSION_STR_LEN_MIN     (1)
#define OTA_VERSION_STR_LEN_MAX     (32)

int iot_ota_generic_reportversion(void *handle, const char *version, iot_ota_type_t ota_type)
{
#define MSG_INFORM_LEN  (128)

    int ret, len;
    char *msg_informed;
    ota_struct_pt h_ota = (ota_struct_pt) handle;

    if ((NULL == h_ota) || (NULL == version)) {
        OTA_LOG_ERROR("one or more invalid parameter");
        return IOT_OTAE_INVALID_PARAM;
    }

    len = strlen(version);
    if ((len < OTA_VERSION_STR_LEN_MIN) || (len > OTA_VERSION_STR_LEN_MAX)) {
        OTA_LOG_ERROR("version string is invalid: must be [1, 32] chars");
        h_ota->err = IOT_OTAE_INVALID_PARAM;
        return -1;
    }

    if (IOT_OTAS_UNINITED == h_ota->state) {
        OTA_LOG_ERROR("handle is uninitialized");
        h_ota->err = IOT_OTAE_INVALID_STATE;
        return -1;
    }

    if (NULL == (msg_informed = OTA_MALLOC(MSG_INFORM_LEN))) {
        OTA_LOG_ERROR("allocate for msg_informed failed");
        h_ota->err = IOT_OTAE_NOMEM;
        return -1;
    }

    ret = qm_otalib_GenInfoMsg(msg_informed, MSG_INFORM_LEN, version, ota_type);
    if (ret != 0) {
        OTA_LOG_ERROR("generate inform message failed");
        h_ota->err = ret;
        ret = -1;
        goto do_exit;
    }

    ret = qm_osc_ReportVersion(h_ota->ch_signal, msg_informed);
    if (0 != ret) {
        OTA_LOG_ERROR("Report version failed");
        h_ota->err = ret;
        ret = -1;
        goto do_exit;
    }
    ret = 0;

do_exit:
    if (NULL != msg_informed) {
        OTA_FREE(msg_informed);
    }
    return ret;

#undef MSG_INFORM_LEN
}


int iot_ota_reportprogress(void *handle, iot_ota_progress_t progress, const char *msg, iot_ota_type_t ota_type)
{
#define MSG_REPORT_LEN  (256)

    int ret = -1;
    char *msg_reported;
    ota_struct_pt h_ota = (ota_struct_pt) handle;

    if (NULL == handle) {
        OTA_LOG_ERROR("handle is NULL");
        return IOT_OTAE_INVALID_PARAM;
    }

    if (IOT_OTAS_UNINITED == h_ota->state) {
        OTA_LOG_ERROR("handle is uninitialized");
        h_ota->err = IOT_OTAE_INVALID_STATE;
        return -1;
    }

    if (!ota_check_progress(progress)) {
        OTA_LOG_ERROR("progress is a invalid parameter");
        h_ota->err = IOT_OTAE_INVALID_PARAM;
        return -1;
    }

    if (NULL == (msg_reported = OTA_MALLOC(MSG_REPORT_LEN))) {
        OTA_LOG_ERROR("allocate for msg_reported failed");
        h_ota->err = IOT_OTAE_NOMEM;
        return -1;
    }

    ret = qm_otalib_GenReportMsg(msg_reported, MSG_REPORT_LEN, h_ota->id, progress, msg, ota_type);
    if (0 != ret) {
        OTA_LOG_ERROR("generate reported message failed");
        h_ota->err = ret;
        goto do_exit;
    }

    ret = qm_osc_ReportProgress(h_ota->ch_signal, msg_reported);
    if (0 != ret) {
        OTA_LOG_ERROR("Report progress failed");
        h_ota->err = ret;
        goto do_exit;
    }

    ret = 0;

do_exit:
    if (NULL != msg_reported) {
        OTA_FREE(msg_reported);
    }
    return ret;

#undef MSG_REPORT_LEN
}


/* check whether is downloading */
int iot_ota_isfetching(void *handle)
{
    ota_struct_pt h_ota = (ota_struct_pt)handle;

    if (NULL == handle) {
        OTA_LOG_ERROR("handle is NULL");
        return 0;
    }

    if (IOT_OTAS_UNINITED == h_ota->state) {
        OTA_LOG_ERROR("handle is uninitialized");
        h_ota->err = IOT_OTAE_INVALID_STATE;
        return 0;
    }

    return (IOT_OTAS_FETCHING == h_ota->state);
}


/* check whether fetch over */
int iot_ota_isfetch_finsh(void *handle)
{
    ota_struct_pt h_ota = (ota_struct_pt) handle;

    if (NULL == handle) {
        OTA_LOG_ERROR("handle is NULL");
        return 0;
    }

    if (IOT_OTAS_UNINITED == h_ota->state) {
        OTA_LOG_ERROR("handle is uninitialized");
        h_ota->err = IOT_OTAE_INVALID_STATE;
        return 0;
    }

    return (IOT_OTAS_FETCHED == h_ota->state);
}


int iot_ota_fetchyield(void *handle, char *buf, uint32_t buf_len, uint32_t timeout_ms)
{
    int ret;
    ota_struct_pt h_ota = (ota_struct_pt) handle;

    if ((NULL == handle) || (NULL == buf) || (0 == buf_len)) {
        OTA_LOG_ERROR("invalid parameter");
        return IOT_OTAE_INVALID_PARAM;
    }

    if (IOT_OTAS_FETCHING != h_ota->state) {
        h_ota->err = IOT_OTAE_INVALID_STATE;
        return IOT_OTAE_INVALID_STATE;
    }

    ret = qm_ofc_Fetch(h_ota->ch_fetch, buf, buf_len, timeout_ms);
    if (ret < 0) {
        OTA_LOG_ERROR("Fetch firmware failed");
        h_ota->state = IOT_OTAS_FETCHED;
        h_ota->err = IOT_OTAE_FETCH_FAILED;
        return -1;
    } else if (0 == h_ota->size_fetched) {
        /* force report status in the first */
        iot_ota_reportprogress(h_ota, IOT_OTAP_FETCH_PERCENTAGE_MIN, "Enter in downloading state", h_ota->ota_type);
    }

    h_ota->size_last_fetched = ret;
    h_ota->size_fetched += ret;

    if (h_ota->size_fetched >= h_ota->size_file) {
        h_ota->state = IOT_OTAS_FETCHED;
    }

    otalib_MD5Update(h_ota->md5, buf, ret);

    return ret;
}


int iot_ota_ioctl(void *handle, iot_ota_cmdtype_t type, void *buf, size_t buf_len)
{
    ota_struct_pt h_ota = (ota_struct_pt) handle;

    if ((NULL == handle) || (NULL == buf) || (0 == buf_len)) {
        OTA_LOG_ERROR("invalid parameter");
        return IOT_OTAE_INVALID_PARAM;
    }

    if (h_ota->state < IOT_OTAS_FETCHING) {
        h_ota->err = IOT_OTAE_INVALID_STATE;
        return IOT_OTAE_INVALID_STATE;
    }

    switch (type) {
        case IOT_OTAG_FETCHED_SIZE:
            if ((4 != buf_len) || (0 != ((unsigned long)buf & 0x3))) {
                OTA_LOG_ERROR("Invalid parameter");
                h_ota->err = IOT_OTAE_INVALID_PARAM;
                return -1;
            } else {
                *((uint32_t *)buf) = h_ota->size_fetched;
                return 0;
            }

        case IOT_OTAG_FILE_SIZE:
            if ((4 != buf_len) || (0 != ((unsigned long)buf & 0x3))) {
                OTA_LOG_ERROR("Invalid parameter");
                h_ota->err = IOT_OTAE_INVALID_PARAM;
                return -1;
            } else {
                *((uint32_t *)buf) = h_ota->size_file;
                return 0;
            };

        case IOT_OTAG_VERSION:
            strncpy(buf, h_ota->version, buf_len);
            ((char *)buf)[buf_len - 1] = '\0';
            break;

        case IOT_OTAG_MD5SUM:
            strncpy(buf, h_ota->md5sum, buf_len);
            ((char *)buf)[buf_len - 1] = '\0';
            break;

        case IOT_OTAG_CHECK_FIRMWARE:
            if ((4 != buf_len) || (0 != ((unsigned long)buf & 0x3))) {
                OTA_LOG_ERROR("Invalid parameter");
                h_ota->err = IOT_OTAE_INVALID_PARAM;
                return -1;
            } else if (h_ota->state != IOT_OTAS_FETCHED) {
                h_ota->err = IOT_OTAE_INVALID_STATE;
                OTA_LOG_ERROR("Firmware can be checked in IOT_OTAS_FETCHED state only");
                return -1;
            } else {
                char md5_str[33];
                otalib_MD5Finalize(h_ota->md5, md5_str);
                OTA_LOG_DEBUG("origin=%s, now=%s", h_ota->md5sum, md5_str);
                if (0 == strcmp(h_ota->md5sum, md5_str)) {
                    *((uint32_t *)buf) = 1;
                } else {
                    *((uint32_t *)buf) = 0;
                }
                return 0;
            }

        default:
            OTA_LOG_ERROR("invalid cmd type");
            h_ota->err = IOT_OTAE_INVALID_PARAM;
            return -1;
    }

    return 0;
}


/* Get last error code */
int iot_ota_get_lasterror(void *handle)
{
    ota_struct_pt h_ota = (ota_struct_pt) handle;

    if (NULL == handle) {
        OTA_LOG_ERROR("handle is NULL");
        return  IOT_OTAE_INVALID_PARAM;
    }

    return h_ota->err;
}

#endif

