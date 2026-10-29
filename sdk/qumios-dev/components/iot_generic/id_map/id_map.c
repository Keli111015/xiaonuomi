#include "id_map.h"
#include "qm.h"
#include "iot_generic.h"

#define LOG_TAG "id_map"

#if CONFIG_IOT_MAP_ALI_WIFI_SUPPORT
extern int iot_map_ali_wifi_init(void);
#endif

#if CONFIG_IOT_MAP_HARMONY_WIFI_SUPPORT
extern int iot_map_harmony_wifi_init(void);
#endif

#if CONFIG_IOT_MAP_HONOR_SUPPORT
extern int iot_map_honor_init(void);
#endif

#if CONFIG_IOT_MAP_BAIDU_MESH_SUPPORT
extern int iot_map_baidu_mesh_init(void);
#endif

#if CONFIG_IOT_MAP_MI_SUPPORT
extern int iot_map_mi_init(void);
#endif

#if CONFIG_IOT_MAP_QM_CLOUD_SUPPORT
extern int iot_map_qm_cloud_init(void);
#endif

static iot_map_hal_t *iot_map_hal[IOT_GENERIC_PLATFORM_TYPE_MAX][IOT_GENERIC_NETWORK_TYPE_MAX]= {0};

static int iot_map_hal_init(void)
{
#if CONFIG_IOT_MAP_QM_CLOUD_SUPPORT
    iot_map_qm_cloud_init();
#endif

#if CONFIG_IOT_MAP_XJIANG_WIFI_SUPPORT
    iot_map_xjiang_wifi_init();
#endif

#if CONFIG_IOT_MAP_HARMONY_WIFI_SUPPORT
    iot_map_harmony_wifi_init();
#endif

#if CONFIG_IOT_MAP_MI_SUPPORT
    iot_map_mi_init();
#endif

#if CONFIG_IOT_MAP_ALI_WIFI_SUPPORT
    iot_map_ali_wifi_init();
#endif

#if CONFIG_IOT_MAP_HONOR_SUPPORT
    iot_map_honor_init();
#endif

#if CONFIG_IOT_MAP_BAIDU_MESH_SUPPORT
    iot_map_baidu_mesh_init();
#endif

    return QM_EOK;
}

int iot_map_hal_register(iot_generic_platform_type_t platform_type, iot_generic_network_type_t network_type, iot_map_hal_t *map_hal)
{
    if(platform_type >= IOT_GENERIC_PLATFORM_TYPE_MAX || 
        network_type >= IOT_GENERIC_NETWORK_TYPE_MAX){
        return -QM_EINVAL;          
    }
    QM_RETURN_ON_FALSE(platform_type < IOT_GENERIC_PLATFORM_TYPE_MAX, -QM_EINVAL, LOG_TAG, "platform_type error");
    QM_RETURN_ON_FALSE(network_type < IOT_GENERIC_NETWORK_TYPE_MAX, -QM_EINVAL, LOG_TAG, "network_type error");
    iot_map_hal[platform_type][network_type] = map_hal;
    return QM_EOK;
}

iot_map_hal_t *iot_map_hal_get(iot_generic_platform_type_t platform_type, iot_generic_network_type_t network_type)
{
    QM_RETURN_ON_FALSE(platform_type < IOT_GENERIC_PLATFORM_TYPE_MAX, NULL, LOG_TAG, "platform_type error");
    QM_RETURN_ON_FALSE(network_type < IOT_GENERIC_NETWORK_TYPE_MAX, NULL, LOG_TAG, "network_type error");
    iot_map_hal_init();

    return iot_map_hal[platform_type][network_type];
}


