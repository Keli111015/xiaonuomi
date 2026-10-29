#include "bp5768d.h"

#include "qm_log.h"
#include "qm_kernel.h"

#define LOG_TAG "bp5768d"

static qm_driver_bp5768d_t conf = {
    .port = 0,
    .iic_clk = 4,
    .iic_sda = 5,
    .freq_khz = 100,
    .current = {10, 10, 10, 20, 20},
};
    
int32_t bp5768d_demo(void)
{
    int ret = QM_EOK;
    ret = qm_bp5768d_init(&conf);
    if(ret != QM_EOK){
        QM_LOGD(LOG_TAG, "qm_bp5768d_init error %d", ret);
    }

    qm_bp5768d_regist_channel(QM_BP5768D_CHANNEL_R, QM_BP5768D_PIN_OUT1);
    qm_bp5768d_regist_channel(QM_BP5768D_CHANNEL_G, QM_BP5768D_PIN_OUT2);
    qm_bp5768d_regist_channel(QM_BP5768D_CHANNEL_B, QM_BP5768D_PIN_OUT3);
    qm_bp5768d_regist_channel(QM_BP5768D_CHANNEL_C, QM_BP5768D_PIN_OUT4);
    qm_bp5768d_regist_channel(QM_BP5768D_CHANNEL_W, QM_BP5768D_PIN_OUT5);

    while (1)
    {
        qm_msleep(1000);

        ret = qm_bp5768d_set_rgb_channel(255, 255, 255);
        if(ret != QM_EOK){
            QM_LOGD(LOG_TAG, "qm_bp5768d SET RGB error %d", ret);
        }

        qm_msleep(1000);
        ret = qm_bp5768d_set_rgb_channel(0, 255, 255);
        if(ret != QM_EOK){
            QM_LOGD(LOG_TAG, "qm_bp5768d SET RGB error %d", ret);
        } 

        qm_msleep(1000);
        ret = qm_bp5768d_set_rgb_channel(255, 0, 255);
        if(ret != QM_EOK){
            QM_LOGD(LOG_TAG, "qm_bp5768d SET RGB error %d", ret);
        }   

        qm_msleep(1000);
        ret = qm_bp5768d_set_rgb_channel(255, 255, 0);
        if(ret != QM_EOK){
            QM_LOGD(LOG_TAG, "qm_bp5768d SET RGB error %d", ret);
        }   

        qm_msleep(1000);
        ret = qm_bp5768d_set_rgb_channel(0, 0, 0);
        if(ret != QM_EOK){
            QM_LOGD(LOG_TAG, "qm_bp5768d SET RGB error %d", ret);
        }

        qm_msleep(1000);
        ret = qm_bp5768d_set_cw_channel(255, 255);
        if(ret != QM_EOK){
            QM_LOGD(LOG_TAG, "qm_bp5768d SET RGB error %d", ret);
        }

        qm_msleep(1000);
        ret = qm_bp5768d_set_cw_channel(0, 255);
        if(ret != QM_EOK){
            QM_LOGD(LOG_TAG, "qm_bp5768d SET RGB error %d", ret);
        }   

        qm_msleep(1000);
        ret = qm_bp5768d_set_cw_channel(255, 0);
        if(ret != QM_EOK){
            QM_LOGD(LOG_TAG, "qm_bp5768d SET RGB error %d", ret);
        }  

        qm_msleep(1000);
        ret = qm_bp5768d_set_cw_channel(0, 0);
        if(ret != QM_EOK){
            QM_LOGD(LOG_TAG, "qm_bp5768d SET RGB error %d", ret);
        }               
    }
    


    return QM_EOK;
} 