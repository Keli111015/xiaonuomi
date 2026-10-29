#include "iot_generic.h"
#include "iot_generic_hal.h"
#include "qm.h"
#include "id_map.h"

#define LOG_TAG "iot_generic"

typedef struct {
    iot_generic_platform_type_t platform_type;
    iot_generic_network_type_t network_type;
    iot_generic_hooks_t *hooks;
    iot_generic_hal_t *generic_hal;
    qm_tlv_ctx_t *option;
    void *map_handle;
    void *ctx;
}iot_generic_ctx_t;

#if CONFIG_IOT_GENERIC_PLATFORM_QM_CLOUD_SUPPORT
extern int iot_generic_hal_qm_cloud_init(void);
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_BAIDU_SUPPORT && CONFIG_IOT_GENERIC_NETWORK_MESH_SUPPORT
extern int iot_generic_hal_baidu_mesh_init(void);
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_ALI_SUPPORT && CONFIG_IOT_GENERIC_NETWORK_WIFI_SUPPORT
extern int iot_generic_hal_ali_wifi_init(void);
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_MI_SUPPORT
extern int iot_generic_hal_mi_init(void);
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_HONOR_SUPPORT && CONFIG_IOT_GENERIC_NETWORK_WIFI_SUPPORT
extern int iot_generic_hal_honor_init(void);
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_HARMONY_SUPPORT && CONFIG_IOT_GENERIC_NETWORK_WIFI_SUPPORT
extern int iot_generic_harmony_wifi_init(void);
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_XJIANG_SUPPORT && CONFIG_IOT_GENERIC_NETWORK_WIFI_SUPPORT
extern int iot_generic_xjiang_wifi_init(void);
#endif

static iot_generic_hal_t  *g_generic_hal[IOT_GENERIC_PLATFORM_TYPE_MAX][IOT_GENERIC_NETWORK_TYPE_MAX] = {0};
static iot_generic_ctx_t  *g_generic_ctx[IOT_GENERIC_PLATFORM_TYPE_MAX][IOT_GENERIC_NETWORK_TYPE_MAX] = {0};

static int iot_generic_hal_init(void)
{

#if CONFIG_IOT_GENERIC_PLATFORM_ALI_SUPPORT && CONFIG_IOT_GENERIC_NETWORK_WIFI_SUPPORT
    iot_generic_hal_ali_wifi_init();
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_HARMONY_SUPPORT && CONFIG_IOT_GENERIC_NETWORK_WIFI_SUPPORT
    iot_generic_harmony_wifi_init();
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_QM_CLOUD_SUPPORT
    iot_generic_hal_qm_cloud_init();
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_MI_SUPPORT
    iot_generic_hal_mi_init();
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_HONOR_SUPPORT
    iot_generic_hal_honor_init();
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_XJIANG_SUPPORT && CONFIG_IOT_GENERIC_NETWORK_WIFI_SUPPORT
    iot_generic_xjiang_wifi_init();
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_BAIDU_SUPPORT && CONFIG_IOT_GENERIC_NETWORK_MESH_SUPPORT
    iot_generic_hal_baidu_mesh_init();
#endif
    return QM_EOK;
}

int iot_generic_hal_register(iot_generic_platform_type_t platform_type, iot_generic_network_type_t network_type, iot_generic_hal_t *generic_hal)
{

    QM_RETURN_ON_FALSE(platform_type < IOT_GENERIC_PLATFORM_TYPE_MAX, -QM_EINVAL, LOG_TAG, "platform_type error");
    QM_RETURN_ON_FALSE(network_type < IOT_GENERIC_NETWORK_TYPE_MAX, -QM_EINVAL, LOG_TAG, "network_type error");
    g_generic_hal[platform_type][network_type] = generic_hal;
    return QM_EOK;
}

static iot_generic_ctx_t *iot_generic_ctx_get(iot_generic_platform_type_t platform_type, iot_generic_network_type_t network_type)
{
    QM_RETURN_ON_FALSE(platform_type < IOT_GENERIC_PLATFORM_TYPE_MAX, NULL, LOG_TAG, "platform_type error");
    QM_RETURN_ON_FALSE(network_type < IOT_GENERIC_NETWORK_TYPE_MAX, NULL, LOG_TAG, "network_type error");

    return g_generic_ctx[platform_type][network_type];
}

