
#include "iot_cloud_debug.h"
#include "iot_device.h"

static iot_conn_info_t     iot_conn_info;
static iot_device_info_t   iot_device_info;
static int                  iot_devinfo_inited = 0;

int iot_device_info_init(void)
{
    if (iot_devinfo_inited) {
        iot_log_debug("device_info already created, return!");
        return 0;
    }

    memset(&iot_device_info, 0x0, sizeof(iot_device_info_t));
    memset(&iot_conn_info, 0x0, sizeof(iot_conn_info_t));
    iot_devinfo_inited = 1;

    iot_log_info("device_info created successfully!");
    return 0;
}

int iot_device_info_set(
            unsigned int product_id,
            const char *device_id,
            const char *device_secret)
{
    iot_log_debug("start to set device info!");

    memset(&iot_device_info, 0x0, sizeof(iot_device_info));
    iot_device_info.product_id = product_id;
    strncpy(iot_device_info.device_id, device_id, DEVICE_ID_LEN);
    strncpy(iot_device_info.device_secret, device_secret, DEVICE_SECRET_LEN);

    iot_log_debug("device_info set successfully!");
    return 0;
}

iot_device_info_pt iot_device_info_get(void)
{
    return &iot_device_info;
}

iot_conn_info_pt iot_conn_info_get(void)
{
    return &iot_conn_info;
}

