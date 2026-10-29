// Copyright (C) 2018-2020 Alibaba Group Holding Limited
// Adaptations to ESP-IDF Copyright (c) 2020 Espressif Systems (Shanghai) Co. Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"

#include "genie_mesh.h"
#include "genie_event.h"
#include "genie_util.h"
#include "genie_model_srv.h"
#include "iot_middleware.h"
#include "iot_generic_hal.h"
#include "qm.h"
#include "qm_utils_list.h"

typedef struct _genie_vendor_model_msg_node {
    int8_t left_retry;
    uint32_t timeout;
    uint16_t retry_period;
    esp_ble_mesh_elem_t *p_elem;
    uint8_t opid;
    uint8_t tid;
    uint16_t len;
    uint8_t data[1];
} genie_model_msg_node_t;


static const char *TAG  = "genie_model_srv";

/**
 * g_vnd_msg_list was used to save the unconfirmed vendor messages
 * g_vnd_msg_timer was used to control when the vendor messages saved in g_vnd_msg_list will be resent
 **/
// static genie_dlist_t g_vnd_msg_list = {0};
static qm_list_t g_vnd_msg_list = {0};
static qm_timer_t g_vnd_msg_timer = {0};
static qm_mutex_t g_vnd_msg_mutex = {0};

static void genie_model_retry_timer_cb(qm_timer_t *timer, void *args);

/**
 * @brief Get Genie Vendor Model Appkey ID
 *
 * @param elem
 * @param p_model
 * @return uint16_t
 */
static uint16_t bt_mesh_model_get_appkey_id(esp_ble_mesh_elem_t *elem, esp_ble_mesh_model_t *p_model)
{
    

    return 0;
}

/**
 * @brief Get Genie Vendor Model Netkey ID
 *
 * @param elem
 *
 * @return uint16_t
 */
static uint16_t bt_mesh_model_get_netkey_id(esp_ble_mesh_elem_t *elem)
{
    

    return 0;
}

/** @def genie_model_msg_gen_tid
 *
 *  @brief generate tid used in vendor model message
 *
 *  @param NULL
 *
 *  @return tid with range of [0x80, 0xff]
 */
static uint8_t genie_model_msg_gen_tid(void)
{
    
    static uint8_t tid = 0xFF;

    if (tid == 0xFF) {
        esp_fill_random(&tid, 1);
        tid &= 0x3F;
    } else {
        tid = (tid + 1) & 0x3F;
    }

    return (tid | 0x80);
}

/** @def genie_model_init
 *
 *  @brief vendor model server related global parameter initialization
 *
 *  @param NULL
 *
 *  @return always return true
 */
static uint16_t genie_model_init(void)
{
    
    static bool init_flag = false;

    if (!init_flag) {
        ESP_LOGD(TAG, "init g_vnd_msg_timer");
        qm_mutex_new(&g_vnd_msg_mutex);
        qm_timer_new(&g_vnd_msg_timer, genie_model_retry_timer_cb, &g_vnd_msg_list, XJIANG_TIMER_REPORT_TIMEOUT_MS, 0);
    }
    init_flag = true;

    return true;
}


/** @def genie_model_msg_node_generate
 *
 *  @brief duplicate genie_model_msg_t and save to genie_model_msg_node_t
 *
 *  @param pointer to the vendor model message to be duplicated
 *
 *  @return pointer to genie_model_msg_node_t for success, NULL for failure
 */
