#include "prodtst_client.h"
#include "prodtst_client_common.h"

#include "qm.h"
#include "qm_at.h"
#include "qm_log.h"

#include "qm_ble_gap.h"
#include "qm_utils_timer.h"

static void *prodtst_handle = NULL;

#define LOG_TAG "APP"

#if CONFIG_PRODTST_CLIENT_GEN_CMD_SUPPORT
int hal_fwname_verify(char *data, uint16_t len) //用于产测盒子向设备进行版本号校验
{
    if(data == NULL || len == 0){
        return -QM_EINVAL;
    }
    
    if(!memcmp(data, "WB60.M000.3007.14", strlen("WB60.M000.3007.14"))){
        // 版本号不一致 设备执行相应功能
    }else{
        // 版本号一致 设备执行相应功能
    }

    //返回结果默认为 QM_EOK,当返回结果不为QM_EOK时,组件包会将实际数据完整返回给平台
    return QM_EOK;
}

int hal_get_fwname(char *data, uint16_t len) //用于产测盒子向设备获取固件版本
{
    if(data == NULL || len == 0){
        return -QM_EINVAL;
    }
    memcpy(data, "WB60.M000.3007.14", strlen("WB60.M000.3007.14"));
    return QM_EOK;
}

static int mi_pid_setup(uint32_t product_id, char model[65])  //米家PID MODEL的写入与校验功能
{
    //先从设备内部中获取米家pid和model以及是否被绑定，若设备未被绑定并且pid和model与写入的不相同，则进行pid model更新

    return QM_AT_RESULT_OK;
}

static int mi_is_use_get(void)
{
    int is_use = 0;
    int len = 64;
    int ret = QM_EOK;
    if(!mi_mesh_otp_is_existed()){
        is_use = 2;
    }else{
        is_use = 1;
    }
    return is_use;
}

static int mi_pid_get(void)
{
    return 1;
}

static int mi_model_get(char model[65])
{
    return QM_EOK;
}

static prodtst_client_generic_cmd_t generic_hal =
{
    .fwname_get = hal_get_fwname,
    .fwname_verify = hal_fwname_verify, 
#if CONFIG_PRODTST_CLIENT_AT_BLE
    .ble_mac_get = NULL,
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_MI_SUPPORT
    .mi_pid_model_set = mi_pid_setup,
    .mi_pid_get = mi_pid_get,
    .mi_model_get = mi_model_get,
    .mi_is_use_get = mi_is_use_get,
#endif
};
#endif


#define CONFIG_WAKEUP_TIMEROUT  (10000)

static void *at_channel = NULL;
static volatile int voice_id = 0;
static qm_timer_t g_voice_timeout = {0};


int qm_voice_id_set(int msg_id)
{
    if(voice_id != -1){
        return -QM_EINVAL;
    }

    voice_id = msg_id;

    qm_timer_change(&g_voice_timeout, 50);

    return QM_EOK;
}

static void qm_wakeup_timeout(qm_timer_t *timer, void *arg)
{
    int get_voice_id = voice_id;

    voice_id = 0;

    if(at_channel == NULL){
        return;
    }

    if(get_voice_id > 0){
        QM_LOGD(LOG_TAG, "ASR ID %d!!!", get_voice_id);
        qm_at_server_printfln(at_channel, "+ASR:0,%d", get_voice_id);
    }else{
        QM_LOGD(LOG_TAG, "ASR ID FAIL!!!");
        qm_at_server_printfln(at_channel, "+ASR:-1");
    }
    qm_at_server_printfln(at_channel, "OK");
}

static  qm_at_result_t asr_setup(void *channel, const char *args)
{
    // qm_at_server_printfln(channel, "+CHK:0");
    int timeout = 0;
    int ret = QM_EOK;

    voice_id = -1;
    at_channel = channel;

    QM_LOGD(LOG_TAG, "AT asr_setup!!!"); 

    sscanf(args + 1, "%d", &timeout);
        
    qm_timer_change(&g_voice_timeout, timeout);

    return QM_AT_RESULT_NULL;
}


static qm_at_cmd_t prodtst_client_func_cmd[] = {  
    {"AT+ASR", NULL, NULL, NULL, asr_setup, NULL},
};

void prodtst_client_event_callback(prodtst_client_event_t event)
{
    qm_ble_scan_params_t scan_params = {0};

    switch (event)
    {
        case PRODTST_CLIENT_ENTER_PRODTST_SUCCESS:
            QM_LOGD(LOG_TAG, "PRODTST_CLIENT_ENTER_PRODTST_SUCCESS!!!"); 
            user_asr_timeout_config(3 * 60 * 1000); //识别超时 3min
        break;

        case PRODTST_CLIENT_ENTER_PRODTST_TIMEOUT:
            QM_LOGD(LOG_TAG, "PRODTST_CLIENT_ENTER_PRODTST_TIMEOUT!!!");
            prodtst_client_deinit(prodtst_handle); //产测超时后反初始化设备
        break;  

        case PRODTST_CLINET_PRODTST_TEST_RECV_TIMEOUT:
            // QM_LOGD(LOG_TAG, "PRODTST_CLINET_PRODTST_TEST_RECV_TIMEOUT!!!");
        break;  

        case PRODTST_CLIENT_EXIT_PRODTST_SUCCESS:
            QM_LOGD(LOG_TAG, "PRODTST_CLIENT_EXIT_PRODTST_SUCCESS!!!");
            // qm_timer_stop(&wakeup_timer);
            qm_reboot();
        break; 

        case PRODTST_CLINET_PRODTST_RESULT_FAILED:
            QM_LOGD(LOG_TAG, "PRODTST_CLINET_PRODTST_RESULT_FAILED!!!");
            // qm_timer_stop(&wakeup_timer);
            qm_reboot();
        break; 

        default:
        break;
    }
}

void qm_application_start(void)
{   
#if CONFIG_PRODTST_CLIENT_GEN_CMD_SUPPORT
    prodtst_client_generic_cmd_register(&generic_hal);
#endif
    prodtst_client_event_notify_register(prodtst_client_event_callback);

    
    QM_LOGD(LOG_TAG, "XJIANG VERSION BHF.0007.SPC.104_FW_X20_0.0.1_0.0.1 !!!");

    QM_LOGD(LOG_TAG, "PRODTST_CLIENT_ENTER_PRODTST_INIT PID %d!!!", CONFIG_PRODTST_CLIENT_BLE_MESH_PID);


    qm_timer_new(&g_voice_timeout, qm_wakeup_timeout, NULL, CONFIG_WAKEUP_TIMEROUT, 0);

    //照明行业的产测类型为 PRODTST_CLIENT_CHANNEL_TYPE_BLE_MESH
    prodtst_handle = prodtst_client_init(PRODTST_CLIENT_CHANNEL_TYPE_BLE_MESH, prodtst_client_func_cmd, QM_ARRAY_SIZE(prodtst_client_func_cmd));

}