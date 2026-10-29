#include "qm_modem.h"
#include "qm_types.h"
#include "qm_errno.h"
#include "qm_log.h"
#include "qm_kernel.h"
#include "qm_config.h"

#include "liot_sim.h"
#include "liot_nw.h"
#include "liot_dev.h"
#include "liot_datacall.h"
#include "liot_sockets.h"
#include "ps_event_callback.h"
#include "cmips.h"
#include "cmisim.h"
#include "cmimm.h"
#include "cmisms.h"
#include "cms_api.h"
#include "networkmgr.h"
#include "liot_lbs_client.h"


#define LOG_TAG "modem"

typedef struct {
    qm_task_t task;
    qm_sem_t sem;
    qm_timer_t timer;
    qm_modem_config_t config;
}qm_modem_handle_t;

#define NETWORK_TIMEOUT (10*60*1000)

static qm_modem_handle_t *g_qm_modem_handle = NULL;


static void liot_nw_ind_callback(uint8_t nSim, unsigned int ind_type, void *ctx)
{
    int ret = -1;
    char csq = 99;
    qm_modem_rssi_t modem_rssi = {0};
    liot_nw_nitz_time_info_s *liot_nw_nitz_time_info = NULL;
    QM_LOGD(LOG_TAG, "nSim=%d, ind_type=%x", nSim, ind_type);
    switch (ind_type)
    {
        case LIOT_NW_SIGNAL_QUALITY_IND:
            csq = *((char *)ctx);
            QM_LOGD(LOG_TAG, "csq=%d", csq);

            if(g_qm_modem_handle->config.event_handler){
                modem_rssi.rssi = csq;
                g_qm_modem_handle->config.event_handler(QM_MODEM_EVENT_RSSI, &modem_rssi, sizeof(modem_rssi), g_qm_modem_handle->config.arg);
            }

            break;
        case LIOT_NW_NITZ_TIME_UPDATE_IND:
            liot_nw_nitz_time_info = (liot_nw_nitz_time_info_s *)ctx;
            QM_LOGD(LOG_TAG, "nitz_time= %s, abs_time= %ld", liot_nw_nitz_time_info->nitz_time, liot_nw_nitz_time_info->abs_time);
            break;
    }

}


static int32_t liot_netModePSUrcCallback(PsEventID eventID, void *param, uint32_t paramLen)
{
    static qm_modem_event_t modem_old_event = QM_MODEM_EVENT_DISCONN;
    liot_data_call_info_t socket_info;
    QM_LOGD(LOG_TAG, "urc recv event 0x%x", eventID);
    switch(eventID)
    {
        case PS_URC_ID_PS_CEREG_CHANGED:
        {
            // liot_nw_register_status_cb
            liot_nw_common_reg_status_info_s liot_nw_msg = {0};
            CmiPsCeregInd *pPsCeregInd                = (CmiPsCeregInd *)param;

            liot_nw_msg.state = pPsCeregInd->state;
            liot_nw_msg.act   = pPsCeregInd->act;
            liot_nw_msg.cid   = pPsCeregInd->cellId;
            liot_nw_msg.lac   = 0; // LTE上好像只有TAC

            QM_LOGD(LOG_TAG, "network state: %d", liot_nw_msg.state);

            if ((LIOT_NW_REG_STATE_HOME_NETWORK == liot_nw_msg.state) || (LIOT_NW_REG_STATE_ROAMING == liot_nw_msg.state)){
                struct sockaddr_in local4;
                memset(&local4, 0x00, sizeof(struct sockaddr_in));
                local4.sin_family = AF_INET;
                local4.sin_port   = 0;

                (void)liot_ip4addr_aton(liot_ip4addr_ntoa(&socket_info.v4.addr.ip), (liot_ip4_addr_t *)&local4.sin_addr);
                QM_LOGD(LOG_TAG, "socket_info.v4.state: %d.", socket_info.v4.state);
                QM_LOGD(LOG_TAG, "socket_info.v4.addr.ip: %s.", liot_ip4addr_ntoa(&socket_info.v4.addr.ip));
                QM_LOGD(LOG_TAG, "socket_info.v4.addr.pri_dns: %s.", liot_ip4addr_ntoa(&socket_info.v4.addr.pri_dns));
                QM_LOGD(LOG_TAG, "socket_info.v4.addr.sec_dns: %s.", liot_ip4addr_ntoa(&socket_info.v4.addr.sec_dns));
                QM_LOGD(LOG_TAG, "local4.sin_addr.s_addr: 0x%x.", local4.sin_addr.s_addr);
                qm_timer_stop(&g_qm_modem_handle->timer);
                if(modem_old_event != QM_MODEM_EVENT_CONN) {
                    modem_old_event = QM_MODEM_EVENT_CONN;
                    if(g_qm_modem_handle->config.event_handler){
                        g_qm_modem_handle->config.event_handler(QM_MODEM_EVENT_CONN, NULL, 0, g_qm_modem_handle->config.arg);
                    }
                }
            } else {
                qm_timer_start(&g_qm_modem_handle->timer);
                if(modem_old_event != QM_MODEM_EVENT_DISCONN) {
                    modem_old_event = QM_MODEM_EVENT_DISCONN;
                    if(g_qm_modem_handle->config.event_handler){
                        g_qm_modem_handle->config.event_handler(QM_MODEM_EVENT_DISCONN, NULL, 0, g_qm_modem_handle->config.arg);
                    }
                }
            }
            
            break;
        }

        case PS_URC_ID_PS_BEARER_ACTED:
            QM_LOGD(LOG_TAG, "PDP Active");
        break;

        case PS_URC_ID_PS_BEARER_DEACTED:
            QM_LOGD(LOG_TAG, "PDP Deactive");
        break;

        default:
            break;

    }
    return 0;
}

