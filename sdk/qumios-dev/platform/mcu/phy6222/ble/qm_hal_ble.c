#include "qm_types.h"
#include "qm_errno.h"
#include "qm_ble_hal_ble.h"
#include "qm_ble_hal_os.h"

#include "bcomdef.h"
#include "OSAL.h"
#include "linkdb.h"
#include "att.h"
#include "gatt.h"
#include "gatt_uuid.h"
#include "gattservapp.h"
#include "gapbondmgr.h"

#include "log.h"
#include "qm_ble_utils.h"
#include "qm_ble_hal_ble.h"
#include "qm_osal_task.h"

#define GATT_PROFILE_NUM_MAX    10

static qms_bt_init_t *qm_hal_ble_info = NULL;

uint16_t  gatt_profile_count = 0;
uint8_t qm_ble_notify_buff[23] = {0};
uint8_t qm_ble_indicate_buff[23] = {0};
gattCharCfg_t qm_ble_notify_config[GATT_MAX_NUM_CONN];
gattCharCfg_t qm_ble_indicate_config[GATT_MAX_NUM_CONN];
gattAttribute_t qm_ble_profile_table[GATT_PROFILE_NUM_MAX] = {0};

static uint8 qm_ble_hal_ReadAttrCB(uint16 connHandle, gattAttribute_t* pAttr,
                                    uint8* pValue, uint16* pLen, uint16 offset, uint8 maxLen);
static bStatus_t qm_ble_hal_WriteAttrCB(uint16 connHandle, gattAttribute_t* pAttr,
                                        uint8* pValue, uint16 len, uint16 offset);
                                        

static uint8 qm_ble_serviceUUID[ATT_BT_UUID_SIZE] =
{
    LO_UINT16(BLE_UUID_QMS_SERVICE), HI_UINT16(BLE_UUID_QMS_SERVICE)
};

static gattAttrType_t qm_ble_Service = {
    .len  = ATT_BT_UUID_SIZE,
    .uuid = qm_ble_serviceUUID,
}; 



CONST gattServiceCBs_t qm_ble_profilecbs =
{
    qm_ble_hal_ReadAttrCB,  // Read callback function pointer
    qm_ble_hal_WriteAttrCB, // Write callback function pointer
    NULL
};

static uint8 qm_ble_hal_ReadAttrCB(uint16 connHandle, gattAttribute_t* pAttr,
                            uint8* pValue, uint16* pLen, uint16 offset, uint8 maxLen)
{
    bStatus_t status = SUCCESS;

    // If attribute permissions require authorization to read, return error
    if (gattPermitAuthorRead(pAttr->permissions)){
        // Insufficient authorization
        return (ATT_ERR_INSUFFICIENT_AUTHOR);
    }

    // Make sure it's not a blob operation (no attributes in the profile are long)
    if ( offset > 0 ||  pAttr->type.len > ATT_BT_UUID_SIZE )
    {
        return ( ATT_ERR_ATTR_NOT_LONG );
    }

    uint16 uuid = BUILD_UINT16( pAttr->type.uuid[0], pAttr->type.uuid[1]);
    LOG("uuid %04X\n", uuid);
    memcpy(pValue, 0, maxLen);
    *pLen = 0;
    switch(uuid)
	{  
        case BLE_UUID_QMS_RC:{
            if (qm_hal_ble_info && qm_hal_ble_info->rc.on_read) {
                *pLen = qm_hal_ble_info->rc.on_read(pValue, maxLen);
            }
        }break; 
        default:
            status = ATT_ERR_READ_NOT_PERMITTED;
        break;
    }

    return (status);
}

static bStatus_t qm_ble_hal_WriteAttrCB(uint16 connHandle, gattAttribute_t* pAttr,
                                 uint8* pValue, uint16 len, uint16 offset)
{
    bStatus_t status = SUCCESS;
    // If attribute permissions require authorization to read, return error
    if (gattPermitAuthorRead(pAttr->permissions)){
        // Insufficient authorization
        return (ATT_ERR_INSUFFICIENT_AUTHOR);
    }

    // Make sure it's not a blob operation (no attributes in the profile are long)
    if ( offset > 0 ||  pAttr->type.len > ATT_BT_UUID_SIZE )
    {
        return ( ATT_ERR_ATTR_NOT_LONG );
    }
    static uint8_t ccc_change_id = 0;
    uint16 uuid = BUILD_UINT16( pAttr->type.uuid[0], pAttr->type.uuid[1]);
    LOG("uuid %04X \n", uuid);
    
    switch(uuid)
	{  
        case BLE_UUID_QMS_WC:{
            if (qm_hal_ble_info && qm_hal_ble_info->wc.on_write) {
                qm_hal_ble_info->wc.on_write(pValue, len);
            }
        }break;   
        case BLE_UUID_QMS_WWNRC:{
            if (qm_hal_ble_info && qm_hal_ble_info->wwnrc.on_write) {
                qm_hal_ble_info->wwnrc.on_write(pValue, len);
            }
        }break;   
        case 0x2902:{
            ccc_change_id ^= pValue[0];
            LOG("ccc_change_id %d \n", ccc_change_id);
            if(pValue[0] == 1){
                GATTServApp_ProcessCCCWriteReq(connHandle, pAttr, pValue, len,
                                                        offset, GATT_CLIENT_CFG_NOTIFY);
            }else if(pValue[0] == 2){
                GATTServApp_ProcessCCCWriteReq(connHandle, pAttr, pValue, len,
                                                        offset, GATT_CLIENT_CFG_INDICATE);
            }
            if (qm_hal_ble_info && qm_hal_ble_info->ic.on_ccc_change) {
                qm_hal_ble_info->ic.on_ccc_change((ccc_change_id & 2));
            }

            if (qm_hal_ble_info && qm_hal_ble_info->nc.on_ccc_change) {
                qm_hal_ble_info->nc.on_ccc_change((ccc_change_id & 1));
            }
        }break;  
        default:
        
        break;
    }

    return SUCCESS;
}


