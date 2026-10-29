#include "map_honor_wifi.h"

#if CONFIG_IOT_MAP_HONOR_SUPPORT

#define CONFIG_QM_LOG_PRINT_BL602 0
#include "generic_serial.h"
#include "generic_serial_iot.h"
#include "qm_iot_operation.h"
#include "qm_config.h"
#include <cJSON.h>
static int map_parse(void *handle, uint8_t *data, int len);
static int map_match(map_match_type_t match_type, void *a, void *b);

int map_match_pid(map_honor_unit_t* a, map_honor_unit_t* b);

void json_baseinfo_pack(cJSON *xj_serviceList, cJSON *honor_serviceList);
void json_devinfo_pack(cJSON *honor_serviceList);
void json_netinfo_pack(cJSON *honor_serviceList);
void json_otainfo_pack(cJSON *honor_serviceList);

int json_prodt_pack(void *handle, char *product_profile);
void json_prop_cope(cJSON *honor_serviceList, cJSON *propertyList, cJSON *service_id, iot_map_honor_t *honor_map); 
void json_action_cope(cJSON *honor_serviceList, cJSON *propertyList, cJSON *service_id, iot_map_honor_t *honor_map); 
void json_event_cope(cJSON *honor_serviceList, cJSON *propertyList, cJSON *service_id, iot_map_honor_t *honor_map); 

// #define MY_PRDKEY "77bd21e75ff1409492d0d62c10968ced"
// #define MY_PRDSECRET "f0e85173fa707759eafbf9e904e1abbe"

/* 统计属性数量，prop_count的值也是小匠的id号 */
// static int prop_count = 0;

static int xid_size = 0;
static int aid_size = 0;
static int eid_size = 0;
char prid[5] = {0};
#define PRODUCT_NAME_MAX_LEN 64
#define PRODUCT_TYPE_MAX_LEN 64
static uint8_t g_product_name[PRODUCT_NAME_MAX_LEN] = {0};
static char g_pro_type[PRODUCT_TYPE_MAX_LEN] = {0};
// be30c37a77e14595ba45da8016cffaf3",   //这个5表示，keyId:be30c37a77e14595ba45da8016cffaf3
//     "6": "eda147805416378b70c3414087e88a5b
product_key_t g_product_key = {
    .pro_key = "be30c37a77e14595ba45da8016cffaf3",
    .pro_secret = "eda147805416378b70c3414087e88a5b",
};


static iot_map_hal_t map_hal_honor = {
    .parse = map_parse,
    .match = map_match,
};



int iot_map_honor_init(void)
{
    iot_map_hal_register(IOT_GENERIC_PLATFORM_TYPE_HONOR, IOT_GENERIC_NETWORK_TYPE_WIFI, &map_hal_honor);
    return QM_EOK;
}

product_key_t *get_pro_key(void)
{
    return &g_product_key;
}

uint8_t *get_product_name(void)
{
    return g_product_name;
}

static int map_parse(void *handle, uint8_t *data, int len)
{
    if (handle == NULL || data == NULL || len == 0) {
        return -QM_EINVAL;
    }
    /* 需要在外部创建全局变量 honor_profile */
    json_prodt_pack(handle, (char*)data);
    return QM_EOK;
}

/**
 * @brief honor xj map匹配
 * @param match_type    匹配类型
 * @param a             保存的map
 * @param b             需要从保存的map中匹配的map
 * @return int          匹配成功返回0，否则返回其他
 * **/
