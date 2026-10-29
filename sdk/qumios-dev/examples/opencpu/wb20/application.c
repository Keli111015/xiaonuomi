#include "id_map.h"
#include "iot_generic.h"

#include "qm.h"
#include "qm_at.h"
#include "qm_log.h"
#include "qm_kernel.h"
#include "qm_kv.h"

#include "gpio_irq_api.h"


#include "air_match_handle.h"
#include "air_func_handle.h"

#include "uart_factory.h"


#define  LOG_TAG

//二位数组列数，用来存放空调默认状态
//下发格式：("开关,模式,温度,风速")
//         ("true,1,20,1")
//         ("false,1,20,1")
#define N 10

#define GPIO_NET_LED_PIN PA_19
#define GPIO_RESET_PIN   PA_17
#define GPIO_LIGHT_MODE_PIN PA_18

#define RESET_KEY_STATUS_PRESS      0
#define RESET_KEY_STATUS_RELEASE    1

#define DEVICE_MODEL    "qjiang.acpartner.wb20"
#define DEVICE_PID      17765


void *iot_handle = NULL;
static void *prodtst_handle = NULL;

qm_timer_t g_reset_key_timer;
qm_timer_t g_net_led_timer;

enum net_led_status
{
    LED_ON_STATUS = 1,
    LED_OFF_STATUS = 0,
    NULL_STATUS = 0,
    CONNECTED_CLOUD_STATUS = 1,
    WIFI_CONFIG_STATUS = 2,
    IR_SEND_STATUS = 3
};

static air_type_buf_temp[512] = {0};
static uint16_t air_type_buf_string1_len = 0;
static uint16_t air_type_buf_string2_len = 0;
static uint16_t air_type_buf_string3_len = 0;
enum net_led_status g_net_led_status = NULL_STATUS;
static uint8_t g_led_status = LED_OFF_STATUS;

static int iot_hook_set(qm_tlv_ctx_t *tlv_ctx, void *ctx);
static int iot_hook_get(qm_tlv_ctx_t *tlv_ctx, void *ctx);
static int iot_hook_action(uint8_t aid, qm_tlv_ctx_t *action, qm_tlv_ctx_t *action_rsp, void *ctx);
static int miio_map_parse_set(iot_map_ctx_t *map_ctx, uint8_t attr_id, uint8_t type_id,
                              uint8_t siid, uint8_t server_type, uint16_t xiid);

static void net_led_timer_cb(qm_timer_t *timer, void *arg);
static void reset_key_timer_cb(qm_timer_t *timer, void *arg);
void app_gpio_net_led_ctrl(uint8_t flag);
static void timer_timeout_handler(uint32_t id);
void app_attr_delay_report(void);
 int user_report_charact(void *handle, uint8_t tag, uint32_t data);
void device_reset(void);
 //初始化上报
qm_timer_t g_report_init_timer = {0};
qm_timer_t g_report_delay_timer = {0};
extern uint8_t ir_send_task_init_flag;

qm_timer_t g_ir_send_delay_timer = {0};

extern void air_func_clear(void);
extern void air_info_clear(void);
extern void air_type_clear(void);

typedef struct app_gpio
{
    gpio_t net_led_pin;
    gpio_t reset_pin;
    gpio_irq_t gpio_irq;

    gpio_t light_led_pin;
    
}app_gpio_t;

static app_gpio_t g_app_gpio;
uint32_t air_info_buf_temp[4] = {0};
qm_timer_t g_ble_scan_timer = {0};
qm_timer_t g_ble_stop_scan_timer = {0};
qm_timer_t g_air_ir_send_timer = {0};

void qm_timer_stop_ble_scan( qm_timer_t *timer, void *arg)
{
    arch_gap_scan_stop();
    qm_printf("---------------------qm_timer_stop_ble_scan = stop-----------------------\r\n");
    if(g_ble_stop_scan_timer.hdl != NULL)
    {
        qm_timer_free(&g_ble_stop_scan_timer);
        g_ble_stop_scan_timer.hdl = NULL;
    }
}

void qm_timer_start_ble_scan( qm_timer_t *timer, void *arg)
{
    arch_gap_scan_start();
    printf("-------------------arch_gap_scan_start_1---------------\r\n");
    qm_timer_new(&g_ble_stop_scan_timer, qm_timer_stop_ble_scan, NULL, 5000, 0);
    qm_timer_start(&g_ble_stop_scan_timer);
    if(g_ble_scan_timer.hdl != NULL)
    {
        qm_timer_free(&g_ble_scan_timer);
        g_ble_scan_timer.hdl = NULL;
    }
}

void qm_timer_air_ir_send_test( qm_timer_t *timer, void *arg)
{
    static uint8_t flag = 0;
    if(flag == 0)
    {
        flag = 1;
        pwm_start_ctrl(1);
        qm_timer_change(&g_air_ir_send_timer, 100);
    }
    else
    {
        flag = 0;
        pwm_start_ctrl(0);
        qm_timer_change(&g_air_ir_send_timer, 900);
    }
}

void factory_test(void)
{
    static uint8_t init = 0;
    if(init == 0)
    {
        init = 1;
    }
    else
    {
        return;
    }
    // arch_gap_scan_stop();
    printf("---------------------qm_timer_stop_ble_scan = stop-----------------------\r\n");
    // if(g_ble_stop_scan_timer.hdl != NULL)
    // {   
    //     qm_timer_stop(&g_ble_stop_scan_timer);
    //     qm_timer_free(&g_ble_stop_scan_timer);
    //     g_ble_stop_scan_timer.hdl = NULL;
    // }
    qm_timer_change(&g_ble_stop_scan_timer, 10);

    if(g_ble_scan_timer.hdl != NULL)
    {
        qm_timer_stop(&g_ble_scan_timer);
        qm_timer_free(&g_ble_scan_timer);
        g_ble_scan_timer.hdl = NULL;
    }

    if(g_air_ir_send_timer.hdl == NULL)
    {
        qm_timer_new(&g_air_ir_send_timer, qm_timer_air_ir_send_test, NULL, 1000, 0);
        qm_timer_start(&g_air_ir_send_timer);
    }

}


