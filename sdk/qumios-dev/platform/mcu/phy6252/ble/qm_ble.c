#include "qm_types.h"
#include "qm_errno.h"
#include "qm_ble_hal_ble.h"
#include "qm_ble_hal_os.h"

#include "bcomdef.h"
#include "rf_phy_driver.h"
#include "global_config.h"
#include "OSAL.h"
#include "OSAL_PwrMgr.h"
#include "gatt.h"
#include "hci.h"
#include "gapgattserver.h"
#include "gattservapp.h"
#include "peripheral.h"
#include "gapbondmgr.h"
#include "pwrmgr.h"
#include "ll.h"
#include "ll_hw_drv.h"
#include "ll_def.h"
#include "hci_tl.h"

#include "qm_osal_task.h"

#define SBP_START_DEVICE_EVT          0x0001
#define SBP_RESET_ADV_EVT             0x0002
#define SBP_RESET_ADV_EVT             0x0002

// Minimum connection interval (units of 1.25ms, 80=100ms) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_MIN_CONN_INTERVAL     80//32//80

// Maximum connection interval (units of 1.25ms, 800=1000ms) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_MAX_CONN_INTERVAL     800//48//800

// Supervision timeout value (units of 10ms, 1000=10s) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_CONN_TIMEOUT          1000//1000

// Whether to enable automatic parameter update request when a connection is formed
#define DEFAULT_ENABLE_UPDATE_REQUEST         TRUE

// Connection Pause Peripheral time value (in seconds)
#define DEFAULT_CONN_PAUSE_PERIPHERAL         6

#define INVALID_CONNHANDLE                    0xFFFF


/*********************************************************************
    GLOBAL VARIABLES
*********************************************************************/

perStatsByChan_t g_perStatsByChanTest;
static qms_bt_init_t *qm_hal_ble_info = NULL;
static void (*g_indication_txdone)(uint8_t res);

extern uint16_t  gatt_profile_count;
extern uint8_t qm_ble_notify_buff[23];
extern uint8_t qm_ble_indicate_buff[23];
extern int qm_ble_hal_addservice(qms_bt_init_t *info);
extern gattCharCfg_t qm_ble_notify_config[GATT_MAX_NUM_CONN];
extern gattCharCfg_t qm_ble_indicate_config[GATT_MAX_NUM_CONN];
extern gattAttribute_t qm_ble_profile_table[10];

void qm_ble_disconnect(uint8_t reason)
{
    uint8_t zreason = 0;
    switch (reason) {
        case QMS_BT_REASON_REMOTE_USER_TERM_CONN:
            zreason = HCI_DISCONNECT_REMOTE_USER_TERM;
            break;
        default:
            zreason = HCI_DISCONNECT_AUTH_FAILURE;
            break;  
    }

    uint16_t connHandle = 0;
    GAPRole_GetParameter(GAPROLE_CONNHANDLE, &connHandle);    
    if(connHandle == INVALID_CONNHANDLE){
        return;
    }

    HCI_DisconnectCmd(connHandle, zreason);
}

qms_err_t qm_ble_send_indication(uint8_t *p_data, uint16_t length, void (*txdone)(uint8_t res))
{
    if(p_data == NULL || length == 0){
        return -QMS_ERR_INVALID_ADV_DATA;
    }
    
    if(txdone){
        g_indication_txdone = txdone;
    }

    uint8_t value = 0;
    uint16 connHandle = 0;
    attHandleValueInd_t indication = {0};
    GAPRole_GetParameter(GAPROLE_CONNHANDLE, &connHandle);
    value = GATTServApp_ReadCharCfg(connHandle, qm_ble_indicate_config);
    if (connHandle == INVALID_CONNHANDLE){
        return -QMS_ERR_GATT_INDICATE_FAIL;
    }
    // If notifications enabled
    if (value & GATT_CLIENT_CFG_INDICATE){
        // Set the handle
        gattAttribute_t *pAttr = NULL;
        pAttr = GATTServApp_FindAttr( qm_ble_profile_table, gatt_profile_count, qm_ble_indicate_buff);
        if(pAttr == NULL){
            return -QMS_ERR_GATT_INDICATE_FAIL;
        }

        indication.len = length;
        indication.handle = pAttr->handle;
        memcpy(indication.value, p_data, length);
        custom_hex_log("indicaton:", indication.value, indication.len);
        // Send the Indication
        return GATT_Indication(connHandle, &indication, FALSE, QM_BLE_OSAL_TASKID);
    }

    return -QMS_ERR_GATT_INDICATE_FAIL;
}