static iot_generic_hal_t *iot_generic_hal_get(iot_generic_platform_type_t platform_type, iot_generic_network_type_t network_type)
{
    QM_RETURN_ON_FALSE(platform_type < IOT_GENERIC_PLATFORM_TYPE_MAX, NULL, LOG_TAG, "platform_type error");
    QM_RETURN_ON_FALSE(network_type < IOT_GENERIC_NETWORK_TYPE_MAX, NULL, LOG_TAG, "network_type error");

    iot_generic_hal_init();

    return g_generic_hal[platform_type][network_type];
}

void *iot_generic_init(iot_generic_platform_type_t platform_type, iot_generic_network_type_t network_type)
{
#if CONFIG_QM_CHECK_SUPPORT
    qm_err_t ret = QM_EOK;
#endif
    iot_generic_ctx_t *generic_ctx = NULL;

    QM_RETURN_ON_FALSE(platform_type < IOT_GENERIC_PLATFORM_TYPE_MAX, NULL, LOG_TAG, "platform_type error");
    QM_RETURN_ON_FALSE(network_type < IOT_GENERIC_NETWORK_TYPE_MAX, NULL, LOG_TAG, "network_type error");

    generic_ctx = (iot_generic_ctx_t*)qm_malloc(sizeof(iot_generic_ctx_t));
    QM_RETURN_ON_FALSE(generic_ctx, NULL, LOG_TAG, "generic_ctx create error");

    memset(generic_ctx, 0, sizeof(iot_generic_ctx_t));
    generic_ctx->platform_type = platform_type;
    generic_ctx->network_type = network_type;
    
    generic_ctx->generic_hal = iot_generic_hal_get(platform_type, network_type);
    QM_GOTO_ON_FALSE(generic_ctx->generic_hal, -QM_ENOMEM, __exit, LOG_TAG, "generic_ctx generic_hal null");

    if(generic_ctx->generic_hal->init){
        generic_ctx->generic_hal->init((void*)generic_ctx);
    }

    g_generic_ctx[platform_type][network_type] = generic_ctx;
    
    return (void*)generic_ctx;

__exit:
    qm_free(generic_ctx);
    generic_ctx = NULL;
    return NULL;
}

#if CONFIG_IOT_MAP_VERSION_V2 == 0

int iot_generic_map_set(void *handle, uint8_t *data, int len)
{
    iot_generic_ctx_t *generic_ctx = (iot_generic_ctx_t*)handle;
    QM_RETURN_ON_FALSE(generic_ctx, 0, LOG_TAG, "generic_ctx null");
    QM_RETURN_ON_FALSE(data, 0, LOG_TAG, "data nullS");
    QM_RETURN_ON_FALSE(len, 0, LOG_TAG, "len error");

    generic_ctx->map_handle = iot_map_init(generic_ctx->platform_type, generic_ctx->network_type);
    QM_RETURN_ON_FALSE(generic_ctx->map_handle, 0, LOG_TAG, "map_handle error");
    return iot_map_parse(generic_ctx->map_handle, data, len);
}
#else
void *iot_generic_map_init(void *handle,int set_num)
{
    iot_generic_ctx_t *generic_ctx = (iot_generic_ctx_t*)handle;
    QM_RETURN_ON_FALSE(handle, NULL, LOG_TAG, "handle null");
    QM_RETURN_ON_FALSE(set_num, NULL, LOG_TAG, "set_num error");

    generic_ctx->map_handle = iot_map_init(generic_ctx->platform_type, generic_ctx->network_type, set_num);
    if(generic_ctx->map_handle == NULL){
        return NULL;
    }

    return generic_ctx->map_handle;
}

void *iot_generic_map_get(void *map_ctx)
{
    QM_RETURN_ON_FALSE(map_ctx, NULL, LOG_TAG, "map_ctx null");
    return iot_map_get(map_ctx);
}
#endif

