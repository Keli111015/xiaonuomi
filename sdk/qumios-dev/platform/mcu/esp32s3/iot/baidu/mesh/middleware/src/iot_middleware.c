#include "iot_middleware.h"

#include "qm.h"
#include "qm_utils_list.h"
#include "iot_generic.h"
#include "iot_generic_hal.h"

#include "esp_log.h"
#include <errno.h>

#include "mbedtls/sha256.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_lighting_model_api.h"
#include "esp_ble_mesh_health_model_api.h"
#include "esp_ble_mesh_time_scene_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"

#include "genie_mesh.h"
#include "genie_util.h"

#include "ble_mesh_example_init.h"

static const char *TAG  = "genie_demo";

static uint8_t xjiang_tid = 0;

static int is_start = 0;
static int is_reset = 0;
static void *xjiang_iot_handle = NULL;
static uint16_t mesh_group = 0;
static qm_timer_t xjiang_timer = {0};
static qm_timer_t xjiang_config_timer = {0};
static uint8_t static_val[16] = {0x64, 0xda, 0x19, 0x8f, 0x58, 0xea, 0x55, 0x85,
                                 0x75, 0x84, 0x0d, 0xbf, 0x4f, 0x6e, 0x36, 0x8f};
static uint8_t dev_uuid[16] = {0x1C, 0x01, 0x71, 0x7F, 0x2C, 0x00, 0x00, 0xda, 0x91, 0x01, 0x7a, 0xfa, 0x28,0x02, 0x00, 0x00};

static int xjiang_iot_init(void *handle);
static void xj_swap_buf(uint8_t *dst, const uint8_t *src, int len);

static void local_operation(uint16_t group_addr, uint16_t app_idx);

static esp_ble_mesh_cfg_srv_t config_server = {
    .relay = ESP_BLE_MESH_RELAY_ENABLED,
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
#if defined(CONFIG_BLE_MESH_FRIEND)
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
#else
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#endif
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
#else
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
    .default_ttl = 6,
    /* 6 */
    .net_transmit = ESP_BLE_MESH_TRANSMIT(30, 10),
    /* 3 transmissions with 20ms interval */
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(30, 10),
};

#ifdef CONFIG_MESH_MODEL_GEN_ONOFF_SRV
ESP_BLE_MESH_MODEL_PUB_DEFINE(onoff_pub_0, 2 + 3, ROLE_NODE);
static esp_ble_mesh_gen_onoff_srv_t onoff_server_0 = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
};
#endif

#ifdef CONFIG_MESH_MODEL_LIGHTNESS_SRV
/* Light Lightness state related context */
static esp_ble_mesh_light_lightness_state_t lightness_state;

/* Light Lightness Server related context */
ESP_BLE_MESH_MODEL_PUB_DEFINE(lightness_pub, 2 + 5, ROLE_NODE);
static esp_ble_mesh_light_lightness_srv_t lightness_server = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .state = &lightness_state,
};

/* Light Lightness Setup Server related context */
ESP_BLE_MESH_MODEL_PUB_DEFINE(lightness_setup_pub, 2 + 5, ROLE_NODE);
static esp_ble_mesh_light_lightness_setup_srv_t lightness_setup_server = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .state = &lightness_state,
};
#endif

#ifdef CONFIG_MESH_MODEL_CTL_SRV
/* Light CTL state related context */
static esp_ble_mesh_light_ctl_state_t ctl_state;

/* Light CTL Server related context */
ESP_BLE_MESH_MODEL_PUB_DEFINE(ctl_pub, 2 + 9, ROLE_NODE);
static esp_ble_mesh_light_ctl_srv_t ctl_server = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .state = &ctl_state,
};

/* Light CTL Setup Server related context */
ESP_BLE_MESH_MODEL_PUB_DEFINE(ctl_setup_pub, 2 + 6, ROLE_NODE);
static esp_ble_mesh_light_ctl_setup_srv_t ctl_setup_server = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .state = &ctl_state,
};
#endif

uint8_t test_ids[1] = {0x00};

/** ESP BLE Mesh Health Server Model Context */
ESP_BLE_MESH_MODEL_PUB_DEFINE(health_pub, 2 + 11, ROLE_NODE);
static esp_ble_mesh_health_srv_t health_server = {
    .health_test.id_count = 1,
    .health_test.test_ids = test_ids,
};

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_HEALTH_SRV(&health_server, &health_pub),
#ifdef CONFIG_MESH_MODEL_GEN_ONOFF_SRV
    ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV(&onoff_pub_0, &onoff_server_0),