static qm_list_node_t *genie_model_msg_node_generate(genie_model_msg_t *p_model_msg)
{
    qm_list_node_t *node = NULL;
    genie_model_msg_node_t *p_msg = NULL;

    if (!p_model_msg->retry) {
        p_model_msg->retry = GENIE_MODEL_MSG_DFT_RETRY_TIMES;
    } else if (p_model_msg->retry > GENIE_MODEL_MSG_MAX_RETRY_TIMES) {
        p_model_msg->retry = GENIE_MODEL_MSG_MAX_RETRY_TIMES;
    }

    node = qm_list_node_extra_new(sizeof(genie_model_msg_node_t) + p_model_msg->len - 1);
    if (node == NULL) {
        ESP_LOGE(TAG, "qm_malloc for genie_model_msg_node_t failed");
        return NULL;
    }

    p_msg = (genie_model_msg_node_t *)node->val;
    
    p_msg->retry_period = p_model_msg->retry_period;
    p_msg->timeout = qm_now_ms() + p_model_msg->retry_period;
    p_msg->left_retry = p_model_msg->retry;
    p_msg->tid = p_model_msg->tid;
    p_msg->opid = p_model_msg->opid;
    p_msg->p_elem = p_model_msg->p_elem;
    p_msg->len = p_model_msg->len;

    memcpy(&p_msg->data[0], &p_model_msg->data[0],p_msg->len);

    return node;
}

/** @def genie_model_msg_list_append
 *
 *  @brief duplicate genie_model_msg_t and append it to vendor model message list to be monitored
 *
 *  @param pointer to the vendor model message to be duplicated
 *
 *  @return 0 for success; negative for failure
 */
static esp_err_t genie_model_msg_list_append(genie_model_msg_t *p_model_msg)
{
    
    qm_list_node_t *p_msg_node = NULL;

    p_msg_node = genie_model_msg_node_generate(p_model_msg);

    if (!p_msg_node) {
        return ESP_ERR_INVALID_ARG;
    }

    qm_mutex_lock(&g_vnd_msg_mutex, QM_WAIT_FOREVER);
    qm_list_rpush(&g_vnd_msg_list, p_msg_node);
    qm_mutex_unlock(&g_vnd_msg_mutex);

    return ESP_OK;
}


static void genie_model_prepare_buffer(genie_model_msg_t *p_model_msg)
{
    // prepare buffer
    uint8_t *data = NULL;
    esp_ble_mesh_msg_ctx_t ctx      = {0};
    esp_ble_mesh_model_t *p_model   = esp_ble_mesh_find_vendor_model(p_model_msg->p_elem, CID_ALIBABA, GENIE_VENDOR_MODEL_SRV_ID);

    data = qm_malloc(p_model_msg->len + 1);
    if (!data) {
        ESP_LOGE(TAG, "qm_malloc failed");
        return ;
    }
    data[0] = p_model_msg->tid;
    memcpy(data + 1, p_model_msg->data, p_model_msg->len);

    ctx.app_idx  = bt_mesh_model_get_appkey_id(p_model_msg->p_elem, p_model);
    ctx.net_idx  = bt_mesh_model_get_netkey_id(p_model_msg->p_elem);
    ctx.addr     = GENIE_RECV_ADDR;
    ctx.send_ttl = BLE_MESH_TTL_DEFAULT;
    ctx.send_rel = 0;

    ESP_LOGI(TAG, "vendor message send: element: %p, app_idx: %d, net_idx: %d, tid: 0x%02x, retry: %02d, len: %02d, opcode: 0x%02x, data: 0x%s",
             p_model_msg->p_elem, ctx.app_idx, ctx.net_idx, p_model_msg->tid, p_model_msg->retry, p_model_msg->len, p_model_msg->opid, util_hex2str(p_model_msg->data, p_model_msg->len));

    esp_ble_mesh_server_model_send_msg(p_model, &ctx, ESP_BLE_MESH_MODEL_OP_3(p_model_msg->opid, CID_ALIBABA), p_model_msg->len + 1, data);

    if (p_model_msg->retry == 0) {
        ESP_LOGW(TAG, "The message has been retried 9 times and may be lost. This is the last retry. tid: 0x%02x", p_model_msg->tid);
    }

    p_model_msg->retry--;
    qm_free(data);
    data = NULL;
}

/** @def genie_model_retry_timer_cb
 *
 *  @brief timeout handler for the g_vnd_msg_timer
 *
 *  @param p_timer - pointer to the timer; args - pointer to g_vnd_msg_list
 *
 *  @return N/A
 */
