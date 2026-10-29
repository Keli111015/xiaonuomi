
#include "qm.h"
#include "qm_ota.h"
#include "qm_work.h"
#include "qm_utils_string.h"
#include "qm_utils_list.h"

#include "harmony_ble_hal.h"
#include "harmony_ble_ota.h"

#include "cJSON.h"
#include "mbedtls/sha256.h"
#define LOG_TAG "harmony ota"

#define STR_MAC_LEN             (12)
#define OTA_UPDATE_DATA_LEN     (160 * 10)

#define MAKE_U16(h,l) 			((uint16_t)(((h) << 8) | (l)))
#define MAKE_U32(a,b,c,d)		((uint32_t)(((a) << 24) | ((b) << 16) | ((c) << 8) | (d)))


typedef struct 
{
    uint8_t recv_seg;
    uint16_t start_index;
    uint16_t old_index;
    uint32_t seg_map1;
    uint32_t seg_map2;
    uint8_t data_type;
    uint32_t file_size;
    int remain_file_size;
    int total_file_size;
    uint8_t payload[1];
}harmony_ble_ota_rx_data_t;

typedef struct 
{
    uint8_t hash_value[32];
    harmony_ble_comm_param_t *param;
    harmony_ble_ota_rx_data_t *rx_data;
}harmony_ble_ota_ctx_t;

static harmony_ble_ota_ctx_t g_ota_ctx = {0};

static int ota_ctrl_net_info_respone(uint8_t *data, int len);
static int ota_ctrl_get_info_respone(uint8_t *data, int len);
static int ota_ctrl_result_respone(uint8_t *data, int len);

static void txdone(uint8_t res)
{

}

static char str_is_number(char c) {
    return (c >= '0' && c <= '9');
}

static char *cjson_str_get(cJSON *cjson, char *key)
{
    cJSON *tmp = NULL;
    tmp = cJSON_GetObjectItem(cjson, key);
    if(tmp == NULL){
        return NULL;
    }
    return tmp->valuestring;
}

static int ota_ctrl_net_info_respone(uint8_t *data, int len)
{
    int index = 0;
    uint8_t rsp_data[HARMONY_BLE_OTA_NEG_REP_SIZE] = {0};
    uint16_t cmd = HARMONY_BLE_OTA_UP_CMD_PACK(HARMONY_BLE_OTA_CMD_NEG_INFO);

    cmd = cpu_to_be16(cmd);
    index += sizeof(uint16_t);
    memcpy(rsp_data, &cmd, sizeof(uint16_t));

    rsp_data[index] = CONFIG_HARMONY_BLE_OTA_PACKET_SIZE;

    return harmony_ble_hal_ota_send_indication(rsp_data, HARMONY_BLE_OTA_NEG_REP_SIZE, txdone);
}

static int ota_ctrl_result_respone(uint8_t *data, int len)
{
    int index = 0;
    uint8_t rsp_data[HARMONY_BLE_OTA_RESULT_REP_SIZE] = {0};
    uint16_t cmd = HARMONY_BLE_OTA_UP_CMD_PACK(HARMONY_BLE_OTA_CMD_OTA_RESULT);

    cmd = cpu_to_be16(cmd);
    index += sizeof(uint16_t);
    memcpy(rsp_data, &cmd, sizeof(uint16_t));

    rsp_data[index] = data[0];

    return harmony_ble_hal_ota_send_indication(rsp_data, HARMONY_BLE_OTA_RESULT_REP_SIZE, txdone);
}

static int ota_ctrl_pack_pid_name(char *str_pid, uint8_t *out_data)
{
    uint8_t str_len = 0;
    uint8_t index = 0;
    uint8_t pid_index = 0;
    QM_RETURN_ON_FALSE(str_pid, -QM_EINVAL, LOG_TAG, "str_pid NULL");
    QM_RETURN_ON_FALSE(out_data, -QM_EINVAL, LOG_TAG, "out_data INVAL");    

    str_len = strlen(str_pid) * 2;
    while (index < str_len)
    {
        out_data[index++] = str_is_number(str_pid[pid_index]) ? '0' : '1';
        out_data[index++] = str_pid[pid_index];
        pid_index++;
    }

    return QM_EOK;
}

