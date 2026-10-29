#include "qm_kv.h"
#include "qm_hal_kv.h"

int qm_kv_init(void) { return qm_hal_kv_init(); }
int qm_kv_set(const char *key, const void *value, int len, int sync)
{
    return qm_hal_kv_set(key, value, len, sync);
}
int qm_kv_get(const char *key, void *buffer, int *buffer_len)
{
    return qm_hal_kv_get(key, buffer, buffer_len);
}
int qm_kv_del(const char *key) { return qm_hal_kv_del(key); }
int qm_kv_clean(void) { return qm_hal_kv_clean(); }

