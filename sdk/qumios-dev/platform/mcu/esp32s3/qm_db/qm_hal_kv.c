#include "qm_hal_kv.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "qm_errno.h"

#define QM_KV_STORAGE_NAMESPACE "qm_kv"

int qm_hal_kv_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret != ESP_OK){
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return QM_EOK;
}

int qm_hal_kv_get(const char *key, void *buffer, int *buffer_len)
{
    esp_err_t err;
    nvs_handle handle;
    size_t length;

    err = nvs_open(QM_KV_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if(err != ESP_OK){
        return -1;
    }

    err = nvs_get_blob(handle, key, NULL, &length);
    if(err != ESP_OK){
        return -1;
    }

    if(buffer){
        nvs_get_blob(handle, key, buffer, &length);
    }

    *buffer_len = length; 

    nvs_close(handle);

    return 0;
}

int qm_hal_kv_set(const char *key, const void *value, int len, int sync)
{
    esp_err_t err;
    nvs_handle handle;

    err = nvs_open(QM_KV_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if(err != ESP_OK){
        return -1;
    }

    err = nvs_set_blob(handle, key, value, len);
    if(err != ESP_OK){
        return -1;
    }

    nvs_commit(handle);
    nvs_close(handle);

    return 0;
}

int qm_hal_kv_del(const char *key)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(QM_KV_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if(err != ESP_OK){
        return -1;
    }
    nvs_erase_key(handle, key);
    return 0;
}

int qm_hal_kv_clean(void)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(QM_KV_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if(err != ESP_OK){
        return -1;
    }
    nvs_erase_all(handle);
    return 0;
}