static void network_timeout(qm_timer_t *timer, void *arg)
{
    uint8_t nSim = 0;
    liot_dev_set_modem_fun(LIOT_DEV_CFUN_MIN, 0, nSim);
    liot_dev_set_modem_fun(LIOT_DEV_CFUN_FULL, 0, nSim);
}

qm_modem_t qm_modem_init(const qm_modem_config_t *config)
{
    qm_err_t ret = QM_EOK;
    qm_task_t task = {0};
    qm_modem_handle_t *handle = NULL;

    handle = (qm_modem_handle_t*)qm_malloc(sizeof(qm_modem_handle_t));
    if(handle == NULL){
        return NULL;
    }

    memset(handle, 0, sizeof(qm_modem_handle_t));
    memcpy(&handle->config, config, sizeof(qm_modem_config_t));

    registerPSEventCallback(PS_GROUP_ALL_MASK, liot_netModePSUrcCallback);

    ret = liot_nw_register_cb(liot_nw_ind_callback);
    if (LIOT_NW_SUCCESS != ret){
        QM_LOGE(LOG_TAG, "liot_nw_register_cb failed, ret is 0x%x.", ret);
    }

    ret = qm_timer_new(&handle->timer, network_timeout, NULL, NETWORK_TIMEOUT, 0);
    if(ret != QM_EOK){
        return NULL;
    }

    qm_timer_start(&handle->timer);

    g_qm_modem_handle = handle;
    return (qm_modem_t)handle;
}

int qm_modem_get_imsi(qm_modem_t modem, char *imsi, int imsi_len)
{
    uint8_t nsim = LIOT_SIM_INVALID;
    liot_sim_errcode_e ret = LIOT_SIM_EXECUTE_ERR;
    ret = liot_sim_get_slot(&nsim);
    if(ret != LIOT_SIM_SUCCESS || nsim == LIOT_SIM_INVALID){
        return -QM_ERROR;
    }
    ret = liot_sim_get_imsi(nsim, imsi, imsi_len);
    if(ret != LIOT_SIM_SUCCESS){
        return -QM_ERROR;
    }
    return QM_EOK;
}

int qm_modem_get_csq(qm_modem_t modem, uint8_t *csq)
{
    uint8_t nsim = LIOT_SIM_INVALID;
    liot_nw_errcode_e errcode;
    liot_sim_errcode_e ret = LIOT_SIM_EXECUTE_ERR;
    if(csq == NULL){
        return -QM_EINVAL;
    }
    ret = liot_sim_get_slot(&nsim);
    if(ret != LIOT_SIM_SUCCESS || nsim == LIOT_SIM_INVALID){
        return -QM_ERROR;
    }
    ret = liot_nw_get_csq(nsim, csq);
    if(ret != LIOT_NW_SUCCESS){
        return -QM_ERROR;
    }
    return QM_EOK;
}

