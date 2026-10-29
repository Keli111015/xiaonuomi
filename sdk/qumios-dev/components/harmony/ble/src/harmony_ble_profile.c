#include "qm.h"
#include "qm_log.h"

#include "harmony_ble_profile.h"

#define LOG_TAG "harmony comm"

harmony_ble_profile_t *harmony_ble_profile_creat(void)
{
    harmony_ble_profile_t *profile = NULL;
    profile = (harmony_ble_profile_t*)qm_malloc(sizeof(harmony_ble_profile_t));
    if(profile == NULL){
        return NULL;
    }
    memset(profile, 0, sizeof(harmony_ble_profile_t));
    return profile;
}

int harmony_ble_profile_delete(harmony_ble_profile_t *profile)
{
    harmony_ble_service_t *service = NULL;
    harmony_ble_service_t *tmp_service = NULL;

    harmony_ble_data_t *data = NULL;
    harmony_ble_data_t *tmp_data = NULL;

    QM_RETURN_ON_FALSE(profile, -QM_EINVAL, LOG_TAG, "profile inval");

    service = profile->service;
    while(service){
        data = service->data_param;
        while (data)
        {
            if(data->len > HARMONY_BLE_BUFF_SIZE){
                qm_free(data->value.pdata);
                data->value.pdata = NULL;
            }

            tmp_data = data;
            data = data->next;

            qm_free(tmp_data);
            tmp_data = NULL;
        }
        tmp_service = service;
        service = service->next;

        qm_free(tmp_service);
        tmp_service = NULL;
    }

    qm_free(profile);

    return QM_EOK;
}

harmony_ble_service_t *harmony_ble_service_creat(void)
{
    harmony_ble_service_t *service = NULL;
    service = (harmony_ble_service_t*)qm_malloc(sizeof(harmony_ble_service_t));
    if(service == NULL){
        return NULL;
    }
    memset(service, 0, sizeof(harmony_ble_service_t));
    return service;
}

int harmony_ble_service_delete(harmony_ble_service_t *service)
{
    harmony_ble_data_t *data_block = NULL;
    harmony_ble_data_t *tmp_block = NULL;

    QM_RETURN_ON_FALSE(service, -QM_EINVAL, LOG_TAG, "service inval");

    data_block = service->data_param;

    while(data_block){

        if(data_block->len > HARMONY_BLE_BUFF_SIZE){
            qm_free(data_block->value.pdata);
            data_block->value.pdata = NULL;
        }

        tmp_block = data_block;
        data_block = data_block->next;
        qm_free(tmp_block);
        tmp_block = NULL;
    }

    qm_free(service);

    return QM_EOK;
}

int harmony_ble_service_add(harmony_ble_profile_t *profile, harmony_ble_service_t *service)
{
    harmony_ble_service_t *m_service = NULL;
    harmony_ble_service_t *service_pre = NULL;

    QM_RETURN_ON_FALSE(profile, -QM_EINVAL, LOG_TAG, "profile inval");
    QM_RETURN_ON_FALSE(service, -QM_EINVAL, LOG_TAG, "service inval");

    if(profile->service == NULL){
        profile->service = service; 
        service->next = NULL;
        profile->count++;
        return QM_EOK;
    }

    m_service = profile->service;
    while(m_service){
        service_pre = m_service;
        m_service = service_pre->next;  
    }
    service_pre->next = service;
    service->next = NULL;
    profile->count++;

    return QM_EOK;
}   

harmony_ble_service_t *harmony_ble_service_find(harmony_ble_profile_t *profile, char *service_id)
{
    harmony_ble_service_t *m_service = NULL;
    QM_RETURN_ON_FALSE(profile, NULL, LOG_TAG, "profile inval");

    m_service = profile->service;
    while(m_service){
        if(!strncmp(m_service->sid, service_id, strlen(m_service->sid))){
            break;
        }
        m_service = m_service->next;
    }
    
    return m_service;
}

