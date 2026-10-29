
#include "iot_ota.h"
#include "iot_ota_fetch.h"
#include "qm_log.h"
#include "qm_errno.h"
#include "qm_kernel.h"
#include "qm_ota.h"
#include "qm_utils_md5.h"

typedef struct  {
    
    iot_ota_state_t state;              /* OTA state */
    uint32_t size_last_fetched;         /* size of last downloaded */
    uint32_t size_fetched;              /* size of already downloaded */
    uint32_t size_file;                 /* size of file */

    char md5sum[OTA_MD5_MAX_LEN + 1];  /* MD5 string */
    void *md5_handle;                  /* MD5 handle */
    char *ota_buf;

    void *arg;
    ota_event_fn ota_event;
    void *fetch_handle;
    int exist;
    qm_task_t task;

} iot_ota_ctx_t;

static iot_ota_ctx_t *g_iot_ota_ctx = NULL;


static void *otalib_MD5Init(void)
{
    qm_md5_context *ctx = qm_malloc(sizeof(qm_md5_context));
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
    int i = 0;
    uint8_t buf_out[16];
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
        qm_free(md5);
    }
}

static int iot_ota_ioctl(void *handle, iot_ota_cmd_t type, void *buf, uint32_t buf_len)
{
    iot_ota_ctx_t *ota_ctx = (iot_ota_ctx_t*) handle;

    if ((NULL == handle) || (NULL == buf) || (0 == buf_len)) {
        OTA_LOG_ERROR("invalid parameter");
        return -QM_EINVAL;
    }

    if (ota_ctx->state < IOT_OTA_STATE_FETCHING) {
        return -QM_EINVAL;
    }

    switch (type) {
        case IOT_OTA_CMD_FETCHED_SIZE:
            if (4 != buf_len) {
                OTA_LOG_ERROR("Invalid parameter");
                return -QM_EINVAL;
            } else {
                *((uint32_t *)buf) = ota_ctx->size_fetched;
                return 0;
            }

        case IOT_OTA_CMD_FILE_SIZE:
            if (4 != buf_len) {
                OTA_LOG_ERROR("Invalid parameter");
                return -QM_EINVAL;
            } else {
                *((uint32_t *)buf) = ota_ctx->size_file;
                return 0;
            };

        case IOT_OTA_CMD_MD5SUM:
            strncpy(buf, ota_ctx->md5sum, buf_len);
            ((char *)buf)[buf_len - 1] = '\0';
            break;

        case IOT_OTA_CMD_CHECK_FIRMWARE:
            if (4 != buf_len) {
                OTA_LOG_ERROR("Invalid parameter");
                return -QM_EINVAL;
            } else if (ota_ctx->state != IOT_OTA_STATE_FETCHED) {
                OTA_LOG_ERROR("Firmware can be checked in IOT_OTAS_FETCHED state only");
                return -QM_EINVAL;
            } else {
                char md5_str[33] = {0};
                otalib_MD5Finalize(ota_ctx->md5_handle, md5_str);
                OTA_LOG_DEBUG("origin=%s, now=%s", ota_ctx->md5sum, md5_str);
                if (0 == strcmp(ota_ctx->md5sum, md5_str)) {
                    *((uint32_t *)buf) = 1;
                } else {
                    *((uint32_t *)buf) = 0;
                }
                return 0;
            }

        default:
            OTA_LOG_ERROR("invalid cmd type");
            return -QM_EINVAL;
    }
    return QM_EOK;
}

/* check whether fetch over */
static int iot_ota_isfetch_finsh(void *handle)
{
    iot_ota_ctx_t *ota_ctx = (iot_ota_ctx_t*) handle;

    if (NULL == handle) {
        OTA_LOG_ERROR("handle is NULL");
        return 0;
    }

    if (IOT_OTA_STATE_UNINITED == ota_ctx->state) {
        OTA_LOG_ERROR("handle is uninitialized");
        return 0;
    }

    return (IOT_OTA_STATE_FETCHED == ota_ctx->state);
}