#endif
#ifdef CONFIG_MESH_MODEL_LIGHTNESS_SRV
    ESP_BLE_MESH_MODEL_LIGHT_LIGHTNESS_SRV(&lightness_pub, &lightness_server),
    ESP_BLE_MESH_MODEL_LIGHT_LIGHTNESS_SETUP_SRV(&lightness_setup_pub, &lightness_setup_server),
#endif
#ifdef CONFIG_MESH_MODEL_CTL_SRV
    ESP_BLE_MESH_MODEL_LIGHT_CTL_SRV(&ctl_pub, &ctl_server),
    ESP_BLE_MESH_MODEL_LIGHT_CTL_SETUP_SRV(&ctl_setup_pub, &ctl_setup_server),
#endif
};

/** @def g_vendor_model_alibaba_op
 *
 *  @brief vendor model operations struct
 *
 */
static esp_ble_mesh_model_op_t g_vendor_model_alibaba_op[] = {
    ESP_BLE_MESH_MODEL_OP(GENIE_MESSAGE_OP_ATTR_GET_STATUS,        2),
    ESP_BLE_MESH_MODEL_OP(GENIE_MESSAGE_OP_ATTR_SET_ACK,           2),
    ESP_BLE_MESH_MODEL_OP(GENIE_MESSAGE_OP_ATTR_SET_UNACK,         2),
    ESP_BLE_MESH_MODEL_OP(GENIE_MESSAGE_OP_ATTR_STATUS,            1),
    ESP_BLE_MESH_MODEL_OP(GENIE_MESSAGE_OP_ATTR_INDICATION,        1),
    ESP_BLE_MESH_MODEL_OP(GENIE_MESSAGE_OP_ATTR_CONFIRMATION,      1),
    ESP_BLE_MESH_MODEL_OP(GENIE_MESSAGE_OP_ATTR_TRANSPARENT_MSG,   1),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t ali_vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(CID_ALIBABA, GENIE_VENDOR_MODEL_SRV_ID, g_vendor_model_alibaba_op, NULL, NULL),
};

esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models,    ali_vnd_models),
};

static esp_ble_mesh_comp_t composition = {
    .cid           = CID_ALIBABA,
    .elements      = elements,
    .element_count = ARRAY_SIZE(elements),
};

/* Disable OOB security for SILabs Android app */
static esp_ble_mesh_prov_t provision = {
    .uuid           = dev_uuid,
    .static_val     = static_val,
    .static_val_len = 16,
    .output_size    = 0,                        // not support output OOB
    .output_actions = ESP_BLE_MESH_NO_OUTPUT,
    .input_size     = 0,                        // not support Input OOB
    .input_actions  = ESP_BLE_MESH_NO_INPUT,
};

void set_silent_broadcast(void)
{
    dev_uuid[13] |= GENIE_UNPROV_ADV_FEATURE_SILENT_ADV;
}

void set_unprovision_broadcast(void)
{
    dev_uuid[13] &= ~GENIE_UNPROV_ADV_FEATURE_SILENT_ADV;
}

void user_genie_event_handle(genie_event_t event, void *p_arg)
{
    genie_model_msg_t *msg = NULL;
    baidu_mesh_data_t on_off_data = {0};

    switch (event) {

        case GENIE_EVT_SDK_XJIANG_IOT_INIT:
            if(xjiang_iot_init(xjiang_iot_handle) == QM_EOK){
                if(qm_timer_is_valid(&xjiang_timer)){
                    qm_timer_stop(&xjiang_timer);
                    qm_timer_free(&xjiang_timer);
                }
            }
        break;

        case GENIE_EVT_HW_RESET_START:
            is_reset = QM_TRUE;
            if(qm_timer_is_valid(&xjiang_timer)){
                qm_timer_stop(&xjiang_timer);
                qm_timer_free(&xjiang_timer);
                xjiang_iot_init(xjiang_iot_handle);
            }
        break;

        case GENIE_EVT_SDK_XJIANG_IOT_SET:
        {
            msg = (genie_model_msg_t *)p_arg;
            if(msg == NULL){
                break;
            }

            msg->retry = 2;
            msg->p_elem = &elements[0];
            msg->opid = GENIE_OP_ATTR_STATUS;
            genie_mesh_msg_send(msg);

            if(msg->tid == xjiang_tid){
                break;
            }

            on_off_data.len = msg->len - sizeof(uint16_t);
            memcpy(&on_off_data.payload.baidu_id, msg->data, sizeof(uint16_t));
            if(on_off_data.len > BAIDU_MESH_DATA_LEN){
                on_off_data.payload.data_param.pdata = (uint8_t *)qm_malloc(on_off_data.len);
                if(on_off_data.payload.data_param.pdata == NULL){
                    ESP_LOGE(TAG, "GENIE_EVT_SDK_XJIANG_IOT_SET NO MEM!!");
                    break;
                }
                memset(on_off_data.payload.data_param.pdata, 0, on_off_data.len);
                memcpy(on_off_data.payload.data_param.pdata, msg->data + sizeof(uint16_t), on_off_data.len);
            }else{
                memcpy(on_off_data.payload.data_param.buff, msg->data + sizeof(uint16_t), on_off_data.len);
            }
            iot_generic_set(IOT_GENERIC_PLATFORM_TYPE_BAIDU, IOT_GENERIC_NETWORK_TYPE_MESH, (char *)&on_off_data, sizeof(baidu_mesh_data_t));

            if(on_off_data.payload.data_param.pdata && on_off_data.len > BAIDU_MESH_DATA_LEN){
                qm_free(on_off_data.payload.data_param.pdata);
                on_off_data.payload.data_param.pdata = NULL;
            }

            xjiang_tid = msg->tid;
        }
        break;

        case GENIE_EVT_SW_RESET:
            ESP_LOGI(TAG, "GENIE_EVT_RESET_START");
            qm_reboot();
        break;

        case GENIE_EVT_SDK_MESH_INIT:
            ESP_LOGI(TAG, "GENIE_EVT_SDK_MESH_INIT");
        break;

        case GENIE_EVT_SDK_MESH_PROV_SUCCESS:
            ESP_LOGI(TAG, "GENIE_EVT_SDK_MESH_PROV_SUCCESS");

        break;

        case GENIE_EVT_SDK_ACTION_DONE: 
            ESP_LOGI(TAG, "GENIE_EVT_SDK_ACTION_DONE");
        break;

        default:
        break;
    }
}