int harmony_ble_service_id_set(harmony_ble_service_t *service, char *id_name)
{
    QM_RETURN_ON_FALSE(id_name, -QM_EINVAL, LOG_TAG, "id_name inval");
    QM_RETURN_ON_FALSE(service, -QM_EINVAL, LOG_TAG, "service inval");

    if(strlen(id_name) > HARMONY_BLE_NAME_LEN){
        return -QM_EINVAL;
    }

    memcpy(service->sid, id_name, strlen(id_name));
    return QM_EOK;
}

char *harmony_ble_service_id_get(harmony_ble_service_t *service)
{
    QM_RETURN_ON_FALSE(service, NULL, LOG_TAG, "service inval");
    return service->sid;
}

harmony_ble_service_t *harmony_ble_service_next(harmony_ble_profile_t *profile, harmony_ble_service_t *service)
{
    QM_RETURN_ON_FALSE(profile, NULL, LOG_TAG, "service inval");

    if(service == NULL){
        return profile->service;
    }else{
        return service->next;
    }
}

harmony_ble_data_t *harmony_ble_data_next(harmony_ble_service_t *service, harmony_ble_data_t *data)
{
    QM_RETURN_ON_FALSE(service, NULL, LOG_TAG, "service inval");

    if(data == NULL){
        return service->data_param;
    }else{
        return data->next;
    }
}

harmony_ble_data_t *harmony_ble_data_creat(void)
{
    harmony_ble_data_t *data = NULL;
    data = (harmony_ble_data_t*)qm_malloc(sizeof(harmony_ble_data_t));
    if(data == NULL){
        return NULL;
    }
    memset(data, 0, sizeof(harmony_ble_data_t));
    return data;
}

int harmony_ble_data_delete(harmony_ble_data_t *data)
{
    QM_RETURN_ON_FALSE(data, -QM_EINVAL, LOG_TAG, "data inval");

    if(data->len > HARMONY_BLE_BUFF_SIZE){
        qm_free(data->value.pdata);
        data->value.pdata = NULL;
    }
    qm_free(data);
    return QM_EOK;
}

int harmony_ble_data_add(harmony_ble_service_t *service, harmony_ble_data_t *data)
{
    harmony_ble_data_t *m_data = NULL;
    harmony_ble_data_t *data_pre = NULL;

    QM_RETURN_ON_FALSE(service, -QM_EINVAL, LOG_TAG, "service inval");
    QM_RETURN_ON_FALSE(data, -QM_EINVAL, LOG_TAG, "data inval");

    if(service->data_param == NULL){
        service->data_param = data; 
        data->next = NULL;
        service->count++;
        return QM_EOK;
    }

    m_data = service->data_param;
    while(m_data){
        data_pre = m_data;
        m_data = m_data->next;  
    }

    data_pre->next = data;
    data->next = NULL;
    service->count++;

    return QM_EOK;
}

int harmony_ble_data_id_set(harmony_ble_data_t *data, char *id_name)
{
    QM_RETURN_ON_FALSE(data, -QM_EINVAL, LOG_TAG, "data inval");
    QM_RETURN_ON_FALSE(id_name, -QM_EINVAL, LOG_TAG, "id_name inval");

    if(strlen(id_name) > HARMONY_BLE_NAME_LEN){
        return -QM_EINVAL;
    }

    memcpy(data->data_id, id_name, strlen(id_name));
    return QM_EOK;
}

char *harmony_ble_data_id_get(harmony_ble_data_t *data)
{
    QM_RETURN_ON_FALSE(data, NULL, LOG_TAG, "service inval");
    return data->data_id;
}

harmony_ble_profile_data_type_t harmony_ble_data_type_get(harmony_ble_data_t *data)
{
    QM_RETURN_ON_FALSE(data, -QM_EINVAL, LOG_TAG, "data inval");
    return data->type;
}