static void lbs_result_cb(liot_lbs_response_data_t *response_data)
{
    qm_modem_lbs_t modem_lbs = {0};
    char latitude_str[30]  = {0};
    char longitude_str[30] = {0};
    if (response_data == NULL || response_data->hndl == 0)
    {
        return;
    }

    qm_modem_handle_t *handle = (qm_modem_handle_t*)response_data->arg;

    QM_LOGD(LOG_TAG, 
        "==lbs_result_cb  lbs result: %d, response_data->pos_num:%d", response_data->result, response_data->pos_num);
    if (response_data->result == LIOT_LBS_OK)
    {
        memcpy(latitude_str, response_data->pos_info->latitude, strlen(response_data->pos_info->latitude));
        memcpy(longitude_str, response_data->pos_info->longitude, strlen(response_data->pos_info->longitude));
        QM_LOGD(LOG_TAG, "lbs_result_cb  Location: longitude:%s, latitude:%s", latitude_str, longitude_str);
        strcpy(modem_lbs.latitude, response_data->pos_info->latitude);
        strcpy(modem_lbs.longitude, response_data->pos_info->longitude);
        if(g_qm_modem_handle->config.event_handler){
            g_qm_modem_handle->config.event_handler(QM_MODEM_EVENT_LBS, &modem_lbs, sizeof(qm_modem_lbs_t), g_qm_modem_handle->config.arg);
        }
    }
}

