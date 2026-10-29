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

#include "qm.h"
#include "qm_rtc.h"

#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"

#include "genie_mesh.h"
#include "genie_model_srv.h"
#include "genie_event.h"
#include "genie_util.h"

#include "qm_utils_list.h"
#include "iot_middleware.h"


static const char *TAG  = "genie_mesh";

extern esp_ble_mesh_elem_t elements[];
extern void set_silent_broadcast(void);

uint8_t g_indication_flag;

static qm_timer_t g_prov_timer;
#ifdef CONFIG_MESH_MODEL_VENDOR_SRV
static qm_timer_t g_indc_timer;
#endif


void genie_pbadv_start_silent_adv(void)
{
    set_silent_broadcast();
    /* disable gatt adv */
    esp_ble_mesh_node_prov_disable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
    esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV);
    // todo: modify beacon interbal
}

static void genie_prov_timer_cb(qm_timer_t *timer, void *arg)
{
    
    genie_event(GENIE_EVT_SDK_MESH_PROV_TIMEOUT, NULL);
}

void genie_prov_timer_start(void)
{
    
    static uint8_t inited = 0;

    if (!inited) {
        qm_timer_new(&g_prov_timer, genie_prov_timer_cb, NULL, GENIE_PROVISIONING_TIMEOUT, 0);
        inited = 1;
    }
    qm_timer_start(&g_prov_timer);
}

void genie_prov_timer_stop(void)
{
    qm_timer_stop(&g_prov_timer);
}

#ifdef CONFIG_MESH_MODEL_VENDOR_SRV
static void poweron_indicate_cb(qm_timer_t *timer, void *arg)
{
    qm_timer_stop(&g_indc_timer);
    qm_timer_free(&g_indc_timer);

    genie_prov_timer_stop();
    genie_event(GENIE_EVT_SDK_MESH_PWRON_INDC, NULL);
}

void poweron_indicate_start(void)
{
    
    static  uint8_t inited = 0;
    uint8_t random_time    = 0;

    if (inited){
        return;
    }

    inited = 1;
    esp_fill_random(&random_time, 1);
#ifdef CONFIG_MESH_MODEL_TRANS
    random_time = 2000 + 8000 * random_time / 255;
#else
    random_time = 500 + 9500 * random_time / 255;
#endif
    ESP_LOGI(TAG, "power on indicate random: %d ms", random_time);

    qm_timer_new(&g_indc_timer, poweron_indicate_cb, NULL, random_time, 0);
    qm_timer_start(&g_indc_timer);
}

uint16_t genie_indicate_hw_reset_event(void)
{
    
    genie_model_msg_t reply_msg  = {0};
    uint8_t           payload[3] = {0};

    payload[0] = GENIE_MODEL_ATTR_DEVICE_EVENT & 0xff;
    payload[1] = (GENIE_MODEL_ATTR_DEVICE_EVENT >> 8) & 0xff;
    payload[2] = (uint8_t)EVENT_HW_RESET;

    reply_msg.opid         = GENIE_OP_ATTR_INDICATE;
    reply_msg.tid          = 0;
    reply_msg.data         = payload;
    reply_msg.len          = 3;
    reply_msg.p_elem       = &elements[0];
    reply_msg.retry_period = 120 + GENIE_MODEL_MSG_RETRY_PERIOD;
    reply_msg.retry        = GENIE_MODEL_MSG_MAX_RETRY_TIMES;

    ESP_LOGI(TAG, "genie_indicate_hw_reset_event");

    genie_mesh_msg_send(&reply_msg);

    return 0;
}
#endif

static void genie_timer_operate_status(uint8_t tid, uint16_t attr_type, uint32_t unix_time)
{
    
    uint16_t          payload_len = 0;
    uint8_t           payload[10] = {0};
    genie_model_msg_t reply_msg   = {0};

    switch (attr_type) {
    case TIMER_ATTR_UNIX_TIME: {
        payload_len = 2 + 4;
        payload[0] = TIMER_ATTR_UNIX_TIME & 0xff;
        payload[1] = (TIMER_ATTR_UNIX_TIME >> 8) & 0xff;
        payload[2] = (uint8_t)(unix_time & 0xFF);
        payload[3] = (uint8_t)((unix_time >> 8) & 0xFF);
        payload[4] = (uint8_t)((unix_time >> 16) & 0xFF);
        payload[5] = (uint8_t)((unix_time >> 24) & 0xFF);
    }
    break;
    case TIMER_ATTR_TIMEZONE_SETTING: {
        
    }
    break;
    case TIMER_ATTR_TIMING_SYNC: {

    }
    break;

    default:
        ESP_LOGW(TAG, "don't handle this attr_type");
        return;
    }

    reply_msg.opid         = GENIE_OP_ATTR_STATUS;
    reply_msg.tid          = tid;
    reply_msg.data         = payload;
    reply_msg.len          = payload_len;
    reply_msg.p_elem       = &elements[0];
    reply_msg.retry_period = 120 + GENIE_MODEL_MSG_RETRY_PERIOD;
    reply_msg.retry        = 1;
    ESP_LOGI(TAG, "genie_timer_operate_status");

    genie_mesh_msg_send(&reply_msg);
    genie_event(GENIE_EVT_SDK_XJIANG_IOT_TIME_UPDATE, NULL);

}

static void genie_timer_unixtime_indicate(void)
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

    ESP_LOGI(TAG, "genie_timer_unixtime_indicate");

    genie_mesh_msg_send(&request_msg);
}