static void iot_hook_event_notify(iot_generic_event_t event, void *ctx)
{

   qm_printf("---------------------iot_hook_event_notify = %d-----------------------\r\n", event);
    switch (event)
    {
    case IOT_GENERIC_EVENT_WIFI_CONFIG:  // 配网
#ifdef LOG_TAG
        QM_LOGD(LOG_TAG, "IOT_GENERIC_EVENT_WIFI_CONFIG");
#endif
        g_net_led_status = WIFI_CONFIG_STATUS;
        app_gpio_net_led_ctrl(0);

        g_led_status = LED_OFF_STATUS;
        qm_timer_change(&g_net_led_timer, 10);

        qm_timer_new(&g_ble_scan_timer, qm_timer_start_ble_scan, NULL, 7000, 0);
        qm_timer_start(&g_ble_scan_timer);
		
        break;
    case IOT_GENERIC_EVENT_CONNECTING_ROUTER:
#ifdef LOG_TAG
        QM_LOGD(LOG_TAG, "IOT_GENERIC_EVENT_CONNECTING_ROUTER");
#endif
        break;
    case IOT_GENERIC_EVENT_CONNECTED_ROUTER:
#ifdef LOG_TAG
        QM_LOGD(LOG_TAG, "IOT_GENERIC_EVENT_CONNECTED_ROUTER");
#endif
        break;
    case IOT_GENERIC_EVENT_CONNECTED_CLOUD:  // 连接到云端
#ifdef LOG_TAG
        QM_LOGD(LOG_TAG, "IOT_GENERIC_EVENT_CONNECTED_CLOUD");
#endif
        g_net_led_status = CONNECTED_CLOUD_STATUS;
        app_gpio_net_led_ctrl(0);
        g_led_status = LED_OFF_STATUS;
        qm_timer_change(&g_net_led_timer, 10);

        app_attr_delay_report();

        if(air_func_get(AIR_FUNC_REG_STATUS) == 0)
        {
            air_func_set(AIR_FUNC_REG_STATUS, 1);
        }

        break;
    case IOT_GENERIC_EVENT_CONFIG_TIMEOUT:
#ifdef LOG_TAG
        QM_LOGD(LOG_TAG, "IOT_GENERIC_EVENT_CONFIG_TIMEOUT");
#endif
        g_net_led_status = NULL_STATUS;
        app_gpio_net_led_ctrl(0);
        g_led_status = LED_OFF_STATUS;
        break;
    case IOT_GENERIC_EVENT_DEVICE_REGISTERED:
        QM_LOGD(LOG_TAG, "IOT_GENERIC_EVENT_DEVICE_REGISTERED");
        break;
    case IOT_GENERIC_EVENT_DEVICE_UNREGISTER:
        device_reset();
        break;
    default:
        break;
    }
}


void air_get_all_status(char *status, char len, char arr[][N])
{
    if(status == NULL || len == 0)
    {
        return;
    }

    char i = 0;
    char j = 0;
    while(*status)
    {
        if(*status == ',')
        {
            arr[i][j] = '\0';
            i++;
            j = 0;
        }
        else if(*status != ' ')
        {
            arr[i][j] = *status;
            j++;
        }
        status++;
    }
}


/**
 * 字符串拼接
 * target: 目标数组
 * target_length: 目标数组长度
 * target_index: 从目标数组的那个位置开始拼接
 * source: 源数组
 * source_length: 源数组长度
 * **/
