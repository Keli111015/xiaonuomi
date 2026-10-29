
#ifndef _OTA_LIB_C_
#define _OTA_LIB_C_

#include "iot_ota.h"
#include "iot_ota_internal.h"
#include "qm_utils_md5.h"
#include "json_parser.h"
#include "qm_iot_import.h"
#include "cloud_common.h"

#if CONFIG_OTA_SUPPORT

static const char *otalib_JsonValueOf(const char *json, uint32_t json_len, const char *key, uint32_t *val_len)
{
    int length;
    const char *val;
    val = qm_json_get_value_by_name((char *)json, json_len, (char *)key, &length, NULL);
    if (NULL != val) {
        *val_len = (uint32_t) length;
    }
    return val;
}

static void *otalib_MD5Init(void)
{
    qm_md5_context *ctx = OTA_MALLOC(sizeof(qm_md5_context));
    if (NULL == ctx) {
        return NULL;
    }

    qm_utils_md5_init(ctx);
    qm_utils_md5_starts(ctx);

    return ctx;
}

static void otalib_MD5Update(void *md5, const char *buf, size_t buf_len)
{
    qm_utils_md5_update(md5, (unsigned char *)buf, buf_len);
}

static void otalib_MD5Finalize(void *md5, char *output_str)
{
    int i;
    unsigned char buf_out[16];
    qm_utils_md5_finish(md5, buf_out);

    for (i = 0; i < 16; ++i) {
        output_str[i * 2] = qm_utils_hb2hex(buf_out[i] >> 4);
        output_str[i * 2 + 1] = qm_utils_hb2hex(buf_out[i]);
    }
    output_str[32] = '\0';
}

static void otalib_MD5Deinit(void *md5)
{
    if (NULL != md5) {
        OTA_FREE(md5);
    }
}

/* Get the specific @key value, and copy to @dest */
/* 0, successful; -1, failed */
static int otalib_GetFirmwareFixlenPara(const char *json_doc,
                                        size_t json_doc_len,
                                        const char *key,
                                        char *dest,
                                        size_t dest_len)
{
    const char *pvalue = NULL;
    uint32_t val_len = 0;

    if (NULL == (pvalue = otalib_JsonValueOf(json_doc, json_doc_len, key, &val_len))) {
        OTA_LOG_ERROR("Not '%s' key in json doc of OTA", key);
        return -1;
    }

    if (val_len > dest_len) {
        OTA_LOG_ERROR("value length of the key is too long");
        return -1;
    }

    memcpy(dest, pvalue, val_len);

    return 0;
}


/* Get variant length parameter of firmware, and copy to @dest */
/* 0, successful; -1, failed */
static int otalib_GetFirmwareVarlenPara(const char *json_doc,
                                        size_t json_doc_len,
                                        const char *key,
                                        char **dest)
{
    const char *pvalue;
    uint32_t val_len;

    if (NULL == (pvalue = otalib_JsonValueOf(json_doc, json_doc_len, key, &val_len))) {
        OTA_LOG_ERROR("Not %s key in json doc of OTA", key);
        return -1;
    }

    if (NULL == (*dest = OTA_MALLOC(val_len + 1))) {
        OTA_LOG_ERROR("allocate for dest failed");
        return -1;
    }

    memcpy(*dest, pvalue, val_len);
    (*dest)[val_len] = '\0';

    return 0;
}


