/*
 * qm_hal_kv.c - QUMI Key-Value Storage HAL for JieLi (WL82/AC79) Platform
 *
 * Implementation mirrors rtl8762e_vela/qm_db/qm_hal_kv.c:
 *   - Persist a key→record_id index table in VM
 *   - Store each value under a dedicated user syscfg ID
 *
 * JieLi syscfg ID allocation (syscfg_id.h):
 *   [  1 ~  59 ]: User-definable, VM only  ← used here
 *   [ 60 ~  99 ]: SDK reserved, VM only
 *   [100 ~ 255 ]: SDK reserved, VM/BTIF/bin
 *
 * Layout:
 *   ID 1              : KV index table (g_kv_store[])
 *   ID 1 + record_id  : value payload for that key (record_id = 1..KV_STORE_MAX_NUM)
 */


#include "generic/typedef.h"
#include "syscfg_id.h"

#include <string.h>


#include "qm.h"
#include "qm_log.h"
#include "qm_hal_kv.h"

#ifndef CONFIG_QM_HAL_KV_NAME_BUFFER_LEN
#define CONFIG_QM_HAL_KV_NAME_BUFFER_LEN 16
#endif

#ifndef CONFIG_QM_HAL_KV_STORE_MAX_NUM
/* ID1=index, ID2..ID(1+N)=values; keep N within user range 1..59 */
#define CONFIG_QM_HAL_KV_STORE_MAX_NUM 50
#endif

#define LOG_TAG "kv hal"

#define DIR_KV_INDEX_ID           1
#define KV_STORE_MAX_NUM          CONFIG_QM_HAL_KV_STORE_MAX_NUM
#define KV_VALUE_ID(record_id)    ((u16)(DIR_KV_INDEX_ID + (record_id)))

#if (DIR_KV_INDEX_ID + KV_STORE_MAX_NUM) > CFG_USER_DEFINE_END
#error "CONFIG_QM_HAL_KV_STORE_MAX_NUM exceeds JieLi user syscfg ID range"
#endif

typedef struct {
    uint8_t buffer[CONFIG_QM_HAL_KV_NAME_BUFFER_LEN];
    uint16_t record_len;
} qm_hal_kv_store_t;

static int s_need_load = 1;
static qm_hal_kv_store_t g_kv_store[KV_STORE_MAX_NUM];

static uint16_t kv_store_record_id(const qm_hal_kv_store_t *entry)
{
    return (uint16_t)(entry - g_kv_store + 1);
}

static int kv_index_save(void)
{
    int ret = syscfg_write(DIR_KV_INDEX_ID, g_kv_store, (u16)sizeof(g_kv_store));
    if (ret != (int)sizeof(g_kv_store)) {
        QM_LOGW(LOG_TAG, "INDEX WRITE %d", ret);
        return -QM_ERROR;
    }
    return QM_EOK;
}

static int kv_index_ensure_loaded(void)
{
    int ret;
    int i;

    if (!s_need_load) {
        return QM_EOK;
    }

    memset(g_kv_store, 0, sizeof(g_kv_store));
    ret = syscfg_read(DIR_KV_INDEX_ID, g_kv_store, (u16)sizeof(g_kv_store));
    if (ret != (int)sizeof(g_kv_store)) {
        QM_LOGW(LOG_TAG, "INDEX READ %d", ret);
        memset(g_kv_store, 0, sizeof(g_kv_store));
    } else {
        for (i = 0; i < KV_STORE_MAX_NUM; i++) {
            qm_hal_kv_store_t *entry = &g_kv_store[i];
            if (entry->buffer[0] == '\0') {
                continue;
            }
            entry->buffer[CONFIG_QM_HAL_KV_NAME_BUFFER_LEN - 1] = '\0';
        }
    }

    s_need_load = 0;
    return QM_EOK;
}

static int kv_key_equal(const qm_hal_kv_store_t *entry, const char *key)
{
    size_t key_len;

    if (entry == NULL || key == NULL) {
        return 0;
    }

    key_len = strlen(key);
    if (key_len == 0 || key_len >= CONFIG_QM_HAL_KV_NAME_BUFFER_LEN) {
        return 0;
    }

    return (strncmp((const char *)entry->buffer, key, CONFIG_QM_HAL_KV_NAME_BUFFER_LEN) == 0);
}

static qm_hal_kv_store_t *kv_store_index_read(const char *key)
{
    int index;

    for (index = 0; index < KV_STORE_MAX_NUM; index++) {
        if (g_kv_store[index].buffer[0] == '\0') {
            continue;
        }
        if (kv_key_equal(&g_kv_store[index], key)) {
            return &g_kv_store[index];
        }
    }
    return NULL;
}

static qm_hal_kv_store_t *kv_store_index_add(const char *key)
{
    int index;
    size_t key_len;
    qm_hal_kv_store_t *kv_store_handle = NULL;

    key_len = strlen(key);
    if (key_len == 0 || key_len >= CONFIG_QM_HAL_KV_NAME_BUFFER_LEN) {
        return NULL;
    }

    for (index = 0; index < KV_STORE_MAX_NUM; index++) {
        if (g_kv_store[index].buffer[0] != '\0') {
            continue;
        }
        kv_store_handle = &g_kv_store[index];
        memset(kv_store_handle, 0, sizeof(*kv_store_handle));
        memcpy(kv_store_handle->buffer, key, key_len);
        break;
    }
    return kv_store_handle;
}