static void example_ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
        esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROVISIONER_RECV_HEARTBEAT_MESSAGE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_RECV_HEARTBEAT_MESSAGE_EVT, hb_src 0x%04x, 0x%04x, %d, %d, %d, 0x%04x, %d", 
            param->provisioner_recv_heartbeat.hb_src,
            param->provisioner_recv_heartbeat.hb_dst, param->provisioner_recv_heartbeat.init_ttl, param->provisioner_recv_heartbeat.rx_ttl,
            param->provisioner_recv_heartbeat.hops, param->provisioner_recv_heartbeat.feature, param->provisioner_recv_heartbeat.rssi);
    break;
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROV_REGISTER_COMP_EVT, err_code %d", param->prov_register_comp.err_code);
    break;
    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT, err_code %d", param->node_prov_enable_comp.err_code);
        // send event to genie event loop
        genie_event(GENIE_EVT_SDK_MESH_INIT, &param->node_prov_enable_comp.err_code);

        if (param->node_prov_enable_comp.err_code == -EALREADY) {
            // genie mesh init
            genie_mesh_init();
        }
    break;
    case ESP_BLE_MESH_NODE_PROV_DISABLE_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_DISABLE_COMP_EVT");
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT, bearer %s",
                 param->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");

        // send event to genie event loop
        genie_event(GENIE_EVT_SDK_MESH_PROV_START, NULL);

        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT, bearer %s",
                 param->node_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
    break;
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT: // already have configuration when device restart, and provisioning completed.
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT");
        ESP_LOG_BUFFER_HEX("NetKey: ", param->node_prov_complete.net_key, 16);
        ESP_LOGI(TAG, "net_idx 0x%04x, NetKey: %s, Primary address: 0x%04x", param->node_prov_complete.net_idx,
                 util_hex2str(param->node_prov_complete.net_key, 16), param->node_prov_complete.addr);
        break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_RESET_EVT");

        // send event to genie event loop
        genie_event(GENIE_EVT_SW_RESET, NULL);
    break;
    case ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT, err_code %d", param->node_set_unprov_dev_name_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_STORE_NODE_COMP_DATA_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_STORE_NODE_COMP_DATA_COMP_EVT, err_code %d", param->provisioner_store_node_comp_data_comp.err_code);
        break;
    case ESP_BLE_MESH_MODEL_SUBSCRIBE_GROUP_ADDR_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_SUBSCRIBE_GROUP_ADDR_COMP_EVT, err_code %d, element_addr 0x%04x, company_id 0x%04x, model_id 0x%04x, group_addr 0x%04x",
                 param->model_sub_group_addr_comp.err_code, param->model_sub_group_addr_comp.element_addr,
                 param->model_sub_group_addr_comp.company_id, param->model_sub_group_addr_comp.model_id,
                 param->model_sub_group_addr_comp.group_addr);
        break;
    case ESP_BLE_MESH_NODE_BIND_APP_KEY_TO_MODEL_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_BIND_APP_KEY_TO_MODEL_COMP_EVT, err_code %d, element_addr 0x%04x, company_id 0x%04x, model_id 0x%04x, app_idx 0x%04x",
                 param->node_bind_app_key_to_model_comp.err_code, param->node_bind_app_key_to_model_comp.element_addr,
                 param->node_bind_app_key_to_model_comp.company_id, param->node_bind_app_key_to_model_comp.model_id,
                 param->node_bind_app_key_to_model_comp.app_idx);
        break;
    default:

    break;
    }
}