static int ota_ctrl_pack_device_type(uint8_t *out_data)
{
    QM_RETURN_ON_FALSE(out_data, -QM_EINVAL, LOG_TAG, "out_data invail");
    
    memcpy(out_data, CONFIG_HARMONY_BLE_OTA_DEVICE_TYPE, strlen(CONFIG_HARMONY_BLE_OTA_DEVICE_TYPE));

    return QM_EOK;
}

static int ota_ctrl_pack_str_mac(uint8_t *out_data)
{
    uint8_t xj_mac[6] = {0};
    char str_mac[STR_MAC_LEN + 1] = {0};
    QM_RETURN_ON_FALSE(out_data, -QM_EINVAL, LOG_TAG, "out_data invail");

    qm_ble_gap_get_local_addr(xj_mac);
    qm_snprintf(str_mac, STR_MAC_LEN + 1, "%02X%02X%02X%02X%02X%02X", xj_mac[0], xj_mac[1], xj_mac[2], xj_mac[3], xj_mac[4], xj_mac[5]);
    memcpy(out_data, str_mac, strlen(str_mac));

    return QM_EOK;
}

static int ota_ctrl_pack_fw_version(char *fw_ver, uint8_t *out_data)
{
    QM_RETURN_ON_FALSE(fw_ver, -QM_EINVAL, LOG_TAG, "fw_ver invail");
    QM_RETURN_ON_FALSE(out_data, -QM_EINVAL, LOG_TAG, "out_data invail");
    memcpy(out_data, fw_ver, strlen(fw_ver));

    return QM_EOK;
}

static int ota_ctrl_pack_mcu_info(char *mcu_info, uint8_t *out_data)
{
    QM_RETURN_ON_FALSE(mcu_info, -QM_EINVAL, LOG_TAG, "fw_ver invail");
    QM_RETURN_ON_FALSE(out_data, -QM_EINVAL, LOG_TAG, "out_data invail");
    memcpy(out_data, mcu_info, strlen(mcu_info));

    return QM_EOK;
}

