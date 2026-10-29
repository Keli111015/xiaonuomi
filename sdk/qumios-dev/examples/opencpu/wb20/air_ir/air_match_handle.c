#include "air_match_handle.h"
 #include "gpio_api.h"


#include "gpio_api.h"

#include "qm_kernel.h"
#include "qm_kv.h"
#include "qm.h"
#include <stdbool.h>

#include "air_func_handle.h"

static ir_data_t ir_data = {0};
static uint32_t air_info_buf[4] = {0};

uint8_t ir_send_task_init_flag = 0;
qm_task_t ir_send_task = {0};
extern void *iot_handle;
void ir_pwm_init(float period);

static uint8_t _g_ir_send_finish_flag = 0;

void air_info_set(uint8_t index, uint32_t value)
{
    air_info_buf[index] = value;
    if(qm_kv_set(AIR_INFO_KEY, air_info_buf, sizeof(air_info_buf), 1) != QM_EOK){
        qm_printf("[LHSMD] air_info_set qm_kv_set error\r\n");
    }
}

uint32_t air_info_get(uint8_t index)
{
    size_t len = sizeof(air_info_buf);
    if(qm_kv_get(AIR_INFO_KEY, air_info_buf, &len) != QM_EOK){
        qm_printf("[LHSMD] air_info_get qm_kv_get error\r\n");
    }
    return air_info_buf[index];
}

void air_info_clear(void)
{
    memset(air_info_buf, 0, sizeof(air_info_buf));
    if(qm_kv_set(AIR_INFO_KEY, air_info_buf, sizeof(air_info_buf), 1) != QM_EOK){
        qm_printf("[LHSMD] air_info_clear qm_kv_set error\r\n");
    }
}

void ir_pwm_period_set(float period)
{
    ir_data.ir_pwmout.ir_pwm_period = period;
}

void ir_air_type_data_set(uint8_t *data, uint16_t len)
{
    memcpy(ir_data.air_type_data, data, len);
    ir_data.air_type_data_len = len;
}

ir_data_t *ir_data_get(void)
{
    return &ir_data;
}


void air_type_clear(void)
{
    uint8_t value = 0;
    ir_data_get()->ir_pwmout.ir_pwm_period = 0.0;
    if(qm_kv_set(IR_MATCH_STATUS_KEY, &value, sizeof(uint8_t), 1) != QM_EOK)
    {
        qm_printf("qm_kv_set IR_MATCH_STATUS_KEY fail!\r\n");
        return;
    }
    
    if(qm_kv_set(IR_PERIOD_KEY, &ir_data_get()->ir_pwmout.ir_pwm_period, sizeof(float), 1) != QM_EOK)
    {
        qm_printf("qm_kv_set IR_PERIOD_KEY fail!\r\n");
        return;
    }

    memset(ir_data_get()->air_type_data, 0, sizeof(ir_data_get()->air_type_data));
    if(qm_kv_set(IR_TYPE_DATA_KEY, ir_data_get()->air_type_data, 512*3, 1) != QM_EOK)
    {
        qm_printf("qm_kv_set IR_TYPE_DATA_KEY fail!\r\n");
        return;
    }
}




static void ir_send_timer_stop(uint32_t periodic)
{
    pwm_start_ctrl(0);
    if(ir_data.ir_timer.ir_timer_is_init)
        gtimer_deinit(&ir_data.ir_timer.ir_send_timer);
    ir_data.ir_timer.ir_timer_is_init = 0;
    
}


static void timer_timeout_handler(uint32_t id)
{
    if ( ir_data.ir_data_index < ir_data.ir_data_len)
    {
        if (ir_data.ir_data_index % 2 == 0)
        {
            pwm_start_ctrl(1);
        }
        else
        {
            pwm_start_ctrl(0);
        }
        gtimer_start_one_shout(&ir_data.ir_timer.ir_send_timer, ir_data.ir_data[ir_data.ir_data_index++], (void *)timer_timeout_handler, 0);
    }
    else
    {
        _g_ir_send_finish_flag = 1;
        ir_send_timer_stop(&ir_data.ir_timer.ir_send_timer);
    }
}