qms_err_t qm_ble_send_notification(uint8_t *p_data, uint16_t length)
{
    if(p_data == NULL || length == 0){
        return -QMS_ERR_INVALID_ADV_DATA;
    }
    uint8_t value = 0;
    uint16_t connHandle = 0;
    attHandleValueNoti_t notify = {0};
    GAPRole_GetParameter(GAPROLE_CONNHANDLE, &connHandle);
    value = GATTServApp_ReadCharCfg(connHandle, qm_ble_notify_config);
    if (connHandle == INVALID_CONNHANDLE){
           return -QMS_ERR_GATT_NOTIFY_FAIL;
    }   

    // If notifications enabled
    if (value & GATT_CLIENT_CFG_NOTIFY)
    {
        gattAttribute_t *pAttr = NULL;
        // Find the characteristic value attribute
        pAttr = GATTServApp_FindAttr( qm_ble_profile_table, gatt_profile_count, qm_ble_notify_buff);
        if(pAttr == NULL){
            return -QMS_ERR_GATT_INDICATE_FAIL;
        }

        notify.len = length;
        notify.handle = pAttr->handle; 
        // Send the Notification
        memcpy(notify.value, p_data, length);
        GATT_Notification(connHandle, &notify, FALSE);
        return QMS_ERR_SUCCESS;
    }    

    return -QMS_ERR_GATT_NOTIFY_FAIL;
}

static void peripheralStateNotificationCB( gaprole_States_t newState )
{
    switch ( newState )
    {
        case GAPROLE_STARTED:
        {

        }break;
        case GAPROLE_ADVERTISING:

        break;
        case GAPROLE_WAITING:
            if (qm_hal_ble_info && qm_hal_ble_info->on_disconnected) {
                qm_hal_ble_info->on_disconnected();
            }
            break;
        case GAPROLE_CONNECTED:
            HCI_PPLUS_ConnEventDoneNoticeCmd(QM_BLE_OSAL_TASKID, NULL);
            if (qm_hal_ble_info && qm_hal_ble_info->on_connected) {
                qm_hal_ble_info->on_connected();
            }
            break;

        case GAPROLE_CONNECTED_ADV:
            break;

        case GAPROLE_WAITING_AFTER_TIMEOUT:
            break;

        case GAPROLE_ERROR:
            break;

        default:
            break;
        }
		
        LOG("[GAP ROLE %d]\n",newState);
}


// GAP Role Callbacks
static gapRolesCBs_t simpleBLEPeripheral_PeripheralCBs =
{
    peripheralStateNotificationCB,  // Profile State Change Callbacks
    NULL,       // When a valid RSSI is read from controller (not used by application)
};


uint16 QM_BLE_ProcessEvent( uint8 task_id, uint16 events )
{
    if ( events & SYS_EVENT_MSG )
    {
        uint8* pmsg;
        if (NULL != (pmsg = osal_msg_receive(QM_BLE_OSAL_TASKID))){
            osal_event_hdr_t *gatt_msg = (osal_event_hdr_t *)pmsg;
            if(gatt_msg->event == GATT_MSG_EVENT){
                if(g_indication_txdone ){
                    g_indication_txdone(0);
                }
            }
            VOID osal_msg_deallocate(pmsg); // Release the OSAL message
        }
        // return unprocessed events
        return (events ^ SYS_EVENT_MSG);
    }
    if ( events & SBP_START_DEVICE_EVT ){
        // Start the Device
        VOID GAPRole_StartDevice( &simpleBLEPeripheral_PeripheralCBs );
        HCI_LE_ReadResolvingListSizeCmd();
        return ( events ^ SBP_START_DEVICE_EVT );
    }

    if ( events & SBP_RESET_ADV_EVT ){
        uint8 initial_advertising_enable = TRUE;
        GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8 ), &initial_advertising_enable );
        return ( events ^ SBP_RESET_ADV_EVT );
    }

    return 0; 
}