static int map_match(map_match_type_t match_type, void *a, void *b)
{
    uint8_t attr_id = 1;
    iot_map_honor_t *a_map = (iot_map_honor_t*)a;
    iot_map_honor_t *b_map = (iot_map_honor_t*)b;

    map_honor_unit_t *tem_map = a_map->map_honor_unit;

    if(a == NULL || b == NULL)
    {
        return 0;
    }
   
 #if CONFIG_QM_LOG_PRINT_BL602 
    qm_printf("+++++++++++++++++++++++++++++++++++++++++\r\n");
    map_honor_unit_t *tem_map_temp = a_map->map_honor_unit;
    tem_map_temp = tem_map_temp->next;
    while(tem_map_temp)
    {
        qm_printf("id = %s  ", tem_map_temp->p.id);
        qm_printf("type = %d\r\n", tem_map_temp->type);
        tem_map_temp = tem_map_temp->next;
    }
    qm_printf("+++++++++++++++++++++++++++++++++++++++++\r\n");
#endif
    tem_map = tem_map->next;
    
    if (match_type == MAP_MATCH_TYPE_ID_BY_PLATFORM) {

        if (memcmp(a_map->s.id, b_map->s.id, strlen(a_map->s.id)) != 0) 
        {
 #if CONFIG_QM_LOG_PRINT_BL602   
            qm_printf("memcmp(a_map->s.id, b_map->s.id, strlen(a_map->s.id)) != 0\r\n");
#endif
            return 0;
        }

        while (tem_map) {
            if (a_map->sid_len > HONOR_ID_MAX_LEN) {
                if(map_match_pid(tem_map, b_map->map_honor_unit))
                {
                    return 1;
                }
            } else {

                if(map_match_pid(tem_map, b_map->map_honor_unit))
                {
 #if CONFIG_QM_LOG_PRINT_BL602                       
                    QM_LOGD("map", "[LHSMD]tem_map->p.id = %s\r\n",  tem_map->p.id);
                    QM_LOGD("map", "[LHSMD] tem_map->type = %d\r\n",  tem_map->type);
                    QM_LOGD("map", "[LHSMD] tem_map->type_id = %d\r\n",  tem_map->type_id);
                    QM_LOGD("map", "[LHSMD] tem_map->attr_id = %d\r\n",  tem_map->attr_id);
#endif
                    a_map->map_honor_unit->type = tem_map->type;
                    a_map->map_honor_unit->type_id = tem_map->type_id;
                    a_map->map_honor_unit->attr_id = tem_map->attr_id;
                    a_map->map_honor_unit->propidx = tem_map->propidx;
                    memset(a_map->map_honor_unit->p.id, 0, sizeof(a_map->map_honor_unit->p.id));
                    strncpy(a_map->map_honor_unit->p.id, tem_map->p.id, strlen(tem_map->p.id));
                    return 1;
                }
            }
            attr_id++;
            tem_map = tem_map->next;
        }
    } 
    else if (match_type == MAP_MATCH_TYPE_PLATFORM_BY_ID)
    {
        while (tem_map) 
        {
 #if CONFIG_QM_LOG_PRINT_BL602   
            QM_LOGD("map", "[%s|%d] attr %d %d %d %d\n", __func__, __LINE__, tem_map->attr_id, b_map->map_honor_unit->attr_id, tem_map->type, b_map->map_honor_unit->type);
 #endif           

            if(tem_map->type_id != 2) //不是事件
            {
                if (tem_map->attr_id == b_map->map_honor_unit->attr_id && \
                    tem_map->type == b_map->map_honor_unit->type) 
                {
 #if CONFIG_QM_LOG_PRINT_BL602   
                    QM_LOGD("map", "[LHSMD] 1 tem_map->p.id = %s\r\n",  tem_map->p.id);
                    QM_LOGD("map", "[LHSMD] 1 tem_map->type = %d\r\n",  tem_map->type);
                    QM_LOGD("map", "[LHSMD] 1 tem_map->type_id = %d\r\n",  tem_map->type_id);
                    QM_LOGD("map", "[LHSMD] 1 tem_map->attr_id = %d\r\n",  tem_map->attr_id);
#endif
                    a_map->map_honor_unit->type = tem_map->type;
                    a_map->map_honor_unit->type_id = tem_map->type_id;
                    a_map->map_honor_unit->attr_id = tem_map->attr_id;
                    a_map->map_honor_unit->propidx = tem_map->propidx;
                    memset(a_map->map_honor_unit->p.id, 0, sizeof(a_map->map_honor_unit->p.id));
                    strncpy(a_map->map_honor_unit->p.id, tem_map->p.id, strlen(tem_map->p.id));
                    return 1;
                } 
            }
            else //事件
            {
                if (tem_map->attr_id == b_map->map_honor_unit->attr_id) 
                {
 #if CONFIG_QM_LOG_PRINT_BL602   
                    QM_LOGD("map", "[LHSMD] event tem_map->p.id = %s\r\n",  tem_map->p.id);
                    QM_LOGD("map", "[LHSMD] event tem_map->type = %d\r\n",  tem_map->type);
                    QM_LOGD("map", "[LHSMD] event tem_map->type_id = %d\r\n",  tem_map->type_id);
                    QM_LOGD("map", "[LHSMD] event tem_map->attr_id = %d\r\n",  tem_map->attr_id);
                    QM_LOGD("map", "[LHSMD] event tem_map->eiid_list[0] = %d\r\n",  tem_map->eiid_list[0]);
#endif
                    a_map->map_honor_unit->type = tem_map->type;
                    a_map->map_honor_unit->type_id = tem_map->type_id;
                    a_map->map_honor_unit->attr_id = tem_map->attr_id;
                    a_map->map_honor_unit->propidx = tem_map->propidx;
                    
                    memset(a_map->map_honor_unit->p.id, 0, sizeof(a_map->map_honor_unit->p.id));
                    strncpy(a_map->map_honor_unit->p.id, tem_map->p.id, strlen(tem_map->p.id));
                    memcpy(a_map->map_honor_unit->eiid_list, tem_map->eiid_list, HONOR_EID_MAX_LEN);
                    return 1;
                } 
            }
            tem_map = tem_map->next;
        }
    }
   
    return QM_EOK;
}