static void ir_send_timer_start(uint32_t periodic)
{
    _g_ir_send_finish_flag = 0;
    ir_data.ir_data_index = 0;
    if(ir_data.ir_timer.ir_timer_is_init == 0)
        gtimer_init(&ir_data.ir_timer.ir_send_timer, PERIODICAL_TIMER);
    else
    {
        ir_send_timer_stop(&ir_data.ir_timer.ir_send_timer);
        gtimer_init(&ir_data.ir_timer.ir_send_timer, PERIODICAL_TIMER);
    }

    ir_data.ir_timer.ir_timer_is_init  = 1;

    gtimer_start_one_shout(&ir_data.ir_timer.ir_send_timer, 100000, (void *)timer_timeout_handler, 0);
}



static uint16_t trans_pulse(uint16_t *out, uint16_t ret_num, uint16_t frequency,
                            uint8_t frame_count, uint32_t *micro_full_pattern)
{
    uint16_t i;
    uint32_t pulse;

    pulse = 1000000 / frequency;
    for (i = 0; i < ret_num; i++)
    {
        micro_full_pattern[i] = pulse * out[i];
    }

    if (frame_count > 1)
    {
        for (i = 1; i < frame_count; i++)
        {
            memmove(&micro_full_pattern[ret_num * i], micro_full_pattern,
                    ret_num * sizeof(micro_full_pattern[0]));
        }
    }

    return ret_num * frame_count;
}

static int32_t hex_to_bytes(const char *hex, uint8_t *byte_buf,
                            int32_t byte_buf_size)
{
    size_t hex_len;
    int32_t i, pos;
    hex_len = strlen(hex);
    for (i = 0; i < hex_len; i++)
    {
        char hexChar;
        uint8_t val;

        hexChar = hex[i];
        if (hexChar >= '0' && hexChar <= '9')
        {
            val = hexChar - '0';
        }
        else if (hexChar >= 'A' && hexChar <= 'F')
        {
            val = hexChar - 'A' + 10;
        }
        else if (hexChar >= 'a' && hexChar <= 'f')
        {
            val = hexChar - 'a' + 10;
        }
        else
        {
            return -1;
        }

        pos = i / 2;
        if (pos >= byte_buf_size)
        {
            return -2;
        }

        if (i % 2 == 0)
        {
            byte_buf[pos] = val << 4;
        }
        else
        {
            byte_buf[pos] += val;
        }
    }

    return pos + 1;
}

void pwm_start_ctrl(uint8_t flag)
{

    if (flag)
    {
        pwmout_start(&ir_data.ir_pwmout.pwm_obj[PWM_IR_IDX]);
    }
    else
    {
        pwmout_stop(&ir_data.ir_pwmout.pwm_obj[PWM_IR_IDX]);
    }
}

void light_brightness_close(void)
{
    if(air_func_get(AIR_FUNC_LIGHT_MODE) == 0) return; //=0 无照明
    pwmout_write(&ir_data.ir_pwmout.pwm_obj[PWM_LIGHT_IDX], 0);
}

void light_brightness_set(void)
{
    if(air_func_get(AIR_FUNC_LIGHT_MODE) == 0) return; //=0 无照明
    uint16_t light_pwm_duty = air_func_get(AIR_FUNC_LIGHT_BRIGHTNESS);
    if(ir_data.ir_pwmout.light_pwm_is_init)
        pwmout_write(&ir_data.ir_pwmout.pwm_obj[PWM_LIGHT_IDX], (float)light_pwm_duty/100.0);
}