static void genie_model_retry_timer_cb(qm_timer_t *timer, void *args)
{
    qm_list_iterator_t self;
    int list_len = 0;
    qm_list_node_t *node = NULL;
    genie_model_msg_node_t *p_msg = NULL;
    genie_model_msg_t send_msg = {0};

    self.next = g_vnd_msg_list.head;  
    self.direction = LIST_HEAD;

    qm_timer_stop(&g_vnd_msg_timer);

    qm_mutex_lock(&g_vnd_msg_mutex, QM_WAIT_FOREVER);
    node = qm_list_iterator_next(&self);
    qm_mutex_unlock(&g_vnd_msg_mutex);

    if(node == NULL){
        return;
    }

    qm_mutex_lock(&g_vnd_msg_mutex, QM_WAIT_FOREVER);

    while (node) {
        p_msg = (genie_model_msg_node_t *)node->val;

        if (p_msg->timeout <= qm_now_ms()) {
            if (--p_msg->left_retry <= 0) {
                qm_list_remove(&g_vnd_msg_list, node);
                qm_list_node_destroy(node);
            }else{
                memset(&send_msg, 0, sizeof(genie_model_msg_t));
                send_msg.retry = p_msg->left_retry; 
                send_msg.tid = p_msg->tid;
                send_msg.data = p_msg->data;
                send_msg.len = p_msg->len;
                send_msg.opid = p_msg->opid;
                send_msg.p_elem = p_msg->p_elem;
                send_msg.retry_period = p_msg->retry_period;
                genie_model_prepare_buffer(&send_msg);
            }
            p_msg->timeout = qm_now_ms() + p_msg->retry_period;
        }
        node = qm_list_iterator_next(&self);
    }  
    list_len = qm_list_len_get(&g_vnd_msg_list);
    qm_mutex_unlock(&g_vnd_msg_mutex);

    /* start new timer */
    if (list_len) {
        qm_timer_start(&g_vnd_msg_timer);
    }

    return;
}

/** @def genie_model_msg_check_tid
 *
 *  @brief check received vendor message's tid
 *
 *  @param pointer to g_vnd_msg_list, tid of the received vendor model message
 *
 *  @return 0 for success; negative for failure
 */
static esp_err_t genie_model_msg_check_tid(qm_list_t *p_head, uint8_t tid)
{
    qm_list_iterator_t self;
    int list_len = 0;
    qm_list_node_t *node = NULL;
    genie_model_msg_node_t *p_msg = NULL;

    if (!p_head) {
        return ESP_FAIL;
    }

    qm_mutex_lock(&g_vnd_msg_mutex, QM_WAIT_FOREVER);
    list_len = qm_list_len_get(&g_vnd_msg_list);
    qm_mutex_unlock(&g_vnd_msg_mutex);

    if(!list_len){
        return ESP_OK;  
    }
    
    self.next = g_vnd_msg_list.head;  
    self.direction = LIST_HEAD;

    qm_mutex_lock(&g_vnd_msg_mutex, QM_WAIT_FOREVER);
    node = qm_list_iterator_next(&self);
    qm_mutex_unlock(&g_vnd_msg_mutex);

    qm_mutex_lock(&g_vnd_msg_mutex, QM_WAIT_FOREVER);
    while (node) {
        p_msg = (genie_model_msg_node_t *)node->val;
        if (p_msg->tid == tid) {
            ESP_LOGI(TAG, "dequeue msg: %p, opid: 0x%02x", p_msg, p_msg->opid);
            qm_list_remove(&g_vnd_msg_list, node);
            qm_list_node_destroy(node);
        }
        node = qm_list_iterator_next(&self);
    }  
    qm_mutex_unlock(&g_vnd_msg_mutex);

    return ESP_OK;
}


/** @def genie_model_msg_send
 *
 *  @brief send the vendor model message
 *
 *  @param pointer to the message to be sent
 *
 *  @return 0 for success; negative for failure
 */
esp_err_t genie_model_msg_send(genie_model_msg_t *p_model_msg)
{
    int list_len = 0;
    genie_model_init();

    if(p_model_msg->tid == 0) {
        p_model_msg->tid = genie_model_msg_gen_tid();
    }

    qm_mutex_lock(&g_vnd_msg_mutex, QM_WAIT_FOREVER);
    list_len = qm_list_len_get(&g_vnd_msg_list);
    qm_mutex_unlock(&g_vnd_msg_mutex);

    if(!list_len){
        genie_model_prepare_buffer(p_model_msg);
        qm_timer_start(&g_vnd_msg_timer);
    }

    genie_model_msg_list_append(p_model_msg);
    

    return QM_EOK;
}