uint16_t genie_vendor_model_msg_handle(genie_model_msg_t *p_msg)
{
    
    uint8_t *p_data = NULL;
    qm_rtc_dev_t rtc_dev = {
        .config.format = QM_RTC_FORMAT_DEC,
    };

    if (!p_msg || !p_msg->data) {
        return -1;
    }

    p_data = p_msg->data;
    ESP_LOGI(TAG, "opcode:0x%x, tid:%d, len:%d", p_msg->opid, p_msg->tid, p_msg->len);
    if (p_data && p_msg->len) {
        ESP_LOGI(TAG, "payload: %s", util_hex2str(p_data, p_msg->len));
    }

    switch (p_msg->opid) {
    case GENIE_OP_ATTR_INDICATE: 
    {
        //uint16_t attr_type = *p_data++ + (*p_data++ << 8);
        uint16_t attr_type = *p_data++;
        attr_type += (*p_data++ << 8);
        ESP_LOGI(TAG, "GENIE_OP_ATTR_INDICATE attr_type:0x%x", attr_type);
        if (attr_type == GENIE_MODEL_ATTR_DEVICE_EVENT) {
            uint8_t event_id = *p_data;
            switch (event_id)
            {
                case EVENT_DEV_UP:
                    poweron_indicate_start(); // When receiving genie's device up status, indication device's state and device up event in sequence
                break;
                default:
                break;
            }
        }
    }
    break;

    case GENIE_OP_ATTR_GET_STATUS: 
    {
        uint16_t attr_type = *p_data++;
        attr_type += (*p_data++ << 8);
        ESP_LOGI(TAG, "attr_type:0x%x", attr_type);
    }
    break;

    case GENIE_OP_ATTR_SET_ACK: 
    {
        uint16_t attr_type = *p_data++;
        attr_type += (*p_data++ << 8);
        ESP_LOGI(TAG, "GENIE_OP_ATTR_SET_ACK attr_type: 0x%04x", attr_type);

        if (attr_type == TIMER_ATTR_UNIX_TIME) {
            uint32_t unix_time = (p_data[0]) | (p_data[1] << 8) | (p_data[2] << 16) | (p_data[3] << 24);
            p_data += 4;
            genie_timer_operate_status(p_msg->tid, attr_type, unix_time);
            qm_rtc_set_timestamp(&rtc_dev, unix_time);
        }else{
            genie_event(GENIE_EVT_SDK_XJIANG_IOT_SET, (void *)p_msg);
        }
    }
    break;

    default:
    break;
    }

    return 0;
}

#ifdef CONFIG_MESH_MODEL_VENDOR_SRV
void genie_standart_indication(elem_state_t *p_elem)
{
    genie_timer_unixtime_indicate();
}
#endif

int16_t genie_mesh_msg_send(genie_model_msg_t *p_vendor_msg)
{
    
    int16_t r    = -1;
    uint8_t opid = 0;

    if (!p_vendor_msg) {
        return r;
    }

    // vnedor confirm message contains 0 extra data except opid
    if (!p_vendor_msg->data && !p_vendor_msg->len) {
        return r;
    }

    opid = p_vendor_msg->opid;
    switch (opid) {
        case GENIE_OP_ATTR_STATUS:
        case GENIE_OP_ATTR_INDICATE:
        case GENIE_OP_ATTR_INDICATE_TG:
        case GENIE_OP_ATTR_TRANS_MSG:
            genie_model_msg_send(p_vendor_msg);
            break;
        default:
            ESP_LOGW(TAG, "unknown opid:0x%x", opid);
        break;
    }
    return 0;
}

#define RECV_MSG_TID_QUEUE_SIZE 10

#define TMALL_GENIE_UADDR_START 0x0001
#define TMALL_GENIE_UADDR_END   0x0010

typedef struct {
    uint8_t tid;
    uint16_t addr;
    uint32_t time;
} _TID_QUEUE_S;

_TID_QUEUE_S tid_queue[RECV_MSG_TID_QUEUE_SIZE];

genie_error_type_t genie_msg_check_tid(uint16_t src_addr, uint8_t tid)
{
    
    static   uint8_t cur_index = 0;
    uint8_t  i                 = cur_index;
    uint8_t  ri                = 0;
    uint32_t cur_time          = qm_now_ms() / 1000;
    uint32_t end_time          = 0;

    if (src_addr >= TMALL_GENIE_UADDR_START && src_addr <= TMALL_GENIE_UADDR_END) {
        src_addr = TMALL_GENIE_UADDR_START;
    }

    while (i < cur_index + RECV_MSG_TID_QUEUE_SIZE) {
        ri = i % RECV_MSG_TID_QUEUE_SIZE;
        if (tid_queue[ri].tid == tid && tid_queue[ri].addr == src_addr) {
            end_time = tid_queue[ri].time + 10000;
            if (cur_time < end_time) {
                break;
            }
        }
        i++;
    }
    if (i < cur_index + RECV_MSG_TID_QUEUE_SIZE) {
        return GENIE_MESH_TID_REPEAT_ERROR;
    } else {
        tid_queue[cur_index].tid  = tid;
        tid_queue[cur_index].addr = src_addr;
        tid_queue[cur_index].time = cur_time;
        cur_index++;
        cur_index %= RECV_MSG_TID_QUEUE_SIZE;
        return GENIE_MESH_SUCCESS;
    }
}

void genie_mesh_init(void)
{
    ESP_LOGI(TAG, "Initializing genie mesh...");
}