/* check whether is downloading */
static int iot_ota_isfetching(void *handle)
{
    iot_ota_ctx_t *ota_ctx = (iot_ota_ctx_t*) handle;

    if (NULL == handle) {
        OTA_LOG_ERROR("handle is NULL");
        return 0;
    }

    if (IOT_OTA_STATE_UNINITED == ota_ctx->state) {
        OTA_LOG_ERROR("handle is uninitialized");
        return 0;
    }

    return (IOT_OTA_STATE_FETCHING == ota_ctx->state);
}

static int iot_ota_fetchyield(void *handle, char *buf, uint32_t buf_len, uint32_t timeout)
{
    int ret = 0;
    int step = 0;
    iot_ota_ctx_t *ota_ctx = (iot_ota_ctx_t*) handle;

    if ((NULL == handle) || (NULL == buf) || (0 == buf_len)) {
        OTA_LOG_ERROR("invalid parameter");
        return -QM_EINVAL;
    }

    if (IOT_OTA_STATE_FETCHING != ota_ctx->state) {
        return -QM_EINVAL;
    }

    ret = ota_fetch(ota_ctx->fetch_handle, buf, buf_len, timeout);
    if (ret < 0) {
        OTA_LOG_ERROR("Fetch firmware failed");
        ota_ctx->state = IOT_OTA_STATE_FETCHED;
        return -QM_EIO;
    } else if (0 == ota_ctx->size_fetched) {
        /* force report status in the first */
        step = (int)IOT_OTA_PROGRESS_FETCH_PERCENTAGE_MIN;
        if(ota_ctx->ota_event){
            ota_ctx->ota_event(IOT_OTA_EVENT_PROGRESS, (void*)step);
        }
    }

    ota_ctx->size_last_fetched = ret;
    ota_ctx->size_fetched += ret;

    if (ota_ctx->size_fetched >= ota_ctx->size_file) {
        ota_ctx->state = IOT_OTA_STATE_FETCHED;
    }
    otalib_MD5Update(ota_ctx->md5_handle, buf, ret);

    return ret;
}

static void ota_task(void *arg)
{
    int len = 0;
    int step = 0;
    qm_err_t ret = QM_EOK;
    qm_task_t task = {0};
    int last_percent = 0;
    int percent = 0;
    int size_downloaded = 0, size_file = 0;
    uint32_t firmware_valid = 0;
    
    iot_ota_ctx_t *ota_ctx = (iot_ota_ctx_t*)arg;

    qm_ota_start(NULL);

    while(!ota_ctx->exist){

        if(iot_ota_isfetching(ota_ctx)){

            len = iot_ota_fetchyield(ota_ctx, ota_ctx->ota_buf, CONFIG_OTA_BUF_LEN, 10000);
            if (len > 0) {
                ret = qm_ota_write(NULL, (uint8_t*)ota_ctx->ota_buf, len);
                if(ret != QM_EOK){
                    step = (int)IOT_OTA_PROGRESS_BURN_FAILED;
                    if(ota_ctx->ota_event){
                        ota_ctx->ota_event(IOT_OTA_EVENT_PROGRESS, (void*)step);
                    }
                }
            } else {
                step = (int)IOT_OTA_PROGRESS_FETCH_FAILED;
                if(ota_ctx->ota_event){
                    ota_ctx->ota_event(IOT_OTA_EVENT_PROGRESS, (void*)step);
                }
                OTA_LOG_ERROR("ota fetch fail");
                goto __exit;
            }

            /* get OTA information */
            iot_ota_ioctl(ota_ctx, IOT_OTA_CMD_FETCHED_SIZE, &size_downloaded, 4);
            iot_ota_ioctl(ota_ctx, IOT_OTA_CMD_FILE_SIZE, &size_file, 4);

            percent = (size_downloaded * 100) / size_file;
            if(percent % 10 == 0 && last_percent != percent)
            {
                last_percent = percent;
                if(ota_ctx->ota_event){
                    ota_ctx->ota_event(IOT_OTA_EVENT_PROGRESS, (void*)percent);
                }
            }
        }
        else if(iot_ota_isfetch_finsh(ota_ctx))
        {
            iot_ota_ioctl(ota_ctx, IOT_OTA_CMD_CHECK_FIRMWARE, &firmware_valid, 4);
            if (0 == firmware_valid) {
                OTA_LOG_DEBUG("The firmware is invalid");
                step = (int)IOT_OTA_PROGRESS_CHECK_FALIED;
                if(ota_ctx->ota_event){
                    ota_ctx->ota_event(IOT_OTA_EVENT_PROGRESS, (void*)step);
                    ota_ctx->ota_event(IOT_OTA_EVENT_FAIL, NULL);
                }
            } else {
                OTA_LOG_DEBUG("The firmware is valid");
                if(ota_ctx->ota_event){
                    ota_ctx->ota_event(IOT_OTA_EVENT_SUCCESS, NULL);
                }

                qm_ota_end(NULL);
            }   
            goto __exit;
        }
    }

__exit:
    memcpy(&task, &ota_ctx->task, sizeof(task));
    ota_fetch_deinit(ota_ctx->fetch_handle);
    otalib_MD5Deinit(ota_ctx->md5_handle);
    qm_free(ota_ctx->ota_buf);
    qm_free(ota_ctx);
    g_iot_ota_ctx = NULL;
    qm_task_exit(&task);
}