//func：功能码
//flag：0表示临时码库，1表示匹配后的码库
int32_t ir_parse(int8_t *buf, uint8_t func, uint8_t flag)
{
    uint8_t switch_flag = 0;
    
    uint16_t *out;
    uint8_t wind_direct = 0;
    uint16_t ret_num, frequency; // 脉冲个数数组，元素个数
    uint8_t frame_count = 1;
    int32_t remote_data_length;
    int32_t ret = 0, i;
    double pulse;
    uint16_t mode_flags[5];
    uint8_t wind_direct_flag;
    uint8_t wind_direct_min, wind_direct_max;
    char *mode[5] = {"cool", "heat", "auto", "air supply", "dry"};
    char *wind_speed[4] = {"auto", "low", "medium", "high"};
    uint8_t remote_data[1500];
    memset(ir_data.ir_data, 0, sizeof(ir_data.ir_data));
    // 约克空调id:77
    if(flag == 0)
    {
         
        if(ir_data.air_type_data_temp == NULL) return 1;
        remote_data_length = hex_to_bytes(ir_data.air_type_data_temp, remote_data,
                                      sizeof(remote_data));
    }
    else
    {
       if(ir_data.air_type_data == NULL) return 1;
        remote_data_length = hex_to_bytes(ir_data.air_type_data, remote_data,
                                      sizeof(remote_data));
    }
    
    if (remote_data_length <= 0)
    {
        qm_printf("hex data error:ret=%d\n", remote_data_length);
        return 1;
    }


    /*
    get_ac_info(const uint8_t *data, uint16_t data_len, uint16_t *mode_flags, uint8_t *wind_direct_flag)
    mode_flags 长度至少包含5个uint16_t元素，0-4一共5个uint16_t分别对应制冷，制热，自动，送风，除湿模式的信息
    mode_flags的每个uint16_t包含信息的规则为：
    为0则表示不支持该模式，从低到高0-3bit存储该模式支持的温度最小值与16的差(制冷模式为例：(mode_flags[0]&0x0F)+16),
    从低到高4-7bit存储该模式支持的温度最大值与16的差(制冷模式为例：((mode_flags[0]>>4)&0x0F)+16)
    最小最大值都为0则表示不支持温度调节(制冷模式为例：(mode_flags[0]&0xFF)==0)
    从低到高8-11bit每bit分别对应风速自动，低，中，高，为1则表示支持该风速，为0则表示不支持
    (制冷模式为例：((mode_flags[0]>>8)&1) 自动， ((mode_flags[0]>>9)&1) 低，((mode_flags[0]>>10)&1) 中，((mode_flags[0]>>11)&1) 高)

    wind_direct_flag 一个字节长度，返回遥控器上下风向支持情况，*wind_direct_flag = 0 表示不支持上下风向调节(get_ac_state_pulse接口wind_direct传-1），
    *wind_direct_flag = 1表示上下风向只有一组码，发这组码空调的上下风向在2-n个状态之间来回切换(get_ac_state_pulse接口wind_direct传1），
    除此之外则表示空调上下风向有多组码（码值组数根据码组序号最大值和最小值确定），每组码对应空调上下风向的一个状态，0-3bit返回风向码组序号的最小值（(*wind_direct_flag) & 0x0F,
    从0开始，0表示发该组码空调进入扫风状态，大于0的值表示一个固定的上下风向，可能停在当前位置或者停到一个预设的位置），
    4-7bit返回风向码组序号的最大值（((*wind_direct_flag)>>4) & 0x0F),(get_ac_state_pulse接口wind_direct传想发送的码组序号即可，比如传0返回扫风的码组）
    */
    ret = get_ac_info(remote_data, remote_data_length, 
                        mode_flags,
                      &wind_direct_flag);
    if (ret != 0)
    {
        qm_printf("get_ac_info return error:ret=%d\n", ret);
        return 1;
    }

    //
    qm_printf("get_ac_info wind_direct_flag=%d\n", wind_direct_flag);

    qm_printf("get_ac_info wind_direct_min=%d,wind_direct_max=%d\n",
              wind_direct_flag & 0x0F, (wind_direct_flag >> 4) & 0x0F);
    wind_direct_min = wind_direct_flag & 0x0F;
    wind_direct_max = (wind_direct_flag >> 4) & 0x0F;

    for (int32_t i = 0; i < 5; i++)
    {
        int32_t min_temp, max_temp;
        if (mode_flags[i] == 0)
        {
            qm_printf("mode %s unsupported\n", mode[i]);
            continue;
        }

        min_temp = mode_flags[i] & 0x0F;
        max_temp = (mode_flags[i] >> 4) & 0x0F;
        if (min_temp == max_temp && min_temp == 0)
        {
            qm_printf("temperature can not be adjusted for %s mode\n", mode[i]);
        }
        else
        {
            min_temp += 16;
            max_temp += 16;
            qm_printf("min_temp =%d, max_temp=%d for %s mode\n", min_temp, max_temp,
                      mode[i]);
        }

        for (int32_t j = 0; j < 4; j++)
        {
            if ((mode_flags[i] >> (8 + j)) & 1)
            {
                qm_printf("%s wind_speed for %s mode\n", wind_speed[j], mode[i]);
            }
            else
            {
                qm_printf("no %s wind_speed for %s mode\n", wind_speed[j], mode[i]);
            }
        }
    }

    out = (uint16_t *)(ir_data.ir_data + MAX_PATTERN_NUM);
    out = out - MAX_PATTERN_NUM;



    // get_ac_state_pulse(const uint8_t *data, uint16_t data_len, 
    //                                 uint8_t power, 
    //                                 uint8_t mode, 
    //                                 int8_t temperature, 
    //                                 int8_t wind_speed, 
    //                                 int8_t wind_direct, 
    //                                 int8_t pressed_fid, 
    //                                 uint16_t *frequency, uint16_t *out, uint16_t out_max_num, uint16_t *ret_num, uint8_t *frame_count)

    // 对应用户使用物理遥控器当前按压的按键id[电源 1，模式 2，温度+ 3，温度- 4，风量 5，扫风 6，定向风 7，如果上下风向只有一组码，则切换上下风向时传7]，

    if(flag == 0)
    {
        ret = get_ac_state_pulse(remote_data, remote_data_length, \
                                    0, \
                                    DEFAULT_FUNC_MODE, \
                                    DEFAULT_FUNC_TEMP,\
                                    0,\
                                    0, \
                                    1,\
                                &frequency, out, MAX_PATTERN_NUM, &ret_num, &frame_count);
    }
    else
    {
        // if(air_info_get(AIR_TYPE) == 1)  func = 7; //只有一组码

        if(wind_direct_flag == 0) //不支持上下风调节
        {
            air_func_set(AIR_FUNC_WIND_DIRECTION, -1);
            if(func == AIR_KEY_WIND_DIRECTION || func == AIR_KEY_SWEPT_WING) 
                func = 6;
        }
        else if(wind_direct_flag == 1) //只有一组码
        {
            air_func_set(AIR_FUNC_WIND_DIRECTION, 1);
            if(func == AIR_KEY_WIND_DIRECTION || func == AIR_KEY_SWEPT_WING) 
                func = 7;
        }
        else
        {
            if(func == AIR_KEY_WIND_DIRECTION)
            {
                wind_direct = air_func_get(AIR_FUNC_WIND_DIRECTION);
                wind_direct++;
                if(wind_direct > wind_direct_max) wind_direct = wind_direct_min;
                if(wind_direct == 0) func = AIR_KEY_SWEPT_WING;  //0，传6，扫风
                air_func_set(AIR_FUNC_WIND_DIRECTION, wind_direct);
                qm_printf("-------------------wind_direct = %d\r\n", wind_direct);
            }

        }

        buf = air_func_get_buf();

        //调节开关，且开关为关
        if(func == AIR_KEY_SWITCH && buf[AIR_FUNC_SWITCH] == 1)
        {
            switch_flag = 1;
            if(air_func_get(AIR_FUNC_SWITCH) == 1) air_func_set(AIR_FUNC_SWITCH, 1);
        }
        else
        {
            switch_flag = 0;
            if(air_func_get(AIR_FUNC_SWITCH) == 0) air_func_set(AIR_FUNC_SWITCH, 0);
        }
        ret = get_ac_state_pulse(remote_data, remote_data_length, \
                                    switch_flag, \
                                    buf[AIR_FUNC_MODE], \
                                    buf[AIR_FUNC_TEMP],\
                                    buf[AIR_FUNC_WIND_SPEED],\
                                    buf[AIR_FUNC_WIND_DIRECTION], \
                                    AIR_KEY_SWITCH,\
                                &frequency, out, MAX_PATTERN_NUM, &ret_num, &frame_count);
    }



    if (ret != 0)
    {
        qm_printf("get_ac_state_pulse return error:ret=%d\n", ret);
        return 1;
    }
    ir_data.ir_pwmout.ir_pwm_period = frequency;
    qm_printf(
        "get_ac_state_pulse:ret=%d, frequency=%d, ret_num=%d, frame_count=%d\n",
        ret, frequency, ret_num, frame_count);
    ir_data.ir_data_len = trans_pulse(out, ret_num, frequency, frame_count, ir_data.ir_data);
    // 高低电平时间序列数组，单位微秒US
    for (int32_t i = 0; i < ir_data.ir_data_len; i++)
    {
        qm_printf("%d,", ir_data.ir_data[i]);
    }
    qm_printf("\n");

    ir_pwm_init(ir_data.ir_pwmout.ir_pwm_period);
    return 0;
}