int iot_generic_option_set(void *handle, iot_generic_option_t option, void *data, int len)
{
    qm_tlv_block_t *tlv_block = NULL;
    iot_generic_ctx_t *generic_ctx = (iot_generic_ctx_t*)handle;

    QM_RETURN_ON_FALSE(generic_ctx, -QM_EINVAL, LOG_TAG, "generic_ctx null");
    QM_RETURN_ON_FALSE(data, -QM_EINVAL, LOG_TAG, "data null");
    QM_RETURN_ON_FALSE(len, -QM_EINVAL, LOG_TAG, "len error");
    QM_RETURN_ON_FALSE(option < IOT_OPTION_MAX, -QM_EINVAL, LOG_TAG, "option error");

    if(generic_ctx->option == NULL){
        generic_ctx->option = qm_tlv_ctx_creat();
        QM_RETURN_ON_FALSE(generic_ctx->option, -QM_ENOMEM, LOG_TAG, "option create error");
    }

    if(generic_ctx->generic_hal->option_set){
        generic_ctx->generic_hal->option_set(option, data, len);
    }

    tlv_block = qm_tlv_block_find(generic_ctx->option, (uint16_t)option);
    if(tlv_block == NULL){
        tlv_block = qm_tlv_block_creat();
        QM_RETURN_ON_FALSE(tlv_block, -QM_ENOMEM, LOG_TAG, "tlv_block create error");
        qm_tlv_block_tag_set(tlv_block, (uint16_t)option);
        qm_tlv_block_add(generic_ctx->option, tlv_block);
    }

    return qm_tlv_pack_bytes(tlv_block, data, len);
}

int iot_generic_option_get(void *handle, iot_generic_option_t option, void *data, int *len)
{
#if CONFIG_QM_CHECK_SUPPORT
    qm_err_t ret = QM_EOK;
#endif
    qm_tlv_block_t *tlv_block = NULL;
    iot_generic_ctx_t *generic_ctx = (iot_generic_ctx_t*)handle;

    QM_RETURN_ON_FALSE(generic_ctx, -QM_EINVAL, LOG_TAG, "generic_ctx null");
    QM_RETURN_ON_FALSE(option < IOT_OPTION_MAX, -QM_EINVAL, LOG_TAG, "option error");
    QM_RETURN_ON_FALSE(generic_ctx->option, -QM_EINVAL, LOG_TAG, "generic_ctx option  NULL");

    tlv_block = qm_tlv_block_find(generic_ctx->option, (uint16_t)option);
    QM_RETURN_ON_FALSE(tlv_block, -QM_EINVAL, LOG_TAG, "tlv_block find error");

    return  qm_tlv_unpack_bytes(tlv_block, (uint8_t*)data, len);
}

int iot_generic_hooks_register(void *handle, iot_generic_hooks_t *hooks)
{
    iot_generic_ctx_t *generic_ctx = (iot_generic_ctx_t*)handle;
    QM_RETURN_ON_FALSE(hooks, -QM_EINVAL, LOG_TAG, "hooks null");
    QM_RETURN_ON_FALSE(generic_ctx, -QM_EINVAL, LOG_TAG, "generic_ctx null");

    generic_ctx->hooks = hooks;

    return QM_EOK;
}

int iot_generic_report(void *handle, qm_tlv_block_t *tlv_block)
{
    qm_err_t ret = QM_EOK;
    qm_tlv_ctx_t tlv_ctx = {0};
    iot_generic_ctx_t *generic_ctx = (iot_generic_ctx_t*)handle;
    QM_RETURN_ON_FALSE(tlv_block, -QM_EINVAL, LOG_TAG, "tlv_block null");
    QM_RETURN_ON_FALSE(generic_ctx, -QM_EINVAL, LOG_TAG, "generic_ctx null");
    QM_GOTO_ON_FALSE(generic_ctx->generic_hal->report, -QM_EINVAL, __exit,LOG_TAG, "generic_hal->report null");
    qm_tlv_block_add(&tlv_ctx, tlv_block);
    ret = generic_ctx->generic_hal->report(generic_ctx->map_handle, &tlv_ctx);

__exit:
    qm_tlv_block_delete(tlv_block);
    return ret;  
}

static void *iot_generic_multi_start(uint8_t eid)
{
    uint32_t id = 0;
    qm_tlv_ctx_t *tlv_ctx = NULL;
    tlv_ctx = qm_tlv_ctx_creat();
    QM_RETURN_ON_FALSE(tlv_ctx, NULL, LOG_TAG, "tlv_ctx create error");
    id = (uint32_t)eid;
    tlv_ctx->arg = (void*)id;

    return (void*)tlv_ctx;
}

static int iot_generic_multi_add(void *multi_handle, qm_tlv_block_t *tlv_block)
{
    qm_tlv_ctx_t *tlv_ctx = (qm_tlv_ctx_t*)multi_handle;
    QM_RETURN_ON_FALSE(tlv_block, -QM_EINVAL, LOG_TAG, "tlv_block null");
    QM_RETURN_ON_FALSE(multi_handle, -QM_EINVAL, LOG_TAG, "multi_handle null");
    return qm_tlv_block_add(tlv_ctx, tlv_block);
}