static int ota_ctrl_get_info_respone(uint8_t *data, int len)
{

    int index = 0;
    uint8_t rsp_data[HARMONY_BLE_OTA_GET_REP_SIZE] = {0};
    uint16_t cmd = HARMONY_BLE_OTA_UP_CMD_PACK(HARMONY_BLE_OTA_CMD_GET_INFO);
    harmony_ble_comm_info_t *info = &g_ota_ctx.param->info;
    
    cmd = cpu_to_be16(cmd);
    memcpy(rsp_data, &cmd, sizeof(uint16_t));
    index += sizeof(uint16_t);
    // DeviceName产品id的数字前加0，大写字母转换为小写后，前加1，例如2EKT，转换成021e1k1t，然后再转换成ASCII码，并在最后补上00，
    rsp_data[index++] = HARMONY_BLE_OTA_GET_DEVICE_NAME;
    rsp_data[index++] = strlen(info->productid) * 2 + 1;
    ota_ctrl_pack_pid_name(info->productid, rsp_data + index);
    index += strlen(info->productid) * 2 + 1;

    // DeviceSUBName产品id的数字前加0，大写字母转换为小写后，前加1，例如2EKT，转换成021e1k1t，然后再转换成ASCII码，并在最后补上00，
    rsp_data[index++] = HARMONY_BLE_OTA_GET_DEVICE_SUB_NAME;
    rsp_data[index++] = strlen(info->productid) * 2 + 1;
    ota_ctrl_pack_pid_name(info->productid, rsp_data + index);
    index += strlen(info->productid) * 2 + 1;

    // 模组设备类型 默认为0
    rsp_data[index++] = HARMONY_BLE_OTA_GET_DEVICE_TYPE;
    rsp_data[index++] = strlen(CONFIG_HARMONY_BLE_OTA_DEVICE_TYPE) + 1;
    ota_ctrl_pack_device_type(rsp_data + index);
    index += strlen(CONFIG_HARMONY_BLE_OTA_DEVICE_TYPE) + 1;

    // 模组设备ID 默认设备mac地址
    rsp_data[index++] = HARMONY_BLE_OTA_GET_DEVICE_ID;
    rsp_data[index++] = STR_MAC_LEN + 1;
    ota_ctrl_pack_str_mac(rsp_data + index);
    index += STR_MAC_LEN + 1;

    // 模组固件版本 
    rsp_data[index++] = HARMONY_BLE_OTA_GET_DEVICE_VER;
    rsp_data[index++] = strlen(info->fw_ver) + 1;
    ota_ctrl_pack_fw_version(info->fw_ver, rsp_data + index);
    index += strlen(info->fw_ver) + 1;

    rsp_data[index++] = HARMONY_BLE_OTA_GET_MCU_DEVICE_NAME;
#if !CONFIG_HARMONY_BLE_MCU_OTA_SUPPORT
    rsp_data[index++] = strlen(HARMONY_BLE_OTA_INVAL_DATA) + 1;
    ota_ctrl_pack_mcu_info(HARMONY_BLE_OTA_INVAL_DATA, rsp_data + index);
    index += strlen(HARMONY_BLE_OTA_INVAL_DATA) + 1;
#endif

    rsp_data[index++] = HARMONY_BLE_OTA_GET_MCU_DEVICE_SUB_NAME;
#if !CONFIG_HARMONY_BLE_MCU_OTA_SUPPORT
    rsp_data[index++] = strlen(HARMONY_BLE_OTA_INVAL_DATA) + 1;
    ota_ctrl_pack_mcu_info(HARMONY_BLE_OTA_INVAL_DATA, rsp_data + index);
    index += strlen(HARMONY_BLE_OTA_INVAL_DATA) + 1;
#endif

    rsp_data[index++] = HARMONY_BLE_OTA_GET_MCU_DEVICE_TYPE;
#if !CONFIG_HARMONY_BLE_MCU_OTA_SUPPORT
    rsp_data[index++] = strlen(HARMONY_BLE_OTA_INVAL_DATA) + 1;
    ota_ctrl_pack_mcu_info(HARMONY_BLE_OTA_INVAL_DATA, rsp_data + index);
    index += strlen(HARMONY_BLE_OTA_INVAL_DATA) + 1;
#endif

    rsp_data[index++] = HARMONY_BLE_OTA_GET_MCU_DEVICE_ID;
#if !CONFIG_HARMONY_BLE_MCU_OTA_SUPPORT
    rsp_data[index++] = strlen(HARMONY_BLE_OTA_INVAL_DATA) + 1;
    ota_ctrl_pack_mcu_info(HARMONY_BLE_OTA_INVAL_DATA, rsp_data + index);
    index += strlen(HARMONY_BLE_OTA_INVAL_DATA) + 1;
#endif

    rsp_data[index++] = HARMONY_BLE_OTA_GET_MCU_DEVICE_VER;
#if !CONFIG_HARMONY_BLE_MCU_OTA_SUPPORT
    rsp_data[index++] = strlen(HARMONY_BLE_OTA_INVAL_DATA) + 1;
    ota_ctrl_pack_mcu_info(HARMONY_BLE_OTA_INVAL_DATA, rsp_data + index);
    index += strlen(HARMONY_BLE_OTA_INVAL_DATA) + 1;
#endif
    QM_HEX_LOGD(LOG_TAG, "GET INFO RSP ", rsp_data, index);

    return harmony_ble_hal_ota_send_indication(rsp_data, index, txdone);
}