ir_send_func ir_send_cb = NULL;

void ir_send_handle_register(ir_send_func func)
{
    ir_send_cb = func;
}

void light_pwm_init(void)
{
    int light_pwm_duty = 0;
    float period_temp = 0.0;
    int perood_result = 0;
    uint8_t light_mode = air_func_get(AIR_FUNC_LIGHT_MODE);
    if(light_mode == 0 || ir_data.ir_pwmout.light_pwm_is_init) //=0 无照明
    {
        return;
    }
    ir_data.ir_pwmout.light_pwm_is_init = 1;
    pwmout_init(&ir_data.ir_pwmout.pwm_obj[PWM_LIGHT_IDX], GPIO_LIGHT_PWM_PIN);
    period_temp = 1000000.0 / AIR_LIGHT_PWM_PERIOD;
    perood_result = (int)(period_temp + 0.5);
    pwmout_period_us(&ir_data.ir_pwmout.pwm_obj[PWM_LIGHT_IDX], perood_result);

    uint8_t reg_status = air_func_get(AIR_FUNC_REG_STATUS);
    if(reg_status == 0 && air_func_get(AIR_FUNC_LIGHT_SWITCH) != 1) 
    {
        air_func_set(AIR_FUNC_LIGHT_BRIGHTNESS, 100);
        air_func_set(AIR_FUNC_LIGHT_SWITCH, 1);
    }   

    uint8_t light_pwm_switch = air_func_get(AIR_FUNC_LIGHT_SWITCH);
    if(light_pwm_switch == 0) light_pwm_duty = 0;
    else
    {
        light_pwm_duty = air_func_get(AIR_FUNC_LIGHT_BRIGHTNESS);
        qm_printf("--------------------light_pwm_duty = %d\r\n", light_pwm_duty);
    }
    pwmout_write(&ir_data.ir_pwmout.pwm_obj[PWM_LIGHT_IDX], (float)light_pwm_duty/100.0);
    pwmout_start(&ir_data.ir_pwmout.pwm_obj[PWM_LIGHT_IDX]);
}