char *xj_get_honor_profile(void)
{
    return honor_profile;
}

char *get_product_type_string(void)
{
    return g_pro_type;
}

cJSON_Hooks hook = {
    .malloc_fn = qm_malloc,
    .free_fn = qm_free
};
/**
 * @brief 小匠json解析(使用AT指令写入，
 *          小匠json是简化后的荣耀json，可以在产测时减少产测的时间)、
 *          还需要还原成荣耀json格式，提供给荣耀的初始化接口
 * @param handle        
 * @param product_profile  待解析json
 * @return int 解析成功返回0，否则返回其他
 * **/
int json_prodt_pack(void *handle, char *product_profile)
{
     int service_size = 0;
    /* 1.小匠Json格式化，同时完成荣耀json打包 */
    cJSON *xj_total = cJSON_Parse(product_profile);  
    if(xj_total == NULL)
    {
        return (-1);
    }

    cJSON *honor_total_obj = cJSON_CreateObject();
    if(honor_total_obj == NULL)
    {
        return -QM_ERROR;
    
    }

    cJSON *key = cJSON_GetObjectItem(xj_total, "5"); 
    if(key != NULL) {
        strncpy(g_product_key.pro_key, key->valuestring, strlen(key->valuestring));
    }
    cJSON *secret = cJSON_GetObjectItem(xj_total, "6"); 
    if(secret != NULL) {
        strncpy(g_product_key.pro_secret, secret->valuestring, strlen(secret->valuestring));
    }
    cJSON_DeleteItemFromObject(xj_total, "5");  
    cJSON_DeleteItemFromObject(xj_total, "6");  

    /* 2.解析服务列表从产品开始，组包从头开始 */
    cJSON *xj_serviceList = cJSON_GetObjectItem(xj_total, "s");  
    if(xj_serviceList == NULL)
    {
        return -QM_ERROR;
    }
    service_size = cJSON_GetArraySize(xj_serviceList);  //得到服务列表中服务个数
    if(service_size <= 0)
    {
        return -QM_ERROR;
    }
    qm_printf("[LHSMD] profile service_size = %d\r\n", service_size);



    cJSON *honor_serviceList = cJSON_CreateArray(); 
    if(honor_serviceList != NULL) 
        cJSON_AddItemToObject(honor_total_obj, "serviceList", honor_serviceList);

    for (int service_index = 0; service_index < service_size; service_index++) {
        /* 3.解析产品属性/活动/事件列表，并完成组包 */
        if (service_index == 0) {
            /* 产品信息 */
            json_baseinfo_pack(xj_serviceList, honor_serviceList);

            /* 设备信息 */
            json_devinfo_pack(honor_serviceList);
            /* 网络信息 */
            json_netinfo_pack(honor_serviceList);
            /* OTA信息 */
            json_otainfo_pack(honor_serviceList);
        } else { //产品属性/活动/事件列表
            cJSON *honor_service_p =  cJSON_CreateObject(); 
            cJSON_AddItemToArray(honor_serviceList, honor_service_p); 

            cJSON *service_id = cJSON_GetArrayItem(xj_serviceList, service_index);
            service_id = cJSON_GetObjectItem(service_id, "1");
            if (service_id == NULL) {
                break;
            }

            /* 荣耀服务层 */
            iot_map_honor_t honor_map = {0};
            iot_map_service_insert(&honor_map, service_id->valuestring, strlen(service_id->valuestring));
            honor_map.map_honor_unit = map_honor_node();

            cJSON *eventList = cJSON_GetArrayItem(xj_serviceList, service_index);
            eventList = cJSON_GetObjectItem(eventList, "e");   //event
            if (eventList != NULL) {
                eid_size = cJSON_GetArraySize(eventList); 
                if (eid_size > 0) {
                    qm_printf("[LHSMD] eid_size = %d\r\n", eid_size);
                    // honor_map.map_honor_unit = map_honor_node();
                    json_event_cope(honor_service_p, eventList, service_id, &honor_map); //还没实现
                }
            }  

            /* 荣耀属性，事件，活动层 */
            cJSON *propertyList = cJSON_GetArrayItem(xj_serviceList, service_index);
            propertyList = cJSON_GetObjectItem(propertyList, "p");  
            if (propertyList != NULL) {
                
                xid_size = cJSON_GetArraySize(propertyList); 
                if (xid_size > 0) {
                   qm_printf("[LHSMD] xid_size = %d\r\n", xid_size);
                    json_prop_cope(honor_service_p, propertyList, service_id, &honor_map);
                } 
            }  

            cJSON *actionList = cJSON_GetArrayItem(xj_serviceList, service_index);
            actionList = cJSON_GetObjectItem(actionList, "a"); //action
            if (actionList != NULL) {
                aid_size = cJSON_GetArraySize(actionList); 
                if (aid_size > 0) {
                    qm_printf("[LHSMD] aid_size = %d\r\n", aid_size);
                    json_action_cope(honor_service_p, actionList, service_id, &honor_map); //还没实现
                }
            }

            cJSON_AddItemToObject(honor_service_p, "index", cJSON_CreateNumber(service_index+3));
            cJSON_AddItemToObject(honor_service_p, "id", cJSON_CreateString(service_id->valuestring));
            foreach_map(honor_map.map_honor_unit);

            int ret = iot_map_add(handle, (uint8_t*)&honor_map, sizeof(honor_map));
            if (ret != QM_EOK) {
                QM_LOGD("map", "[%s|%d] iot_map_add fail!", __func__, __LINE__);
                break;
            }
        } 
    }
    cJSON_AddItemToObject(honor_total_obj, "prdID", cJSON_CreateString(prid));
    char *honor_total_obj_str = cJSON_PrintUnformatted(honor_total_obj);
    if(honor_total_obj_str == NULL) {
        QM_LOGD("map", "[%s|%d] honor_total_obj_str malloc fail!", __func__, __LINE__);
        return -QM_ERROR;
    }
    int len_honor_json = strlen(honor_total_obj_str);
    if(honor_profile == NULL)
    {
        honor_profile = (char *)qm_malloc(len_honor_json+1);
        if (honor_profile == NULL) {
            QM_LOGD("map", "[%s|%d] honor_profile malloc fail!", __func__, __LINE__);
            return -QM_ERROR;
        }
    }
    else
    {
        qm_free(honor_profile);
        honor_profile = NULL;
        honor_profile = (char *)qm_malloc(len_honor_json+1);
        if (honor_profile == NULL) {
            QM_LOGD("map", "[%s|%d] honor_profile malloc fail!", __func__, __LINE__);
            return -QM_ERROR;
        }

    }
    memset(honor_profile, 0, len_honor_json+1);
    memcpy(honor_profile, honor_total_obj_str, len_honor_json);

    
    cJSON_free(xj_total);
    cJSON_Delete(xj_serviceList);        
    cJSON_free(honor_total_obj_str);
    cJSON_Delete(honor_total_obj);
    return 0;
}