static int ota_ctrl_check_info_respone(uint8_t *data, int len)
{
    uint8_t result = HARMONY_BLE_OTA_CHECK_RESULT_SUCCESS;
    int index = HARMONY_BLE_OTA_HEADER_SIZE;
    uint8_t rsp_data[HARMONY_BLE_OTA_CHECK_REP_SIZE] = {0};
    uint16_t cmd = HARMONY_BLE_OTA_UP_CMD_PACK(HARMONY_BLE_OTA_CMD_CHECK_INFO);

    while (index < len)
    {
        switch (data[index++])
        {
            case HARMONY_BLE_OTA_CHECK_PROD_ID:
                QM_LOGD(LOG_TAG, "LEN %d PID %s", data[index], &data[index+1]);

            break;
        
            case HARMONY_BLE_OTA_CHECK_BIN_VER:
                QM_LOGD(LOG_TAG, "LEN %d  BIN VER %s", data[index], &data[index+1]);
            break;

            case HARMONY_BLE_OTA_CHECK_BIN_SIZE:
            {
                uint32_t bin_size = 0;
                memcpy(&bin_size, &data[index+1], sizeof(uint32_t));
                bin_size = be32_to_cpu(bin_size);
                QM_LOGD(LOG_TAG, "BIN SIZE %d", bin_size);
            }
            break;

            default:

            break;
        }
        index += data[index] + 1;
    }
    
    cmd = cpu_to_be16(cmd);
    memcpy(rsp_data, &cmd, sizeof(uint16_t));
    index = sizeof(uint16_t);
    rsp_data[index] = result;

    return harmony_ble_hal_ota_send_indication(rsp_data, HARMONY_BLE_OTA_CHECK_REP_SIZE, txdone);
}

int harmony_ble_ota_ctrl_read(uint8_t *data, int len)
{
    harmony_ble_ota_cmd_t cmd = HARMONY_BLE_OTA_CMD_ERROR;
    QM_RETURN_ON_FALSE(data, -QM_EINVAL, LOG_TAG, "data NULL");
    QM_RETURN_ON_FALSE(len >= 2, -QM_EINVAL, LOG_TAG, "len INVAL");    

    cmd = HARMONY_BLE_OTA_CMD_GET(data);
    switch (cmd)
    {
        case HARMONY_BLE_OTA_CMD_NEG_INFO:
            QM_LOGD(LOG_TAG, "HARMONY_BLE_OTA_CMD_NEG_INFO");
            ota_ctrl_net_info_respone(data, len);
        break;

        case HARMONY_BLE_OTA_CMD_GET_INFO:
            QM_LOGD(LOG_TAG, "HARMONY_BLE_OTA_CMD_GET_INFO");
            ota_ctrl_get_info_respone(data, len);
        break;

        case HARMONY_BLE_OTA_CMD_CHECK_INFO:
            QM_LOGD(LOG_TAG, "HARMONY_BLE_OTA_CMD_CHECK_INFO");
            ota_ctrl_check_info_respone(data, len);
        break;
        
        default:
        break;
    }


    return QM_EOK;
}

static int ota_moudle_hash_value_set(uint8_t *str_data)
{
    int ret = QM_EOK;
    char *str = NULL;
    cJSON *root = NULL;
    cJSON *params_data = NULL;

    QM_RETURN_ON_FALSE(str_data, -QM_EINVAL, LOG_TAG, "data NULL");

    root = cJSON_Parse((const char*)str_data);
    if(root == NULL){
        return -QM_EINVAL;
    }

    params_data = cJSON_GetObjectItem(root, HARMONY_BLE_OTA_BIN_NAME1);
    if(params_data == NULL){
        QM_LOGE(LOG_TAG, "json data key get fail");
        ret = -QM_EINVAL;
        goto __exit;
    }

    str = cJSON_PrintUnformatted(params_data);
    QM_LOGE(LOG_TAG, "json value : %s", str);
    cJSON_free(str);
    
    str = cjson_str_get(params_data, HARMONY_BLE_OTA_HASH_VALUE);
    if(str == NULL){
        QM_LOGE(LOG_TAG, "json value key get fail");
        ret = -QM_EINVAL;
        goto __exit;  
    }

    hex_str_to_nums(str, strlen(str), g_ota_ctx.hash_value);
    QM_HEX_LOGD(LOG_TAG, "SHA256 VALUE ", g_ota_ctx.hash_value, (strlen(str) / 2));
 
__exit:
    cJSON_Delete(root);
    return ret;
}