static void timer_unixtime_indicate(void)
{
    genie_model_msg_t request_msg = {0};
    uint8_t           payload[2]  = {0};

    payload[0] = TIMER_ATTR_UNIX_TIME & 0xff;
    payload[1] = (TIMER_ATTR_UNIX_TIME >> 8) & 0xff;

    request_msg.opid         = GENIE_OP_ATTR_INDICATE;
    request_msg.tid          = 0;
    request_msg.data         = payload;
    request_msg.len          = sizeof(payload);
    request_msg.p_elem       = &elements[0];
    request_msg.retry_period = 120 + GENIE_MODEL_MSG_RETRY_PERIOD;
    request_msg.retry        = GENIE_MODEL_MSG_MAX_RETRY_TIMES;

    ESP_LOGI(TAG, "timer_unixtime_indicate");

    genie_mesh_msg_send(&request_msg);
}


static void example_ble_mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
        esp_ble_mesh_cfg_server_cb_param_t *param)
{

    if (event == ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {

        switch (param->ctx.recv_op) {
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
            // send event to genie event loop
            genie_event(GENIE_EVT_SDK_APPKEY_ADD, &param->value.state_change.appkey_add.app_idx);

            // local bind AppKEY and Subcribe Group Address
            local_operation(mesh_group, param->value.state_change.appkey_add.app_idx);
            // genie mesh init
            genie_mesh_init();
        break;
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_UPDATE:
            ESP_LOGD(TAG, "ESP_BLE_MESH_MODEL_OP_APP_KEY_UPDATE");
            break;
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_DELETE:
            ESP_LOGD(TAG, "ESP_BLE_MESH_MODEL_OP_APP_KEY_DELETE");
            break;

        case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND");
            ESP_LOGI(TAG, "elem_addr 0x%04x, app_idx 0x%04x, cid 0x%04x, mod_id 0x%04x",
                     param->value.state_change.mod_app_bind.element_addr,
                     param->value.state_change.mod_app_bind.app_idx,
                     param->value.state_change.mod_app_bind.company_id,
                     param->value.state_change.mod_app_bind.model_id);

            timer_unixtime_indicate();
            qm_timer_stop(&xjiang_config_timer);
            qm_timer_free(&xjiang_config_timer);
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_BAIDU, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_DEVICE_REGISTERED);
        
        break;

        case ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD:
            ESP_LOGD(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD");
            ESP_LOGD(TAG, "elem_addr 0x%04x, sub_addr 0x%04x, cid 0x%04x, mod_id 0x%04x",
                     param->value.state_change.mod_sub_add.element_addr,
                     param->value.state_change.mod_sub_add.sub_addr,
                     param->value.state_change.mod_sub_add.company_id,
                     param->value.state_change.mod_sub_add.model_id);
        break;

        case ESP_BLE_MESH_MODEL_OP_MODEL_SUB_DELETE:
            ESP_LOGD(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_SUB_DELETE");
        break;

        case ESP_BLE_MESH_MODEL_OP_HEARTBEAT_PUB_SET:
            ESP_LOGD(TAG, "ESP_BLE_MESH_MODEL_OP_HEARTBEAT_PUB_SET");
        break;

        case ESP_BLE_MESH_MODEL_OP_HEARTBEAT_SUB_SET:
            ESP_LOGD(TAG, "ESP_BLE_MESH_MODEL_OP_HEARTBEAT_SUB_SET");
        break;

        default:
            ESP_LOGW(TAG, "%s, event %d", __FUNCTION__, event);
        break;
        }
    }
}

static void example_ble_mesh_generic_server_cb(esp_ble_mesh_generic_server_cb_event_t event,
        esp_ble_mesh_generic_server_cb_param_t *param)
{
    static uint8_t on_off = 0;
    static uint32_t set_time = 0;
    baidu_mesh_data_t on_off_data = {0};
    esp_ble_mesh_gen_onoff_srv_t *srv = NULL;
    ESP_LOGI(TAG, "tid 0x%02x event 0x%02x, opcode 0x%04x, src 0x%04x, dst 0x%04x",
             param->value.set.onoff.tid, event, param->ctx.recv_op, param->ctx.addr, param->ctx.recv_dst);

    switch (event) {
        case ESP_BLE_MESH_GENERIC_SERVER_STATE_CHANGE_EVT:
            ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_SERVER_STATE_CHANGE_EVT");
            
            if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET ||
                param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK) {

                srv = param->model->user_data;

                ESP_LOGI(TAG, "onoff: 0x%02x", srv->state.onoff);
                if(set_time > qm_now_ms() && on_off == param->value.set.onoff.onoff){
                    break;
                }

                on_off =  srv->state.onoff;
                set_time = qm_now_ms() + GENIE_MODEL_MSG_RETRY_PERIOD;

                on_off_data.len = sizeof(uint8_t);
                on_off_data.payload.data_param.buff[0] =  srv->state.onoff;
                on_off_data.payload.baidu_id = BAIDU_MESH_ON_OFF_ID;
                iot_generic_set(IOT_GENERIC_PLATFORM_TYPE_BAIDU, IOT_GENERIC_NETWORK_TYPE_MESH, (char *)&on_off_data, sizeof(baidu_mesh_data_t));
            }
        break;
        case ESP_BLE_MESH_GENERIC_SERVER_RECV_GET_MSG_EVT:
            ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_SERVER_RECV_GET_MSG_EVT");
            if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET) {
                srv = param->model->user_data;
                ESP_LOGI(TAG, "onoff: 0x%02x", srv->state.onoff);
                esp_ble_mesh_server_model_send_msg(param->model, &param->ctx,
                                                ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_STATUS, sizeof(srv->state.onoff), &srv->state.onoff);
            }
        break;
        case ESP_BLE_MESH_GENERIC_SERVER_RECV_SET_MSG_EVT:
            ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_SERVER_RECV_SET_MSG_EVT");
            if (    param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET ||
                    param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK) {
                    ESP_LOGI(TAG, "onoff 0x%02x, tid 0x%02x", param->value.set.onoff.onoff, param->value.set.onoff.tid);


            }
        break;
        default:
            ESP_LOGW(TAG, "Unknown Generic Server event 0x%02x", event);
        break;
    }
}

