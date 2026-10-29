#include "qm_tls.h"
#include "qm_log.h"
#include "qm_types.h"
#include "qm_errno.h"

#include "aos/aos.h"
#include "iot_import.h"

#define LOG_TAG "hal tls"


void *qm_tls_establish(const char *host, uint16_t port, const char *ca_crt, uint32_t ca_crt_len)
{
    uintptr_t id = 0;
    id = HAL_SSL_Establish(host, port, NULL, 0);
    return (void *)id;
}

int32_t qm_tls_destroy(void *handle)
{
    uintptr_t id = (uintptr_t)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }
    return HAL_SSL_Destroy(id);
}

int32_t qm_tls_write(void *handle, const char *buf, int len, uint32_t timeout_ms)
{
    uintptr_t id = (uintptr_t)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }
    return HAL_SSL_Write(id, buf, len, timeout_ms);
}

int32_t qm_tls_read(void * handle, char *buf, int len, uint32_t timeout_ms)
{
    uintptr_t id = (uintptr_t)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }

    if(timeout_ms == 0){
        timeout_ms = 20;
    }

    return HAL_SSL_Read(id, buf, len, timeout_ms);
}