static void ota_data_flash_write(void)
{
    int index = 0;
    uint32_t off_set = 0;   
    uint8_t write_data[CONFIG_HARMONY_BLE_OTA_PAGE_SIZE] = {0};
    int write_size =  g_ota_ctx.rx_data->file_size;
    harmony_ble_ota_rx_data_t *rx_data = g_ota_ctx.rx_data;

    if(rx_data == NULL){
        return;
    }

    for (index = 0; index < write_size; index += CONFIG_HARMONY_BLE_OTA_PAGE_SIZE)
    {
        if (index + CONFIG_HARMONY_BLE_OTA_PAGE_SIZE <= write_size){
            qm_ota_write(&off_set, rx_data->payload + index, CONFIG_HARMONY_BLE_OTA_PAGE_SIZE);
            g_ota_ctx.rx_data->file_size -= CONFIG_HARMONY_BLE_OTA_PAGE_SIZE;
        }
    }

    if(g_ota_ctx.rx_data->file_size > 0){
        memcpy(write_data, rx_data->payload + index - CONFIG_HARMONY_BLE_OTA_PAGE_SIZE, g_ota_ctx.rx_data->file_size);
        memset(&rx_data->payload[0], 0, OTA_UPDATE_DATA_LEN);
        memcpy(&rx_data->payload[0], write_data, g_ota_ctx.rx_data->file_size);
    }else{
        memset(&rx_data->payload[0], 0, OTA_UPDATE_DATA_LEN);
    }   

    if(g_ota_ctx.rx_data->remain_file_size <= 0){
        if(g_ota_ctx.rx_data->file_size > 0){
            qm_ota_write(&off_set, rx_data->payload + index - CONFIG_HARMONY_BLE_OTA_PAGE_SIZE, g_ota_ctx.rx_data->file_size);
        }
    }
}

static void ota_data_flash_verify_read(void)
{
    int read_index = 0;
    uint32_t off_set = 0;
    uint8_t  *updata = NULL;
    uint8_t reuslt = 0;
    mbedtls_sha256_context sha256Ctx = {0};
    harmony_ble_ota_rx_data_t *rx_data = g_ota_ctx.rx_data;
    if(rx_data == NULL || rx_data->remain_file_size){
        return;
    }

    updata = (uint8_t *)qm_malloc(CONFIG_HARMONY_BLE_OTA_PAGE_SIZE);
    if(updata == NULL){
        return;
    }
    memset(updata, 0, CONFIG_HARMONY_BLE_OTA_PAGE_SIZE);

    mbedtls_sha256_init(&sha256Ctx);
    mbedtls_sha256_starts(&sha256Ctx, false);
    while (read_index < rx_data->total_file_size)
    {
        if (read_index + CONFIG_HARMONY_BLE_OTA_PAGE_SIZE <= rx_data->total_file_size){
            read_index += CONFIG_HARMONY_BLE_OTA_PAGE_SIZE;
            qm_ota_read(&off_set, &updata[0], CONFIG_HARMONY_BLE_OTA_PAGE_SIZE);
            mbedtls_sha256_update(&sha256Ctx, &updata[0], CONFIG_HARMONY_BLE_OTA_PAGE_SIZE);
        }else{
            qm_ota_read(&off_set, &updata[0],  rx_data->total_file_size - read_index);
            mbedtls_sha256_update(&sha256Ctx, &updata[0], rx_data->total_file_size - read_index);
            read_index = rx_data->total_file_size;
        }
    }
    memset(&updata[0], 0, CONFIG_HARMONY_BLE_OTA_PAGE_SIZE);
    mbedtls_sha256_finish(&sha256Ctx, &updata[0]);
    mbedtls_sha256_free(&sha256Ctx);

    if(!memcmp(&updata[0], g_ota_ctx.hash_value, 32)){
        QM_LOGD(LOG_TAG, "OTA update Success");
        reuslt = HARMONY_BLE_OTA_RESULT_SUCCESS;
        if(g_ota_ctx.param->event_notify){
            g_ota_ctx.param->event_notify(HARMONY_BLE_EVENT_DEVICE_OTA_COMPLETE, NULL);
        }
    }else{
        QM_LOGE(LOG_TAG, "OTA update failure");
        QM_HEX_LOGW(LOG_TAG, "OTA sha256Ctx ", updata, 32);
        QM_HEX_LOGW(LOG_TAG, "OTA SHA ", g_ota_ctx.hash_value, 32);
        reuslt = HARMONY_BLE_OTA_RESULT_VERIFY_ERROR;
        if(g_ota_ctx.param->event_notify){
            g_ota_ctx.param->event_notify(HARMONY_BLE_EVENT_DEVICE_OTA_FAIL, NULL);
        }
    }

    qm_free(updata);
    updata = NULL;

    ota_ctrl_result_respone(&reuslt, sizeof(uint8_t));

    if(reuslt == HARMONY_BLE_OTA_RESULT_SUCCESS){
        qm_ota_end(NULL);
    }
}

