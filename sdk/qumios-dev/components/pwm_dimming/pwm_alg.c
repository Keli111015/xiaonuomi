#include "pwm_alg.h"
#include "stdint.h"
#include "math.h"

static uint16_t g_table_main[101];

void pwm_alg_linear_table_init(uint16_t *table, int lightness_min)
{
    //线性曲线
    table[0] = 0;
    table[1] = 1 + lightness_min;
    //将[lightness_min, 10000]平均分
    for (int i = 2; i <= 100; i++)
    {
        table[i] = (LIGHTNESS_MAX - lightness_min) * i / 100 + lightness_min;
        if (table[i] > 10000)
        {
            table[i] = 10000;
        }
    }
    table[100] = 10000;
}
static uint16_t _delta[] =
{
    0,
    2692,
    2148,
    1668,
    1252,
    900,
    612,
    388,
    228,
    132,
};
static uint16_t _log_table[] =
{
    0,
    0,
    0,
    0,
    0,
    1267,
    1542,
    1810,
    2071,
    2326,
    2575,
    2817,
    3054,
    3284,
    3508,
    3727,
    3940,
    4147,
    4348,
    4544,
    4735,
    4920,
    5100,
    5274,
    5444,
    5609,
    5768,
    5923,
    6073,
    6219,
    6360,
    6496,
    6628,
    6756,
    6879,
    6998,
    7114,
    7225,
    7332,
    7436,
    7535,
    7632,
    7724,
    7813,
    7899,
    7982,
    8061,
    8137,
    8210,
    8281,
    8348,
    8413,
    8474,
    8534,
    8591,
    8645,
    8697,
    8747,
    8794,
    8840,
    8883,
    8925,
    8964,
    9002,
    9039,
    9073,
    9107,
    9138,
    9169,
    9198,
    9226,
    9253,
    9280,
    9305,
    9329,
    9353,
    9376,
    9399,
    9421,
    9442,
    9464,
    9485,
    9506,
    9527,
    9548,
    9570,
    9591,
    9613,
    9635,
    9658,
    9681,
    9705,
    9730,
    9755,
    9782,
    9809,
    9838,
    9867,
    9898,
    9931,
    10000,
};

void pwm_alg_log_table_init(uint16_t *table, int lightness_min)
{
    table[0] = 0;
    table[1] = 1 + lightness_min;
    float tmp = 0;

    {
        _log_table[1] = lightness_min;
        float delta = (_log_table[5] - lightness_min) / 4.0;
        for (int i = 2; i < 5; i++)
        {
            _log_table[i] = lightness_min + delta *( i - 1);
        }
        memcpy(table, _log_table, sizeof(_log_table));
    }
    table[100] = 10000;
}

static uint16_t _s_table[] =
{
    0,
    425,
    818,
    1181,
    1517,
    1827,
    2112,
    2374,
    2614,
    2835,
    3036,
    3220,
    3388,
    3540,
    3678,
    3804,
    3917,
    4019,
    4110,
    4193,
    4266,
    4332,
    4390,
    4442,
    4488,
    4528,
    4564,
    4595,
    4622,
    4645,
    4666,
    4683,
    4698,
    4711,
    4722,
    4731,
    4739,
    4746,
    4751,
    4756,
    4760,
    4763,
    4765,
    4768,
    4769,
    4771,
    4772,
    4773,
    4774,
    4775,
    4775,
    4776,
    4777,
    4778,
    4779,
    4781,
    4783,
    4785,
    4787,
    4790,
    4794,
    4799,
    4804,
    4811,
    4819,
    4828,
    4839,
    4852,
    4867,
    4884,
    4905,
    4928,
    4955,
    4986,
    5022,
    5062,
    5108,
    5159,
    5218,
    5283,
    5357,
    5439,
    5530,
    5632,
    5745,
    5870,
    6008,
    6161,
    6328,
    6512,
    6713,
    6933,
    7174,
    7436,
    7720,
    8030,
    8365,
    8729,
    9122,
    9546,
    10000,
};
void pmw_alg_s_table_init(uint16_t *table, int lightness_min)
{
    {
        _s_table[1] = lightness_min;
        float delta = (_s_table[3] - lightness_min) / 2.0;
        for (int i = 2; i < 3; i++)
        {
            _s_table[i] = lightness_min + delta *( i - 1);
        }
        memcpy(table, _s_table, sizeof(_s_table));
    }

    table[100] = 10000;
}

void pwm_alg_exp_table_init(uint16_t *table, int lightness_min)
{
    table[0] = 0;

    //最低亮度限制
    table[1] = 1 + lightness_min;
    //生成gamma table
    double factor = EXP_FACTOR; 
    double temp = 0;
    double temp1 = 0;

    for (int i = 2; i <= 100; i++)
    {
        temp = 0.5 + 100 * pow((double)(i-1) / 100.0, factor);

        temp1 = (temp + (lightness_min / 100)) * 10000.0 / 100.0;
        if (temp1 > 10000)
        {
            temp1 = 10000;
        }
        table[i] = temp1;

    }
    //最后一个数据应该是10000
    table[100] = 10000;
}
//TODO:impl
void pwm_alg_table_init(int curve_type, int lightness_min)
{
    switch (curve_type)
    {
        default:
        case DIMMING_CURVE_TYPE_LINEAR:
        {
            pwm_alg_linear_table_init(g_table_main, lightness_min);
            break;
        }
        case DIMMING_CURVE_TYPE_EXP:
        {
            pwm_alg_exp_table_init(g_table_main, lightness_min);
            break;
        }
        case DIMMING_CURVE_TYPE_LOG:
        {
            pwm_alg_log_table_init(g_table_main, lightness_min);
            break;
        }
        case DIMMING_CURVE_TYPE_S:
        {
            pmw_alg_s_table_init(g_table_main, lightness_min);
            break;
        }
    }
    return;
}

int pwm_alg_get_exp(int light_id, int lightness)
{
    //查表 g_table, table [0,100]
    //lightness [0,10000]
    //lightness 0~10000, 0->0 10000->10000
    int index = lightness * 1000 / 10000;
    int step = index % 10;
    index /= 10;

    int pwm = 0;

    if (index >= 100)
    {
        pwm = 10000;
    }
    else
    {
        // if (light_id == MAIN_LIGHT_INDEX)
        {
            pwm = g_table_main[index] + (g_table_main[index + 1] - g_table_main[index]) * step / 10;
        }
        // else if (light_id == AUX_LIGHT_INDEX)
        // {
        //     pwm = g_table_aux[index] + (g_table_aux[index + 1] - g_table_aux[index]) * step / 10;
        // }
        // else 
        // {
        //     pwm = lightness;
        // }
    }

    return pwm;
}
