#include "qm.h"
#include "qm_hal_kv.h"

#include <string.h>

#include <bk_ef.h>
#include <easyflash.h>

#define PSM_KV_FORMAT "qm_kv/%s"

int qm_hal_kv_init(void)
{
    return QM_EOK;
}

static int qm_hal_kv_name_build(const char *key, char **out_name)
{
    size_t len;

    if (key == NULL || out_name == NULL) {
        return -QM_EINVAL;
    }

    len = strlen(key) + strlen("qm_kv/") + 1;
    *out_name = (char *)qm_malloc(len);
    if (*out_name == NULL) {
        return -QM_ENOMEM;
    }

    qm_snprintf(*out_name, (int)len, PSM_KV_FORMAT, key);
    return QM_EOK;
}

int qm_hal_kv_get(const char *key, void *buffer, int *buffer_len)
{
    int length = 0;
    char *name = NULL;

    if (buffer == NULL || key == NULL || buffer_len == NULL) {
        return -QM_EINVAL;
    }

    if (qm_hal_kv_name_build(key, &name) != QM_EOK) {
        return -QM_ENOMEM;
    }

    length = bk_get_env_enhance(name, buffer, *buffer_len);
    if (length <= 0) {
        *buffer_len = 0;
    } else {
        *buffer_len = length;
    }

    qm_free(name);
    return QM_EOK;
}

int qm_hal_kv_set(const char *key, const void *value, int len, int sync)
{
    char *name = NULL;
    EfErrCode ret;

    (void)sync;

    if (value == NULL || key == NULL || len <= 0) {
        return -QM_EINVAL;
    }

    if (qm_hal_kv_name_build(key, &name) != QM_EOK) {
        return -QM_ENOMEM;
    }

    ret = bk_set_env_enhance(name, value, len);
    qm_free(name);

    return (ret == EF_NO_ERR) ? QM_EOK : -QM_ERROR;
}

int qm_hal_kv_del(const char *key)
{
    char *name = NULL;
    EfErrCode ret;

    if (key == NULL) {
        return -QM_EINVAL;
    }

    if (qm_hal_kv_name_build(key, &name) != QM_EOK) {
        return -QM_ENOMEM;
    }

    ret = ef_del_env(name);
    qm_free(name);

    return (ret == EF_NO_ERR) ? QM_EOK : -QM_ERROR;
}

int qm_hal_kv_clean(void)
{
    return QM_EOK;
}