void json_baseinfo_pack(cJSON *xj_serviceList, cJSON *honor_serviceList) {
    char *str  = NULL;
     cJSON *honor_service =  cJSON_CreateObject(); 
     cJSON_AddItemToArray(honor_serviceList, honor_service); 

    cJSON *dev_info_list = cJSON_GetArrayItem(xj_serviceList, 0);
    dev_info_list = cJSON_GetObjectItem(dev_info_list, "d");  

    memset(g_pro_type, 0, sizeof(g_pro_type));
    
    if(cJSON_GetArrayItem(dev_info_list, 18) != NULL)
    {
        str = cJSON_GetArrayItem(dev_info_list, 18)->valuestring;
        if(str != NULL)
        {
            memcpy(g_pro_type, str, strlen(str));
        }
        else
        {
            memcpy(g_pro_type, "DP22A1", strlen("DP22A1"));
        }
    }
    else
    {
        memcpy(g_pro_type, "DP22A1", strlen("DP22A1"));
    }

    char baseinfo_list[][17] = {"prdID","prdName","manufactureID","manufactureName","prdTypeId","prdTypeName","protocolType",
        "supportNetCfg","supportRegister","pinMode","authMode","supportCloudCtrl","netCfgWlan","modelId","offeringCode",
        "prdSubTypeId","prdSubTypeName","qrScanMode"};
    
    cJSON *honor_base_arr = cJSON_CreateArray(); 
    cJSON_AddItemToObject(honor_service, "propertyList", honor_base_arr);
    for (int i = 0; i <= 17; i++) {
        cJSON *honor_dev_list =  cJSON_CreateObject();
        cJSON_AddItemToArray(honor_base_arr, honor_dev_list);

        cJSON_AddItemToObject(honor_dev_list, "access", cJSON_CreateNumber(0));
        cJSON_AddItemToObject(honor_dev_list, "index", cJSON_CreateNumber(i));
        cJSON_AddItemToObject(honor_dev_list, "id", cJSON_CreateString(baseinfo_list[i]));
        if ((i >= 7 && i <= 10) || i == 11 || i == 17) {
            int number = atoi(cJSON_GetArrayItem(dev_info_list, i)->valuestring);
            cJSON_AddItemToObject(honor_dev_list, "type", cJSON_CreateString("int"));
            cJSON_AddItemToObject(honor_dev_list, "value", cJSON_CreateNumber(number));
        } else {
            char *src = cJSON_GetArrayItem(dev_info_list, i)->valuestring;
            if (i == 0) {
                strncpy(prid, src, 4);
            }
            else if(i == 1)
            {
                if(strlen(src) < PRODUCT_NAME_MAX_LEN)
                    memcpy(g_product_name, src, strlen(src));
            }
            cJSON_AddItemToObject(honor_dev_list, "type", cJSON_CreateString("string"));
            cJSON_AddItemToObject(honor_dev_list, "value", cJSON_CreateString(src));

            switch (i)
            {
            case 0:
            case 2:
            case 4:
            case 6:
                cJSON_AddItemToObject(honor_dev_list, "maxLength", cJSON_CreateNumber(4));
                break;
            case 13:
                cJSON_AddItemToObject(honor_dev_list, "maxLength", cJSON_CreateNumber(6));
                break;
            case 14:
                cJSON_AddItemToObject(honor_dev_list, "maxLength", cJSON_CreateNumber(16));
                break;
            case 15:
                cJSON_AddItemToObject(honor_dev_list, "maxLength", cJSON_CreateNumber(3));
                break;
            default:
                break;
            }
        }
        
    }
    cJSON_AddItemToObject(honor_service, "index", cJSON_CreateNumber(0));
    cJSON_AddItemToObject(honor_service, "id", cJSON_CreateString("baseInfo"));

}