void array_concatenate(char* target, int target_length, int target_index, char* source, int source_length) 
{  
    // 确保目标数组有足够的空间来存储源数组  
    if (target_index + source_length > target_length) {  
        qm_printf("目标数组没有足够的空间来存储源数组\n");  
        return;  
    }  
  
    // 进行拼接  
    for (int i = 0; i < source_length; i++) {  
        target[target_index + i] = source[i];  
    }  
}  
static uint8_t report_flag = 0;
uint8_t queue_buf[4] = {0};
static int iot_hook_set(qm_tlv_ctx_t *tlv_ctx, void *ctx)
{
    
    
    qm_tlv_block_t *tlv_block = NULL;
    qm_tlv_block_t *tlv_block_report = NULL;
    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    while (tlv_block)
    {
        switch (tlv_block->type)
        {
            case QM_DTTV_TYPE_BOOL:
            {
                bool_t value;
                qm_tlv_unpack_bool(tlv_block, &value);
#ifdef LOG_TAG
                QM_LOGD(LOG_TAG, "[iot_hook_set] XJIANG attr_id %d type %d value %d\r\n", tlv_block->tag, tlv_block->type, value);
#endif
                qm_printf("[iot_hook_set] XJIANG attr_id %d type %d value %d\r\n", tlv_block->tag, tlv_block->type, value);
                if(tlv_block->tag == AIR_OPEN_CLOSE_TAG)    //开关
                {
                    if(value == 0) value = 1;
                    else value = 0;
                    air_func_set(AIR_FUNC_SWITCH, value);
                    air_func_current_operation_set(AIR_KEY_SWITCH);
                    report_flag = 1;
                }
                else if(tlv_block->tag == AIR_WIND_UPDOWN_TAG) //上下摆风
                {
                    air_func_set(AIR_FUNC_WIND_DIRECTION_SWITCH, value);
                    if(value == 0)
                    {
                        air_func_current_operation_set(AIR_KEY_WIND_DIRECTION);  //定向风
                    }
                    else
                    {
                        air_func_current_operation_set(AIR_KEY_SWEPT_WING); //扫风
                    }
                    
                    report_flag = 1;
                }
                else if(tlv_block->tag == AIR_TIPS_LED_TAG) //指示灯开关
                {
                    air_func_set(AIR_FUNC_TIPS_LED, value);
                }
                else if(tlv_block->tag == AIR_LIGHT_SWITCH_TAG) //灯光开关
                {
                    air_func_set(AIR_FUNC_LIGHT_SWITCH, value);
                    if(value == 0)
                    {
                        light_brightness_close();
                    }
                    else
                    {
                        light_brightness_set();
                    }
                    
                }
            }
            break;
            case QM_DTTV_TYPE_UINT8:
            {
                uint8_t value;
                qm_tlv_unpack_uint8(tlv_block, &value);

                qm_printf("[iot_hook_set] XJIANG attr_id %d type %d value %d\r\n", tlv_block->tag, tlv_block->type, value);
                if(tlv_block->tag == AIR_MATCH_STATUS_TAG)  //配对状态
                {
                    if(qm_kv_set(IR_MATCH_STATUS_KEY, &value, sizeof(uint8_t), 1) != QM_EOK)
                    {
                        qm_printf("qm_kv_set IR_MATCH_STATUS_KEY fail!\r\n");
                        return;
                    }
                    qm_printf("qm_kv_set IR_MATCH_STATUS_KEY success!\r\n");
                    if(value == 1)
                    {
                        memset(ir_data_get()->air_type_data, 0, sizeof(ir_data_get()->air_type_data));
                        memcpy(ir_data_get()->air_type_data, ir_data_get()->air_type_data_temp, sizeof(ir_data_get()->air_type_data));
                        if(qm_kv_set(IR_PERIOD_KEY, &ir_data_get()->ir_pwmout.ir_pwm_period, sizeof(float), 1) != QM_EOK)
                        {
                            qm_printf("qm_kv_set IR_PERIOD_KEY fail!\r\n");
                            return;
                        }
                        qm_printf("qm_kv_set IR_PERIOD_KEY success!\r\n");

                        if(qm_kv_set(IR_TYPE_DATA_KEY, ir_data_get()->air_type_data, 512*3, 1) != QM_EOK)
                        {
                            qm_printf("qm_kv_set IR_TYPE_DATA_KEY fail!\r\n");
                            return;
                        }
                        qm_printf("qm_kv_set IR_TYPE_DATA_KEY success!\r\n");

                        air_func_set(AIR_FUNC_TEMP, DEFAULT_FUNC_TEMP);
                        air_func_set(AIR_FUNC_MODE, DEFAULT_FUNC_MODE);
                        air_func_set(AIR_FUNC_WIND_SPEED, 0);
                        air_func_set(AIR_FUNC_WIND_DIRECTION, 0);
                        air_func_set(AIR_FUNC_SWITCH, 0);
                        air_func_current_operation_set(AIR_KEY_WIND_DIRECTION); //定向风

                        if(g_report_delay_timer.hdl != NULL)
                            qm_timer_change(&g_report_delay_timer, 1000);
                    }
                }
                else if(tlv_block->tag == AIR_MODE_TAG)  //模式
                {
                    air_func_set(AIR_FUNC_MODE, value);
                    air_func_current_operation_set(AIR_KEY_MODE);
                    report_flag = 1;
                }
                else if(tlv_block->tag == AIR_TEMP_TAG) //温度
                {
                    air_func_set(AIR_FUNC_TEMP, value);
                    if(air_func_get(AIR_FUNC_TEMP) >= value)  //减小温度
                    {
                        air_func_current_operation_set(AIR_KEY_TEMP_SUB);
                    }
                    else
                    {
                        air_func_current_operation_set(AIR_KEY_TEMP_ADD);
                    }
                    report_flag = 1;
                }
                else if(tlv_block->tag == AIR_WIND_SPEED_TAG) //风速
                {
                    air_func_set(AIR_FUNC_WIND_SPEED, value);
                    air_func_current_operation_set(AIR_KEY_WIND_SPEED);
                    report_flag = 1;
                }
                else if(tlv_block->tag == AIR_LIGHT_BRIGHTNESS_TAG) //灯光亮度
                {
                    air_func_set(AIR_FUNC_LIGHT_BRIGHTNESS, value);
                    light_brightness_set();
                }
            }
            break;
            case QM_DTTV_TYPE_STRING:
            {
                int size = 0;
                char status[4][N] = {0};
                char value[15] = {0};
                qm_tlv_unpack_string(tlv_block, value, &size);
                qm_printf("[iot_hook_set] XJIANG attr_id %d type %d value %s len = %d\r\n", tlv_block->tag, tlv_block->type, value, size);
                if(tlv_block->tag == AIR_SET_ALL_STATUS_TAG) //设置所有状态 开关,模式,温度,风速(true/false/,1,20,1  bool,uint8,uint8,uint8)
                {
                    air_get_all_status(value, size, status);
                    qm_printf("status[0] = %s, status[1] = %s, status[2] = %s, status[3] = %s\r\n", status[0], status[1], status[2], status[3]);
                    if(strncmp(status[0], "true", 4) == 0)
                    {
                        air_func_set(AIR_FUNC_SWITCH, 0);
                    }
                    else
                    {
                        air_func_set(AIR_FUNC_SWITCH, 1);
                    }
                    user_report_charact(iot_handle, AIR_OPEN_CLOSE_TAG, air_func_get(AIR_FUNC_SWITCH));

                    if(atoi(status[1]) >= 0 && atoi(status[1]) <= 4)  //模式有0-4
                    {
                        air_func_set(AIR_FUNC_MODE, atoi(status[1]));
                        user_report_charact(iot_handle, AIR_MODE_TAG, air_func_get(AIR_FUNC_MODE));
                    }

                    if(atoi(status[2]) >= 16 && atoi(status[2]) <= 32) //温度有16-30
                    {
                        air_func_set(AIR_FUNC_TEMP, atoi(status[2]));
                        user_report_charact(iot_handle, AIR_TEMP_TAG, air_func_get(AIR_FUNC_TEMP));
                    }

                    if(atoi(status[3]) >= 0 && atoi(status[3]) <= 3)    //风速有0-3
                    {
                        air_func_set(AIR_FUNC_WIND_SPEED, atoi(status[3]));
                        user_report_charact(iot_handle, AIR_WIND_SPEED_TAG, air_func_get(AIR_FUNC_WIND_SPEED));
                    }

                    air_func_current_operation_set(AIR_KEY_SWITCH);
                    report_flag = 1;
                }

            }

            break;
            default:
                break;
        }

         tlv_block_report = qm_tlv_block_creat();
        if (tlv_block_report == NULL)
        {
            return -QM_ENOMEM;
        }
        qm_tlv_block_copy(tlv_block_report, tlv_block);
        iot_generic_report(iot_handle, tlv_block_report); // 内部会自行释放 tlv_block_report
        queue_buf[0] = 1;
        qm_timer_change(&g_ir_send_delay_timer, 500);
        
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);

    }

    return QM_EOK;
}