static int32_t bt_mesh_ceiling(float num)
{
    int32_t inum = (int32_t)num;
    if (num == (float)inum) {
        return inum;
    }
    return inum + 1;
}

uint16_t convert_lightness_actual_to_linear(uint16_t actual)
{
    float tmp = ((float) actual / UINT16_MAX);
    return bt_mesh_ceiling(UINT16_MAX * tmp * tmp);
}

static void example_ble_mesh_lighting_server_cb(esp_ble_mesh_lighting_server_cb_event_t event,
        esp_ble_mesh_lighting_server_cb_param_t *param)
{
#ifdef CONFIG_MESH_MODEL_LIGHTNESS_SRV
    uint16_t lightness;
#endif
#ifdef CONFIG_MESH_MODEL_CTL_SRV
    uint16_t temperature;
    uint16_t delta_uv;
#endif

    ESP_LOGI(TAG, "event 0x%02x, opcode 0x%04x, src 0x%04x, dst 0x%04x",
             event, param->ctx.recv_op, param->ctx.addr, param->ctx.recv_dst);

    switch (event) {
    case ESP_BLE_MESH_LIGHTING_SERVER_STATE_CHANGE_EVT:
        switch (param->ctx.recv_op) {
#ifdef CONFIG_MESH_MODEL_LIGHTNESS_SRV
        /*!< Light Lightness Message Opcode */
        case ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET:
        case ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET_UNACK: {
            /* Light Lightness Server Model - Primary Element */
            ESP_LOGI(TAG, "lightness actual %d", param->value.state_change.lightness_set.lightness);
            /* Update bound states */
            /* Update bound states second way */
            break;
        }
        case ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_LINEAR_SET:
        case ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_LINEAR_SET_UNACK: {
            /* Light Lightness Server Model - Primary Element */
            ESP_LOGI(TAG, "lightness linear %d", param->value.state_change.lightness_linear_set.lightness);

            /* Update bound states second way */
            break;
        }
        /*!< Light Lightness Setup Message Opcode */
        case ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_DEFAULT_SET:
        case ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_DEFAULT_SET_UNACK:
            /* Light Lightness Setup Server Model - Primary Element */
            ESP_LOGI(TAG, "lightness default %d", param->value.state_change.lightness_default_set.lightness);
            break;
        case ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_RANGE_SET:
        case ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_RANGE_SET_UNACK:
            /* Light Lightness Setup Server Model - Primary Element */
            ESP_LOGI(TAG, "lightness min %d, max %d",
                     param->value.state_change.lightness_range_set.range_min,
                     param->value.state_change.lightness_range_set.range_max);
            break;
#endif
#ifdef CONFIG_MESH_MODEL_CTL_SRV
        /*!< Light CTL Message Opcode */
        case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_SET:
        case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_SET_UNACK:
            /* Light CTL Server Model - Primary Element */
            ESP_LOGI(TAG, "lightness 0x%04x, temperature 0x%04x, delta uv 0x%04x",
                     param->value.state_change.ctl_set.lightness,
                     param->value.state_change.ctl_set.temperature,
                     param->value.state_change.ctl_set.delta_uv);
            /* Update bound states */
            lightness = param->value.state_change.ctl_set.lightness;
            temperature = param->value.state_change.ctl_set.temperature;
            delta_uv = param->value.state_change.ctl_set.delta_uv;
            state.light_lightness_actual.lightness = lightness;
            esp_ble_mesh_server_model_update_state(lightness_server.model, ESP_BLE_MESH_LIGHT_LIGHTNESS_ACTUAL_STATE, &state);
            state.light_lightness_linear.lightness = convert_lightness_actual_to_linear(lightness);
            esp_ble_mesh_server_model_update_state(lightness_server.model, ESP_BLE_MESH_LIGHT_LIGHTNESS_LINEAR_STATE, &state);
            state.gen_onoff.onoff = lightness ? 1 : 0;
            esp_ble_mesh_server_model_update_state(onoff_server_0.model, ESP_BLE_MESH_GENERIC_ONOFF_STATE, &state);

            /* Update bound states second way */
            break;
        case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_SET:
        case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_SET_UNACK:
            /* Light CTL Temperature Server Model - Secondary Element */
            ESP_LOGI(TAG, "temperature 0x%04x, delta uv 0x%04x",
                     param->value.state_change.ctl_temp_set.temperature,
                     param->value.state_change.ctl_temp_set.delta_uv);
            break;
        /*!< Light CTL Setup Message Opcode */
        case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_DEFAULT_SET:
        case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_DEFAULT_SET_UNACK:
            /* Light CTL Setup Server Model - Primary Element */
            ESP_LOGI(TAG, "lightness 0x%04x, temperature 0x%04x, delta uv 0x%04x",
                     param->value.state_change.ctl_default_set.lightness,
                     param->value.state_change.ctl_default_set.temperature,
                     param->value.state_change.ctl_default_set.delta_uv);
            break;
        case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_RANGE_SET:
        case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_RANGE_SET_UNACK:
            /* Light CTL Setup Server Model - Primary Element */
            ESP_LOGI(TAG, "temperature min 0x%04x, max 0x%04x",
                     param->value.state_change.ctl_temp_range_set.range_min,
                     param->value.state_change.ctl_temp_range_set.range_max);
            break;
#endif
        }
        break;
    default:
        ESP_LOGE(TAG, "Unknown Lighting Server event 0x%02x", event);
        break;
    }
}