void json_devinfo_pack(cJSON *honor_serviceList) {
    cJSON *honor_service =  cJSON_CreateObject(); 
    cJSON_AddItemToArray(honor_serviceList, honor_service); 

    char devindo_list[][16] = {"sn","udid","mac","devName","devModel","subDevType","firmwareVersion",
        "sdkVersion","hardwareVersion","MCUVersion"};
    char dev_len[10] = {32, 64, 17, 63, 15, 1, 32, 32, 32, 32};
    
    cJSON *honor_dev_arr = cJSON_CreateArray(); 
    cJSON_AddItemToObject(honor_service, "propertyList", honor_dev_arr);
    cJSON_AddItemToObject(honor_service, "index", cJSON_CreateNumber(1));
    cJSON_AddItemToObject(honor_service, "id", cJSON_CreateString("deviceInfo"));
    for (int i = 0; i <= 9; i++) {
        cJSON *honor_dev_list =  cJSON_CreateObject();
        cJSON_AddItemToArray(honor_dev_arr, honor_dev_list);

        if (i == 3) {
            cJSON_AddItemToObject(honor_dev_list, "access", cJSON_CreateNumber(7));
        } else {
            cJSON_AddItemToObject(honor_dev_list, "access", cJSON_CreateNumber(5));
        }

        cJSON_AddItemToObject(honor_dev_list, "maxlength", cJSON_CreateNumber(dev_len[i]));
        cJSON_AddItemToObject(honor_dev_list, "index", cJSON_CreateNumber(i));
        cJSON_AddItemToObject(honor_dev_list, "id", cJSON_CreateString(devindo_list[i]));
        cJSON_AddItemToObject(honor_dev_list, "type", cJSON_CreateString("string"));
        cJSON_AddItemToObject(honor_dev_list, "value", cJSON_CreateString(""));
    }

}