static void ota_rx_dispatcher(void)
{
    harmony_ble_ota_rx_data_t *rx_data = g_ota_ctx.rx_data;
    if(rx_data == NULL){
        return;
    }
    
    switch (rx_data->data_type)
    {
        case HARMONY_BLE_OTA_DATA_TYPE_MOUDLE_HASH_LIST:
            QM_LOGD(LOG_TAG, "HASH LIST:%.*s...", rx_data->file_size, &rx_data->payload[0]);
            ota_moudle_hash_value_set(&rx_data->payload[0]);
            qm_ota_start(NULL);
            if(g_ota_ctx.param->event_notify){
                g_ota_ctx.param->event_notify(HARMONY_BLE_EVENT_DEVICE_OTA_START, NULL);
            }
        break;
        
        case HARMONY_BLE_OTA_DATA_TYPE_MOUDLE_HASH_SIGNATURE:
            QM_HEX_LOGD(LOG_TAG, "HASH SIGNATURE: ", &rx_data->payload[0], rx_data->file_size);
        break;

        case HARMONY_BLE_OTA_DATA_TYPE_MOUDLE_FILE:
        {
            ota_data_flash_write();
            ota_data_flash_verify_read();
        }
        break;
        default:

        break;
    }

}

static int harmony_ble_ota_data_respone(uint16_t seg_index)
{
    uint8_t index = 0;
    uint32_t seg_map = 0;
    uint16_t tmp_index = 0;
    harmony_ble_ota_rx_data_t *rx_data = g_ota_ctx.rx_data;
    uint8_t rsp_data[HARMONY_BLE_OTA_DATA_REP_SIZE] = {0};
    uint16_t cmd = HARMONY_BLE_OTA_UP_CMD_PACK(HARMONY_BLE_OTA_CMD_TRANS_DATA);
    
    cmd = cpu_to_be16(cmd);
    memcpy(rsp_data, &cmd, sizeof(uint16_t));
    index = sizeof(uint16_t);
    tmp_index = cpu_to_be16(seg_index);
    memcpy(rsp_data + index, &tmp_index, sizeof(uint16_t));
    index += sizeof(uint16_t);
    
    seg_map = cpu_to_be32(rx_data->seg_map1);
    memcpy(rsp_data + index, &seg_map, sizeof(uint32_t));
    index += sizeof(uint32_t);
    seg_map = cpu_to_be32(rx_data->seg_map2);
    memcpy(rsp_data + index, &seg_map, sizeof(uint32_t));

    QM_HEX_LOGD(LOG_TAG, "SEND ", rsp_data, HARMONY_BLE_OTA_DATA_REP_SIZE);

    return harmony_ble_hal_ota_send_indication(rsp_data, HARMONY_BLE_OTA_DATA_REP_SIZE, txdone);
}

static void ota_rx_reset(void)
{
    g_ota_ctx.rx_data->recv_seg = 0;
    g_ota_ctx.rx_data->seg_map2 = 0;
    g_ota_ctx.rx_data->seg_map1 = 0;
    g_ota_ctx.rx_data->start_index = 0;

    if(g_ota_ctx.rx_data->remain_file_size <= 0){
        qm_free(g_ota_ctx.rx_data);
        g_ota_ctx.rx_data = NULL;
    }
}

