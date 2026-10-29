#include "qm_hal_kv.h"
#include "qm.h"
#include "qm_utils_string.h"

#include "qm_log.h"
#include "miio_user_api.h"

int qm_hal_kv_init(void)
{
    return QM_EOK;
}

int qm_hal_kv_get(const char *key, void *buffer, int *buffer_len)
{
    if ((key == NULL) || (buffer_len == NULL) || (buffer == NULL) || ((*buffer_len) == 0))
    {
        return -QM_EINVAL;
    }
    uint16_t record_id = int_str_to_num(key, strlen(key));
    return miio_record_read(record_id, buffer, *buffer_len);
}

int qm_hal_kv_set(const char *key, const void *value, int len, int sync)
{
    uint16_t record_id = int_str_to_num(key, strlen(key));

    return miio_record_write(record_id, value, len);
}

int qm_hal_kv_del(const char *key)
{
    uint16_t record_id = int_str_to_num(key, strlen(key));

    return miio_record_delete(record_id);
}

int qm_hal_kv_clean(void)
{

    return QM_EOK;
}