static qm_hal_kv_store_t *kv_store_index_write(const char *key, uint32_t record_len)
{
    qm_hal_kv_store_t *kv_store_handle = kv_store_index_read(key);

    if (kv_store_handle == NULL) {
        return NULL;
    }

    if (record_len > 0xFFFFu) {
        return NULL;
    }

    kv_store_handle->record_len = (uint16_t)record_len;
    if (kv_index_save() != QM_EOK) {
        return NULL;
    }
    return kv_store_handle;
}

static qm_hal_kv_store_t *kv_store_index_del(const char *key)
{
    qm_hal_kv_store_t *kv_store_handle = kv_store_index_read(key);

    if (kv_store_handle == NULL) {
        return NULL;
    }

    memset(kv_store_handle, 0, sizeof(*kv_store_handle));
    if (kv_index_save() != QM_EOK) {
        return NULL;
    }
    return kv_store_handle;
}

int qm_hal_kv_init(void)
{
    s_need_load = 1;
    memset(g_kv_store, 0, sizeof(g_kv_store));
    return QM_EOK;
}

int qm_hal_kv_get(const char *key, void *buffer, int *buffer_len)
{
    int ret;
    qm_hal_kv_store_t *kv_store_handle = NULL;

    QM_RETURN_ON_FALSE(key, -QM_EINVAL, LOG_TAG, "key invail");
    QM_RETURN_ON_FALSE(buffer_len, -QM_EINVAL, LOG_TAG, "buffer_len invail");
    QM_RETURN_ON_FALSE(*buffer_len > 0, -QM_EINVAL, LOG_TAG, "buffer_len == 0");

    kv_index_ensure_loaded();

    kv_store_handle = kv_store_index_read(key);
    if (kv_store_handle == NULL) {
        return -QM_ENOENT;
    }

    if (buffer == NULL) {
        *buffer_len = (int)kv_store_handle->record_len;
        return QM_EOK;
    }

    if (*buffer_len < (int)kv_store_handle->record_len) {
        *buffer_len = (int)kv_store_handle->record_len;
        return -QM_EINVAL;
    }

    if (kv_store_handle->record_len == 0) {
        *buffer_len = 0;
        return QM_EOK;
    }

    ret = syscfg_read(KV_VALUE_ID(kv_store_record_id(kv_store_handle)),
                      buffer,
                      kv_store_handle->record_len);
    if (ret != (int)kv_store_handle->record_len) {
        QM_LOGW(LOG_TAG, "RECORD READ id=%u ret=%d",
                (unsigned int)kv_store_record_id(kv_store_handle), ret);
        return -QM_ERROR;
    }

    *buffer_len = (int)kv_store_handle->record_len;
    return QM_EOK;
}

int qm_hal_kv_set(const char *key, const void *value, int len, int sync)
{
    int ret;
    qm_hal_kv_store_t *kv_store_handle = NULL;

    (void)sync;

    QM_RETURN_ON_FALSE(key, -QM_EINVAL, LOG_TAG, "key invail");
    QM_RETURN_ON_FALSE(value, -QM_EINVAL, LOG_TAG, "value invail");
    QM_RETURN_ON_FALSE(len > 0, -QM_EINVAL, LOG_TAG, "len == 0");
    QM_RETURN_ON_FALSE(len <= 0xFFFF, -QM_EINVAL, LOG_TAG, "len too large");

    kv_index_ensure_loaded();

    kv_store_handle = kv_store_index_read(key);
    if (kv_store_handle == NULL) {
        kv_store_handle = kv_store_index_add(key);
        if (kv_store_handle == NULL) {
            QM_LOGE(LOG_TAG, "kv_store_handle null");
            return -QM_EFULL;
        }
    }

    ret = syscfg_write(KV_VALUE_ID(kv_store_record_id(kv_store_handle)),
                       (void *)value,
                       (u16)len);
    if (ret != len) {
        QM_LOGE(LOG_TAG, "syscfg_write err id=%u ret=%d len=%d",
                (unsigned int)kv_store_record_id(kv_store_handle), ret, len);
        return -QM_ERROR;
    }

    if (kv_store_index_write(key, (uint32_t)len) == NULL) {
        return -QM_ERROR;
    }

    return QM_EOK;
}

int qm_hal_kv_del(const char *key)
{
    qm_hal_kv_store_t *kv_store_handle = NULL;
    uint16_t record_id;
    uint8_t stub = 0;

    QM_RETURN_ON_FALSE(key, -QM_EINVAL, LOG_TAG, "key invail");

    kv_index_ensure_loaded();

    kv_store_handle = kv_store_index_read(key);
    if (kv_store_handle == NULL) {
        return QM_EOK;
    }

    record_id = kv_store_record_id(kv_store_handle);
    if (kv_store_index_del(key) == NULL) {
        return -QM_ERROR;
    }

    /* JieLi has no syscfg_delete; overwrite with a stub to mark stale data. */
    (void)syscfg_write(KV_VALUE_ID(record_id), &stub, sizeof(stub));
    return QM_EOK;
}

int qm_hal_kv_clean(void)
{
    int index;
    uint8_t stub = 0;

    kv_index_ensure_loaded();

    for (index = 0; index < KV_STORE_MAX_NUM; index++) {
        if (g_kv_store[index].buffer[0] == '\0') {
            continue;
        }
        (void)syscfg_write(KV_VALUE_ID((uint16_t)(index + 1)),
                           &stub,
                           sizeof(stub));
    }

    memset(g_kv_store, 0, sizeof(g_kv_store));
    if (kv_index_save() != QM_EOK) {
        return -QM_ERROR;
    }

    s_need_load = 0;
    return QM_EOK;
}