int harmony_ble_ota_data_read(uint8_t *data, int len)
{
    uint8_t index = 0;
    uint16_t seg_index = 0;
    uint8_t tlv_tag = 0;
    uint8_t file_type = 0;
    uint32_t file_size = 0;
    uint32_t malloc_len = 0;
    
    QM_RETURN_ON_FALSE(data, -QM_EINVAL, LOG_TAG, "data NULL");
    QM_RETURN_ON_FALSE(len >= 2, -QM_EINVAL, LOG_TAG, "len INVAL");    
    
    memcpy(&seg_index, &data[index], sizeof(uint16_t));
    seg_index = cpu_to_be16(seg_index);
    index += sizeof(uint16_t);
    QM_LOGD(LOG_TAG, "seg_index %d ", seg_index);

    if(!seg_index){
        index+=2;
        tlv_tag = data[index++];
        QM_LOGD(LOG_TAG, "tlv_tag %d ", tlv_tag);

        if(tlv_tag != HARMONY_BLE_OTA_DATA_TLV_TAG){
            return -QM_EINVAL;
        }

        index++; 
        
        file_type = data[index++]; 
        memcpy(&file_size, &data[index], sizeof(uint32_t));
        file_size = cpu_to_be32(file_size);
        index += sizeof(uint32_t);

        QM_LOGD(LOG_TAG, "file_type %d file_size %d recv_length %d ", file_type, file_size, (len - index));

        if(file_size > OTA_UPDATE_DATA_LEN){
            malloc_len = sizeof(harmony_ble_ota_rx_data_t) + OTA_UPDATE_DATA_LEN - 1;
        }else{
            malloc_len = sizeof(harmony_ble_ota_rx_data_t) + file_size - 1;
        }

        g_ota_ctx.rx_data = (harmony_ble_ota_rx_data_t *)qm_malloc(malloc_len);
        if(g_ota_ctx.rx_data == NULL){
            QM_LOGD(LOG_TAG, "rx_data malloc failed %d...", malloc_len);
            return -QM_ENOMEM;
        }
        memset(g_ota_ctx.rx_data, 0, malloc_len);

        g_ota_ctx.rx_data->recv_seg = 0;
        g_ota_ctx.rx_data->start_index = seg_index;
        g_ota_ctx.rx_data->data_type = file_type;
        g_ota_ctx.rx_data->total_file_size = file_size;
        g_ota_ctx.rx_data->remain_file_size = file_size;
    }

    if (g_ota_ctx.rx_data == NULL){
        return -QM_EINVAL;
    }

    if(seg_index && ((seg_index - g_ota_ctx.rx_data->old_index) != 1)){
        g_ota_ctx.rx_data->seg_map2 = 0;
        g_ota_ctx.rx_data->seg_map1 = 0;
        goto __exit;
    }
    
    g_ota_ctx.rx_data->old_index = seg_index;

    memcpy(g_ota_ctx.rx_data->payload + g_ota_ctx.rx_data->file_size, &data[index], len - index);
    
    g_ota_ctx.rx_data->file_size += (len - index);
    g_ota_ctx.rx_data->remain_file_size -= (len - index);

    if(g_ota_ctx.rx_data->start_index == 0 && (seg_index - 1 > 0)){
        g_ota_ctx.rx_data->start_index = seg_index;
    }

    if((seg_index % CONFIG_HARMONY_BLE_OTA_PACKET_SIZE) >= 32){
        g_ota_ctx.rx_data->seg_map2 |= (1 << (31 - ((seg_index % CONFIG_HARMONY_BLE_OTA_PACKET_SIZE) - 32)));
    }else{
        g_ota_ctx.rx_data->seg_map1 |= (1 << (31 - (seg_index % CONFIG_HARMONY_BLE_OTA_PACKET_SIZE)));
    }
    
__exit:
    if(((seg_index % CONFIG_HARMONY_BLE_OTA_PACKET_SIZE) + 1 >= CONFIG_HARMONY_BLE_OTA_PACKET_SIZE)|| 
        g_ota_ctx.rx_data->remain_file_size <= 0){
        harmony_ble_ota_data_respone(g_ota_ctx.rx_data->start_index);
        ota_rx_dispatcher();
        ota_rx_reset();
    }
    return QM_EOK;
}


int harmony_ble_ota_register(harmony_ble_comm_param_t *param)
{
    g_ota_ctx.param = param;
    return QM_EOK;
}