#include "qm.h"
#include "qm_kv.h"
#include "qm_log.h"
#include "qm_errno.h"

#define LOG_TAG "qm_kv"

void qm_application_start(void)
{
    qm_err_t ret = QM_EOK;
    const char *test_set = "hello";
    int set_len = strlen(test_set);
    char test_get[16] = {0};
    int get_len = 16;
    
    ret = qm_kv_set("test_set", test_set, set_len, 1);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "kv set error");
    }

    QM_LOGD(LOG_TAG, "kv set: %s", test_set);

    ret = qm_kv_get("test_set", test_get, &get_len);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "kv get error");
    }

    QM_LOGD(LOG_TAG, "kv get: %s", test_get);

    qm_kv_del("test_set");

    QM_LOGD(LOG_TAG, "kv del success");
    
}