static int iot_hook_get(qm_tlv_ctx_t *tlv_ctx, void *ctx)
{
    
    qm_tlv_block_t *tlv_block = NULL;
    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    while (tlv_block)
    {
#ifdef LOG_TAG
        QM_LOGD(LOG_TAG, "[iot_hook_get] XJIANG attr_id %d ", tlv_block->tag);
#endif

        switch(tlv_block->tag)
        {
            case AIR_OPEN_CLOSE_TAG:
            {
                uint8_t value = air_func_get(AIR_FUNC_SWITCH);
                qm_tlv_pack_bool(tlv_block, value);
                QM_LOGD(LOG_TAG, "[iot_hook_get] AIR_OPEN_CLOSE_TAG value %d ", value);
            }
            break;
            case AIR_MODE_TAG:
            {
                uint8_t value = air_func_get(AIR_FUNC_MODE);
                qm_tlv_pack_uint8(tlv_block, value);
                QM_LOGD(LOG_TAG, "[iot_hook_get] AIR_MODE_TAG value %d ", value);
            }
            break;
            case AIR_TEMP_TAG:
            {
                uint8_t value = air_func_get(AIR_FUNC_TEMP);
                qm_tlv_pack_uint8(tlv_block, value);
                QM_LOGD(LOG_TAG, "[iot_hook_get] AIR_TEMP_TAG value %d ", value);
            }
            break;
            case AIR_WIND_SPEED_TAG:
            {
                uint8_t value = air_func_get(AIR_FUNC_WIND_SPEED);
                qm_tlv_pack_uint8(tlv_block, value);
                QM_LOGD(LOG_TAG, "[iot_hook_get] AIR_WIND_SPEED_TAG value %d ", value);
            }
            break;
            case AIR_WIND_UPDOWN_TAG:
            {
                uint8_t value = air_func_get(AIR_FUNC_WIND_DIRECTION_SWITCH);
                qm_tlv_pack_bool(tlv_block, value);
                QM_LOGD(LOG_TAG, "[iot_hook_get] AIR_WIND_UPDOWN_TAG value %d ", value);
            }
            break;
            case AIR_TIPS_LED_TAG:  //指示灯开关
            {
                uint8_t value = air_func_get(AIR_FUNC_TIPS_LED);
                qm_tlv_pack_bool(tlv_block, value);
                QM_LOGD(LOG_TAG, "[iot_hook_get] AIR_TIPS_LED_TAG value %d ", value);
            }
            break;
            case AIR_MATCH_STATUS_TAG:
            {
                uint8_t value = 0;
                size_t len = sizeof(uint8_t);
                if(qm_kv_get(IR_MATCH_STATUS_KEY, &value, &len) != QM_EOK)
                {
                    qm_printf("qm_kv_get IR_MATCH_STATUS_KEY fail!\r\n");
                    return;
                }
                qm_printf("qm_kv_get IR_MATCH_STATUS_KEY success!\r\n");
                qm_tlv_pack_uint8(tlv_block, value);
                QM_LOGD(LOG_TAG, "[iot_hook_get] AIR_MATCH_STATUS_TAG value %d ", value);
            }
            break;
            case AIR_REMOTE_ID_TAG:
            {
                uint32_t value = air_info_get(AIR_REMOTE_ID);
                qm_tlv_pack_uint32(tlv_block, value);
                QM_LOGD(LOG_TAG, "[iot_hook_get] AIR_REMOTE_ID_TAG value %d ", value);
            }
            break;
            case AIR_BRAND_ID_TAG:
            {
                uint32_t value = air_info_get(AIR_BRAND);
                qm_tlv_pack_uint32(tlv_block, value);
                QM_LOGD(LOG_TAG, "[iot_hook_get] AIR_BRAND_ID_TAG value %d ", value);
            }
            break;
            case AIR_TYPE_TAG:
            {
                uint32_t value = air_info_get(AIR_TYPE);
                qm_tlv_pack_uint8(tlv_block, (uint8_t)value);
                QM_LOGD(LOG_TAG, "[iot_hook_get] AIR_TYPE_TAG value %d ",value);
            }
            break;
            case AIR_LIGHT_SWITCH_TAG:
            {
                uint8_t value = air_func_get(AIR_FUNC_LIGHT_SWITCH);
                qm_tlv_pack_bool(tlv_block, value);
                QM_LOGD(LOG_TAG, "[iot_hook_get] AIR_LIGHT_SWITCH_TAG value %d ", value);
            }
            break;
            case AIR_LIGHT_BRIGHTNESS_TAG:
            {
                uint8_t value = air_func_get(AIR_FUNC_LIGHT_BRIGHTNESS);
                qm_tlv_pack_uint8(tlv_block, value);
                QM_LOGD(LOG_TAG, "[iot_hook_get] AIR_LIGHT_BRIGHTNESS_TAG value %d ", value);
            }
            break;
            case AIR_LIGHT_MODE_TAG:
            {
                uint8_t value = air_func_get(AIR_FUNC_LIGHT_MODE);
                qm_tlv_pack_uint8(tlv_block, value);
                QM_LOGD(LOG_TAG, "[iot_hook_get] AIR_LIGHT_MODE_TAG value %d ", value);
            }

            default:
                break;
        }
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }

    return QM_EOK;
}