void json_netinfo_pack(cJSON *honor_serviceList) {
    cJSON *honor_service =  cJSON_CreateObject(); 
    cJSON_AddItemToArray(honor_serviceList, honor_service); 

    char netindo_list[][6] = {"ssid","rssi","ip","bssid"};
    char net_len[4] = {32, 0, 15, 17};
    
    cJSON *honor_net_arr = cJSON_CreateArray(); 
    cJSON_AddItemToObject(honor_service, "propertyList", honor_net_arr);
    cJSON_AddItemToObject(honor_service, "index", cJSON_CreateNumber(2));
    cJSON_AddItemToObject(honor_service, "id", cJSON_CreateString("netInfo"));
    for (int i = 0; i <= 3; i++) {
        cJSON *honor_net_list =  cJSON_CreateObject();
        cJSON_AddItemToArray(honor_net_arr, honor_net_list);

        cJSON_AddItemToObject(honor_net_list, "access", cJSON_CreateNumber(5));
        if (i != 1) {
            cJSON_AddItemToObject(honor_net_list, "maxlength", cJSON_CreateNumber(net_len[i]));
        }
        cJSON_AddItemToObject(honor_net_list, "index", cJSON_CreateNumber(i));
        cJSON_AddItemToObject(honor_net_list, "id", cJSON_CreateString(netindo_list[i]));
        if (i == 1) {
            cJSON_AddItemToObject(honor_net_list, "type", cJSON_CreateString("int"));
            cJSON_AddItemToObject(honor_net_list, "value", cJSON_CreateNumber(0));
        } else {
            cJSON_AddItemToObject(honor_net_list, "type", cJSON_CreateString("string"));
            cJSON_AddItemToObject(honor_net_list, "value", cJSON_CreateString(""));
        }        
    }
}

void json_otainfo_pack(cJSON *honor_serviceList) {
    cJSON *honor_service =  cJSON_CreateObject(); 
    cJSON_AddItemToArray(honor_serviceList, honor_service); 

    char netindo_list[][16] = {"otaStatus","checkResult","downloadTimeout","newVersion",
        "MCUNewVersion","introduction","MCUIntroduction","autoOtaSwitch","otaProgress","pkgSize"};

    cJSON *honor_ota_arr = cJSON_CreateArray(); 
    cJSON_AddItemToObject(honor_service, "propertyList", honor_ota_arr);
    cJSON_AddItemToObject(honor_service, "index", cJSON_CreateNumber(3));
    cJSON_AddItemToObject(honor_service, "id", cJSON_CreateString("ota"));
    for (int i = 0; i <= 10; i++) {
        cJSON *honor_ota_list =  cJSON_CreateObject();
        cJSON_AddItemToArray(honor_ota_arr, honor_ota_list);

        if (i == 7) {
            cJSON_AddItemToObject(honor_ota_list, "access", cJSON_CreateNumber(7));
        } else {
            cJSON_AddItemToObject(honor_ota_list, "access", cJSON_CreateNumber(5));
        }   
        cJSON_AddItemToObject(honor_ota_list, "index", cJSON_CreateNumber(i));
        cJSON_AddItemToObject(honor_ota_list, "id", cJSON_CreateString(netindo_list[i]));

        if (i >= 3 && i <= 6) {
            cJSON_AddItemToObject(honor_ota_list, "type", cJSON_CreateString("string"));
            cJSON_AddItemToObject(honor_ota_list, "value", cJSON_CreateString(""));
        } else {
            cJSON_AddItemToObject(honor_ota_list, "type", cJSON_CreateString("int"));
            cJSON_AddItemToObject(honor_ota_list, "value", cJSON_CreateNumber(0));
        }        
    }

    cJSON *honor_ota_action = cJSON_CreateArray(); 
    cJSON_AddItemToObject(honor_service, "actionList", honor_ota_action);
    for (int i = 0; i <= 1; i++) {
        cJSON *honor_ota_action_list =  cJSON_CreateObject();
        cJSON_AddItemToArray(honor_ota_action, honor_ota_action_list);
        cJSON_AddItemToObject(honor_ota_action_list, "index", cJSON_CreateNumber(i));
        if (i == 0) {
            cJSON_AddItemToObject(honor_ota_action_list, "id", cJSON_CreateString("checkVersion"));
        } else {
            cJSON_AddItemToObject(honor_ota_action_list, "id", cJSON_CreateString("otaAction"));
        }
    }
    
}

