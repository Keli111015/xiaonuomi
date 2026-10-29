#include "id_map.h"
#include "qm_errno.h"

#if CONFIG_IOT_MAP_HARMONY_WIFI_SUPPORT

static int map_parse(void *list_handle, uint8_t *data, int len);
static int map_match(map_match_type_t match_type, void *a, void *b);

static iot_map_hal_t map_hal_harmony_wifi = {
    .parse = map_parse,
    .match = map_match,
};

int iot_map_harmony_wifi_init(void)
{
    return iot_map_hal_register(IOT_GENERIC_PLATFORM_TYPE_HARMONY, IOT_GENERIC_NETWORK_TYPE_WIFI, &map_hal_harmony_wifi);
}

static int map_parse(void *list_handle, uint8_t *data, int len)
{
    
    return QM_EOK;
}

static int map_match(map_match_type_t match_type, void *a, void *b)
{

    return QM_EOK;
}

#endif