/** @def genie_model_analyze
 *
 *  @brief analyze the received message and notify genie SDK
 *
 *  @param pointer to the received message (vendor model, context and the message buffer) and opid
 *
 *  @return if success return 0; if fails return error no.
 */
static esp_err_t genie_model_analyze(esp_ble_mesh_model_t *p_model,
                                   esp_ble_mesh_msg_ctx_t *p_ctx,
                                   struct net_buf_simple *p_buf,
                                   uint8_t opid)
{
    
    genie_model_msg_t msg = {0};

    if (!p_model || !p_buf) {
        return GENIE_MESH_ANALYZE_ARGS_ERROR;
    }

    if (p_buf->len < 3) {
        ESP_LOGE(TAG, "invalid buf len: %d", p_buf->len);
        return GENIE_MESH_ANALYZE_SIZE_ERROR;
    }

    memset(&msg, 0, sizeof(genie_model_msg_t));

    msg.opid = opid;
    msg.tid  = net_buf_simple_pull_u8(p_buf);

    if (genie_msg_check_tid(p_ctx->addr, msg.tid) != GENIE_MESH_SUCCESS) {
        return GENIE_MESH_TID_REPEAT_ERROR;
    }

    msg.len = p_buf->len;
    ESP_LOGD(TAG, "opcode: 0x%02x, tid: 0x%02x, len: %d", msg.opid, msg.tid, msg.len);

    if (msg.len) {
        msg.data = (uint8_t *)p_buf->data;
        net_buf_simple_pull(p_buf, msg.len);
    } else {
        msg.data = NULL;
    }

    genie_event(GENIE_EVT_SDK_VENDOR_MSG, (void *)&msg);

    return ESP_OK;
}

/** @def genie_model_get_status
 *
 *  @brief handle GENIE_OP_ATTR_GET_STATUS message
 *
 *  @param pointer to the received message (vendor model, context and the message buffer)
 *
 *  @return N/A
 */
static void genie_model_get_status(esp_ble_mesh_model_t *model,
                                   esp_ble_mesh_msg_ctx_t *ctx,
                                   struct net_buf_simple *buf)
{
    genie_model_analyze(model, ctx, buf, GENIE_OP_ATTR_GET_STATUS);
}

/** @def genie_model_set_ack
 *
 *  @brief handle GENIE_OP_ATTR_SET_ACK message
 *
 *  @param pointer to the received message (vendor model, context and the message buffer)
 *
 *  @return N/A
 */
static void genie_model_set_ack(esp_ble_mesh_model_t *model,
                                esp_ble_mesh_msg_ctx_t *ctx,
                                struct net_buf_simple *buf)
{
    genie_model_analyze(model, ctx, buf, GENIE_OP_ATTR_SET_ACK);
}

/** @def genie_model_set_unack
 *
 *  @brief handle GENIE_OP_ATTR_SET_UNACK message
 *
 *  @param pointer to the received message (vendor model, context and the message buffer)
 *
 *  @return N/A
 */
static void genie_model_set_unack(esp_ble_mesh_model_t *model,
                                  esp_ble_mesh_msg_ctx_t *ctx,
                                  struct net_buf_simple *buf)
{
    
    genie_model_analyze(model, ctx, buf, GENIE_OP_ATTR_SET_UNACK);
}

/** @def genie_model_confirm
 *
 *  @brief handle GENIE_OP_ATTR_CONFIME message
 *
 *  @param pointer to the received message (vendor model, context and the message buffer)
 *
 *  @return N/A
 */
static void genie_model_confirm(esp_ble_mesh_model_t *model,
                                esp_ble_mesh_msg_ctx_t *ctx,
                                struct net_buf_simple *buf)
{
    
    uint8_t tid = 0x0;