static int iot_hook_action(uint8_t aid, qm_tlv_ctx_t *action, qm_tlv_ctx_t *action_rsp, void *ctx)
{
#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "[iot_hook_action] aid : %d", aid);
    if(aid ==  AIR_MATCH_OPEN_TAG)
    {
        queue_buf[0] = 0;
        report_flag = 1;
        ir_data_get()->ir_pwmout.ir_pwm_is_init = 0;
        // qm_queue_send(&ir_data_get()->ir_queue.ir_send_queue,queue_buf, 1);
        qm_timer_change(&g_ir_send_delay_timer, 500);
        
    }
    else if(aid == AIR_SWITCH_STATUS_CHANGE_TAG)
    {
        QM_LOGD(LOG_TAG, "[iot_hook_action] aid : %d AIR_SWITCH_STATUS_CHANGE_TAG", aid);
        air_func_get(AIR_FUNC_SWITCH) == 0 ? air_func_set(AIR_FUNC_SWITCH, 0) : air_func_set(AIR_FUNC_SWITCH, 1);
        air_func_current_operation_set(AIR_KEY_SWITCH);
        queue_buf[0] = 1;
         report_flag = 1;
        // qm_queue_send(&ir_data_get()->ir_queue.ir_send_queue,queue_buf, 1);
        qm_timer_change(&g_ir_send_delay_timer, 500);
        user_report_charact(iot_handle, AIR_OPEN_CLOSE_TAG, air_func_get(AIR_FUNC_SWITCH));
    }
    else if(aid == AIR_WIND_DIRECT_TAG)
    {
        QM_LOGD(LOG_TAG, "[iot_hook_action] aid : %d AIR_WIND_DIRECT_TAG", aid);
        air_func_set(AIR_FUNC_WIND_DIRECTION_SWITCH, 0); //上下摆风关闭
        user_report_charact(iot_handle, (uint8_t)AIR_WIND_UPDOWN_TAG, (uint32_t)0); //自动扫风关闭
        air_func_current_operation_set(AIR_KEY_WIND_DIRECTION); //扫风
        queue_buf[0] = 1;
         report_flag = 1;
        // qm_queue_send(&ir_data_get()->ir_queue.ir_send_queue,queue_buf, 1);
        qm_timer_change(&g_ir_send_delay_timer, 500);
    }
#endif
    int out_index = 0;
    qm_tlv_block_t *tlv_block_in = NULL;
    qm_tlv_block_t *tlv_block_out = NULL;
    tlv_block_in = qm_tlv_block_next(action, NULL);
    while (tlv_block_in)
    {
        QM_LOGD(LOG_TAG, "[iot_hook_action] tlv_block_in->type : %d", tlv_block_in->type);
        switch (tlv_block_in->type)
        {
        case QM_DTTV_TYPE_UINT8:
        {
            uint8_t value;
            qm_tlv_unpack_uint8(tlv_block_in, &value);
            QM_LOGD(LOG_TAG, "[iot_hook_action_IN] attr_id %d type %d value %d\r\n", tlv_block_in->tag, tlv_block_in->type, value);
            qm_printf("[iot_hook_set] tag = %d, send queue AIR_OPEN\r\n", aid);
           
            if(aid == AIR_INFO_TAG)
            {
                if(tlv_block_in->tag == AIR_TYPE_TAG)
                {
                    air_info_set(AIR_TYPE, value);
                }
            }
        }
        break;

        case QM_DTTV_TYPE_UINT32:
        {
            if(aid == AIR_INFO_TAG) //ir_type_data
            {
                uint32_t value;
                qm_tlv_unpack_uint32(tlv_block_in, &value);
                if(tlv_block_in->tag == AIR_BRAND_ID_TAG)
                {
                    air_info_set(AIR_BRAND, value);
                }
                else if(tlv_block_in->tag == AIR_REMOTE_ID_TAG)
                {
                    air_info_set(AIR_REMOTE_ID, value);
                }

#ifdef LOG_TAG
                QM_LOGD(LOG_TAG, "[iot_hook_action_IN] attr_id %d type %d value %d", tlv_block_in->tag, tlv_block_in->type, value);
#endif
            }
        }
        break;

        case QM_DTTV_TYPE_STRING: // &ir_data_get()->air_type_data_temp
        {
            int len = sizeof(air_type_buf_temp);
            memset(air_type_buf_temp, 0, sizeof(air_type_buf_temp));
            qm_tlv_unpack_string(tlv_block_in, air_type_buf_temp, &len);
#ifdef LOG_TAG
            QM_LOGD(LOG_TAG, "[iot_hook_action_IN] attr_id %d type %d value %s len = %d\r\n", tlv_block_in->tag, tlv_block_in->type, air_type_buf_temp,\
                             len);
            
#endif
            if(tlv_block_in->tag == AIR_MATCH_TYPE_STRING1_TAG)
            {
                air_type_buf_string1_len = len;
                array_concatenate(ir_data_get()->air_type_data_temp, sizeof(ir_data_get()->air_type_data_temp), 0, air_type_buf_temp, len);
            }
            else if(tlv_block_in->tag == AIR_MATCH_TYPE_STRING2_TAG)
            {
                air_type_buf_string2_len = len;
                array_concatenate(ir_data_get()->air_type_data_temp, sizeof(ir_data_get()->air_type_data_temp), air_type_buf_string1_len-1, air_type_buf_temp, len);
            }
            else if(tlv_block_in->tag == AIR_MATCH_TYPE_STRING3_TAG)
            {
                air_type_buf_string3_len = len;
                array_concatenate(ir_data_get()->air_type_data_temp, sizeof(ir_data_get()->air_type_data_temp), \
                    air_type_buf_string1_len + air_type_buf_string2_len-2, air_type_buf_temp, len);

                
                QM_LOGD(LOG_TAG, "air_type_data_temp: %s len = %d\r\n", ir_data_get()->air_type_data_temp, \
                                                                        strlen(ir_data_get()->air_type_data_temp));
            }

            if(ir_send_task_init_flag == 0)
            {
                app_ir_send_task_init();
            }
           
        }
        break;

        default:
            break;
        }
        tlv_block_in = qm_tlv_block_next(action, tlv_block_in);
    }
    return QM_EOK;
}

