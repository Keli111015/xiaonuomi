#include "qm_config.h"
#include "qm_db_log.h"
#include "qm_string.h"

#define LOG_TAG "QM_DB_TS"

#if CONFIG_QM_TS_SUPPORT
#include "qm_ts.h"
#include "qm_kernel.h"
#include "flashdb.h"

typedef struct{
    qm_mutex_t lock;
    struct fdb_tsdb tsdb;
}qm_ts_ctx_t;

static void lock(fdb_db_t db)
{
    qm_ts_ctx_t *ts_ctx = (qm_ts_ctx_t*)db->user_data;
    qm_mutex_lock(&ts_ctx->lock, QM_WAIT_FOREVER);
}

static void unlock(fdb_db_t db)
{
    qm_ts_ctx_t *ts_ctx = (qm_ts_ctx_t*)db->user_data;
    qm_mutex_unlock(&ts_ctx->lock);
}

void *qm_ts_init(const char *name, qm_ts_get_time get_time, uint32_t max_len)
{
    qm_err_t ret = QM_EOK;

    qm_ts_ctx_t *ts_ctx = (qm_ts_ctx_t*)qm_malloc(sizeof(qm_ts_ctx_t));
    if(ts_ctx == NULL){
        QM_DB_LOGE(LOG_TAG, "TS database allocation failed");
        return NULL;
    }

    memset(ts_ctx, 0, sizeof(qm_ts_ctx_t));

    ret = qm_mutex_new(&ts_ctx->lock);
    if(ret != QM_EOK){
        QM_DB_LOGE(LOG_TAG, "TS mutex initialization failed ret=%d", ret);
        qm_free(ts_ctx);
        return NULL;
    }
    
    /* set the lock and unlock function if you want */
    fdb_tsdb_control(&ts_ctx->tsdb, FDB_TSDB_CTRL_SET_LOCK, (void *)lock);
    fdb_tsdb_control(&ts_ctx->tsdb, FDB_TSDB_CTRL_SET_UNLOCK, (void *)unlock);
       
    ret = fdb_tsdb_init(&ts_ctx->tsdb, "ts", name, get_time, max_len, (void*)ts_ctx);
    if(ret != QM_EOK){
        QM_DB_LOGE(LOG_TAG, "TS database initialization failed ret=%d", ret);
        qm_mutex_free(&ts_ctx->lock);
        qm_free(ts_ctx);
        return NULL;
    }
    QM_DB_LOGI(LOG_TAG, QM_DB_LOG_LIFECYCLE, "TS database initialized name_len=%lu max_len=%lu",
               name ? (unsigned long)qm_strlen(name) : 0U, (unsigned long)max_len);
    return (void*)ts_ctx;
}

int qm_ts_clean(void *db)
{
    qm_ts_ctx_t *ts_ctx = (qm_ts_ctx_t*)db; 
    if(db == NULL){
        return -QM_EINVAL;
    }
    fdb_tsl_clean(&ts_ctx->tsdb);
    QM_DB_LOGI(LOG_TAG, QM_DB_LOG_CONTROL, "TS database cleaned");
    return QM_EOK;
}

int qm_ts_append(void *db, const void *buf, uint32_t len)
{
    struct fdb_blob blob = {0};
    fdb_err_t result = FDB_NO_ERR;
    qm_ts_ctx_t *ts_ctx = (qm_ts_ctx_t*)db; 
    if(db == NULL){
        return -QM_EINVAL;
    }
    result = fdb_tsl_append(&ts_ctx->tsdb, fdb_blob_make(&blob, buf, len));
    if(result != FDB_NO_ERR){
        QM_DB_LOGE(LOG_TAG, "TS append failed ret=%d len=%lu", result, (unsigned long)len);
        return -QM_EIO;
    }
    QM_DB_LOGD(LOG_TAG, QM_DB_LOG_TS, "TS append len=%lu", (unsigned long)len);
    return QM_EOK;
}

int qm_ts_read(void *db, qm_ts_t *ts, void *buf, uint32_t len)
{
    uint32_t read_len = 0;
    struct fdb_tsl tsl = {0};
    struct fdb_blob blob = {0};
    qm_ts_ctx_t *ts_ctx = (qm_ts_ctx_t*)db; 
    if(db == NULL){
        return -QM_EINVAL;
    }
    
    tsl.addr.log = ts->addr;
    tsl.addr.index = ts->index;
    tsl.log_len = ts->len;
    tsl.time = ts->time;
    tsl.status = (fdb_tsl_status_t)ts->status;

    read_len = fdb_blob_read((fdb_db_t)&ts_ctx->tsdb, fdb_tsl_to_blob(&tsl, fdb_blob_make(&blob, buf, len)));
    if(read_len == 0){
        QM_DB_LOGE(LOG_TAG, "TS read failed addr=%lu index=%lu len=%lu",
                   (unsigned long)ts->addr, (unsigned long)ts->index, (unsigned long)len);
        return -QM_EIO;
    }
    QM_DB_LOGD(LOG_TAG, QM_DB_LOG_TS, "TS read addr=%lu index=%lu len=%lu",
               (unsigned long)ts->addr, (unsigned long)ts->index, (unsigned long)read_len);
    return (int)read_len;
}

typedef struct {
    qm_ts_cb cb;
    void *arg;
}ts_cb_t;

