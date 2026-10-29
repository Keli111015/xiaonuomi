#include "air_func_handle.h"
#include "qm_kv.h"

int8_t air_func_buf[AIR_KEY_MAX + 1] = {0, DEFAULT_FUNC_TEMP, DEFAULT_FUNC_MODE, 0, 0, 0, 1, 1, 1, 100, 0, 0};
 int8_t air_func_buf_temp[AIR_KEY_MAX + 1] = {0, DEFAULT_FUNC_TEMP, DEFAULT_FUNC_MODE, 0, 0, 0, 1, 1, 1, 100, 0, 0};
uint8_t g_current_operation = 0;

void air_func_current_operation_set(int8_t value)
{
    g_current_operation = value;
}

int8_t air_func_current_operation_get(void)
{
    return g_current_operation;
}

int8_t *air_func_get_buf(void)
{
    size_t len = sizeof(air_func_buf);
    if(qm_kv_get(AIR_FUNC_DATA_KEY, air_func_buf, &len) != QM_EOK){
        qm_printf("[LHSMD] air_func_get qm_kv_get error\r\n");
        air_func_clear();
        qm_kv_get(AIR_FUNC_DATA_KEY, air_func_buf, &len);
    }
    return air_func_buf;
}

void air_func_set(uint8_t index, int8_t value)
{
    air_func_buf[index] = value;
    if(qm_kv_set(AIR_FUNC_DATA_KEY, air_func_buf, sizeof(air_func_buf), 1) != QM_EOK){
        qm_printf("[LHSMD] air_func_set qm_kv_set error\r\n");
    }
}

int8_t air_func_get(uint8_t index)
{
    size_t len = sizeof(air_func_buf);
    if(qm_kv_get(AIR_FUNC_DATA_KEY, air_func_buf, &len) != QM_EOK){
        qm_printf("[LHSMD] air_func_get qm_kv_get error\r\n");
        air_func_clear();
        qm_kv_get(AIR_FUNC_DATA_KEY, air_func_buf, &len);
    }
    if(index == AIR_FUNC_SWITCH)
    {
        if(air_func_buf[index] == 1)
        {
            return 0;
        }
        else
        {
            return 1;
        }
    }
    return air_func_buf[index];
}

void air_func_clear(void)
{
    for(int i = 0; i < AIR_KEY_MAX + 1; i++)
    {
        air_func_set(i, air_func_buf_temp[i]);
        qm_msleep(10);
    }
}