void ir_pwm_init(float period)
{
    float period_temp = 0.0;
    int perood_result = 0;
    static uint8_t flag = 0;

    if(ir_data.ir_pwmout.ir_pwm_is_init)
    {
        return;
    }
    ir_data.ir_pwmout.ir_pwm_is_init = 1;
     //红外发送发送gpio初始化    PA23
    if(flag == 0)
    {
        pwmout_init(&ir_data.ir_pwmout.pwm_obj[PWM_IR_IDX], GPIO_IR_SEND_PIN);
        flag = 1;
    }
    period_temp = 1000000.0 / period;
    perood_result = (int)(period_temp + 0.5);
    pwmout_period_us(&ir_data.ir_pwmout.pwm_obj[PWM_IR_IDX], perood_result);
    pwmout_pulsewidth_us(&ir_data.ir_pwmout.pwm_obj[PWM_IR_IDX], perood_result / 2.0);
    pwm_start_ctrl(0);
}


static void ir_send_task_entry(void *arg)
{
    unsigned int size = 0;
    if(qm_queue_new(&ir_data.ir_queue.ir_send_queue, IR_QUEUE_LEN, IR_QUEUE_SIZE) != QM_EOK)
    {
        qm_printf("[LHSMD] ir_send_task_entry error: qm_queue_new error\r\n");
        return;
    }
    while(1)
    { 
        if(qm_queue_recv(&ir_data.ir_queue.ir_send_queue, ir_data.ir_queue.ir_send_queue_buf, \
                &size, QM_WAIT_FOREVER) == QM_EOK)
        {
            qm_printf("------------------ir_send_task_entry -----------------------\r\n");
            qm_printf("------------------ir_data.ir_queue.ir_send_queue_buf[0] = %d-----------------------\r\n", ir_data.ir_queue.ir_send_queue_buf[0]);
            qm_printf("------------------ir_data.ir_queue.ir_send_queue_buf[1] = %d-----------------------\r\n", ir_data.ir_queue.ir_send_queue_buf[1]);
            user_report_charact(iot_handle, AIR_OPEN_CLOSE_TAG, air_func_get(AIR_FUNC_SWITCH));
            ir_parse(air_func_get_buf(), air_func_current_operation_get(), ir_data.ir_queue.ir_send_queue_buf[0]);
            ir_send_timer_start(&ir_data.ir_timer.ir_send_timer);
            hal_delay_ms(1024);
            
            if(ir_send_cb != NULL)
                ir_send_cb(NULL);
            
        }
        vTaskDelay(10);
    }
}