static bool ts_callback(fdb_tsl_t tsl, void *arg)
{
    ts_cb_t *ts_cb = (ts_cb_t*)arg;
    qm_ts_t db_ts = {0};

    db_ts.addr = tsl->addr.log;
    db_ts.index = tsl->addr.index;
    db_ts.time = tsl->time;
    db_ts.len = tsl->log_len;
    db_ts.status = (qm_ts_status_t)tsl->status;

    return ts_cb->cb(&db_ts, ts_cb->arg);
}

int qm_ts_iter(void *db, qm_ts_cb cb, void *arg)
{
    ts_cb_t ts_cb = {0};
    qm_ts_ctx_t *ts_ctx = (qm_ts_ctx_t*)db; 
    if(db == NULL || cb == NULL){
        return -QM_EINVAL;
    }

    ts_cb.arg = arg;
    ts_cb.cb = cb;

    fdb_tsl_iter(&ts_ctx->tsdb, ts_callback, &ts_cb);

    return QM_EOK;
}

int qm_ts_iter_reverse(void *db, qm_ts_cb cb, void *arg)
{
    ts_cb_t ts_cb = {0};
    qm_ts_ctx_t *ts_ctx = (qm_ts_ctx_t*)db; 
    if(db == NULL || cb == NULL){
        return -QM_EINVAL;
    }

    ts_cb.arg = arg;
    ts_cb.cb = cb;

    fdb_tsl_iter_reverse(&ts_ctx->tsdb, ts_callback, &ts_cb);

    return QM_EOK;    
}

int qm_ts_iter_by_status(void *db, qm_ts_status_t status, qm_ts_cb cb, void *arg)
{
    ts_cb_t ts_cb = {0};
    qm_ts_ctx_t *ts_ctx = (qm_ts_ctx_t*)db; 
    if(db == NULL || cb == NULL){
        return -QM_EINVAL;
    }

    ts_cb.arg = arg;
    ts_cb.cb = cb;

    fdb_tsl_iter_by_status(&ts_ctx->tsdb, (fdb_tsl_status_t)status, ts_callback, &ts_cb);

    return QM_EOK;    
}

int qm_ts_iter_reverse_by_status(void *db, qm_ts_status_t status, qm_ts_cb cb, void *arg)
{
    ts_cb_t ts_cb = {0};
    qm_ts_ctx_t *ts_ctx = (qm_ts_ctx_t*)db; 
    if(db == NULL || cb == NULL){
        return -QM_EINVAL;
    }

    ts_cb.arg = arg;
    ts_cb.cb = cb;

    fdb_tsl_iter_reverse_by_status(&ts_ctx->tsdb, (fdb_tsl_status_t)status, ts_callback, &ts_cb);

    return QM_EOK; 
}

int qm_ts_iter_by_time(void *db, uint32_t from, uint32_t to, qm_ts_cb cb, void *arg)
{
    ts_cb_t ts_cb = {0};
    qm_ts_ctx_t *ts_ctx = (qm_ts_ctx_t*)db; 
    if(db == NULL || cb == NULL){
        return -QM_EINVAL;
    }

    ts_cb.arg = arg;
    ts_cb.cb = cb;

    fdb_tsl_iter_by_time(&ts_ctx->tsdb, (fdb_time_t)from, (fdb_time_t)to, ts_callback, &ts_cb);

    return QM_EOK; 
}

int qm_ts_iter_by_time_and_status(void *db, uint32_t from, uint32_t to, qm_ts_status_t status, qm_ts_cb cb, void *arg)
{
    ts_cb_t ts_cb = {0};
    qm_ts_ctx_t *ts_ctx = (qm_ts_ctx_t*)db; 
    if(db == NULL || cb == NULL){
        return -QM_EINVAL;
    }

    ts_cb.arg = arg;
    ts_cb.cb = cb;

    fdb_tsl_iter_by_time_and_status(&ts_ctx->tsdb, (fdb_time_t)from, (fdb_time_t)to, (fdb_tsl_status_t)status, ts_callback, &ts_cb);

    return QM_EOK; 
}

int qm_ts_query_count(void *db, uint32_t from, uint32_t to, qm_ts_status_t status)
{
    qm_ts_ctx_t *ts_ctx = (qm_ts_ctx_t*)db; 
    if(db == NULL){
        return -QM_EINVAL;
    }
    
    return (int)fdb_tsl_query_count(&ts_ctx->tsdb, (fdb_time_t)from, (fdb_time_t)to, (fdb_tsl_status_t)status);
}

int qm_ts_set_status(void *db, qm_ts_t *ts, qm_ts_status_t status)
{
    struct fdb_tsl tsl = {0};
    fdb_err_t result = FDB_NO_ERR;

    qm_ts_ctx_t *ts_ctx = (qm_ts_ctx_t*)db; 
    if(db == NULL){
        return -QM_EINVAL;
    }
    tsl.addr.index = ts->index;
    result = fdb_tsl_set_status(&ts_ctx->tsdb, &tsl, (fdb_tsl_status_t)status);
    if(result != FDB_NO_ERR){
        QM_DB_LOGE(LOG_TAG, "TS status update failed ret=%d addr=%lu index=%lu status=%d",
                   result, (unsigned long)ts->addr, (unsigned long)ts->index, (int)status);
        return -QM_EIO;
    }
    QM_DB_LOGD(LOG_TAG, QM_DB_LOG_TS, "TS status updated addr=%lu index=%lu status=%d",
               (unsigned long)ts->addr, (unsigned long)ts->index, (int)status);
    return QM_EOK;
}

#endif