void json_prop_cope(cJSON *honor_serviceList, cJSON *propertyList, 
                     cJSON *service_id, iot_map_honor_t *honor_map) 
{
    cJSON *honor_x_arr = cJSON_CreateArray(); 
    cJSON_AddItemToObject(honor_serviceList, "propertyList", honor_x_arr);

    /* 对荣耀json填充数据 */
    for (int j = 0; j < xid_size; j++) {    
        cJSON *honor_p_list =  cJSON_CreateObject();
        cJSON_AddItemToArray(honor_x_arr, honor_p_list);
        cJSON_AddItemToObject(honor_p_list, "index", cJSON_CreateNumber(j));

        cJSON *xj_prop_access = cJSON_GetArrayItem(propertyList, j);
        xj_prop_access =  cJSON_GetObjectItem(xj_prop_access,"9");  //access属性权限
        cJSON_AddItemToObject(honor_p_list, "access", cJSON_CreateNumber(xj_prop_access->valueint));
        

        cJSON *xj_prop_id = cJSON_GetArrayItem(propertyList, j);
        xj_prop_id = cJSON_GetObjectItem(xj_prop_id,"2");  //属性id
        cJSON_AddItemToObject(honor_p_list, "id", cJSON_CreateString(xj_prop_id->valuestring));

        cJSON *xj_prop_type = cJSON_GetArrayItem(propertyList, j);
        xj_prop_type = cJSON_GetObjectItem(xj_prop_type,"3");  //数据类型

        switch (xj_prop_type->valueint)
        {
        case QM_DTTV_TYPE_BOOL:
        case QM_DTTV_TYPE_INT8:
        case QM_DTTV_TYPE_UINT8:
        case QM_DTTV_TYPE_INT16:
        case QM_DTTV_TYPE_UINT16:
        case QM_DTTV_TYPE_INT32:
        case QM_DTTV_TYPE_UINT32:
            cJSON_AddItemToObject(honor_p_list, "type", cJSON_CreateString("int"));
            cJSON_AddItemToObject(honor_p_list, "value", cJSON_CreateNumber(0));
            break;
        case QM_DTTV_TYPE_STRING:
            cJSON_AddItemToObject(honor_p_list, "type", cJSON_CreateString("string"));
            cJSON_AddItemToObject(honor_p_list, "value", cJSON_CreateString(""));
            break;
        default:
            break;
        }
        
        cJSON *json_p_maxlen = cJSON_GetArrayItem(propertyList, j);
        json_p_maxlen = cJSON_GetObjectItem(json_p_maxlen,"5");  //
        if (json_p_maxlen != NULL) {
            cJSON_AddItemToObject(honor_p_list, "maxLength", cJSON_CreateNumber(json_p_maxlen->valueint));
        }

         cJSON *xj_prop_xj_id = cJSON_GetArrayItem(propertyList, j);   //小匠id
        xj_prop_xj_id = cJSON_GetObjectItem(xj_prop_xj_id, "6");
        if(xj_prop_xj_id == NULL) {
            break;  
        }


        /* 设置小匠映射关系，普通属性type_id为0 */
        iot_map_insert(honor_map, map_node_assignment(xj_prop_id->valuestring, 0, xj_prop_type->valueint, xj_prop_xj_id->valueint, j, NULL, 0));
        

    }
    
}

void json_action_cope(cJSON *honor_serviceList, cJSON *actionList, 
                     cJSON *service_id, iot_map_honor_t *honor_map) 
{
    cJSON *honor_x_arr = cJSON_CreateArray(); 
    cJSON_AddItemToObject(honor_serviceList, "actionList", honor_x_arr);

    /* 对荣耀json填充数据 */
    for (int j = 0; j < aid_size; j++) {    
        cJSON *honor_p_list =  cJSON_CreateObject();
        cJSON_AddItemToArray(honor_x_arr, honor_p_list);
        cJSON_AddItemToObject(honor_p_list, "index", cJSON_CreateNumber(j));
        cJSON_AddItemToObject(honor_p_list, "useCache", cJSON_CreateNumber(0));
        

        cJSON *xj_prop_id = cJSON_GetArrayItem(actionList, j);
        xj_prop_id = cJSON_GetObjectItem(xj_prop_id,"2");  //属性id
        cJSON_AddItemToObject(honor_p_list, "id", cJSON_CreateString(xj_prop_id->valuestring));

        cJSON *xj_act_in_data = cJSON_GetArrayItem(actionList, j);
        xj_act_in_data = cJSON_GetObjectItem(xj_act_in_data,"7");  //inDataList
        cJSON *xj_act_in_data_arr = cJSON_CreateArray();
        int num = cJSON_GetArraySize(xj_act_in_data);
        if(num == 0)
            cJSON_AddItemToObject(honor_p_list, "inDataList", xj_act_in_data_arr);
        else
        {
            for(uint8_t i = 0; i < num;i++)
            {
                 cJSON_AddItemToArray(xj_act_in_data_arr, cJSON_CreateNumber(cJSON_GetArrayItem(xj_act_in_data, i)->valueint));
            }
            cJSON_AddItemToObject(honor_p_list, "inDataList", xj_act_in_data_arr);
        }

        
        cJSON *xj_act_out_data = cJSON_GetArrayItem(actionList, j);
        xj_act_out_data = cJSON_GetObjectItem(xj_act_out_data,"8");  //outDataList
        cJSON *xj_act_out_data_arr = cJSON_CreateArray();
        num = cJSON_GetArraySize(xj_act_out_data);
        if(num == 0)
            cJSON_AddItemToObject(honor_p_list, "outDataList", xj_act_out_data_arr);
        else
        {
            for(uint8_t i = 0; i < num;i++)
            {
                 cJSON_AddItemToArray(xj_act_out_data_arr, cJSON_CreateNumber(cJSON_GetArrayItem(xj_act_out_data, i)->valueint));
            }
            cJSON_AddItemToObject(honor_p_list, "outDataList", xj_act_out_data_arr);
        }

        cJSON *xj_act_xj_id = cJSON_GetArrayItem(actionList, j);   //小匠id
        xj_act_xj_id = cJSON_GetObjectItem(xj_act_xj_id, "6");
        if(xj_act_xj_id == NULL) {
            break;  
        }

        /* 设置小匠映射关系,action的type_id为1 */
        iot_map_insert(honor_map, map_node_assignment(xj_prop_id->valuestring, 1, 0, xj_act_xj_id->valueint, j, NULL, 0));
        
    }

}