void app_ir_send_task_init(void)
{
    qm_task_new(&ir_send_task, "ir_send_task", ir_send_task_entry, NULL, 1024 * 5, 6);
    ir_send_task_init_flag = 1;
}

void ir_send_task_init(void)
{
    light_pwm_init();
    int len = sizeof(ir_data.ir_pwmout.ir_pwm_period);
    if(qm_kv_get(IR_PERIOD_KEY, &ir_data.ir_pwmout.ir_pwm_period, &len) != QM_EOK)
    {
        qm_printf("[LHSMD] IR_PERIOD_KEY error: qm_kv_get error\r\n");
        ir_pwm_init(38000);
        return;
    }
  
    len = AIR_TYPE_DATA_LEN;
    if(qm_kv_get(IR_TYPE_DATA_KEY, ir_data.air_type_data, &len) != QM_EOK)
    {
        qm_printf("[LHSMD] IR_TYPE_DATA_KEY error: qm_kv_get error\r\n");
        ir_pwm_init(38000);
        return;
    }

    ir_data.air_type_data_len = strlen(ir_data.air_type_data);
    if(ir_data.air_type_data_len == 0)
    {
        qm_printf("[LHSMD] ir_data.air_type_data_len == 0\r\n");
        ir_pwm_init(38000);
        return;
    }

    ir_pwm_init(ir_data.ir_pwmout.ir_pwm_period);
    
    app_ir_send_task_init();
    qm_printf("---------------------------------------------------------\r\n");
    qm_printf("ir_data.ir_pwmout.ir_pwm_period = %f\r\n", ir_data.ir_pwmout.ir_pwm_period);
    qm_printf("ir_data.air_type_data_len = %d\r\n", ir_data.air_type_data_len);
    qm_printf("ir_data.air_type_data = %s\r\n", ir_data.air_type_data);
    qm_printf("---------------------------------------------------------\r\n");
    
}