#if CONFIG_IOT_MAP_VERSION_V2
void *iot_map_init(iot_generic_platform_type_t platform_type, iot_generic_network_type_t network_type, int set_num)
{
    iot_map_ctx_t *map_ctx = NULL;
    QM_RETURN_ON_FALSE(platform_type < IOT_GENERIC_PLATFORM_TYPE_MAX, NULL, LOG_TAG, "platform_type error");
    QM_RETURN_ON_FALSE(network_type < IOT_GENERIC_NETWORK_TYPE_MAX, NULL, LOG_TAG, "network_type error");
    map_ctx = (iot_map_ctx_t*)qm_malloc(sizeof(iot_map_ctx_t));
    if(map_ctx == NULL){
        goto _error;
    }

    memset(map_ctx, 0, sizeof(iot_map_ctx_t));
    map_ctx->map_hal = iot_map_hal_get(platform_type, network_type);
    if(map_ctx->map_hal == NULL){
        return NULL;
    }

    map_ctx->num = set_num;
    map_ctx->map_data = (iot_map_data_t*)qm_malloc(sizeof(iot_map_data_t) * set_num);
    if(map_ctx->map_data == NULL){
        return NULL;
    }
    memset(map_ctx->map_data, 0, sizeof(iot_map_data_t) * set_num);
    return (void*)map_ctx;

_error:
    if(map_ctx->map_data){
        qm_free(map_ctx->map_data);
        map_ctx->map_data = NULL;
    }

    if(map_ctx){
        qm_free(map_ctx);
        map_ctx = NULL;
    }
    return NULL;
}

int iot_map_deinit(void *map_handle)
{
    iot_map_ctx_t *map_ctx = (iot_map_ctx_t*)map_handle;
    QM_RETURN_ON_FALSE(map_ctx, -QM_EINVAL, LOG_TAG, "map_handle NULL");
    qm_free(map_ctx->map_data);
    map_ctx->map_data = NULL;

    qm_free(map_ctx);
    map_ctx = NULL;
    return QM_EOK;
}

void *iot_map_get(void *map_handle)
{
    void *get_handle = NULL;
    iot_map_hal_t *map_hal = NULL;
    iot_map_ctx_t *map_ctx = (iot_map_ctx_t*)map_handle;
    QM_RETURN_ON_FALSE(map_ctx, NULL, LOG_TAG, "map_handle NULL");
    QM_RETURN_ON_FALSE(map_ctx->map_hal, NULL, LOG_TAG, "map_hal NULL");
    QM_RETURN_ON_FALSE(map_ctx->index < map_ctx->num, NULL, LOG_TAG, "map_hal NULL");

    map_hal = map_ctx->map_hal;
    if(map_hal->parse == NULL){
        return NULL;
    }
    QM_RETURN_ON_FALSE(map_hal->parse, NULL, LOG_TAG, "map_hal parse NULL");

    get_handle = map_hal->parse(map_ctx->map_data + map_ctx->index);
    QM_RETURN_ON_FALSE(get_handle, NULL, LOG_TAG, "get_handle NULL");

    map_ctx->index++;
    return get_handle;
}   

int iot_map_match(void *map_handle, map_match_type_t match_type, uint8_t *map_data, int len)
{
    int i = 0;
    int index = 0;
    void *parse_data = NULL;
    iot_map_data_t *head_map_data = NULL;
    iot_map_ctx_t *map_ctx = (iot_map_ctx_t*)map_handle;
    iot_map_hal_t *map_hal = NULL;

    QM_RETURN_ON_FALSE(len, -QM_EINVAL, LOG_TAG, "map_data NULL");
    QM_RETURN_ON_FALSE(map_data, -QM_EINVAL, LOG_TAG, "map_data NULL");
    QM_RETURN_ON_FALSE(map_handle, -QM_EINVAL, LOG_TAG, "map_handle NULL");
    QM_RETURN_ON_FALSE(match_type < MAP_MATCH_TYPE_MAX, -QM_EINVAL, LOG_TAG, "map_data NULL");

    map_hal = map_ctx->map_hal;
    QM_RETURN_ON_FALSE(map_hal->match, -QM_EINVAL, LOG_TAG, "map_data NULL");

    index = map_ctx->index;
    do{
        head_map_data = map_ctx->map_data + i;
        parse_data = map_hal->parse(head_map_data);
        if(map_hal->match(match_type, parse_data, map_data)){
            memcpy(map_data,(uint8_t *)parse_data, len);
            return QM_EOK;
        }
    }while(++i < index);

    return -QM_ERROR;
}