static int iot_generic_multi_end(void *handle, iot_generic_msg_type_t msg_type, void *multi_handle)
{
#if CONFIG_IOT_GENERIC_EVENT_SUPPORT
    uint32_t id = 0;
#endif
    qm_err_t ret = QM_EOK;
    iot_generic_ctx_t *generic_ctx = (iot_generic_ctx_t*)handle;
    qm_tlv_ctx_t *tlv_ctx = (qm_tlv_ctx_t*)multi_handle;
    QM_RETURN_ON_FALSE(handle, -QM_EINVAL, LOG_TAG, "handle null");
    QM_RETURN_ON_FALSE(multi_handle, -QM_EINVAL, LOG_TAG, "multi_handle null");
    if(msg_type == IOT_GENERIC_MSG_TYPE_REPORT){
        QM_GOTO_ON_FALSE(generic_ctx->generic_hal->report, -QM_EINVAL, __exit,LOG_TAG, "generic_hal->report null");
        ret = generic_ctx->generic_hal->report(generic_ctx->map_handle, tlv_ctx);
    }
#if CONFIG_IOT_GENERIC_EVENT_SUPPORT
    else if(msg_type == IOT_GENERIC_MSG_TYPE_EVENT){
        QM_GOTO_ON_FALSE(generic_ctx->generic_hal->event, -QM_EINVAL, __exit,LOG_TAG, "generic_hal->event null");
        id = (uint32_t)tlv_ctx->arg;
        ret = generic_ctx->generic_hal->event(generic_ctx->map_handle, (uint8_t)id, tlv_ctx);
    }
#endif
__exit:
    qm_tlv_ctx_delete(tlv_ctx);
    return ret;
}


void *iot_generic_multi_report_start(void)
{
    return iot_generic_multi_start(0);
}

int iot_generic_multi_report(void *multi_handle, qm_tlv_block_t *tlv_block)
{
    return iot_generic_multi_add(multi_handle, tlv_block);
}

int iot_generic_multi_report_end(void *handle, void *multi_handle)
{
    return iot_generic_multi_end(handle, IOT_GENERIC_MSG_TYPE_REPORT, multi_handle);
}

#if CONFIG_IOT_GENERIC_EVENT_SUPPORT

int iot_generic_event_report(void *handle, uint8_t eid, qm_tlv_block_t *tlv_block)
{
    qm_err_t ret = QM_EOK;
    qm_tlv_ctx_t tlv_ctx = {0};
    iot_generic_ctx_t *generic_ctx = (iot_generic_ctx_t*)handle;
    QM_RETURN_ON_FALSE(handle, -QM_EINVAL, LOG_TAG, "handle null");
    QM_RETURN_ON_FALSE(tlv_block, -QM_EINVAL, LOG_TAG, "tlv_block null");
    QM_GOTO_ON_FALSE(generic_ctx->generic_hal->event, -QM_EINVAL, __exit,LOG_TAG, "generic_hal->event null");
    qm_tlv_block_add(&tlv_ctx, tlv_block);
    ret = generic_ctx->generic_hal->event(generic_ctx->map_handle, eid, &tlv_ctx);
__exit:
    qm_tlv_block_delete(tlv_block);
    return ret;  
}

void *iot_generic_multi_event_report_start(uint8_t eid)
{
    return iot_generic_multi_start(eid);
}

int iot_generic_multi_event_report(void *multi_handle, qm_tlv_block_t *tlv_block)
{
    return iot_generic_multi_add(multi_handle, tlv_block);
}

int iot_generic_multi_event_report_end(void *handle, void *multi_handle)
{
    return iot_generic_multi_end(handle, IOT_GENERIC_MSG_TYPE_EVENT, multi_handle);
}

#endif