static void qm_ble_hal_handle_connstatus_cb( uint16 connHandle, uint8 changeType )
{
    // Make sure this is not loopback connection
    if ( connHandle != LOOPBACK_CONNHANDLE )
    {
        // Reset Client Char Config if connection has dropped
        if ( ( changeType == LINKDB_STATUS_UPDATE_REMOVED )      ||
                ( ( changeType == LINKDB_STATUS_UPDATE_STATEFLAGS ) &&
                  ( !linkDB_Up( connHandle ) ) ) )
        {
            LOG("clear client configuration\n");
        }
        LOG("changeType %d\n", changeType);
    }
    GATTServApp_InitCharCfg( connHandle, qm_ble_notify_config );
    GATTServApp_InitCharCfg( connHandle, qm_ble_indicate_config );
}


int qm_ble_hal_addservice(qms_bt_init_t *info)
{
    if(info == NULL){
        return -QM_EINVAL;
    }
    int ret = QMS_ERR_SUCCESS;
    qms_uuid_16_t *temp_uuid = NULL;
    // Register with Link DB to receive link status change callback
    GATTServApp_InitCharCfg( INVALID_CONNHANDLE, qm_ble_notify_config );
    GATTServApp_InitCharCfg( INVALID_CONNHANDLE, qm_ble_indicate_config );
    VOID linkDB_Register(qm_ble_hal_handle_connstatus_cb);
	
    qm_hal_ble_info = info;

    qm_ble_profile_table[gatt_profile_count].type.len    =  ATT_BT_UUID_SIZE;
    qm_ble_profile_table[gatt_profile_count].type.uuid   =  (uint8_t *)primaryServiceUUID;
    qm_ble_profile_table[gatt_profile_count].permissions =  GATT_PERMIT_READ;
    qm_ble_profile_table[gatt_profile_count].pValue =  (uint8_t *)&qm_ble_Service;
    gatt_profile_count++;

    {
        qm_ble_profile_table[gatt_profile_count].type.len    =  ATT_BT_UUID_SIZE;
        qm_ble_profile_table[gatt_profile_count].type.uuid   =  (uint8_t *)characterUUID;
        qm_ble_profile_table[gatt_profile_count].permissions =  GATT_PERMIT_READ;
        qm_ble_profile_table[gatt_profile_count].pValue =  (uint8_t *)&info->rc.prop;
        gatt_profile_count++;

        temp_uuid = (qms_uuid_16_t *)info->rc.uuid;
        qm_ble_profile_table[gatt_profile_count].type.len    =  ATT_BT_UUID_SIZE;
        qm_ble_profile_table[gatt_profile_count].type.uuid  =  (uint8_t *)&temp_uuid->val;
        qm_ble_profile_table[gatt_profile_count].permissions =  GATT_PERMIT_READ;
        qm_ble_profile_table[gatt_profile_count].pValue =  (uint8_t *)qm_ble_malloc(6);
        memset(qm_ble_profile_table[gatt_profile_count].pValue, 0, 6);
        gatt_profile_count++;
   
    }


    {
        qm_ble_profile_table[gatt_profile_count].type.len    =  ATT_BT_UUID_SIZE;
        qm_ble_profile_table[gatt_profile_count].type.uuid   =  (uint8_t *)characterUUID;
        qm_ble_profile_table[gatt_profile_count].permissions =  GATT_PERMIT_READ | GATT_PERMIT_WRITE;
        qm_ble_profile_table[gatt_profile_count].pValue =  (uint8_t *)&info->wc.prop;
        gatt_profile_count++;

        temp_uuid = (qms_uuid_16_t *)info->wc.uuid;
        qm_ble_profile_table[gatt_profile_count].type.len    =  ATT_BT_UUID_SIZE;
        qm_ble_profile_table[gatt_profile_count].type.uuid  =  (uint8_t *)&temp_uuid->val;
        qm_ble_profile_table[gatt_profile_count].permissions =  GATT_PERMIT_READ | GATT_PERMIT_WRITE;
        qm_ble_profile_table[gatt_profile_count].pValue =  (uint8_t *)qm_ble_malloc(21);
        memset(qm_ble_profile_table[gatt_profile_count].pValue, 0, 21);
        gatt_profile_count++;
    }

    {
        qm_ble_profile_table[gatt_profile_count].type.len    =  ATT_BT_UUID_SIZE;
        qm_ble_profile_table[gatt_profile_count].type.uuid   =  (uint8_t *)characterUUID;
        qm_ble_profile_table[gatt_profile_count].permissions =  GATT_PERMIT_READ ;
        qm_ble_profile_table[gatt_profile_count].pValue =  (uint8_t *)&info->ic.prop;
        gatt_profile_count++;

        temp_uuid = (qms_uuid_16_t *)info->ic.uuid;
        qm_ble_profile_table[gatt_profile_count].type.len    =  ATT_BT_UUID_SIZE;
        qm_ble_profile_table[gatt_profile_count].type.uuid  =  (uint8_t *)&temp_uuid->val;
        qm_ble_profile_table[gatt_profile_count].permissions =  GATT_PERMIT_READ ;
        qm_ble_profile_table[gatt_profile_count].pValue =  qm_ble_indicate_buff;
        gatt_profile_count++;


        qm_ble_profile_table[gatt_profile_count].type.len    =  ATT_BT_UUID_SIZE;
        qm_ble_profile_table[gatt_profile_count].type.uuid  =  clientCharCfgUUID;
        qm_ble_profile_table[gatt_profile_count].permissions =  GATT_PERMIT_READ | GATT_PERMIT_WRITE ;
        qm_ble_profile_table[gatt_profile_count].pValue =  (uint8_t *)qm_ble_indicate_config;
        gatt_profile_count++;

    }

    {
        qm_ble_profile_table[gatt_profile_count].type.len    =  ATT_BT_UUID_SIZE;
        qm_ble_profile_table[gatt_profile_count].type.uuid   =  (uint8_t *)characterUUID;
        qm_ble_profile_table[gatt_profile_count].permissions =  GATT_PERMIT_READ | GATT_PERMIT_WRITE;
        qm_ble_profile_table[gatt_profile_count].pValue =  (uint8_t *)&info->wwnrc.prop;
        gatt_profile_count++;

        temp_uuid = (qms_uuid_16_t *)info->wwnrc.uuid;
        qm_ble_profile_table[gatt_profile_count].type.len    =  ATT_BT_UUID_SIZE;
        qm_ble_profile_table[gatt_profile_count].type.uuid  =  (uint8_t *)&temp_uuid->val;
        qm_ble_profile_table[gatt_profile_count].permissions =  GATT_PERMIT_READ | GATT_PERMIT_WRITE;
        qm_ble_profile_table[gatt_profile_count].pValue =  (uint8_t *)qm_ble_malloc(21);
        memset(qm_ble_profile_table[gatt_profile_count].pValue, 0, 21);
        gatt_profile_count++;
    }

    {
        qm_ble_profile_table[gatt_profile_count].type.len    =  ATT_BT_UUID_SIZE;
        qm_ble_profile_table[gatt_profile_count].type.uuid   =  (uint8_t *)characterUUID;
        qm_ble_profile_table[gatt_profile_count].permissions =  GATT_PERMIT_READ;
        qm_ble_profile_table[gatt_profile_count].pValue =  (uint8_t *)&info->nc.prop;
        gatt_profile_count++;

        temp_uuid = (qms_uuid_16_t *)info->nc.uuid;
        qm_ble_profile_table[gatt_profile_count].type.len    =  ATT_BT_UUID_SIZE;
        qm_ble_profile_table[gatt_profile_count].type.uuid  =  (uint8_t *)&temp_uuid->val;
        qm_ble_profile_table[gatt_profile_count].permissions =  GATT_PERMIT_READ;
        qm_ble_profile_table[gatt_profile_count].pValue =  qm_ble_notify_buff;
        gatt_profile_count++;

        qm_ble_profile_table[gatt_profile_count].type.len    =  ATT_BT_UUID_SIZE;
        qm_ble_profile_table[gatt_profile_count].type.uuid  =  clientCharCfgUUID;
        qm_ble_profile_table[gatt_profile_count].permissions =  GATT_PERMIT_READ | GATT_PERMIT_WRITE ;
        qm_ble_profile_table[gatt_profile_count].pValue =  (uint8_t *)qm_ble_notify_config;
        gatt_profile_count++;

    }

    // Register GATT attribute list and CBs with GATT Server App
    ret = GATTServApp_RegisterService( qm_ble_profile_table,
                                          gatt_profile_count,
                                          &qm_ble_profilecbs );

    if(ret != QMS_ERR_SUCCESS){
        LOG("Add rawpass service failed!\n");
    }


    return NULL;
}