#else
void *iot_map_init(iot_generic_platform_type_t platform_type, iot_generic_network_type_t network_type)
{
    iot_map_ctx_t *map_ctx = NULL;
    if(platform_type >= IOT_GENERIC_PLATFORM_TYPE_MAX || 
        network_type >= IOT_GENERIC_NETWORK_TYPE_MAX){
        return NULL;          
    }

    map_ctx = (iot_map_ctx_t*)qm_malloc(sizeof(iot_map_ctx_t));
    if(map_ctx == NULL){
        return NULL;
    }

    map_ctx->map_hal = iot_map_hal_get(platform_type, network_type);
    if(map_ctx->map_hal == NULL){
        return NULL;
    }

    map_ctx->head_map_data = NULL;

    return (void*)map_ctx;
}

static int iot_map_delete(iot_map_data_t *head_map_data)
{
    iot_map_data_t *map_data = NULL;
    if(head_map_data == NULL){
        return -QM_EINVAL;
    }
    while(head_map_data){
        map_data = head_map_data;
        head_map_data = head_map_data->next;
        qm_free(map_data);
        map_data = NULL;
    }
    return QM_EOK;
}

int iot_map_deinit(void *map_handle)
{
    iot_map_ctx_t *map_ctx = (iot_map_ctx_t*)map_handle;
    if(map_ctx == NULL){
        return -QM_EINVAL;
    }

    iot_map_delete(map_ctx->head_map_data);
    qm_free(map_ctx);
    map_ctx = NULL;
    return QM_EOK;
}

int iot_map_parse(void *map_handle, uint8_t *data, int len)
{
    qm_err_t ret = QM_EOK;
    iot_map_ctx_t *map_ctx = (iot_map_ctx_t*)map_handle;
    if(map_ctx == NULL){
        return -QM_EINVAL;
    }

    if(map_ctx->map_hal->parse == NULL){
        return -QM_EINVAL;
    }

    ret = map_ctx->map_hal->parse((void*)map_ctx, data, len);
    if(ret != QM_EOK){
        iot_map_delete(map_ctx->head_map_data);
        return ret;
    }
    return ret;
}

int iot_map_match(void *map_handle, map_match_type_t match_type, uint8_t *map_data, int len)
{
    iot_map_data_t *head_map_data = NULL;
    iot_map_ctx_t *map_ctx = (iot_map_ctx_t*)map_handle;
    iot_map_hal_t *map_hal = NULL;

    if(map_handle == NULL || map_data == NULL || 
        match_type >= MAP_MATCH_TYPE_MAX || len == 0){
        return -QM_EINVAL;
    }
    map_hal = map_ctx->map_hal;
    if(map_hal->match == NULL){
        return -QM_EINVAL;
    }
    head_map_data = map_ctx->head_map_data;
    while(head_map_data){
        if(map_hal->match(match_type, head_map_data->map_data, map_data)){
            memcpy(map_data, head_map_data->map_data, len);
            return QM_EOK;
        }
        head_map_data = head_map_data->next;
    }

    return -QM_ERROR;
}

int iot_map_add(void *handle, uint8_t *map_data, int len)
{
    iot_map_data_t *iot_map_data = NULL;
    iot_map_data_t *pre_map_data = NULL;
    iot_map_data_t *head_map_data = NULL;
    iot_map_ctx_t *map_ctx = (iot_map_ctx_t*)handle;

    if(handle == NULL || len == 0){
        return -QM_EINVAL;
    }

    iot_map_data = (iot_map_data_t*)qm_malloc(sizeof(iot_map_data_t) + len -1);
    if(iot_map_data == NULL){
        return -QM_ENOMEM;
    }

    iot_map_data->next = NULL;
    memcpy(iot_map_data->map_data, map_data, len);

    head_map_data = map_ctx->head_map_data;
    
    if(map_ctx->head_map_data == NULL){
        map_ctx->head_map_data = iot_map_data;
    }else{
        while(head_map_data){
            pre_map_data = head_map_data;
            head_map_data = head_map_data->next;
        }
        pre_map_data->next = iot_map_data;
    }
    return QM_EOK;
}
#endif