int iot_generic_get(iot_generic_platform_type_t platform_type, iot_generic_network_type_t network_type, char *in_data, int in_len, char *out_data, int *out_len)
{
    qm_err_t ret = QM_EOK;
    qm_tlv_ctx_t *tlv_ctx = NULL;
    iot_generic_ctx_t *generic_ctx =  NULL;
    generic_ctx = iot_generic_ctx_get(platform_type, network_type);
    if(generic_ctx == NULL){
        return -QM_EINVAL;
    }
    QM_RETURN_ON_FALSE(in_data, -QM_EINVAL, LOG_TAG, "in_data null");
    QM_RETURN_ON_FALSE(in_len, -QM_EINVAL, LOG_TAG, "in_len 0");
    QM_RETURN_ON_FALSE(out_data, -QM_EINVAL, LOG_TAG, "out_data null");
    QM_RETURN_ON_FALSE(out_len, -QM_EINVAL, LOG_TAG, "out_len null");

    QM_RETURN_ON_FALSE(generic_ctx->hooks, -QM_EINVAL, LOG_TAG, "hooks null");
    QM_RETURN_ON_FALSE(generic_ctx->hooks->get, -QM_EINVAL, LOG_TAG, "get null");
    QM_RETURN_ON_FALSE(generic_ctx->generic_hal->get_req, -QM_EINVAL, LOG_TAG, "get_req null");
    QM_RETURN_ON_FALSE(generic_ctx->generic_hal->get_rsp, -QM_EINVAL, LOG_TAG, "get_rsp null");

    tlv_ctx = qm_tlv_ctx_creat();
    QM_GOTO_ON_FALSE(tlv_ctx, -QM_ENOMEM, __exit,LOG_TAG, "tlv_ctx create null");
    ret = generic_ctx->generic_hal->get_req(in_data, in_len, generic_ctx->map_handle, tlv_ctx);
    QM_GOTO_ON_ERROR(ret, __exit,LOG_TAG, "get_req error");


    ret = generic_ctx->hooks->get(tlv_ctx, generic_ctx->hooks->ctx);
    QM_GOTO_ON_ERROR(ret, __exit,LOG_TAG, "get error");


    ret = generic_ctx->generic_hal->get_rsp(generic_ctx->map_handle, tlv_ctx, out_data, out_len);
    QM_GOTO_ON_ERROR(ret, __exit,LOG_TAG, "get_rsp error");

__exit:
    qm_tlv_ctx_delete(tlv_ctx);
    return ret;
}

#if CONFIG_IOT_GENERIC_ACTION_SUPPORT
int iot_generic_action(iot_generic_platform_type_t platform_type, iot_generic_network_type_t network_type, char *in_data, int in_len, char *out_data, int *out_len)
{
    uint8_t aid = 0;
    qm_err_t ret = QM_EOK;
    qm_tlv_ctx_t *action = NULL;
    qm_tlv_ctx_t *action_rsp = NULL;
    iot_generic_ctx_t *generic_ctx =  NULL;
    generic_ctx = iot_generic_ctx_get(platform_type, network_type);
    if(generic_ctx == NULL){
        return -QM_EINVAL;
    }
    QM_RETURN_ON_FALSE(in_data, -QM_EINVAL, LOG_TAG, "in_data null");
    QM_RETURN_ON_FALSE(in_len, -QM_EINVAL, LOG_TAG, "in_len 0");
    QM_RETURN_ON_FALSE(out_data, -QM_EINVAL, LOG_TAG, "out_data null");
    QM_RETURN_ON_FALSE(out_len, -QM_EINVAL, LOG_TAG, "out_len null");

    QM_RETURN_ON_FALSE(generic_ctx->hooks, -QM_EINVAL, LOG_TAG, "hooks null");
    QM_RETURN_ON_FALSE(generic_ctx->hooks->action, -QM_EINVAL, LOG_TAG, "actions null");
    QM_RETURN_ON_FALSE(generic_ctx->generic_hal->action_req, -QM_EINVAL, LOG_TAG, "action_req null");
    QM_RETURN_ON_FALSE(generic_ctx->generic_hal->action_rsp, -QM_EINVAL, LOG_TAG, "action_rsp null");

    action = qm_tlv_ctx_creat();
    QM_GOTO_ON_FALSE(action, -QM_ENOMEM, __exit,LOG_TAG, "action create error");

    ret = generic_ctx->generic_hal->action_req(in_data, in_len, generic_ctx->map_handle, &aid, action);
    QM_GOTO_ON_ERROR(ret, __exit,LOG_TAG, "action_req error");

    action_rsp = qm_tlv_ctx_creat();
    QM_GOTO_ON_FALSE(action_rsp, -QM_ENOMEM, __exit,LOG_TAG, "action_rsp create error");


    ret = generic_ctx->hooks->action(aid, action, action_rsp, generic_ctx->hooks->ctx);
    QM_GOTO_ON_ERROR(ret, __exit,LOG_TAG, "action error");

    ret = generic_ctx->generic_hal->action_rsp(generic_ctx->map_handle, aid, action_rsp, out_data, out_len);
    QM_GOTO_ON_ERROR(ret, __exit,LOG_TAG, "action_rsp error");

__exit:
    if(action){
        qm_tlv_ctx_delete(action);
    }
    if(action_rsp){
        qm_tlv_ctx_delete(action_rsp);
    }
    
    return ret;
}
#endif