static void liot_lbs_task(void *arg)
{
    uint8_t nSim = 0;
    int ret                  = 0;
    int cid                  = 1;
    int i                    = 0;
    liot_lbs_option_t lbs_option;
    liot_nw_cell_info_s cell_info;
    liot_lbs_client_hndl lbs_client = 0;
    liot_nw_seclection_info_s select_info;
    liot_lbs_cell_info_t lbs_cell_info[LBS_MAX_CELL_NUM] = {0};
    char imei_str[64]                                    = {0};
    qm_modem_handle_t *handle = (qm_modem_handle_t*)arg;

    liot_lbs_basic_info_t basic_info = {
        .type       = 1,
        .encrypt    = 1,
        .key_index  = 1,
        .pos_format = 1,
        .loc_method = 4,
    };

    liot_lbs_auth_info_t auth_info = {
        .user_name = "18857497092",
        .user_pwd  = "wwh84900592",
        .token     = "1AFC5EE2FF406650BE1B8C5E623E7FA9",
        .rand      = liot_rtos_rand(),
    };

    memset(&imei_str, 0x00, sizeof(imei_str));
    liot_dev_get_imei(imei_str, 64, nSim);

    strcpy(auth_info.imei, imei_str);

    if (liot_nw_get_cell_info(nSim, &cell_info) != LIOT_NW_SUCCESS)
    {
        QM_LOGD(LOG_TAG, "===============lbs get cell info fail===============\n");
        goto __exit;
    }
    ret = liot_nw_get_selection(nSim, &select_info);
    if (ret != 0)
    {
        QM_LOGD(LOG_TAG, "liot_nw_get_selection ret: %d", ret);
        goto __exit;
    } 
    QM_LOGD(LOG_TAG, 
        "%s:%d  info_num:%d  valid:%d", __FUNCTION__, __LINE__, cell_info.lte_info_num, cell_info.lte_info_valid);
    QM_LOGD(LOG_TAG, "nw_act_type=%d", select_info.act);

    if (select_info.act == LIOT_NW_ACCESS_TECH_E_UTRAN)
    {
        /*
            QM_LOGD(LOG_TAG, "mcc=%d,mnc=%d,cell_id=%d,signal=%d,tac=%d,bcch=%d,bsic=%d,rsrq=%d,pci=%d,earfcn=%d,lac_id:%d",
            lbs_cell_info.mcc,lbs_cell_info.mnc,lbs_cell_info.cell_id,lbs_cell_info.signal,lbs_cell_info.tac,
            lbs_cell_info.bcch,lbs_cell_info.bsic,lbs_cell_info.rsrq,lbs_cell_info.pci,lbs_cell_info.earfcn,lbs_cell_info.lac_id);
            */
        char mcc_str[5] = {0};
        char mnc_str[5] = {0};

        lbs_cell_info[0].radio = 3;

        snprintf(mcc_str, 5, "%03X", cell_info.lte_info[0].mcc);
        lbs_cell_info[0].mcc = atoi(mcc_str);
        memset(mcc_str, 0x0, 5);

        snprintf(mnc_str, 5, "%02X", cell_info.lte_info[0].mnc & 0XFFF);
        lbs_cell_info[0].mnc = atoi(mnc_str);
        memset(mnc_str, 0x0, 5);

        lbs_cell_info[0].cell_id = cell_info.lte_info[0].cid;
        lbs_cell_info[0].lac_id  = cell_info.lte_info[0].tac;
        lbs_cell_info[0].pci     = cell_info.lte_info[0].pci;
        lbs_cell_info[0].earfcn  = cell_info.lte_info[0].earfcn;
        lbs_cell_info[0].bcch    = cell_info.lte_info[0].earfcn;
        lbs_cell_info[0].signal  = cell_info.lte_info[0].rssi;

        for (i = 0; i < cell_info.lte_info_num; i++)
        {
            lbs_cell_info[i + 1].radio = 3;

            snprintf(mcc_str, 5, "%03X", cell_info.lte_info[i + 1].mcc);
            lbs_cell_info[i + 1].mcc = atoi(mcc_str);
            memset(mcc_str, 0x0, 5);

            snprintf(mnc_str, 5, "%02X", cell_info.lte_info[i + 1].mnc & 0XFFF);
            lbs_cell_info[i + 1].mnc = atoi(mnc_str);
            memset(mnc_str, 0x0, 5);

            lbs_cell_info[i + 1].cell_id = cell_info.lte_info[i + 1].cid;
            lbs_cell_info[i + 1].lac_id  = cell_info.lte_info[i + 1].tac;
            lbs_cell_info[i + 1].pci     = cell_info.lte_info[i + 1].pci;
            lbs_cell_info[i + 1].earfcn  = cell_info.lte_info[i + 1].earfcn;
            lbs_cell_info[i + 1].bcch    = cell_info.lte_info[i + 1].earfcn;
            lbs_cell_info[i + 1].signal  = cell_info.lte_info[i + 1].rssi;
        }

        QM_LOGD(LOG_TAG, "mcc=%d,mnc=%d,cell_id=%d,signal=%d,bcch=%d,pci=%d,earfcn=%d,lac_id:%d",
                   lbs_cell_info[0].mcc,
                   lbs_cell_info[0].mnc,
                   lbs_cell_info[0].cell_id,
                   lbs_cell_info[0].signal,
                   lbs_cell_info[0].bcch,
                   lbs_cell_info[0].pci,
                   lbs_cell_info[0].earfcn,
                   lbs_cell_info[0].lac_id);
    }
    else
    {
        QM_LOGD(LOG_TAG, "network access technology type error");
        goto __exit;
    }

    memset(&lbs_option, 0x00, sizeof(liot_lbs_option_t));
    lbs_option.pdp_cid     = cid;
    lbs_option.sim_id      = nSim;
    lbs_option.req_timeout = 60;
    lbs_option.basic_info  = &basic_info;
    lbs_option.auth_info   = &auth_info;
    lbs_option.cell_num    = cell_info.lte_info_num;
    lbs_option.cell_info   = lbs_cell_info;

    if (LIOT_LBS_OK == liot_lbs_get_position(&lbs_client,
                                             "http://locator-aep.xiot.senthink.com:80/locator/v0.1/locate",
                                             &lbs_option,
                                             lbs_result_cb,
                                             handle)){
        QM_LOGD(LOG_TAG, "lbs success");
    }

__exit:
   qm_task_exit(NULL);                               
}

int qm_modem_get_lbs(qm_modem_t modem)
{
    qm_err_t ret = QM_EOK;
    qm_modem_handle_t *handle = (qm_modem_handle_t*)modem;

    ret = qm_task_new(&handle->task, "lbs", liot_lbs_task, handle, 4096, CONFIG_QM_APP_TASK_PRIO);
    if(ret != QM_EOK){
        return ret;
    }
    return QM_EOK;
}

int qm_modem_start_ppp(qm_modem_t modem, uint32_t timeout)
{
    return QM_EOK;
}

int qm_modem_restart_ppp(qm_modem_t modem, uint32_t timeout)
{
    return QM_EOK;
}

int qm_modem_stop_ppp(qm_modem_t modem, uint32_t timeout)
{
    return QM_EOK;
}