static iot_generic_hooks_t mi_hook = {
    .set = iot_hook_set,
    .get = iot_hook_get,
#if CONFIG_IOT_GENERIC_ACTION_SUPPORT
    .action = iot_hook_action,
#endif
    .event_notify = iot_hook_event_notify,
    .ctx = NULL,
};

static int miio_map_parse_set(iot_map_ctx_t *map_ctx, uint8_t attr_id, uint8_t type_id,
                              uint8_t siid, uint8_t server_type, uint16_t xiid)

{
    iot_map_mi_t *map_mi = NULL;
    if (map_ctx == NULL)
    {
        return -QM_EINVAL;
    }

    map_mi = (iot_map_mi_t *)iot_generic_map_get((void *)map_ctx);
    if (map_mi == NULL)
    {
        return;
    }

    map_mi->type_id = type_id;
    map_mi->attr_id = attr_id;
    map_mi->siid = siid;
    map_mi->type = server_type;
    map_mi->xiid = xiid;

    return QM_EOK;
}


static void app_device_reset(void *arg)
{
    // uint8_t times = 0;
    uint8_t dir = 0;
    while(1)
    {
        // times++;
        qm_msleep(100);
        if(gpio_read(&g_app_gpio.reset_pin) == 0 && dir == 0) //按下去了
        {
            dir = 1;
            qm_timer_change(&g_reset_key_timer, 8000);
        }
        else if(gpio_read(&g_app_gpio.reset_pin) == 1 && dir == 1)
        {
            dir = 0;
            qm_timer_stop(&g_reset_key_timer);
        }

        // if(times >= 10)
        // {
        //     times = 0;
        //     qm_printf("app_device_reset->!->%d\r\n", gpio_read(&g_app_gpio.reset_pin));
        // }
    }
}


qm_task_t g_app_device_reset = {0};
void app_gpio_init(void)
{
    //网络指示灯初始化 PA20
    gpio_init(&g_app_gpio.net_led_pin, GPIO_NET_LED_PIN);
    gpio_dir(&g_app_gpio.net_led_pin, PIN_OUTPUT);
    gpio_mode(&g_app_gpio.net_led_pin, PullDown);
    qm_timer_new(&g_net_led_timer, net_led_timer_cb, NULL, 200, 0);

    //重置按键初始化    PA17
    gpio_init(&g_app_gpio.reset_pin, GPIO_RESET_PIN);
    gpio_dir(&g_app_gpio.reset_pin, PIN_INPUT);
    gpio_mode(&g_app_gpio.reset_pin, PullUp);
    qm_task_new(&g_app_device_reset, "app_device_reset", app_device_reset, NULL, 1*1024, 6);
    qm_timer_new(&g_reset_key_timer, reset_key_timer_cb, NULL, 8000, 0);


    //照明选择io初始化
    gpio_init(&g_app_gpio.light_led_pin, GPIO_LIGHT_MODE_PIN);
    gpio_dir(&g_app_gpio.light_led_pin, PIN_INPUT);
    gpio_mode(&g_app_gpio.light_led_pin, PullUp);  //上拉
    if(gpio_read(&g_app_gpio.light_led_pin) == 0) //低电平，有照明功能
    {
        air_func_set(AIR_FUNC_LIGHT_MODE, 1);
        
    }
    else
    {
        air_func_set(AIR_FUNC_LIGHT_MODE, 0); 
    }
    qm_printf("AIR_FUNC_LIGHT_MODE = %d\r\n", gpio_read(&g_app_gpio.light_led_pin));
}

void app_gpio_net_led_ctrl(uint8_t flag)
{
    if(air_func_get(AIR_FUNC_TIPS_LED) == 0)
        return;
    if (flag)
    {
        gpio_write(&g_app_gpio.net_led_pin, 1);
    }
    else
    {
        gpio_write(&g_app_gpio.net_led_pin, 0);
    }
}

void device_reset(void)
{
    air_func_clear();
    air_type_clear();
    
    air_info_clear();
    if(gpio_read(&g_app_gpio.light_led_pin) == 0) //低电平，有照明功能
    {
        air_func_set(AIR_FUNC_LIGHT_SWITCH, 1);
        air_func_set(AIR_FUNC_LIGHT_BRIGHTNESS, 100);
    }
    else
    {
        air_func_set(AIR_FUNC_LIGHT_MODE, 0); 
        air_func_set(AIR_FUNC_LIGHT_SWITCH, 0);
        air_func_set(AIR_FUNC_LIGHT_BRIGHTNESS, 0);
    }
    qm_printf("device_reset success!\r\n");
    qm_msleep(500);

}


static void reset_key_timer_cb(qm_timer_t *timer, void *arg)
{
    if(iot_handle != NULL)
    {
        device_reset();
        iot_generic_reset(iot_handle);

    }
}