int qm_otalib_GetParams(char *json_doc, uint32_t json_len, char **url, char **version, char *md5,
                     uint32_t *file_size, iot_ota_type_t *ota_type)
{
    #define OTA_FILESIZE_STR_LEN    (16)
    #define OTA_CODE_STR_LEN    (4)
    #define OTA_MODULE_STR_LEN  (8)
    char file_size_str[OTA_FILESIZE_STR_LEN + 1] = {0};
    char code_str[OTA_CODE_STR_LEN + 1] = {0};
    char module[OTA_MODULE_STR_LEN+1] = {0};

    /* get version */
    if (0 != otalib_GetFirmwareVarlenPara(json_doc, json_len, "version", version)) {
        OTA_LOG_ERROR("get value of version key failed");
        return -1;
    }

    /* get URL */
    if (0 != otalib_GetFirmwareVarlenPara(json_doc, json_len, "url", url)) {
        OTA_LOG_ERROR("get value of url key failed");
        return -1;
    }

    /* get md5 */
    if (0 != otalib_GetFirmwareFixlenPara(json_doc, json_len, "md5", md5, 32)) {
        OTA_LOG_ERROR("get value of md5 key failed");
        return -1;
    }

    /* get file size */
    if (0 != otalib_GetFirmwareFixlenPara(json_doc, json_len, "size", file_size_str, OTA_FILESIZE_STR_LEN)) {
        OTA_LOG_ERROR("get value of size key failed");
        return -1;
    }

    *file_size = atoi(file_size_str);

        /* get file size */
    if (0 != otalib_GetFirmwareFixlenPara(json_doc, json_len, "module", module, OTA_MODULE_STR_LEN)) {
        OTA_LOG_ERROR("get value of module key failed");
    }

    if( 0 == strlen(module)){
        *ota_type = IOT_OTA_TYPE_DEVICE;
    }else if(strlen(module) > 0 && 0 == memcmp(module, "MCU", strlen("MCU"))){
        *ota_type = IOT_OTA_TYPE_MCU;
    }else{
        *ota_type = IOT_OTA_TYPE_NONE;
    }

    return 0;

#undef OTA_FILESIZE_STR_LEN
}


/* Generate firmware information according to @id, @version */
/* and then copy to @buf. */
/* 0, successful; -1, failed */
int qm_otalib_GenInfoMsg(char *buf, size_t buf_len, const char *version, iot_ota_type_t ota_type)
{
    int ret = 0;

    if(ota_type == IOT_OTA_TYPE_DEVICE){

        ret = hal_snprintf(buf,
                        buf_len,
                        "{\"method\":\"inform\",\"id\":\"%d\",\"params\":{\"version\":\"%s\"}}",
                        hal_uptime_ms(),
                        version);
    }
#ifdef MCU_OTA_SUPPORT 
    if(ota_type == IOT_OTA_TYPE_MCU){

        ret = hal_snprintf(buf,
                buf_len,
                "{\"method\":\"inform\",\"id\":\"%d\",\"params\":{\"version\":\"%s\", \"module\":\"MCU\"}}",
                hal_uptime_ms(),
                version);
    }
#endif

    if (ret < 0) {
        OTA_LOG_ERROR("hal_snprintf failed");
        return -1;
    }

    return 0;
}


/* Generate report information according to @id, @msg */
/* and then copy to @buf. */
/* 0, successful; -1, failed */
int qm_otalib_GenReportMsg(char *buf, size_t buf_len, uint32_t id, int progress, const char *msg_detail, iot_ota_type_t ota_type)
{
    int ret = 0;
    if (NULL == msg_detail) {
        if(ota_type == IOT_OTA_TYPE_DEVICE){
            ret = hal_snprintf(buf,
                            buf_len,
                            "{\"method\":\"progress\",\"id\":\"%d\",\"params\":{\"step\":\"%d\"}}",
                            hal_uptime_ms(),
                            progress);
        }
#ifdef MCU_OTA_SUPPORT      
        if(ota_type == IOT_OTA_TYPE_MCU){
                      ret = hal_snprintf(buf,
                            buf_len,
                            "{\"method\":\"progress\",\"id\":\"%d\",\"params\":{\"step\":\"%d\",\"module\":\"MCU\"}}",
                            hal_uptime_ms(),
                            progress);
        }
#endif

    } else {
        if(ota_type == IOT_OTA_TYPE_DEVICE){
            ret = hal_snprintf(buf,
                            buf_len,
                            "{\"method\":\"progress\",\"id\":\"%d\",\"params\":{\"step\":\"%d\",\"desc\":\"%s\"}}",
                            hal_uptime_ms(),
                            progress,
                            msg_detail);
        }
#ifdef MCU_OTA_SUPPORT
        if(ota_type == IOT_OTA_TYPE_MCU){

            ret = hal_snprintf(buf,
                            buf_len,
                            "{\"method\":\"progress\",\"id\":\"%d\",\"params\":{\"step\":\"%d\",\"desc\":\"%s\",\"module\":\"MCU\"}}",
                            hal_uptime_ms(),
                            progress,
                            msg_detail);
        }
#endif
    }


    if (ret < 0) {
        OTA_LOG_ERROR("hal_snprintf failed");
        return -1;
    } else if (ret >= buf_len) {
        OTA_LOG_ERROR("msg is too long");
        return IOT_OTAE_STR_TOO_LONG;
    }

    return 0;
}

#endif

#endif /* _OTA_LIB_C_ */