qms_err_t qm_ble_stack_init(qms_bt_init_t *info)
{   
    if(info == NULL){
        return -QM_EINVAL;
    }

    qm_hal_ble_info = info;
    // Setup the GAP
    VOID GAP_SetParamValue( TGAP_CONN_PAUSE_PERIPHERAL, DEFAULT_CONN_PAUSE_PERIPHERAL );
    // Setup the GAP Peripheral Role Profile
   {
        // device starts advertising upon initialization
        uint8 initial_advertising_enable = QM_FALSE;
        uint8 enable_update_request = DEFAULT_ENABLE_UPDATE_REQUEST;
        uint8 advChnMap = GAP_ADVCHAN_37 | GAP_ADVCHAN_38 | GAP_ADVCHAN_39;
        // By setting this to zero, the device will go into the waiting state after
        // being discoverable for 30.72 second, and will not being advertising again
        // until the enabler is set back to TRUE
        uint16 gapRole_AdvertOffTime = 0;
        uint16 desired_min_interval = DEFAULT_DESIRED_MIN_CONN_INTERVAL;
        uint16 desired_max_interval = DEFAULT_DESIRED_MAX_CONN_INTERVAL;
        uint16 desired_conn_timeout = DEFAULT_DESIRED_CONN_TIMEOUT;

        uint8 advType = LL_ADV_CONNECTABLE_UNDIRECTED_EVT;// LL_ADV_NONCONNECTABLE_UNDIRECTED_EVT;//LL_ADV_SCANNABLE_UNDIRECTED_EVT;//LL_ADV_CONNECTABLE_LDC_DIRECTED_EVT;//;    // it seems a  bug to set GAP_ADTYPE_ADV_NONCONN_IND = 0x03
        GAPRole_SetParameter( GAPROLE_ADV_EVENT_TYPE, sizeof( uint8 ), &advType ); 
        // set adv channel map
        GAPRole_SetParameter(GAPROLE_ADV_CHANNEL_MAP, sizeof(uint8), &advChnMap);
     // Set the GAP Role Parameters
        GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8 ), &initial_advertising_enable);
        GAPRole_SetParameter( GAPROLE_ADVERT_OFF_TIME, sizeof( uint16 ), &gapRole_AdvertOffTime );  
        GAPRole_SetParameter( GAPROLE_PARAM_UPDATE_ENABLE, sizeof( uint8 ), &enable_update_request );
        GAPRole_SetParameter( GAPROLE_MIN_CONN_INTERVAL, sizeof( uint16 ), &desired_min_interval );
        GAPRole_SetParameter( GAPROLE_MAX_CONN_INTERVAL, sizeof( uint16 ), &desired_max_interval );
        GAPRole_SetParameter( GAPROLE_TIMEOUT_MULTIPLIER, sizeof( uint16 ), &desired_conn_timeout );
   } 

    // Set advertising interval
    {
        uint16 advInt = 2400;//2400;//1600;//1600;//800;//1600;   // actual time = advInt * 625us
        GAP_SetParamValue( TGAP_LIM_DISC_ADV_INT_MIN, advInt );
        GAP_SetParamValue( TGAP_LIM_DISC_ADV_INT_MAX, advInt );
        GAP_SetParamValue( TGAP_GEN_DISC_ADV_INT_MIN, advInt );
        GAP_SetParamValue( TGAP_GEN_DISC_ADV_INT_MAX, advInt );
    }  

    // Initialize GATT attributes
    GGS_AddService( GATT_ALL_SERVICES );            // GAP
    GATTServApp_AddService( GATT_ALL_SERVICES );    // GATT attributes
    qm_ble_hal_addservice(info);

    uint8_t mtuSet = 247;
    llInitFeatureSet2MPHY(TRUE);
    llInitFeatureSetDLE(TRUE);
    ATT_SetMTUSizeMax(mtuSet);
    LOG("[2Mbps | DLE | MTU %d] \n",mtuSet);

    // Setup a delayed profile startup
    osal_set_event(QM_BLE_OSAL_TASKID, SBP_START_DEVICE_EVT );
    // for receive HCI complete message
    GAP_RegisterForHCIMsgs(QM_BLE_OSAL_TASKID);
    LL_PLUS_PerStats_Init(&g_perStatsByChanTest); 
    LOG("=====SimpleBLEPeripheral_Init Done=====\n");     
    return QMS_ERR_SUCCESS;
}