void *iot_ota_init(iot_ota_param_t *param)
{
    if(g_iot_ota_ctx){
        return g_iot_ota_ctx;
    }
    g_iot_ota_ctx = (iot_ota_ctx_t*)qm_malloc(sizeof(iot_ota_ctx_t));
    if(g_iot_ota_ctx == NULL){
        return NULL;
    }

    memset(g_iot_ota_ctx, 0 ,sizeof(iot_ota_ctx_t));
    g_iot_ota_ctx->arg = param->arg;
    g_iot_ota_ctx->ota_event = param->event;

    return g_iot_ota_ctx;
}

int iot_ota_start(void *handle, iot_ota_upgrade_info_t *upgrade_info)
{
    qm_err_t ret = QM_EOK;
    iot_ota_ctx_t *ota_ctx = (iot_ota_ctx_t*)handle;
    if(handle == NULL || upgrade_info == NULL){
        return -QM_EINVAL;
    }
    
    if(ota_ctx->fetch_handle){
        return QM_EOK;
    }

    memcpy(ota_ctx->md5sum, upgrade_info->md5str, OTA_MD5_MAX_LEN);
    ota_ctx->size_file = upgrade_info->file_size;

    ota_ctx->fetch_handle = ota_fetch_init(upgrade_info->url, upgrade_info->port);
    if(ota_ctx->fetch_handle == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }

    if(NULL == (ota_ctx->ota_buf = qm_malloc( CONFIG_OTA_BUF_LEN ))){
        OTA_LOG_ERROR("ota buf allocate failed");
        ret = -QM_ENOMEM;
        goto __exit;
    }

    
    ota_ctx->md5_handle = otalib_MD5Init();
    if (NULL == ota_ctx->md5_handle) {
        OTA_LOG_ERROR("initialize md5 failed");
        goto __exit;
    }

    ret = qm_task_new(&ota_ctx->task, "ota", ota_task, ota_ctx, CONFIG_OTA_TASK_SIZE, CONFIG_OTA_TASK_PRIO);
    if(ret != QM_EOK){
        goto __exit;
    }

    ota_ctx->state = IOT_OTA_STATE_FETCHING;
    return QM_EOK;

__exit:

    if(ota_ctx->md5_handle){
        otalib_MD5Deinit(ota_ctx->md5_handle);
    }
    if(ota_ctx->fetch_handle){
        ota_fetch_deinit(ota_ctx->fetch_handle);
    }
    if(ota_ctx->ota_buf){
        qm_free(ota_ctx->ota_buf);
    }
    return ret;
}

int iot_ota_deinit(void *handle)
{
    iot_ota_ctx_t *ota_ctx = (iot_ota_ctx_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }

    if(ota_ctx->task.hdl){
        ota_ctx->exist = 1;   
    }else{
        qm_free(ota_ctx);
    }
    return QM_EOK;
}