static void example_ble_mesh_custom_model_cb(esp_ble_mesh_model_cb_event_t event,
        esp_ble_mesh_model_cb_param_t *param)
{
    ESP_LOGD(TAG, "event 0x%02x", event);

    switch (event) {
        case ESP_BLE_MESH_MODEL_OPERATION_EVT:
            ESP_LOGD(TAG, "ESP_BLE_MESH_MODEL_OPERATION_EVT opcode: 0x%06x", param->model_operation.opcode);
            genie_model_dispatch(param->model_operation.opcode, param->model_operation.model,
                                param->model_operation.ctx, param->model_operation.msg, param->model_operation.length);
        break;
        case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
            if (param->model_send_comp.err_code) {
                ESP_LOGE(TAG, "Failed to send message: 0x%06x, err: %d", param->model_send_comp.opcode, param->model_send_comp.err_code);
                break;
            }
        break;
        case ESP_BLE_MESH_MODEL_EVT_MAX:
            ESP_LOGW(TAG, "ESP_BLE_MESH_MODEL_EVT_MAX");
        break;
        default:
        break;
    }
}

static void xjiang_mesh_init_request_cb(qm_timer_t *timer, void *arg)
{
    genie_event(GENIE_EVT_SDK_XJIANG_IOT_INIT, NULL);
}


static esp_err_t ble_mesh_init(void *iot_handle)
{
    esp_err_t err = ESP_OK;

    xjiang_iot_handle = iot_handle;

    qm_timer_new(&xjiang_timer, xjiang_mesh_init_request_cb, NULL, XJIANG_TIMER_TIMEOUT_MS, 1);
    qm_timer_start(&xjiang_timer);

    return err;
}

