
#include "qm_config.h"
#include "qm_db_log.h"
#include "qm_errno.h"
#include "qm_string.h"

#define LOG_TAG "QM_DB_KV"

#if CONFIG_QM_KV_SUPPORT

#if CONFIG_QM_KV_SYSTEM_SUPPORT
#include "qm_kv.h"
#include "qm_hal_kv.h"
#endif

#if CONFIG_QM_KV_DB_SUPPORT
#include "qm_kernel.h"
#include "flashdb.h"
#include "qm_log.h"
#include "qm_errno.h"
#include "fal_cfg.h"

static struct fdb_kvdb* qm_kvdb = NULL;
static qm_mutex_t kvdb_lock = {0};

static void lock(fdb_db_t db)
{
    qm_mutex_lock(&kvdb_lock, QM_WAIT_FOREVER);
}

static void unlock(fdb_db_t db)
{
    qm_mutex_unlock(&kvdb_lock);
}

int qm_kv_init(void)
{
    fdb_err_t ret = FDB_NO_ERR;
    if (qm_kvdb != NULL) {
        return QM_EOK;
    }
    qm_kvdb = qm_malloc(sizeof(struct fdb_kvdb));
    if (qm_kvdb == NULL) {
        QM_DB_LOGE(LOG_TAG, "KV database allocation failed");
        return -QM_ENOMEM;
    }
    memset(qm_kvdb, 0, sizeof(struct fdb_kvdb));

        /* set the lock and unlock function if you want */
    fdb_kvdb_control(qm_kvdb, FDB_KVDB_CTRL_SET_LOCK, (void *)lock);
    fdb_kvdb_control(qm_kvdb, FDB_KVDB_CTRL_SET_UNLOCK, (void *)unlock);
        
    qm_mutex_new(&kvdb_lock);
    if(ret != QM_EOK){
        QM_DB_LOGE(LOG_TAG, "KV mutex initialization failed ret=%d", ret);
        return ret;
    }

    ret = fdb_kvdb_init(qm_kvdb, "env", FAL_KV_FLASH_NAME, NULL, NULL);
    if(ret != FDB_NO_ERR){
        QM_DB_LOGE(LOG_TAG, "KV database initialization failed ret=%d", ret);
        qm_mutex_free(&kvdb_lock);
        return -QM_EIO;
    }
    QM_DB_LOGI(LOG_TAG, QM_DB_LOG_LIFECYCLE, "KV database initialized");
    return QM_EOK;
}

int qm_kv_set(const char *key, const void *value, int len, int sync)
{
    struct fdb_blob blob = {0};
    if (qm_kvdb == NULL) {
        QM_DB_LOGE(LOG_TAG, "KV set rejected: database not initialized");
        return -QM_EINIT;
    }
    if(key == NULL || value == NULL || len == 0){
        return -QM_EINVAL;
    }
	
	blob.buf = (void*)value;
	blob.size = len;
	int ret = fdb_kv_set_blob(qm_kvdb, key, &blob);
    if(ret != FDB_NO_ERR){
        QM_DB_LOGE(LOG_TAG, "KV set failed ret=%d key_len=%lu value_len=%d",
                   ret, (unsigned long)qm_strlen(key), len);
        return -QM_EIO;
    }
	QM_DB_LOGD(LOG_TAG, QM_DB_LOG_KV, "KV set key_len=%lu value_len=%d sync=%d",
               (unsigned long)qm_strlen(key), len, sync);
	return QM_EOK;
}

int qm_kv_get(const char *key, void *buffer, int *buffer_len)
{
    int ret = 0;
    struct fdb_blob blob = {0};

    if (qm_kvdb == NULL) {
        QM_DB_LOGE(LOG_TAG, "KV get rejected: database not initialized");
        return -QM_EINIT;
    }
    if(key == NULL || buffer == NULL || buffer_len == NULL){
        return -QM_EINVAL;
    }
	blob.buf = buffer;
	blob.size = *buffer_len;
    ret = (int)fdb_kv_get_blob(qm_kvdb, key, &blob);
    *buffer_len = ret;
    if(ret == 0){
        QM_DB_LOGE(LOG_TAG, "KV get failed key_len=%lu", (unsigned long)qm_strlen(key));
        return -QM_EIO;
    }
	QM_DB_LOGD(LOG_TAG, QM_DB_LOG_KV, "KV get key_len=%lu value_len=%d",
               (unsigned long)qm_strlen(key), ret);
	return QM_EOK;
}

int qm_kv_del(const char *key)
{
    if (qm_kvdb == NULL) {
        QM_DB_LOGE(LOG_TAG, "KV delete rejected: database not initialized");
        return -QM_EINIT;
    }
    if(key == NULL){
        return -QM_EINVAL;
    }
	{
        int ret = fdb_kv_del(qm_kvdb, key);
        QM_DB_LOGD(LOG_TAG, QM_DB_LOG_KV, "KV delete key_len=%lu ret=%d",
                   (unsigned long)qm_strlen(key), ret);
        return ret;
	}
}

int qm_kv_clean(void)
{
    fdb_err_t ret = FDB_NO_ERR;
    if (qm_kvdb == NULL) {
        QM_DB_LOGE(LOG_TAG, "KV clean rejected: database not initialized");
        return -QM_EINIT;
    }
    ret = fdb_kv_set_default(qm_kvdb);
    if(ret != FDB_NO_ERR){
        QM_DB_LOGE(LOG_TAG, "KV clean failed ret=%d", ret);
        return -QM_EIO;
    }
    QM_DB_LOGI(LOG_TAG, QM_DB_LOG_CONTROL, "KV database cleaned");
    return QM_EOK;
}

#endif

#if CONFIG_QM_KV_SYSTEM_SUPPORT == 1

int qm_kv_init(void)
{
    int ret = qm_hal_kv_init();
    QM_DB_LOGI(LOG_TAG, QM_DB_LOG_LIFECYCLE, "system KV init ret=%d", ret);
    return ret;
}

int qm_kv_set(const char *key, const void *value, int len, int sync)
{
    int ret = qm_hal_kv_set(key, value, len, sync);
    if (ret != QM_EOK) {
        QM_DB_LOGE(LOG_TAG, "system KV set failed ret=%d key=%s key_len=%lu value_len=%d",
                   ret, key ? key : "(null)", key ? (unsigned long)qm_strlen(key) : 0U, len);
    }
    return ret;
}

int qm_kv_get(const char *key, void *buffer, int *buffer_len)
{
    int ret = qm_hal_kv_get(key, buffer, buffer_len);
    if (ret != QM_EOK) {
        QM_DB_LOGE(LOG_TAG, "system KV get failed ret=%d key=%s key_len=%lu",
                   ret, key ? key : "(null)", key ? (unsigned long)qm_strlen(key) : 0U);
    }
    return ret;
}

int qm_kv_del(const char *key)
{
    int ret = qm_hal_kv_del(key);
    QM_DB_LOGD(LOG_TAG, QM_DB_LOG_KV, "system KV delete key_len=%lu ret=%d",
               key ? (unsigned long)qm_strlen(key) : 0U, ret);
    return ret;
}

int qm_kv_clean(void)
{
    int ret = qm_hal_kv_clean();
    QM_DB_LOGI(LOG_TAG, QM_DB_LOG_CONTROL, "system KV clean ret=%d", ret);
    return ret;
}

#endif

#endif