    if (buf->len < 1) {
        ESP_LOGE(TAG, "invalid buf len: %d", buf->len);
        return;
    }

    tid = net_buf_simple_pull_u8(buf);
    ESP_LOGI(TAG, "confirm tid: 0x%02x", tid);
    genie_model_msg_check_tid(&g_vnd_msg_list, tid);
}

/** @def genie_model_confirm_tg
 *
 *  @brief handle GENIE_OP_ATTR_CONFIME_TG message
 *
 *  @param pointer to the received message (vendor model, context and the message buffer)
 *
 *  @return N/A
 */
static void genie_model_confirm_tg(esp_ble_mesh_model_t *model,
                                   esp_ble_mesh_msg_ctx_t *ctx,
                                   struct net_buf_simple *buf)
{
    
    uint8_t tid = 0x0;

    if (buf->len < 1) {
        ESP_LOGE(TAG, "invalid buf len: %d", buf->len);
        return;
    }

    tid = net_buf_simple_pull_u8(buf);
    ESP_LOGI(TAG, "confirm_tg tid: 0x%02x", tid);
    genie_model_msg_check_tid(&g_vnd_msg_list, tid);
}

/** @def genie_model_transparent
 *
 *  @brief handle GENIE_OP_ATTR_TRANS_MSG message
 *
 *  @param pointer to the received message (vendor model, context and the message buffer)
 *
 *  @return N/A
 */
static void genie_model_transparent(esp_ble_mesh_model_t *model,
                                    esp_ble_mesh_msg_ctx_t *ctx,
                                    struct net_buf_simple *buf)
{
    
    genie_model_analyze(model, ctx, buf, GENIE_OP_ATTR_TRANS_MSG);
}

/** @def genie_model_transparent_ack
 *
 *  @brief handle GENIE_OP_ATTR_TRANS_ACK message
 *
 *  @param pointer to the received message (vendor model, context and the message buffer)
 *
 *  @return N/A
 */
static void genie_model_transparent_ack(esp_ble_mesh_model_t *model,
                                        esp_ble_mesh_msg_ctx_t *ctx,
                                        struct net_buf_simple *buf)
{
    
    uint8_t tid = 0x0;

    if (buf->len != 1) {
        ESP_LOGE(TAG, "invalid buf len: %d", buf->len);
        return;
    }

    tid = net_buf_simple_pull_u8(buf);
    genie_model_msg_check_tid(&g_vnd_msg_list, tid);
}

static genie_opcode_cb_t genie_opcode_cb[] = {
    {GENIE_MESSAGE_OP_ATTR_CONFIRMATION,       genie_model_confirm},
    {GENIE_MESSAGE_OP_ATTR_SET_ACK,            genie_model_set_ack},
    {GENIE_MESSAGE_OP_ATTR_SET_UNACK,          genie_model_set_unack},
    {GENIE_MESSAGE_OP_ATTR_CONFIRMATION_TG,    genie_model_confirm_tg},
    {GENIE_MESSAGE_OP_ATTR_GET_STATUS,         genie_model_get_status},
    {GENIE_MESSAGE_OP_ATTR_TRANSPARENT_MSG,    genie_model_transparent},
    {GENIE_MESSAGE_OP_ATTR_TRANSPARENT_ACK,    genie_model_transparent_ack},
};

/**
 * @brief Dispatch message according opcode.
 *
 * @param opcode
 * @param model
 * @param ctx
 * @param msg
 * @param length
 */
void genie_model_dispatch(uint32_t opcode, esp_ble_mesh_model_t *model,
                          esp_ble_mesh_msg_ctx_t *ctx, uint8_t *msg, uint16_t length)
{
    

    NET_BUF_SIMPLE_DEFINE(buf, length);
    memcpy(buf.__buf, msg, length);
    buf.len = length;

    for (uint8_t i = 0; i < ARRAY_SIZE(genie_opcode_cb); i++) {
        if (genie_opcode_cb[i].opcode == opcode) {
            genie_opcode_cb[i].cb(model, ctx, &buf);
            return;
        }
    }
    ESP_LOGW(TAG, "not find callback function for opcode: 0x%02x", opcode);
}