void iot_middleware_init(void *handle)
{
    esp_err_t err;

    /* Initialize the Bluetooth Mesh Subsystem */
    err = ble_mesh_init(handle);
    if (err) {
        ESP_LOGE(TAG, "Bluetooth mesh init failed (err %d)", err);
    }
}

void iot_middleware_report(baidu_mesh_data_t *operation)
{
    genie_model_msg_t reply_msg = {0};

    if(operation == NULL){
        return;
    }

    reply_msg.opid         = GENIE_OP_ATTR_INDICATE;
    reply_msg.tid          = 0;
    reply_msg.data         = (uint8_t *)&operation->payload;
    reply_msg.len          = operation->len + sizeof(uint16_t);
    reply_msg.p_elem       = &elements[0];
    reply_msg.retry_period = XJIANG_TIMER_REPORT_TIMEOUT_MS;
    reply_msg.retry = GENIE_MODEL_MSG_MAX_RETRY_TIMES;
    genie_mesh_msg_send(&reply_msg);
}


static void xj_swap_buf(uint8_t *dst, const uint8_t *src, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        dst[len - 1 - i] = src[i];
    }
}

static uint8_t *mac_str2hex(const char *mac_str, uint8_t *mac_hex)
{
    uint32_t mac_data[6] = {0};

    sscanf(mac_str, "%02x%02x%02x%02x%02x%02x",
           mac_data, mac_data + 1, mac_data + 2, mac_data + 3, mac_data + 4, mac_data + 5);

    for (int i = 0; i < 6; i++) {
        mac_hex[i] = mac_data[i];
    }

    return mac_hex;
}

static void xjiang_mesh_config_timeout_cb(qm_timer_t *timer, void *arg)
{
    esp_ble_mesh_deinit_param_t param = {
        .erase_flash = 1,
    };
    ESP_LOGE(TAG, "Xjiang_mesh_config_timeout !!" );

    if(qm_timer_is_valid(&xjiang_config_timer)){
        qm_timer_stop(&xjiang_config_timer);
        qm_timer_free(&xjiang_config_timer);
    }
    esp_ble_mesh_node_prov_disable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
    esp_ble_mesh_deinit(&param);

    iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_BAIDU, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_CONFIG_TIMEOUT);
}

int iot_middleware_is_start(void)
{
    return is_start;
}

static int xjiang_iot_init(void *handle)
{
    int ret = QM_EOK;
    int len = 0;

    uint32_t op_pid = 0;
    uint8_t *op_name = NULL;
    uint8_t *op_secret = NULL;
    char *authvalue_string = NULL;

    uint8_t sha256_out[32] = {0};
    uint8_t product_id[4]= {0};
    uint8_t ali_mac_address[6] = {0};

    ret = iot_generic_option_get(handle, IOT_OPTION_PRODUCT_ID, &op_pid, &len);
    if(ret != QM_EOK || len == 0){
        ret = -QM_EINVAL;
        goto _next;
    }

    len = 0;
    ret = iot_generic_option_get(handle, IOT_OPTION_MODEL, NULL, &len);
    if(ret != QM_EOK || len == 0){
        ret = -QM_EINVAL;
        goto _next;
    }

    op_name = (uint8_t *)qm_malloc(len + 1);
    if(op_name == NULL){
        ret = -QM_EINVAL;
        goto _next;
    }
    memset(op_name, 0, len + 1);

    ret = iot_generic_option_get(handle, IOT_OPTION_MODEL, op_name, &len);
    if(ret != QM_EOK || len == 0){
        ret = -QM_EINVAL;
        goto _next;
    }

    len = 0;
    ret = iot_generic_option_get(handle, IOT_OPTION_DEVICE_SECRET, NULL, &len);
    if(ret != QM_EOK || len == 0){
        ret = -QM_EINVAL;
        goto _next;
    }

    op_secret = (uint8_t *)qm_malloc(len + 1);
    if(op_secret == NULL){
        ret = -QM_EINVAL;
        goto _next;
    }
    memset(op_secret, 0, len + 1);

    ret = iot_generic_option_get(handle, IOT_OPTION_DEVICE_SECRET, op_secret, &len);
    if(ret != QM_EOK || len == 0){
        ret = -QM_EINVAL;
        goto _next;
    }

    ret = iot_generic_option_get(handle, IOT_OPTION_MESH_GROUP, &mesh_group, &len);
    if(ret != QM_EOK || len == 0){
        ret = QM_EOK; //模组品类无需组播地址,暂不做错误判断
    }
    
    asprintf(&authvalue_string, "%08x,%s,%s", op_pid, op_name, op_secret);
    ESP_LOGI(TAG, "authvalue_string: %s", authvalue_string);

    mbedtls_sha256_ret((const unsigned char *)authvalue_string, strlen(authvalue_string), sha256_out, 0);
    memcpy(static_val, sha256_out, 16);
    provision.static_val = static_val;

    mac_str2hex(authvalue_string, product_id);
    xj_swap_buf(dev_uuid + 3, product_id, 4);

    mac_str2hex((const char *)op_name, ali_mac_address);
    xj_swap_buf(dev_uuid + 7, ali_mac_address, 6);
    ali_mac_address[5] -= 2;
    is_start = 1;

    esp_base_mac_addr_set(ali_mac_address);

    bluetooth_init();

    // 注册处理配网和入网相关事件的回调函数
    ESP_ERROR_CHECK(esp_ble_mesh_register_prov_callback(example_ble_mesh_provisioning_cb));
    // 注册处理 Configuration Server Model 相关事件的回调函数
    ESP_ERROR_CHECK(esp_ble_mesh_register_config_server_callback(example_ble_mesh_config_server_cb));
    ESP_ERROR_CHECK(esp_ble_mesh_register_generic_server_callback(example_ble_mesh_generic_server_cb));
    ESP_ERROR_CHECK(esp_ble_mesh_register_lighting_server_callback(example_ble_mesh_lighting_server_cb));
    ESP_ERROR_CHECK(esp_ble_mesh_register_custom_model_callback(example_ble_mesh_custom_model_cb));


    esp_ble_mesh_init(&provision, &composition);
    esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);

    if(esp_ble_mesh_node_is_provisioned()){
        iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_BAIDU, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_CONNECTING_ROUTER);
    }else{
        iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_BAIDU, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_BLE_CONFIG);
        qm_timer_new(&xjiang_config_timer, xjiang_mesh_config_timeout_cb, NULL, XJIANG_TIMER_CONFIG_TIMEOUT_MS, 0);
        qm_timer_start(&xjiang_config_timer);
    }
    