int harmony_ble_data_pack_bool(harmony_ble_data_t *data, bool_t value)
{
    QM_RETURN_ON_FALSE(data, -QM_EINVAL, LOG_TAG, "data inval");

    if(data->len > HARMONY_BLE_BUFF_SIZE){
        if(data->value.pdata){
            qm_free(data->value.pdata);
            data->value.pdata = NULL;
        }
    }

    data->len = sizeof(bool_t);
    data->value.bytes[0] = value;
    data->type = HARMONY_BLE_PROFILE_DATA_TYPE_BOOL;
    return QM_EOK;
}

int harmony_ble_data_unpack_bool(harmony_ble_data_t *data, bool_t *value)
{
    QM_RETURN_ON_FALSE(data, -QM_EINVAL, LOG_TAG, "data inval");
    QM_RETURN_ON_FALSE(value, -QM_EINVAL, LOG_TAG, "value inval");

    *value = data->value.bytes[0];
    return QM_EOK;
}

int harmony_ble_data_pack_number(harmony_ble_data_t *data, int value)
{
    int tmp = 0;

    QM_RETURN_ON_FALSE(data, -QM_EINVAL, LOG_TAG, "data inval");

    if(data->len > HARMONY_BLE_BUFF_SIZE){
        if(data->value.pdata){
            qm_free(data->value.pdata);
            data->value.pdata = NULL;
        }
    }
    tmp = cpu_to_be32(value);
    memset(data->value.bytes, 0, HARMONY_BLE_BUFF_SIZE);
    memcpy(data->value.bytes, &tmp, sizeof(int));

    data->len = sizeof(int);
    data->type = HARMONY_BLE_PROFILE_DATA_TYPE_NUMBER;
    return QM_EOK;
}

int harmony_ble_data_unpack_number(harmony_ble_data_t *data, int *value)
{
    int tmp = 0;
    QM_RETURN_ON_FALSE(data, -QM_EINVAL, LOG_TAG, "data inval");
    QM_RETURN_ON_FALSE(value, -QM_EINVAL, LOG_TAG, "value inval");

    memcpy(&tmp, data->value.bytes, sizeof(int));
    *value = be32_to_cpu(tmp);
    return QM_EOK;
}

int harmony_ble_data_pack_string(harmony_ble_data_t *data, char *value)
{

    QM_RETURN_ON_FALSE(data, -QM_EINVAL, LOG_TAG, "data inval");
    QM_RETURN_ON_FALSE(value, -QM_EINVAL, LOG_TAG, "value inval");

    if(data->len > HARMONY_BLE_BUFF_SIZE){
        if(data->value.pdata){
            qm_free(data->value.pdata);
            data->value.pdata = NULL;
        }
    }

    if(strlen(value) < HARMONY_BLE_BUFF_SIZE){
        memset(data->value.bytes, 0, HARMONY_BLE_BUFF_SIZE);
        memcpy(data->value.bytes, value, strlen(value));
    }else{
        data->value.pdata = (uint8_t*)qm_malloc(strlen(value) + 1);
        if(data->value.pdata == NULL){
            return -QM_ENOMEM;   
        }
        memset(data->value.pdata, 0, strlen(value) + 1);
        memcpy(data->value.pdata, value, strlen(value));
    }

    data->len = strlen(value);
    data->type = HARMONY_BLE_PROFILE_DATA_TYPE_STRING;
    return QM_EOK;
}

int harmony_ble_data_unpack_string(harmony_ble_data_t *data, char *value)
{
    QM_RETURN_ON_FALSE(data, -QM_EINVAL, LOG_TAG, "data inval");
    QM_RETURN_ON_FALSE(value, -QM_EINVAL, LOG_TAG, "value inval");


    if(data->len < HARMONY_BLE_BUFF_SIZE){
        memcpy(value, data->value.bytes, data->len);
    }else{
        memcpy(value, data->value.pdata, data->len);
    }
    *(value + data->len) = '\0';
    return QM_EOK;
}