static void net_led_timer_cb(qm_timer_t *timer, void *arg)
{
    static uint8_t led_blink_cnt = 0;
    switch(g_net_led_status)
    {
        case NULL_STATUS:
            break;
        case CONNECTED_CLOUD_STATUS:
            if(g_led_status == LED_ON_STATUS)
            {
                app_gpio_net_led_ctrl(0);
                g_led_status = LED_OFF_STATUS;
                led_blink_cnt++;
                if(led_blink_cnt >= 3)
                {
                    app_gpio_net_led_ctrl(0);
                    g_led_status = LED_OFF_STATUS;
                    led_blink_cnt = 0;
                    return;
                }
                else
                {
                    qm_timer_change(&g_net_led_timer, 200);
                    
                }
            }
            else if(g_led_status == LED_OFF_STATUS)
            {
                app_gpio_net_led_ctrl(1);
                g_led_status = LED_ON_STATUS;
                qm_timer_change(&g_net_led_timer, 200);
            }
            else
            {   
                app_gpio_net_led_ctrl(1);
                g_led_status = LED_ON_STATUS;
                qm_timer_change(&g_net_led_timer, 200);
            }
            break;
        case WIFI_CONFIG_STATUS:
            if(g_led_status == LED_ON_STATUS)
            {
                app_gpio_net_led_ctrl(0);
                g_led_status = LED_OFF_STATUS;
                qm_timer_change(&g_net_led_timer, 200);
            }
            else if(g_led_status == LED_OFF_STATUS)
            {
                app_gpio_net_led_ctrl(1);
                g_led_status = LED_ON_STATUS;
                qm_timer_change(&g_net_led_timer, 200);
            }
            else
            {   
                app_gpio_net_led_ctrl(1);
                g_led_status = LED_ON_STATUS;
                qm_timer_change(&g_net_led_timer, 200);
            }
            break;
        case IR_SEND_STATUS:  //发送红外数据，只亮一次
            if(g_led_status == LED_ON_STATUS)
            {
                app_gpio_net_led_ctrl(0);
                g_led_status = LED_OFF_STATUS;
                led_blink_cnt++;
                if(led_blink_cnt >= 1)
                {
                    app_gpio_net_led_ctrl(0);
                    g_led_status = LED_OFF_STATUS;
                    led_blink_cnt = 0;
                }
                else
                    qm_timer_change(&g_net_led_timer, 100);
            }
            else if(g_led_status == LED_OFF_STATUS)
            {
                app_gpio_net_led_ctrl(1);
                g_led_status = LED_ON_STATUS;
                qm_timer_change(&g_net_led_timer, 100);
            }
            else
            {   
                app_gpio_net_led_ctrl(1);
                g_led_status = LED_ON_STATUS;
                 led_blink_cnt++;
                if(led_blink_cnt >= 1)
                {
                    app_gpio_net_led_ctrl(0);
                    g_led_status = LED_OFF_STATUS;
                    led_blink_cnt = 0;
                }
                else
                    qm_timer_change(&g_net_led_timer, 100);
            }
            break;
        default:
            break;
    }
  
}

//发送红外码的时候，led闪烁一次
static void ir_send_func_cb(void *arg)
{
    g_net_led_status = IR_SEND_STATUS;
    app_gpio_net_led_ctrl(0);
    g_led_status = LED_OFF_STATUS;
    qm_timer_change(&g_net_led_timer, 1);
}


/**
 * @brief 属性主动上报
 * @param handle	上报iot句柄
 * @param tag		上报属性标识
 * @param data		需要上报的数据
 * @return 成功返回0，失败返回其他
 * **/
int user_report_charact(void *handle, uint8_t tag, uint32_t data)
{
	uint8_t eid = 0;
	uint8_t sid = 0;
	int ret = (-1);
	if(handle == NULL)  return ret;
	qm_tlv_block_t *tlv_block = qm_tlv_block_creat();
	if(tlv_block == NULL) return (-1);
	switch(tag)
	{
        case AIR_OPEN_CLOSE_TAG: //开关 bool
        case AIR_WIND_UPDOWN_TAG:  //上下摆风
        case AIR_TIPS_LED_TAG: //指示灯开关
        case AIR_LIGHT_MODE_TAG:    //灯光有无
        case AIR_LIGHT_BRIGHTNESS_TAG: //亮度
        case AIR_LIGHT_SWITCH_TAG:  //灯光开关
        {
            uint8_t value = (uint8_t)data;
            qm_tlv_block_data_type_set(tlv_block, QM_DTTV_TYPE_BOOL);
            qm_tlv_pack_bool(tlv_block, value);
            QM_LOGD(LOG_TAG, "[LHSMD] report tag = %d, data = %d !!!\n", tag, value);
        }   
        break;
        case AIR_MODE_TAG:      //uint8_t
        case AIR_FAIL_TAG:
        case AIR_TEMP_TAG:
        case AIR_WIND_SPEED_TAG:
        case AIR_MATCH_STATUS_TAG:
        case AIR_TYPE_TAG: //空调类型 1单一码  2组合码
        {
            uint8_t value = (uint8_t)data;
            qm_tlv_block_data_type_set(tlv_block, QM_DTTV_TYPE_UINT8);
            qm_tlv_pack_uint8(tlv_block, value);
            QM_LOGD(LOG_TAG, "[LHSMD] report tag = %d, data = %d !!!\n", tag, value);
        }

        break;
        case AIR_BRAND_ID_TAG:  //uint32_t
        case AIR_REMOTE_ID_TAG:
        {
            uint32_t value = (uint32_t)data;
            qm_tlv_block_data_type_set(tlv_block, QM_DTTV_TYPE_UINT32);
            qm_tlv_pack_uint32(tlv_block, value);
            QM_LOGD(LOG_TAG, "[LHSMD] report tag = %d, data = %d !!!\n", tag, value);
        }
        break;
		default:
			qm_free(tlv_block);
			return (-1);
	}
	
	qm_tlv_block_tag_set(tlv_block, tag);
	ret = iot_generic_report(handle, tlv_block);
	QM_LOGD(LOG_TAG, "[LHSMD] report ret = %d !!!\n", ret);
	return 0;
}


 