_next:
    
    if(is_reset && !is_start){
        esp_base_mac_addr_set(ali_mac_address);
        bluetooth_init();
        esp_ble_mesh_init(&provision, &composition);
        ret = QM_EOK;
    }

    if(op_name){
        qm_free(op_name);
        op_name = NULL;
    }

    if(op_secret){
        qm_free(op_secret);
        op_secret = NULL;
    }

    if(authvalue_string){
        qm_free(authvalue_string);
        authvalue_string = NULL;
    }
    return ret;
}

static void local_operation(uint16_t group_addr, uint16_t app_idx)
{
    if(group_addr != 0){
        ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_HEALTH_SRV, group_addr));
    }
    ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_HEALTH_SRV, GENIE_ALL_GROUP_ADDR));
    ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_HEALTH_SRV, app_idx));
#ifdef CONFIG_MESH_MODEL_GEN_ONOFF_SRV
    if(group_addr != 0){
        ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV, group_addr));
    }
    ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV, GENIE_ALL_GROUP_ADDR));
    ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV, app_idx));
#endif
#ifdef CONFIG_MESH_MODEL_LIGHTNESS_SRV
    if(group_addr != 0){
        ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_SRV, group_addr));
    }
    ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_SRV, GENIE_ALL_GROUP_ADDR));
    ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_SRV, app_idx));
    if(group_addr != 0){
        ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_SETUP_SRV, group_addr));
    }
    ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_SETUP_SRV, GENIE_ALL_GROUP_ADDR));
    ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_SETUP_SRV, app_idx));
#endif
#ifdef CONFIG_MESH_MODEL_CTL_SRV
    if(group_addr != 0){
        ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_SRV, group_addr));
    }
    ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_SRV, GENIE_ALL_GROUP_ADDR));
    ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_SRV, app_idx));
    if(group_addr != 0){ 
        ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_SETUP_SRV, group_addr));
    }
    ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_SETUP_SRV, GENIE_ALL_GROUP_ADDR));
    ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_SETUP_SRV, app_idx));
#endif
#ifdef CONFIG_MESH_MODEL_HSL_SRV
    if(group_addr != 0){
        ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_SRV, group_addr));
    }
    ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_SRV, GENIE_ALL_GROUP_ADDR));
    ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_SRV, app_idx));
    ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_SETUP_SRV, GENIE_LIGHT_GROUP_ADDR));
    ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_SETUP_SRV, GENIE_ALL_GROUP_ADDR));
    ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_SETUP_SRV, app_idx));
#endif
    if(group_addr != 0){
        ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), CID_ALIBABA, GENIE_VENDOR_MODEL_SRV_ID, group_addr));
    }
    ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), CID_ALIBABA, GENIE_VENDOR_MODEL_SRV_ID, GENIE_ALL_GROUP_ADDR));
    ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address(), CID_ALIBABA, GENIE_VENDOR_MODEL_SRV_ID, app_idx));

}