qms_err_t qm_ble_stack_deinit(void)
{
    
    return QMS_ERR_SUCCESS;
}



qms_err_t qm_ble_advertising_start(qms_adv_init_t *adv)
{
    uint8_t flag = 0;
    uint16_t index = 0;
    uint8_t srv[] = {0xB7, 0xFE};
    uint8_t initial_advertising_enable = QM_TRUE;
    uint32_t malloc_len = adv->vdata.len + 2 + strlen(adv->name.name) + 2 + 3 + 4;

    uint8_t *adv_data = (uint8_t *)qm_ble_malloc(malloc_len);
    if(adv_data == NULL){
        return -QMS_ERR_MEM_FAIL;
    }
    memset(adv_data, 0, adv->vdata.len + 9);

    if (adv->flag & QMS_AD_GENERAL) {
        flag |= GAP_ADTYPE_FLAGS_GENERAL;
    }

    if (adv->flag & QMS_AD_NO_BREDR) {
        flag |= GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED;
    }

    if(adv->flag & QMS_AD_LIMITED){
        flag |= GAP_ADTYPE_FLAGS_LIMITED;
    }    

    adv_data[index++] = 2;
    adv_data[index++]=  GAP_ADTYPE_FLAGS;
    adv_data[index++] = flag;

    adv_data[index++] = sizeof(uint16_t) + 1;
    adv_data[index++] = GAP_ADTYPE_16BIT_COMPLETE; //BT_DATA_UUID16_ALL
    adv_data[index++] = srv[0];
    adv_data[index++] = srv[1];

    adv_data[index++] = strlen(adv->name.name) + 1;
    switch (adv->name.ntype) {
        case QMS_ADV_NAME_SHORT:
            adv_data[index++]  = GAP_ADTYPE_LOCAL_NAME_SHORT;
            break;
        case QMS_ADV_NAME_FULL:
            adv_data[index++]  = GAP_ADTYPE_LOCAL_NAME_COMPLETE;
            break;
        default:
            LOG("Invalid adv name type.\r\n");
            return QMS_ERR_INVALID_ADV_DATA;
    }
    memcpy(&adv_data[index], adv->name.name, strlen(adv->name.name));
    index += strlen(adv->name.name);
    if (adv->vdata.len != 0) {
        adv_data[index++]     = adv->vdata.len + 1;
        adv_data[index++] = GAP_ADTYPE_MANUFACTURER_SPECIFIC; // BT_DATA_MANUFACTURER_DATA
        memcpy(&adv_data[index], adv->vdata.data, adv->vdata.len);
    } 

    custom_hex_log("adv_data",  adv_data, adv->vdata.len + 9);
    LOG("adv_data name  %s len %d",  adv->name.name, strlen(adv->name.name));

    // Set the GAP Characteristics
    GAPRole_SetParameter( GAPROLE_ADVERT_DATA, adv->vdata.len + 9, adv_data);
    GGS_SetParameter( GGS_DEVICE_NAME_ATT, strlen(adv->name.name), adv->name.name );
    GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8_t ), &initial_advertising_enable );
    LOG("======================qm_ble_advertising_start====================\n");
    qm_ble_free(adv_data);
    adv_data = NULL;
    return QMS_ERR_SUCCESS;
}   

qms_err_t qm_ble_advertising_stop(void)
{
    uint8_t initial_advertising_enable = QM_FALSE;
    GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8_t ), &initial_advertising_enable );
    return 0;
}

qms_err_t qm_ble_get_mac(uint8_t *mac)
{
    qms_err_t err = 0;
    err = GAPRole_GetParameter(GAPROLE_BD_ADDR, mac);
    return err;
}


#ifdef EN_QM_BLE_LONG_MTU
int qm_ble_get_att_mtu(uint16_t *att_mtu)
{
    return 0;
}
#endif