int iot_generic_reset(void *handle)
{
    iot_generic_ctx_t *generic_ctx = (iot_generic_ctx_t*)handle;
    QM_RETURN_ON_FALSE(generic_ctx, -QM_EINVAL, LOG_TAG, "generic_ctx null");
    QM_RETURN_ON_FALSE(generic_ctx->generic_hal->reset, -QM_EINVAL, LOG_TAG, "reset null");
    return generic_ctx->generic_hal->reset(NULL);
}

int iot_generic_deinit(void *handle)
{
    qm_err_t ret = QM_EOK;
    iot_generic_ctx_t *generic_ctx = (iot_generic_ctx_t*)handle;

    QM_RETURN_ON_FALSE(generic_ctx, -QM_EINVAL, LOG_TAG, "generic_ctx null");
    QM_RETURN_ON_FALSE(generic_ctx->generic_hal->deinit, -QM_EINVAL, LOG_TAG, "deinit null");
    ret = generic_ctx->generic_hal->deinit(NULL);
    if(ret != QM_EOK){
        return -QM_ERROR;
    }
    
    g_generic_ctx[generic_ctx->platform_type][generic_ctx->network_type] = NULL;

    qm_free(generic_ctx);
    return QM_EOK;
}

int iot_generic_set(iot_generic_platform_type_t platform_type, iot_generic_network_type_t network_type, char *data, int len)
{
    qm_err_t ret = QM_EOK;
    qm_tlv_ctx_t *tlv_ctx = NULL;
    iot_generic_ctx_t *generic_ctx =  NULL;
    generic_ctx = iot_generic_ctx_get(platform_type, network_type);
    if(generic_ctx == NULL){
        return -QM_EINVAL;
    }
    
    QM_RETURN_ON_FALSE(data, -QM_EINVAL, LOG_TAG, "data null");
    QM_RETURN_ON_FALSE(len, -QM_EINVAL, LOG_TAG, "len 0");
    QM_RETURN_ON_FALSE(generic_ctx->hooks, -QM_EINVAL, LOG_TAG, "hooks null");
    QM_RETURN_ON_FALSE(generic_ctx->generic_hal->set, -QM_EINVAL, LOG_TAG, "generic_hal set null");
    QM_RETURN_ON_FALSE(generic_ctx->hooks->set, -QM_EINVAL, LOG_TAG, "hooks set null");

    tlv_ctx = qm_tlv_ctx_creat();
    QM_RETURN_ON_FALSE(tlv_ctx, -QM_ENOMEM, LOG_TAG, "tlv_ctx create error");

    ret = generic_ctx->generic_hal->set(data, len, generic_ctx->map_handle, tlv_ctx);
    QM_GOTO_ON_ERROR(ret, __exit,LOG_TAG, "action_req error");


    ret = generic_ctx->hooks->set(tlv_ctx, generic_ctx->hooks->ctx);
    QM_GOTO_ON_ERROR(ret, __exit,LOG_TAG, "action_req error");


__exit:
    qm_tlv_ctx_delete(tlv_ctx);
    return ret;
}

int iot_generic_event_notify(iot_generic_platform_type_t platform_type, iot_generic_network_type_t network_type, iot_generic_event_t event)
{
    iot_generic_ctx_t *generic_ctx =  NULL;
    generic_ctx = iot_generic_ctx_get(platform_type, network_type);
    if(generic_ctx == NULL){
        return -QM_EINVAL;
    }

    QM_RETURN_ON_FALSE(generic_ctx, -QM_EINVAL, LOG_TAG, "generic_ctx null");
    QM_RETURN_ON_FALSE(generic_ctx->hooks, -QM_EINVAL, LOG_TAG, "hooks null");
    QM_RETURN_ON_FALSE(generic_ctx->hooks->event_notify, -QM_EINVAL, LOG_TAG, "hooks event_notify null");

    generic_ctx->hooks->event_notify(event, generic_ctx->hooks->ctx);
    return QM_EOK;
}