void air_report_all_func_cb(qm_timer_t *timer, void *arg)
{
    static uint8_t report_cnt = 0;
    if(report_cnt == 0)
    {
        user_report_charact(iot_handle, AIR_OPEN_CLOSE_TAG, air_func_get(AIR_FUNC_SWITCH));
        user_report_charact(iot_handle, AIR_MODE_TAG, air_func_get(AIR_FUNC_MODE));
        user_report_charact(iot_handle, AIR_TEMP_TAG, air_func_get(AIR_FUNC_TEMP));
    
        user_report_charact(iot_handle, AIR_WIND_SPEED_TAG, air_func_get(AIR_FUNC_WIND_SPEED));
        user_report_charact(iot_handle, AIR_WIND_UPDOWN_TAG, air_func_get(AIR_FUNC_WIND_DIRECTION_SWITCH));
        user_report_charact(iot_handle, AIR_TIPS_LED_TAG, air_func_get(AIR_FUNC_TIPS_LED));
        report_cnt = 1;
        qm_timer_change(&g_report_delay_timer, 3000);

    }
    else if(report_cnt == 1)
    {

        user_report_charact(iot_handle, AIR_LIGHT_SWITCH_TAG, air_func_get(AIR_FUNC_LIGHT_SWITCH));
        user_report_charact(iot_handle, AIR_LIGHT_BRIGHTNESS_TAG, air_func_get(AIR_FUNC_LIGHT_BRIGHTNESS));
        user_report_charact(iot_handle, AIR_LIGHT_MODE_TAG, air_func_get(AIR_FUNC_LIGHT_MODE));
        report_cnt = 0;
        qm_timer_stop(&g_report_delay_timer);
    }
}
static void app_attr_init_report_cb(qm_timer_t *timer, void *arg)
{
    uint8_t match_status = 0;
    if(iot_handle == NULL) return;
   

    if(qm_kv_set(IR_MATCH_STATUS_KEY, &match_status, sizeof(uint8_t), 1) != QM_EOK)
    {
        qm_printf("qm_kv_set IR_MATCH_STATUS_KEY fail!\r\n");
        return;
    }
    user_report_charact(iot_handle, AIR_MATCH_STATUS_TAG, match_status);

    if(g_report_delay_timer.hdl != NULL)
    {
        qm_timer_stop(&g_report_delay_timer);
        qm_timer_free(&g_report_delay_timer);
    }
    qm_timer_new(&g_report_delay_timer, air_report_all_func_cb, NULL, 1000, 0);
    qm_timer_start(&g_report_delay_timer);

    user_report_charact(iot_handle, AIR_REMOTE_ID_TAG, air_info_get(AIR_REMOTE_ID));
    user_report_charact(iot_handle, AIR_BRAND_ID_TAG, air_info_get(AIR_BRAND));
    user_report_charact(iot_handle, AIR_TYPE_TAG, air_info_get(AIR_TYPE));
    qm_timer_stop(&g_report_init_timer);
    qm_timer_free(&g_report_init_timer);
    g_report_init_timer.hdl = NULL;
}

static void ir_send_delay_timer_cb(qm_timer_t *timer, void *arg)
{
    // uint8_t queue_buf[4] = {0};
    if(report_flag)
    {
        report_flag = 0;
        qm_queue_send(&ir_data_get()->ir_queue.ir_send_queue, queue_buf, 1);
    }
}

void ir_send_delay_timer_init(void)
{
    if(g_ir_send_delay_timer.hdl != NULL)
    {
        qm_timer_stop(&g_ir_send_delay_timer);
        qm_timer_free(&g_ir_send_delay_timer);
    }
    qm_timer_new(&g_ir_send_delay_timer, ir_send_delay_timer_cb, NULL, 400, 0);
    // qm_timer_start(&g_ir_send_delay_timer);

}

//初始化上报
void app_attr_delay_report(void)
{
    if(g_report_init_timer.hdl != NULL)
    {
        qm_timer_stop(&g_report_init_timer);
        qm_timer_free(&g_report_init_timer);
    }
    qm_timer_new(&g_report_init_timer, app_attr_init_report_cb, NULL, 3000, 0);
    qm_timer_start(&g_report_init_timer);


    
}

void qm_application_start(void)
{
    uint8_t xj_id = 0;
    uint32_t pid = DEVICE_PID;

    iot_map_ctx_t *map_ctx = NULL;
    QM_LOGD("TEST", "qm_application_start !!");

    iot_handle = iot_generic_init(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI);
    iot_generic_hooks_register(iot_handle, &mi_hook);

    iot_generic_option_set(iot_handle, IOT_OPTION_MODEL, DEVICE_MODEL, strlen(DEVICE_MODEL));
    iot_generic_option_set(iot_handle, IOT_OPTION_PRODUCT_ID, &pid, sizeof(uint32_t));

    map_ctx = (iot_map_ctx_t *)iot_generic_map_init(iot_handle, 24);
    if (map_ctx == NULL)
    {
        return;
    }

    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_BOOL, 3, 0, 1);   //0
    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_UINT8, 3, 0, 2);   //1
    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_UINT8, 3, 0, 3);   //2
    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_UINT8, 3, 0, 4);   //3

    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_UINT8, 3, 1, 1);   //4

    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_UINT8, 4, 0, 2);   //5
    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_BOOL, 4, 0, 4);   //6

    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_UINT8, 4, 1, 1);   //7

    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_BOOL, 6, 0, 1);   //8

    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_UINT8, 8, 0, 1);   //9
    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_STRING, 8, 0, 3); //10
    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_UINT32, 8, 0, 4); //11
    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_UINT32, 8, 0, 5); //12
    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_UINT8, 8, 0, 6);   //13
    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_STRING, 8, 0, 7); //14
    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_STRING, 8, 0, 8); //15
    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_STRING, 8, 0, 9); //16

    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_UINT8, 8, 1, 1);   //17
    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_STRING, 8, 1, 2);   //18

    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_UINT8, 9, 1, 1);   //19 匹配控制开关，固定开，在匹配界面

    //下面是后面加的功能，为了不影响前面的逻辑，所以放在后面
    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_BOOL, 6, 0, 2);   //20  照明开关
    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_UINT8, 6, 0, 3); //21   照明亮度 1-99%可调
    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_UINT8, 9, 0, 1); //22   照面功能选择 0表示有照明功能  1表示无照明功能

    miio_map_parse_set(map_ctx, xj_id++, QM_DTTV_TYPE_STRING, 9, 0, 2); //23   

    ir_send_delay_timer_init();
    air_func_get_buf();
    app_gpio_init();

    ir_send_handle_register(ir_send_func_cb);
    ir_send_task_init();

    uart_fac_init();
}