void json_event_cope(cJSON *honor_serviceList, cJSON *eventList, 
                     cJSON *service_id, iot_map_honor_t *honor_map) 
{
    uint8_t *eid = NULL;
    uint8_t eid_buf[5] = {0};
    cJSON *honor_x_arr = cJSON_CreateArray(); 
    cJSON_AddItemToObject(honor_serviceList, "eventList", honor_x_arr);

    /* 对荣耀json填充数据 */
    for (int j = 0; j < eid_size; j++) {    
        cJSON *honor_p_list =  cJSON_CreateObject();
        cJSON_AddItemToArray(honor_x_arr, honor_p_list);
        cJSON_AddItemToObject(honor_p_list, "index", cJSON_CreateNumber(j));

        cJSON *xj_event_type = cJSON_GetArrayItem(eventList, j);
        xj_event_type = cJSON_GetObjectItem(xj_event_type,"4");  //eventType
        cJSON_AddItemToObject(honor_p_list, "eventType", cJSON_CreateNumber(xj_event_type->valueint));
        

        cJSON *xj_event_id = cJSON_GetArrayItem(eventList, j);
        xj_event_id = cJSON_GetObjectItem(xj_event_id,"2");  //属性id
        cJSON_AddItemToObject(honor_p_list, "id", cJSON_CreateString(xj_event_id->valuestring));

        cJSON *xj_event_in_data = cJSON_GetArrayItem(eventList, j);
        xj_event_in_data = cJSON_GetObjectItem(xj_event_in_data,"7");  //数据类型
        cJSON *xj_event_in_data_arr = cJSON_CreateArray();
        int num = cJSON_GetArraySize(xj_event_in_data);
        if(num == 0)
        {
            cJSON_AddItemToObject(honor_p_list, "outDataList", xj_event_in_data_arr);
            eid = NULL;
        }
        else
        {
            for(uint8_t i = 0; i < num;i++)
            {
                cJSON_AddItemToArray(xj_event_in_data_arr, cJSON_CreateNumber(cJSON_GetArrayItem(xj_event_in_data, i)->valueint));
                eid_buf[i] = cJSON_GetArrayItem(xj_event_in_data, i)->valueint;
            }
            cJSON_AddItemToObject(honor_p_list, "outDataList", xj_event_in_data_arr);
            eid = eid_buf;
        }
        
        cJSON *xj_event_xj_id = cJSON_GetArrayItem(eventList, j);   //小匠id
        xj_event_xj_id = cJSON_GetObjectItem(xj_event_xj_id, "6");
        if(xj_event_xj_id == NULL) {
            break;  
        }
        /* 设置小匠映射关系，事件type_id = 2 */
        iot_map_insert(honor_map, map_node_assignment(xj_event_id->valuestring, 2, 0, xj_event_xj_id->valueint, j, eid, num));

    }

}


int map_match_pid(map_honor_unit_t* a, map_honor_unit_t* b) 
{
    QM_LOGD("map", "[%s|%d] p_src %s %s\n", __func__, __LINE__, a->p.id, b->p.id);
    if (a->pid_len > HONOR_ID_MAX_LEN) {
        if (strcmp(a->p.pid, b->p.pid) == 0) {
            QM_LOGD("map", "[%s|%d] p_src %s %s\n", __func__, __LINE__, a->p.pid, b->p.pid);
            return 1;
        } 
    } else {
        if (strcmp(a->p.id, b->p.id) == 0) {
            QM_LOGD("map", "[%s|%d] p_src %s %s\n", __func__, __LINE__, a->p.id, b->p.id);
            return 1;
        }
    }
    return QM_EOK;
}
#endif